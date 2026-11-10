#include "parsers/kernel_text.h"

#include <algorithm>

namespace readout {

CpuUsage computeCpuUsage(const CpuTimes& before, const CpuTimes& after) {
    CpuUsage usage;
    if (after.total() <= before.total()) {
        return usage;
    }
    const auto delta = [](std::uint64_t from, std::uint64_t to) -> double {
        return to >= from ? static_cast<double>(to - from) : 0.0;
    };
    const double total = static_cast<double>(after.total() - before.total());
    const double idle = delta(before.idleTotal(), after.idleTotal());
    const double user = delta(before.user, after.user) + delta(before.nice, after.nice);
    const double system = delta(before.system, after.system) + delta(before.irq, after.irq) +
                          delta(before.softIrq, after.softIrq) + delta(before.steal, after.steal);
    usage.total = std::clamp((total - idle) / total * 100.0, 0.0, 100.0);
    usage.user = std::clamp(user / total * 100.0, 0.0, 100.0);
    usage.system = std::clamp(system / total * 100.0, 0.0, 100.0);
    usage.ioWait = std::clamp(delta(before.ioWait, after.ioWait) / total * 100.0, 0.0, 100.0);
    return usage;
}

MemoryInfo parseMeminfo(std::string_view text) {
    MemoryInfo info;
    struct Field {
        std::string_view key;
        std::uint64_t* target;
    };
    const Field fields[] = {
        {"MemTotal", &info.total},          {"MemFree", &info.free},
        {"MemAvailable", &info.available},  {"Buffers", &info.buffers},
        {"Cached", &info.cached},           {"Shmem", &info.shared},
        {"SReclaimable", &info.reclaimableSlab}, {"SwapTotal", &info.swapTotal},
        {"SwapFree", &info.swapFree},       {"SwapCached", &info.swapCached},
        {"Dirty", &info.dirty},             {"AnonPages", &info.anonymous},
        {"Zswap", &info.zswap},             {"Zswapped", &info.zswapped},
    };
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view key;
        std::string_view value;
        if (!splitKeyValue(line, ':', key, value)) {
            continue;
        }
        for (const Field& field : fields) {
            if (field.key != key) {
                continue;
            }
            std::string_view rest = value;
            const auto number = parseNumber<std::uint64_t>(takeToken(rest));
            if (number) {
                const std::string_view unit = takeToken(rest);
                *field.target = unit == "kB" ? *number * 1024 : *number;
                if (field.target == &info.available) {
                    info.hasAvailable = true;
                }
            }
            break;
        }
    }
    if (!info.hasAvailable) {
        // Kernels older than 3.14: approximate.
        info.available = info.free + info.buffers + info.cached;
    }
    return info;
}

std::optional<LoadAverage> parseLoadAverage(std::string_view text) {
    LoadAverage load;
    const auto one = parseNumber<double>(takeToken(text));
    const auto five = parseNumber<double>(takeToken(text));
    const auto fifteen = parseNumber<double>(takeToken(text));
    if (!one || !five || !fifteen) {
        return std::nullopt;
    }
    load.oneMinute = *one;
    load.fiveMinutes = *five;
    load.fifteenMinutes = *fifteen;
    const std::string_view tasks = takeToken(text);
    const std::size_t slash = tasks.find('/');
    if (slash != std::string_view::npos) {
        load.runnable = parseNumber<std::uint32_t>(tasks.substr(0, slash)).value_or(0);
        load.total = parseNumber<std::uint32_t>(tasks.substr(slash + 1)).value_or(0);
    }
    return load;
}

std::optional<double> parseUptimeSeconds(std::string_view text) {
    return parseNumber<double>(takeToken(text));
}

std::optional<Pressure> parsePressure(std::string_view text) {
    Pressure pressure;
    bool hasSome = false;
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view rest = line;
        const std::string_view kind = takeToken(rest);
        PressureLine* target = nullptr;
        if (kind == "some") {
            target = &pressure.some;
            hasSome = true;
        } else if (kind == "full") {
            target = &pressure.full;
            pressure.hasFull = true;
        } else {
            continue;
        }
        for (std::string_view token = takeToken(rest); !token.empty(); token = takeToken(rest)) {
            std::string_view key;
            std::string_view value;
            if (!splitKeyValue(token, '=', key, value)) {
                continue;
            }
            if (key == "avg10") {
                target->average10 = parseNumber<double>(value).value_or(0);
            } else if (key == "avg60") {
                target->average60 = parseNumber<double>(value).value_or(0);
            } else if (key == "avg300") {
                target->average300 = parseNumber<double>(value).value_or(0);
            } else if (key == "total") {
                target->totalMicroseconds = parseNumber<std::uint64_t>(value).value_or(0);
            }
        }
    }
    if (!hasSome) {
        return std::nullopt;
    }
    return pressure;
}

std::string unescapeMountField(std::string_view field) {
    std::string result;
    result.reserve(field.size());
    for (std::size_t index = 0; index < field.size(); ++index) {
        const char character = field[index];
        if (character == '\\' && index + 3 < field.size()) {
            const char first = field[index + 1];
            const char second = field[index + 2];
            const char third = field[index + 3];
            const auto isOctal = [](char digit) { return digit >= '0' && digit <= '7'; };
            if (isOctal(first) && isOctal(second) && isOctal(third)) {
                result.push_back(static_cast<char>(((first - '0') << 6) | ((second - '0') << 3) | (third - '0')));
                index += 3;
                continue;
            }
        }
        result.push_back(character);
    }
    return result;
}

std::optional<ProcessStat> parseProcessStat(std::string_view text) {
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return std::nullopt;
    }
    ProcessStat stat;
    stat.command = text.substr(open + 1, close - open - 1);
    std::string_view rest = text.substr(close + 1);
    // Field indexes relative to the state field (field 3 in proc(5)).
    constexpr int userIndex = 11;
    constexpr int systemIndex = 12;
    constexpr int startTimeIndex = 19;
    constexpr int residentIndex = 21;
    for (int index = 0; index <= residentIndex; ++index) {
        const std::string_view token = takeToken(rest);
        if (token.empty()) {
            return std::nullopt;
        }
        switch (index) {
            case 0:
                stat.state = token.front();
                break;
            case userIndex:
                stat.userTicks = parseNumber<std::uint64_t>(token).value_or(0);
                break;
            case systemIndex:
                stat.systemTicks = parseNumber<std::uint64_t>(token).value_or(0);
                break;
            case startTimeIndex:
                stat.startTimeTicks = parseNumber<std::uint64_t>(token).value_or(0);
                break;
            case residentIndex:
                stat.residentPages = parseNumber<std::int64_t>(token).value_or(0);
                break;
            default:
                break;
        }
    }
    return stat;
}

std::optional<ProcessIo> parseProcessIo(std::string_view text) {
    ProcessIo io;
    bool hasRead = false;
    bool hasWrite = false;
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view key;
        std::string_view value;
        if (!splitKeyValue(line, ':', key, value)) {
            continue;
        }
        if (key == "read_bytes") {
            io.readBytes = parseNumber<std::uint64_t>(value).value_or(0);
            hasRead = true;
        } else if (key == "write_bytes") {
            io.writeBytes = parseNumber<std::uint64_t>(value).value_or(0);
            hasWrite = true;
        }
    }
    if (!hasRead || !hasWrite) {
        return std::nullopt;
    }
    return io;
}

std::string_view parseCpuModel(std::string_view cpuinfo) {
    std::string_view fallback;
    std::string_view line;
    while (takeLine(cpuinfo, line)) {
        std::string_view key;
        std::string_view value;
        if (!splitKeyValue(line, ':', key, value) || value.empty()) {
            continue;
        }
        if (key == "model name") {
            return value;
        }
        if (fallback.empty() && (key == "Model" || key == "Hardware" || key == "cpu model")) {
            fallback = value;
        }
    }
    return fallback;
}

std::optional<ZramStat> parseZramMmStat(std::string_view text) {
    ZramStat stat;
    const auto original = parseNumber<std::uint64_t>(takeToken(text));
    const auto compressed = parseNumber<std::uint64_t>(takeToken(text));
    const auto used = parseNumber<std::uint64_t>(takeToken(text));
    if (!original || !compressed || !used) {
        return std::nullopt;
    }
    stat.originalBytes = *original;
    stat.compressedBytes = *compressed;
    stat.memoryUsedBytes = *used;
    return stat;
}

PciName lookupPciName(std::string_view pciIds, std::uint32_t vendorId, std::uint32_t deviceId) {
    PciName name;
    bool inVendor = false;
    std::string_view line;
    while (takeLine(pciIds, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() != '\t') {
            if (inVendor) {
                break; // past our vendor block
            }
            if (line.starts_with("C ")) {
                break; // device classes section
            }
            const auto id = parseHex(line.substr(0, 4));
            if (id && *id == vendorId && line.size() > 6) {
                name.vendor = std::string(trim(line.substr(4)));
                inVendor = true;
            }
            continue;
        }
        if (!inVendor || line.size() < 2 || line[1] == '\t') {
            continue; // subsystem lines
        }
        const std::string_view entry = line.substr(1);
        const auto id = parseHex(entry.substr(0, 4));
        if (id && *id == deviceId && entry.size() > 6) {
            name.device = std::string(trim(entry.substr(4)));
            break;
        }
    }
    return name;
}

std::optional<std::string_view> findKeyValue(std::string_view text, std::string_view wantedKey, char separator) {
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view key;
        std::string_view value;
        if (splitKeyValue(line, separator, key, value) && key == wantedKey) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace readout

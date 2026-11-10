// Template parser implementations. Included from kernel_text.h only.
#pragma once

#include "common/text.h"

namespace readout {

template <typename Callback>
void parseProcStat(std::string_view text, Callback&& onLine) {
    std::string_view line;
    while (takeLine(text, line)) {
        if (!line.starts_with("cpu")) {
            continue;
        }
        std::string_view rest = line;
        const std::string_view label = takeToken(rest);
        int index = -1;
        if (label.size() > 3) {
            const auto parsed = parseNumber<int>(label.substr(3));
            if (!parsed) {
                continue;
            }
            index = *parsed;
        }
        CpuTimes times;
        std::uint64_t* const fields[] = {&times.user,   &times.nice, &times.system,  &times.idle,
                                         &times.ioWait, &times.irq,  &times.softIrq, &times.steal};
        bool valid = true;
        for (std::uint64_t* field : fields) {
            const std::string_view token = takeToken(rest);
            if (token.empty()) {
                break; // older kernels have fewer columns; missing ones stay 0
            }
            const auto value = parseNumber<std::uint64_t>(token);
            if (!value) {
                valid = false;
                break;
            }
            *field = *value;
        }
        if (valid) {
            onLine(index, times);
        }
    }
}

template <typename Callback>
void parseNetDev(std::string_view text, Callback&& onInterface) {
    std::string_view line;
    while (takeLine(text, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue; // header lines
        }
        const std::string_view name = trim(line.substr(0, colon));
        if (name.empty() || name.contains('|')) {
            continue;
        }
        std::string_view rest = line.substr(colon + 1);
        std::uint64_t values[16] = {};
        int count = 0;
        for (; count < 16; ++count) {
            const std::string_view token = takeToken(rest);
            if (token.empty()) {
                break;
            }
            const auto value = parseNumber<std::uint64_t>(token);
            if (!value) {
                break;
            }
            values[count] = *value;
        }
        if (count < 12) {
            continue;
        }
        InterfaceCounters counters;
        counters.receivedBytes = values[0];
        counters.receivedPackets = values[1];
        counters.receiveErrors = values[2];
        counters.receiveDrops = values[3];
        counters.sentBytes = values[8];
        counters.sentPackets = values[9];
        counters.sendErrors = values[10];
        counters.sendDrops = values[11];
        onInterface(name, counters);
    }
}

template <typename Callback>
void parseDiskStats(std::string_view text, Callback&& onDisk) {
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view rest = line;
        const auto major = parseNumber<std::uint32_t>(takeToken(rest));
        const auto minor = parseNumber<std::uint32_t>(takeToken(rest));
        const std::string_view name = takeToken(rest);
        if (!major || !minor || name.empty()) {
            continue;
        }
        std::uint64_t values[10] = {};
        int count = 0;
        for (; count < 10; ++count) {
            const auto value = parseNumber<std::uint64_t>(takeToken(rest));
            if (!value) {
                break;
            }
            values[count] = *value;
        }
        if (count < 10) {
            continue;
        }
        DiskCounters counters;
        counters.major = *major;
        counters.minor = *minor;
        counters.readsCompleted = values[0];
        counters.sectorsRead = values[2];
        counters.writesCompleted = values[4];
        counters.sectorsWritten = values[6];
        counters.ioMilliseconds = values[9];
        onDisk(name, counters);
    }
}

template <typename Callback>
void parseMountInfo(std::string_view text, Callback&& onMount) {
    std::string_view line;
    while (takeLine(text, line)) {
        std::string_view rest = line;
        takeToken(rest); // mount id
        takeToken(rest); // parent id
        const std::string_view device = takeToken(rest);
        const std::string_view root = takeToken(rest);
        const std::string_view mountPoint = takeToken(rest);
        const std::size_t colon = device.find(':');
        if (colon == std::string_view::npos || mountPoint.empty()) {
            continue;
        }
        const auto major = parseNumber<std::uint32_t>(device.substr(0, colon));
        const auto minor = parseNumber<std::uint32_t>(device.substr(colon + 1));
        if (!major || !minor) {
            continue;
        }
        // Skip mount options and optional fields up to the "-" separator.
        std::string_view token;
        do {
            token = takeToken(rest);
        } while (!token.empty() && token != "-");
        if (token.empty()) {
            continue;
        }
        MountEntry entry;
        entry.major = *major;
        entry.minor = *minor;
        entry.root = root;
        entry.mountPoint = mountPoint;
        entry.fsType = takeToken(rest);
        entry.source = takeToken(rest);
        onMount(entry);
    }
}

template <typename Callback>
bool parseCpuList(std::string_view text, Callback&& onCpu) {
    text = trim(text);
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view part = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        const std::size_t dash = part.find('-');
        const auto first = parseNumber<int>(part.substr(0, dash));
        if (!first) {
            return false;
        }
        int last = *first;
        if (dash != std::string_view::npos) {
            const auto parsedLast = parseNumber<int>(part.substr(dash + 1));
            if (!parsedLast || *parsedLast < *first) {
                return false;
            }
            last = *parsedLast;
        }
        for (int cpu = *first; cpu <= last; ++cpu) {
            onCpu(cpu);
        }
    }
    return true;
}

} // namespace readout

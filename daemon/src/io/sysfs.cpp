#include "io/sysfs.h"

#include "common/text.h"
#include "io/held_file.h"

#include <dirent.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <memory>

namespace readout {

std::vector<std::string> listDirectory(const std::string& path) {
    std::vector<std::string> names;
    const std::unique_ptr<DIR, int (*)(DIR*)> directory(::opendir(path.c_str()), &::closedir);
    if (!directory) {
        return names;
    }
    while (const dirent* entry = ::readdir(directory.get())) {
        const std::string_view name(entry->d_name);
        if (name != "." && name != "..") {
            names.emplace_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string resolvePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved) == nullptr) {
        return {};
    }
    return resolved;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

namespace {

bool parseInputName(const std::string& file, std::string& kind, std::string& attribute, std::string& suffix) {
    static constexpr std::string_view kinds[] = {"temp", "in", "fan", "power", "curr", "energy"};
    for (const std::string_view candidate : kinds) {
        if (!file.starts_with(candidate)) {
            continue;
        }
        std::size_t position = candidate.size();
        const std::size_t digitsStart = position;
        while (position < file.size() && file[position] >= '0' && file[position] <= '9') {
            ++position;
        }
        if (position == digitsStart || position >= file.size() || file[position] != '_') {
            return false;
        }
        kind = std::string(candidate);
        attribute = file.substr(0, position);
        suffix = file.substr(position + 1);
        return true;
    }
    return false;
}

} // namespace

std::vector<HwmonChip> listHwmonChips(const SystemPaths& paths) {
    std::vector<HwmonChip> chips;
    const std::string root = paths.sys + "/class/hwmon";
    for (const std::string& entry : listDirectory(root)) {
        HwmonChip chip;
        chip.directory = root + "/" + entry;
        const auto name = readTextFile(chip.directory + "/name");
        chip.name = name ? std::string(trim(*name)) : entry;
        chip.devicePath = resolvePath(chip.directory + "/device");
        chip.stableId = chip.name;
        if (!chip.devicePath.empty()) {
            chip.stableId += "@" + baseName(chip.devicePath);
        }
        // Some drivers put attributes in device/ (very old kernels); ignore those.
        const std::vector<std::string> files = listDirectory(chip.directory);
        for (const std::string& file : files) {
            std::string kind;
            std::string attribute;
            std::string suffix;
            if (!parseInputName(file, kind, attribute, suffix)) {
                continue;
            }
            const bool isInput = suffix == "input";
            // Power sensors that only report an average (amdgpu) have no _input file.
            const bool isPowerAverage = kind == "power" && suffix == "average" &&
                                        !std::binary_search(files.begin(), files.end(), attribute + "_input");
            if (!isInput && !isPowerAverage) {
                continue;
            }
            HwmonInput input;
            input.attribute = attribute;
            input.kind = kind;
            input.inputFile = file;
            if (std::binary_search(files.begin(), files.end(), attribute + "_label")) {
                const auto label = readTextFile(chip.directory + "/" + attribute + "_label");
                if (label) {
                    input.label = std::string(trim(*label));
                }
            }
            chip.inputs.push_back(std::move(input));
        }
        // Natural order: temp2 before temp10, grouped by kind.
        const auto index = [](const HwmonInput& input) {
            return parseNumber<int>(std::string_view(input.attribute).substr(input.kind.size())).value_or(0);
        };
        std::stable_sort(chip.inputs.begin(), chip.inputs.end(), [&index](const HwmonInput& left, const HwmonInput& right) {
            if (left.kind != right.kind) {
                return left.kind < right.kind;
            }
            return index(left) < index(right);
        });
        chips.push_back(std::move(chip));
    }
    // Deduplicate stable ids (two chips with the same name on the same device).
    for (std::size_t index = 0; index < chips.size(); ++index) {
        int duplicate = 1;
        for (std::size_t other = 0; other < index; ++other) {
            if (chips[other].stableId == chips[index].stableId ||
                chips[other].stableId.starts_with(chips[index].stableId + "#")) {
                ++duplicate;
            }
        }
        if (duplicate > 1) {
            chips[index].stableId += "#" + std::to_string(duplicate);
        }
    }
    return chips;
}

bool isPciDisplayDevice(const std::string& devicePath) {
    const auto pciClass = readTextFile(devicePath + "/class");
    if (!pciClass) {
        return false;
    }
    const auto value = parseHex(*pciClass);
    return value && (*value >> 16U) == 0x03U;
}

bool isSecondaryGpu(const SystemPaths& paths, const std::string& devicePath) {
    if (devicePath.empty() || !isPciDisplayDevice(devicePath)) {
        return false;
    }
    if (readIntegerFile(devicePath + "/boot_vga").value_or(0) == 1) {
        return false;
    }
    // Only secondary if some other display device is the boot device.
    const std::string drm = paths.sys + "/class/drm";
    for (const std::string& entry : listDirectory(drm)) {
        if (!entry.starts_with("card") || entry.contains('-')) {
            continue;
        }
        const std::string other = resolvePath(drm + "/" + entry + "/device");
        if (!other.empty() && other != devicePath && readIntegerFile(other + "/boot_vga").value_or(0) == 1) {
            return true;
        }
    }
    return false;
}

} // namespace readout

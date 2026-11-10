// Discovery helpers for /sys. Used at startup and on hotplug, never per sample.
#pragma once

#include "common/paths.h"

#include <cstdint>
#include <string>
#include <vector>

namespace readout {

// Names of the entries in a directory (without "." and ".."). Empty if it cannot be read.
std::vector<std::string> listDirectory(const std::string& path);

// realpath(); empty on failure.
std::string resolvePath(const std::string& path);
std::string baseName(const std::string& path);

struct HwmonInput {
    std::string attribute; // "temp1"
    std::string kind;      // "temp", "in", "fan", "power", "curr", "energy"
    std::string inputFile; // "temp1_input" (or "power1_average")
    std::string label;     // contents of temp1_label, may be empty
};

struct HwmonChip {
    std::string directory;  // /sys/class/hwmon/hwmon4
    std::string name;       // contents of "name"
    std::string devicePath; // resolved "device" link, empty for virtual chips
    std::string stableId;   // name@device-basename, stable across reboots
    std::vector<HwmonInput> inputs;
};

std::vector<HwmonChip> listHwmonChips(const SystemPaths& paths);

// PCI helpers. `devicePath` is a resolved /sys/devices/... path.
bool isPciDisplayDevice(const std::string& devicePath);
// True for a display device that is not the boot VGA device while another one is.
// Reading such a device (sysfs or hwmon) resets its runtime-PM timer and keeps it awake.
bool isSecondaryGpu(const SystemPaths& paths, const std::string& devicePath);

} // namespace readout

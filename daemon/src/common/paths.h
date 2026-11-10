// Roots of the kernel interfaces. Tests point these at fixture trees.
#pragma once

#include <string>

namespace readout {

struct SystemPaths {
    std::string proc = "/proc";
    std::string sys = "/sys";
    // pci.ids database used to name GPUs. The first existing file wins.
    std::string pciIds = "/usr/share/hwdata/pci.ids";
    std::string pciIdsFallback = "/usr/share/misc/pci.ids";
};

} // namespace readout

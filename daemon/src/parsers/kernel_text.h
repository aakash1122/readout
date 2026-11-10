// Pure parsers for /proc and /sys text formats.
// They take a string_view, never do I/O, and never allocate (except where noted).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace readout {

// ---- /proc/stat --------------------------------------------------------------------------

struct CpuTimes {
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t ioWait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softIrq = 0;
    std::uint64_t steal = 0;

    [[nodiscard]] std::uint64_t idleTotal() const { return idle + ioWait; }
    [[nodiscard]] std::uint64_t total() const { return user + nice + system + idle + ioWait + irq + softIrq + steal; }
};

struct CpuUsage {
    double total = 0;  // percent, 0..100
    double user = 0;   // user + nice
    double system = 0; // system + irq + softirq + steal
    double ioWait = 0;
};

// Calls `onLine(cpuIndex, times)` for each "cpuN" line and `onLine(-1, times)` for the aggregate line.
template <typename Callback>
void parseProcStat(std::string_view text, Callback&& onLine);

// Usage between two samples. Returns zero usage if the counters did not advance or went backwards.
CpuUsage computeCpuUsage(const CpuTimes& before, const CpuTimes& after);

// ---- /proc/meminfo -----------------------------------------------------------------------

struct MemoryInfo {
    std::uint64_t total = 0;
    std::uint64_t free = 0;
    std::uint64_t available = 0;
    std::uint64_t buffers = 0;
    std::uint64_t cached = 0;
    std::uint64_t shared = 0;
    std::uint64_t reclaimableSlab = 0;
    std::uint64_t swapTotal = 0;
    std::uint64_t swapFree = 0;
    std::uint64_t swapCached = 0;
    std::uint64_t dirty = 0;
    std::uint64_t anonymous = 0;
    std::uint64_t zswap = 0;    // compressed size held by zswap
    std::uint64_t zswapped = 0; // uncompressed size of pages in zswap
    bool hasAvailable = false;
};

// Values are converted to bytes.
MemoryInfo parseMeminfo(std::string_view text);

// ---- /proc/loadavg, /proc/uptime, /proc/pressure/* ---------------------------------------

struct LoadAverage {
    double oneMinute = 0;
    double fiveMinutes = 0;
    double fifteenMinutes = 0;
    std::uint32_t runnable = 0;
    std::uint32_t total = 0;
};
std::optional<LoadAverage> parseLoadAverage(std::string_view text);

std::optional<double> parseUptimeSeconds(std::string_view text);

struct PressureLine {
    double average10 = 0;
    double average60 = 0;
    double average300 = 0;
    std::uint64_t totalMicroseconds = 0;
};
struct Pressure {
    PressureLine some;
    PressureLine full;
    bool hasFull = false;
};
std::optional<Pressure> parsePressure(std::string_view text);

// ---- /proc/net/dev -----------------------------------------------------------------------

struct InterfaceCounters {
    std::uint64_t receivedBytes = 0;
    std::uint64_t receivedPackets = 0;
    std::uint64_t receiveErrors = 0;
    std::uint64_t receiveDrops = 0;
    std::uint64_t sentBytes = 0;
    std::uint64_t sentPackets = 0;
    std::uint64_t sendErrors = 0;
    std::uint64_t sendDrops = 0;
};

// Calls `onInterface(name, counters)` for each interface.
template <typename Callback>
void parseNetDev(std::string_view text, Callback&& onInterface);

// ---- /proc/diskstats ---------------------------------------------------------------------

struct DiskCounters {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint64_t readsCompleted = 0;
    std::uint64_t sectorsRead = 0;
    std::uint64_t writesCompleted = 0;
    std::uint64_t sectorsWritten = 0;
    std::uint64_t ioMilliseconds = 0; // time spent doing I/O (busy time)
};

template <typename Callback>
void parseDiskStats(std::string_view text, Callback&& onDisk);

// ---- /proc/self/mountinfo ----------------------------------------------------------------

struct MountEntry {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::string_view root;        // root of the mount within the filesystem (escaped)
    std::string_view mountPoint;  // escaped as in the kernel output (\040 for space)
    std::string_view fsType;
    std::string_view source;
};

template <typename Callback>
void parseMountInfo(std::string_view text, Callback&& onMount);

// Decodes the octal escapes (\040, \011, \012, \134) used in mountinfo. Allocates.
std::string unescapeMountField(std::string_view field);

// ---- /proc/<pid>/stat, /proc/<pid>/io ----------------------------------------------------

struct ProcessStat {
    std::string_view command; // the comm field, without parentheses
    char state = '?';
    std::uint64_t userTicks = 0;
    std::uint64_t systemTicks = 0;
    std::uint64_t startTimeTicks = 0;
    std::int64_t residentPages = 0;
};
std::optional<ProcessStat> parseProcessStat(std::string_view text);

struct ProcessIo {
    std::uint64_t readBytes = 0;
    std::uint64_t writeBytes = 0;
};
std::optional<ProcessIo> parseProcessIo(std::string_view text);

// ---- Miscellaneous -----------------------------------------------------------------------

// First "model name" (x86) or "Model"/"Hardware" (ARM) value in /proc/cpuinfo.
std::string_view parseCpuModel(std::string_view cpuinfo);

// Parses a kernel CPU list such as "0-3,8,10-11". Calls `onCpu(index)` for each CPU.
// Returns false on malformed input.
template <typename Callback>
bool parseCpuList(std::string_view text, Callback&& onCpu);

// /sys/block/zramN/mm_stat: original size, compressed size, memory used (bytes).
struct ZramStat {
    std::uint64_t originalBytes = 0;
    std::uint64_t compressedBytes = 0;
    std::uint64_t memoryUsedBytes = 0;
};
std::optional<ZramStat> parseZramMmStat(std::string_view text);

// Looks up "vendor device" names in the pci.ids database.
struct PciName {
    std::string vendor;
    std::string device;
};
PciName lookupPciName(std::string_view pciIds, std::uint32_t vendorId, std::uint32_t deviceId);

// Reads the value of a "key value" line from a DRM fdinfo or similar file. Unused key -> nullopt.
std::optional<std::string_view> findKeyValue(std::string_view text, std::string_view key, char separator);

} // namespace readout

#include "parsers/kernel_text_inline.h"

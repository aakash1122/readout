// Shared per-process data, collected at most once per tick and only while a popup asks for it.
//
// Each process keeps its /proc/<pid>/stat descriptor open between ticks, so a refresh costs
// one getdents64() for the PID list plus one pread() per process.
#pragma once

#include "common/paths.h"
#include "io/held_file.h"

#include <cstdint>
#include <span>
#include <vector>

namespace readout {

struct ProcessEntry {
    int pid = 0;
    std::uint64_t startTime = 0;
    char command[16] = {};      // comm, sanitized UTF-8
    char displayName[64] = {};  // resolved lazily for top lists
    bool displayNameResolved = false;

    std::uint64_t cpuTicks = 0;
    double cpuPercent = 0;      // percent of one core
    std::uint64_t residentBytes = 0;

    bool ioDenied = false;
    bool ioKnown = false;
    std::uint64_t readBytes = 0;
    std::uint64_t writeBytes = 0;
    double readRate = 0;  // bytes per second
    double writeRate = 0;

    HeldFile statFile;
    HeldFile ioFile;
    bool seen = false; // has a previous sample for rate computation
};

class ProcessTable {
public:
    explicit ProcessTable(const SystemPaths& paths);
    ~ProcessTable();
    ProcessTable(const ProcessTable&) = delete;
    ProcessTable& operator=(const ProcessTable&) = delete;
    ProcessTable(ProcessTable&&) = delete;
    ProcessTable& operator=(ProcessTable&&) = delete;

    void refresh(std::uint64_t nowMicroseconds, bool collectIo);
    // Drops all state (and open descriptors). Used when no module needs processes.
    void clear();
    void resetBaselines();

    [[nodiscard]] std::span<const ProcessEntry> entries() const { return entries_; }

    // Fills `output` with indexes of the top `count` entries by `key`, highest first.
    enum class SortKey : std::uint8_t { Cpu, Memory, DiskIo };
    void top(SortKey key, std::size_t count, std::vector<std::uint32_t>& output) const;

    // Returns a friendly name (cmdline basename when comm was truncated). Cached per process.
    const char* displayName(std::uint32_t index) const;

private:
    void listPids();
    void readProcess(ProcessEntry& entry, double elapsedSeconds, bool collectIo);
    void resolveDisplayName(ProcessEntry& entry) const;

    SystemPaths paths_;
    int procDirectory_ = -1;
    long clockTicksPerSecond_ = 100;
    long pageSize_ = 4096;
    std::uint64_t lastRefresh_ = 0;

    std::vector<int> pids_;
    std::vector<char> directoryBuffer_;
    std::vector<char> readBuffer_;
    mutable std::vector<ProcessEntry> entries_;
    std::vector<ProcessEntry> nextEntries_;
};

} // namespace readout

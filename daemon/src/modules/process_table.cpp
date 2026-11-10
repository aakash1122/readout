#include "modules/process_table.h"

#include "common/text.h"
#include "parsers/kernel_text.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>

namespace readout {

namespace {

constexpr std::size_t directoryBufferSize = std::size_t{32} * 1024;

bool isAllDigits(const char* name) {
    if (*name == '\0') {
        return false;
    }
    for (; *name != '\0'; ++name) {
        if (*name < '0' || *name > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

ProcessTable::ProcessTable(const SystemPaths& paths) : paths_(paths) {
    clockTicksPerSecond_ = std::max(1L, ::sysconf(_SC_CLK_TCK));
    pageSize_ = std::max(1L, ::sysconf(_SC_PAGESIZE));
}

ProcessTable::~ProcessTable() {
    clear();
}

void ProcessTable::clear() {
    entries_.clear();
    entries_.shrink_to_fit();
    nextEntries_.clear();
    nextEntries_.shrink_to_fit();
    pids_.clear();
    pids_.shrink_to_fit();
    directoryBuffer_.clear();
    directoryBuffer_.shrink_to_fit();
    if (procDirectory_ >= 0) {
        ::close(procDirectory_);
        procDirectory_ = -1;
    }
    lastRefresh_ = 0;
}

void ProcessTable::resetBaselines() {
    for (ProcessEntry& entry : entries_) {
        entry.seen = false;
        entry.cpuPercent = 0;
        entry.readRate = 0;
        entry.writeRate = 0;
    }
    lastRefresh_ = 0;
}

void ProcessTable::listPids() {
    pids_.clear();
    if (procDirectory_ < 0) {
        procDirectory_ = ::open(paths_.proc.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (procDirectory_ < 0) {
            return;
        }
    }
    if (directoryBuffer_.size() < directoryBufferSize) {
        directoryBuffer_.resize(directoryBufferSize);
    }
    if (::lseek(procDirectory_, 0, SEEK_SET) < 0) {
        return;
    }
    for (;;) {
        const ssize_t count = ::getdents64(procDirectory_, directoryBuffer_.data(), directoryBuffer_.size());
        if (count <= 0) {
            break;
        }
        ssize_t offset = 0;
        while (offset < count) {
            const auto* entry = reinterpret_cast<const dirent64*>(directoryBuffer_.data() + offset);
            offset += entry->d_reclen;
            if ((entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) && isAllDigits(entry->d_name)) {
                const auto pid = parseNumber<int>(entry->d_name);
                if (pid && *pid > 0) {
                    pids_.push_back(*pid);
                }
            }
        }
    }
    std::sort(pids_.begin(), pids_.end());
}

void ProcessTable::refresh(std::uint64_t nowMicroseconds, bool collectIo) {
    const double elapsedSeconds =
        lastRefresh_ != 0 && nowMicroseconds > lastRefresh_ ? static_cast<double>(nowMicroseconds - lastRefresh_) / 1e6
                                                            : 0.0;
    lastRefresh_ = nowMicroseconds;
    listPids();

    nextEntries_.clear();
    nextEntries_.reserve(pids_.size());
    auto previous = entries_.begin();
    for (const int pid : pids_) {
        while (previous != entries_.end() && previous->pid < pid) {
            ++previous;
        }
        if (previous != entries_.end() && previous->pid == pid) {
            nextEntries_.push_back(std::move(*previous));
            ++previous;
        } else {
            ProcessEntry& created = nextEntries_.emplace_back();
            created.pid = pid;
        }
        ProcessEntry& entry = nextEntries_.back();
        readProcess(entry, elapsedSeconds, collectIo);
        if (entry.pid == 0) {
            nextEntries_.pop_back(); // exited or unreadable
        }
    }
    entries_.swap(nextEntries_);
    nextEntries_.clear(); // closes descriptors of exited processes
}

void ProcessTable::readProcess(ProcessEntry& entry, double elapsedSeconds, bool collectIo) {
    if (!entry.statFile.isOpen()) {
        std::array<char, 32> path{};
        std::snprintf(path.data(), path.size(), "%d/stat", entry.pid);
        if (!entry.statFile.openAt(procDirectory_, path.data())) {
            entry.pid = 0;
            return;
        }
    }
    const auto text = entry.statFile.readAll(readBuffer_);
    const auto stat = text ? parseProcessStat(*text) : std::nullopt;
    if (!stat || stat->state == 'Z') {
        entry.pid = 0;
        return;
    }
    if (entry.seen && stat->startTimeTicks != entry.startTime) {
        // PID reused by a new process.
        const int pid = entry.pid;
        HeldFile statFile = std::move(entry.statFile);
        entry = ProcessEntry{};
        entry.pid = pid;
        entry.statFile = std::move(statFile);
    }
    if (!entry.seen) {
        entry.startTime = stat->startTimeTicks;
        copySanitizedUtf8(stat->command, entry.command, sizeof(entry.command));
    }

    const std::uint64_t ticks = stat->userTicks + stat->systemTicks;
    if (entry.seen && elapsedSeconds > 0 && ticks >= entry.cpuTicks) {
        entry.cpuPercent = static_cast<double>(ticks - entry.cpuTicks) * 100.0 /
                           (elapsedSeconds * static_cast<double>(clockTicksPerSecond_));
    } else {
        entry.cpuPercent = 0;
    }
    entry.cpuTicks = ticks;
    entry.residentBytes = stat->residentPages > 0 ? static_cast<std::uint64_t>(stat->residentPages) *
                                                        static_cast<std::uint64_t>(pageSize_)
                                                  : 0;

    if (collectIo && !entry.ioDenied) {
        if (!entry.ioFile.isOpen()) {
            std::array<char, 32> path{};
            std::snprintf(path.data(), path.size(), "%d/io", entry.pid);
            if (!entry.ioFile.openAt(procDirectory_, path.data())) {
                entry.ioDenied = true;
            }
        }
        const auto ioText = entry.ioFile.isOpen() ? entry.ioFile.readAll(readBuffer_) : std::nullopt;
        const auto io = ioText ? parseProcessIo(*ioText) : std::nullopt;
        if (io) {
            if (entry.ioKnown && elapsedSeconds > 0) {
                const auto rate = [elapsedSeconds](std::uint64_t before, std::uint64_t after) {
                    return after >= before ? static_cast<double>(after - before) / elapsedSeconds : 0.0;
                };
                entry.readRate = rate(entry.readBytes, io->readBytes);
                entry.writeRate = rate(entry.writeBytes, io->writeBytes);
            }
            entry.readBytes = io->readBytes;
            entry.writeBytes = io->writeBytes;
            entry.ioKnown = true;
        } else if (entry.ioFile.isOpen()) {
            entry.ioDenied = true;
            entry.ioFile.close();
        }
    } else if (!collectIo && entry.ioFile.isOpen()) {
        entry.ioFile.close();
        entry.ioKnown = false;
        entry.readRate = 0;
        entry.writeRate = 0;
    }
    entry.seen = true;
}

void ProcessTable::top(SortKey key, std::size_t count, std::vector<std::uint32_t>& output) const {
    output.clear();
    for (std::uint32_t index = 0; index < entries_.size(); ++index) {
        output.push_back(index);
    }
    const auto value = [this, key](std::uint32_t index) -> double {
        const ProcessEntry& entry = entries_[index];
        switch (key) {
            case SortKey::Cpu:
                return entry.cpuPercent;
            case SortKey::Memory:
                return static_cast<double>(entry.residentBytes);
            case SortKey::DiskIo:
                return entry.readRate + entry.writeRate;
        }
        return 0;
    };
    const std::size_t limit = std::min(count, output.size());
    std::partial_sort(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(limit), output.end(),
                      [&value](std::uint32_t left, std::uint32_t right) { return value(left) > value(right); });
    output.resize(limit);
}

const char* ProcessTable::displayName(std::uint32_t index) const {
    ProcessEntry& entry = entries_[index];
    if (!entry.displayNameResolved) {
        resolveDisplayName(entry);
        entry.displayNameResolved = true;
    }
    return entry.displayName[0] != '\0' ? entry.displayName : entry.command;
}

void ProcessTable::resolveDisplayName(ProcessEntry& entry) const {
    // comm is truncated to 15 bytes. Only then is the cmdline worth reading.
    if (std::strlen(entry.command) < 15 || procDirectory_ < 0) {
        return;
    }
    std::array<char, 32> path{};
    std::snprintf(path.data(), path.size(), "%d/cmdline", entry.pid);
    const int fd = ::openat(procDirectory_, path.data(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    std::array<char, 512> buffer{};
    const ssize_t count = ::read(fd, buffer.data(), buffer.size() - 1);
    ::close(fd);
    if (count <= 0) {
        return;
    }
    std::string_view argumentZero(buffer.data(), std::strlen(buffer.data()));
    // Some programs rewrite argv into one space-separated string.
    if (const std::size_t space = argumentZero.find(' '); space != std::string_view::npos) {
        argumentZero = argumentZero.substr(0, space);
    }
    if (const std::size_t slash = argumentZero.rfind('/'); slash != std::string_view::npos) {
        argumentZero.remove_prefix(slash + 1);
    }
    if (!argumentZero.empty()) {
        copySanitizedUtf8(argumentZero, entry.displayName, sizeof(entry.displayName));
    }
}

} // namespace readout

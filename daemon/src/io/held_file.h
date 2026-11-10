// A file descriptor that stays open and is re-read from offset 0 with pread().
// Keeping /proc and /sys files open avoids an open() and close() per sample.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace readout {

class HeldFile {
public:
    HeldFile() = default;
    ~HeldFile();

    HeldFile(const HeldFile&) = delete;
    HeldFile& operator=(const HeldFile&) = delete;
    HeldFile(HeldFile&& other) noexcept;
    HeldFile& operator=(HeldFile&& other) noexcept;

    // Opens `path` read-only. Returns false (and stays closed) on failure.
    bool open(const std::string& path);
    // Opens `relativePath` relative to the directory descriptor `directoryFd`.
    bool openAt(int directoryFd, const char* relativePath);
    void close();

    [[nodiscard]] bool isOpen() const { return fd_ >= 0; }
    [[nodiscard]] int lastError() const { return lastError_; }

    // Reads the whole file into `buffer`, growing it if needed.
    // The returned view points into `buffer`.
    std::optional<std::string_view> readAll(std::vector<char>& buffer);

    // Reads a small file (at most 127 bytes are used) and parses it as an integer.
    std::optional<std::int64_t> readInteger();
    // Reads a small file into `output` (at most `outputSize - 1` bytes, trimmed, NUL-terminated).
    bool readSmallText(char* output, std::size_t outputSize);

private:
    int fd_ = -1;
    int lastError_ = 0;
};

// One-shot helpers for values read rarely (startup, hotplug).
std::optional<std::string> readTextFile(const std::string& path);
std::optional<std::int64_t> readIntegerFile(const std::string& path);
bool fileExists(const std::string& path);

} // namespace readout

#include "io/held_file.h"

#include "common/text.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <utility>

namespace readout {

namespace {

constexpr std::size_t initialBufferSize = 4096;

ssize_t preadRetrying(int fd, char* data, std::size_t size, off_t offset) {
    for (;;) {
        const ssize_t result = ::pread(fd, data, size, offset);
        if (result >= 0 || errno != EINTR) {
            return result;
        }
    }
}

} // namespace

HeldFile::~HeldFile() {
    close();
}

HeldFile::HeldFile(HeldFile&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), lastError_(other.lastError_) {}

HeldFile& HeldFile::operator=(HeldFile&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
        lastError_ = other.lastError_;
    }
    return *this;
}

bool HeldFile::open(const std::string& path) {
    return openAt(AT_FDCWD, path.c_str());
}

bool HeldFile::openAt(int directoryFd, const char* relativePath) {
    close();
    fd_ = ::openat(directoryFd, relativePath, O_RDONLY | O_CLOEXEC | O_NOCTTY);
    lastError_ = fd_ < 0 ? errno : 0;
    return fd_ >= 0;
}

void HeldFile::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::optional<std::string_view> HeldFile::readAll(std::vector<char>& buffer) {
    if (fd_ < 0) {
        return std::nullopt;
    }
    if (buffer.size() < initialBufferSize) {
        buffer.resize(initialBufferSize);
    }
    std::size_t total = 0;
    for (;;) {
        const ssize_t count = preadRetrying(fd_, buffer.data() + total, buffer.size() - total,
                                            static_cast<off_t>(total));
        if (count < 0) {
            lastError_ = errno;
            return std::nullopt;
        }
        if (count == 0) {
            break;
        }
        total += static_cast<std::size_t>(count);
        if (total == buffer.size()) {
            buffer.resize(buffer.size() * 2);
        }
    }
    return std::string_view(buffer.data(), total);
}

std::optional<std::int64_t> HeldFile::readInteger() {
    std::array<char, 128> text{};
    if (!readSmallText(text.data(), text.size())) {
        return std::nullopt;
    }
    return parseNumber<std::int64_t>(text.data());
}

bool HeldFile::readSmallText(char* output, std::size_t outputSize) {
    if (fd_ < 0 || outputSize == 0) {
        return false;
    }
    const ssize_t count = preadRetrying(fd_, output, outputSize - 1, 0);
    if (count < 0) {
        lastError_ = errno;
        output[0] = '\0';
        return false;
    }
    const std::string_view trimmed = trim(std::string_view(output, static_cast<std::size_t>(count)));
    const std::size_t offset = static_cast<std::size_t>(trimmed.data() - output);
    for (std::size_t index = 0; index < trimmed.size(); ++index) {
        output[index] = output[offset + index];
    }
    output[trimmed.size()] = '\0';
    return true;
}

std::optional<std::string> readTextFile(const std::string& path) {
    HeldFile file;
    if (!file.open(path)) {
        return std::nullopt;
    }
    std::vector<char> buffer;
    const auto text = file.readAll(buffer);
    if (!text) {
        return std::nullopt;
    }
    return std::string(*text);
}

std::optional<std::int64_t> readIntegerFile(const std::string& path) {
    HeldFile file;
    if (!file.open(path)) {
        return std::nullopt;
    }
    return file.readInteger();
}

bool fileExists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

} // namespace readout

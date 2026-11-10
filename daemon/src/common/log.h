// Minimal logging to stderr (journald adds timestamps and the unit name).
#pragma once

#include <cstdio>
#include <cstring>

namespace readout {

void setVerbose(bool verbose);
bool isVerbose();

// printf-style. Messages are prefixed with a syslog priority for journald.
[[gnu::format(printf, 1, 2)]] void logInfo(const char* format, ...);
[[gnu::format(printf, 1, 2)]] void logWarning(const char* format, ...);
[[gnu::format(printf, 1, 2)]] void logDebug(const char* format, ...);

// strerror() for a negative or positive errno value.
inline const char* errnoName(int error) {
    return std::strerror(error < 0 ? -error : error);
}

} // namespace readout

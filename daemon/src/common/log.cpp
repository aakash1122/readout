#include "common/log.h"

#include <cstdarg>

namespace readout {

namespace {

bool verboseLogging = false;

[[gnu::format(printf, 2, 0)]] void writeLine(const char* priority, const char* format, va_list arguments) {
    std::fputs(priority, stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
}

} // namespace

void setVerbose(bool verbose) {
    verboseLogging = verbose;
}

bool isVerbose() {
    return verboseLogging;
}

void logInfo(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLine("<6>", format, arguments);
    va_end(arguments);
}

void logWarning(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    writeLine("<4>", format, arguments);
    va_end(arguments);
}

void logDebug(const char* format, ...) {
    if (!verboseLogging) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    writeLine("<7>", format, arguments);
    va_end(arguments);
}

} // namespace readout

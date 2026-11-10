// Converts sd-bus message values to JSON text. Used by readoutctl and the tests.
#pragma once

#include <systemd/sd-bus.h>

#include <string>

namespace readout {

// Appends the next complete value in `message` to `out` as JSON. Returns a negative errno on failure.
int writeJsonValue(sd_bus_message* message, std::string& out);

// Reads all remaining values of `message` (as a JSON array if more than one).
int messageToJson(sd_bus_message* message, std::string& out);

} // namespace readout

#include "bus/json_dump.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace readout {

namespace {

void printJsonString(std::string& out, const char* text) {
    out += '"';
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const auto character = static_cast<unsigned char>(*cursor);
        switch (character) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (character < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", character);
                    out += escaped;
                } else {
                    out += static_cast<char>(character);
                }
        }
    }
    out += '"';
}

void printNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%.15g", value);
    out += text;
}

} // namespace

// Converts the next complete value in `message` to JSON. Returns a negative errno on failure.
int writeJsonValue(sd_bus_message* message, std::string& out) {
    char type = 0;
    const char* contents = nullptr;
    int result = sd_bus_message_peek_type(message, &type, &contents);
    if (result <= 0) {
        return result < 0 ? result : -EINVAL;
    }
    switch (type) {
        case 'a': {
            const bool isDictionary = contents[0] == '{';
            result = sd_bus_message_enter_container(message, 'a', contents);
            if (result < 0) {
                return result;
            }
            out += isDictionary ? '{' : '[';
            bool first = true;
            while (!sd_bus_message_at_end(message, false)) {
                if (!first) {
                    out += ',';
                }
                first = false;
                if (isDictionary) {
                    const std::string entrySignature(contents + 1, std::strlen(contents) - 2);
                    result = sd_bus_message_enter_container(message, 'e', entrySignature.c_str());
                    if (result < 0) {
                        return result;
                    }
                    char keyType = 0;
                    sd_bus_message_peek_type(message, &keyType, nullptr);
                    if (keyType == 's' || keyType == 'o') {
                        const char* key = nullptr;
                        sd_bus_message_read_basic(message, keyType, &key);
                        printJsonString(out, key);
                    } else {
                        std::string key;
                        result = writeJsonValue(message, key);
                        if (result < 0) {
                            return result;
                        }
                        printJsonString(out, key.c_str());
                    }
                    out += ':';
                    result = writeJsonValue(message, out);
                    if (result < 0) {
                        return result;
                    }
                    sd_bus_message_exit_container(message);
                } else {
                    result = writeJsonValue(message, out);
                    if (result < 0) {
                        return result;
                    }
                }
            }
            out += isDictionary ? '}' : ']';
            return sd_bus_message_exit_container(message);
        }
        case 'v':
        case 'r': {
            result = sd_bus_message_enter_container(message, type, contents);
            if (result < 0) {
                return result;
            }
            if (type == 'r') {
                out += '[';
                bool first = true;
                while (!sd_bus_message_at_end(message, false)) {
                    if (!first) {
                        out += ',';
                    }
                    first = false;
                    result = writeJsonValue(message, out);
                    if (result < 0) {
                        return result;
                    }
                }
                out += ']';
            } else {
                result = writeJsonValue(message, out);
                if (result < 0) {
                    return result;
                }
            }
            return sd_bus_message_exit_container(message);
        }
        case 's':
        case 'o':
        case 'g': {
            const char* text = nullptr;
            result = sd_bus_message_read_basic(message, type, &text);
            printJsonString(out, text != nullptr ? text : "");
            return result;
        }
        case 'b': {
            int value = 0;
            result = sd_bus_message_read_basic(message, 'b', &value);
            out += value != 0 ? "true" : "false";
            return result;
        }
        case 'd': {
            double value = 0;
            result = sd_bus_message_read_basic(message, 'd', &value);
            printNumber(out, value);
            return result;
        }
        case 'y': {
            std::uint8_t value = 0;
            result = sd_bus_message_read_basic(message, 'y', &value);
            out += std::to_string(value);
            return result;
        }
        case 'n': {
            std::int16_t value = 0;
            result = sd_bus_message_read_basic(message, 'n', &value);
            out += std::to_string(value);
            return result;
        }
        case 'q': {
            std::uint16_t value = 0;
            result = sd_bus_message_read_basic(message, 'q', &value);
            out += std::to_string(value);
            return result;
        }
        case 'i': {
            std::int32_t value = 0;
            result = sd_bus_message_read_basic(message, 'i', &value);
            out += std::to_string(value);
            return result;
        }
        case 'u': {
            std::uint32_t value = 0;
            result = sd_bus_message_read_basic(message, 'u', &value);
            out += std::to_string(value);
            return result;
        }
        case 'x': {
            std::int64_t value = 0;
            result = sd_bus_message_read_basic(message, 'x', &value);
            out += std::to_string(value);
            return result;
        }
        case 't': {
            std::uint64_t value = 0;
            result = sd_bus_message_read_basic(message, 't', &value);
            out += std::to_string(value);
            return result;
        }
        default:
            return -ENOTSUP;
    }
}


int messageToJson(sd_bus_message* message, std::string& out) {
    std::string values;
    int count = 0;
    while (!sd_bus_message_at_end(message, false)) {
        if (count > 0) {
            values += ',';
        }
        const int result = writeJsonValue(message, values);
        if (result < 0) {
            return result;
        }
        ++count;
    }
    out += count == 1 ? values : "[" + values + "]";
    return 0;
}

} // namespace readout

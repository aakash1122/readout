// Allocation-free helpers for parsing kernel text interfaces.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace readout {

// Removes the next line from `text` and stores it (without the newline) in `line`.
// Returns false when `text` is empty.
inline bool takeLine(std::string_view& text, std::string_view& line) {
    if (text.empty()) {
        return false;
    }
    const std::size_t end = text.find('\n');
    if (end == std::string_view::npos) {
        line = text;
        text = {};
    } else {
        line = text.substr(0, end);
        text.remove_prefix(end + 1);
    }
    return true;
}

inline bool isBlank(char character) {
    return character == ' ' || character == '\t';
}

// Removes and returns the next whitespace-separated token. Returns an empty view at the end.
inline std::string_view takeToken(std::string_view& text) {
    std::size_t start = 0;
    while (start < text.size() && isBlank(text[start])) {
        ++start;
    }
    std::size_t end = start;
    while (end < text.size() && !isBlank(text[end])) {
        ++end;
    }
    const std::string_view token = text.substr(start, end - start);
    text.remove_prefix(end);
    return token;
}

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && (isBlank(text.front()) || text.front() == '\n' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (isBlank(text.back()) || text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

template <typename Number>
std::optional<Number> parseNumber(std::string_view token) {
    token = trim(token);
    Number value{};
    const char* first = token.data();
    const char* last = token.data() + token.size();
    if (first != last && *first == '+') {
        ++first;
    }
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || end == first) {
        return std::nullopt;
    }
    return value;
}

// Parses a hexadecimal number with an optional "0x" prefix.
inline std::optional<std::uint32_t> parseHex(std::string_view token) {
    token = trim(token);
    if (token.starts_with("0x") || token.starts_with("0X")) {
        token.remove_prefix(2);
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value, 16);
    if (error != std::errc{} || end == token.data()) {
        return std::nullopt;
    }
    return value;
}

// Splits "Key: value" lines. Returns false if there is no separator.
inline bool splitKeyValue(std::string_view line, char separator, std::string_view& key, std::string_view& value) {
    const std::size_t position = line.find(separator);
    if (position == std::string_view::npos) {
        return false;
    }
    key = trim(line.substr(0, position));
    value = trim(line.substr(position + 1));
    return true;
}

// Copies `text` into `output` as valid, NUL-terminated UTF-8.
// Invalid bytes become '?'. Truncates on a character boundary if the output is too small.
void copySanitizedUtf8(std::string_view text, char* output, std::size_t outputSize);

} // namespace readout

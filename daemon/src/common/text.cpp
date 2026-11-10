#include "common/text.h"

namespace readout {

namespace {

// Returns the length of the valid UTF-8 sequence starting at `text[index]`, or 0 if invalid.
std::size_t validSequenceLength(std::string_view text, std::size_t index) {
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        return lead == 0 ? 0 : 1;
    }
    std::size_t length = 0;
    std::uint32_t codePoint = 0;
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        codePoint = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        codePoint = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        codePoint = lead & 0x07U;
    } else {
        return 0;
    }
    if (index + length > text.size()) {
        return 0;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto next = static_cast<unsigned char>(text[index + offset]);
        if ((next & 0xC0U) != 0x80U) {
            return 0;
        }
        codePoint = (codePoint << 6U) | (next & 0x3FU);
    }
    const bool overlong = (length == 2 && codePoint < 0x80) || (length == 3 && codePoint < 0x800) ||
                          (length == 4 && codePoint < 0x10000);
    const bool surrogate = codePoint >= 0xD800 && codePoint <= 0xDFFF;
    if (overlong || surrogate || codePoint > 0x10FFFF) {
        return 0;
    }
    return length;
}

} // namespace

void copySanitizedUtf8(std::string_view text, char* output, std::size_t outputSize) {
    if (outputSize == 0) {
        return;
    }
    std::size_t written = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t length = validSequenceLength(text, index);
        const bool valid = length != 0;
        if (!valid) {
            length = 1;
        }
        const std::size_t needed = valid ? length : 1;
        if (written + needed >= outputSize) {
            break;
        }
        if (valid) {
            for (std::size_t offset = 0; offset < length; ++offset) {
                output[written++] = text[index + offset];
            }
        } else {
            output[written++] = '?';
        }
        index += length;
    }
    output[written] = '\0';
}

} // namespace readout

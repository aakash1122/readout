#include "bus/dict_writer.h"

#include <vector>

namespace readout {

DictWriter::DictWriter(sd_bus_message* message) : message_(message) {
    check(sd_bus_message_open_container(message_, 'a', "{sv}"));
}

DictWriter::DictWriter(sd_bus_message* message, NestedTag /*tag*/) : message_(message) {
    check(sd_bus_message_open_container(message_, 'a', "{sv}"));
}

void DictWriter::check(int result) {
    if (result < 0 && error_ >= 0) {
        error_ = result;
    }
}

void DictWriter::number(const char* key, double value) {
    check(sd_bus_message_append(message_, "{sv}", key, "d", value));
}

void DictWriter::count(const char* key, std::uint64_t value) {
    check(sd_bus_message_append(message_, "{sv}", key, "t", value));
}

void DictWriter::integer(const char* key, std::int64_t value) {
    check(sd_bus_message_append(message_, "{sv}", key, "x", value));
}

void DictWriter::flag(const char* key, bool value) {
    check(sd_bus_message_append(message_, "{sv}", key, "b", static_cast<int>(value)));
}

void DictWriter::text(const char* key, const char* value) {
    check(sd_bus_message_append(message_, "{sv}", key, "s", value != nullptr ? value : ""));
}

void DictWriter::numbers(const char* key, std::span<const double> values) {
    check(sd_bus_message_open_container(message_, 'e', "sv"));
    check(sd_bus_message_append_basic(message_, 's', key));
    check(sd_bus_message_open_container(message_, 'v', "ad"));
    check(sd_bus_message_append_array(message_, 'd', values.data(), values.size() * sizeof(double)));
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
}

void DictWriter::texts(const char* key, std::span<const char* const> values) {
    check(sd_bus_message_open_container(message_, 'e', "sv"));
    check(sd_bus_message_append_basic(message_, 's', key));
    check(sd_bus_message_open_container(message_, 'v', "as"));
    check(sd_bus_message_open_container(message_, 'a', "s"));
    for (const char* value : values) {
        check(sd_bus_message_append_basic(message_, 's', value));
    }
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
}

bool DictWriter::finish() {
    check(sd_bus_message_close_container(message_));
    return error_ >= 0;
}

} // namespace readout

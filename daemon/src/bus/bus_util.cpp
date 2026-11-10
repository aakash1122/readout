#include "bus/bus_util.h"

#include "common/log.h"

#include <string>

namespace readout {

std::optional<double> VariantValue::number() {
    if (consumed_ || signature_ == nullptr || signature_[0] == '\0' || signature_[1] != '\0') {
        return std::nullopt;
    }
    consumed_ = true;
    switch (signature_[0]) {
        case 'y': {
            std::uint8_t value = 0;
            return sd_bus_message_read_basic(message_, 'y', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        case 'n': {
            std::int16_t value = 0;
            return sd_bus_message_read_basic(message_, 'n', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        case 'q': {
            std::uint16_t value = 0;
            return sd_bus_message_read_basic(message_, 'q', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        case 'i': {
            std::int32_t value = 0;
            return sd_bus_message_read_basic(message_, 'i', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        case 'u': {
            std::uint32_t value = 0;
            return sd_bus_message_read_basic(message_, 'u', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        case 'x': {
            std::int64_t value = 0;
            return sd_bus_message_read_basic(message_, 'x', &value) >= 0
                       ? std::optional<double>(static_cast<double>(value))
                       : std::nullopt;
        }
        case 't': {
            std::uint64_t value = 0;
            return sd_bus_message_read_basic(message_, 't', &value) >= 0
                       ? std::optional<double>(static_cast<double>(value))
                       : std::nullopt;
        }
        case 'd': {
            double value = 0;
            return sd_bus_message_read_basic(message_, 'd', &value) >= 0 ? std::optional<double>(value) : std::nullopt;
        }
        default:
            consumed_ = false;
            return std::nullopt;
    }
}

std::optional<std::int64_t> VariantValue::integer() {
    const auto value = number();
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
}

std::optional<bool> VariantValue::boolean() {
    if (consumed_ || std::string_view(signature_) != "b") {
        return std::nullopt;
    }
    consumed_ = true;
    int value = 0;
    if (sd_bus_message_read_basic(message_, 'b', &value) < 0) {
        return std::nullopt;
    }
    return value != 0;
}

std::optional<std::string> VariantValue::text() {
    const std::string_view signature(signature_);
    if (consumed_ || (signature != "s" && signature != "o")) {
        return std::nullopt;
    }
    consumed_ = true;
    const char* value = nullptr;
    if (sd_bus_message_read_basic(message_, signature_[0], &value) < 0 || value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<std::vector<std::string>> VariantValue::texts() {
    const std::string_view signature(signature_);
    if (consumed_ || (signature != "as" && signature != "ao")) {
        return std::nullopt;
    }
    consumed_ = true;
    const char elementType = signature_[1];
    const char elementSignature[2] = {elementType, '\0'};
    if (sd_bus_message_enter_container(message_, 'a', elementSignature) < 0) {
        return std::nullopt;
    }
    std::vector<std::string> values;
    const char* value = nullptr;
    int result = 0;
    while ((result = sd_bus_message_read_basic(message_, elementType, &value)) > 0) {
        values.emplace_back(value);
    }
    sd_bus_message_exit_container(message_);
    if (result < 0) {
        return std::nullopt;
    }
    return values;
}

std::optional<std::vector<std::uint8_t>> VariantValue::bytes() {
    if (consumed_ || std::string_view(signature_) != "ay") {
        return std::nullopt;
    }
    consumed_ = true;
    const void* data = nullptr;
    std::size_t size = 0;
    if (sd_bus_message_read_array(message_, 'y', &data, &size) < 0) {
        return std::nullopt;
    }
    const auto* begin = static_cast<const std::uint8_t*>(data);
    return std::vector<std::uint8_t>(begin, begin + size);
}

int getAllPropertiesAsync(sd_bus* bus, SlotHandle& slot, const char* destination, const char* path,
                          const char* interface, sd_bus_message_handler_t callback, void* userData) {
    sd_bus_slot* newSlot = nullptr;
    const int result = sd_bus_call_method_async(bus, &newSlot, destination, path, "org.freedesktop.DBus.Properties",
                                                "GetAll", callback, userData, "s", interface);
    if (result >= 0) {
        slot.reset(newSlot);
    }
    return result;
}

int matchPropertiesChanged(sd_bus* bus, SlotHandle& slot, const char* sender, const char* pathNamespace,
                           sd_bus_message_handler_t callback, void* userData) {
    std::string rule = "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'";
    rule += ",sender='";
    rule += sender;
    rule += "'";
    if (pathNamespace != nullptr) {
        rule += ",path_namespace='";
        rule += pathNamespace;
        rule += "'";
    }
    sd_bus_slot* newSlot = nullptr;
    const int result = sd_bus_add_match_async(bus, &newSlot, rule.c_str(), callback, nullptr, userData);
    if (result >= 0) {
        slot.reset(newSlot);
    }
    return result;
}

sd_bus* SystemBus::get() {
    if (bus_ || failed_) {
        return bus_.get();
    }
    sd_bus* bus = nullptr;
    int result = sd_bus_open_system(&bus);
    if (result >= 0) {
        result = sd_bus_attach_event(bus, event_, SD_EVENT_PRIORITY_NORMAL);
    }
    if (result < 0) {
        logWarning("system bus unavailable: %s", errnoName(result));
        sd_bus_unref(bus);
        failed_ = true;
        return nullptr;
    }
    bus_.reset(bus);
    return bus_.get();
}

} // namespace readout

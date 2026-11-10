// Small RAII and parsing helpers around sd-bus.
#pragma once

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace readout {

struct SlotDeleter {
    void operator()(sd_bus_slot* slot) const { sd_bus_slot_unref(slot); }
};
using SlotHandle = std::unique_ptr<sd_bus_slot, SlotDeleter>;

struct MessageDeleter {
    void operator()(sd_bus_message* message) const { sd_bus_message_unref(message); }
};
using MessageHandle = std::unique_ptr<sd_bus_message, MessageDeleter>;

struct BusDeleter {
    void operator()(sd_bus* bus) const { sd_bus_flush_close_unref(bus); }
};
using BusHandle = std::unique_ptr<sd_bus, BusDeleter>;

struct EventSourceDeleter {
    void operator()(sd_event_source* source) const { sd_event_source_disable_unref(source); }
};
using EventSourceHandle = std::unique_ptr<sd_event_source, EventSourceDeleter>;

// A variant value inside a message being read. Each accessor reads the value once.
// Values not read by the handler are skipped automatically.
class VariantValue {
public:
    VariantValue(sd_bus_message* message, const char* signature) : message_(message), signature_(signature) {}

    [[nodiscard]] std::string_view signature() const { return signature_; }
    // Numeric types (y n q i u x t d) as double.
    std::optional<double> number();
    std::optional<std::int64_t> integer();
    std::optional<bool> boolean();
    // "s" or "o".
    std::optional<std::string> text();
    // "as" or "ao".
    std::optional<std::vector<std::string>> texts();
    // "ay".
    std::optional<std::vector<std::uint8_t>> bytes();

    [[nodiscard]] bool consumed() const { return consumed_; }

private:
    sd_bus_message* message_;
    const char* signature_;
    bool consumed_ = false;
};

// Reads an "a{sv}" container. Calls `onProperty(name, VariantValue&)` per entry.
// Returns a negative errno on malformed input.
template <typename Callback>
int readProperties(sd_bus_message* message, Callback&& onProperty) {
    int result = sd_bus_message_enter_container(message, 'a', "{sv}");
    if (result < 0) {
        return result;
    }
    while ((result = sd_bus_message_enter_container(message, 'e', "sv")) > 0) {
        const char* name = nullptr;
        result = sd_bus_message_read_basic(message, 's', &name);
        if (result < 0) {
            return result;
        }
        const char* contents = nullptr;
        result = sd_bus_message_peek_type(message, nullptr, &contents);
        if (result < 0) {
            return result;
        }
        result = sd_bus_message_enter_container(message, 'v', contents);
        if (result < 0) {
            return result;
        }
        VariantValue value(message, contents);
        onProperty(std::string_view(name), value);
        if (!value.consumed()) {
            result = sd_bus_message_skip(message, contents);
            if (result < 0) {
                return result;
            }
        }
        result = sd_bus_message_exit_container(message); // variant
        if (result < 0) {
            return result;
        }
        result = sd_bus_message_exit_container(message); // dict entry
        if (result < 0) {
            return result;
        }
    }
    if (result < 0) {
        return result;
    }
    return sd_bus_message_exit_container(message);
}

// Starts an asynchronous org.freedesktop.DBus.Properties.GetAll call.
int getAllPropertiesAsync(sd_bus* bus, SlotHandle& slot, const char* destination, const char* path,
                          const char* interface, sd_bus_message_handler_t callback, void* userData);

// Adds an asynchronous match for PropertiesChanged signals from `sender`.
// `pathNamespace` limits the match to objects at or below that path (may be null).
int matchPropertiesChanged(sd_bus* bus, SlotHandle& slot, const char* sender, const char* pathNamespace,
                           sd_bus_message_handler_t callback, void* userData);

// Lazily opened system bus connection attached to the event loop.
class SystemBus {
public:
    explicit SystemBus(sd_event* event) : event_(event) {}
    // Returns null if the system bus is unavailable (logged once).
    sd_bus* get();
    // Tests: never connect.
    void disable() { failed_ = true; }

private:
    sd_event* event_;
    BusHandle bus_;
    bool failed_ = false;
};

} // namespace readout

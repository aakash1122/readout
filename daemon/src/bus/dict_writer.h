// Writes a{sv} dictionaries into an sd-bus message.
// Every payload readoutd sends is a dictionary with documented keys (docs/dbus-api.md),
// so clients in Qt and GJS can read it without custom type registration.
#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace readout {

class DictWriter {
public:
    // Opens an "a{sv}" container on `message`. Call finish() to close it.
    explicit DictWriter(sd_bus_message* message);
    DictWriter(const DictWriter&) = delete;
    DictWriter& operator=(const DictWriter&) = delete;
    DictWriter(DictWriter&&) = delete;
    DictWriter& operator=(DictWriter&&) = delete;
    ~DictWriter() = default;

    void number(const char* key, double value);
    void count(const char* key, std::uint64_t value);
    void integer(const char* key, std::int64_t value);
    void flag(const char* key, bool value);
    // `value` must be valid UTF-8. Use copySanitizedUtf8() for untrusted text.
    void text(const char* key, const char* value);
    void numbers(const char* key, std::span<const double> values);
    void texts(const char* key, std::span<const char* const> values);

    // Writes key -> "aa{sv}". `writeItems` receives a ListWriter.
    template <typename Callback>
    void list(const char* key, Callback&& writeItems);

    // Writes key -> "a{sv}" (a nested dictionary).
    template <typename Callback>
    void dictionary(const char* key, Callback&& writeEntries);

    // Closes the container. Returns false if any write failed.
    bool finish();
    [[nodiscard]] bool ok() const { return error_ >= 0; }
    [[nodiscard]] sd_bus_message* message() const { return message_; }

private:
    friend class ListWriter;
    struct NestedTag {};
    DictWriter(sd_bus_message* message, NestedTag tag);
    void check(int result);

    sd_bus_message* message_;
    int error_ = 0;
};

// Writes the items of an "aa{sv}" list.
class ListWriter {
public:
    explicit ListWriter(DictWriter& owner) : owner_(owner) {}

    // Writes one item; `writeEntries` receives a DictWriter for the item's dictionary.
    template <typename Callback>
    void item(Callback&& writeEntries) {
        DictWriter entry(owner_.message_, DictWriter::NestedTag{});
        writeEntries(entry);
        owner_.check(entry.error_);
        owner_.check(entry.finish() ? 0 : -1);
    }

private:
    DictWriter& owner_;
};

template <typename Callback>
void DictWriter::list(const char* key, Callback&& writeItems) {
    check(sd_bus_message_open_container(message_, 'e', "sv"));
    check(sd_bus_message_append_basic(message_, 's', key));
    check(sd_bus_message_open_container(message_, 'v', "aa{sv}"));
    check(sd_bus_message_open_container(message_, 'a', "a{sv}"));
    ListWriter items(*this);
    writeItems(items);
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
}

template <typename Callback>
void DictWriter::dictionary(const char* key, Callback&& writeEntries) {
    check(sd_bus_message_open_container(message_, 'e', "sv"));
    check(sd_bus_message_append_basic(message_, 's', key));
    check(sd_bus_message_open_container(message_, 'v', "a{sv}"));
    DictWriter nested(message_, NestedTag{});
    writeEntries(nested);
    check(nested.error_);
    check(nested.finish() ? 0 : -1);
    check(sd_bus_message_close_container(message_));
    check(sd_bus_message_close_container(message_));
}

} // namespace readout

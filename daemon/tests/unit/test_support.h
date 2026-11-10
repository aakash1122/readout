// Helpers shared by the unit tests.
#pragma once

#include "bus/dict_writer.h"
#include "bus/json_dump.h"
#include "modules/module.h"

#include <systemd/sd-bus.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace readout::test {

inline std::string fixturePath(const std::string& relative) {
    return std::string(READOUT_FIXTURE_DIR) + "/" + relative;
}

inline std::string readFixture(const std::string& relative) {
    std::ifstream stream(fixturePath(relative), std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// A temporary directory tree for fake /proc and /sys layouts. Removed on destruction.
class FakeTree {
public:
    FakeTree() {
        std::string pattern = (std::filesystem::temp_directory_path() / "readout-test-XXXXXX").string();
        root_ = ::mkdtemp(pattern.data());
    }
    ~FakeTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    FakeTree(const FakeTree&) = delete;
    FakeTree& operator=(const FakeTree&) = delete;
    FakeTree(FakeTree&&) = delete;
    FakeTree& operator=(FakeTree&&) = delete;

    [[nodiscard]] const std::string& root() const { return root_; }
    [[nodiscard]] std::string path(const std::string& relative) const { return root_ + "/" + relative; }

    void write(const std::string& relative, std::string_view content) const {
        const std::filesystem::path file = path(relative);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << content;
    }
    // Creates `relative` as a symlink to `target` (absolute, or relative to the link's directory).
    void link(const std::string& relative, const std::string& target) const {
        const std::filesystem::path file = path(relative);
        std::filesystem::create_directories(file.parent_path());
        std::filesystem::create_symlink(target, file);
    }
    void directory(const std::string& relative) const { std::filesystem::create_directories(path(relative)); }

private:
    std::string root_;
};

// Opens a bus connection used only to create messages. Tests run under dbus-run-session.
inline sd_bus* testBus() {
    static sd_bus* bus = [] {
        sd_bus* created = nullptr;
        if (sd_bus_open_user(&created) < 0) {
            return static_cast<sd_bus*>(nullptr);
        }
        return created;
    }();
    return bus;
}

// Encodes a module with `context` into a sealed message and returns the payload as JSON.
inline std::string encodeToJson(const Module& module, const EncodeContext& context) {
    sd_bus* bus = testBus();
    if (bus == nullptr) {
        ADD_FAILURE() << "no session bus (run the tests under dbus-run-session)";
        return {};
    }
    sd_bus_message* message = nullptr;
    EXPECT_GE(sd_bus_message_new_signal(bus, &message, "/test", "test.Interface", "Test"), 0);
    {
        DictWriter writer(message);
        module.encode(writer, context);
        EXPECT_TRUE(writer.finish());
    }
    EXPECT_GE(sd_bus_message_seal(message, 1, 0), 0);
    EXPECT_GE(sd_bus_message_rewind(message, 1), 0);
    std::string json;
    EXPECT_GE(messageToJson(message, json), 0);
    sd_bus_message_unref(message);
    return json;
}

inline std::string encodeToJson(const Module& module, Tier tier, std::span<const std::string> sensorIds = {}) {
    EncodeContext context;
    context.tier = tier;
    context.sensorIds = sensorIds;
    return encodeToJson(module, context);
}

inline std::string encodeInfoToJson(Module& module) {
    sd_bus* bus = testBus();
    if (bus == nullptr) {
        ADD_FAILURE() << "no session bus";
        return {};
    }
    sd_bus_message* message = nullptr;
    EXPECT_GE(sd_bus_message_new_signal(bus, &message, "/test", "test.Interface", "Test"), 0);
    {
        DictWriter writer(message);
        module.encodeInfo(writer);
        EXPECT_TRUE(writer.finish());
    }
    EXPECT_GE(sd_bus_message_seal(message, 1, 0), 0);
    EXPECT_GE(sd_bus_message_rewind(message, 1), 0);
    std::string json;
    EXPECT_GE(messageToJson(message, json), 0);
    sd_bus_message_unref(message);
    return json;
}

// Finds `"key":<number>` in JSON text (first occurrence at or after `from`).
inline std::optional<double> jsonNumber(std::string_view json, std::string_view key, std::size_t from = 0) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const std::size_t position = json.find(needle, from);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string value(json.substr(position + needle.size(), 32));
    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return std::nullopt;
    }
    return number;
}

inline std::optional<std::string> jsonText(std::string_view json, std::string_view key, std::size_t from = 0) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const std::size_t position = json.find(needle, from);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t start = position + needle.size();
    const std::size_t end = json.find('"', start);
    return std::string(json.substr(start, end - start));
}

inline bool jsonHasKey(std::string_view json, std::string_view key) {
    return json.find("\"" + std::string(key) + "\":") != std::string_view::npos;
}

} // namespace readout::test

// readoutctl: command-line client for readoutd.
//
//   readoutctl info                      Print GetInfo() as JSON
//   readoutctl watch [MODULE[:TIER]...]  Subscribe and print each update as one JSON line
//   readoutctl once [MODULE[:TIER]...]   Print the first update and exit
//   readoutctl history MODULE            Print GetHistory() as JSON

#include "bus/json_dump.h"

#include <systemd/sd-bus.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef READOUT_BUS_NAME
#define READOUT_BUS_NAME "io.github.aakash1122.Readout1"
#endif
#ifndef READOUT_OBJECT_PATH
#define READOUT_OBJECT_PATH "/io/github/aakash1122/Readout1"
#endif

namespace {

constexpr const char* busName = READOUT_BUS_NAME;
constexpr const char* objectPath = READOUT_OBJECT_PATH;
constexpr const char* interfaceName = READOUT_BUS_NAME;

int fail(const char* what, int error) {
    std::fprintf(stderr, "readoutctl: %s: %s\n", what, std::strerror(error < 0 ? -error : error));
    return EXIT_FAILURE;
}

int failBus(const char* what, const sd_bus_error& error) {
    std::fprintf(stderr, "readoutctl: %s: %s\n", what, error.message != nullptr ? error.message : "failed");
    return EXIT_FAILURE;
}

struct WatchState {
    bool once = false;
    bool done = false;
};

int handleUpdate(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* state = static_cast<WatchState*>(userData);
    std::uint64_t timestamp = 0;
    if (sd_bus_message_read_basic(message, 't', &timestamp) < 0) {
        return 0;
    }
    std::string out = "{\"timestamp\":" + std::to_string(timestamp) + ",\"modules\":";
    if (readout::writeJsonValue(message, out) < 0) {
        return 0;
    }
    out += "}\n";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
    if (state->once) {
        state->done = true;
    }
    return 0;
}

void printUsage() {
    std::puts("Usage: readoutctl COMMAND [ARGUMENTS]\n"
              "\n"
              "Commands:\n"
              "  info                        Print static system information as JSON\n"
              "  watch [MODULE[:TIER]...]    Print updates as JSON lines (default: all modules, panel tier)\n"
              "  once [MODULE[:TIER]...]     Print one update and exit\n"
              "  history MODULE              Print recent history as JSON\n"
              "\n"
              "Options:\n"
              "  --interval=MS               Update interval for watch (default 1000)\n"
              "\n"
              "Modules: cpu memory network disk gpu sensors battery bluetooth\n"
              "Tiers:   panel detail");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
        printUsage();
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    const std::string_view command = argv[1];
    std::uint32_t intervalMilliseconds = 1000;
    std::vector<std::string> arguments;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument.starts_with("--interval=")) {
            intervalMilliseconds = static_cast<std::uint32_t>(std::strtoul(argv[index] + 11, nullptr, 10));
        } else {
            arguments.emplace_back(argument);
        }
    }

    sd_bus* bus = nullptr;
    int result = sd_bus_open_user(&bus);
    if (result < 0) {
        return fail("cannot connect to the session bus", result);
    }
    sd_bus_error error{}; // same as SD_BUS_ERROR_NULL (a C compound literal)
    sd_bus_message* reply = nullptr;

    if (command == "info" || command == "history") {
        if (command == "history" && arguments.empty()) {
            printUsage();
            return EXIT_FAILURE;
        }
        result = command == "info"
                     ? sd_bus_call_method(bus, busName, objectPath, interfaceName, "GetInfo", &error, &reply, "")
                     : sd_bus_call_method(bus, busName, objectPath, interfaceName, "GetHistory", &error, &reply, "s",
                                          arguments.front().c_str());
        if (result < 0) {
            return failBus("call failed", error);
        }
        std::string out;
        result = readout::writeJsonValue(reply, out);
        if (result < 0) {
            return fail("cannot decode reply", result);
        }
        std::puts(out.c_str());
        sd_bus_message_unref(reply);
        sd_bus_flush_close_unref(bus);
        return EXIT_SUCCESS;
    }

    if (command != "watch" && command != "once") {
        printUsage();
        return EXIT_FAILURE;
    }
    if (arguments.empty()) {
        arguments = {"cpu", "memory", "network", "disk", "gpu", "sensors", "battery", "bluetooth"};
    }
    WatchState state;
    state.once = command == "once";
    sd_bus_slot* slot = nullptr;
    result = sd_bus_match_signal(bus, &slot, busName, objectPath, interfaceName, "Update", &handleUpdate, &state);
    if (result < 0) {
        return fail("cannot add match", result);
    }
    sd_bus_message* call = nullptr;
    result = sd_bus_message_new_method_call(bus, &call, busName, objectPath, interfaceName, "Subscribe");
    if (result < 0) {
        return fail("cannot create call", result);
    }
    sd_bus_message_open_container(call, 'a', "{ss}");
    for (const std::string& argument : arguments) {
        const std::size_t colon = argument.find(':');
        const std::string module = argument.substr(0, colon);
        const std::string tier = colon == std::string::npos ? "panel" : argument.substr(colon + 1);
        sd_bus_message_append(call, "{ss}", module.c_str(), tier.c_str());
    }
    sd_bus_message_close_container(call);
    sd_bus_message_append(call, "u", intervalMilliseconds);
    sd_bus_message_append(call, "a{sv}", 0);
    result = sd_bus_call(bus, call, 0, &error, nullptr);
    sd_bus_message_unref(call);
    if (result < 0) {
        return failBus("subscribe failed", error);
    }
    while (!state.done) {
        result = sd_bus_process(bus, nullptr);
        if (result < 0) {
            return fail("bus error", result);
        }
        if (result > 0) {
            continue;
        }
        result = sd_bus_wait(bus, UINT64_MAX);
        if (result < 0 && result != -EINTR) {
            return fail("wait failed", result);
        }
    }
    sd_bus_slot_unref(slot);
    sd_bus_flush_close_unref(bus);
    return EXIT_SUCCESS;
}

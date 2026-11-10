// readoutd: session D-Bus service that samples system statistics on demand.

#include "bus/bus_util.h"
#include "bus/service.h"
#include "common/log.h"
#include "common/paths.h"
#include "common/text.h"

#include <getopt.h>
#include <sys/resource.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifndef READOUT_VERSION
#define READOUT_VERSION "0.0.0"
#endif
#ifndef READOUT_BUS_NAME
#define READOUT_BUS_NAME "io.github.aakash1122.Readout1"
#endif
#ifndef READOUT_OBJECT_PATH
#define READOUT_OBJECT_PATH "/io/github/aakash1122/Readout1"
#endif

namespace {

void printUsage() {
    std::puts("Usage: readoutd [OPTIONS]\n"
              "\n"
              "Session D-Bus service for the Readout system monitor.\n"
              "\n"
              "  --idle-timeout=SECONDS  Exit this long after the last subscriber leaves (default 30)\n"
              "  --no-idle-exit          Keep running without subscribers\n"
              "  --proc=PATH             Root of procfs (default /proc, for testing)\n"
              "  --sys=PATH              Root of sysfs (default /sys, for testing)\n"
              "  --verbose               Log debug messages\n"
              "  --version               Print the version and exit\n"
              "  --help                  Show this help");
}

// One descriptor is kept open per process while a popup shows process lists.
void raiseFileLimit() {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = std::min<rlim_t>(limit.rlim_max, 65536);
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace readout;

    SystemPaths paths;
    ServiceOptions options;
    options.busName = READOUT_BUS_NAME;
    options.objectPath = READOUT_OBJECT_PATH;
    options.interfaceName = READOUT_BUS_NAME;

    enum OptionId : std::uint16_t { IdleTimeout = 1000, NoIdleExit, Proc, Sys, Verbose, Version, Help };
    static const option longOptions[] = {
        {"idle-timeout", required_argument, nullptr, IdleTimeout},
        {"no-idle-exit", no_argument, nullptr, NoIdleExit},
        {"proc", required_argument, nullptr, Proc},
        {"sys", required_argument, nullptr, Sys},
        {"verbose", no_argument, nullptr, Verbose},
        {"version", no_argument, nullptr, Version},
        {"help", no_argument, nullptr, Help},
        {nullptr, 0, nullptr, 0},
    };
    int choice = 0;
    while ((choice = ::getopt_long(argc, argv, "", longOptions, nullptr)) != -1) {
        switch (choice) {
            case IdleTimeout: {
                const auto seconds = parseNumber<std::uint64_t>(optarg);
                if (!seconds) {
                    std::fprintf(stderr, "invalid --idle-timeout value\n");
                    return EXIT_FAILURE;
                }
                options.idleTimeoutMicroseconds = *seconds * 1'000'000U;
                break;
            }
            case NoIdleExit:
                options.exitWhenIdle = false;
                break;
            case Proc:
                paths.proc = optarg;
                break;
            case Sys:
                paths.sys = optarg;
                break;
            case Verbose:
                setVerbose(true);
                break;
            case Version:
                std::puts("readoutd " READOUT_VERSION);
                return EXIT_SUCCESS;
            case Help:
                printUsage();
                return EXIT_SUCCESS;
            default:
                printUsage();
                return EXIT_FAILURE;
        }
    }

    raiseFileLimit();

    sd_event* event = nullptr;
    int result = sd_event_default(&event);
    if (result < 0) {
        logWarning("cannot create event loop: %s", errnoName(result));
        return EXIT_FAILURE;
    }
    // A null handler makes the loop exit on these signals.
    sd_event_add_signal(event, nullptr, SIGTERM | SD_EVENT_SIGNAL_PROCMASK, nullptr, nullptr);
    sd_event_add_signal(event, nullptr, SIGINT | SD_EVENT_SIGNAL_PROCMASK, nullptr, nullptr);

    sd_bus* rawBus = nullptr;
    result = sd_bus_open_user(&rawBus);
    if (result < 0) {
        logWarning("cannot connect to the session bus: %s", errnoName(result));
        sd_event_unref(event);
        return EXIT_FAILURE;
    }
    BusHandle bus(rawBus);
    sd_bus_set_exit_on_disconnect(bus.get(), 1);
    result = sd_bus_attach_event(bus.get(), event, SD_EVENT_PRIORITY_NORMAL);
    if (result < 0) {
        logWarning("cannot attach the session bus: %s", errnoName(result));
        sd_event_unref(event);
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_SUCCESS;
    {
        Service service(event, bus.get(), paths, options);
        result = service.start();
        if (result < 0) {
            exitCode = EXIT_FAILURE;
        } else {
            result = sd_event_loop(event);
            // With exit-on-disconnect, losing the session bus (logout) ends the loop with
            // -ECONNRESET or -ENOTCONN. That is a normal end of the session, not a failure.
            if (result == -ECONNRESET || result == -ENOTCONN) {
                logInfo("session bus closed, exiting");
            } else if (result < 0) {
                logWarning("event loop failed: %s", errnoName(result));
                exitCode = EXIT_FAILURE;
            }
        }
    }
    bus.reset();
    sd_event_unref(event);
    return exitCode;
}

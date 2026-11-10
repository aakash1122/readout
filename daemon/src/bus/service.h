// The D-Bus object: subscriptions, the tick scheduler, and update delivery.
// See docs/dbus-api.md for the interface contract.
#pragma once

#include "bus/bus_util.h"
#include "common/paths.h"
#include "modules/module.h"
#include "modules/process_table.h"

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace readout {

struct ServiceOptions {
    std::uint64_t idleTimeoutMicroseconds = 30'000'000;
    bool exitWhenIdle = true;
    const char* busName = nullptr;
    const char* objectPath = nullptr;
    const char* interfaceName = nullptr;
};

class Service {
public:
    static constexpr std::uint32_t minimumIntervalMilliseconds = 500;
    static constexpr std::uint32_t maximumIntervalMilliseconds = 60'000;
    static constexpr std::size_t maximumSubscribers = 64;
    static constexpr std::size_t maximumSensorSelection = 64;

    Service(sd_event* event, sd_bus* bus, const SystemPaths& paths, const ServiceOptions& options);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    // Registers the object, requests the bus name and arms the idle timer. Negative errno on failure.
    int start();

private:
    static constexpr std::size_t moduleCount = 8;

    struct Subscriber {
        std::string busName;
        std::array<Tier, moduleCount> tiers{};
        std::uint64_t intervalMicroseconds = 2'000'000;
        std::uint64_t nextDue = 0;
        std::vector<std::string> sensorIds;
        bool cores = false;
        SlotHandle nameWatch;
        SlotHandle ownerCheck;
        Service* service = nullptr;
    };

    static int handleGetInfo(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleSubscribe(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleUnsubscribe(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleGetHistory(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleVersion(sd_bus* bus, const char* path, const char* interface, const char* property,
                             sd_bus_message* reply, void* userData, sd_bus_error* error);
    static int handleSubscriberCount(sd_bus* bus, const char* path, const char* interface, const char* property,
                                     sd_bus_message* reply, void* userData, sd_bus_error* error);
    static int handleNameOwnerChanged(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleNameWatchInstalled(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleOwnerCheck(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handlePrepareForSleep(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleTimer(sd_event_source* source, std::uint64_t usec, void* userData);
    static int handleIdleTimer(sd_event_source* source, std::uint64_t usec, void* userData);

    int findModule(std::string_view name) const;
    Subscriber* findSubscriber(std::string_view busName);
    void removeSubscriber(std::string_view busName);
    // Removal requested from inside a subscriber's own D-Bus callback runs on the next loop iteration,
    // so the callback's slot is not destroyed while it executes.
    void scheduleRemoval(std::string_view busName);
    static int handleDeferredRemoval(sd_event_source* source, void* userData);
    void watchSubscriber(Subscriber& subscriber);

    // Recomputes each module's highest tier and the sensor selection. Returns modules whose tier rose.
    std::array<bool, moduleCount> applyTiers();
    void sampleModules(const std::array<Tier, moduleCount>& tiers, std::uint64_t current);
    void tick(std::uint64_t current);
    void sendUpdate(const Subscriber& subscriber, std::uint64_t current);
    void scheduleTimer();
    void updateIdleTimer();
    static std::uint64_t now();

    sd_event* event_;
    sd_bus* bus_;
    SystemPaths paths_;
    ServiceOptions options_;
    SystemBus systemBus_;
    ProcessTable processes_;
    std::array<std::unique_ptr<Module>, moduleCount> modules_;
    std::vector<std::unique_ptr<Subscriber>> subscribers_;

    SlotHandle objectSlot_;
    SlotHandle sleepMatch_;
    EventSourceHandle timer_;
    EventSourceHandle idleTimer_;
    EventSourceHandle removalSource_;
    std::vector<std::string> pendingRemovals_;
};

} // namespace readout

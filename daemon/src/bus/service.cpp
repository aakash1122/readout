#include "bus/service.h"

#include "bus/dict_writer.h"
#include "common/log.h"
#include "modules/battery.h"
#include "modules/bluetooth.h"
#include "modules/cpu.h"
#include "modules/disk.h"
#include "modules/gpu.h"
#include "modules/memory.h"
#include "modules/network.h"
#include "modules/sensors.h"

#include <algorithm>
#include <cerrno>
#include <ctime>

#ifndef READOUT_VERSION
#define READOUT_VERSION "0.0.0"
#endif

namespace readout {

namespace {

// Subscribers due within this window are served by the same wakeup.
constexpr std::uint64_t coalesceWindowMicroseconds = 20'000;
// Timer slack handed to sd-event so the kernel can batch our wakeup with others.
constexpr std::uint64_t timerAccuracyMicroseconds = 50'000;
// After a tier rises, wait this long so rate values have a baseline.
constexpr std::uint64_t primingDelayMicroseconds = 500'000;

int replyError(sd_bus_error* error, const char* name, const char* text) {
    return sd_bus_error_set(error, name, text);
}

} // namespace

const char* tierName(Tier tier) {
    switch (tier) {
        case Tier::None:
            return "none";
        case Tier::Panel:
            return "panel";
        case Tier::Detail:
            return "detail";
    }
    return "none";
}

bool parseTier(std::string_view text, Tier& tier) {
    if (text == "none") {
        tier = Tier::None;
    } else if (text == "panel") {
        tier = Tier::Panel;
    } else if (text == "detail") {
        tier = Tier::Detail;
    } else {
        return false;
    }
    return true;
}

Service::Service(sd_event* event, sd_bus* bus, const SystemPaths& paths, const ServiceOptions& options)
    : event_(event), bus_(bus), paths_(paths), options_(options), systemBus_(event), processes_(paths) {
    modules_[0] = std::make_unique<CpuModule>(paths_);
    modules_[1] = std::make_unique<MemoryModule>(paths_);
    modules_[2] = std::make_unique<NetworkModule>(paths_, event_, systemBus_);
    modules_[3] = std::make_unique<DiskModule>(paths_, event_, systemBus_);
    modules_[4] = std::make_unique<GpuModule>(paths_);
    modules_[5] = std::make_unique<SensorsModule>(paths_);
    modules_[6] = std::make_unique<BatteryModule>(paths_, systemBus_);
    modules_[7] = std::make_unique<BluetoothModule>(systemBus_);
}

Service::~Service() {
    subscribers_.clear();
    for (auto& module : modules_) {
        module->setTier(Tier::None);
    }
}

std::uint64_t Service::now() {
    timespec time{};
    ::clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<std::uint64_t>(time.tv_sec) * 1'000'000U + static_cast<std::uint64_t>(time.tv_nsec) / 1000U;
}

int Service::start() {
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD_WITH_ARGS("GetInfo", SD_BUS_NO_ARGS, SD_BUS_RESULT("a{sv}", info), &Service::handleGetInfo,
                                SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD_WITH_ARGS("Subscribe", SD_BUS_ARGS("a{ss}", modules, "u", interval_ms, "a{sv}", options),
                                SD_BUS_NO_RESULT, &Service::handleSubscribe, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD_WITH_ARGS("Unsubscribe", SD_BUS_NO_ARGS, SD_BUS_NO_RESULT, &Service::handleUnsubscribe,
                                SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD_WITH_ARGS("GetHistory", SD_BUS_ARGS("s", module), SD_BUS_RESULT("a{sa(td)}", series),
                                &Service::handleGetHistory, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL_WITH_ARGS("Update", SD_BUS_ARGS("t", timestamp, "a{sa{sv}}", modules), 0),
        SD_BUS_PROPERTY("Version", "s", &Service::handleVersion, 0, SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("SubscriberCount", "u", &Service::handleSubscriberCount, 0, 0),
        SD_BUS_VTABLE_END,
    };

    sd_bus_slot* slot = nullptr;
    int result = sd_bus_add_object_vtable(bus_, &slot, options_.objectPath, options_.interfaceName, vtable, this);
    if (result < 0) {
        logWarning("cannot register object: %s", errnoName(result));
        return result;
    }
    objectSlot_.reset(slot);

    result = sd_bus_request_name(bus_, options_.busName, 0);
    if (result < 0) {
        logWarning("cannot own %s: %s", options_.busName, errnoName(result));
        return result;
    }

    if (sd_bus* system = systemBus_.get()) {
        sd_bus_slot* sleepSlot = nullptr;
        if (sd_bus_match_signal_async(system, &sleepSlot, "org.freedesktop.login1", "/org/freedesktop/login1",
                                      "org.freedesktop.login1.Manager", "PrepareForSleep",
                                      &Service::handlePrepareForSleep, nullptr, this) >= 0) {
            sleepMatch_.reset(sleepSlot);
        }
    }

    sd_event_source* timer = nullptr;
    result = sd_event_add_time(event_, &timer, CLOCK_MONOTONIC, UINT64_MAX, timerAccuracyMicroseconds,
                               &Service::handleTimer, this);
    if (result < 0) {
        return result;
    }
    timer_.reset(timer);
    sd_event_source_set_enabled(timer, SD_EVENT_OFF);

    sd_event_source* idleTimer = nullptr;
    result = sd_event_add_time(event_, &idleTimer, CLOCK_MONOTONIC, UINT64_MAX, 1'000'000,
                               &Service::handleIdleTimer, this);
    if (result < 0) {
        return result;
    }
    idleTimer_.reset(idleTimer);
    updateIdleTimer();
    logInfo("readoutd %s ready on %s", READOUT_VERSION, options_.busName);
    return 0;
}

int Service::findModule(std::string_view name) const {
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        if (name == modules_[index]->name()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

Service::Subscriber* Service::findSubscriber(std::string_view busName) {
    for (auto& subscriber : subscribers_) {
        if (subscriber->busName == busName) {
            return subscriber.get();
        }
    }
    return nullptr;
}

void Service::removeSubscriber(std::string_view busName) {
    const auto position = std::find_if(subscribers_.begin(), subscribers_.end(),
                                       [busName](const auto& subscriber) { return subscriber->busName == busName; });
    if (position == subscribers_.end()) {
        return;
    }
    logDebug("subscriber %s removed", (*position)->busName.c_str());
    subscribers_.erase(position);
    applyTiers();
    scheduleTimer();
    updateIdleTimer();
}

void Service::scheduleRemoval(std::string_view busName) {
    pendingRemovals_.emplace_back(busName);
    if (!removalSource_) {
        sd_event_source* source = nullptr;
        if (sd_event_add_defer(event_, &source, &Service::handleDeferredRemoval, this) < 0) {
            return;
        }
        removalSource_.reset(source);
    }
    sd_event_source_set_enabled(removalSource_.get(), SD_EVENT_ONESHOT);
}

int Service::handleDeferredRemoval(sd_event_source* /*source*/, void* userData) {
    auto* service = static_cast<Service*>(userData);
    std::vector<std::string> names;
    names.swap(service->pendingRemovals_);
    for (const std::string& name : names) {
        service->removeSubscriber(name);
    }
    return 0;
}

void Service::updateIdleTimer() {
    if (!options_.exitWhenIdle || !idleTimer_) {
        return;
    }
    if (subscribers_.empty()) {
        sd_event_source_set_time(idleTimer_.get(), now() + options_.idleTimeoutMicroseconds);
        sd_event_source_set_enabled(idleTimer_.get(), SD_EVENT_ONESHOT);
    } else {
        sd_event_source_set_enabled(idleTimer_.get(), SD_EVENT_OFF);
    }
}

int Service::handleIdleTimer(sd_event_source* /*source*/, std::uint64_t /*usec*/, void* userData) {
    auto* service = static_cast<Service*>(userData);
    if (service->subscribers_.empty()) {
        logInfo("no subscribers, exiting");
        sd_bus_release_name(service->bus_, service->options_.busName);
        sd_event_exit(service->event_, 0);
    }
    return 0;
}

// ---- Methods ------------------------------------------------------------------------------

int Service::handleGetInfo(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* service = static_cast<Service*>(userData);
    service->updateIdleTimer();
    sd_bus_message* reply = nullptr;
    int result = sd_bus_message_new_method_return(message, &reply);
    if (result < 0) {
        return result;
    }
    const MessageHandle replyHandle(reply);
    DictWriter writer(reply);
    writer.text("version", READOUT_VERSION);
    std::array<const char*, moduleCount> names{};
    for (std::size_t index = 0; index < moduleCount; ++index) {
        names[index] = service->modules_[index]->name();
    }
    writer.texts("modules", names);
    for (auto& module : service->modules_) {
        writer.dictionary(module->name(), [&module](DictWriter& moduleWriter) { module->encodeInfo(moduleWriter); });
    }
    if (!writer.finish()) {
        return -EINVAL;
    }
    return sd_bus_send(nullptr, reply, nullptr);
}

int Service::handleSubscribe(sd_bus_message* message, void* userData, sd_bus_error* error) {
    auto* service = static_cast<Service*>(userData);
    const char* sender = sd_bus_message_get_sender(message);
    if (sender == nullptr) {
        return replyError(error, SD_BUS_ERROR_ACCESS_DENIED, "Unknown sender");
    }

    std::array<Tier, moduleCount> tiers{};
    int result = sd_bus_message_enter_container(message, 'a', "{ss}");
    if (result < 0) {
        return result;
    }
    const char* moduleName = nullptr;
    const char* tierText = nullptr;
    while ((result = sd_bus_message_read(message, "{ss}", &moduleName, &tierText)) > 0) {
        const int index = service->findModule(moduleName);
        Tier tier = Tier::None;
        if (index < 0) {
            return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Unknown module '%s'", moduleName);
        }
        if (!parseTier(tierText, tier)) {
            return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Unknown tier '%s'", tierText);
        }
        tiers[static_cast<std::size_t>(index)] = tier;
    }
    if (result < 0) {
        return result;
    }
    sd_bus_message_exit_container(message);

    std::uint32_t intervalMilliseconds = 0;
    result = sd_bus_message_read_basic(message, 'u', &intervalMilliseconds);
    if (result < 0) {
        return result;
    }
    intervalMilliseconds = std::clamp(intervalMilliseconds, minimumIntervalMilliseconds, maximumIntervalMilliseconds);

    std::vector<std::string> sensorIds;
    bool sensorsGiven = false;
    bool cores = false;
    result = readProperties(message, [&](std::string_view key, VariantValue& value) {
        if (key == "sensors") {
            if (auto ids = value.texts()) {
                sensorIds = std::move(*ids);
                sensorsGiven = true;
            }
        } else if (key == "cores") {
            cores = value.boolean().value_or(false);
        }
    });
    if (result < 0) {
        return result;
    }
    if (sensorIds.size() > maximumSensorSelection) {
        sensorIds.resize(maximumSensorSelection);
    }

    const bool anyTier = std::any_of(tiers.begin(), tiers.end(), [](Tier tier) { return tier != Tier::None; });
    if (!anyTier) {
        service->removeSubscriber(sender);
        return sd_bus_reply_method_return(message, nullptr);
    }

    Subscriber* subscriber = service->findSubscriber(sender);
    if (subscriber == nullptr) {
        if (service->subscribers_.size() >= maximumSubscribers) {
            return replyError(error, SD_BUS_ERROR_LIMITS_EXCEEDED, "Too many subscribers");
        }
        auto created = std::make_unique<Subscriber>();
        created->busName = sender;
        created->service = service;
        subscriber = created.get();
        service->subscribers_.push_back(std::move(created));
        service->watchSubscriber(*subscriber);
        logDebug("subscriber %s added", sender);
    }
    subscriber->tiers = tiers;
    subscriber->intervalMicroseconds = static_cast<std::uint64_t>(intervalMilliseconds) * 1000U;
    if (sensorsGiven) {
        subscriber->sensorIds = std::move(sensorIds);
    }
    subscriber->cores = cores;

    const std::uint64_t current = now();
    const std::array<bool, moduleCount> raised = service->applyTiers();
    // Prime modules whose tier rose so the first update already has rates.
    std::array<Tier, moduleCount> primeTiers{};
    bool primed = false;
    for (std::size_t index = 0; index < moduleCount; ++index) {
        if (raised[index]) {
            primeTiers[index] = service->modules_[index]->tier();
            primed = true;
        }
    }
    if (primed) {
        service->sampleModules(primeTiers, current);
    }
    subscriber->nextDue = primed ? current + primingDelayMicroseconds : current;
    service->scheduleTimer();
    service->updateIdleTimer();
    return sd_bus_reply_method_return(message, nullptr);
}

int Service::handleUnsubscribe(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* service = static_cast<Service*>(userData);
    const char* sender = sd_bus_message_get_sender(message);
    if (sender != nullptr) {
        service->removeSubscriber(sender);
    }
    return sd_bus_reply_method_return(message, nullptr);
}

int Service::handleGetHistory(sd_bus_message* message, void* userData, sd_bus_error* error) {
    auto* service = static_cast<Service*>(userData);
    const char* moduleName = nullptr;
    int result = sd_bus_message_read_basic(message, 's', &moduleName);
    if (result < 0) {
        return result;
    }
    const int index = service->findModule(moduleName);
    if (index < 0) {
        return sd_bus_error_setf(error, SD_BUS_ERROR_INVALID_ARGS, "Unknown module '%s'", moduleName);
    }
    sd_bus_message* reply = nullptr;
    result = sd_bus_message_new_method_return(message, &reply);
    if (result < 0) {
        return result;
    }
    const MessageHandle replyHandle(reply);
    int status = sd_bus_message_open_container(reply, 'a', "{sa(td)}");
    for (const HistorySeries& series : service->modules_[static_cast<std::size_t>(index)]->history()) {
        status = std::min(status, sd_bus_message_open_container(reply, 'e', "sa(td)"));
        status = std::min(status, sd_bus_message_append_basic(reply, 's', series.name));
        status = std::min(status, sd_bus_message_open_container(reply, 'a', "(td)"));
        series.buffer->forEach([&](std::uint64_t time, double value) {
            status = std::min(status, sd_bus_message_append(reply, "(td)", time, value));
        });
        status = std::min(status, sd_bus_message_close_container(reply));
        status = std::min(status, sd_bus_message_close_container(reply));
    }
    status = std::min(status, sd_bus_message_close_container(reply));
    if (status < 0) {
        return status;
    }
    return sd_bus_send(nullptr, reply, nullptr);
}

int Service::handleVersion(sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
                           const char* /*property*/, sd_bus_message* reply, void* /*userData*/,
                           sd_bus_error* /*error*/) {
    return sd_bus_message_append_basic(reply, 's', READOUT_VERSION);
}

int Service::handleSubscriberCount(sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
                                   const char* /*property*/, sd_bus_message* reply, void* userData,
                                   sd_bus_error* /*error*/) {
    const auto* service = static_cast<Service*>(userData);
    const auto count = static_cast<std::uint32_t>(service->subscribers_.size());
    return sd_bus_message_append_basic(reply, 'u', &count);
}

// ---- Subscriber lifetime --------------------------------------------------------------------

void Service::watchSubscriber(Subscriber& subscriber) {
    std::string rule = "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',"
                       "interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='";
    rule += subscriber.busName;
    rule += "'";
    sd_bus_slot* slot = nullptr;
    if (sd_bus_add_match_async(bus_, &slot, rule.c_str(), &Service::handleNameOwnerChanged,
                               &Service::handleNameWatchInstalled, &subscriber) >= 0) {
        subscriber.nameWatch.reset(slot);
    }
}

int Service::handleNameWatchInstalled(sd_bus_message* /*message*/, void* userData, sd_bus_error* /*error*/) {
    // The client may have vanished before the match existed. Check that it still owns its name.
    auto* subscriber = static_cast<Subscriber*>(userData);
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(subscriber->service->bus_, &slot, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                 "org.freedesktop.DBus", "GetNameOwner", &Service::handleOwnerCheck, subscriber, "s",
                                 subscriber->busName.c_str()) >= 0) {
        subscriber->ownerCheck.reset(slot);
    }
    return 0;
}

int Service::handleOwnerCheck(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* subscriber = static_cast<Subscriber*>(userData);
    if (sd_bus_message_is_method_error(message, nullptr)) {
        subscriber->service->scheduleRemoval(subscriber->busName);
    }
    return 0;
}

int Service::handleNameOwnerChanged(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* subscriber = static_cast<Subscriber*>(userData);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) < 0) {
        return 0;
    }
    if (newOwner == nullptr || newOwner[0] == '\0') {
        subscriber->service->scheduleRemoval(subscriber->busName);
    }
    return 0;
}

int Service::handlePrepareForSleep(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* service = static_cast<Service*>(userData);
    int sleeping = 0;
    if (sd_bus_message_read_basic(message, 'b', &sleeping) < 0) {
        return 0;
    }
    if (sleeping == 0) {
        logDebug("resumed from suspend, resetting baselines");
        for (auto& module : service->modules_) {
            module->resetBaselines();
        }
        service->processes_.resetBaselines();
    }
    return 0;
}

// ---- Scheduling ------------------------------------------------------------------------------

std::array<bool, Service::moduleCount> Service::applyTiers() {
    std::array<bool, moduleCount> raised{};
    std::vector<std::string> sensorUnion;
    for (std::size_t index = 0; index < moduleCount; ++index) {
        Tier highest = Tier::None;
        for (const auto& subscriber : subscribers_) {
            highest = std::max(highest, subscriber->tiers[index]);
        }
        Module& module = *modules_[index];
        if (highest != module.tier()) {
            raised[index] = highest > module.tier();
            module.setTier(highest);
        }
    }
    for (const auto& subscriber : subscribers_) {
        for (const std::string& id : subscriber->sensorIds) {
            if (std::find(sensorUnion.begin(), sensorUnion.end(), id) == sensorUnion.end()) {
                sensorUnion.push_back(id);
            }
        }
    }
    for (auto& module : modules_) {
        module->setSelectedSensors(sensorUnion);
    }
    bool needsProcesses = false;
    for (auto& module : modules_) {
        needsProcesses = needsProcesses || module->processNeeds(module->tier()).basic;
    }
    if (!needsProcesses) {
        processes_.clear();
    }
    return raised;
}

void Service::sampleModules(const std::array<Tier, moduleCount>& tiers, std::uint64_t current) {
    ProcessNeeds needs;
    for (std::size_t index = 0; index < moduleCount; ++index) {
        if (tiers[index] == Tier::None) {
            continue;
        }
        const ProcessNeeds moduleNeeds = modules_[index]->processNeeds(tiers[index]);
        needs.basic = needs.basic || moduleNeeds.basic;
        needs.io = needs.io || moduleNeeds.io;
    }
    if (needs.basic) {
        processes_.refresh(current, needs.io);
    }
    for (std::size_t index = 0; index < moduleCount; ++index) {
        if (tiers[index] == Tier::None) {
            continue;
        }
        SampleContext context;
        context.nowMicroseconds = current;
        context.tier = tiers[index];
        context.processes = needs.basic ? &processes_ : nullptr;
        modules_[index]->sample(context);
    }
}

void Service::tick(std::uint64_t current) {
    std::array<Tier, moduleCount> tiers{};
    bool anyDue = false;
    for (const auto& subscriber : subscribers_) {
        if (subscriber->nextDue > current + coalesceWindowMicroseconds) {
            continue;
        }
        anyDue = true;
        for (std::size_t index = 0; index < moduleCount; ++index) {
            tiers[index] = std::max(tiers[index], subscriber->tiers[index]);
        }
    }
    if (!anyDue) {
        return;
    }
    sampleModules(tiers, current);
    for (auto& subscriber : subscribers_) {
        if (subscriber->nextDue > current + coalesceWindowMicroseconds) {
            continue;
        }
        sendUpdate(*subscriber, current);
        // Align to multiples of the interval so subscribers with related intervals share wakeups.
        // Never schedule closer than half an interval (happens right after a subscribe).
        const std::uint64_t interval = subscriber->intervalMicroseconds;
        std::uint64_t next = (current / interval + 1) * interval;
        if (next - current < interval / 2) {
            next += interval;
        }
        subscriber->nextDue = next;
    }
}

void Service::sendUpdate(const Subscriber& subscriber, std::uint64_t current) {
    sd_bus_message* message = nullptr;
    int result = sd_bus_message_new_signal(bus_, &message, options_.objectPath, options_.interfaceName, "Update");
    if (result < 0) {
        return;
    }
    const MessageHandle handle(message);
    result = sd_bus_message_set_destination(message, subscriber.busName.c_str());
    result = std::min(result, sd_bus_message_append_basic(message, 't', &current));
    result = std::min(result, sd_bus_message_open_container(message, 'a', "{sa{sv}}"));
    for (std::size_t index = 0; index < moduleCount && result >= 0; ++index) {
        const Tier tier = subscriber.tiers[index];
        if (tier == Tier::None) {
            continue;
        }
        const Module& module = *modules_[index];
        result = std::min(result, sd_bus_message_open_container(message, 'e', "sa{sv}"));
        result = std::min(result, sd_bus_message_append_basic(message, 's', module.name()));
        DictWriter writer(message);
        EncodeContext context;
        context.tier = tier;
        context.sensorIds = subscriber.sensorIds;
        context.cores = subscriber.cores;
        module.encode(writer, context);
        result = std::min(result, writer.finish() ? 0 : -EINVAL);
        result = std::min(result, sd_bus_message_close_container(message));
    }
    result = std::min(result, sd_bus_message_close_container(message));
    if (result < 0) {
        logWarning("failed to build update: %s", errnoName(result));
        return;
    }
    result = sd_bus_send(bus_, message, nullptr);
    if (result < 0) {
        logDebug("failed to send update to %s: %s", subscriber.busName.c_str(), errnoName(result));
    }
}

void Service::scheduleTimer() {
    if (!timer_) {
        return;
    }
    std::uint64_t earliest = UINT64_MAX;
    for (const auto& subscriber : subscribers_) {
        earliest = std::min(earliest, subscriber->nextDue);
    }
    if (earliest == UINT64_MAX) {
        sd_event_source_set_enabled(timer_.get(), SD_EVENT_OFF);
        return;
    }
    sd_event_source_set_time(timer_.get(), earliest);
    sd_event_source_set_enabled(timer_.get(), SD_EVENT_ONESHOT);
}

int Service::handleTimer(sd_event_source* /*source*/, std::uint64_t /*usec*/, void* userData) {
    auto* service = static_cast<Service*>(userData);
    service->tick(now());
    service->scheduleTimer();
    return 0;
}

} // namespace readout

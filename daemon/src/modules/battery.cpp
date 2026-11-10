#include "modules/battery.h"

#include "common/text.h"
#include "io/held_file.h"
#include "io/sysfs.h"

#include <algorithm>
#include <array>

namespace readout {

namespace {

constexpr const char* upowerService = "org.freedesktop.UPower";
constexpr const char* upowerPath = "/org/freedesktop/UPower";
constexpr const char* upowerInterface = "org.freedesktop.UPower";
constexpr const char* deviceInterface = "org.freedesktop.UPower.Device";
constexpr const char* displayDevicePath = "/org/freedesktop/UPower/devices/DisplayDevice";

constexpr std::uint32_t typeLinePower = 1;
constexpr std::uint32_t typeBattery = 2;

std::string sanitized(const std::optional<std::string>& text) {
    std::array<char, 128> buffer{};
    copySanitizedUtf8(text.value_or(""), buffer.data(), buffer.size());
    return buffer.data();
}

} // namespace

const char* BatteryModule::stateName(std::uint32_t state) {
    switch (state) {
        case 1:
            return "charging";
        case 2:
            return "discharging";
        case 3:
            return "empty";
        case 4:
            return "full";
        case 5:
            return "pending-charge";
        case 6:
            return "pending-discharge";
        default:
            return "unknown";
    }
}

const char* BatteryModule::kindName(std::uint32_t type) {
    static constexpr const char* names[] = {
        "unknown", "line-power", "battery",  "ups",      "monitor",  "mouse",    "keyboard", "pda",
        "phone",   "media-player", "tablet", "computer", "gaming-input", "pen", "touchpad", "modem",
        "network", "headset",  "speakers", "headphones", "video",   "other-audio", "remote-control",
        "printer", "scanner",  "camera",   "wearable", "toy",      "bluetooth-generic",
    };
    return type < std::size(names) ? names[type] : "unknown";
}

const char* BatteryModule::technologyName(std::uint32_t technology) {
    static constexpr const char* names[] = {"unknown", "lithium-ion", "lithium-polymer", "lithium-iron-phosphate",
                                            "lead-acid", "nickel-cadmium", "nickel-metal-hydride"};
    return technology < std::size(names) ? names[technology] : "unknown";
}

void BatteryModule::applyProperty(Device& device, std::string_view key, VariantValue& value) {
    if (key == "Type") {
        device.type = static_cast<std::uint32_t>(value.integer().value_or(0));
    } else if (key == "PowerSupply") {
        device.powerSupply = value.boolean().value_or(false);
    } else if (key == "IsPresent") {
        device.present = value.boolean().value_or(false);
    } else if (key == "Online") {
        device.online = value.boolean().value_or(false);
    } else if (key == "Vendor") {
        device.vendor = sanitized(value.text());
    } else if (key == "Model") {
        device.model = sanitized(value.text());
    } else if (key == "Percentage") {
        device.percentage = value.number().value_or(-1);
    } else if (key == "State") {
        device.state = static_cast<std::uint32_t>(value.integer().value_or(0));
    } else if (key == "TimeToEmpty") {
        device.timeToEmpty = value.integer().value_or(0);
    } else if (key == "TimeToFull") {
        device.timeToFull = value.integer().value_or(0);
    } else if (key == "EnergyRate") {
        device.energyRate = value.number().value_or(0);
    } else if (key == "Energy") {
        device.energy = value.number().value_or(0);
    } else if (key == "EnergyFull") {
        device.energyFull = value.number().value_or(0);
    } else if (key == "EnergyFullDesign") {
        device.energyFullDesign = value.number().value_or(0);
    } else if (key == "Voltage") {
        device.voltage = value.number().value_or(0);
    } else if (key == "Capacity") {
        device.capacity = value.number().value_or(0);
    } else if (key == "Temperature") {
        device.temperature = value.number().value_or(0);
    } else if (key == "ChargeCycles") {
        device.cycles = value.integer().value_or(-1);
    } else if (key == "Technology") {
        device.technology = static_cast<std::uint32_t>(value.integer().value_or(0));
    }
}

BatteryModule::BatteryModule(const SystemPaths& paths, SystemBus& systemBus) : paths_(paths), systemBus_(systemBus) {
    historySeries_[0] = HistorySeries{"level", &levelHistory_};
}

BatteryModule::~BatteryModule() {
    stop();
}

void BatteryModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (tier == Tier::None) {
        stop();
    } else if (previous == Tier::None) {
        start();
    }
}

void BatteryModule::start() {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr) {
        return;
    }
    const auto addMatch = [&](const char* rule, sd_bus_message_handler_t handler) {
        sd_bus_slot* slot = nullptr;
        if (sd_bus_add_match_async(bus, &slot, rule, handler, nullptr, this) >= 0) {
            matches_.emplace_back(slot);
        }
    };
    addMatch("type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.DBus.Properties',"
             "member='PropertiesChanged',path_namespace='/org/freedesktop/UPower'",
             &BatteryModule::handlePropertiesChanged);
    addMatch("type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.UPower',member='DeviceAdded'",
             &BatteryModule::handleDeviceAdded);
    addMatch("type='signal',sender='org.freedesktop.UPower',interface='org.freedesktop.UPower',member='DeviceRemoved'",
             &BatteryModule::handleDeviceRemoved);
    addMatch("type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.freedesktop.UPower'",
             &BatteryModule::handleOwnerChanged);
    refreshAll();
}

void BatteryModule::stop() {
    matches_.clear();
    pending_.clear();
    managerCall_.reset();
    enumerateCall_.reset();
    devices_.clear();
    display_ = Device{};
    available_ = false;
    onBattery_ = false;
}

void BatteryModule::refreshAll() {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr) {
        return;
    }
    devices_.clear();
    pending_.clear();
    display_ = Device{};
    getAllPropertiesAsync(bus, managerCall_, upowerService, upowerPath, upowerInterface, &BatteryModule::handleManager,
                          this);
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus, &slot, upowerService, upowerPath, upowerInterface, "EnumerateDevices",
                                 &BatteryModule::handleEnumerate, this, "") >= 0) {
        enumerateCall_.reset(slot);
    }
    requestDevice(displayDevicePath);
}

void BatteryModule::requestDevice(const std::string& path) {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr) {
        return;
    }
    PendingCall& call = pending_.emplace_back();
    call.module = this;
    call.path = path;
    if (getAllPropertiesAsync(bus, call.slot, upowerService, path.c_str(), deviceInterface,
                              &BatteryModule::handleDeviceProperties, &call) < 0) {
        pending_.pop_back();
    }
}

BatteryModule::Device* BatteryModule::findDevice(std::string_view path) {
    if (path == displayDevicePath) {
        return &display_;
    }
    for (Device& device : devices_) {
        if (device.path == path) {
            return &device;
        }
    }
    return nullptr;
}

bool BatteryModule::isSystemBattery(const Device& device) const {
    return device.type == typeBattery && device.powerSupply;
}

int BatteryModule::handleManager(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    if (!sd_bus_message_is_method_error(message, nullptr)) {
        module->available_ = true;
        readProperties(message, [module](std::string_view key, VariantValue& value) {
            if (key == "OnBattery") {
                module->onBattery_ = value.boolean().value_or(false);
            }
        });
    }
    module->managerCall_.reset();
    return 0;
}

int BatteryModule::handleEnumerate(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    if (!sd_bus_message_is_method_error(message, nullptr) && sd_bus_message_enter_container(message, 'a', "o") > 0) {
        const char* path = nullptr;
        while (sd_bus_message_read_basic(message, 'o', &path) > 0) {
            module->requestDevice(path);
        }
        sd_bus_message_exit_container(message);
    }
    module->enumerateCall_.reset();
    return 0;
}

int BatteryModule::handleDeviceProperties(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* call = static_cast<PendingCall*>(userData);
    BatteryModule* module = call->module;
    if (!sd_bus_message_is_method_error(message, nullptr)) {
        Device* device = module->findDevice(call->path);
        if (device == nullptr) {
            device = &module->devices_.emplace_back();
            device->path = call->path;
        }
        Device& target = *device;
        readProperties(message, [&target](std::string_view key, VariantValue& value) { applyProperty(target, key, value); });
    }
    std::erase_if(module->pending_, [call](const PendingCall& pending) { return &pending == call; });
    return 0;
}

int BatteryModule::handlePropertiesChanged(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    const char* path = sd_bus_message_get_path(message);
    const char* interface = nullptr;
    if (path == nullptr || sd_bus_message_read_basic(message, 's', &interface) < 0) {
        return 0;
    }
    if (std::string_view(interface) == upowerInterface) {
        readProperties(message, [module](std::string_view key, VariantValue& value) {
            if (key == "OnBattery") {
                module->onBattery_ = value.boolean().value_or(false);
            }
        });
        return 0;
    }
    if (std::string_view(interface) != deviceInterface) {
        return 0;
    }
    Device* device = module->findDevice(path);
    if (device == nullptr) {
        module->requestDevice(path);
        return 0;
    }
    Device& target = *device;
    readProperties(message, [&target](std::string_view key, VariantValue& value) { applyProperty(target, key, value); });
    return 0;
}

int BatteryModule::handleDeviceAdded(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    const char* path = nullptr;
    if (sd_bus_message_read_basic(message, 'o', &path) >= 0 && path != nullptr && module->findDevice(path) == nullptr) {
        module->requestDevice(path);
    }
    return 0;
}

int BatteryModule::handleDeviceRemoved(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    const char* path = nullptr;
    if (sd_bus_message_read_basic(message, 'o', &path) >= 0 && path != nullptr) {
        const std::string_view removed(path);
        std::erase_if(module->devices_, [removed](const Device& device) { return device.path == removed; });
    }
    return 0;
}

int BatteryModule::handleOwnerChanged(sd_bus_message* /*message*/, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BatteryModule*>(userData);
    module->available_ = false;
    module->refreshAll();
    return 0;
}

void BatteryModule::sample(const SampleContext& context) {
    if (display_.present && display_.percentage >= 0) {
        levelHistory_.push(context.nowMicroseconds, display_.percentage);
    }
}

void BatteryModule::encode(DictWriter& writer, const EncodeContext& context) const {
    const bool hasBattery =
        std::any_of(devices_.begin(), devices_.end(), [this](const Device& device) {
            return isSystemBattery(device) && device.present;
        });
    writer.flag("available", available_);
    writer.flag("present", hasBattery);
    bool externalPower = !onBattery_;
    for (const Device& device : devices_) {
        if (device.type == typeLinePower && device.powerSupply) {
            externalPower = device.online;
        }
    }
    writer.flag("externalPower", externalPower);
    if (hasBattery) {
        writer.number("percentage", display_.percentage);
        writer.text("state", stateName(display_.state));
        writer.integer("timeToEmpty", display_.timeToEmpty);
        writer.integer("timeToFull", display_.timeToFull);
        writer.number("power", display_.energyRate);
    }
    if (context.tier != Tier::Detail) {
        return;
    }
    writer.list("batteries", [this](ListWriter& items) {
        for (const Device& device : devices_) {
            if (!isSystemBattery(device) || !device.present) {
                continue;
            }
            items.item([&device](DictWriter& item) {
                item.text("vendor", device.vendor.c_str());
                item.text("model", device.model.c_str());
                item.number("percentage", device.percentage);
                item.text("state", stateName(device.state));
                item.number("energy", device.energy);
                item.number("energyFull", device.energyFull);
                item.number("energyFullDesign", device.energyFullDesign);
                const double health = device.capacity > 0 ? device.capacity
                                      : device.energyFullDesign > 0
                                          ? device.energyFull * 100.0 / device.energyFullDesign
                                          : 0;
                item.number("health", std::min(health, 100.0));
                item.integer("cycles", device.cycles);
                item.number("voltage", device.voltage);
                item.number("power", device.energyRate);
                if (device.temperature > 0) {
                    item.number("temperature", device.temperature);
                }
                item.text("technology", technologyName(device.technology));
                item.integer("timeToEmpty", device.timeToEmpty);
                item.integer("timeToFull", device.timeToFull);
            });
        }
    });
    writer.list("peripherals", [this](ListWriter& items) {
        for (const Device& device : devices_) {
            if (device.powerSupply || device.type == typeLinePower || device.percentage < 0 ||
                (device.type == typeBattery && !device.present)) {
                continue;
            }
            items.item([&device](DictWriter& item) {
                item.text("model", device.model.c_str());
                item.text("kind", kindName(device.type));
                item.number("percentage", device.percentage);
                item.text("state", stateName(device.state));
            });
        }
    });
}

void BatteryModule::encodeInfo(DictWriter& writer) {
    // A system battery in sysfs (peripheral batteries have scope "Device"). No UPower round-trip needed.
    bool hasBattery = false;
    const std::string root = paths_.sys + "/class/power_supply/";
    for (const std::string& entry : listDirectory(paths_.sys + "/class/power_supply")) {
        const auto type = readTextFile(root + entry + "/type");
        const auto scope = readTextFile(root + entry + "/scope");
        if (type && trim(*type) == "Battery" && (!scope || trim(*scope) != "Device")) {
            hasBattery = true;
            break;
        }
    }
    writer.flag("hasBattery", hasBattery);
}

} // namespace readout

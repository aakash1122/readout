#include "modules/bluetooth.h"

#include "common/text.h"

#include <algorithm>
#include <array>

namespace readout {

namespace {

constexpr std::string_view adapterInterface = "org.bluez.Adapter1";
constexpr std::string_view deviceInterface = "org.bluez.Device1";
constexpr std::string_view batteryInterface = "org.bluez.Battery1";

std::string sanitized(const std::optional<std::string>& text) {
    std::array<char, 128> buffer{};
    copySanitizedUtf8(text.value_or(""), buffer.data(), buffer.size());
    return buffer.data();
}

} // namespace

BluetoothModule::BluetoothModule(SystemBus& systemBus) : systemBus_(systemBus) {}

BluetoothModule::~BluetoothModule() {
    stop();
}

void BluetoothModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (tier == Tier::None) {
        stop();
    } else if (previous == Tier::None) {
        start();
    }
}

void BluetoothModule::start() {
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
    addMatch("type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'",
             &BluetoothModule::handleInterfacesAdded);
    addMatch("type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',"
             "member='InterfacesRemoved'",
             &BluetoothModule::handleInterfacesRemoved);
    addMatch("type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
             "member='PropertiesChanged',path_namespace='/org/bluez'",
             &BluetoothModule::handlePropertiesChanged);
    addMatch("type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.bluez'",
             &BluetoothModule::handleOwnerChanged);
    refresh();
}

void BluetoothModule::stop() {
    matches_.clear();
    managedObjectsCall_.reset();
    adapters_.clear();
    devices_.clear();
    available_ = false;
}

void BluetoothModule::refresh() {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr) {
        return;
    }
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus, &slot, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                                 "GetManagedObjects", &BluetoothModule::handleManagedObjects, this, "") >= 0) {
        managedObjectsCall_.reset(slot);
    }
}

BluetoothModule::Device& BluetoothModule::deviceFor(const std::string& path) {
    for (Device& device : devices_) {
        if (device.path == path) {
            return device;
        }
    }
    Device& added = devices_.emplace_back();
    added.path = path;
    return added;
}

BluetoothModule::Adapter& BluetoothModule::adapterFor(const std::string& path) {
    for (Adapter& adapter : adapters_) {
        if (adapter.path == path) {
            return adapter;
        }
    }
    Adapter& added = adapters_.emplace_back();
    added.path = path;
    return added;
}

void BluetoothModule::applyProperties(sd_bus_message* message, const std::string& path, std::string_view interface) {
    if (interface == adapterInterface) {
        Adapter& adapter = adapterFor(path);
        readProperties(message, [&adapter](std::string_view key, VariantValue& value) {
            if (key == "Alias") {
                adapter.name = sanitized(value.text());
            } else if (key == "Address") {
                adapter.address = sanitized(value.text());
            } else if (key == "Powered") {
                adapter.powered = value.boolean().value_or(false);
            }
        });
    } else if (interface == deviceInterface) {
        Device& device = deviceFor(path);
        readProperties(message, [&device](std::string_view key, VariantValue& value) {
            if (key == "Alias") {
                device.name = sanitized(value.text());
            } else if (key == "Address") {
                device.address = sanitized(value.text());
            } else if (key == "Icon") {
                device.icon = sanitized(value.text());
            } else if (key == "Connected") {
                device.connected = value.boolean().value_or(false);
            } else if (key == "Paired") {
                device.paired = value.boolean().value_or(false);
            } else if (key == "Trusted") {
                device.trusted = value.boolean().value_or(false);
            }
        });
    } else if (interface == batteryInterface) {
        Device& device = deviceFor(path);
        readProperties(message, [&device](std::string_view key, VariantValue& value) {
            if (key == "Percentage") {
                device.battery = value.number().value_or(-1);
            }
        });
    } else {
        sd_bus_message_skip(message, "a{sv}");
    }
}

void BluetoothModule::readInterfaces(sd_bus_message* message, const std::string& path) {
    if (sd_bus_message_enter_container(message, 'a', "{sa{sv}}") <= 0) {
        return;
    }
    while (sd_bus_message_enter_container(message, 'e', "sa{sv}") > 0) {
        const char* interface = nullptr;
        if (sd_bus_message_read_basic(message, 's', &interface) < 0) {
            break;
        }
        applyProperties(message, path, interface);
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
}

int BluetoothModule::handleManagedObjects(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BluetoothModule*>(userData);
    module->adapters_.clear();
    module->devices_.clear();
    module->available_ = false;
    if (!sd_bus_message_is_method_error(message, nullptr) &&
        sd_bus_message_enter_container(message, 'a', "{oa{sa{sv}}}") > 0) {
        module->available_ = true;
        while (sd_bus_message_enter_container(message, 'e', "oa{sa{sv}}") > 0) {
            const char* path = nullptr;
            sd_bus_message_read_basic(message, 'o', &path);
            module->readInterfaces(message, path != nullptr ? path : "");
            sd_bus_message_exit_container(message);
        }
        sd_bus_message_exit_container(message);
    }
    // Objects that only had a Battery1 interface before Device1 arrived have no address.
    std::erase_if(module->devices_, [](const Device& device) { return device.address.empty(); });
    module->managedObjectsCall_.reset();
    return 0;
}

int BluetoothModule::handleInterfacesAdded(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BluetoothModule*>(userData);
    const char* path = nullptr;
    if (sd_bus_message_read_basic(message, 'o', &path) >= 0 && path != nullptr) {
        module->available_ = true;
        module->readInterfaces(message, path);
    }
    return 0;
}

int BluetoothModule::handleInterfacesRemoved(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BluetoothModule*>(userData);
    const char* path = nullptr;
    if (sd_bus_message_read_basic(message, 'o', &path) < 0 || path == nullptr) {
        return 0;
    }
    const std::string objectPath = path;
    if (sd_bus_message_enter_container(message, 'a', "s") <= 0) {
        return 0;
    }
    const char* interface = nullptr;
    while (sd_bus_message_read_basic(message, 's', &interface) > 0) {
        const std::string_view name(interface);
        if (name == deviceInterface) {
            std::erase_if(module->devices_, [&](const Device& device) { return device.path == objectPath; });
        } else if (name == adapterInterface) {
            std::erase_if(module->adapters_, [&](const Adapter& adapter) { return adapter.path == objectPath; });
        } else if (name == batteryInterface) {
            for (Device& device : module->devices_) {
                if (device.path == objectPath) {
                    device.battery = -1;
                }
            }
        }
    }
    sd_bus_message_exit_container(message);
    return 0;
}

int BluetoothModule::handlePropertiesChanged(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<BluetoothModule*>(userData);
    const char* path = sd_bus_message_get_path(message);
    const char* interface = nullptr;
    if (path == nullptr || sd_bus_message_read_basic(message, 's', &interface) < 0) {
        return 0;
    }
    const std::string_view name(interface);
    if (name == adapterInterface || name == deviceInterface || name == batteryInterface) {
        module->applyProperties(message, path, name);
    }
    return 0;
}

int BluetoothModule::handleOwnerChanged(sd_bus_message* /*message*/, void* userData, sd_bus_error* /*error*/) {
    static_cast<BluetoothModule*>(userData)->refresh();
    return 0;
}

void BluetoothModule::sample(const SampleContext& /*context*/) {}

void BluetoothModule::encode(DictWriter& writer, const EncodeContext& /*context*/) const {
    const bool powered = std::any_of(adapters_.begin(), adapters_.end(), [](const Adapter& adapter) { return adapter.powered; });
    writer.flag("available", available_ && !adapters_.empty());
    writer.flag("powered", powered);
    std::uint64_t connected = 0;
    for (const Device& device : devices_) {
        connected += device.connected ? 1U : 0U;
    }
    writer.count("connectedCount", connected);
    writer.list("devices", [this](ListWriter& items) {
        // Connected first, then paired, by name.
        std::vector<const Device*>& ordered = ordered_;
        ordered.clear();
        for (const Device& device : devices_) {
            if (device.paired || device.connected) {
                ordered.push_back(&device);
            }
        }
        std::sort(ordered.begin(), ordered.end(), [](const Device* left, const Device* right) {
            if (left->connected != right->connected) {
                return left->connected;
            }
            return left->name < right->name;
        });
        for (const Device* device : ordered) {
            items.item([device](DictWriter& item) {
                item.text("name", device->name.empty() ? device->address.c_str() : device->name.c_str());
                item.text("address", device->address.c_str());
                item.text("icon", device->icon.c_str());
                item.flag("connected", device->connected);
                item.flag("paired", device->paired);
                if (device->battery >= 0) {
                    item.number("battery", device->battery);
                }
            });
        }
    });
}

} // namespace readout

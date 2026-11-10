#pragma once

#include "bus/bus_util.h"
#include "modules/module.h"

#include <string>
#include <vector>

namespace readout {

// Bluetooth adapters and paired devices from BlueZ, including battery levels. Event-driven.
class BluetoothModule final : public Module {
public:
    explicit BluetoothModule(SystemBus& systemBus);
    ~BluetoothModule() override;
    BluetoothModule(const BluetoothModule&) = delete;
    BluetoothModule& operator=(const BluetoothModule&) = delete;
    BluetoothModule(BluetoothModule&&) = delete;
    BluetoothModule& operator=(BluetoothModule&&) = delete;

    [[nodiscard]] const char* name() const override { return "bluetooth"; }
    void setTier(Tier tier) override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;

private:
    struct Adapter {
        std::string path;
        std::string name;
        std::string address;
        bool powered = false;
    };
    struct Device {
        std::string path;
        std::string name;
        std::string address;
        std::string icon;
        bool connected = false;
        bool paired = false;
        bool trusted = false;
        double battery = -1;
    };

    void start();
    void stop();
    void refresh();
    // Reads "a{sa{sv}}" (interfaces with properties) for `path`.
    void readInterfaces(sd_bus_message* message, const std::string& path);
    void applyProperties(sd_bus_message* message, const std::string& path, std::string_view interface);
    Device& deviceFor(const std::string& path);
    Adapter& adapterFor(const std::string& path);

    static int handleManagedObjects(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleInterfacesAdded(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleInterfacesRemoved(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handlePropertiesChanged(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleOwnerChanged(sd_bus_message* message, void* userData, sd_bus_error* error);

    SystemBus& systemBus_;
    bool available_ = false;
    std::vector<Adapter> adapters_;
    std::vector<Device> devices_;
    std::vector<SlotHandle> matches_;
    SlotHandle managedObjectsCall_;
    mutable std::vector<const Device*> ordered_; // reused by encode()
};

} // namespace readout

#pragma once

#include "bus/bus_util.h"
#include "common/paths.h"
#include "modules/module.h"

#include <list>
#include <string>
#include <vector>

namespace readout {

// Batteries and AC state from UPower. Event-driven: nothing is polled.
// Also lists peripheral batteries (mouse, keyboard, headset) that UPower knows about.
class BatteryModule final : public Module {
public:
    BatteryModule(const SystemPaths& paths, SystemBus& systemBus);
    ~BatteryModule() override;
    BatteryModule(const BatteryModule&) = delete;
    BatteryModule& operator=(const BatteryModule&) = delete;
    BatteryModule(BatteryModule&&) = delete;
    BatteryModule& operator=(BatteryModule&&) = delete;

    [[nodiscard]] const char* name() const override { return "battery"; }
    void setTier(Tier tier) override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }

    static const char* stateName(std::uint32_t state);
    static const char* kindName(std::uint32_t type);
    static const char* technologyName(std::uint32_t technology);

    struct Device {
        std::string path;
        std::uint32_t type = 0;
        bool powerSupply = false;
        bool present = false;
        bool online = false;
        std::string vendor;
        std::string model;
        double percentage = -1;
        std::uint32_t state = 0;
        std::int64_t timeToEmpty = 0;
        std::int64_t timeToFull = 0;
        double energyRate = 0;
        double energy = 0;
        double energyFull = 0;
        double energyFullDesign = 0;
        double voltage = 0;
        double capacity = 0; // health percent
        double temperature = 0;
        std::int64_t cycles = -1;
        std::uint32_t technology = 0;
    };
    // Applies one property to a device. Exposed for tests.
    static void applyProperty(Device& device, std::string_view key, VariantValue& value);

private:
    struct PendingCall {
        BatteryModule* module = nullptr;
        std::string path;
        SlotHandle slot;
    };

    void start();
    void stop();
    void refreshAll();
    void requestDevice(const std::string& path);
    Device* findDevice(std::string_view path);
    [[nodiscard]] bool isSystemBattery(const Device& device) const;

    static int handleManager(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleEnumerate(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleDeviceProperties(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handlePropertiesChanged(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleDeviceAdded(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleDeviceRemoved(sd_bus_message* message, void* userData, sd_bus_error* error);
    static int handleOwnerChanged(sd_bus_message* message, void* userData, sd_bus_error* error);

    SystemPaths paths_;
    SystemBus& systemBus_;
    bool available_ = false;
    bool onBattery_ = false;
    Device display_;
    std::vector<Device> devices_;
    std::list<PendingCall> pending_;
    std::vector<SlotHandle> matches_;
    SlotHandle managerCall_;
    SlotHandle enumerateCall_;

    HistoryBuffer levelHistory_;
    std::array<HistorySeries, 1> historySeries_{};
};

} // namespace readout

#pragma once

#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"

#include <string>
#include <vector>

namespace readout {

// hwmon sensors: temperatures, voltages, power, current and fans (read only).
//
// Cost rules:
// - Panel tier reads only the sensors panel subscribers selected, plus the CPU and GPU summary sensors.
// - Detail tier reads everything.
// - Sensors on a runtime-suspended PCI device are never read (reading would wake it).
// - Sensors on a secondary GPU are only read in the detail tier (reading resets its autosuspend timer).
class SensorsModule final : public Module {
public:
    explicit SensorsModule(const SystemPaths& paths);

    [[nodiscard]] const char* name() const override { return "sensors"; }
    void setTier(Tier tier) override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    void setSelectedSensors(std::span<const std::string> sensorIds) override;
    void resetBaselines() override;

    enum class Kind : std::uint8_t { Temperature, Voltage, Power, Current, Fan, Energy };
    static const char* groupForChip(std::string_view chipName);

private:
    struct Chip {
        std::string stableId;
        std::string name;
        std::string displayName;
        std::string group;
        HeldFile runtimeStatus;
        bool secondaryGpu = false;
        bool suspended = false;
    };
    struct Sensor {
        std::string id;    // "<chip stable id>/<attribute>"
        std::string label;
        std::string path;
        std::size_t chip = 0;
        Kind kind = Kind::Temperature;
        HeldFile file;
        double value = 0;
        bool valid = false;
        bool selected = false;
        bool summary = false; // used for cpuTemperature / gpuTemperature
        // Energy counters are reported as power.
        std::uint64_t lastEnergy = 0;
        std::uint64_t lastTime = 0;
    };

    void discover();
    void readSensor(Sensor& sensor, std::uint64_t now);
    static const char* kindName(Kind kind);
    static const char* unitName(Kind kind);
    void writeSensor(DictWriter& item, const Sensor& sensor) const;

    SystemPaths paths_;
    std::vector<Chip> chips_;
    std::vector<Sensor> sensors_;
    std::vector<std::string> selectedIds_;
    bool discovered_ = false;
    int cpuSummary_ = -1;
    int gpuSummary_ = -1;
    double maxTemperature_ = 0;
    bool hasMaxTemperature_ = false;
};

} // namespace readout

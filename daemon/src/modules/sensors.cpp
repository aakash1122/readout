#include "modules/sensors.h"

#include "common/text.h"
#include "io/sysfs.h"

#include <algorithm>
#include <array>

namespace readout {

namespace {

bool startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes) {
    return std::any_of(prefixes.begin(), prefixes.end(), [text](std::string_view prefix) { return text.starts_with(prefix); });
}

} // namespace

const char* SensorsModule::groupForChip(std::string_view chipName) {
    if (startsWithAny(chipName, {"k10temp", "coretemp", "zenpower", "cpu_thermal", "cpu-thermal", "via_cputemp"})) {
        return "CPU";
    }
    if (startsWithAny(chipName, {"amdgpu", "radeon", "xe", "i915", "nouveau", "nvidia"})) {
        return "GPU";
    }
    if (startsWithAny(chipName, {"nvme", "drivetemp"})) {
        return "Storage";
    }
    if (startsWithAny(chipName, {"spd5118", "jc42", "ee1004"})) {
        return "Memory";
    }
    if (startsWithAny(chipName, {"iwlwifi", "ath", "mt79", "mt76", "rtw", "brcmfmac"})) {
        return "Wireless";
    }
    if (startsWithAny(chipName, {"r8169", "igc", "e1000", "ixgbe", "atlantic", "bnxt", "mlx"})) {
        return "Network";
    }
    if (startsWithAny(chipName, {"BAT", "ADP", "AC", "ucsi", "macsmc_battery"})) {
        return "Power";
    }
    return "System";
}

const char* SensorsModule::kindName(Kind kind) {
    switch (kind) {
        case Kind::Temperature:
            return "temperature";
        case Kind::Voltage:
            return "voltage";
        case Kind::Power:
        case Kind::Energy:
            return "power";
        case Kind::Current:
            return "current";
        case Kind::Fan:
            return "fan";
    }
    return "temperature";
}

const char* SensorsModule::unitName(Kind kind) {
    switch (kind) {
        case Kind::Temperature:
            return "°C";
        case Kind::Voltage:
            return "V";
        case Kind::Power:
        case Kind::Energy:
            return "W";
        case Kind::Current:
            return "A";
        case Kind::Fan:
            return "RPM";
    }
    return "";
}

SensorsModule::SensorsModule(const SystemPaths& paths) : paths_(paths) {}

void SensorsModule::discover() {
    chips_.clear();
    sensors_.clear();
    cpuSummary_ = -1;
    gpuSummary_ = -1;
    for (const HwmonChip& hwmon : listHwmonChips(paths_)) {
        Chip chip;
        chip.stableId = hwmon.stableId;
        chip.name = hwmon.name;
        chip.group = groupForChip(hwmon.name);
        chip.displayName = hwmon.name;
        if (!hwmon.devicePath.empty()) {
            // NVMe controllers and some other devices carry a model name.
            const auto model = readTextFile(hwmon.devicePath + "/model");
            if (model && !trim(*model).empty()) {
                std::array<char, 96> text{};
                copySanitizedUtf8(trim(*model), text.data(), text.size());
                chip.displayName = text.data();
            }
            chip.runtimeStatus.open(hwmon.devicePath + "/power/runtime_status");
            chip.secondaryGpu = isSecondaryGpu(paths_, hwmon.devicePath);
        }
        const std::size_t chipIndex = chips_.size();
        chips_.push_back(std::move(chip));
        for (const HwmonInput& input : hwmon.inputs) {
            Sensor sensor;
            sensor.id = hwmon.stableId + "/" + input.attribute;
            sensor.label = input.label.empty() ? input.attribute : input.label;
            sensor.path = hwmon.directory + "/" + input.inputFile;
            sensor.chip = chipIndex;
            if (input.kind == "temp") {
                sensor.kind = Kind::Temperature;
            } else if (input.kind == "in") {
                sensor.kind = Kind::Voltage;
            } else if (input.kind == "power") {
                sensor.kind = Kind::Power;
            } else if (input.kind == "curr") {
                sensor.kind = Kind::Current;
            } else if (input.kind == "fan") {
                sensor.kind = Kind::Fan;
            } else {
                sensor.kind = Kind::Energy;
            }
            sensors_.push_back(std::move(sensor));
        }
    }
    // Summary sensors: the CPU package/die temperature and the first GPU's main temperature.
    const auto pickSummary = [this](std::string_view group, std::initializer_list<std::string_view> labels) -> int {
        for (const std::string_view wanted : labels) {
            for (std::size_t index = 0; index < sensors_.size(); ++index) {
                const Sensor& sensor = sensors_[index];
                const Chip& chip = chips_[sensor.chip];
                if (chip.group == group && sensor.kind == Kind::Temperature && !chip.secondaryGpu &&
                    (wanted.empty() || sensor.label == wanted)) {
                    return static_cast<int>(index);
                }
            }
        }
        return -1;
    };
    cpuSummary_ = pickSummary("CPU", {"Tdie", "Tctl", "Package id 0", ""});
    gpuSummary_ = pickSummary("GPU", {"junction", "edge", "pkg", ""});
    if (cpuSummary_ >= 0) {
        sensors_[static_cast<std::size_t>(cpuSummary_)].summary = true;
    }
    if (gpuSummary_ >= 0) {
        sensors_[static_cast<std::size_t>(gpuSummary_)].summary = true;
    }
    for (Sensor& sensor : sensors_) {
        sensor.selected = std::find(selectedIds_.begin(), selectedIds_.end(), sensor.id) != selectedIds_.end();
    }
    discovered_ = true;
}

void SensorsModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (tier == Tier::None) {
        chips_.clear();
        sensors_.clear();
        discovered_ = false;
        return;
    }
    if (previous == Tier::None || !discovered_) {
        discover();
    }
}

void SensorsModule::setSelectedSensors(std::span<const std::string> sensorIds) {
    selectedIds_.assign(sensorIds.begin(), sensorIds.end());
    for (Sensor& sensor : sensors_) {
        sensor.selected = std::find(selectedIds_.begin(), selectedIds_.end(), sensor.id) != selectedIds_.end();
    }
}

void SensorsModule::resetBaselines() {
    for (Sensor& sensor : sensors_) {
        sensor.lastTime = 0;
    }
}

void SensorsModule::readSensor(Sensor& sensor, std::uint64_t now) {
    if (!sensor.file.isOpen() && !sensor.file.open(sensor.path)) {
        sensor.valid = false;
        return;
    }
    const auto raw = sensor.file.readInteger();
    if (!raw) {
        sensor.valid = false;
        return;
    }
    const auto value = static_cast<double>(*raw);
    switch (sensor.kind) {
        case Kind::Temperature:
            sensor.value = value / 1000.0;
            // Drivers report sentinel values for absent probes.
            sensor.valid = sensor.value > -100.0 && sensor.value < 250.0;
            return;
        case Kind::Voltage:
            sensor.value = value / 1000.0;
            break;
        case Kind::Power:
            sensor.value = value / 1e6;
            break;
        case Kind::Current:
            sensor.value = value / 1000.0;
            break;
        case Kind::Fan:
            sensor.value = value;
            break;
        case Kind::Energy: {
            const auto energy = static_cast<std::uint64_t>(*raw);
            if (sensor.lastTime != 0 && now > sensor.lastTime && energy >= sensor.lastEnergy) {
                // microjoules per microsecond = watts
                sensor.value = static_cast<double>(energy - sensor.lastEnergy) / static_cast<double>(now - sensor.lastTime);
                sensor.valid = true;
            } else {
                sensor.valid = false;
            }
            sensor.lastEnergy = energy;
            sensor.lastTime = now;
            return;
        }
    }
    sensor.valid = true;
}

void SensorsModule::sample(const SampleContext& context) {
    for (Chip& chip : chips_) {
        std::array<char, 32> status{};
        chip.suspended = chip.runtimeStatus.isOpen() && chip.runtimeStatus.readSmallText(status.data(), status.size()) &&
                         std::string_view(status.data()) == "suspended";
    }
    const bool detail = context.tier == Tier::Detail;
    maxTemperature_ = 0;
    hasMaxTemperature_ = false;
    for (Sensor& sensor : sensors_) {
        const Chip& chip = chips_[sensor.chip];
        const bool wanted = detail || sensor.selected || sensor.summary;
        const bool allowed = !chip.suspended && (detail || !chip.secondaryGpu);
        if (!wanted || !allowed) {
            sensor.valid = false;
            continue;
        }
        readSensor(sensor, context.nowMicroseconds);
        if (sensor.valid && sensor.kind == Kind::Temperature &&
            (!hasMaxTemperature_ || sensor.value > maxTemperature_)) {
            maxTemperature_ = sensor.value;
            hasMaxTemperature_ = true;
        }
    }
}

void SensorsModule::writeSensor(DictWriter& item, const Sensor& sensor) const {
    const Chip& chip = chips_[sensor.chip];
    item.text("id", sensor.id.c_str());
    item.text("label", sensor.label.c_str());
    item.text("chip", chip.displayName.c_str());
    item.text("group", chip.group.c_str());
    item.text("kind", kindName(sensor.kind));
    item.text("unit", unitName(sensor.kind));
    item.flag("valid", sensor.valid);
    item.flag("sleeping", chip.suspended);
    item.number("value", sensor.valid ? sensor.value : 0);
}

void SensorsModule::encode(DictWriter& writer, const EncodeContext& context) const {
    const auto summary = [this](int index) -> const Sensor* {
        if (index < 0) {
            return nullptr;
        }
        const Sensor& sensor = sensors_[static_cast<std::size_t>(index)];
        return sensor.valid ? &sensor : nullptr;
    };
    if (const Sensor* cpu = summary(cpuSummary_)) {
        writer.number("cpuTemperature", cpu->value);
    }
    if (const Sensor* gpu = summary(gpuSummary_)) {
        writer.number("gpuTemperature", gpu->value);
    }
    if (hasMaxTemperature_) {
        writer.number("maxTemperature", maxTemperature_);
    }
    writer.list("sensors", [&](ListWriter& items) {
        for (const Sensor& sensor : sensors_) {
            const bool include =
                context.tier == Tier::Detail ||
                std::find(context.sensorIds.begin(), context.sensorIds.end(), sensor.id) != context.sensorIds.end();
            if (include) {
                items.item([&](DictWriter& item) { writeSensor(item, sensor); });
            }
        }
    });
}

void SensorsModule::encodeInfo(DictWriter& writer) {
    const bool wasDiscovered = discovered_;
    if (!wasDiscovered) {
        discover();
    }
    writer.list("sensors", [this](ListWriter& items) {
        for (const Sensor& sensor : sensors_) {
            items.item([&](DictWriter& item) {
                const Chip& chip = chips_[sensor.chip];
                item.text("id", sensor.id.c_str());
                item.text("label", sensor.label.c_str());
                item.text("chip", chip.displayName.c_str());
                item.text("group", chip.group.c_str());
                item.text("kind", kindName(sensor.kind));
                item.text("unit", unitName(sensor.kind));
            });
        }
    });
    if (!wasDiscovered) {
        chips_.clear();
        sensors_.clear();
        discovered_ = false;
    }
}

} // namespace readout

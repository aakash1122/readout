#include "modules/gpu.h"

#include "common/log.h"
#include "common/text.h"
#include "io/sysfs.h"
#include "parsers/kernel_text.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>

namespace readout {

// ---- NVML (loaded at runtime, never required at build time) ----------------------------------

class NvmlLibrary {
public:
    using Device = void*;
    struct Utilization {
        unsigned int gpu;
        unsigned int memory;
    };
    struct Memory {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    NvmlLibrary() = default;
    ~NvmlLibrary() { unload(); }
    NvmlLibrary(const NvmlLibrary&) = delete;
    NvmlLibrary& operator=(const NvmlLibrary&) = delete;
    NvmlLibrary(NvmlLibrary&&) = delete;
    NvmlLibrary& operator=(NvmlLibrary&&) = delete;

    bool load() {
        if (initialized_) {
            return true;
        }
        if (failed_) {
            return false;
        }
        handle_ = ::dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            failed_ = true;
            logInfo("NVML not available; NVIDIA GPUs report only their power state");
            return false;
        }
        const bool resolved = resolve(init_, "nvmlInit_v2") && resolve(shutdown_, "nvmlShutdown") &&
                              resolve(byPciBusId_, "nvmlDeviceGetHandleByPciBusId_v2") &&
                              resolve(utilization_, "nvmlDeviceGetUtilizationRates") &&
                              resolve(temperature_, "nvmlDeviceGetTemperature") &&
                              resolve(memory_, "nvmlDeviceGetMemoryInfo") &&
                              resolve(power_, "nvmlDeviceGetPowerUsage") &&
                              resolve(clock_, "nvmlDeviceGetClockInfo") &&
                              resolve(fanSpeed_, "nvmlDeviceGetFanSpeed") && resolve(name_, "nvmlDeviceGetName");
        resolve(maxClock_, "nvmlDeviceGetMaxClockInfo");
        resolve(powerLimit_, "nvmlDeviceGetEnforcedPowerLimit");
        if (!resolved || init_() != 0) {
            failed_ = true;
            unload();
            return false;
        }
        initialized_ = true;
        return true;
    }

    void unload() {
        if (initialized_ && shutdown_ != nullptr) {
            shutdown_();
        }
        initialized_ = false;
        if (handle_ != nullptr) {
            ::dlclose(handle_);
            handle_ = nullptr;
        }
    }

    [[nodiscard]] Device device(const std::string& pciSlot) const {
        Device device = nullptr;
        return byPciBusId_(pciSlot.c_str(), &device) == 0 ? device : nullptr;
    }
    bool utilization(Device device, Utilization& value) const { return utilization_(device, &value) == 0; }
    bool temperature(Device device, unsigned int& value) const { return temperature_(device, 0, &value) == 0; }
    bool memory(Device device, Memory& value) const { return memory_(device, &value) == 0; }
    bool powerMilliwatts(Device device, unsigned int& value) const { return power_(device, &value) == 0; }
    bool clockMegahertz(Device device, int type, unsigned int& value) const { return clock_(device, type, &value) == 0; }
    bool fanPercent(Device device, unsigned int& value) const { return fanSpeed_(device, &value) == 0; }
    // Optional: older drivers may lack them.
    bool maxClockMegahertz(Device device, int type, unsigned int& value) const {
        return maxClock_ != nullptr && maxClock_(device, type, &value) == 0;
    }
    bool powerLimitMilliwatts(Device device, unsigned int& value) const {
        return powerLimit_ != nullptr && powerLimit_(device, &value) == 0;
    }
    std::string name(Device device) const {
        std::array<char, 96> text{};
        if (name_(device, text.data(), static_cast<unsigned int>(text.size())) != 0) {
            return {};
        }
        return text.data();
    }

private:
    template <typename Function>
    bool resolve(Function& target, const char* symbol) {
        target = reinterpret_cast<Function>(::dlsym(handle_, symbol));
        return target != nullptr;
    }

    void* handle_ = nullptr;
    bool initialized_ = false;
    bool failed_ = false;
    int (*init_)() = nullptr;
    int (*shutdown_)() = nullptr;
    int (*byPciBusId_)(const char*, Device*) = nullptr;
    int (*utilization_)(Device, Utilization*) = nullptr;
    int (*temperature_)(Device, int, unsigned int*) = nullptr;
    int (*memory_)(Device, Memory*) = nullptr;
    int (*power_)(Device, unsigned int*) = nullptr;
    int (*clock_)(Device, int, unsigned int*) = nullptr;
    int (*fanSpeed_)(Device, unsigned int*) = nullptr;
    int (*name_)(Device, char*, unsigned int) = nullptr;
    int (*maxClock_)(Device, int, unsigned int*) = nullptr;
    int (*powerLimit_)(Device, unsigned int*) = nullptr;
};

// ---- Module -----------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t vendorAmd = 0x1002;
constexpr std::uint32_t vendorIntel = 0x8086;
constexpr std::uint32_t vendorNvidia = 0x10de;

const char* vendorName(std::uint32_t vendorId) {
    switch (vendorId) {
        case vendorAmd:
            return "AMD";
        case vendorIntel:
            return "Intel";
        case vendorNvidia:
            return "NVIDIA";
        default:
            return "";
    }
}

// Finds hwmon/hwmonN below a device directory.
std::string findHwmon(const std::string& devicePath) {
    for (const std::string& entry : listDirectory(devicePath + "/hwmon")) {
        if (entry.starts_with("hwmon")) {
            return devicePath + "/hwmon/" + entry;
        }
    }
    return {};
}

// Opens the first temperature with one of `labels` (in order), else temp1_input.
void openLabeledTemperature(HeldFile& file, const std::string& hwmon, std::initializer_list<std::string_view> labels) {
    if (hwmon.empty()) {
        return;
    }
    const std::vector<std::string> files = listDirectory(hwmon);
    for (const std::string_view wanted : labels) {
        for (const std::string& entry : files) {
            if (!entry.starts_with("temp") || !entry.ends_with("_label")) {
                continue;
            }
            const auto label = readTextFile(hwmon + "/" + entry);
            if (label && trim(*label) == wanted) {
                const std::string input = entry.substr(0, entry.size() - 6) + "_input";
                if (file.open(hwmon + "/" + input)) {
                    return;
                }
            }
        }
    }
    file.open(hwmon + "/temp1_input");
}

// A whole-number MHz file (xe rp0_freq, i915 gt_RP0_freq_mhz), or -1.
double readMegahertz(const std::string& path) {
    const auto text = readTextFile(path);
    const auto megahertz = text ? parseNumber<std::int64_t>(*text) : std::nullopt;
    return megahertz && *megahertz > 0 ? static_cast<double>(*megahertz) : -1;
}

} // namespace

std::string GpuModule::friendlyName(std::string_view vendorText, std::string_view deviceName, std::uint32_t vendorId) {
    const char* shortVendor = vendorName(vendorId);
    std::string_view model = deviceName;
    const std::size_t open = deviceName.find('[');
    const std::size_t close = deviceName.rfind(']');
    if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
        model = deviceName.substr(open + 1, close - open - 1);
    }
    if (model.empty()) {
        return std::string(vendorText.empty() ? "GPU" : vendorText);
    }
    if (shortVendor[0] == '\0') {
        return std::string(model);
    }
    std::string result = shortVendor;
    result += ' ';
    result += model;
    return result;
}

GpuModule::GpuModule(const SystemPaths& paths) : paths_(paths) {}

GpuModule::~GpuModule() = default;

void GpuModule::discover() {
    gpus_.clear();
    historySeries_.clear();
    std::optional<std::string> pciIds = readTextFile(paths_.pciIds);
    if (!pciIds) {
        pciIds = readTextFile(paths_.pciIdsFallback);
    }
    const std::string drm = paths_.sys + "/class/drm";
    for (const std::string& entry : listDirectory(drm)) {
        if (!entry.starts_with("card") || entry.contains('-')) {
            continue;
        }
        auto gpu = std::make_unique<Gpu>();
        gpu->cardPath = drm + "/" + entry;
        gpu->devicePath = resolvePath(gpu->cardPath + "/device");
        if (gpu->devicePath.empty() || !isPciDisplayDevice(gpu->devicePath)) {
            continue;
        }
        gpu->id = baseName(gpu->devicePath);
        gpu->driver = baseName(resolvePath(gpu->devicePath + "/driver"));
        const auto vendorText = readTextFile(gpu->devicePath + "/vendor");
        const auto deviceText = readTextFile(gpu->devicePath + "/device");
        const std::uint32_t vendorId = vendorText ? parseHex(*vendorText).value_or(0) : 0;
        const std::uint32_t deviceId = deviceText ? parseHex(*deviceText).value_or(0) : 0;
        gpu->vendor = vendorId == vendorAmd      ? Vendor::Amd
                      : vendorId == vendorIntel  ? Vendor::Intel
                      : vendorId == vendorNvidia ? Vendor::Nvidia
                                                 : Vendor::Other;
        PciName pciName;
        if (pciIds) {
            pciName = lookupPciName(*pciIds, vendorId, deviceId);
        }
        gpu->name = friendlyName(pciName.vendor, pciName.device, vendorId);
        if (gpu->name == "GPU" && vendorName(vendorId)[0] != '\0') {
            gpu->name = std::string(vendorName(vendorId)) + " GPU";
        }
        gpu->secondary = isSecondaryGpu(paths_, gpu->devicePath);
        gpu->historyName = gpu->id + "/usage";
        gpus_.push_back(std::move(gpu));
    }
    for (const auto& gpu : gpus_) {
        historySeries_.push_back(HistorySeries{gpu->historyName.c_str(), &gpu->usageHistory});
    }
    discovered_ = true;
}

double GpuModule::highestDpmMegahertz(std::string_view table) {
    double highest = -1;
    while (!table.empty()) {
        const std::size_t end = table.find('\n');
        std::string_view line = table.substr(0, end);
        table = end == std::string_view::npos ? std::string_view{} : table.substr(end + 1);
        // "1: 2500Mhz *": the number between the colon and "Mhz".
        const std::size_t colon = line.find(':');
        const std::size_t unit = line.find("Mhz");
        if (colon == std::string_view::npos || unit == std::string_view::npos || unit < colon) {
            continue;
        }
        if (const auto megahertz = parseNumber<std::int64_t>(line.substr(colon + 1, unit - colon - 1))) {
            highest = std::max(highest, static_cast<double>(*megahertz));
        }
    }
    return highest > 0 ? highest : -1;
}

void GpuModule::openFiles(Gpu& gpu) {
    const std::string& device = gpu.devicePath;
    const std::string hwmon = findHwmon(device);
    switch (gpu.vendor) {
        case Vendor::Amd:
            gpu.busyPercent.open(device + "/gpu_busy_percent");
            gpu.vramUsed.open(device + "/mem_info_vram_used");
            gpu.vramTotal.open(device + "/mem_info_vram_total");
            if (const auto table = readTextFile(device + "/pp_dpm_sclk")) {
                gpu.maxCoreClockMegahertz = highestDpmMegahertz(*table);
            }
            if (!hwmon.empty()) {
                gpu.coreClock.open(hwmon + "/freq1_input");
                gpu.memoryClock.open(hwmon + "/freq2_input");
                if (!gpu.power.open(hwmon + "/power1_average")) {
                    gpu.power.open(hwmon + "/power1_input");
                }
                gpu.fan.open(hwmon + "/fan1_input");
            }
            openLabeledTemperature(gpu.temperature, hwmon, {"edge", "junction"});
            break;
        case Vendor::Intel: {
            // xe: tileN/gtN/gtidle with name "gtN-rc" is the render engine.
            bool found = false;
            for (const std::string& tile : listDirectory(device)) {
                if (!tile.starts_with("tile")) {
                    continue;
                }
                for (const std::string& gt : listDirectory(device + "/" + tile)) {
                    const std::string gtPath = device + "/" + tile + "/" + gt;
                    const auto idleName = readTextFile(gtPath + "/gtidle/name");
                    if (!idleName || !trim(*idleName).ends_with("-rc")) {
                        continue;
                    }
                    found = gpu.idleResidency.open(gtPath + "/gtidle/idle_residency_ms");
                    gpu.actualFrequency.open(gtPath + "/freq0/act_freq");
                    gpu.maxCoreClockMegahertz = readMegahertz(gtPath + "/freq0/rp0_freq");
                    break;
                }
                if (found) {
                    break;
                }
            }
            if (!found) {
                // i915
                if (!gpu.idleResidency.open(gpu.cardPath + "/gt/gt0/rc6_residency_ms")) {
                    gpu.idleResidency.open(gpu.cardPath + "/power/rc6_residency_ms");
                }
                if (!gpu.actualFrequency.open(gpu.cardPath + "/gt/gt0/rps_act_freq_mhz")) {
                    gpu.actualFrequency.open(gpu.cardPath + "/gt_act_freq_mhz");
                }
                gpu.maxCoreClockMegahertz = readMegahertz(gpu.cardPath + "/gt_RP0_freq_mhz");
            }
            if (!hwmon.empty()) {
                gpu.energy.open(hwmon + "/energy1_input");
                gpu.fan.open(hwmon + "/fan1_input");
                openLabeledTemperature(gpu.temperature, hwmon, {"pkg"});
            }
            break;
        }
        case Vendor::Nvidia:
            if (!hwmon.empty()) { // nouveau
                openLabeledTemperature(gpu.temperature, hwmon, {});
                gpu.fan.open(hwmon + "/fan1_input");
                gpu.power.open(hwmon + "/power1_input");
            }
            break;
        case Vendor::Other:
            openLabeledTemperature(gpu.temperature, hwmon, {});
            break;
    }
    if (!hwmon.empty()) {
        // power1_cap is the sustained power limit in microwatts (amdgpu, xe, i915, nouveau).
        if (const auto text = readTextFile(hwmon + "/power1_cap")) {
            const auto microwatts = parseNumber<std::int64_t>(*text);
            gpu.powerLimitWatts = microwatts && *microwatts > 0 ? static_cast<double>(*microwatts) / 1e6 : -1;
        }
    }
    gpu.filesOpen = true;
}

void GpuModule::closeFiles(Gpu& gpu) {
    for (HeldFile* file : {&gpu.busyPercent, &gpu.vramUsed, &gpu.vramTotal, &gpu.idleResidency, &gpu.actualFrequency,
                           &gpu.coreClock, &gpu.memoryClock, &gpu.temperature, &gpu.power, &gpu.energy, &gpu.fan}) {
        file->close();
    }
    gpu.filesOpen = false;
    gpu.lastIdleTime = 0;
    gpu.lastEnergyTime = 0;
}

void GpuModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (!discovered_) {
        discover();
    }
    if (tier == Tier::None) {
        for (const auto& gpu : gpus_) {
            closeFiles(*gpu);
            gpu->runtimeStatus.close();
        }
        nvml_.reset();
        return;
    }
    for (const auto& gpu : gpus_) {
        if (!gpu->runtimeStatus.isOpen()) {
            gpu->runtimeStatus.open(gpu->devicePath + "/power/runtime_status");
        }
        // Leaving detail: stop touching secondary GPUs so they can suspend.
        if (previous == Tier::Detail && tier == Tier::Panel && gpu->secondary) {
            closeFiles(*gpu);
            gpu->sampled = false;
            if (gpu->vendor == Vendor::Nvidia) {
                nvml_.reset();
            }
        }
    }
}

void GpuModule::resetBaselines() {
    for (const auto& gpu : gpus_) {
        gpu->lastIdleTime = 0;
        gpu->lastEnergyTime = 0;
    }
}

void GpuModule::sample(const SampleContext& context) {
    for (const auto& gpu : gpus_) {
        std::array<char, 32> status{};
        gpu->suspended = gpu->runtimeStatus.isOpen() && gpu->runtimeStatus.readSmallText(status.data(), status.size()) &&
                         std::string_view(status.data()) == "suspended";
        const bool allowed = !gpu->suspended && (context.tier == Tier::Detail || !gpu->secondary);
        if (!allowed) {
            gpu->sampled = false;
            gpu->lastIdleTime = 0;
            gpu->lastEnergyTime = 0;
            continue;
        }
        sampleGpu(*gpu, context.nowMicroseconds);
        if (gpu->usage >= 0) {
            gpu->usageHistory.push(context.nowMicroseconds, gpu->usage);
        }
    }
}

void GpuModule::sampleGpu(Gpu& gpu, std::uint64_t now) {
    if (!gpu.filesOpen) {
        openFiles(gpu);
    }
    gpu.sampled = true;
    gpu.usage = -1;
    if (gpu.vendor == Vendor::Nvidia && gpu.driver == "nvidia") {
        sampleNvidia(gpu);
        return;
    }
    if (const auto busy = gpu.busyPercent.isOpen() ? gpu.busyPercent.readInteger() : std::nullopt) {
        gpu.usage = std::clamp(static_cast<double>(*busy), 0.0, 100.0);
    }
    if (gpu.idleResidency.isOpen()) {
        const auto idle = gpu.idleResidency.readInteger();
        if (idle) {
            const auto idleMilliseconds = static_cast<std::uint64_t>(*idle);
            if (gpu.lastIdleTime != 0 && now > gpu.lastIdleTime && idleMilliseconds >= gpu.lastIdle) {
                const double elapsedMilliseconds = static_cast<double>(now - gpu.lastIdleTime) / 1000.0;
                const double idleShare = static_cast<double>(idleMilliseconds - gpu.lastIdle) / elapsedMilliseconds;
                gpu.usage = std::clamp((1.0 - idleShare) * 100.0, 0.0, 100.0);
            }
            gpu.lastIdle = idleMilliseconds;
            gpu.lastIdleTime = now;
        }
    }
    const auto read = [](HeldFile& file) { return file.isOpen() ? file.readInteger() : std::nullopt; };
    if (const auto used = read(gpu.vramUsed)) {
        gpu.memoryUsedBytes = static_cast<std::uint64_t>(*used);
    }
    if (gpu.memoryTotalBytes == 0) {
        if (const auto total = read(gpu.vramTotal)) {
            gpu.memoryTotalBytes = static_cast<std::uint64_t>(*total);
        }
    }
    const auto temperature = read(gpu.temperature);
    gpu.temperatureValue = temperature ? static_cast<double>(*temperature) / 1000.0 : -1;
    if (const auto hertz = read(gpu.coreClock)) {
        gpu.coreClockMegahertz = static_cast<double>(*hertz) / 1e6;
    } else if (const auto megahertz = read(gpu.actualFrequency)) {
        // xe/i915 report 0 while the GT is in RC6; that means "idle", not a clock speed.
        gpu.coreClockMegahertz = *megahertz > 0 ? static_cast<double>(*megahertz) : -1;
    }
    if (const auto hertz = read(gpu.memoryClock)) {
        gpu.memoryClockMegahertz = static_cast<double>(*hertz) / 1e6;
    }
    if (const auto microwatts = read(gpu.power)) {
        gpu.powerWatts = static_cast<double>(*microwatts) / 1e6;
    } else if (const auto energy = read(gpu.energy)) {
        const auto microjoules = static_cast<std::uint64_t>(*energy);
        if (gpu.lastEnergyTime != 0 && now > gpu.lastEnergyTime && microjoules >= gpu.lastEnergy) {
            gpu.powerWatts = static_cast<double>(microjoules - gpu.lastEnergy) / static_cast<double>(now - gpu.lastEnergyTime);
        }
        gpu.lastEnergy = microjoules;
        gpu.lastEnergyTime = now;
    }
    if (const auto rpm = read(gpu.fan)) {
        gpu.fanRpm = static_cast<double>(*rpm);
    }
}

void GpuModule::sampleNvidia(Gpu& gpu) {
    if (!nvml_) {
        nvml_ = std::make_unique<NvmlLibrary>();
    }
    if (!nvml_->load()) {
        return;
    }
    const NvmlLibrary::Device device = nvml_->device(gpu.id);
    if (device == nullptr) {
        return;
    }
    NvmlLibrary::Utilization utilization{};
    if (nvml_->utilization(device, utilization)) {
        gpu.usage = utilization.gpu;
    }
    unsigned int value = 0;
    gpu.temperatureValue = nvml_->temperature(device, value) ? value : -1;
    NvmlLibrary::Memory memory{};
    if (nvml_->memory(device, memory)) {
        gpu.memoryUsedBytes = memory.used;
        gpu.memoryTotalBytes = memory.total;
    }
    gpu.powerWatts = nvml_->powerMilliwatts(device, value) ? value / 1000.0 : -1;
    gpu.coreClockMegahertz = nvml_->clockMegahertz(device, 0, value) ? value : -1;
    gpu.memoryClockMegahertz = nvml_->clockMegahertz(device, 2, value) ? value : -1;
    gpu.fanPercent = nvml_->fanPercent(device, value) ? value : -1;
    if (gpu.maxCoreClockMegahertz < 0 && nvml_->maxClockMegahertz(device, 0, value)) {
        gpu.maxCoreClockMegahertz = value;
    }
    if (gpu.powerLimitWatts < 0 && nvml_->powerLimitMilliwatts(device, value) && value > 0) {
        gpu.powerLimitWatts = value / 1000.0;
    }
    const std::string name = nvml_->name(device);
    if (!name.empty() && name != gpu.name) {
        gpu.name = name.starts_with("NVIDIA") ? name : "NVIDIA " + name;
    }
}

void GpuModule::encode(DictWriter& writer, const EncodeContext& /*context*/) const {
    writer.list("gpus", [this](ListWriter& items) {
        for (const auto& gpuPointer : gpus_) {
            const Gpu& gpu = *gpuPointer;
            items.item([&gpu](DictWriter& item) {
                item.text("id", gpu.id.c_str());
                item.text("name", gpu.name.c_str());
                item.text("state", gpu.suspended ? "suspended" : gpu.sampled ? "active" : "idle");
                item.flag("secondary", gpu.secondary);
                if (!gpu.sampled) {
                    return;
                }
                if (gpu.usage >= 0) {
                    item.number("usage", gpu.usage);
                }
                if (gpu.temperatureValue >= 0) {
                    item.number("temperature", gpu.temperatureValue);
                }
                if (gpu.memoryTotalBytes > 0) {
                    item.count("memoryUsed", gpu.memoryUsedBytes);
                    item.count("memoryTotal", gpu.memoryTotalBytes);
                }
                if (gpu.coreClockMegahertz >= 0) {
                    item.number("coreClock", gpu.coreClockMegahertz);
                }
                if (gpu.memoryClockMegahertz >= 0) {
                    item.number("memoryClock", gpu.memoryClockMegahertz);
                }
                if (gpu.maxCoreClockMegahertz > 0) {
                    item.number("maxCoreClock", gpu.maxCoreClockMegahertz);
                }
                if (gpu.powerWatts >= 0) {
                    item.number("power", gpu.powerWatts);
                }
                if (gpu.powerLimitWatts > 0) {
                    item.number("powerLimit", gpu.powerLimitWatts);
                }
                if (gpu.fanRpm >= 0) {
                    item.number("fanRpm", gpu.fanRpm);
                }
                if (gpu.fanPercent >= 0) {
                    item.number("fanPercent", gpu.fanPercent);
                }
            });
        }
    });
}

void GpuModule::encodeInfo(DictWriter& writer) {
    if (!discovered_) {
        discover();
    }
    writer.list("gpus", [this](ListWriter& items) {
        for (const auto& gpu : gpus_) {
            items.item([&gpu](DictWriter& item) {
                item.text("id", gpu->id.c_str());
                item.text("name", gpu->name.c_str());
                item.text("driver", gpu->driver.c_str());
                item.text("vendor", gpu->vendor == Vendor::Amd      ? "AMD"
                                    : gpu->vendor == Vendor::Intel  ? "Intel"
                                    : gpu->vendor == Vendor::Nvidia ? "NVIDIA"
                                                                    : "");
                item.flag("secondary", gpu->secondary);
            });
        }
    });
}

} // namespace readout

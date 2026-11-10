#pragma once

#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"

#include <memory>
#include <string>
#include <vector>

namespace readout {

class NvmlLibrary;

// GPU usage, memory, clocks, temperature, power and fan.
//
// Power rules (never wake or keep awake a sleeping GPU):
// - power/runtime_status is checked before every read. A "suspended" GPU is reported as sleeping.
// - A secondary GPU (not the boot display device) is only queried in the detail tier, because each
//   read restarts its runtime-PM autosuspend timer.
class GpuModule final : public Module {
public:
    explicit GpuModule(const SystemPaths& paths);
    ~GpuModule() override;
    GpuModule(const GpuModule&) = delete;
    GpuModule& operator=(const GpuModule&) = delete;
    GpuModule(GpuModule&&) = delete;
    GpuModule& operator=(GpuModule&&) = delete;

    [[nodiscard]] const char* name() const override { return "gpu"; }
    void setTier(Tier tier) override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }
    void resetBaselines() override;

    // "Battlemage G21 [Arc B580]" from vendor "Intel Corporation" -> "Intel Arc B580".
    static std::string friendlyName(std::string_view vendorText, std::string_view deviceName, std::uint32_t vendorId);

    // Highest clock in an amdgpu pp_dpm_sclk table ("0: 500Mhz\n1: 2500Mhz *\n"), or -1.
    static double highestDpmMegahertz(std::string_view table);

private:
    enum class Vendor : std::uint8_t { Amd, Intel, Nvidia, Other };

    struct Gpu {
        std::string id; // PCI slot, e.g. 0000:03:00.0
        std::string name;
        std::string driver;
        std::string cardPath;
        std::string devicePath;
        Vendor vendor = Vendor::Other;
        bool secondary = false;

        HeldFile runtimeStatus;
        HeldFile busyPercent;   // amdgpu
        HeldFile vramUsed;      // amdgpu
        HeldFile vramTotal;     // amdgpu
        HeldFile idleResidency; // Intel: xe gtidle or i915 rc6 residency (ms)
        HeldFile actualFrequency; // Intel, MHz
        HeldFile coreClock;     // hwmon freq1_input (Hz)
        HeldFile memoryClock;   // hwmon freq2_input (Hz)
        HeldFile temperature;   // hwmon, millidegrees
        HeldFile power;         // hwmon power, microwatts
        HeldFile energy;        // hwmon energy, microjoules
        HeldFile fan;           // hwmon fan, RPM
        bool filesOpen = false;

        std::uint64_t lastIdle = 0;
        std::uint64_t lastIdleTime = 0;
        std::uint64_t lastEnergy = 0;
        std::uint64_t lastEnergyTime = 0;

        bool suspended = false;
        bool sampled = false;
        double usage = -1;
        double temperatureValue = -1;
        std::uint64_t memoryUsedBytes = 0;
        std::uint64_t memoryTotalBytes = 0;
        double coreClockMegahertz = -1;
        double memoryClockMegahertz = -1;
        double powerWatts = -1;
        double fanRpm = -1;
        double fanPercent = -1;
        // Read once when the files are opened; they do not change while the driver is loaded.
        double maxCoreClockMegahertz = -1;
        double powerLimitWatts = -1;

        std::string historyName;
        HistoryBuffer usageHistory;
    };

    void discover();
    void openFiles(Gpu& gpu);
    void closeFiles(Gpu& gpu);
    void sampleGpu(Gpu& gpu, std::uint64_t now);
    void sampleNvidia(Gpu& gpu);

    SystemPaths paths_;
    std::vector<std::unique_ptr<Gpu>> gpus_;
    std::vector<HistorySeries> historySeries_;
    bool discovered_ = false;
    std::unique_ptr<NvmlLibrary> nvml_;
};

} // namespace readout

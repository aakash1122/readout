#pragma once

#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"
#include "parsers/kernel_text.h"

#include <array>
#include <string>
#include <vector>

namespace readout {

class CpuModule final : public Module {
public:
    explicit CpuModule(const SystemPaths& paths);

    [[nodiscard]] const char* name() const override { return "cpu"; }
    void setTier(Tier tier) override;
    [[nodiscard]] ProcessNeeds processNeeds(Tier tier) const override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }
    void resetBaselines() override;

    static constexpr std::size_t topProcessCount = 10;

private:
    enum class CoreType : std::uint8_t { Unknown, Performance, Efficiency };

    void detectTopology();
    void detectTemperatureSensor();
    void openFrequencyFiles();
    void sampleFrequencies(std::uint64_t now);

    SystemPaths paths_;
    HeldFile statFile_;
    HeldFile loadFile_;
    HeldFile uptimeFile_;
    HeldFile temperatureFile_;
    std::string temperatureSource_;
    std::vector<char> buffer_;

    bool hasBaseline_ = false;
    CpuTimes previousTotal_;
    CpuUsage usage_;
    std::vector<CpuTimes> previousCores_;
    std::vector<CpuTimes> currentCores_;
    std::vector<double> coreUsage_;
    std::vector<CoreType> coreTypes_;
    bool hybrid_ = false;
    double performanceUsage_ = 0;
    double efficiencyUsage_ = 0;

    double temperature_ = 0;
    bool hasTemperature_ = false;

    LoadAverage load_;
    double uptimeSeconds_ = 0;

    std::vector<HeldFile> frequencyFiles_;
    std::vector<double> frequencies_; // MHz
    double averageFrequency_ = 0;
    double maximumFrequency_ = 0;     // hardware maximum, MHz
    std::uint64_t lastFrequencySample_ = 0;
    bool frequencyFilesOpen_ = false;

    std::vector<std::uint32_t> topIndexes_;
    const ProcessTable* processes_ = nullptr;

    std::string model_;
    std::uint32_t physicalCores_ = 0;

    HistoryBuffer usageHistory_;
    std::array<HistorySeries, 1> historySeries_{};
};

} // namespace readout

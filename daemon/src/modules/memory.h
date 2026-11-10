#pragma once

#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"
#include "parsers/kernel_text.h"

#include <array>
#include <vector>

namespace readout {

class MemoryModule final : public Module {
public:
    explicit MemoryModule(const SystemPaths& paths);

    [[nodiscard]] const char* name() const override { return "memory"; }
    void setTier(Tier tier) override;
    [[nodiscard]] ProcessNeeds processNeeds(Tier tier) const override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }

    static constexpr std::size_t topProcessCount = 10;

    // Pressure level from PSI averages: "normal", "warning" or "critical".
    static const char* pressureLevel(const Pressure& pressure);

private:
    SystemPaths paths_;
    HeldFile meminfoFile_;
    HeldFile pressureFile_;
    std::vector<HeldFile> zramFiles_;
    std::vector<char> buffer_;

    MemoryInfo info_;
    Pressure pressure_;
    bool hasPressure_ = false;
    ZramStat zram_;
    bool hasZram_ = false;

    std::vector<std::uint32_t> topIndexes_;
    const ProcessTable* processes_ = nullptr;

    HistoryBuffer usageHistory_;
    std::array<HistorySeries, 1> historySeries_{};
};

} // namespace readout

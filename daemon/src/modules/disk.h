#pragma once

#include "bus/bus_util.h"
#include "common/paths.h"
#include "io/held_file.h"
#include "modules/module.h"
#include "parsers/kernel_text.h"

#include <array>
#include <string>
#include <vector>

namespace readout {

class DiskModule final : public Module {
public:
    DiskModule(const SystemPaths& paths, sd_event* event, SystemBus& systemBus);
    ~DiskModule() override;
    DiskModule(const DiskModule&) = delete;
    DiskModule& operator=(const DiskModule&) = delete;
    DiskModule(DiskModule&&) = delete;
    DiskModule& operator=(DiskModule&&) = delete;

    [[nodiscard]] const char* name() const override { return "disk"; }
    void setTier(Tier tier) override;
    [[nodiscard]] ProcessNeeds processNeeds(Tier tier) const override;
    void sample(const SampleContext& context) override;
    void encode(DictWriter& writer, const EncodeContext& context) const override;
    void encodeInfo(DictWriter& writer) override;
    [[nodiscard]] std::span<const HistorySeries> history() const override { return historySeries_; }
    void resetBaselines() override;

    static constexpr std::size_t topProcessCount = 10;

    // True for filesystems shown as disks (not tmpfs, overlay, squashfs and similar).
    static bool isRealFilesystem(std::string_view fsType, std::string_view source);

private:
    struct Disk {
        std::string name; // nvme0n1
        std::string model;
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        std::uint64_t sizeBytes = 0;
        bool removable = false;
        bool rotational = false;
        bool hasPrevious = false;
        DiskCounters previous;
        DiskCounters current;
        double readRate = 0;
        double writeRate = 0;
        double busyPercent = 0;
        HeldFile temperatureFile; // NVMe/drivetemp hwmon, detail tier only
        double temperature = 0;
        bool hasTemperature = false;
        // From UDisks2
        bool hasHealth = false;
        bool healthy = true;
        double powerOnHours = -1;
        std::string udisksDrive;
        // Per-drive history, exposed as "<name>/read" and "<name>/write".
        std::string readSeriesName;
        std::string writeSeriesName;
        HistoryBuffer readHistory;
        HistoryBuffer writeHistory;
    };

    struct Mount {
        std::string mountPoint;
        std::string fsType;
        std::string device;  // physical disk name, may be empty
        std::uint64_t totalBytes = 0;
        std::uint64_t freeBytes = 0;      // available to unprivileged users
        std::uint64_t usedBytes = 0;
    };

    void discoverDisks();
    void refreshMounts();
    void refreshCapacity();
    [[nodiscard]] std::string physicalDiskFor(std::uint32_t major, std::uint32_t minor) const;
    void openTemperatureFiles();

    void watchMounts();
    static int handleMountsChanged(sd_event_source* source, int fd, std::uint32_t events, void* userData);

    void requestHealth();
    static int handleManagedObjects(sd_bus_message* message, void* userData, sd_bus_error* error);

    SystemPaths paths_;
    sd_event* event_;
    SystemBus& systemBus_;
    HeldFile diskStatsFile_;
    std::vector<char> buffer_;
    std::uint64_t lastSample_ = 0;
    std::uint64_t lastCapacity_ = 0;
    std::vector<Disk> disks_;
    std::vector<Mount> mounts_;
    bool mountsDirty_ = true;
    bool temperaturesOpen_ = false;

    int mountsFd_ = -1;
    EventSourceHandle mountsSource_;
    SlotHandle healthCall_;
    std::uint64_t lastHealthRequest_ = 0;

    double totalReadRate_ = 0;
    double totalWriteRate_ = 0;

    std::vector<std::uint32_t> topIndexes_;
    const ProcessTable* processes_ = nullptr;

    HistoryBuffer readHistory_;
    HistoryBuffer writeHistory_;
    // Totals first, then two series per drive. Points into disks_, rebuilt by discoverDisks().
    std::vector<HistorySeries> historySeries_;
};

} // namespace readout

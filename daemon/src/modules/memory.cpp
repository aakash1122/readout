#include "modules/memory.h"

#include "io/sysfs.h"
#include "modules/process_table.h"

#include <algorithm>

namespace readout {

MemoryModule::MemoryModule(const SystemPaths& paths) : paths_(paths) {
    historySeries_[0] = HistorySeries{"usage", &usageHistory_};
}

void MemoryModule::setTier(Tier tier) {
    Module::setTier(tier);
    if (tier == Tier::None) {
        meminfoFile_.close();
        pressureFile_.close();
        zramFiles_.clear();
        buffer_.clear();
        buffer_.shrink_to_fit();
        return;
    }
    if (!meminfoFile_.isOpen()) {
        meminfoFile_.open(paths_.proc + "/meminfo");
    }
    if (tier == Tier::Detail) {
        if (!pressureFile_.isOpen()) {
            pressureFile_.open(paths_.proc + "/pressure/memory");
        }
        if (zramFiles_.empty()) {
            const std::string blockRoot = paths_.sys + "/block";
            for (const std::string& device : listDirectory(blockRoot)) {
                if (!device.starts_with("zram")) {
                    continue;
                }
                HeldFile file;
                if (file.open(blockRoot + "/" + device + "/mm_stat")) {
                    zramFiles_.push_back(std::move(file));
                }
            }
        }
    }
}

ProcessNeeds MemoryModule::processNeeds(Tier tier) const {
    ProcessNeeds needs;
    needs.basic = tier == Tier::Detail;
    return needs;
}

const char* MemoryModule::pressureLevel(const Pressure& pressure) {
    if ((pressure.hasFull && pressure.full.average10 >= 10.0) || pressure.some.average10 >= 40.0) {
        return "critical";
    }
    if (pressure.some.average10 >= 10.0) {
        return "warning";
    }
    return "normal";
}

void MemoryModule::sample(const SampleContext& context) {
    processes_ = context.processes;
    if (const auto text = meminfoFile_.readAll(buffer_)) {
        info_ = parseMeminfo(*text);
        if (info_.total > 0) {
            const double used = static_cast<double>(info_.total - std::min(info_.available, info_.total));
            usageHistory_.push(context.nowMicroseconds, used * 100.0 / static_cast<double>(info_.total));
        }
    }
    if (context.tier != Tier::Detail) {
        return;
    }
    const auto pressureText = pressureFile_.isOpen() ? pressureFile_.readAll(buffer_) : std::nullopt;
    const auto pressure = pressureText ? parsePressure(*pressureText) : std::nullopt;
    hasPressure_ = pressure.has_value();
    if (pressure) {
        pressure_ = *pressure;
    }
    zram_ = ZramStat{};
    hasZram_ = false;
    for (HeldFile& file : zramFiles_) {
        const auto text = file.readAll(buffer_);
        const auto stat = text ? parseZramMmStat(*text) : std::nullopt;
        if (stat) {
            zram_.originalBytes += stat->originalBytes;
            zram_.compressedBytes += stat->compressedBytes;
            zram_.memoryUsedBytes += stat->memoryUsedBytes;
            hasZram_ = true;
        }
    }
    if (processes_ != nullptr) {
        processes_->top(ProcessTable::SortKey::Memory, topProcessCount, topIndexes_);
    }
}

void MemoryModule::encode(DictWriter& writer, const EncodeContext& context) const {
    const std::uint64_t available = std::min(info_.available, info_.total);
    const std::uint64_t used = info_.total - available;
    const std::uint64_t swapUsed = info_.swapTotal - std::min(info_.swapFree, info_.swapTotal);
    writer.count("total", info_.total);
    writer.count("used", used);
    writer.count("available", available);
    writer.number("usage", info_.total > 0 ? static_cast<double>(used) * 100.0 / static_cast<double>(info_.total) : 0);
    writer.count("swapTotal", info_.swapTotal);
    writer.count("swapUsed", swapUsed);
    if (context.tier != Tier::Detail) {
        return;
    }
    // "cache" mirrors free(1): page cache plus reclaimable slab, minus shared memory counted in Cached.
    const std::uint64_t cacheWithShared = info_.cached + info_.reclaimableSlab;
    const std::uint64_t cache = cacheWithShared > info_.shared ? cacheWithShared - info_.shared : 0;
    writer.count("free", info_.free);
    writer.count("app", info_.anonymous);
    writer.count("cache", cache);
    writer.count("buffers", info_.buffers);
    writer.count("shared", info_.shared);
    writer.count("dirty", info_.dirty);
    const std::uint64_t compressed = zram_.memoryUsedBytes + info_.zswap;
    const std::uint64_t compressedOriginal = zram_.originalBytes + info_.zswapped;
    writer.count("compressed", compressed);
    writer.count("compressedOriginal", compressedOriginal);
    if (hasPressure_) {
        writer.number("pressureSome", pressure_.some.average10);
        writer.number("pressureFull", pressure_.hasFull ? pressure_.full.average10 : 0);
        writer.text("pressureLevel", pressureLevel(pressure_));
    }
    if (processes_ != nullptr) {
        const auto entries = processes_->entries();
        writer.list("processes", [&](ListWriter& items) {
            for (const std::uint32_t index : topIndexes_) {
                if (index >= entries.size()) {
                    continue;
                }
                items.item([&](DictWriter& item) {
                    item.integer("pid", entries[index].pid);
                    item.text("name", processes_->displayName(index));
                    item.count("memory", entries[index].residentBytes);
                });
            }
        });
    }
}

void MemoryModule::encodeInfo(DictWriter& writer) {
    const auto text = readTextFile(paths_.proc + "/meminfo");
    const MemoryInfo info = text ? parseMeminfo(*text) : MemoryInfo{};
    writer.count("total", info.total);
    writer.count("swapTotal", info.swapTotal);
    writer.flag("hasPressure", fileExists(paths_.proc + "/pressure/memory"));
}

} // namespace readout

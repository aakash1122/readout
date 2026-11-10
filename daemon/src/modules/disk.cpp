#include "modules/disk.h"

#include "common/log.h"
#include "common/text.h"
#include "io/sysfs.h"
#include "modules/process_table.h"

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <map>

namespace readout {

namespace {

constexpr std::uint64_t capacityIntervalMicroseconds = 30'000'000;
constexpr std::uint64_t healthIntervalMicroseconds = 300'000'000;
constexpr double sectorBytes = 512.0;

} // namespace

bool DiskModule::isRealFilesystem(std::string_view fsType, std::string_view source) {
    static constexpr std::string_view types[] = {
        "ext2", "ext3",  "ext4",    "xfs",      "btrfs",   "f2fs",    "vfat",    "exfat",   "ntfs",   "ntfs3",
        "fuseblk", "zfs", "bcachefs", "jfs", "reiserfs", "hfsplus", "apfs", "udf", "iso9660", "nilfs2",
    };
    const bool knownType = std::find(std::begin(types), std::end(types), fsType) != std::end(types);
    if (!knownType) {
        return false;
    }
    return fsType == "zfs" || fsType == "bcachefs" || source.starts_with("/dev/");
}

DiskModule::DiskModule(const SystemPaths& paths, sd_event* event, SystemBus& systemBus)
    : paths_(paths), event_(event), systemBus_(systemBus) {
    historySeries_ = {HistorySeries{"read", &readHistory_}, HistorySeries{"write", &writeHistory_}};
}

DiskModule::~DiskModule() {
    mountsSource_.reset();
    if (mountsFd_ >= 0) {
        ::close(mountsFd_);
    }
}

void DiskModule::setTier(Tier tier) {
    const Tier previous = tier_;
    Module::setTier(tier);
    if (tier == Tier::None) {
        diskStatsFile_.close();
        mountsSource_.reset();
        if (mountsFd_ >= 0) {
            ::close(mountsFd_);
            mountsFd_ = -1;
        }
        healthCall_.reset();
        disks_.clear();
        mounts_.clear();
        buffer_.clear();
        buffer_.shrink_to_fit();
        temperaturesOpen_ = false;
        lastSample_ = 0;
        lastCapacity_ = 0;
        lastHealthRequest_ = 0;
        return;
    }
    if (previous == Tier::None) {
        discoverDisks();
        diskStatsFile_.open(paths_.proc + "/diskstats");
        watchMounts();
        mountsDirty_ = true;
    }
    if (tier == Tier::Detail && previous != Tier::Detail) {
        openTemperatureFiles();
        requestHealth();
    }
    if (tier != Tier::Detail && temperaturesOpen_) {
        for (Disk& disk : disks_) {
            disk.temperatureFile.close();
            disk.hasTemperature = false;
        }
        temperaturesOpen_ = false;
    }
}

ProcessNeeds DiskModule::processNeeds(Tier tier) const {
    ProcessNeeds needs;
    needs.basic = tier == Tier::Detail;
    needs.io = tier == Tier::Detail;
    return needs;
}

void DiskModule::resetBaselines() {
    for (Disk& disk : disks_) {
        disk.hasPrevious = false;
    }
    lastSample_ = 0;
}

void DiskModule::discoverDisks() {
    disks_.clear();
    const std::string blockRoot = paths_.sys + "/block/";
    for (const std::string& entry : listDirectory(paths_.sys + "/block")) {
        const std::string directory = blockRoot + entry;
        // Only physical disks: virtual devices (loop, dm, md, zram) have no "device" link
        // and would count the same I/O twice.
        if (!fileExists(directory + "/device")) {
            continue;
        }
        Disk disk;
        disk.name = entry;
        const auto model = readTextFile(directory + "/device/model");
        if (model) {
            std::array<char, 128> text{};
            copySanitizedUtf8(trim(*model), text.data(), text.size());
            disk.model = text.data();
        }
        disk.sizeBytes = static_cast<std::uint64_t>(readIntegerFile(directory + "/size").value_or(0)) * 512U;
        disk.removable = readIntegerFile(directory + "/removable").value_or(0) == 1;
        disk.rotational = readIntegerFile(directory + "/queue/rotational").value_or(0) == 1;
        const auto device = readTextFile(directory + "/dev");
        if (device) {
            const std::string_view value = trim(*device);
            const std::size_t colon = value.find(':');
            if (colon != std::string_view::npos) {
                disk.major = parseNumber<std::uint32_t>(value.substr(0, colon)).value_or(0);
                disk.minor = parseNumber<std::uint32_t>(value.substr(colon + 1)).value_or(0);
            }
        }
        disk.readSeriesName = disk.name + "/read";
        disk.writeSeriesName = disk.name + "/write";
        disks_.push_back(std::move(disk));
    }
    // disks_ does not change until the next discovery, so these pointers stay valid.
    historySeries_.resize(2);
    for (const Disk& disk : disks_) {
        historySeries_.push_back(HistorySeries{disk.readSeriesName.c_str(), &disk.readHistory});
        historySeries_.push_back(HistorySeries{disk.writeSeriesName.c_str(), &disk.writeHistory});
    }
}

void DiskModule::openTemperatureFiles() {
    for (Disk& disk : disks_) {
        const std::string device = paths_.sys + "/block/" + disk.name + "/device";
        // NVMe: device/hwmonN (the controller). drivetemp: device/hwmon/hwmonN.
        for (const std::string& base : {device, device + "/hwmon"}) {
            for (const std::string& entry : listDirectory(base)) {
                if (entry.starts_with("hwmon") && entry.size() > 5 &&
                    disk.temperatureFile.open(base + "/" + entry + "/temp1_input")) {
                    break;
                }
            }
            if (disk.temperatureFile.isOpen()) {
                break;
            }
        }
    }
    temperaturesOpen_ = true;
}

std::string DiskModule::physicalDiskFor(std::uint32_t major, std::uint32_t minor) const {
    for (const Disk& disk : disks_) {
        if (disk.major == major && disk.minor == minor) {
            return disk.name;
        }
    }
    return {};
}

void DiskModule::watchMounts() {
    if (mountsFd_ >= 0) {
        return;
    }
    mountsFd_ = ::open((paths_.proc + "/self/mountinfo").c_str(), O_RDONLY | O_CLOEXEC);
    if (mountsFd_ < 0) {
        return;
    }
    // The kernel signals POLLPRI on mountinfo whenever the mount table changes.
    sd_event_source* source = nullptr;
    if (sd_event_add_io(event_, &source, mountsFd_, EPOLLPRI, &DiskModule::handleMountsChanged, this) >= 0) {
        mountsSource_.reset(source);
    }
}

int DiskModule::handleMountsChanged(sd_event_source* /*source*/, int fd, std::uint32_t /*events*/, void* userData) {
    auto* module = static_cast<DiskModule*>(userData);
    // Reading the file acknowledges the change.
    std::array<char, 4096> scratch{};
    ::lseek(fd, 0, SEEK_SET);
    while (::read(fd, scratch.data(), scratch.size()) > 0) {
    }
    module->mountsDirty_ = true;
    return 0;
}

void DiskModule::refreshMounts() {
    mounts_.clear();
    const auto text = readTextFile(paths_.proc + "/self/mountinfo");
    if (!text) {
        return;
    }
    std::vector<std::string> seenSources;
    parseMountInfo(*text, [&](const MountEntry& entry) {
        if (!isRealFilesystem(entry.fsType, entry.source)) {
            return;
        }
        // Bind mounts and btrfs subvolumes of the same device: keep the first (outermost) mount.
        const std::string source = unescapeMountField(entry.source);
        std::string resolvedSource = resolvePath(source);
        if (resolvedSource.empty()) {
            resolvedSource = source;
        }
        if (std::find(seenSources.begin(), seenSources.end(), resolvedSource) != seenSources.end()) {
            return;
        }
        seenSources.push_back(resolvedSource);
        Mount mount;
        mount.mountPoint = unescapeMountField(entry.mountPoint);
        mount.fsType = std::string(entry.fsType);
        // Map to a physical disk: partition -> parent, device-mapper -> its first slave.
        std::string blockName = baseName(resolvedSource);
        for (int depth = 0; depth < 4 && !blockName.empty(); ++depth) {
            if (std::any_of(disks_.begin(), disks_.end(), [&](const Disk& disk) { return disk.name == blockName; })) {
                mount.device = blockName;
                break;
            }
            const std::string classPath = paths_.sys + "/class/block/" + blockName;
            if (fileExists(classPath + "/partition")) {
                blockName = baseName(resolvePath(classPath + "/.."));
                continue;
            }
            const std::vector<std::string> slaves = listDirectory(classPath + "/slaves");
            blockName = slaves.empty() ? std::string{} : slaves.front();
        }
        if (mount.device.empty()) {
            mount.device = physicalDiskFor(entry.major, entry.minor);
        }
        mounts_.push_back(std::move(mount));
    });
    // "/" first, then by path.
    std::stable_sort(mounts_.begin(), mounts_.end(), [](const Mount& left, const Mount& right) {
        if ((left.mountPoint == "/") != (right.mountPoint == "/")) {
            return left.mountPoint == "/";
        }
        return left.mountPoint < right.mountPoint;
    });
}

void DiskModule::refreshCapacity() {
    for (Mount& mount : mounts_) {
        struct statvfs info {};
        if (::statvfs(mount.mountPoint.c_str(), &info) != 0) {
            continue;
        }
        const auto fragment = static_cast<std::uint64_t>(info.f_frsize);
        mount.totalBytes = static_cast<std::uint64_t>(info.f_blocks) * fragment;
        mount.freeBytes = static_cast<std::uint64_t>(info.f_bavail) * fragment;
        const std::uint64_t reallyFree = static_cast<std::uint64_t>(info.f_bfree) * fragment;
        mount.usedBytes = mount.totalBytes > reallyFree ? mount.totalBytes - reallyFree : 0;
    }
}

void DiskModule::sample(const SampleContext& context) {
    processes_ = context.processes;
    if (mountsDirty_) {
        refreshMounts();
        mountsDirty_ = false;
        lastCapacity_ = 0;
    }
    if (lastCapacity_ == 0 || context.nowMicroseconds - lastCapacity_ >= capacityIntervalMicroseconds) {
        refreshCapacity();
        lastCapacity_ = context.nowMicroseconds;
    }

    const std::uint64_t elapsed = lastSample_ != 0 ? context.nowMicroseconds - lastSample_ : 0;
    lastSample_ = context.nowMicroseconds;
    if (const auto text = diskStatsFile_.readAll(buffer_)) {
        parseDiskStats(*text, [&](std::string_view diskName, const DiskCounters& counters) {
            for (Disk& disk : disks_) {
                if (disk.name != diskName) {
                    continue;
                }
                disk.current = counters;
                if (disk.hasPrevious && elapsed > 0) {
                    disk.readRate = ratePerSecond(disk.previous.sectorsRead, counters.sectorsRead, elapsed) * sectorBytes;
                    disk.writeRate =
                        ratePerSecond(disk.previous.sectorsWritten, counters.sectorsWritten, elapsed) * sectorBytes;
                    const double busyMilliseconds =
                        ratePerSecond(disk.previous.ioMilliseconds, counters.ioMilliseconds, elapsed);
                    disk.busyPercent = std::clamp(busyMilliseconds / 10.0, 0.0, 100.0);
                } else {
                    disk.readRate = 0;
                    disk.writeRate = 0;
                    disk.busyPercent = 0;
                }
                disk.previous = counters;
                disk.hasPrevious = true;
                break;
            }
        });
    }
    totalReadRate_ = 0;
    totalWriteRate_ = 0;
    for (Disk& disk : disks_) {
        totalReadRate_ += disk.readRate;
        totalWriteRate_ += disk.writeRate;
        disk.readHistory.push(context.nowMicroseconds, disk.readRate);
        disk.writeHistory.push(context.nowMicroseconds, disk.writeRate);
    }
    readHistory_.push(context.nowMicroseconds, totalReadRate_);
    writeHistory_.push(context.nowMicroseconds, totalWriteRate_);

    if (context.tier == Tier::Detail) {
        for (Disk& disk : disks_) {
            const auto milliDegrees = disk.temperatureFile.isOpen() ? disk.temperatureFile.readInteger() : std::nullopt;
            disk.hasTemperature = milliDegrees.has_value();
            disk.temperature = milliDegrees ? static_cast<double>(*milliDegrees) / 1000.0 : 0;
        }
        if (lastHealthRequest_ == 0 || context.nowMicroseconds - lastHealthRequest_ >= healthIntervalMicroseconds) {
            requestHealth();
            lastHealthRequest_ = context.nowMicroseconds;
        }
        if (processes_ != nullptr) {
            processes_->top(ProcessTable::SortKey::DiskIo, topProcessCount, topIndexes_);
        }
    }
}

void DiskModule::encode(DictWriter& writer, const EncodeContext& context) const {
    writer.number("read", totalReadRate_);
    writer.number("write", totalWriteRate_);
    if (!mounts_.empty()) {
        const Mount& root = mounts_.front();
        writer.text("mountPoint", root.mountPoint.c_str());
        writer.count("total", root.totalBytes);
        writer.count("used", root.usedBytes);
        writer.count("free", root.freeBytes);
        writer.number("usage", root.totalBytes > 0 ? static_cast<double>(root.usedBytes) * 100.0 /
                                                         static_cast<double>(root.totalBytes)
                                                   : 0);
    }
    if (context.tier != Tier::Detail) {
        return;
    }
    writer.list("disks", [this](ListWriter& items) {
        for (const Disk& disk : disks_) {
            items.item([&disk](DictWriter& item) {
                item.text("name", disk.name.c_str());
                item.text("model", disk.model.c_str());
                item.count("size", disk.sizeBytes);
                item.flag("removable", disk.removable);
                item.flag("rotational", disk.rotational);
                item.number("read", disk.readRate);
                item.number("write", disk.writeRate);
                item.number("busy", disk.busyPercent);
                item.count("totalRead", disk.current.sectorsRead * 512U);
                item.count("totalWritten", disk.current.sectorsWritten * 512U);
                if (disk.hasTemperature) {
                    item.number("temperature", disk.temperature);
                }
                if (disk.hasHealth) {
                    item.flag("healthy", disk.healthy);
                    if (disk.powerOnHours >= 0) {
                        item.number("powerOnHours", disk.powerOnHours);
                    }
                }
            });
        }
    });
    writer.list("mounts", [this](ListWriter& items) {
        for (const Mount& mount : mounts_) {
            items.item([&mount](DictWriter& item) {
                item.text("mountPoint", mount.mountPoint.c_str());
                item.text("fsType", mount.fsType.c_str());
                item.text("disk", mount.device.c_str());
                item.count("total", mount.totalBytes);
                item.count("used", mount.usedBytes);
                item.count("free", mount.freeBytes);
            });
        }
    });
    if (processes_ != nullptr) {
        const auto entries = processes_->entries();
        writer.list("processes", [&](ListWriter& items) {
            for (const std::uint32_t index : topIndexes_) {
                if (index >= entries.size() || !entries[index].ioKnown ||
                    entries[index].readRate + entries[index].writeRate <= 0) {
                    continue;
                }
                items.item([&](DictWriter& item) {
                    item.integer("pid", entries[index].pid);
                    item.text("name", processes_->displayName(index));
                    item.number("read", entries[index].readRate);
                    item.number("write", entries[index].writeRate);
                });
            }
        });
    }
}

void DiskModule::encodeInfo(DictWriter& writer) {
    const bool wasActive = tier_ != Tier::None;
    if (!wasActive) {
        discoverDisks();
    }
    writer.list("disks", [this](ListWriter& items) {
        for (const Disk& disk : disks_) {
            items.item([&disk](DictWriter& item) {
                item.text("name", disk.name.c_str());
                item.text("model", disk.model.c_str());
                item.count("size", disk.sizeBytes);
                item.flag("removable", disk.removable);
                item.flag("rotational", disk.rotational);
            });
        }
    });
    if (!wasActive) {
        disks_.clear();
    }
}

// ---- UDisks2 health ----------------------------------------------------------------------------

void DiskModule::requestHealth() {
    sd_bus* bus = systemBus_.get();
    if (bus == nullptr || healthCall_) {
        return;
    }
    sd_bus_slot* slot = nullptr;
    if (sd_bus_call_method_async(bus, &slot, "org.freedesktop.UDisks2", "/org/freedesktop/UDisks2",
                                 "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                                 &DiskModule::handleManagedObjects, this, "") >= 0) {
        healthCall_.reset(slot);
    }
}

int DiskModule::handleManagedObjects(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    auto* module = static_cast<DiskModule*>(userData);
    struct DriveHealth {
        bool healthy = true;
        double powerOnHours = -1;
        bool known = false;
    };
    std::map<std::string, std::string> blockToDrive; // "nvme0n1" -> drive object path
    std::map<std::string, DriveHealth> drives;

    if (!sd_bus_message_is_method_error(message, nullptr) &&
        sd_bus_message_enter_container(message, 'a', "{oa{sa{sv}}}") > 0) {
        while (sd_bus_message_enter_container(message, 'e', "oa{sa{sv}}") > 0) {
            const char* objectPath = nullptr;
            sd_bus_message_read_basic(message, 'o', &objectPath);
            const std::string path = objectPath != nullptr ? objectPath : "";
            sd_bus_message_enter_container(message, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(message, 'e', "sa{sv}") > 0) {
                const char* interface = nullptr;
                sd_bus_message_read_basic(message, 's', &interface);
                const std::string_view interfaceName = interface != nullptr ? interface : "";
                if (interfaceName == "org.freedesktop.UDisks2.Block") {
                    std::string drive;
                    std::string device;
                    readProperties(message, [&](std::string_view key, VariantValue& value) {
                        if (key == "Drive") {
                            drive = value.text().value_or("/");
                        } else if (key == "Device") {
                            const auto bytes = value.bytes();
                            if (bytes) {
                                device.assign(bytes->begin(), std::find(bytes->begin(), bytes->end(), 0));
                            }
                        }
                    });
                    if (drive != "/" && !drive.empty() && device.starts_with("/dev/")) {
                        blockToDrive[device.substr(5)] = drive;
                    }
                } else if (interfaceName == "org.freedesktop.UDisks2.Drive.Ata") {
                    DriveHealth& health = drives[path];
                    readProperties(message, [&](std::string_view key, VariantValue& value) {
                        if (key == "SmartFailing") {
                            health.healthy = !value.boolean().value_or(false);
                            health.known = true;
                        } else if (key == "SmartPowerOnSeconds") {
                            const double seconds = value.number().value_or(0);
                            if (seconds > 0) {
                                health.powerOnHours = seconds / 3600.0;
                            }
                        }
                    });
                } else if (interfaceName == "org.freedesktop.UDisks2.NVMe.Controller") {
                    DriveHealth& health = drives[path];
                    readProperties(message, [&](std::string_view key, VariantValue& value) {
                        if (key == "SmartCriticalWarning") {
                            const auto warnings = value.texts();
                            health.healthy = !warnings || warnings->empty();
                            health.known = warnings.has_value();
                        } else if (key == "SmartPowerOnHours") {
                            health.powerOnHours = value.number().value_or(-1);
                        }
                    });
                } else {
                    sd_bus_message_skip(message, "a{sv}");
                }
                sd_bus_message_exit_container(message);
            }
            sd_bus_message_exit_container(message);
            sd_bus_message_exit_container(message);
        }
    }
    for (Disk& disk : module->disks_) {
        const auto drive = blockToDrive.find(disk.name);
        if (drive == blockToDrive.end()) {
            continue;
        }
        const auto health = drives.find(drive->second);
        if (health == drives.end() || !health->second.known) {
            continue;
        }
        disk.hasHealth = true;
        disk.healthy = health->second.healthy;
        disk.powerOnHours = health->second.powerOnHours;
        disk.udisksDrive = drive->second;
    }
    module->healthCall_.reset();
    return 0;
}

} // namespace readout

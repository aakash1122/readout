#include "modules/cpu.h"

#include "common/text.h"
#include "io/sysfs.h"
#include "modules/process_table.h"

#include <algorithm>
#include <set>
#include <utility>

namespace readout {

namespace {

constexpr std::uint64_t frequencyIntervalMicroseconds = 2'000'000;

} // namespace

CpuModule::CpuModule(const SystemPaths& paths) : paths_(paths) {
    historySeries_[0] = HistorySeries{"usage", &usageHistory_};
    detectTopology();
    detectTemperatureSensor();
    const auto cpuinfo = readTextFile(paths_.proc + "/cpuinfo");
    if (cpuinfo) {
        std::array<char, 128> model{};
        copySanitizedUtf8(parseCpuModel(*cpuinfo), model.data(), model.size());
        model_ = model.data();
    }
    if (model_.empty()) {
        const auto deviceTreeModel = readTextFile(paths_.sys + "/firmware/devicetree/base/model");
        if (deviceTreeModel) {
            std::array<char, 128> model{};
            // The device tree string is NUL-terminated inside the file.
            copySanitizedUtf8(trim(std::string_view(deviceTreeModel->c_str())), model.data(), model.size()); // NOLINT(readability-redundant-string-cstr)
            model_ = model.data();
        }
    }
}

void CpuModule::detectTopology() {
    const std::string cpuRoot = paths_.sys + "/devices/system/cpu";
    std::set<std::pair<std::int64_t, std::int64_t>> physical;
    int highestIndex = -1;
    for (const std::string& entry : listDirectory(cpuRoot)) {
        if (entry.size() <= 3 || !entry.starts_with("cpu")) {
            continue;
        }
        const auto index = parseNumber<int>(std::string_view(entry).substr(3));
        if (!index) {
            continue;
        }
        highestIndex = std::max(highestIndex, *index);
        const std::string topology = cpuRoot + "/" + entry + "/topology/";
        const auto package = readIntegerFile(topology + "physical_package_id");
        const auto core = readIntegerFile(topology + "core_id");
        if (package && core) {
            physical.emplace(*package, *core);
        }
    }
    physicalCores_ = static_cast<std::uint32_t>(physical.size());
    const std::size_t coreCount = highestIndex >= 0 ? static_cast<std::size_t>(highestIndex) + 1 : 0;
    coreTypes_.assign(coreCount, CoreType::Unknown);

    // Intel hybrid: separate PMUs list their CPUs.
    const auto performanceList = readTextFile(paths_.sys + "/devices/cpu_core/cpus");
    const auto efficiencyList = readTextFile(paths_.sys + "/devices/cpu_atom/cpus");
    const auto mark = [this](const std::optional<std::string>& list, CoreType type) {
        if (!list) {
            return;
        }
        parseCpuList(*list, [this, type](int cpu) {
            if (cpu >= 0 && static_cast<std::size_t>(cpu) < coreTypes_.size()) {
                coreTypes_[static_cast<std::size_t>(cpu)] = type;
            }
        });
    };
    if (performanceList && efficiencyList) {
        mark(performanceList, CoreType::Performance);
        mark(efficiencyList, CoreType::Efficiency);
        hybrid_ = true;
        return;
    }
    // ARM big.LITTLE: cores below the highest capacity are efficiency cores.
    std::vector<std::int64_t> capacities(coreCount, -1);
    std::int64_t highestCapacity = -1;
    for (std::size_t cpu = 0; cpu < coreCount; ++cpu) {
        capacities[cpu] = readIntegerFile(cpuRoot + "/cpu" + std::to_string(cpu) + "/cpu_capacity").value_or(-1);
        highestCapacity = std::max(highestCapacity, capacities[cpu]);
    }
    bool sawEfficiency = false;
    for (std::size_t cpu = 0; cpu < coreCount && highestCapacity > 0; ++cpu) {
        if (capacities[cpu] < 0) {
            continue;
        }
        const bool efficient = capacities[cpu] < highestCapacity * 3 / 4;
        coreTypes_[cpu] = efficient ? CoreType::Efficiency : CoreType::Performance;
        sawEfficiency = sawEfficiency || efficient;
    }
    hybrid_ = sawEfficiency;
    if (!hybrid_) {
        std::fill(coreTypes_.begin(), coreTypes_.end(), CoreType::Unknown);
    }
}

void CpuModule::detectTemperatureSensor() {
    struct Preference {
        std::string_view chip;
        std::string_view labels[3];
    };
    static constexpr Preference preferences[] = {
        {"k10temp", {"Tdie", "Tctl", ""}},
        {"zenpower", {"Tdie", "Tctl", ""}},
        {"coretemp", {"Package id 0", "Physical id 0", ""}},
        {"cpu_thermal", {"", "", ""}},
        {"cpu-thermal", {"", "", ""}},
    };
    const std::vector<HwmonChip> chips = listHwmonChips(paths_);
    for (const Preference& preference : preferences) {
        for (const HwmonChip& chip : chips) {
            if (chip.name != preference.chip) {
                continue;
            }
            const HwmonInput* chosen = nullptr;
            for (const std::string_view wanted : preference.labels) {
                for (const HwmonInput& input : chip.inputs) {
                    if (input.kind == "temp" && (wanted.empty() || input.label == wanted)) {
                        chosen = &input;
                        break;
                    }
                }
                if (chosen != nullptr) {
                    break;
                }
            }
            if (chosen != nullptr && temperatureFile_.open(chip.directory + "/" + chosen->inputFile)) {
                temperatureSource_ = chip.name + (chosen->label.empty() ? "" : " " + chosen->label);
                return;
            }
        }
    }
    // Fallback: thermal zones.
    const std::string zones = paths_.sys + "/class/thermal";
    for (const std::string_view wanted : {"x86_pkg_temp", "cpu-thermal", "cpu_thermal", "soc_thermal"}) {
        for (const std::string& entry : listDirectory(zones)) {
            if (!entry.starts_with("thermal_zone")) {
                continue;
            }
            const auto type = readTextFile(zones + "/" + entry + "/type");
            if (type && trim(*type) == wanted && temperatureFile_.open(zones + "/" + entry + "/temp")) {
                temperatureSource_ = std::string(wanted);
                return;
            }
        }
    }
}

void CpuModule::openFrequencyFiles() {
    frequencyFiles_.clear();
    frequencyFiles_.resize(coreTypes_.size());
    frequencies_.assign(coreTypes_.size(), 0.0);
    const std::string cpuRoot = paths_.sys + "/devices/system/cpu/cpu";
    for (std::size_t cpu = 0; cpu < frequencyFiles_.size(); ++cpu) {
        const std::string directory = cpuRoot + std::to_string(cpu) + "/cpufreq/";
        // cpuinfo_avg_freq (Linux 6.13+) is computed from counters without waking the CPU.
        // scaling_cur_freq may send an IPI on x86, so it is the fallback.
        if (!frequencyFiles_[cpu].open(directory + "cpuinfo_avg_freq")) {
            frequencyFiles_[cpu].open(directory + "scaling_cur_freq");
        }
        if (maximumFrequency_ == 0) {
            const auto maximum = readIntegerFile(directory + "cpuinfo_max_freq");
            if (maximum) {
                maximumFrequency_ = static_cast<double>(*maximum) / 1000.0;
            }
        }
    }
    frequencyFilesOpen_ = true;
}

void CpuModule::setTier(Tier tier) {
    Module::setTier(tier);
    if (tier == Tier::None) {
        statFile_.close();
        loadFile_.close();
        uptimeFile_.close();
        frequencyFiles_.clear();
        frequencyFilesOpen_ = false;
        buffer_.clear();
        buffer_.shrink_to_fit();
        hasBaseline_ = false;
        return;
    }
    if (!statFile_.isOpen()) {
        statFile_.open(paths_.proc + "/stat");
    }
    if (tier == Tier::Detail) {
        if (!loadFile_.isOpen()) {
            loadFile_.open(paths_.proc + "/loadavg");
        }
        if (!uptimeFile_.isOpen()) {
            uptimeFile_.open(paths_.proc + "/uptime");
        }
        if (!frequencyFilesOpen_) {
            openFrequencyFiles();
        }
    }
}

ProcessNeeds CpuModule::processNeeds(Tier tier) const {
    ProcessNeeds needs;
    needs.basic = tier == Tier::Detail;
    return needs;
}

void CpuModule::resetBaselines() {
    hasBaseline_ = false;
}

void CpuModule::sample(const SampleContext& context) {
    processes_ = context.processes;
    const auto text = statFile_.readAll(buffer_);
    if (text) {
        CpuTimes total;
        std::fill(currentCores_.begin(), currentCores_.end(), CpuTimes{});
        parseProcStat(*text, [&](int index, const CpuTimes& times) {
            if (index < 0) {
                total = times;
                return;
            }
            const auto position = static_cast<std::size_t>(index);
            if (position >= currentCores_.size()) {
                currentCores_.resize(position + 1);
            }
            currentCores_[position] = times;
        });
        if (previousCores_.size() != currentCores_.size()) {
            previousCores_.resize(currentCores_.size());
            coreUsage_.assign(currentCores_.size(), 0.0);
            hasBaseline_ = false;
        }
        if (hasBaseline_) {
            usage_ = computeCpuUsage(previousTotal_, total);
            double performanceSum = 0;
            double efficiencySum = 0;
            int performanceCount = 0;
            int efficiencyCount = 0;
            for (std::size_t core = 0; core < currentCores_.size(); ++core) {
                coreUsage_[core] = computeCpuUsage(previousCores_[core], currentCores_[core]).total;
                if (core < coreTypes_.size()) {
                    if (coreTypes_[core] == CoreType::Performance) {
                        performanceSum += coreUsage_[core];
                        ++performanceCount;
                    } else if (coreTypes_[core] == CoreType::Efficiency) {
                        efficiencySum += coreUsage_[core];
                        ++efficiencyCount;
                    }
                }
            }
            performanceUsage_ = performanceCount > 0 ? performanceSum / performanceCount : 0;
            efficiencyUsage_ = efficiencyCount > 0 ? efficiencySum / efficiencyCount : 0;
            usageHistory_.push(context.nowMicroseconds, usage_.total);
        }
        previousTotal_ = total;
        std::swap(previousCores_, currentCores_);
        currentCores_.resize(previousCores_.size());
        hasBaseline_ = true;
    }

    if (temperatureFile_.isOpen()) {
        const auto milliDegrees = temperatureFile_.readInteger();
        hasTemperature_ = milliDegrees.has_value();
        temperature_ = milliDegrees ? static_cast<double>(*milliDegrees) / 1000.0 : 0;
    }

    if (context.tier == Tier::Detail) {
        if (const auto loadText = loadFile_.readAll(buffer_)) {
            load_ = parseLoadAverage(*loadText).value_or(LoadAverage{});
        }
        if (const auto uptimeText = uptimeFile_.readAll(buffer_)) {
            uptimeSeconds_ = parseUptimeSeconds(*uptimeText).value_or(0);
        }
        sampleFrequencies(context.nowMicroseconds);
        if (processes_ != nullptr) {
            processes_->top(ProcessTable::SortKey::Cpu, topProcessCount, topIndexes_);
        }
    }
}

void CpuModule::sampleFrequencies(std::uint64_t now) {
    if (lastFrequencySample_ != 0 && now - lastFrequencySample_ < frequencyIntervalMicroseconds) {
        return;
    }
    lastFrequencySample_ = now;
    double sum = 0;
    int count = 0;
    for (std::size_t cpu = 0; cpu < frequencyFiles_.size(); ++cpu) {
        const auto kiloHertz = frequencyFiles_[cpu].isOpen() ? frequencyFiles_[cpu].readInteger() : std::nullopt;
        frequencies_[cpu] = kiloHertz ? static_cast<double>(*kiloHertz) / 1000.0 : 0;
        if (kiloHertz) {
            sum += frequencies_[cpu];
            ++count;
        }
    }
    averageFrequency_ = count > 0 ? sum / count : 0;
}

void CpuModule::encode(DictWriter& writer, const EncodeContext& context) const {
    writer.number("usage", usage_.total);
    writer.number("user", usage_.user);
    writer.number("system", usage_.system);
    writer.number("ioWait", usage_.ioWait);
    if (hasTemperature_) {
        writer.number("temperature", temperature_);
    }
    if (context.tier == Tier::Detail || context.cores) {
        writer.numbers("cores", coreUsage_);
    }
    if (context.tier != Tier::Detail) {
        return;
    }
    if (hybrid_) {
        writer.number("performanceUsage", performanceUsage_);
        writer.number("efficiencyUsage", efficiencyUsage_);
    }
    writer.number("load1", load_.oneMinute);
    writer.number("load5", load_.fiveMinutes);
    writer.number("load15", load_.fifteenMinutes);
    writer.count("threadCount", load_.total);
    if (processes_ != nullptr) {
        writer.count("processCount", processes_->entries().size());
    }
    writer.number("uptime", uptimeSeconds_);
    if (averageFrequency_ > 0) {
        writer.number("frequency", averageFrequency_);
        writer.numbers("frequencies", frequencies_);
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
                    item.number("cpu", entries[index].cpuPercent);
                });
            }
        });
    }
}

void CpuModule::encodeInfo(DictWriter& writer) {
    writer.text("model", model_.c_str());
    writer.count("logicalCores", coreTypes_.size());
    writer.count("physicalCores", physicalCores_);
    writer.flag("hybrid", hybrid_);
    if (hybrid_) {
        std::vector<const char*> types;
        types.reserve(coreTypes_.size());
        for (const CoreType type : coreTypes_) {
            types.push_back(type == CoreType::Performance  ? "performance"
                            : type == CoreType::Efficiency ? "efficiency"
                                                           : "unknown");
        }
        writer.texts("coreTypes", types);
    }
    if (maximumFrequency_ == 0) {
        const auto maximum =
            readIntegerFile(paths_.sys + "/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
        if (maximum) {
            maximumFrequency_ = static_cast<double>(*maximum) / 1000.0;
        }
    }
    writer.number("maxFrequency", maximumFrequency_);
    writer.text("temperatureSource", temperatureSource_.c_str());
}

} // namespace readout

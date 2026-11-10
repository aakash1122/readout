// Interface every monitoring module implements.
#pragma once

#include "bus/dict_writer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace readout {

class ProcessTable;

// How much data a subscriber wants from a module.
// Panel: the few values a panel widget draws. Detail: everything a popup shows.
enum class Tier : std::uint8_t { None = 0, Panel = 1, Detail = 2 };

const char* tierName(Tier tier);
bool parseTier(std::string_view text, Tier& tier);

// What the shared process table must collect this tick.
struct ProcessNeeds {
    bool basic = false; // CPU time, resident memory, name
    bool io = false;    // /proc/<pid>/io
};

struct SampleContext {
    std::uint64_t nowMicroseconds = 0; // CLOCK_MONOTONIC
    Tier tier = Tier::None;
    const ProcessTable* processes = nullptr; // set when processNeeds() asked for it
};

// Per-subscriber view used when encoding.
struct EncodeContext {
    Tier tier = Tier::None;
    std::span<const std::string> sensorIds; // sensors module only
    bool cores = false;                      // cpu module: per-core usage in the panel tier too
};

// Fixed-size history of (time, value) points.
class HistoryBuffer {
public:
    static constexpr std::size_t capacity = 180;

    void push(std::uint64_t timeMicroseconds, double value) {
        points_[head_] = Point{timeMicroseconds, static_cast<float>(value)};
        head_ = (head_ + 1) % capacity;
        if (size_ < capacity) {
            ++size_;
        }
    }
    void clear() {
        head_ = 0;
        size_ = 0;
    }
    [[nodiscard]] std::size_t size() const { return size_; }
    // Oldest first.
    template <typename Callback>
    void forEach(Callback&& onPoint) const {
        const std::size_t start = (head_ + capacity - size_) % capacity;
        for (std::size_t offset = 0; offset < size_; ++offset) {
            const Point& point = points_[(start + offset) % capacity];
            onPoint(point.time, static_cast<double>(point.value));
        }
    }

private:
    struct Point {
        std::uint64_t time = 0;
        float value = 0;
    };
    std::array<Point, capacity> points_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

struct HistorySeries {
    const char* name = "";
    const HistoryBuffer* buffer = nullptr;
};

class Module {
public:
    Module() = default;
    virtual ~Module() = default;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    [[nodiscard]] virtual const char* name() const = 0;

    // The highest tier any subscriber wants. None means: release resources, stop watching.
    virtual void setTier(Tier tier) { tier_ = tier; }
    [[nodiscard]] Tier tier() const { return tier_; }

    [[nodiscard]] virtual ProcessNeeds processNeeds(Tier /*tier*/) const { return {}; }

    virtual void sample(const SampleContext& context) = 0;
    virtual void encode(DictWriter& writer, const EncodeContext& context) const = 0;

    // Static facts for GetInfo(). Called rarely.
    virtual void encodeInfo(DictWriter& /*writer*/) {}

    // Series available through GetHistory().
    [[nodiscard]] virtual std::span<const HistorySeries> history() const { return {}; }

    // Called after resume from suspend: rate baselines are stale.
    virtual void resetBaselines() {}

    // Sensors only: the union of sensor ids that panel subscribers selected.
    virtual void setSelectedSensors(std::span<const std::string> /*sensorIds*/) {}

protected:
    Tier tier_ = Tier::None;
};

// Rate of change per second between two counter readings. Returns 0 on reset or wrap.
inline double ratePerSecond(std::uint64_t before, std::uint64_t after, std::uint64_t elapsedMicroseconds) {
    if (after < before || elapsedMicroseconds == 0) {
        return 0;
    }
    return static_cast<double>(after - before) * 1e6 / static_cast<double>(elapsedMicroseconds);
}

} // namespace readout

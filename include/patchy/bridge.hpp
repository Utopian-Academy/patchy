#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy {
// One owner, bounded storage. Construct/prepare outside the audio callback.
class FrameRing {
public:
    FrameRing(std::size_t capacity, std::size_t channels);
    bool push(const float*, std::size_t frames) noexcept;
    float at(std::size_t frame, std::size_t channel) const noexcept;
    void consume(std::size_t frames) noexcept;
    void clear() noexcept;
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }
private:
    std::size_t capacity_, channels_, head_{}, size_{};
    std::vector<float> data_;
};
class DriftController {
public:
    double update(double occupancy_error_seconds, double elapsed_seconds) noexcept;
    void reset() noexcept;
    double ppm() const noexcept { return correction_ * 1e6; }
private:
    double filtered_{}, integral_{}, correction_{};
};
struct BridgeStats {
    std::uint64_t input_frames{}, output_frames{}, silent_frames{};
    std::uint64_t underruns{}, overflows{}, resets{};
};
// Adaptive windowed-sinc converter. Supports ratio 0.5..2, 1..8 matching channels.
// pull() always initializes output, using silence while priming/starved.
class Bridge {
public:
    Bridge(std::uint32_t input_rate, std::uint32_t output_rate, std::uint16_t channels,
           std::size_t target_frames, std::size_t capacity_frames);
    void push(const float*, std::size_t frames) noexcept;
    void pull(float*, std::size_t frames) noexcept;
    void reset() noexcept;
    const BridgeStats& stats() const noexcept { return stats_; }
    std::size_t queued() const noexcept { return ring_.size(); }
    double correction_ppm() const noexcept { return drift_.ppm(); }
private:
    static constexpr std::size_t taps = 64, phases = 512, half = taps / 2;
    FrameRing ring_;
    std::uint32_t in_rate_, out_rate_;
    std::uint16_t channels_;
    std::size_t target_;
    double nominal_, position_{static_cast<double>(half)};
    bool primed_{};
    DriftController drift_;
    BridgeStats stats_;
    std::vector<float> coefficients_;
};
}

#include "patchy/bridge.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace patchy {
namespace {
std::size_t checked_samples(std::size_t frames, std::size_t channels) {
    if (!frames || !channels || channels > 8 || frames > 192000 * 2 ||
        frames > std::numeric_limits<std::size_t>::max() / channels)
        throw std::invalid_argument("invalid bounded audio buffer dimensions");
    return frames * channels;
}
}
FrameRing::FrameRing(std::size_t capacity, std::size_t channels)
    : capacity_(capacity), channels_(channels), data_(checked_samples(capacity, channels)) {}
bool FrameRing::push(const float* input, std::size_t frames) noexcept {
    if (frames > capacity_ - size_) return false;
    for (std::size_t f = 0; f < frames; ++f) {
        const auto offset = ((head_ + size_ + f) % capacity_) * channels_;
        std::copy_n(input + f * channels_, channels_, data_.data() + offset);
    }
    size_ += frames;
    return true;
}
float FrameRing::at(std::size_t frame, std::size_t channel) const noexcept {
    return data_[((head_ + frame) % capacity_) * channels_ + channel];
}
void FrameRing::consume(std::size_t frames) noexcept {
    frames = std::min(frames, size_);
    head_ = (head_ + frames) % capacity_;
    size_ -= frames;
}
void FrameRing::clear() noexcept { head_ = size_ = 0; }
double DriftController::update(double error, double dt) noexcept {
    if (!std::isfinite(error) || !std::isfinite(dt) || dt <= 0) return correction_;
    dt = std::min(dt, 0.1);
    // Slow filtering rejects normal packet-size sawtooth; PI corrects steady drift.
    filtered_ += (dt / (0.5 + dt)) * (error - filtered_);
    integral_ = std::clamp(integral_ + filtered_ * dt * 0.002, -0.0015, 0.0015);
    const double wanted = std::clamp(filtered_ * 0.12 + integral_, -0.002, 0.002);
    const double slew = 0.0002 * dt; // 200 ppm/s
    correction_ += std::clamp(wanted - correction_, -slew, slew);
    return correction_;
}
void DriftController::reset() noexcept { filtered_ = integral_ = correction_ = 0; }
Bridge::Bridge(std::uint32_t input_rate, std::uint32_t output_rate, std::uint16_t channels,
               std::size_t target_frames, std::size_t capacity_frames)
    : ring_(capacity_frames, channels), in_rate_(input_rate), out_rate_(output_rate),
      channels_(channels), target_(target_frames),
      nominal_(output_rate ? static_cast<double>(input_rate) / output_rate : 0),
      coefficients_(taps * (phases + 1)) {
    if (input_rate < 8000 || input_rate > 192000 || output_rate < 8000 ||
        output_rate > 192000 || nominal_ < 0.5 || nominal_ > 2 ||
        target_frames < taps * 2 || target_frames + taps >= capacity_frames)
        throw std::invalid_argument("invalid bridge rate, target or capacity");
    // Reserve a transition band; cutoff accounts for maximum drift correction.
    const double cutoff = 0.45 / std::max(1.0, nominal_ * 1.002);
    for (std::size_t p = 0; p <= phases; ++p) {
        const double frac = static_cast<double>(p) / phases;
        double sum = 0;
        for (std::size_t t = 0; t < taps; ++t) {
            const double x = static_cast<double>(t) - static_cast<double>(half) + 1 - frac;
            const double arg = 2 * std::numbers::pi * cutoff * x;
            const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(arg) / arg;
            const double window = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * t / (taps - 1))
                                      + 0.08 * std::cos(4 * std::numbers::pi * t / (taps - 1));
            const double value = 2 * cutoff * sinc * window;
            coefficients_[p * taps + t] = static_cast<float>(value);
            sum += value;
        }
        for (std::size_t t = 0; t < taps; ++t)
            coefficients_[p * taps + t] = static_cast<float>(coefficients_[p * taps + t] / sum);
    }
}
void Bridge::reset() noexcept {
    ring_.clear();
    position_ = static_cast<double>(half);
    primed_ = false;
    drift_.reset();
    ++stats_.resets;
}
void Bridge::push(const float* input, std::size_t frames) noexcept {
    stats_.input_frames += frames;
    if (!ring_.push(input, frames)) {
        ++stats_.overflows;
        reset();
        // Bound latency by retaining the latest frames that fit, never overwrite unread data.
        if (frames > ring_.capacity()) {
            input += (frames - ring_.capacity()) * channels_;
            frames = ring_.capacity();
        }
        (void)ring_.push(input, frames);
    }
}
void Bridge::pull(float* output, std::size_t frames) noexcept {
    std::fill_n(output, frames * channels_, 0.0f);
    stats_.output_frames += frames;
    if (!primed_) {
        if (ring_.size() < target_ + half) { stats_.silent_frames += frames; return; }
        primed_ = true;
    }
    // Occupancy is sampled just before rendering, so target includes one render quantum.
    const double error = (static_cast<double>(ring_.size()) - target_ - half -
                          static_cast<double>(frames) * nominal_ * 0.5) / in_rate_;
    const double step = nominal_ * (1 + drift_.update(error, static_cast<double>(frames) / out_rate_));
    std::size_t produced = 0;
    for (; produced < frames; ++produced) {
        const auto base = static_cast<std::size_t>(position_);
        if (base + half >= ring_.size()) break;
        const double phase = (position_ - static_cast<double>(base)) * phases;
        const auto p = static_cast<std::size_t>(phase);
        const float alpha = static_cast<float>(phase - static_cast<double>(p));
        for (std::size_t c = 0; c < channels_; ++c) {
            float sum = 0;
            for (std::size_t t = 0; t < taps; ++t) {
                const float a = coefficients_[p * taps + t];
                const float b = coefficients_[(p + 1) * taps + t];
                sum += ring_.at(base - half + 1 + t, c) * (a + alpha * (b - a));
            }
            output[produced * channels_ + c] = sum;
        }
        position_ += step;
    }
    const auto consumed = static_cast<std::size_t>(position_) - half;
    ring_.consume(consumed);
    position_ -= static_cast<double>(consumed);
    if (produced < frames) {
        stats_.silent_frames += frames - produced;
        ++stats_.underruns;
        reset();
    }
}
}

#pragma once
#include "patchy/format.hpp"
#include <array>
#include <span>

namespace patchy {
// One 1-based input channel per output; 0 writes silence. An empty map means
// identity and requires matching channel layouts. Prepare outside audio processing.
std::string validate_channel_map(const Format& input, const Format& output,
                                 std::span<const unsigned> mapping);
class ChannelMap {
public:
    ChannelMap(const Format& input, const Format& output,
               std::span<const unsigned> mapping = {});
    // Buffers must not overlap and must hold frames * their respective channel count.
    // No allocation, mixing or gain change. Input is already decoded finite PCM.
    void process(const float* input, float* output, std::size_t frames) const noexcept;
private:
    unsigned input_channels_{}, output_channels_{};
    std::array<unsigned, 8> sources_{};
};
}

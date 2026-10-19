#include "patchy/channel_map.hpp"
#include <stdexcept>

namespace patchy {
std::string validate_channel_map(const Format& input, const Format& output,
                                 std::span<const unsigned> mapping) {
    if (auto error = validate_format(input); !error.empty()) return "source: " + error;
    if (auto error = validate_format(output); !error.empty()) return "destination: " + error;
    if (mapping.empty()) {
        if (input.channels != output.channels || effective_mask(input) != effective_mask(output))
            return "channel count/layout differs; supply an explicit --map";
    } else {
        if (mapping.size() != output.channels)
            return "channel map must contain one entry per destination channel";
        for (const auto channel : mapping)
            if (channel > input.channels)
                return "channel map references a missing input channel (0 means silence)";
    }
    return {};
}
ChannelMap::ChannelMap(const Format& input, const Format& output,
                       std::span<const unsigned> mapping)
    : input_channels_(input.channels), output_channels_(output.channels) {
    if (auto error = validate_channel_map(input, output, mapping); !error.empty())
        throw std::invalid_argument(error);
    for (unsigned ch = 0; ch < output_channels_; ++ch)
        sources_[ch] = mapping.empty() ? ch + 1 : mapping[ch];
}
void ChannelMap::process(const float* input, float* output, std::size_t frames) const noexcept {
    for (std::size_t frame = 0; frame < frames; ++frame)
        for (unsigned ch = 0; ch < output_channels_; ++ch) {
            const auto source = sources_[ch];
            output[frame * output_channels_ + ch] = source ? input[frame * input_channels_ + source - 1] : 0.0f;
        }
}
}

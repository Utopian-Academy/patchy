#include "patchy/format.hpp"
#include "patchy/channel_map.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace patchy {
std::uint32_t effective_mask(const Format& f) noexcept {
    if (f.channel_mask) return f.channel_mask;
    return f.channels == 1 ? 4u : (f.channels == 2 ? 3u : 0u);
}
std::string validate_format(const Format& f) {
    if (f.rate < 8000 || f.rate > 192000) return "sample rate outside 8000..192000 Hz";
    if (f.channels < 1 || f.channels > 8) return "bridge supports 1..8 channels";
    if (f.encoding == Encoding::unsupported) return "unsupported encoding";
    if (f.encoding == Encoding::float32 && (f.bits != 32 || f.valid_bits != 32))
        return "float format must have 32 container and valid bits";
    if (f.encoding == Encoding::pcm &&
        ((f.bits != 16 && f.bits != 24 && f.bits != 32) ||
         !f.valid_bits || f.valid_bits > f.bits))
        return "PCM requires 16/24/32-bit containers and valid bit count";
    if (f.block_align != f.channels * (f.bits / 8)) return "inconsistent block alignment";
    const auto mask = effective_mask(f);
    if (!mask || std::popcount(mask) != f.channels) return "unknown or inconsistent channel layout";
    return {};
}
std::string validate_route(std::wstring_view source, std::wstring_view destination,
                           const Format& input, const Format& output, std::span<const unsigned> mapping) {
    if (source.empty() || destination.empty()) return "two explicit endpoint identities are required";
    if (source == destination) return "feedback protection: source and destination are the same endpoint";
    if (auto error = validate_channel_map(input, output, mapping); !error.empty()) return error;
    const double ratio = static_cast<double>(input.rate) / output.rate;
    if (ratio < 0.5 || ratio > 2.0) return "initial SRC supports rate ratios from 1:2 to 2:1";
    return {};
}
void decode(const std::byte* input, float* output, std::size_t frames,
            const Format& f, bool silent) noexcept {
    const auto samples = frames * f.channels;
    if (silent) { std::fill_n(output, samples, 0.0f); return; }
    const auto bytes = f.bits / 8;
    for (std::size_t i = 0; i < samples; ++i) {
        if (f.encoding == Encoding::float32) {
            float v{};
            std::memcpy(&v, input + i * 4, 4);
            output[i] = std::isfinite(v) ? v : 0.0f;
        } else {
            std::uint32_t raw{};
            for (unsigned b = 0; b < static_cast<unsigned>(bytes); ++b)
                raw |= std::to_integer<std::uint32_t>(input[i * bytes + b]) << (8 * b);
            // Sign-extend via 64-bit arithmetic; no signed left-shift or aliasing UB.
            std::int64_t v = raw;
            if (raw & (std::uint32_t{1} << (f.bits - 1))) v -= std::int64_t{1} << f.bits;
            output[i] = static_cast<float>(static_cast<double>(v) /
                                          static_cast<double>(std::uint64_t{1} << (f.bits - 1)));
        }
    }
}
void encode(const float* input, std::byte* output, std::size_t frames, const Format& f) noexcept {
    const auto bytes = f.bits / 8;
    for (std::size_t i = 0; i < frames * f.channels; ++i) {
        const float v = std::isfinite(input[i]) ? input[i] : 0.0f;
        if (f.encoding == Encoding::float32) {
            // Do not clip the shared-mode floating point pipeline.
            std::memcpy(output + i * 4, &v, 4);
        } else {
            const auto scale = std::int64_t{1} << (f.valid_bits - 1);
            auto q = static_cast<std::int64_t>(std::llround(
                std::clamp(static_cast<double>(v), -1.0, 1.0) * static_cast<double>(scale)));
            q = std::clamp(q, -scale, scale - 1);
            q *= std::int64_t{1} << (f.bits - f.valid_bits);
            const auto raw = static_cast<std::uint32_t>(q);
            for (unsigned b = 0; b < static_cast<unsigned>(bytes); ++b)
                output[i * bytes + b] = static_cast<std::byte>((raw >> (8 * b)) & 255);
        }
    }
}
}

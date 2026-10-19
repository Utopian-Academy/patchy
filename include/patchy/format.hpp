#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace patchy {
enum class Encoding { pcm, float32, unsupported };
struct Format {
    std::uint32_t rate{};
    std::uint16_t channels{}, bits{}, valid_bits{}, block_align{};
    std::uint32_t channel_mask{};
    Encoding encoding{Encoding::unsupported};
};
std::string validate_format(const Format&);
std::string validate_route(std::wstring_view source, std::wstring_view destination,
                           const Format& input, const Format& output,
                           std::span<const unsigned> mapping = {});
std::uint32_t effective_mask(const Format&) noexcept;
// Caller supplies complete buffers (frames * block_align / channels). No allocation.
// PCM is Windows little-endian; valid bits in PCM containers are left-aligned.
void decode(const std::byte* input, float* output, std::size_t frames,
            const Format&, bool silent = false) noexcept;
void encode(const float* input, std::byte* output, std::size_t frames,
            const Format&) noexcept;
}

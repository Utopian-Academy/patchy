#include "patchy/channel_map.hpp"
#include "patchy/bridge.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
patchy::Format format(unsigned channels, unsigned mask) {
    return {48000, static_cast<std::uint16_t>(channels), 32, 32,
            static_cast<std::uint16_t>(channels * 4), mask, patchy::Encoding::float32};
}
void rejects(const patchy::Format& input, const patchy::Format& output,
             std::span<const unsigned> mapping) {
    try { patchy::ChannelMap map(input, output, mapping); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid channel map accepted");
}
}
int main() {
    try {
        const auto mono = format(1, 4), stereo = format(2, 3), surround = format(8, 0x63f);
        const std::array<float, 6> input{1, 10, 2, 20, 3, 30};
        std::array<float, 8> output; output.fill(-99);
        patchy::ChannelMap identity(stereo, stereo);
        identity.process(input.data(), output.data() + 1, 3);
        require(std::equal(input.begin(), input.end(), output.begin() + 1), "identity changed samples");
        require(output.front() == -99 && output.back() == -99, "mapping overwrote buffer bounds");
        patchy::ChannelMap swap(stereo, stereo, std::array{2u, 1u});
        swap.process(input.data(), output.data(), 3);
        require(std::equal(output.begin(), output.begin() + 6, std::array{10.f, 1.f, 20.f, 2.f, 30.f, 3.f}.begin()),
                "channel swap interleaving failed");
        patchy::ChannelMap select(stereo, mono, std::array{2u});
        select.process(input.data(), output.data(), 3);
        require(output[0] == 10 && output[1] == 20 && output[2] == 30, "input channel selection failed");
        patchy::ChannelMap duplicate(mono, stereo, std::array{1u, 1u});
        const std::array<float, 3> mono_input{0.25f, -0.5f, 0.75f};
        duplicate.process(mono_input.data(), output.data(), 3);
        require(output[0] == .25f && output[1] == .25f && output[2] == -.5f &&
                output[3] == -.5f && output[4] == .75f && output[5] == .75f, "mono duplication changed gain");
        patchy::ChannelMap sparse(stereo, surround, std::array{2u, 1u, 0u, 0u, 1u, 2u, 0u, 0u});
        sparse.process(input.data(), output.data(), 1);
        require(output == std::array{10.f, 1.f, 0.f, 0.f, 1.f, 10.f, 0.f, 0.f}, "8-channel assignment/silence failed");
        const auto saved = output;
        sparse.process(nullptr, output.data(), 0);
        require(output == saved, "zero-frame processing touched output");
        rejects(mono, stereo, {});
        rejects(stereo, format(2, 12), {});
        rejects(stereo, stereo, std::array{1u});
        rejects(mono, stereo, std::array{1u, 2u});
        rejects(stereo, stereo, std::array{1u, 2u, 0u});
        rejects(format(8, 0), surround, std::array{1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u});
        require(patchy::validate_route(L"a", L"b", mono, stereo, std::array{1u, 1u}).empty(),
                "explicit mapping rejected by route negotiation");
        require(!patchy::validate_route(L"same", L"same", mono, stereo, std::array{1u, 1u}).empty(),
                "mapping bypassed feedback protection");
        // Exercise the prepared mono -> stereo -> adaptive SRC path over many blocks.
        patchy::Bridge bridge(48000, 44100, 2, 1440, 8192);
        patchy::ChannelMap left_only(mono, stereo, std::array{1u, 0u});
        std::array<float, 480> samples; samples.fill(.25f);
        std::array<float, 960> mapped{};
        std::array<float, 882> rendered{};
        for (unsigned block = 0; block < 100; ++block) {
            left_only.process(samples.data(), mapped.data(), samples.size());
            bridge.push(mapped.data(), samples.size());
            bridge.pull(rendered.data(), rendered.size() / 2);
            if (block > 10)
                for (std::size_t i = 0; i < rendered.size(); i += 2)
                    require(std::abs(rendered[i] - .25f) < 1e-5f && rendered[i + 1] == 0,
                            "mapped SRC lost gain or channel isolation");
        }
        require(!bridge.stats().underruns && !bridge.stats().overflows, "mapped SRC queue failed");
        std::cout << "PASS channel selection, duplication, silence, bounds, validation and mapped SRC\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

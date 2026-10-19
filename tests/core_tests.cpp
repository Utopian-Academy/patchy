#include "patchy/bridge.hpp"
#include "patchy/clock.hpp"
#include "patchy/format.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
patchy::Format format(unsigned rate = 48000, unsigned bits = 32,
                      patchy::Encoding encoding = patchy::Encoding::float32) {
    return {rate, 2, static_cast<std::uint16_t>(bits), static_cast<std::uint16_t>(bits),
            static_cast<std::uint16_t>(bits / 8 * 2), 3, encoding};
}
void formats() {
    auto f = format();
    require(patchy::validate_format(f).empty(), "valid float format rejected");
    require(!patchy::validate_route(L"same", L"same", f, f).empty(), "self-route allowed");
    require(patchy::validate_route(L"a", L"b", f, format(44100)).empty(), "44.1/48 route rejected");
    auto bad = f; bad.channel_mask = 4;
    require(!patchy::validate_format(bad).empty(), "bad channel mask allowed");
    bad = f; bad.block_align = 1;
    require(!patchy::validate_format(bad).empty(), "bad alignment allowed");
    bad = f; bad.rate = 0;
    require(!patchy::validate_format(bad).empty(), "zero rate allowed");
    bad = f; bad.channels = 6; bad.channel_mask = 0; bad.block_align = 24;
    require(!patchy::validate_format(bad).empty(), "unknown multichannel layout allowed");
    bad = f; bad.channel_mask = 12;
    require(!patchy::validate_route(L"a", L"b", f, bad).empty(), "different layouts allowed");
    require(!patchy::validate_route(L"a", L"b", f, format(8000)).empty(), "unsupported ratio allowed");
}
void pcm() {
    const std::array<float, 8> input{-1, -0.5f, 0, 0.5f, 1, 2, -2, 0.1234f};
    for (unsigned bits : {16u, 24u, 32u}) {
        auto f = format(48000, bits, patchy::Encoding::pcm);
        std::array<std::byte, 32> bytes{};
        std::array<float, 8> output{};
        patchy::encode(input.data(), bytes.data(), 4, f);
        patchy::decode(bytes.data(), output.data(), 4, f);
        for (std::size_t i = 0; i < input.size(); ++i)
            require(std::abs(output[i] - std::clamp(input[i], -1.0f, 1.0f)) < 0.00004f,
                    "PCM roundtrip/clipping failed");
        f.valid_bits = 16;
        patchy::encode(input.data(), bytes.data(), 4, f);
        patchy::decode(bytes.data(), output.data(), 4, f);
        require(std::abs(output[1] + 0.5f) < 1e-6f, "left-aligned PCM failed");
    }
    auto f = format();
    std::array<float, 2> nonfinite{std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity()}, clean{};
    patchy::decode(reinterpret_cast<const std::byte*>(nonfinite.data()), clean.data(), 1, f);
    require(clean[0] == 0 && clean[1] == 0, "nonfinite audio propagated");
    clean.fill(1);
    patchy::decode(nullptr, clean.data(), 1, f, true);
    require(clean[0] == 0 && clean[1] == 0, "null silent buffer mishandled");
}
void ring() {
    patchy::FrameRing ring(4, 2);
    const float input[]{1, 10, 2, 20, 3, 30, 4, 40};
    require(ring.push(input, 3), "ring push");
    ring.consume(2);
    require(ring.push(input, 3), "wrapped push");
    require(!ring.push(input, 1), "overflow not rejected");
    require(ring.at(0, 0) == 3 && ring.at(0, 1) == 30 &&
            ring.at(3, 0) == 3 && ring.at(3, 1) == 30, "channel order after wrap");
    ring.consume(100);
    require(ring.size() == 0, "consume bound");
}
void clocks() {
    patchy::ClockRateWindow window;
    require(!window.observe(0, 1000000, 48000), "clock origin produced a rate");
    require(window.observe(96048, 21000000, 48000), "clock window not ready");
    require(std::abs(*window.rate() - 1.0005) < 1e-8, "clock ppm calculation");
    require(!window.observe(0, 22000000, 48000) && !window.rate(), "clock rollback not reset");
    require(window.observe(90000, 42000000, 48000), "second clock window not ready");
    require(std::abs(*window.rate() - 0.9375) < 1e-8, "gross clock mismatch not measured");
    require(!window.observe(90001, 42000000, 48000) && !window.rate(), "stale QPC accepted");
    window.reset();
    require(!window.rate(), "clock reset retained rate");
    require(!window.observe(0, 0, 48000), "invalid timestamp accepted");
}
void drift() {
    for (double hardware_ppm : {-500.0, 500.0}) {
        patchy::DriftController controller;
        double occupancy = 0;
        for (int n = 0; n < 120000; ++n) {
            const double correction = controller.update(occupancy, 0.01);
            occupancy += (hardware_ppm / 1e6 - correction) * 0.01;
            require(std::abs(occupancy) < 0.015, "drift queue unbounded");
            require(std::abs(controller.ppm()) <= 2000.01, "correction out of bounds");
        }
        require(std::abs(controller.ppm() - hardware_ppm) < 1.0, "drift failed to converge");
        controller.reset();
        require(controller.ppm() == 0, "drift reset");
    }
}
double convert_tone(unsigned input_rate, unsigned output_rate, double hz) {
    const unsigned in_block = input_rate / 100, out_block = output_rate / 100;
    patchy::Bridge bridge(input_rate, output_rate, 2, input_rate * 30 / 1000, input_rate / 2);
    std::vector<float> input(in_block * 2), output(out_block * 2);
    std::uint64_t frame = 0, measured = 0;
    double energy = 0;
    for (unsigned block = 0; block < 150; ++block) {
        for (unsigned n = 0; n < in_block; ++n, ++frame) {
            input[n * 2] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * hz * frame / input_rate));
            input[n * 2 + 1] = 0; // Detect accidental channel crosstalk.
        }
        bridge.push(input.data(), in_block);
        bridge.pull(output.data(), out_block);
        if (block > 20) {
            for (unsigned n = 0; n < out_block; ++n) {
                energy += output[n * 2] * output[n * 2];
                ++measured;
                require(output[n * 2 + 1] == 0, "resampler channel crosstalk");
                require(std::isfinite(output[n * 2]), "resampler nonfinite");
            }
        }
    }
    require(bridge.stats().overflows == 0 && bridge.stats().underruns == 0, "steady SRC xrun");
    return std::sqrt(energy / static_cast<double>(measured));
}
void resampling() {
    for (const auto rates : {std::pair{44100u, 48000u}, std::pair{48000u, 44100u},
                             std::pair{48000u, 48000u}, std::pair{96000u, 48000u}}) {
        const auto rms = convert_tone(rates.first, rates.second, 1000);
        require(std::abs(rms - 0.353553) < 0.003, "SRC passband gain");
    }
    require(convert_tone(96000, 48000, 36000) < 0.001, "SRC downsample alias rejection");
    patchy::Bridge b(48000, 48000, 1, 480, 2048);
    std::array<float, 4096> audio{}; audio.fill(0.5f);
    std::array<float, 128> output{}; output.fill(99);
    b.pull(output.data(), output.size());
    require(std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }), "priming silence");
    b.push(audio.data(), 1000);
    for (int i = 0; i < 10; ++i) b.pull(output.data(), output.size());
    require(b.stats().underruns == 1, "underrun not counted/reprimed");
    b.push(audio.data(), audio.size());
    require(b.stats().overflows == 1 && b.queued() <= 2048, "overflow not bounded");
    b.reset();
    require(b.queued() == 0, "reset kept stale audio");
}
}
int main() {
    unsigned passed = 0;
    struct Test { const char* name; void (*run)(); };
    const Test tests[]{{"formats and feedback", formats}, {"PCM and silence", pcm},
                       {"ring wrap and overflow", ring}, {"clock drift", drift},
                       {"SRC and recovery", resampling}, {"clock timestamps", clocks}};
    for (const auto& test : tests) {
        try { test.run(); ++passed; std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n'; return 1;
        }
    }
    std::cout << passed << " processing test groups passed\n";
}

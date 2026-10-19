// Opt-in live signal smoke test. Emits a quiet (-46 dBFS peak) 997 Hz tone.
// Captures destination loopback only into a reusable buffer; never records a file.
#include "common.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>
#include <vector>

using namespace patchy;
using namespace patchy::win;
namespace {
struct RunningRoute {
    Handle cancel{true};
    RouteStats stats;
    std::jthread worker;

    explicit RunningRoute(RouteOptions options)
        : worker([this, options] { (void)run_route(options, cancel.get(), stats); }) {}
    ~RunningRoute() { SetEvent(cancel.get()); worker.join(); }
};
struct Stopper {
    IAudioClient* client;
    ~Stopper() { client->Stop(); }
};
struct Spectrum {
    double re{}, im{}, neighbor_re{}, neighbor_im{};
    std::uint64_t frames{};
    void add(const float* data, UINT32 count, const Format& format) {
        for (UINT32 i = 0; i < count; ++i, ++frames) {
            const double t = static_cast<double>(frames) / format.rate;
            const double v = data[static_cast<std::size_t>(i) * format.channels];
            re += v * std::cos(2 * std::numbers::pi * 997 * t);
            im += v * std::sin(2 * std::numbers::pi * 997 * t);
            neighbor_re += v * std::cos(2 * std::numbers::pi * 1559 * t);
            neighbor_im += v * std::sin(2 * std::numbers::pi * 1559 * t);
        }
    }
    double tone() const { return frames ? 2 * std::hypot(re, im) / frames : 0; }
    double neighbor() const { return frames ? 2 * std::hypot(neighbor_re, neighbor_im) / frames : 0; }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3 && argc != 4) {
            std::cerr << "Usage: patchy_signal_test SOURCE_OUTPUT_ID DESTINATION_OUTPUT_ID [QUEUE_MS]\n"
                         "Emits a quiet 997 Hz tone for approximately six seconds.\n";
            return 2;
        }
        ComApartment apartment;
        const auto source_id = resolve_render_id(argv[1]), destination_id = resolve_render_id(argv[2]);
        if (source_id == destination_id) throw std::invalid_argument("same-endpoint test refused");
        Handle source_event, destination_event;
        auto e = enumerator();
        ComPtr<IMMDevice> source, destination;
        check(e->GetDevice(source_id.c_str(), &source), "test source");
        check(e->GetDevice(destination_id.c_str(), &destination), "test destination");
        auto producer = activate(source.Get()), observer = activate(destination.Get());
        MixFormat source_mix(producer.Get()), destination_mix(observer.Get());
        const auto input = parse_format(source_mix.value), output = parse_format(destination_mix.value);
        if (const auto error = validate_route(source_id, destination_id, input, output); !error.empty())
            throw std::invalid_argument(error);
        check(producer->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  0, 0, source_mix.value, nullptr), "test producer initialize");
        check(observer->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  0, 0, destination_mix.value, nullptr), "test observer initialize");
        check(producer->SetEventHandle(source_event.get()), "producer event");
        check(observer->SetEventHandle(destination_event.get()), "observer event");
        UINT32 source_capacity{}, destination_capacity{};
        check(producer->GetBufferSize(&source_capacity), "producer capacity");
        check(observer->GetBufferSize(&destination_capacity), "observer capacity");
        if (!source_capacity || !destination_capacity || source_capacity > input.rate ||
            destination_capacity > output.rate) throw std::runtime_error("test buffer bound exceeded");
        std::vector<float> generated(static_cast<std::size_t>(source_capacity) * input.channels);
        std::vector<float> observed(static_cast<std::size_t>(destination_capacity) * output.channels);
        ComPtr<IAudioRenderClient> render;
        ComPtr<IAudioCaptureClient> capture;
        check(producer->GetService(IID_PPV_ARGS(&render)), "test render service");
        check(observer->GetService(IID_PPV_ARGS(&capture)), "test capture service");
        unsigned queue_ms = 30;
        if (argc == 4) {
            const std::wstring value = argv[3];
            if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                throw std::invalid_argument("QUEUE_MS requires decimal digits");
            const auto requested = std::stoul(value);
            if (requested < 10 || requested > 200) throw std::invalid_argument("QUEUE_MS must be 10..200");
            queue_ms = static_cast<unsigned>(requested);
        }
        RunningRoute route({source_id, destination_id, queue_ms});
        const auto opening = GetTickCount64();
        while (route.stats.state.load() != RouteState::running) {
            if (route.stats.state.load() == RouteState::failed || GetTickCount64() - opening > 5000)
                throw std::runtime_error("route did not start within five seconds");
            Sleep(10);
        }
        check(observer->Start(), "observer start");
        Stopper stop_observer{observer.Get()};
        BYTE* prime{};
        check(render->GetBuffer(source_capacity, &prime), "producer prime");
        check(render->ReleaseBuffer(source_capacity, AUDCLNT_BUFFERFLAGS_SILENT), "producer prime release");
        check(producer->Start(), "producer start");
        Stopper stop_producer{producer.Get()};
        std::uint64_t source_frame{};
        Spectrum baseline, signal;
        unsigned last_second{};
        std::uint64_t discontinuities{};
        const auto start = GetTickCount64();
        HANDLE events[]{source_event.get(), destination_event.get()};
        while (GetTickCount64() - start < 8000) {
            const auto wait = WaitForMultipleObjects(2, events, FALSE, 250);
            if (wait == WAIT_FAILED) throw std::runtime_error("test wait failed");
            const auto elapsed = GetTickCount64() - start;
            const auto second = static_cast<unsigned>(elapsed / 1000);
            if (second != last_second) {
                last_second = second;
                std::cout << "second=" << second << " produced=" << source_frame
                          << " captured=" << route.stats.captured.load()
                          << " rendered=" << route.stats.rendered.load()
                          << " queue=" << route.stats.queued.load()
                          << " underruns=" << route.stats.underruns.load()
                          << " discontinuities=" << route.stats.discontinuities.load()
                          << " ppm=" << route.stats.correction_ppm.load() << " timestamp-rate-error-ppm=" << route.stats.timestamp_rate_error_ppm.load() << '\n';
            }
            if (wait == WAIT_OBJECT_0) {
                UINT32 padding{};
                check(producer->GetCurrentPadding(&padding), "test producer padding");
                if (padding > source_capacity) throw std::runtime_error("invalid producer padding");
                const auto frames = source_capacity - padding;
                if (frames) {
                    for (UINT32 i = 0; i < frames; ++i, ++source_frame) {
                        const float sample = elapsed >= 1500
                            ? static_cast<float>(0.005 * std::sin(2 * std::numbers::pi * 997 * source_frame / input.rate))
                            : 0.0f;
                        for (unsigned c = 0; c < input.channels; ++c)
                            generated[static_cast<std::size_t>(i) * input.channels + c] = c == 0 ? sample : 0;
                    }
                    BYTE* data{};
                    check(render->GetBuffer(frames, &data), "test producer buffer");
                    encode(generated.data(), reinterpret_cast<std::byte*>(data), frames, input);
                    check(render->ReleaseBuffer(frames, 0), "test producer release");
                }
            }
            for (unsigned packets = 0; packets < 64; ++packets) {
                UINT32 next{};
                check(capture->GetNextPacketSize(&next), "observer next packet");
                if (!next) break;
                BYTE* data{}; UINT32 frames{}; DWORD flags{};
                check(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "observer buffer");
                if (frames > destination_capacity) {
                    capture->ReleaseBuffer(frames);
                    throw std::runtime_error("observer packet exceeds bound");
                }
                decode(reinterpret_cast<const std::byte*>(data), observed.data(), frames, output,
                       (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                check(capture->ReleaseBuffer(frames), "observer release");
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities;
                if (elapsed >= 500 && elapsed < 1300) baseline.add(observed.data(), frames, output);
                if (elapsed >= 2500) signal.add(observed.data(), frames, output);
            }
        }
        SetEvent(route.cancel.get());
        std::cout << "source-rate=" << input.rate << " destination-rate=" << output.rate
                  << " observed-frames=" << signal.frames << " baseline-997-peak=" << baseline.tone()
                  << " signal-997-peak=" << signal.tone() << " neighbor-1559-peak=" << signal.neighbor()
                  << " observer-discontinuities=" << discontinuities
                  << " route-underruns=" << route.stats.underruns.load()
                  << " route-overflows=" << route.stats.overflows.load() << '\n';
        const bool detected = signal.frames > output.rate && signal.tone() > 0.00005 &&
                              signal.tone() > baseline.tone() * 5 &&
                              signal.tone() > signal.neighbor() * 10;
        std::cout << (detected ? "PASS" : "FAIL")
                  << " destination loopback tone detection (not an analog latency/fidelity measurement)\n";
        const bool healthy = route.stats.state.load() != RouteState::failed &&
                             route.stats.underruns.load() == 0 && route.stats.overflows.load() == 0;
        std::cout << (healthy ? "PASS" : "FAIL") << " route health during signal test\n";
        return detected && healthy ? 0 : 1;
    } catch (const AudioError& error) {
        std::cerr << error.what() << ": " << error_text(error.code) << '\n';
        return 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

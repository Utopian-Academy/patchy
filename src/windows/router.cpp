#include "common.hpp"
#include "patchy/bridge.hpp"
#include "patchy/clock.hpp"
#include "patchy/channel_map.hpp"
#include <cmath>
#include <avrt.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace patchy::win {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<long>::is_always_lock_free);
struct Mmcss {
    HANDLE handle{};
    Mmcss() { DWORD task{}; handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task); }
    ~Mmcss() { if (handle) AvRevertMmThreadCharacteristics(handle); }
};
struct Streams {
    ComPtr<IAudioClient> capture, render;
    bool capture_started{}, render_started{};
    void stop() noexcept {
        if (capture_started) { capture->Stop(); capture_started = false; }
        if (render_started) { render->Stop(); render_started = false; }
    }
    ~Streams() { stop(); }
};
bool transient(HRESULT hr) {
    return hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
           hr == AUDCLNT_E_SERVICE_NOT_RUNNING || hr == AUDCLNT_E_DEVICE_IN_USE ||
           hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
           hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}
struct AttemptResult { HRESULT hr{S_OK}; const char* operation{"stopped"}; bool ran{}; };
void require_direction(IMMDevice* device, EDataFlow expected) {
    ComPtr<IMMEndpoint> endpoint;
    check(device->QueryInterface(IID_PPV_ARGS(&endpoint)), "query stream endpoint direction");
    EDataFlow flow{};
    check(endpoint->GetDataFlow(&flow), "stream endpoint direction");
    if (flow != expected) throw std::invalid_argument(expected == eCapture
        ? "monitor source must be an input endpoint" : "selected stream endpoint must be an output");
}
AttemptResult attempt(const RouteOptions& options, HANDLE cancel, RouteStats& stats) {
    stats.state.store(RouteState::opening);
    // Events precede stream objects, so they outlive COM client release.
    Handle capture_event, render_event;
    Streams streams;
    auto e = enumerator();
    ComPtr<IMMDevice> source, destination;
    check(e->GetDevice(options.source_id.c_str(), &source), "open selected source");
    check(e->GetDevice(options.destination_id.c_str(), &destination), "open selected destination");
    const bool input_source = options.source_kind == SourceKind::input;
    require_direction(source.Get(), input_source ? eCapture : eRender);
    require_direction(destination.Get(), eRender);
    DWORD state{};
    check(source->GetState(&state), "source state");
    if (!(state & DEVICE_STATE_ACTIVE)) throw AudioError(AUDCLNT_E_DEVICE_INVALIDATED, "source inactive");
    check(destination->GetState(&state), "destination state");
    if (!(state & DEVICE_STATE_ACTIVE)) throw AudioError(AUDCLNT_E_DEVICE_INVALIDATED, "destination inactive");
    streams.capture = activate(source.Get());
    streams.render = activate(destination.Get());
    MixFormat source_mix(streams.capture.Get()), destination_mix(streams.render.Get());
    const Format input = parse_format(source_mix.value), output = parse_format(destination_mix.value);
    if (auto message = validate_route(device_id(source.Get()), device_id(destination.Get()), input, output,
                                     options.channel_map);
        !message.empty()) throw std::invalid_argument(message);
    ChannelMap channel_map(input, output, options.channel_map);
    check(streams.capture->Initialize(AUDCLNT_SHAREMODE_SHARED,
          AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (input_source ? 0u : AUDCLNT_STREAMFLAGS_LOOPBACK),
          0, 0, source_mix.value, nullptr), input_source ? "initialize event input capture" : "initialize event loopback");
    check(streams.capture->SetEventHandle(capture_event.get()), "capture SetEventHandle");

    bool low_latency = false;
    UINT32 period{};
    {
        ComPtr<IAudioClient3> client3;
        if (SUCCEEDED(streams.render.As(&client3))) {
            UINT32 normal{}, fundamental{}, minimum{}, maximum{};
            if (SUCCEEDED(client3->GetSharedModeEnginePeriod(destination_mix.value,
                    &normal, &fundamental, &minimum, &maximum)) && minimum && fundamental) {
                const auto hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                 minimum, destination_mix.value, nullptr);
                low_latency = SUCCEEDED(hr);
                if (low_latency) period = minimum;
            }
        }
    }
    if (!low_latency) {
        // A failed Initialize cannot safely be retried on the same IAudioClient.
        streams.render = activate(destination.Get());
        check(streams.render->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                         0, 0, destination_mix.value, nullptr), "initialize shared render fallback");
        REFERENCE_TIME normal{}, minimum{};
        check(streams.render->GetDevicePeriod(&normal, &minimum), "render device period");
        period = static_cast<UINT32>((normal * output.rate) / 10000000);
    }
    check(streams.render->SetEventHandle(render_event.get()), "render SetEventHandle");
    UINT32 capture_frames{}, render_frames{};
    check(streams.capture->GetBufferSize(&capture_frames), "capture buffer size");
    check(streams.render->GetBufferSize(&render_frames), "render buffer size");
    if (!capture_frames || !render_frames || capture_frames > input.rate * 2 || render_frames > output.rate * 2)
        throw std::runtime_error("endpoint buffer dimensions exceed the supported bound");
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    check(streams.capture->GetService(IID_PPV_ARGS(&capture)), "IAudioCaptureClient");
    check(streams.render->GetService(IID_PPV_ARGS(&render)), "IAudioRenderClient");
    ComPtr<IAudioClock> render_clock;
    UINT64 render_frequency{};
    if (SUCCEEDED(streams.render->GetService(IID_PPV_ARGS(&render_clock)))) {
        if (FAILED(render_clock->GetFrequency(&render_frequency))) render_clock.Reset();
    }
    ClockRateWindow source_clock_window, render_clock_window;
    unsigned bad_clock_windows{};
    const auto target = std::max<std::size_t>({128,
        static_cast<std::size_t>(input.rate) * options.queue_ms / 1000,
        static_cast<std::size_t>(capture_frames) * 2});
    const auto capacity = std::max<std::size_t>(target * 4 + 128, input.rate / 2);
    Bridge bridge(input.rate, output.rate, output.channels, target, capacity);
    std::vector<float> decoded(static_cast<std::size_t>(capture_frames) * input.channels);
    std::vector<float> mapped(static_cast<std::size_t>(capture_frames) * output.channels);
    std::vector<float> converted(static_cast<std::size_t>(render_frames) * output.channels);
    stats.queue_target.store(static_cast<UINT32>(target));
    stats.source_buffer.store(capture_frames);
    stats.destination_buffer.store(render_frames);
    stats.source_rate.store(input.rate);
    stats.destination_rate.store(output.rate);
    stats.render_period.store(period);
    stats.low_latency.store(low_latency);
    BYTE* prime{};
    check(render->GetBuffer(render_frames, &prime), "prime render buffer");
    check(render->ReleaseBuffer(render_frames, AUDCLNT_BUFFERFLAGS_SILENT), "release priming silence");
    check(streams.capture->Start(), "start capture");
    streams.capture_started = true;
    check(streams.render->Start(), "start render");
    streams.render_started = true;
    stats.successful_starts.fetch_add(1);
    stats.state.store(RouteState::running);
    stats.last_error.store(S_OK);
    const auto captured_base = stats.captured.load(), rendered_base = stats.rendered.load();
    const auto silent_base = stats.silent.load(), under_base = stats.underruns.load(), over_base = stats.overflows.load();
    auto publish = [&]() noexcept {
        const auto& s = bridge.stats();
        stats.captured.store(captured_base + s.input_frames, std::memory_order_relaxed);
        stats.rendered.store(rendered_base + s.output_frames, std::memory_order_relaxed);
        stats.silent.store(silent_base + s.silent_frames, std::memory_order_relaxed);
        stats.underruns.store(under_base + s.underruns, std::memory_order_relaxed);
        stats.overflows.store(over_base + s.overflows, std::memory_order_relaxed);
        stats.queued.store(static_cast<UINT32>(bridge.queued()), std::memory_order_relaxed);
        stats.correction_ppm.store(static_cast<std::int32_t>(bridge.correction_ppm()), std::memory_order_relaxed);
    };
    AttemptResult result{S_OK, "stopped", true};
    {
        Mmcss mmcss;
        stats.mmcss.store(mmcss.handle != nullptr);
        HANDLE events[]{cancel, capture_event.get(), render_event.get()};
        ULONGLONG last_render = GetTickCount64();
        ULONGLONG last_capture = last_render;
        bool done = false;
        while (!done) {
            const DWORD wait = WaitForMultipleObjects(3, events, FALSE, 250);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED) { result = {HRESULT_FROM_WIN32(GetLastError()), "wait for audio event", true}; break; }
            // Capture drains are capped so capture cannot starve rendering or cancellation.
            for (unsigned packets = 0; packets < 64; ++packets) {
                UINT32 next{};
                HRESULT hr = capture->GetNextPacketSize(&next);
                if (FAILED(hr)) { result = {hr, "capture packet size", true}; done = true; break; }
                if (!next) break;
                BYTE* data{};
                UINT32 frames{};
                DWORD flags{};
                UINT64 device_position{}, qpc{};
                hr = capture->GetBuffer(&data, &frames, &flags, &device_position, &qpc);
                if (FAILED(hr)) { result = {hr, "capture GetBuffer", true}; done = true; break; }
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                    stats.discontinuities.fetch_add(1, std::memory_order_relaxed);
                    bridge.reset();
                    source_clock_window.reset();
                    bad_clock_windows = 0;
                }
                if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
                    stats.timestamp_errors.fetch_add(1, std::memory_order_relaxed);
                    source_clock_window.reset();
                    bad_clock_windows = 0;
                }
                if (frames > capture_frames) {
                    capture->ReleaseBuffer(frames);
                    result = {E_UNEXPECTED, "capture packet exceeds prepared buffer", true}; done = true; break;
                }
                decode(reinterpret_cast<const std::byte*>(data), decoded.data(), frames,
                       input, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                channel_map.process(decoded.data(), mapped.data(), frames);
                bridge.push(mapped.data(), frames);
                if (frames) last_capture = GetTickCount64();
                hr = capture->ReleaseBuffer(frames);
                if (FAILED(hr)) { result = {hr, "capture ReleaseBuffer", true}; done = true; break; }
                if (frames && !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) &&
                    source_clock_window.observe(device_position, qpc, input.rate)) {
                    const auto source_rate = source_clock_window.rate();
                    const auto destination_rate = render_clock_window.rate();
                    if (source_rate && destination_rate && *destination_rate > 0) {
                        const double error = *source_rate / *destination_rate - 1.0;
                        stats.timestamp_rate_error_ppm.store(static_cast<std::int32_t>(
                            std::clamp(error * 1e6, -1000000.0, 1000000.0)), std::memory_order_relaxed);
                        // Conservative guard, not a precision drift estimator: require two
                        // completed windows beyond 5000 ppm. Normal correction is +/-2000 ppm.
                        bad_clock_windows = std::abs(error) > 0.005 ? bad_clock_windows + 1 : 0;
                        if (bad_clock_windows >= 2) {
                            result = {MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201),
                                "endpoint timestamp rates exceed prototype clock-correction range; inspect driver/sample-rate configuration", true};
                            done = true;
                            break;
                        }
                    }
                }
            }
            if (done) break;
            // Only submit render data on its event, not on capture wakeups.
            if (wait == WAIT_OBJECT_0 + 2) {
                last_render = GetTickCount64();
                if (render_clock) {
                    UINT64 position{}, qpc{};
                    const auto clock_hr = render_clock->GetPosition(&position, &qpc);
                    if (clock_hr == S_OK) render_clock_window.observe(position, qpc, render_frequency);
                    else { render_clock_window.reset(); bad_clock_windows = 0; }
                }
                UINT32 padding{};
                HRESULT hr = streams.render->GetCurrentPadding(&padding);
                if (FAILED(hr)) { result = {hr, "render padding", true}; break; }
                if (padding > render_frames) { result = {E_UNEXPECTED, "invalid render padding", true}; break; }
                const auto available = render_frames - padding;
                if (available) {
                    BYTE* data{};
                    hr = render->GetBuffer(available, &data);
                    if (FAILED(hr)) { result = {hr, "render GetBuffer", true}; break; }
                    bridge.pull(converted.data(), available);
                    encode(converted.data(), reinterpret_cast<std::byte*>(data), available, output);
                    hr = render->ReleaseBuffer(available, 0);
                    if (FAILED(hr)) { result = {hr, "render ReleaseBuffer", true}; break; }
                }
            }
            publish();
            if (GetTickCount64() - last_render > 3000) {
                result = {HRESULT_FROM_WIN32(ERROR_TIMEOUT), "render event watchdog", true}; break;
            }
            // Input capture should deliver packets even when silent. Idle loopback
            // need not, so only apply the packet watchdog to real capture endpoints.
            if (input_source && GetTickCount64() - last_capture > 3000) {
                result = {HRESULT_FROM_WIN32(ERROR_TIMEOUT), "input capture packet watchdog", true}; break;
            }
        }
        publish();
    } // Leave MMCSS before stop/release, exceptions or logging.
    streams.stop();
    return result;
}
}
int run_route(const RouteOptions& options, HANDLE cancel, RouteStats& stats) {
    try {
        if (options.queue_ms < 10 || options.queue_ms > 200)
            throw std::invalid_argument("queue target must be 10..200 ms");
        if (options.source_kind != SourceKind::loopback && options.source_kind != SourceKind::input)
            throw std::invalid_argument("unknown source kind");
        if (!cancel || cancel == INVALID_HANDLE_VALUE)
            throw std::invalid_argument("route requires a valid cancellation event");
        ComApartment apartment;
        unsigned failures{};
        for (;;) {
            if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) break;
            AttemptResult result;
            std::string setup_operation;
            try { result = attempt(options, cancel, stats); }
            catch (const AudioError& error) {
                setup_operation = error.what();
                result = {error.code, setup_operation.c_str(), false};
            }
            if (SUCCEEDED(result.hr)) break;
            stats.last_error.store(result.hr);
            std::cerr << (std::string(result.operation) + ": " + error_text(result.hr) + "\n");
            if (!transient(result.hr)) { stats.state.store(RouteState::failed); return 1; }
            stats.state.store(RouteState::waiting);
            stats.restarts.fetch_add(1);
            if (result.ran) failures = 0;
            const DWORD backoff = std::min<DWORD>(500u << std::min(failures++, 3u), 4000u);
            if (WaitForSingleObject(cancel, backoff) == WAIT_OBJECT_0) break;
        }
        stats.state.store(RouteState::stopped);
        return 0;
    } catch (const std::exception& error) {
        stats.state.store(RouteState::failed);
        std::cerr << "Route failed: " << error.what() << '\n';
        return 1;
    }
}
}

#include "common.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

namespace {
HANDLE cancel_event{}; // Process-lifetime event; valid even during console teardown callbacks.
BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (cancel_event) SetEvent(cancel_event);
        return TRUE;
    }
    return FALSE;
}
unsigned number(const std::wstring& text, unsigned minimum, unsigned maximum) {
    if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::invalid_argument("numeric options require decimal digits");
    unsigned value{};
    for (const auto ch : text) {
        const auto digit = static_cast<unsigned>(ch - L'0');
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
            throw std::invalid_argument("numeric option outside its documented range");
        value = value * 10 + digit;
    }
    if (value < minimum)
        throw std::invalid_argument("numeric option outside its documented range");
    return value;
}
struct Arguments {
    std::wstring command{L"help"}, source{L"default"}, destination;
    unsigned seconds{}, queue_ms{30};
    std::vector<unsigned> channel_map;
};
std::vector<unsigned> parse_map(const std::wstring& value) {
    std::vector<unsigned> result;
    std::size_t start = 0;
    for (;;) {
        const auto end = value.find(L',', start);
        result.push_back(number(value.substr(start, end == std::wstring::npos ? end : end - start), 0, 8));
        if (result.size() > 8) throw std::invalid_argument("channel map supports at most 8 output channels");
        if (end == std::wstring::npos) return result;
        start = end + 1;
    }
}
Arguments parse(int argc, wchar_t** argv) {
    Arguments args;
    if (argc > 1) args.command = argv[1];
    if (args.command == L"--help" || args.command == L"-h") args.command = L"help";
    if (args.command != L"help" && args.command != L"list" && args.command != L"report" &&
        args.command != L"watch" && args.command != L"route" && args.command != L"monitor")
        throw std::invalid_argument("unknown command; use patchy --help");
    const bool routing = args.command == L"route" || args.command == L"monitor";
    std::map<std::wstring, bool> seen;
    for (int i = 2; i < argc; ++i) {
        const std::wstring key = argv[i];
        if (seen[key]) throw std::invalid_argument("duplicate option");
        seen[key] = true;
        if (++i == argc) throw std::invalid_argument("option requires a value");
        const std::wstring value = argv[i];
        if (key == L"--seconds" && (args.command == L"watch" || routing))
            args.seconds = number(value, 1, 86400);
        else if (routing && key == L"--source") args.source = value;
        else if (routing && key == L"--to") args.destination = value;
        else if (routing && key == L"--queue-ms") args.queue_ms = number(value, 10, 200);
        else if (routing && key == L"--map") args.channel_map = parse_map(value);
        else throw std::invalid_argument("unknown option for this command");
    }
    if (routing && args.destination.empty())
        throw std::invalid_argument("route/monitor requires --to with an output endpoint ID");
    return args;
}
void help() {
    std::cout <<
        "Patchy 0.1.0 - open audio infrastructure for Windows (prototype)\n"
        "  patchy list\n"
        "  patchy report                         privacy-safe text snapshot to stdout\n"
        "  patchy watch [--seconds 1..86400]\n"
        "  patchy route --source default|ID --to ID [--seconds 1..86400] [--queue-ms 10..200]\n"
        "  patchy monitor --source default|ID --to ID [--seconds 1..86400] [--queue-ms 10..200]\n"
        "Both commands accept --map CH,CH,...: one input channel per output (1-based; 0=silence).\n"
        "Example: --map 1,1 duplicates input channel 1 to both stereo output channels.\n"
        "Quote IDs. route captures an OUTPUT's loopback; monitor captures an INPUT.\n"
        "Source defaults to the current console output/input respectively, resolved once.\n"
        "Monitor sends live input to the chosen output; use headphones to avoid acoustic feedback.\n"
        "Ctrl+C stops. No audio is saved.\n"
        "No virtual microphone/device is installed. Original playback remains audible.\n"
        "1..8 channels; layouts must match unless mapped. PCM16/24/32 or float32, rate ratio 0.5..2.\n"
        "Queue target is not measured end-to-end latency. Default target: 30 ms.\n"
        "Exit: 0 success/stop, 1 runtime failure, 2 usage or no successful stream start.\n";
}
void print_device(const patchy::win::Device& d, std::size_t index, bool private_report) {
    using namespace patchy::win;
    std::cout << "device-" << index << " " << (d.render ? "output" : "input")
              << " [" << state_name(d.state) << "] default-roles=" << d.default_roles << '\n';
    if (!private_report) {
        std::cout << "  name: " << utf8(d.name) << "\n  id: " << utf8(d.id) << '\n';
    }
    if (d.volume) std::cout << "  endpoint volume: " << *d.volume << " (0..1)" << '\n';
    if (d.muted) std::cout << "  endpoint muted: " << (*d.muted ? "yes" : "no") << '\n';
    if (d.format) {
        std::cout << "  shared mix: " << describe(*d.format) << '\n';
        std::cout << "  engine default period: " << d.default_period / 10000.0
                  << " ms; device minimum period: " << d.minimum_period / 10000.0 << " ms\n";
    } else std::cout << "  shared mix unavailable\n";
    if (!d.error.empty()) std::cout << "  diagnostic: " << d.error << '\n';
}
void inventory(bool private_report) {
    if (private_report)
        std::cout << "Patchy 0.1.0 report schema 1: names/IDs/paths/serials/apps/audio omitted.\n";
    const auto devices = patchy::win::enumerate_devices();
    std::cout << devices.size() << " MMDevice endpoints. Mix formats are not exhaustive hardware capabilities.\n";
    std::cout << "Default role bitmask: console=1, multimedia=2, communications=4.\n";
    for (std::size_t i = 0; i < devices.size(); ++i) print_device(devices[i], i, private_report);
}
void watch(unsigned seconds) {
    using namespace patchy::win;
    DeviceWatcher watcher; // Register before snapshot to avoid losing changes during enumeration.
    auto generation = watcher.generation();
    auto previous = enumerate_devices();
    for (std::size_t i = 0; i < previous.size(); ++i) print_device(previous[i], i, false);
    const auto start = GetTickCount64();
    while (WaitForSingleObject(cancel_event, 200) == WAIT_TIMEOUT) {
        if (seconds && GetTickCount64() - start >= seconds * 1000ull) break;
        const auto next = watcher.generation();
        if (next == generation) continue;
        generation = next;
        const auto current = enumerate_devices();
        for (const auto& old : previous)
            if (std::none_of(current.begin(), current.end(), [&](const Device& d) { return d.id == old.id; }))
                std::cout << "REMOVED " << utf8(old.id) << '\n';
        for (std::size_t i = 0; i < current.size(); ++i) {
            const auto& d = current[i];
            const auto old = std::find_if(previous.begin(), previous.end(),
                                         [&](const Device& o) { return o.id == d.id; });
            if (old == previous.end()) std::cout << "ADDED\n";
            else if (old->state != d.state || old->name != d.name || old->default_roles != d.default_roles ||
                     old->error != d.error || old->format.has_value() != d.format.has_value() ||
                     (d.format && old->format && describe(*d.format) != describe(*old->format)))
                std::cout << "CHANGED\n";
            else continue;
            print_device(d, i, false);
        }
        std::cout << "Endpoint notification batch processed (events may coalesce).\n";
        previous = current;
    }
}
const char* state_text(patchy::win::RouteState state) {
    using patchy::win::RouteState;
    switch (state) {
    case RouteState::opening: return "opening";
    case RouteState::running: return "running";
    case RouteState::waiting: return "waiting/retrying";
    case RouteState::stopped: return "stopped";
    default: return "failed";
    }
}
void print_stats(const patchy::win::RouteStats& s) {
    std::cout << "state=" << state_text(s.state.load()) << " captured=" << s.captured.load()
              << " rendered=" << s.rendered.load() << " silence=" << s.silent.load()
              << " underruns=" << s.underruns.load() << " overflows=" << s.overflows.load()
              << " discontinuities=" << s.discontinuities.load()
              << " timestamp-errors=" << s.timestamp_errors.load()
              << " queue=" << s.queued.load() << "/target=" << s.queue_target.load() << " correction-ppm=" << s.correction_ppm.load()
              << " timestamp-rate-error-ppm=" << s.timestamp_rate_error_ppm.load() << " retries=" << s.restarts.load() << '\n';
}
int route(const Arguments& args) {
    using namespace patchy::win;
    const bool monitor = args.command == L"monitor";
    RouteOptions options;
    options.source_id = monitor ? resolve_capture_id(args.source) : resolve_render_id(args.source);
    options.destination_id = resolve_render_id(args.destination);
    options.queue_ms = args.queue_ms;
    options.source_kind = monitor ? SourceKind::input : SourceKind::loopback;
    options.channel_map = args.channel_map;
    if (options.source_id == options.destination_id)
        throw std::invalid_argument("feedback protection: source and destination are the same endpoint");
    std::cout << (monitor ? "Shared input monitoring; use headphones to avoid acoustic feedback. No audio is saved.\n"
                         : "Shared output loopback bridge; original playback remains audible.\n")
              << "Pinned source: " << utf8(options.source_id) << "\nPinned destination: "
              << utf8(options.destination_id) << '\n';
    std::cout << "Channel map:";
    if (options.channel_map.empty()) std::cout << " identity (matching layouts required)";
    else for (const auto channel : options.channel_map) std::cout << ' ' << channel;
    std::cout << '\n';
    std::cout.flush();
    RouteStats stats;
    std::atomic<bool> finished{false};
    int result{};
    std::jthread worker([&] { result = run_route(options, cancel_event, stats); finished.store(true); });
    const auto start = GetTickCount64();
    bool announced = false;
    while (!finished.load()) {
        const auto wait = WaitForSingleObject(cancel_event, 1000);
        if (wait == WAIT_OBJECT_0 || (args.seconds && GetTickCount64() - start >= args.seconds * 1000ull)) {
            SetEvent(cancel_event);
            break;
        }
        if (!announced && stats.destination_buffer.load()) {
            std::cout << "Negotiated " << stats.source_rate.load() << " -> " << stats.destination_rate.load()
                      << " Hz; capture buffer " << stats.source_buffer.load()
                      << ", render buffer " << stats.destination_buffer.load()
                      << ", render period " << stats.render_period.load() << " frames; IAudioClient3="
                      << stats.low_latency.load() << ", MMCSS=" << stats.mmcss.load() << '\n';
            announced = true;
        }
        print_stats(stats);
    }
    SetEvent(cancel_event);
    worker.join();
    print_stats(stats);
    if (result == 0 && !stats.successful_starts.load()) return 2;
    return result;
}
}
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        const auto args = parse(argc, argv);
        if (args.command == L"help") { help(); return 0; }
        patchy::win::ComApartment apartment;
        cancel_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!cancel_event || !SetConsoleCtrlHandler(console_handler, TRUE))
            throw std::runtime_error("could not create console cancellation handler");
        if (args.command == L"list") inventory(false);
        else if (args.command == L"report") inventory(true);
        else if (args.command == L"watch") watch(args.seconds);
        else if (args.command == L"route" || args.command == L"monitor") return route(args);
        return 0;
    } catch (const patchy::win::AudioError& error) {
        std::cerr << error.what() << ": " << patchy::win::error_text(error.code) << '\n';
        return 1;
    } catch (const std::invalid_argument& error) {
        std::cerr << error.what() << "\nUse patchy --help.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

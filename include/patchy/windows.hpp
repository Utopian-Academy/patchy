#pragma once
#include "patchy/format.hpp"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace patchy::win {
struct Device {
    std::wstring id, name;
    bool render{};
    DWORD state{};
    unsigned default_roles{};
    std::optional<Format> format;
    std::optional<float> volume;
    std::optional<bool> muted;
    std::int64_t default_period{}, minimum_period{}; // 100 ns units
    std::string error;
};
std::vector<Device> enumerate_devices();
std::wstring resolve_render_id(const std::wstring& selector);
std::wstring resolve_capture_id(const std::wstring& selector);
std::string utf8(const std::wstring&);
std::string describe(const Format&);
std::string state_name(DWORD);
class DeviceWatcher {
public:
    DeviceWatcher();
    ~DeviceWatcher();
    DeviceWatcher(const DeviceWatcher&) = delete;
    DeviceWatcher& operator=(const DeviceWatcher&) = delete;
    std::uint64_t generation() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
enum class RouteState : unsigned { opening, running, waiting, stopped, failed };
struct RouteStats {
    std::atomic<RouteState> state{RouteState::opening};
    std::atomic<std::uint64_t> captured{}, rendered{}, silent{}, underruns{}, overflows{}, successful_starts{};
    std::atomic<std::uint64_t> discontinuities{}, timestamp_errors{}, restarts{};
    std::atomic<std::uint32_t> queued{}, queue_target{}, source_buffer{}, destination_buffer{};
    std::atomic<std::uint32_t> source_rate{}, destination_rate{}, render_period{};
    std::atomic<std::int32_t> correction_ppm{}, timestamp_rate_error_ppm{};
    std::atomic<long> last_error{};
    std::atomic<bool> mmcss{}, low_latency{};
};
enum class SourceKind { loopback, input };
struct RouteOptions {
    std::wstring source_id, destination_id;
    unsigned queue_ms{30};
    SourceKind source_kind{SourceKind::loopback};
    std::vector<unsigned> channel_map; // One 1-based input per output; 0 means silence.
};
// Own all WASAPI clients on the calling worker; cancellation is a manual-reset event.
// Returns 0 on cancellation, 1 on terminal failure. Errors are reported after RT exits.
int run_route(const RouteOptions&, HANDLE cancel, RouteStats&);
}

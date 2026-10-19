#pragma once
#include <cstdint>
#include <optional>

namespace patchy {
// Correlate stream ticks with WASAPI's QPC values (already converted to 100 ns).
// Two-second windows reject packet jitter. Invalid timestamps reset, never fit them.
class ClockRateWindow {
public:
    void reset() noexcept { have_origin_ = false; rate_.reset(); }
    bool observe(std::uint64_t position, std::uint64_t qpc, std::uint64_t frequency) noexcept {
        if (!frequency || !qpc) { reset(); return false; }
        if (!have_origin_ || position < last_position_ || qpc <= last_qpc_) {
            origin_position_ = last_position_ = position;
            origin_qpc_ = last_qpc_ = qpc;
            have_origin_ = true;
            rate_.reset();
            return false;
        }
        last_position_ = position;
        last_qpc_ = qpc;
        const auto elapsed = qpc - origin_qpc_;
        if (elapsed < 20000000) return false;
        if (elapsed > 100000000) { reset(); return false; } // Suspend/long gap is not a fit.
        rate_ = (static_cast<double>(position - origin_position_) / static_cast<double>(frequency)) /
                (static_cast<double>(elapsed) / 10000000.0);
        origin_position_ = position;
        origin_qpc_ = qpc;
        return true;
    }
    std::optional<double> rate() const noexcept { return rate_; }
private:
    bool have_origin_{};
    std::uint64_t origin_position_{}, origin_qpc_{}, last_position_{}, last_qpc_{};
    std::optional<double> rate_;
};
}

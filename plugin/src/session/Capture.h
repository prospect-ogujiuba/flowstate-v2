// MIDI capture: "Use what I just played". Always on, last 64 bars of incoming notes.
//
// Audio thread:   CaptureRing::push() per incoming note event (no allocation, locks or I/O).
// Message thread: CaptureRing::drain() into CaptureHistory, which trims to the window.
//
// Notes are stamped twice. `clockPpq` is a monotonic capture clock (quarter notes, advanced by
// every block at the current tempo, whether or not the host is playing), which defines "the last
// N bars". `hostPpq` is the host position when the transport was playing (or -1), which lets the
// MIDI->IR analyzer line the capture up with the song's bars.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace flowstate::plugin {

struct CapturedEvent {
    double clockPpq = 0.0;
    double hostPpq = -1.0;
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 0;  // 0 = note-off
};

// Single-producer (audio), single-consumer (message) ring. Drops events when full rather than
// blocking; `dropped()` counts them.
class CaptureRing {
public:
    static constexpr std::size_t kCapacity = 8192;  // power of two

    bool push(const CapturedEvent& e) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[w & (kCapacity - 1)] = e;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    template <typename F>
    std::size_t drain(F&& consume) {
        auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        const auto count = static_cast<std::size_t>(w - r);
        for (; r != w; ++r) consume(slots_[r & (kCapacity - 1)]);
        read_.store(r, std::memory_order_release);
        return count;
    }

    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    std::array<CapturedEvent, kCapacity> slots_{};
    std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> read_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

class CaptureHistory {
public:
    static constexpr int kMaxBars = 64;

    void add(const CapturedEvent& e) { events_.push_back(e); }

    // Drops events older than kMaxBars bars before `nowClockPpq`.
    void trim(double nowClockPpq, double ppqPerBar);

    // Whole bars (rounded up) from the oldest captured note-on to now, 0..kMaxBars.
    int barsAvailable(double nowClockPpq, double ppqPerBar) const;

    // Events from the last `bars` bars, oldest first.
    std::vector<CapturedEvent> lastBars(int bars, double nowClockPpq, double ppqPerBar) const;

    void clear() { events_.clear(); }
    std::size_t size() const { return events_.size(); }

private:
    std::deque<CapturedEvent> events_;
};

}  // namespace flowstate::plugin

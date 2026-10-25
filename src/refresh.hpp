#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace ag
{
struct DisplayRate
{
    double hz{60};
    bool known{};
};
DisplayRate display_rate(HWND window);

// One producer, one UI consumer, at most one queued notification. This supplies
// presentation opportunities, not simulated samples or a physical scanout clock.
class RefreshClock
{
    HANDLE wake_{}, timer_{};
    HWND window_{};
    std::thread worker_;
    std::atomic<bool> stop_{}, pending_{};
    std::atomic<std::int64_t> period_{};
    std::atomic<std::uint64_t> posted_{}, coalesced_{};
    std::atomic<DWORD> error_{};
    bool high_resolution_{};
    void run() noexcept;

  public:
    explicit RefreshClock(HWND window);
    ~RefreshClock();
    void configure(double hz) noexcept;
    void complete() noexcept
    {
        pending_ = false;
    }
    bool active() const noexcept
    {
        return period_.load() != 0;
    }
    bool high_resolution() const noexcept
    {
        return high_resolution_;
    }
    std::uint64_t posted() const noexcept
    {
        return posted_.load();
    }
    std::uint64_t coalesced() const noexcept
    {
        return coalesced_.load();
    }
    DWORD error() const noexcept
    {
        return error_.load();
    }
};
} // namespace ag

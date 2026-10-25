#include "internal.hpp"
#include "refresh.hpp"

namespace ag
{
DisplayRate display_rate(HWND window)
{
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return {};
    // Preserve rational refresh (e.g. 60000/1001); use the fastest target when
    // several displays clone the same GDI source. Retry topology-size races only.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        UINT32 path_count{}, mode_count{};
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const auto result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                                               modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (result != ERROR_SUCCESS)
            break;
        double hz{};
        for (UINT32 i = 0; i < path_count; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(name),
                           paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS &&
                _wcsicmp(name.viewGdiDeviceName, monitor.szDevice) == 0)
            {
                const auto rate = paths[i].targetInfo.refreshRate;
                if (rate.Denominator)
                    hz = std::max(hz, double(rate.Numerator) / rate.Denominator);
            }
        }
        if (hz > 1 && hz <= 2000)
            return {hz, true};
        break;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsExW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0) &&
        mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency <= 2000)
        return {double(mode.dmDisplayFrequency), true};
    return {};
}

RefreshClock::RefreshClock(HWND window) : window_(window)
{
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    wincheck(wake_ != nullptr, L"Refresh wake event unavailable");
    timer_ =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    high_resolution_ = timer_ != nullptr;
    if (!timer_)
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!timer_)
    {
        const auto code = GetLastError();
        CloseHandle(wake_);
        throw Failure{code ? HRESULT_FROM_WIN32(code) : E_FAIL, L"Refresh timer unavailable"};
    }
    try
    {
        worker_ = std::thread(&RefreshClock::run, this);
    }
    catch (...)
    {
        CloseHandle(timer_);
        CloseHandle(wake_);
        throw;
    }
}
RefreshClock::~RefreshClock()
{
    stop_ = true;
    SetEvent(wake_);
    worker_.join();
    CloseHandle(timer_);
    CloseHandle(wake_);
}
void RefreshClock::configure(double hz) noexcept
{
    const auto period = hz > 0 ? std::max<std::int64_t>(1, std::int64_t(10000000.0 / hz)) : 0;
    if (period_.exchange(period) != period)
        SetEvent(wake_);
}
void RefreshClock::run() noexcept
{
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::int64_t deadline{};
    while (!stop_)
    {
        const auto period = period_.load();
        if (!period)
        {
            CancelWaitableTimer(timer_);
            WaitForSingleObject(wake_, INFINITE);
            deadline = 0;
            continue;
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto ticks = std::max<std::int64_t>(1, period * frequency.QuadPart / 10000000);
        if (!deadline || deadline <= now.QuadPart)
            deadline = now.QuadPart + ticks;
        LARGE_INTEGER due{};
        due.QuadPart = -std::max<std::int64_t>(1, (deadline - now.QuadPart) * 10000000 / frequency.QuadPart);
        if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0))
        {
            error_ = GetLastError();
            WaitForSingleObject(wake_, 100);
            deadline = 0;
            continue;
        }
        const HANDLE handles[]{wake_, timer_};
        const auto wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 + 1)
        {
            if (wait == WAIT_FAILED)
            {
                error_ = GetLastError();
                WaitForSingleObject(wake_, 100);
            }
            deadline = 0;
            continue;
        }
        if (!stop_ && period_.load())
        {
            if (pending_.exchange(true))
                ++coalesced_;
            else if (PostMessageW(window_, refresh_message, 0, 0))
                ++posted_;
            else
            {
                error_ = GetLastError();
                pending_ = false;
            }
        }
        deadline += ticks;
    }
}
} // namespace ag

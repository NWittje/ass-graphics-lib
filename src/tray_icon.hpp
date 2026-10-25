#pragma once
#include "internal.hpp"

namespace ag
{
// Read the actual HWND's icons, not its HINSTANCE: a library-owned window can
// display artwork loaded by its application from a different resource module.
inline HICON window_icon(HWND window) noexcept
{
    const auto dpi = GetDpiForWindow(window);
    for (const auto size : {ICON_SMALL, ICON_BIG})
        if (const auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, size, dpi)))
            return icon;
    for (const auto field : {GCLP_HICONSM, GCLP_HICON})
        if (const auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, field)))
            return icon;
    if (const auto icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, ICON_SMALL2, dpi)))
        return icon;
    return LoadIconW(nullptr, IDI_APPLICATION); // Shared fallback; only its copy is destroyed.
}

class TrayNotification
{
    using Notify = BOOL(WINAPI *)(DWORD, PNOTIFYICONDATAW);
    Notify notify_ = Shell_NotifyIconW;
    HICON owned_icon_{};
    bool added_{};

public:
    NOTIFYICONDATAW data{};
    TrayNotification() = default;
    TrayNotification(const TrayNotification &) = delete;
    TrayNotification &operator=(const TrayNotification &) = delete;
    ~TrayNotification()
    {
        if (added_)
            notify_(NIM_DELETE, &data);
        if (owned_icon_)
            DestroyIcon(owned_icon_);
    }
    // The transport argument is internal test injection. Production uses the
    // Windows shell directly; no additional public ABI or application policy.
    void create(HWND window, const wchar_t *tooltip, UINT message, Notify notify = Shell_NotifyIconW)
    {
        require(!owned_icon_, L"Tray notification already initialized");
        notify_ = notify;
        owned_icon_ = CopyIcon(window_icon(window));
        wincheck(owned_icon_ != nullptr, L"Application tray icon copy failed");
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = 1;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = message;
        data.hIcon = owned_icon_;
        wcsncpy_s(data.szTip, tooltip, _TRUNCATE);
        wincheck(notify_(NIM_ADD, &data), L"Tray icon creation failed");
        added_ = true;
        data.uVersion = NOTIFYICON_VERSION_4;
        notify_(NIM_SETVERSION, &data);
    }
    void restore() noexcept
    {
        // Explorer loses its copy; the library retains the owned image and
        // current tooltip. Restore the callback version after every NIM_ADD.
        notify_(NIM_ADD, &data);
        notify_(NIM_SETVERSION, &data);
    }
    void tooltip(const wchar_t *text)
    {
        if (std::wstring_view(data.szTip) == text)
            return;
        auto next = data;
        wcscpy_s(next.szTip, text);
        wincheck(notify_(NIM_MODIFY, &next), L"Tray tooltip update failed");
        data = next; // Failed modification retains the previous state/image.
    }
};
} // namespace ag

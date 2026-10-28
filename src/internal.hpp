#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ass_graphics/ass_graphics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <commctrl.h>
#include <d2d1_1.h>
#include <dwrite_3.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <shellapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
namespace ag
{
using Microsoft::WRL::ComPtr;
// Diagnostics must still work when reporting allocation failure. Never allocate
// from a catch block or permit exceptions to escape a Windows/C ABI callback.
struct ErrorBuffer
{
    std::array<wchar_t, 1024> text{};
    ErrorBuffer &operator=(std::wstring_view message) noexcept
    {
        auto count = std::min(message.size(), text.size() - 1);
        std::copy_n(message.data(), count, text.data());
        text[count] = 0;
        return *this;
    }
    void clear() noexcept
    {
        text[0] = 0;
    }
    operator std::wstring_view() const noexcept
    {
        return text.data();
    }
};
inline thread_local ErrorBuffer error;
extern thread_local uint64_t cpp_allocations;
struct Failure
{
    HRESULT hr;
    std::wstring message;
};
inline void require(bool ok, const wchar_t *msg, HRESULT hr = E_INVALIDARG)
{
    if (!ok)
        throw Failure{hr, msg};
}
inline void check(HRESULT hr, const wchar_t *msg)
{
    if (FAILED(hr))
        throw Failure{hr, msg};
}
inline void wincheck(BOOL ok, const wchar_t *msg)
{
    if (!ok)
    {
        DWORD e = GetLastError();
        throw Failure{e ? HRESULT_FROM_WIN32(e) : E_FAIL, msg};
    }
}
template <class F> ag_result api(F &&fn) noexcept
{
    try
    {
        fn();
        error.clear();
        return S_OK;
    }
    catch (const Failure &e)
    {
        error = e.message;
        return e.hr;
    }
    catch (const std::bad_alloc &)
    {
        error = L"Allocation failed";
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        error = L"Unexpected library error";
        return E_FAIL;
    }
}
inline D2D1_COLOR_F color(ag_color c)
{
    return {c.r, c.g, c.b, c.a};
}
inline D2D1_RECT_F rect(ag_rect r)
{
    return {r.left, r.top, r.right, r.bottom};
}
inline bool finite(float f)
{
    return std::isfinite(f);
}
enum class Kind
{
    Context,
    Window,
    Control,
    Tray,
    Canvas,
    Chart,
    Scene,
    Settings,
    Path,
    Display, DisplayCandidate
};
struct Context;
struct Object
{
    Kind kind;
    Context *context;
    DWORD thread;
    HWND hwnd{};
    HWND tooltip{};
    std::wstring tooltip_text;
    ag_rect layout_bounds{};
    bool has_bounds{};
    ag_context_fn context_callback{};
    void *context_user{};
    Object(Kind k, Context *c);
    virtual ~Object();
    virtual bool callback_active() const { return false; }
    // Internal HWND sizing only: public logical bounds and ABI remain unchanged.
    virtual int native_layout_height(int requested) const { return requested; }
    void ui() const
    {
        require(GetCurrentThreadId() == thread, L"Operation requires owning UI thread", RPC_E_WRONG_THREAD);
    }
};
struct Context final : Object
{
    uint32_t children{};
    bool com_initialized{};
    ag_theme theme{};
    ComPtr<ID2D1Factory1> d2d;
    ComPtr<IDWriteFactory5> write;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontCollection1> collection;
    ComPtr<IWICImagingFactory> wic;
    std::array<HANDLE, 2> gdi_fonts{};
    // Own exact selected external bytes; ag_font_data borrows until Context destruction.
    std::array<std::vector<uint8_t>, 2> font_storage;
    std::array<const void *, 2> font_bytes{};
    std::array<uint32_t, 2> font_sizes{};
    std::map<uint64_t, HFONT> fonts;
    std::vector<Object *> objects;
    HWND reaper{};
    Context();
    ~Context();
    void load_fonts();
    HFONT font(float points = 0, bool bold = false, UINT dpi = 96);
    ComPtr<IDWriteTextFormat> format(float size, bool bold = false);
};
template <class T> T *get(ag_handle h, Kind k)
{
    require(h != nullptr, L"Null handle");
    auto *o = static_cast<Object *>(h);
    require(o->kind == k, L"Wrong handle type");
    return static_cast<T *>(o);
}
inline Object *object(ag_handle h)
{
    require(h != nullptr, L"Null handle");
    return static_cast<Object *>(h);
}
inline void output(ag_handle *p)
{
    require(p != nullptr, L"Null output");
    *p = nullptr;
}
inline HMODULE module()
{
    HMODULE m{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module), &m);
    return m;
}
inline UINT dpi(HWND h)
{
    return h ? GetDpiForWindow(h) : 96;
}
inline RECT pixels(ag_rect r, UINT d)
{
    require(finite(r.left) && finite(r.top) && finite(r.right) && finite(r.bottom) && r.right >= r.left &&
                r.bottom >= r.top &&
                std::max({std::abs(r.left), std::abs(r.top), std::abs(r.right), std::abs(r.bottom)}) <=
                    1000000,
            L"Invalid or excessive window bounds");
    float s = d / 96.f;
    return {LONG(std::lround(r.left * s)), LONG(std::lround(r.top * s)), LONG(std::lround(r.right * s)),
            LONG(std::lround(r.bottom * s))};
}
inline void copy_text(std::wstring_view s, wchar_t *out, uint32_t cap, uint32_t *needed)
{
    if (needed)
        *needed = static_cast<uint32_t>(s.size() + 1);
    require(out || cap == 0, L"Null text buffer");
    if (!out)
        return;
    require(cap > s.size(), L"Text buffer too small", HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    std::copy(s.begin(), s.end(), out);
    out[s.size()] = 0;
}
constexpr UINT submit_message = WM_APP + 0x315;
constexpr UINT tray_message = WM_APP + 0x316;
constexpr UINT refresh_message = WM_APP + 0x320;
constexpr UINT dispatch_message = WM_APP + 0x321;
int panel_scroll_offset(HWND parent) noexcept;
struct Snapshot
{
    struct PathDraw { ComPtr<ID2D1PathGeometry> geometry; ComPtr<ID2D1StrokeStyle> style; bool fill{}; };
    std::vector<ag_draw> commands;
    std::vector<ag_point> points;
    std::vector<PathDraw> retained_paths;
};
bool context_request(Object *, LPARAM);
Snapshot copy_snapshot(Context *, const ag_draw *, uint32_t, const ag_point *, uint32_t, const ag_path_draw *, uint32_t);
void validate_snapshot(const ag_draw *, uint32_t, const ag_point *, uint32_t, uint32_t path_count=0);
void save_pixels(Context *, const wchar_t *, UINT, UINT, UINT, const BYTE *, float);
struct Painter
{
    template <class T> struct Cached
    {
        ComPtr<T> value;
        uint64_t generation{};
    };
    Context *context;
    ID2D1RenderTarget *owner{};
    D2D1::Matrix3x2F root{D2D1::Matrix3x2F::Identity()};
    ComPtr<ID2D1SolidColorBrush> brush;
    uint64_t generation{};
    std::map<uint64_t, Cached<IDWriteTextFormat>> formats;
    std::map<std::wstring, Cached<ID2D1Bitmap>> images;
    std::vector<ComPtr<ID2D1PathGeometry>> paths;
    explicit Painter(Context *c) : context(c) {}
    void reset()
    {
        owner = nullptr;
        brush.Reset();
        images.clear();
        formats.clear();
        paths.clear();
    }
    void prepare(ID2D1RenderTarget *, const Snapshot &);
    void draw(ID2D1RenderTarget *, const Snapshot &);
};
struct Surface : Object
{
    ag_metrics metrics{};
    std::mutex mutex;
    bool notified{}, dirty{true};
    HRESULT last_result{S_OK};
    ag_canvas_pointer_fn pointer_callback{};
    void* pointer_user{};
    bool pointer_pressed{};
    ag_point pointer_last{};
    unsigned pointer_dispatch_depth{};
    bool pointer_busy{}, pointer_disposing{}, pointer_cancel_pending{};
    std::vector<HWND> pointer_ancestors;
    bool callback_active() const override { return pointer_dispatch_depth || pointer_busy || pointer_disposing; }
    void emit_pointer(uint32_t, ag_point) noexcept;
    void cancel_pointer() noexcept;
    void detach_pointer_ancestors() noexcept;
    void begin_pointer(ag_point);
    void dispose_pointer() noexcept;
    static LRESULT CALLBACK ancestor_pointer_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    Surface(Kind k, Context *c) : Object(k, c) {}
    virtual void timer(UINT_PTR){}
    virtual void get_metrics(ag_metrics &out){out=metrics;}
    virtual void paint() = 0;
    virtual void resize() = 0;
    virtual void reset() = 0;
    void schedule_locked()
    {
        ++metrics.submissions;
        if (notified)
        {
            ++metrics.coalesced;
            return;
        }
        require(PostMessageW(hwnd, submit_message, 0, 0) != 0, L"Cannot schedule update", E_FAIL);
        notified = true;
    }
    void create(HWND, ag_rect);
    virtual ~Surface()
    {
        dispose_pointer();
        if (hwnd)
            DestroyWindow(hwnd);
    }
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM) noexcept;
};
} // namespace ag

#include "internal.hpp"
#include <charconv>
#include <limits>
namespace ag
{
struct ReleaseRequest
{
    ag_handle handle;
    ag_released_fn callback;
    void *user;
};
void finish_release(ReleaseRequest *raw)
{
    std::unique_ptr<ReleaseRequest> request(raw);
    auto result = ag_destroy(request->handle);
    if (SUCCEEDED(result) && request->callback)
        request->callback(request->user);
}
Object::Object(Kind k, Context *c) : kind(k), context(c), thread(GetCurrentThreadId())
{
    if (c)
    {
        c->ui();
        c->objects.push_back(this);
        ++c->children;
    }
}
Object::~Object()
{
    if (tooltip)
        DestroyWindow(tooltip);
    if (context)
    {
        --context->children;
        std::erase(context->objects, this);
    }
}
Context::Context() : Object(Kind::Context, nullptr)
{
    theme = {{.973f, .973f, .973f, 1},
             {.10f, .12f, .15f, 1},
             {.165f, .4f, .776f, 1},
             {.45f, .47f, .5f, 1},
             {.824f, .227f, .243f, 1},
             {.7f, .44f, .08f, 1},
             {.165f, .55f, .298f, 1},
             13,
             8,
             L"B612"};
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    require(SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE, L"COM initialization failed", hr);
    com_initialized = SUCCEEDED(hr);
    try
    {
        INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_WIN95_CLASSES};
        wincheck(InitCommonControlsEx(&ic), L"Common controls initialization failed");
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()),
              L"Direct2D factory failed");
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                  reinterpret_cast<IUnknown **>(write.GetAddressOf())),
              L"DirectWrite 5 requires Windows 10 1703 or newer");
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)),
              L"WIC factory failed");
        load_fonts();
        WNDCLASSW wc{};
        wc.hInstance = module();
        wc.lpszClassName = L"AssGraphics.Release.1";
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT
        {
            if (m == submit_message)
            {
                ag_destroy(reinterpret_cast<ag_handle>(l));
                return 0;
            }
            if (m == submit_message + 2)
            {
                finish_release(reinterpret_cast<ReleaseRequest *>(l));
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        RegisterClassW(&wc);
        reaper =
            CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module(), nullptr);
        wincheck(reaper != nullptr, L"Release window failed");
    }
    catch (...)
    {
        for (auto f : gdi_fonts)
            if (f)
                RemoveFontMemResourceEx(f);
        collection.Reset();
        if (write && loader)
            write->UnregisterFontFileLoader(loader.Get());
        wic.Reset();
        loader.Reset();
        write.Reset();
        d2d.Reset();
        if (com_initialized)
            CoUninitialize();
        throw;
    }
}
Context::~Context()
{
    if (reaper)
        DestroyWindow(reaper);
    for (auto [key, f] : fonts)
        DeleteObject(f);
    for (auto f : gdi_fonts)
        if (f)
            RemoveFontMemResourceEx(f);
    collection.Reset();
    if (write && loader)
        write->UnregisterFontFileLoader(loader.Get());
    loader.Reset();
    write.Reset();
    d2d.Reset();
    wic.Reset();
    if (com_initialized)
        CoUninitialize();
}
HFONT Context::font(float points, bool bold, UINT d)
{
    if (points <= 0)
        points = theme.font_size * 72.f / 96.f;
    uint64_t key = uint64_t(std::lround(points * 64)) | (uint64_t(d) << 24) | (uint64_t(bold) << 56);
    auto it = fonts.find(key);
    if (it != fonts.end())
        return it->second;
    HFONT f = CreateFontW(-int(std::lround(points * d / 72.f)), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE,
                          FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, theme.font_family);
    require(f != nullptr, L"Font creation failed", E_FAIL);
    fonts.emplace(key, f);
    return f;
}
ComPtr<IDWriteTextFormat> Context::format(float size, bool bold)
{
    ComPtr<IDWriteTextFormat> f;
    check(write->CreateTextFormat(theme.font_family,
                                  wcscmp(theme.font_family, L"B612") == 0 ? collection.Get() : nullptr,
                                  bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &f),
          L"Text format failed");
    return f;
}
} // namespace ag
using namespace ag;
uint32_t ag_abi_version()
{
    return 1;
}
ag_result ag_last_error(wchar_t *b, uint32_t c, uint32_t *n)
{
    try
    {
        copy_text(error, b, c, n);
        return S_OK;
    }
    catch (...)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
}
ag_result ag_context_create(ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            *out = new Context;
        });
}
ag_result ag_destroy(ag_handle h)
{
    return api(
        [&]
        {
            if (!h)
                return;
            auto *p = object(h);
            p->ui();
            require(!p->callback_active(), L"Cannot destroy a source during its callback or input teardown",
                    HRESULT_FROM_WIN32(ERROR_BUSY));
            if (p->kind == Kind::Context)
                require(static_cast<Context *>(p)->children == 0,
                        L"Destroy child handles before their context", HRESULT_FROM_WIN32(ERROR_BUSY));
            delete p;
        });
}
ag_result ag_release(ag_handle h)
{
    return api(
        [&]
        {
            if (!h)
                return;
            auto p = object(h);
            if (p->thread == GetCurrentThreadId() || p->kind == Kind::Settings)
            {
                require(!p->callback_active(), L"Cannot release a source during its callback or input teardown",
                        HRESULT_FROM_WIN32(ERROR_BUSY));
                if (p->kind == Kind::Context)
                    require(static_cast<Context *>(p)->children == 0, L"Context still has children",
                            HRESULT_FROM_WIN32(ERROR_BUSY));
                delete p;
                return;
            }
            auto c = p->kind == Kind::Context ? static_cast<Context *>(p) : p->context;
            require(c, L"No owning context");
            wincheck(PostMessageW(c->reaper, submit_message, 0, reinterpret_cast<LPARAM>(p)),
                     L"Deferred release failed");
        });
}
ag_result ag_release_notify(ag_handle h, ag_released_fn callback, void *user)
{
    return api(
        [&]
        {
            if (!h)
            {
                if (callback)
                    callback(user);
                return;
            }
            auto p = object(h);
            if (p->thread == GetCurrentThreadId() || p->kind == Kind::Settings)
            {
                check(ag_release(h), L"Handle release failed");
                if (callback)
                    callback(user);
                return;
            }
            auto c = p->kind == Kind::Context ? static_cast<Context *>(p) : p->context;
            require(c, L"No owning context");
            auto request = std::make_unique<ReleaseRequest>(ReleaseRequest{h, callback, user});
            wincheck(PostMessageW(c->reaper, submit_message + 2, 0, reinterpret_cast<LPARAM>(request.get())),
                     L"Deferred release failed");
            request.release();
        });
}
ag_result ag_theme_get(ag_handle h, ag_theme *t)
{
    return api(
        [&]
        {
            require(t, L"Null theme");
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            *t = c->theme;
        });
}
ag_result ag_theme_set(ag_handle h, const ag_theme *t)
{
    return api(
        [&]
        {
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            require(t && t->font_size > 0 && t->font_size <= 1000 && std::isfinite(t->font_size) &&
                        t->spacing >= 0 && std::isfinite(t->spacing) && wmemchr(t->font_family, 0, 64),
                    L"Invalid theme");
            for (auto color :
                 {t->background, t->foreground, t->accent, t->muted, t->error, t->warning, t->success})
                for (float component : {color.r, color.g, color.b, color.a})
                    require(finite(component) && component >= 0 && component <= 1,
                            L"Theme colors must be finite RGBA in [0,1]");
            require(c->children == 0, L"Set theme before creating windows and surfaces",
                    HRESULT_FROM_WIN32(ERROR_BUSY));
            for (auto [k, f] : c->fonts)
                DeleteObject(f);
            c->fonts.clear();
            c->theme = *t;
        });
}
ag_result ag_font_data(ag_handle h, uint32_t b, const void **p, uint32_t *n)
{
    return api(
        [&]
        {
            auto c = get<Context>(h, Kind::Context);
            require(p && n && b < 2, L"Invalid font arguments");
            *p = c->font_bytes[b];
            *n = c->font_sizes[b];
        });
}
ag_result ag_font_create(ag_handle h, float pt, uint32_t b, uint32_t d, void **f)
{
    return api(
        [&]
        {
            auto c = get<Context>(h, Kind::Context);
            c->ui();
            require(f && pt > 0 && pt < 1000 && d >= 48 && d <= 960, L"Invalid font request");
            *f = c->font(pt, b != 0, d);
        });
}
ag_result ag_native_window(ag_handle h, void **w)
{
    return api(
        [&]
        {
            auto p = object(h);
            p->ui();
            require(w && IsWindow(p->hwnd), L"Handle has no live window");
            *w = p->hwnd;
        });
}
ag_result ag_bounds_set(ag_handle h, ag_rect r)
{
    return api(
        [&]
        {
            auto p = object(h);
            p->ui();
            require(IsWindow(p->hwnd) && finite(r.left) && finite(r.top) && finite(r.right) &&
                        finite(r.bottom) && r.right >= r.left && r.bottom >= r.top,
                    L"Invalid bounds");
            p->layout_bounds = r;
            p->has_bounds = true;
            RECT b = pixels(r, dpi(p->hwnd));
            const int scroll = panel_scroll_offset(GetParent(p->hwnd));
            b.top -= scroll;
            b.bottom -= scroll;
            wincheck(MoveWindow(p->hwnd, b.left, b.top, b.right - b.left, p->native_layout_height(b.bottom - b.top), TRUE),
                     L"Move window failed");
        });
}
double ag_slider_quantize(double v, double lo, double hi, double step)
{
    if (!std::isfinite(v) || !std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(step) || step <= 0 ||
        hi < lo)
        return std::numeric_limits<double>::quiet_NaN();
    v = std::clamp(v, lo, hi);
    auto regular = std::clamp(lo + std::round((v - lo) / step) * step, lo, hi);
    // The final trackbar position represents the upper endpoint even when the
    // range is not an exact multiple of the step.
    return hi - v < std::abs(regular - v) ? hi : regular;
}
double ag_symlog(double v, double t)
{
    return t > 0 && std::isfinite(t) ? std::copysign(std::log1p(std::abs(v) / t), v)
                                     : std::numeric_limits<double>::quiet_NaN();
}
ag_result ag_viewport_project(const ag_viewport *v, double x, double y, ag_point *p)
{
    return api(
        [&]
        {
            require(v && p && std::isfinite(x) && std::isfinite(y) && std::isfinite(v->left) &&
                        std::isfinite(v->right) && std::isfinite(v->top) && std::isfinite(v->bottom) &&
                        v->right > v->left && v->bottom > v->top && v->screen.right > v->screen.left &&
                        v->screen.bottom > v->screen.top,
                    L"Invalid viewport");
            p->x = float(v->screen.left +
                         (x - v->left) / (v->right - v->left) * (v->screen.right - v->screen.left));
            p->y = float(v->screen.top +
                         (y - v->top) / (v->bottom - v->top) * (v->screen.bottom - v->screen.top));
        });
}
ag_result ag_viewport_unproject(const ag_viewport *v, ag_point p, double *x, double *y)
{
    return api(
        [&]
        {
            require(v && x && y && finite(p.x) && finite(p.y) && std::isfinite(v->left) &&
                        std::isfinite(v->right) && std::isfinite(v->top) && std::isfinite(v->bottom) &&
                        v->right > v->left && v->bottom > v->top && v->screen.right > v->screen.left &&
                        v->screen.bottom > v->screen.top,
                    L"Invalid viewport");
            *x = v->left + (p.x - v->screen.left) / (v->screen.right - v->screen.left) * (v->right - v->left);
            *y = v->top + (p.y - v->screen.top) / (v->screen.bottom - v->screen.top) * (v->bottom - v->top);
        });
}
ag_result ag_viewport_zoom(ag_viewport *v, ag_point p, double f)
{
    return api(
        [&]
        {
            require(f > 0 && std::isfinite(f), L"Invalid zoom");
            double x, y;
            check(ag_viewport_unproject(v, p, &x, &y), L"Invalid viewport");
            ag_viewport n = *v;
            n.left = x + (v->left - x) / f;
            n.right = x + (v->right - x) / f;
            n.top = y + (v->top - y) / f;
            n.bottom = y + (v->bottom - y) / f;
            require(std::isfinite(n.left) && std::isfinite(n.right) && std::isfinite(n.top) &&
                        std::isfinite(n.bottom) && n.right > n.left && n.bottom > n.top,
                    L"Zoom overflow");
            *v = n;
        });
}
int32_t ag_hit_rect(ag_rect r, ag_point p)
{
    return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
}
int32_t ag_clip_line(ag_rect r, ag_point *a, ag_point *b)
{
    if (!a || !b || !finite(a->x) || !finite(a->y) || !finite(b->x) || !finite(b->y) ||
        !(r.right >= r.left) || !(r.bottom >= r.top))
        return 0;
    double dx = b->x - a->x, dy = b->y - a->y, lo = 0, hi = 1;
    auto edge = [&](double p, double q)
    {
        if (p == 0)
            return q >= 0;
        double t = q / p;
        if (p < 0)
            lo = std::max(lo, t);
        else
            hi = std::min(hi, t);
        return lo <= hi;
    };
    if (!edge(-dx, a->x - r.left) || !edge(dx, r.right - a->x) || !edge(-dy, a->y - r.top) ||
        !edge(dy, r.bottom - a->y))
        return 0;
    ag_point old = *a;
    *a = {float(old.x + lo * dx), float(old.y + lo * dy)};
    *b = {float(old.x + hi * dx), float(old.y + hi * dy)};
    return 1;
}
ag_result ag_project_wire(const ag_vec3 *v, uint32_t n, float yaw, float pitch, float scale, ag_point center,
                          ag_point *o)
{
    return api(
        [&]
        {
            require((!n || (v && o)) && finite(yaw) && finite(pitch) && finite(scale), L"Invalid projection");
            float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
            for (uint32_t i = 0; i < n; ++i)
            {
                require(finite(v[i].x) && finite(v[i].y) && finite(v[i].z), L"Nonfinite vertex");
                float x = cy * v[i].x - sy * v[i].y, y = sy * v[i].x + cy * v[i].y;
                o[i] = {center.x + x * scale, center.y - (sp * y + cp * v[i].z) * scale};
            }
        });
}

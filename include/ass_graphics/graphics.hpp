#pragma once
#include "ass_graphics.h"
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
namespace ass::graphics
{
class Error : public std::runtime_error
{
  public:
    ag_result result;
    std::wstring detail;
    Error(ag_result r, const wchar_t *text)
        : std::runtime_error("ass_graphics operation failed; see detail for Unicode diagnostic"), result(r),
          detail(text)
    {
    }
};
inline void check(ag_result r)
{
    if (r < 0)
    {
        wchar_t text[1024]{};
        ag_last_error(text, 1024, nullptr);
        throw Error(r, text);
    }
}
class Object
{
  protected:
    struct State
    {
        ag_handle handle{};
        std::shared_ptr<State> context;
        ~State()
        {
            ag_destroy(handle);
        }
    };
    std::shared_ptr<State> state_;
    void own(ag_handle h, const Object *context = nullptr)
    {
        try
        {
            state_ = std::make_shared<State>();
        }
        catch (...)
        {
            // Creation succeeded, but the consumer could not allocate its RAII state.
            ag_destroy(h);
            throw;
        }
        state_->handle = h;
        if (context)
            state_->context = context->state_;
    }

  public:
    Object() = default;
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
    Object(Object &&) = default;
    Object &operator=(Object &&) = default;
    ag_handle handle() const noexcept
    {
        return state_ ? state_->handle : nullptr;
    }
    void *hwnd() const
    {
        void *h{};
        check(ag_native_window(handle(), &h));
        return h;
    }
    void bounds(ag_rect r)
    {
        check(ag_bounds_set(handle(), r));
    }
    ag_rect client_bounds() const
    {
        ag_rect r{};
        check(ag_client_bounds(handle(), &r));
        return r;
    }
};
/// Owns the UI-thread factories, private B612 fonts and immutable theme. Destroy last.
class Context : public Object
{
  public:
    Context()
    {
        ag_handle h{};
        check(ag_context_create(&h));
        own(h);
    }
    ag_theme theme() const
    {
        ag_theme t{};
        check(ag_theme_get(handle(), &t));
        return t;
    }
    void theme(const ag_theme &t)
    {
        check(ag_theme_set(handle(), &t));
    }
};
/// Standalone application window and optional message loop; host HWNDs need no Window
/// wrapper.
class Window : public Object
{
  public:
    Window(Context &c, const wchar_t *title, int w = 1000, int h = 700, ag_event_fn callback = nullptr,
           void *user = nullptr)
    {
        ag_handle o{};
        check(ag_window_create(c.handle(), title, w, h, callback, user, &o));
        own(o, &c);
    }
    void show(bool v = true)
    {
        check(ag_window_show(handle(), v));
    }
    void close()
    {
        check(ag_window_close(handle()));
    }
    int run()
    {
        int32_t code{};
        check(ag_window_run(handle(), &code));
        return code;
    }
    void fit()
    {
        check(ag_window_fit(handle()));
    }
    void defer_close(bool value)
    {
        check(ag_window_defer_close(handle(), value));
    }
    void timer(uint32_t id, uint32_t milliseconds)
    {
        check(ag_window_timer(handle(), id, milliseconds));
    }
    void title(const wchar_t *text)
    {
        check(ag_window_title(handle(), text));
    }
    bool single_instance(const wchar_t *id)
    {
        uint32_t p{};
        check(ag_single_instance(handle(), id, &p));
        return p != 0;
    }
};
/// Tray lifetime and Show/Hide/Exit integration for a library-owned Window.
class Tray : public Object
{
  public:
    Tray(Window &w, const wchar_t *title)
    {
        ag_handle h{};
        check(ag_tray_create(w.handle(), title, &h));
        own(h, &w);
    }
};
/// Native settings/readout controls. Keep parent HWND alive and destroy on the UI thread.
class Control : public Object
{
  public:
    Control() = default;
    Control(Context &c, void *parent, const ag_control_desc &desc, ag_event_fn callback = nullptr,
            void *user = nullptr)
    {
        ag_handle h{};
        check(ag_control_create(c.handle(), parent, &desc, callback, user, &h));
        own(h, &c);
    }
    void value(double v)
    {
        check(ag_control_value_set(handle(), v));
    }
    double value() const
    {
        double v{};
        check(ag_control_value_get(handle(), &v));
        return v;
    }
    void text(const wchar_t *s)
    {
        check(ag_control_text_set(handle(), s));
    }
    std::wstring text() const
    {
        uint32_t count{};
        check(ag_control_text_get(handle(), nullptr, 0, &count));
        std::wstring result(count, 0);
        check(ag_control_text_get(handle(), result.data(), count, &count));
        result.resize(count ? count - 1 : 0);
        return result;
    }
    void enabled(bool value)
    {
        check(ag_control_enabled(handle(), value));
    }
    ag_point text_extent(float width_dips) const
    {
        ag_point size{};
        check(ag_control_text_extent(handle(), width_dips, &size));
        return size;
    }
    void add(const wchar_t *text)
    {
        check(ag_choice_add(handle(), text));
    }
    void events(uint32_t flags)
    {
        check(ag_control_events(handle(), flags));
    }
    uint32_t activity() const
    {
        uint32_t flags{};
        check(ag_control_activity(handle(), &flags));
        return flags;
    }
    void update_begin()
    {
        check(ag_control_update_begin(handle()));
    }
    void update_end()
    {
        check(ag_control_update_end(handle()));
    }
    // Keep the control alive and balance nested redraw suppression on exception.
    // The scope and control must stay on their owning UI thread; do not pump here.
    class UpdateScope
    {
        std::shared_ptr<State> state;
      public:
        explicit UpdateScope(Control &control) : state(control.state_)
        {
            check(ag_control_update_begin(control.handle()));
        }
        UpdateScope(const UpdateScope &) = delete;
        UpdateScope &operator=(const UpdateScope &) = delete;
        UpdateScope(UpdateScope &&) noexcept = default;
        UpdateScope &operator=(UpdateScope &&) = delete;
        ~UpdateScope() { if (state) ag_control_update_end(state->handle); }
        void finish()
        {
            if (auto owner = std::exchange(state, {}))
                check(ag_control_update_end(owner->handle));
        }
    };
    [[nodiscard]] UpdateScope update_scope() { return UpdateScope(*this); }
    void replace(std::span<const wchar_t *const> items, int32_t selected = -1)
    {
        check(ag_choice_replace(handle(), items.data(), uint32_t(items.size()), selected));
    }
    float preferred_width() const
    {
        float width{};
        check(ag_choice_preferred_width(handle(), &width));
        return width;
    }
    void viewport_notify(bool enabled)
    {
        check(ag_panel_viewport_notify(handle(), enabled));
    }
    ag_rect viewport() const
    {
        ag_rect r{};
        check(ag_panel_viewport_get(handle(), &r));
        return r;
    }

    void status(const wchar_t *s, uint32_t state = 0)
    {
        check(ag_status_submit(handle(), s, state));
    }
    void extent(float height)
    {
        check(ag_panel_extent(handle(), height));
    }
    Control tab(const wchar_t *name)
    {
        ag_handle h{};
        check(ag_tab_add(handle(), name, &h));
        Control p;
        p.own(h, this);
        return p;
    }
    void equal_width(bool enabled) { check(ag_tab_equal_width(handle(), enabled)); }
};
class Surface : public Object
{
  public:
    void reset_device()
    {
        check(ag_device_reset(handle()));
    }
    ag_metrics metrics() const
    {
        ag_metrics m{};
        check(ag_surface_metrics(handle(), &m));
        return m;
    }
};
/// Bulk Direct2D commands for schematics, instruments and flat overlays.
class Path : public Object
{
  public:
    Path(Context &c, std::span<const ag_path_command> commands)
    {
        ag_handle h{};
        check(ag_path_create(c.handle(),commands.data(),uint32_t(commands.size()),&h));
        own(h,&c);
    }
};
class Canvas : public Surface
{
  public:
    Canvas(Context &c, void *parent, ag_rect r)
    {
        ag_handle h{};
        check(ag_canvas_create(c.handle(), parent, r, &h));
        own(h, &c);
    }
    void input(ag_canvas_pointer_fn callback,void* user=nullptr){check(ag_canvas_input(handle(),callback,user));}
    ag_canvas_render_state render_state() const
    {
        ag_canvas_render_state state{};state.size=sizeof(state);state.version=1;
        check(ag_canvas_state(handle(),&state));return state;
    }
    void submit(std::span<const ag_draw> commands, std::span<const ag_point> points = {})
    {
        check(ag_canvas_submit(handle(), commands.data(), uint32_t(commands.size()), points.data(),
                               uint32_t(points.size())));
    }
    void submit_paths(std::span<const ag_draw> commands, std::span<const ag_path_draw> paths,
                      std::span<const ag_point> points = {})
    {
        check(ag_canvas_submit_paths(handle(),commands.data(),uint32_t(commands.size()),
              points.data(),uint32_t(points.size()),paths.data(),uint32_t(paths.size())));
    }
    void png(const wchar_t *file, uint32_t w, uint32_t h, float dpi = 96)
    {
        check(ag_canvas_export_png(handle(), file, w, h, dpi));
    }
};
/// Telemetry history panels with units, patterned series, autoscaling and missing-data
/// gaps.
class Chart : public Surface
{
  public:
    void png(const wchar_t *file, uint32_t w, uint32_t h, float dpi = 96)
    {
        check(ag_chart_export_png(handle(), file, w, h, dpi));
    }
    Chart(Context &c, void *parent, ag_rect r)
    {
        ag_handle h{};
        check(ag_chart_create(c.handle(), parent, r, &h));
        own(h, &c);
    }
    void submit(std::span<const ag_chart_panel> panels, std::span<const ag_series> series,
                std::span<const ag_sample> samples)
    {
        check(ag_chart_submit(handle(), panels.data(), uint32_t(panels.size()), series.data(),
                              uint32_t(series.size()), samples.data(), uint32_t(samples.size())));
    }
};
/// D3D11 indexed meshes, left-handed camera, blur and a Direct2D overlay.
class Scene : public Surface
{
  public:
    Scene(Context &c, void *parent, ag_rect r, bool warp = false)
    {
        ag_handle h{};
        check(ag_scene_create(c.handle(), parent, r, warp, &h));
        own(h, &c);
    }
    void mesh(std::span<const ag_vertex> vertices, std::span<const uint32_t> indices)
    {
        check(ag_scene_mesh(handle(), vertices.data(), uint32_t(vertices.size()), indices.data(),
                            uint32_t(indices.size())));
    }
    void png(const wchar_t *file)
    {
        check(ag_scene_export_png(handle(), file));
    }
    void camera(const ag_camera &c)
    {
        check(ag_scene_camera(handle(), &c));
    }
    void options(float blur = 0, bool wire = false)
    {
        check(ag_scene_options(handle(), blur, wire));
    }
    void overlay(std::span<const ag_draw> commands, std::span<const ag_point> points = {})
    {
        check(ag_scene_overlay(handle(), commands.data(), uint32_t(commands.size()), points.data(),
                               uint32_t(points.size())));
    }
};
/// Schema-neutral UTF-8 sections and typed values with transactional disk replacement.
class Settings : public Object
{
  public:
    explicit Settings(const wchar_t *path = nullptr)
    {
        ag_handle h{};
        check(ag_settings_create(path, &h));
        own(h);
    }
    void load()
    {
        check(ag_settings_load(handle()));
    }
    void save()
    {
        check(ag_settings_save(handle()));
    }
    void set(const wchar_t *section, const wchar_t *key, const wchar_t *value)
    {
        check(ag_settings_set(handle(), section, key, value));
    }
    std::wstring get(const wchar_t *section, const wchar_t *key) const
    {
        uint32_t n{};
        check(ag_settings_get(handle(), section, key, nullptr, 0, &n));
        std::wstring s(n, 0);
        check(ag_settings_get(handle(), section, key, s.data(), n, &n));
        s.resize(n - 1);
        return s;
    }
    void number(const wchar_t *s, const wchar_t *k, double v)
    {
        check(ag_settings_set_number(handle(), s, k, v));
    }
    double number(const wchar_t *s, const wchar_t *k, double fallback, double lo, double hi) const
    {
        double v{};
        check(ag_settings_get_number(handle(), s, k, fallback, lo, hi, &v));
        return v;
    }
    void set_boolean(const wchar_t *s, const wchar_t *k, bool v)
    {
        check(ag_settings_set_bool(handle(), s, k, v));
    }
    bool boolean(const wchar_t *s, const wchar_t *k, bool fallback) const
    {
        uint32_t v{};
        check(ag_settings_get_bool(handle(), s, k, fallback, &v));
        return v != 0;
    }
};
/// 2D coordinate mapping and cursor-anchored zoom; no implicit axis conversion.
class Viewport
{
  public:
    ag_viewport data{};
    ag_point project(double x, double y) const
    {
        ag_point p{};
        check(ag_viewport_project(&data, x, y, &p));
        return p;
    }
    std::pair<double, double> unproject(ag_point p) const
    {
        double x, y;
        check(ag_viewport_unproject(&data, p, &x, &y));
        return {x, y};
    }
    void zoom(ag_point anchor, double factor)
    {
        check(ag_viewport_zoom(&data, anchor, factor));
    }
};
} // namespace ass::graphics

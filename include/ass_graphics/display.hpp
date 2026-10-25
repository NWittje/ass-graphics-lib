#pragma once
#include "graphics.hpp"
#include "replay.h"
namespace ass::graphics
{
class DisplayCandidate : public Object
{
  public:
    DisplayCandidate(ag_handle h, Object &owner)
    {
        own(h, &owner);
    }
    void constants(uint32_t pass, uint32_t slot, std::span<const std::byte> data,
                   std::span<const ag_constant_binding> bindings = {})
    {
        check(ag_display_candidate_constants(handle(), pass, slot, data.data(), uint32_t(data.size()), bindings.data(),
                                             uint32_t(bindings.size())));
    }
};
class Display : public Object
{
  public:
    Display(Context &c, void *parent, ag_rect bounds, const ag_display_desc &desc)
    {
        ag_handle h{};
        check(ag_display_create(c.handle(), parent, bounds, &desc, &h));
        own(h, &c);
    }
    DisplayCandidate prepare(const ag_display_desc *desc, const ag_effect_graph *graph = nullptr)
    {
        ag_handle c{};
        check(ag_display_prepare(handle(), desc, graph, &c));
        return DisplayCandidate(c, *this);
    }
    void commit(DisplayCandidate &c)
    {
        check(ag_display_commit(handle(), c.handle()));
    }
    void constants(uint32_t pass, uint32_t slot, std::span<const std::byte> data)
    {
        check(ag_display_constants(handle(), pass, slot, data.data(), uint32_t(data.size())));
    }
    void clear_history()
    {
        check(ag_display_history_clear(handle()));
    }
    void configure(const ag_display_desc &desc)
    {
        check(ag_display_configure(handle(), &desc));
    }
    void output_size(uint32_t w, uint32_t h)
    {
        check(ag_display_output_size(handle(), w, h));
    }
    void submit(std::span<const ag_draw> draws, std::span<const ag_point> points = {},
                std::span<const ag_path_draw> paths = {})
    {
        check(ag_display_submit(handle(), draws.data(), uint32_t(draws.size()), points.data(), uint32_t(points.size()),
                                paths.data(), uint32_t(paths.size())));
    }
    void submit_replay(const ag_replay_snapshot &snapshot){check(ag_display_submit_replay(handle(), &snapshot));}
    ag_replay_metrics replay_metrics()const{ag_replay_metrics m{sizeof(m),1};check(ag_display_replay_metrics(handle(), &m));return m;}
    void effects(const ag_effect_graph *graph = nullptr)
    {
        check(ag_display_effects(handle(), graph));
    }
    void parameters(uint32_t pass, std::span<const std::byte> bytes)
    {
        check(ag_display_parameters(handle(), pass, bytes.data(), uint32_t(bytes.size())));
    }
    void render(double seconds)
    {
        check(ag_display_render(handle(), seconds));
    }
    void export_png(const wchar_t *path)
    {
        check(ag_display_export_png(handle(), path));
    }
    void reset()
    {
        check(ag_device_reset(handle()));
    }
    ag_display_metrics metrics() const
    {
        ag_display_metrics m{sizeof(m), 1};
        check(ag_display_metrics_get(handle(), &m));
        return m;
    }
    ag_display_caps capabilities() const
    {
        ag_display_caps c{sizeof(c), 1};
        check(ag_display_capabilities(handle(), &c));
        return c;
    }
};
class Dialog : public Object
{
  public:
    Dialog(Window &owner, const wchar_t *title, uint32_t w, uint32_t h, ag_event_fn callback = nullptr,
           void *user = nullptr)
    {
        ag_handle d{};
        check(ag_dialog_create(owner.handle(), title, w, h, callback, user, &d));
        own(d, &owner);
    }
    void show(bool visible = true, bool activate = true)
    {
        check(ag_dialog_show(handle(), visible, activate));
    }
    void close()
    {
        check(ag_window_close(handle()));
    }
    void defer_close(bool enabled)
    {
        check(ag_window_defer_close(handle(), enabled));
    }
};
inline void context_requests(Object &o, ag_context_fn callback, void *user = nullptr)
{
    check(ag_context_requests(o.handle(), callback, user));
}
inline void monitors(Context &c, ag_monitor_fn callback, void *user = nullptr)
{
    check(ag_monitors(c.handle(), callback, user));
}
inline ag_window_placement placement(Window &w)
{
    ag_window_placement p{sizeof(p), 1};
    check(ag_window_placement_get(w.handle(), &p));
    return p;
}
inline void placement(Window &w, const ag_window_placement &p)
{
    check(ag_window_placement_set(w.handle(), &p));
}
inline void shortcut(Window &w, uint32_t key, uint32_t modifiers, uint32_t id)
{
    check(ag_window_shortcut(w.handle(), key, modifiers, id));
}
inline void borderless(Window &w, bool enabled, const wchar_t *monitor = nullptr)
{
    check(ag_window_borderless(w.handle(), enabled, monitor));
}
} // namespace ass::graphics

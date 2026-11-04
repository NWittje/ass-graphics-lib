#pragma once
#include "display.hpp"
#include "ordered.h"
namespace ass::graphics
{
class OrderedDisplay;
class OrderedProgram : public Object
{
    friend class OrderedDisplay;
    OrderedProgram(const Object &owner, ag_handle value)
    {
        own(value, &owner);
    }
};
/// UI-thread fixed-source integer material renderer. Inputs are copied at admission.
class OrderedDisplay : public Object
{
  public:
    OrderedDisplay(Context &context, const ag_display_desc &desc, void *parent = nullptr, ag_rect bounds = {})
    {
        ag_handle value{};
        check(ag_ordered_display_create(context.handle(), parent, bounds, &desc, &value, nullptr));
        own(value, &context);
    }
    ag_ordered_caps capabilities() const
    {
        ag_ordered_caps result{sizeof(result), 1};
        check(ag_ordered_capabilities(handle(), &result));
        return result;
    }
    OrderedProgram program(const ag_ordered_program_desc &desc)
    {
        ag_handle value{};
        check(ag_ordered_program_create(handle(), &desc, &value));
        return OrderedProgram(*this, value);
    }
    void submit(const ag_ordered_snapshot &snapshot)
    {
        check(ag_ordered_submit(handle(), &snapshot));
    }
    void replay(uint64_t sequence)
    {
        check(ag_ordered_replay(handle(), sequence));
    }
    void render(double seconds)
    {
        check(ag_display_render(handle(), seconds));
    }
    void output_size(uint32_t w, uint32_t h)
    {
        check(ag_display_output_size(handle(), w, h));
    }
    void effects(const ag_effect_graph *graph = nullptr)
    {
        check(ag_display_effects(handle(), graph));
    }
    void export_png(const wchar_t *file)
    {
        check(ag_display_export_png(handle(), file));
    }
    void reset()
    {
        check(ag_device_reset(handle()));
    }
    ag_ordered_state state() const
    {
        ag_ordered_state result{sizeof(result), 1};
        check(ag_ordered_state_get(handle(), &result));
        return result;
    }
};
} // namespace ass::graphics

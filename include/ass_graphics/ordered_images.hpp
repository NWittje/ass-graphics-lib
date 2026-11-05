#pragma once
#include "ordered.hpp"
#include "ordered_images.h"
namespace ass::graphics
{
class OrderedImage : public Object
{
  public:
    OrderedImage(OrderedDisplay &display, const ag_ordered_image_desc &desc)
    {
        ag_handle value{};
        check(ag_ordered_image_create(display.handle(), &desc, &value));
        own(value, &display);
    }
    ag_ordered_image_state state() const
    {
        ag_ordered_image_state value{sizeof(value), 1};
        check(ag_ordered_image_state_get(handle(), &value));
        return value;
    }
};
inline void submit_images(OrderedDisplay &display, const ag_ordered_snapshot &snapshot,
                          const OrderedImage *first, const OrderedImage *second = nullptr)
{
    ag_ordered_image_bindings bindings{
        sizeof(bindings),
        1,
        {first ? first->handle() : nullptr, second ? second->handle() : nullptr},
        {0, 0}};
    check(ag_ordered_submit_images(display.handle(), &snapshot, &bindings));
}
} // namespace ass::graphics

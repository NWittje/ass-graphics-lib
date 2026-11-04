#pragma once
#include "graphics.hpp"
#include "timing.h"
namespace ass::graphics
{
inline ag_display_timing timing(const Object &display)
{
    ag_display_timing result{sizeof(result), 1};
    check(ag_display_timing_get(display.handle(), &result));
    return result;
}
} // namespace ass::graphics

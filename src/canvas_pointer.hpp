#pragma once
#include <ass_graphics/ass_graphics.h>
namespace ag {
// Signed client pixels -> local DIPs. No viewport/drawing transform is applied.
inline ag_point canvas_local_point(int x, int y, uint32_t dpi) noexcept
{
    const float scale=96.f/float(dpi);
    return {float(x)*scale,float(y)*scale};
}
}

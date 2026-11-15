#pragma once
#include "ass_graphics.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Opt-in, world-space Y-up lighting. Vertex RGB is albedo; alpha is unchanged.
       Sun direction points FROM the surface TO the sun. RGB and alpha fields must
       be finite [0,1] (light/fog alpha is reserved and ignored). Intensities [0,16].
       Direction components are bounded to +/-1e6; length must exceed 1e-6.
       Fog is linear camera-to-surface Euclidean distance in mesh coordinate units:
       0 <= start < end <= 1e12, with end-start >= 1e-6. It is applied after lighting, before existing blur
       and overlays. These artistic controls are not a physical atmosphere model.
       Initialize with defaults; size must equal sizeof(ag_scene_lighting_options).
       enabled is exactly 0 or 1. Disabled uses the unchanged legacy shader path.
       Set/get use the Scene owning UI thread; failed validation changes no state. */
    typedef struct ag_scene_lighting_options
    {
        uint32_t size, enabled;
        ag_vec3 sun_direction;
        float sun_intensity;
        ag_color sun_color, sky_color, ground_color;
        float sky_intensity, ground_intensity;
        ag_color fog_color;
        float fog_start, fog_end;
    } ag_scene_lighting_options;
    AG_API ag_result ag_scene_lighting_defaults(ag_scene_lighting_options *options);
    AG_API ag_result ag_scene_lighting_set(ag_handle scene, const ag_scene_lighting_options *options);
    AG_API ag_result ag_scene_lighting_get(ag_handle scene, ag_scene_lighting_options *options);
#ifdef __cplusplus
}
#endif

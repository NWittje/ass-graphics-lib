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
    /* Independent opt-in fog override, applied after either lighting path.
       enabled=0 restores unmodified 1.9 camera-distance/legacy behavior.
       XZ origin is world-space, height and camera do not affect radius.
       Origin components finite within +/-1e12; 0 <= start < end <= 1e12,
       end-start >= 1e-6. Color finite [0,1]; its alpha is ignored.
       Getter returns defaults (disabled, origin 0, radii 0/100, white) on new
       Scenes. Set/get require owning UI thread. Settings survive mesh/camera
       changes and reset; failed validation changes no state. */
    typedef struct ag_scene_radial_fog_options
    {
        uint32_t size, enabled;
        float origin_x, origin_z, start_radius, end_radius;
        ag_color fog_color;
    } ag_scene_radial_fog_options;
    AG_API ag_result ag_scene_radial_fog_set(ag_handle scene, const ag_scene_radial_fog_options *options);
    AG_API ag_result ag_scene_radial_fog_get(ag_handle scene, ag_scene_radial_fog_options *options);
    /* Opt-in procedural world-+Y sky; below-horizon rays use horizon_color.
       No Context-theme change or texture. Colors finite [0,1], alpha ignored.
       Set/get UI-thread-only; retained across reset. Disabled by default. */
    typedef struct ag_scene_sky_options
    {
        uint32_t size, enabled;
        ag_color horizon_color, zenith_color;
    } ag_scene_sky_options;
    AG_API ag_result ag_scene_sky_set(ag_handle scene, const ag_scene_sky_options *options);
    AG_API ag_result ag_scene_sky_get(ag_handle scene, ag_scene_sky_options *options);
    /* Explicit opt-in alpha reinterpretation: color.a becomes interpolated
       fog weight [0,1], with opaque output. Overrides other fog AFTER lighting.
       Disabled retains all existing alpha/fog semantics. Current/subsequent
       mesh weights must be[0,1] while enabled. Size24; enabled0/1; finite color
       channels[0,1], alpha ignored. UI-thread get/set; retained across reset.
       Invalid requests leave state unchanged. */
    typedef struct ag_scene_vertex_fog_options
    {
        uint32_t size, enabled;
        ag_color fog_color;
    } ag_scene_vertex_fog_options;
    AG_API ag_result ag_scene_vertex_fog_set(ag_handle scene, const ag_scene_vertex_fog_options *options);
    AG_API ag_result ag_scene_vertex_fog_get(ag_handle scene, ag_scene_vertex_fog_options *options);
#ifdef __cplusplus
}
#endif

#pragma once
#include "ass_graphics.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Convenience API for translucent slot 0: opt-in horizontal-water mesh, drawn after terrain, before
       blur/UI. Depth-tested LESS_EQUAL, depth writes disabled, straight-alpha blending. Fixed unlit
       RGB/opacity; vertex normal/color/alpha and terrain fog ignored. Fresnel adds
       (1-opacity)*strength*(1-abs(dot(+Y,view)))^5 to opacity. Not a physical fluid/reflection/refraction
       model; caller supplies geometry. Size32; enabled0/1; finite color channels/opacity/strength in[0,1].
       Color.a ignored. Defaults disabled, opacity .35, strength0. Owning-UI-thread-only; CPU mesh/settings
       survive camera/reset. Invalid requests change no state. Independent limits: 262,144 vertices and
       786,432 triangle-list indices. No automatic water sorting; use a non-overlapping horizontal sheet. */
    typedef struct ag_scene_water_options
    {
        uint32_t size, enabled;
        float opacity, fresnel_strength;
        ag_color color;
    } ag_scene_water_options;
    AG_API ag_result ag_scene_water_set(ag_handle scene, const ag_scene_water_options *options);
    AG_API ag_result ag_scene_water_get(ag_handle scene, ag_scene_water_options *options);
    AG_API ag_result ag_scene_water_mesh(ag_handle scene, const ag_vertex *vertices, uint32_t vertex_count,
                                         const uint32_t *indices, uint32_t index_count);
    AG_API ag_result ag_scene_water_clear(ag_handle scene);
#ifdef __cplusplus
}
#endif

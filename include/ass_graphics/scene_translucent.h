#pragma once
#include "scene_water.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Four independent translucent mesh slots, 0..3. Water convenience API uses 0.
       Uses ag_vertex layout40, with fixed unlit RGB and opacity independent of
       vertex colors/alpha and terrain fog. All attributes must still be finite.
       Terrain renders first. Slots draw in ascending order, triangle submission
       order within each slot, depth-tested LESS_EQUAL with depth writes disabled.
       There is no automatic triangle sorting: callers choose order back-to-front;
       intersecting transparent meshes may exhibit ordering artifacts. Suitable for
       simple water sheets and reference walls, not general transparent scenes.
       Optional Fresnel assumes a horizontal +Y surface; use strength0 for walls.
       Each slot has independent enabled/material/mesh state; clear drops geometry
       while retaining material. CPU state survives camera changes/device reset.
       Limits per slot: 262144 vertices, 786432 triangle-list indices. UI thread only.
       Set/mesh validate transactionally. No pointers are retained after return. */
    typedef ag_scene_water_options ag_scene_translucent_options;
    AG_API ag_result ag_scene_translucent_get(ag_handle scene, uint32_t layer,
                                              ag_scene_translucent_options *options);
    AG_API ag_result ag_scene_translucent_set(ag_handle scene, uint32_t layer,
                                              const ag_scene_translucent_options *options);
    AG_API ag_result ag_scene_translucent_mesh(ag_handle scene, uint32_t layer, const ag_vertex *vertices,
                                               uint32_t vertex_count, const uint32_t *indices,
                                               uint32_t index_count);
    AG_API ag_result ag_scene_translucent_clear(ag_handle scene, uint32_t layer);
#ifdef __cplusplus
}
#endif

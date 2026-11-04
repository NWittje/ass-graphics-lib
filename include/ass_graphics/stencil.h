#pragma once
#include "replay.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Additive ABI1 descriptors. All calls use the Display's UI thread. Arrays
       are borrowed for submit only; success means CPU acceptance, not display
       or GPU completion. Each snapshot is a complete ordered reconstruction. */
    enum ag_stencil_kind
    {
        AG_STENCIL_REPLAY = 1,
        AG_STENCIL_CLEAR,
        AG_STENCIL_WRITE_FILL,
        AG_STENCIL_WRITE_STROKE,
        AG_STENCIL_TEST,
        AG_STENCIL_DISABLE_TEST,
        /* context + reference (AG_REPLAY_TILE resource ID, or zero for solid).
           Children inherit paint. REPLAY_COLOUR replaces tile with solid paint.
           Aliased opaque fill/positive-width stroke only; source-raster origin
           (0,0), row-major texel = (floor(y)&3)*4+(floor(x)&3). Transforms/clips
           change coverage, not phase. CLEAR remains solid opaque black. */
        AG_STENCIL_PAINT_TILE
    };
    enum ag_stencil_comparison
    {
        AG_STENCIL_EQUAL = 1,
        AG_STENCIL_NOT_EQUAL = 2
    };
    enum ag_stencil_flags
    {
        AG_STENCIL_COMPLETE = 1
    };
    typedef struct ag_stencil_command
    {
        uint32_t size, version, kind, context;
        uint32_t mask_index, path, clear_bits, set_bits;
        uint32_t test_bits, reference, comparison, reserved;
        ag_replay_command replay;
    } ag_stencil_command;
    typedef struct ag_stencil_snapshot
    {
        uint32_t size, version, command_count, resource_count;
        const ag_stencil_command *commands;
        const ag_replay_resource *resources;
        uint32_t mask_count, initial_values[2], flags;
        float flattening_tolerance;
        uint32_t mesh_vertex_limit, reserved[2];
    } ag_stencil_snapshot;
    typedef struct ag_stencil_metrics
    {
        uint32_t size, version;
        uint64_t cpu_bytes, mesh_vertices, mesh_builds, mesh_cache_hits;
        uint64_t texture_bytes, buffer_bytes, resource_creations, source_draws;
        uint64_t gpu_draws, mask_clears;
        double admission_ms;
    } ag_stencil_metrics;
    /* Fixed-source Display only. 0..2 independent byte masks start at explicit
       values. WRITE applies (old & ~clear_bits) | set_bits, ignoring paint/test.
       TEST selects ONE mask and compares masked bytes; zero is valid data.
       CLEAR ignores clip/transform/test and preserves previously painted colour.
       Child contexts inherit tests. No simultaneous two-mask predicate exists.

       REPLAY has only its nested command populated. Other records have a zero
       nested command; all unused named fields are zero (padding is irrelevant).
       Masked paint supports opaque solid or tile FILL/DRAW with AA disabled;
       CLEAR always paints opaque black. Tile paint also works without a test.
       Masked images, fractional opacity/alpha and hairline strokes are rejected.
       Mask writes accept filled or positive-width stroked paths, using current
       transform and frozen clip. No CPU colour rasterization or readback occurs.

       Positive finite tolerance is in source-raster units. mesh_vertex_limit is
       1..1048576, counting distinct referenced meshes. Limits: 16384 commands,
       4096 resources, 64 MiB retained/candidate CPU payload, 16 MiB mesh cache.
       Display texture_budget includes the source-sized candidate colour texture,
       each 4-byte D24/S8 mask, retained images and replacement overlap. These are
       payload bounds, not process/driver memory bounds. GPU buffers are separately
       bounded by the mesh cache/retained mesh capacities. See SDK stencil guide. */
    AG_API ag_result ag_display_submit_stencil(ag_handle display, const ag_stencil_snapshot *snapshot);
    AG_API ag_result ag_display_stencil_metrics(ag_handle display, ag_stencil_metrics *out);
#ifdef __cplusplus
}
#endif

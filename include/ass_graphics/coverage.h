#pragma once
#include "ordered_images.h"
#ifdef __cplusplus
extern "C" {
#endif
    enum ag_ordered_backend { AG_BACKEND_ORDERED = 0, AG_BACKEND_VECTOR = 1 };
    /* scene.flags, contract3 only: preserve binary coverage for separately
       supplied fill primitives. Connected strokes retain antialiasing. */
    enum ag_coverage_flags { AG_COVERAGE_BINARY_FILLS = 2 };
    /* Additive ABI1 complete-frame selection. ORDERED accepts contracts1/2;
       VECTOR accepts contract3. All buffers copy on the owning UI thread.
       Contract3 extends contract2 with float2 TEXCOORD0 and Texture2D<float4>t3.
       t3.Load(int3(TEXCOORD0,0)).g is inner coverage; .r is outer-only coverage.
       Coverage is geometric, not colour or an application material operation.
       Outer widths are indexed by command, zero for fills/state/no outer stroke.
       Stroke widths come from the active stroke resource in logical units.
       Pixel positions/coverage coordinates are physical source-raster pixels.
       MOVE/closure and independent paint operations retain their boundaries. */
    typedef struct ag_ordered_backend_snapshot {
        uint32_t size, version, backend, outer_width_count;
        ag_ordered_snapshot scene;
        ag_ordered_image_bindings images;
        const float *outer_widths;
    } ag_ordered_backend_snapshot;
    typedef struct ag_coverage_metrics {
        uint32_t size, version, accepted_backend, completed_backend;
        uint64_t paths, atlas_pages, atlas_bytes, rasterizations;
        double preparation_ms, raster_cpu_ms;
        uint64_t geometry_cache_hits, geometry_cache_builds;
        double geometry_preparation_ms;
    } ag_coverage_metrics;
    /* Selection is attached to the complete scene. Last completed output stays
       visible until the selected candidate completes. Failure keeps acceptance
       unchanged. No implicit software or alternate-backend fallback. */
    AG_API ag_result ag_ordered_submit_backend(ag_handle display, const ag_ordered_backend_snapshot *snapshot);
    AG_API ag_result ag_ordered_coverage_metrics(ag_handle display, ag_coverage_metrics *out);
#ifdef __cplusplus
}
#endif

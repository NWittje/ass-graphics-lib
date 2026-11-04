#pragma once
#include "ass_graphics.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ag_timing_flags
    {
        AG_TIMING_ORDERED_SUBMITTED = 1,
        AG_TIMING_SOURCE_UPDATED = 2,
        AG_TIMING_COMPOSITED = 4,
        AG_TIMING_PRESENT_CALLED = 8,
        AG_TIMING_FAILED = 16
    };
    /* Additive ABI1. UI-owner-thread, nonblocking DONOTFLUSH collection. GPU and
       render/Present CPU fields describe sample_id, not necessarily the latest call.
       IDs count queued render intervals, including material-only submissions;
       ordinary display metrics.frames still counts output compositions.
       ordered_sequence is nonzero only for a new ordered GPU submission in that
       interval; composited_sequence identifies completed ordered colour sampled by
       output composition (zero without composition/availability). Neither is scanout.

       gpu_total_ms brackets all GPU work queued by Render, before Present.
       gpu_source_ms includes target preparation, ordered work/colour commit, lookup
       upload and source/mipmap maintenance. gpu_graph_ms covers effects, output,
       history and backbuffer copy. Only known/non-disjoint successful intervals are
       measurements. Source is not exclusively material execution on mixed intervals.
       CPU render includes driver submission/flush but excludes CPU Present time.

       Admission fields independently report the latest successful full ordered
       Submit's native CPU preparation and original sequence. Replay does not prepare
       a new recipe; ordinary source admission and caller recording are not measured.
       gpu_pending and dropped_samples are current/cumulative, not sample-local.
       No GPU wait/readback occurs; a full four-slot ring drops new timing samples. */
    typedef struct ag_display_timing
    {
        uint32_t size, version;
        uint64_t sample_id, device_generation, ordered_sequence, composited_sequence;
        uint64_t admission_sequence, dropped_samples;
        uint32_t gpu_known, gpu_disjoint, gpu_pending, flags;
        double gpu_total_ms, gpu_source_ms, gpu_graph_ms;
        double cpu_render_ms, cpu_present_ms, cpu_admission_ms;
    } ag_display_timing;
    AG_API ag_result ag_display_timing_get(ag_handle display, ag_display_timing *out);
#ifdef __cplusplus
}
#endif

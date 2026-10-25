#pragma once
#include "display.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* ABI1 additions. UI-thread calls copy all borrowed payloads. The library owns
       retained resources and never interprets application transport formats. */
    enum ag_replay_resource_kind
    {
        AG_REPLAY_PATH = 1,
        AG_REPLAY_STROKE = 2,
        AG_REPLAY_IMAGE = 3
    };
    enum ag_replay_kind
    {
        AG_REPLAY_DRAW = 1,
        AG_REPLAY_FILL,
        AG_REPLAY_CLIP,
        AG_REPLAY_INTERSECT,
        AG_REPLAY_CREATE,
        AG_REPLAY_DISPOSE,
        AG_REPLAY_TRANSFORM,
        AG_REPLAY_COLOUR,
        AG_REPLAY_USE_STROKE,
        AG_REPLAY_OPACITY,
        AG_REPLAY_AA,
        AG_REPLAY_SAMPLING,
        AG_REPLAY_BITMAP,
        AG_REPLAY_CLEAR
    };
    typedef struct ag_replay_segment
    {
        uint32_t kind, reserved;
        double p[6];
    } ag_replay_segment;
    typedef struct ag_replay_resource
    {
        uint32_t size, version, id, kind;
        uint32_t winding, segment_count;
        const ag_replay_segment *segments;
        float width, miter, phase;
        uint32_t cap, join, dash_count;
        const float *dashes;
        uint32_t image_width, image_height, stride, pixel_bytes;
        const void *pixels;
    } ag_replay_resource;
    typedef struct ag_replay_command
    {
        uint32_t size, version, kind, context, resource, value;
        double matrix[6];
        float opacity;
        uint32_t argb;
    } ag_replay_command;
    typedef struct ag_replay_snapshot
    {
        uint32_t size, version, command_count, resource_count;
        const ag_replay_command *commands;
        const ag_replay_resource *resources;
    } ag_replay_snapshot;
    typedef struct ag_replay_metrics
    {
        uint32_t size, version;
        uint64_t cpu_bytes, cache_bytes, resource_creations, cache_hits, commands;
    } ag_replay_metrics;
    AG_API ag_result ag_display_submit_replay(ag_handle display, const ag_replay_snapshot *snapshot);
    AG_API ag_result ag_display_replay_metrics(ag_handle display, ag_replay_metrics *out);
#ifdef __cplusplus
}
#endif

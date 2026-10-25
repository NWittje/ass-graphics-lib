#pragma once
#include "ass_graphics.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* ABI1 additions. All calls use the context UI thread. Descriptors and referenced
       buffers are copied during calls. Shader bytecode is DXBC SM5, never source. */
    enum ag_raster_policy
    {
        AG_RASTER_OUTPUT = 0,
        AG_RASTER_FIXED = 1
    };
    enum ag_texture_format
    {
        AG_BGRA8 = 0,
        AG_RGBA8 = 1,
        AG_RGBA16_FLOAT = 2
    };
    enum ag_input_kind
    {
        AG_INPUT_SOURCE = 0,
        AG_INPUT_PASS = 1,
        AG_INPUT_NAMED = 2,
        AG_INPUT_LOOKUP = 3,
        AG_INPUT_HISTORY = 4
    };
    enum ag_scale_kind
    {
        AG_SCALE_INPUT = 0,
        AG_SCALE_VIEWPORT = 1,
        AG_SCALE_ABSOLUTE = 2
    };
    typedef struct ag_display_desc
    {
        uint32_t size, version, warp, raster_policy;
        float logical_width, logical_height;
        uint32_t source_width, source_height, output_width, output_height;
        uint64_t texture_budget; /* zero:256MiB; maximum:1GiB, includes swap buffers */
    } ag_display_desc;
    typedef struct ag_effect_input
    {
        uint32_t size, version, kind, index, slot, linear, repeat, mipmaps, srgb;
        char name[64]; /* ASCII alias; HISTORY index is 0 or 1, original-source history */
    } ag_effect_input;
    typedef struct ag_effect_pass
    {
        uint32_t size, version;
        char name[64];
        const void *vertex_bytecode;
        uint32_t vertex_bytes;
        const void *pixel_bytecode;
        uint32_t pixel_bytes;
        const ag_effect_input *inputs;
        uint32_t input_count;
        uint32_t scale_x_kind, scale_y_kind, scale_input;
        float scale_x, scale_y;
        uint32_t format, srgb, mipmaps;
        const void *parameters;
        uint32_t parameter_bytes, parameter_slot;
    } ag_effect_pass;
    typedef struct ag_lookup_texture
    {
        uint32_t size, version;
        char name[64];
        uint32_t width, height, format, stride, mipmaps;
        const void *pixels;
        uint32_t bytes;
    } ag_lookup_texture;
    typedef struct ag_effect_graph
    {
        uint32_t size, version;
        const ag_effect_pass *passes;
        uint32_t pass_count;
        const ag_lookup_texture *lookups;
        uint32_t lookup_count;
        uint32_t history_count, temporal;
    } ag_effect_graph;
    /* b0, VS and PS: source/output/viewport/time then input_size[16], float4 each.
       Size vectors are width,height,1/width,1/height. time is seconds,delta,frame,0.
       Custom parameters occupy a caller-selected b1..b13, padded to 16 bytes.
       Default VS draws a full-screen triangle with SV_VertexID; TEXCOORD0 is UV. */
    typedef struct ag_display_metrics
    {
        uint32_t size, version, source_width, source_height, viewport_width, viewport_height;
        uint64_t frames, source_draws, texture_bytes, readbacks, device_recreations;
        uint64_t cpp_allocations;
        double cpu_ms, gpu_ms;
        uint32_t gpu_known, temporal, suspended, error;
    } ag_display_metrics;
    typedef struct ag_display_caps
    {
        uint32_t size, version, feature_level, max_texture_dimension;
        uint32_t max_passes, max_inputs, max_lookups, max_history;
        uint32_t bgra8, rgba8, rgba16_float, mipmaps;
    } ag_display_caps;
    AG_API ag_result ag_display_create(ag_handle context, void *parent, ag_rect bounds, const ag_display_desc *desc,
                                       ag_handle *out);
    AG_API ag_result ag_display_configure(ag_handle display, const ag_display_desc *desc);
    AG_API ag_result ag_display_output_size(ag_handle display, uint32_t width, uint32_t height);
    AG_API ag_result ag_display_submit(ag_handle display, const ag_draw *commands, uint32_t count,
                                       const ag_point *points, uint32_t point_count, const ag_path_draw *paths,
                                       uint32_t path_count);
    AG_API ag_result ag_display_effects(ag_handle display, const ag_effect_graph *graph);
    AG_API ag_result ag_display_parameters(ag_handle display, uint32_t pass, const void *data, uint32_t bytes);
    AG_API ag_result ag_display_render(ag_handle display, double elapsed_seconds);
    AG_API ag_result ag_display_export_png(ag_handle display, const wchar_t *path);
    AG_API ag_result ag_display_metrics_get(ag_handle display, ag_display_metrics *out);
    AG_API ag_result ag_display_capabilities(ag_handle display, ag_display_caps *out);

    /* 1.2.1 resource transactions. Candidate is UI-thread owned; abort before
       destroying its display. Resize/reset/replacement invalidates a candidate.
       Prepare counts active + replacement texture storage against the new budget. */
    enum ag_constant_kind
    {
        AG_CONSTANT_SOURCE_SIZE = 0,
        AG_CONSTANT_INPUT_SIZE = 1,
        AG_CONSTANT_OUTPUT_SIZE = 2,
        AG_CONSTANT_VIEWPORT_SIZE = 3,
        AG_CONSTANT_FRAME_UINT = 4,
        AG_CONSTANT_TIME_FLOAT = 5
    };
    typedef struct ag_constant_binding
    {
        uint32_t size, version, offset, kind, index;
    } ag_constant_binding;
    AG_API ag_result ag_display_prepare(ag_handle display, const ag_display_desc *desc, const ag_effect_graph *graph,
                                        ag_handle *candidate);
    AG_API ag_result ag_display_candidate_constants(ag_handle candidate, uint32_t pass, uint32_t slot, const void *data,
                                                    uint32_t bytes, const ag_constant_binding *bindings,
                                                    uint32_t count);
    AG_API ag_result ag_display_commit(ag_handle display, ag_handle candidate);
    AG_API ag_result ag_display_abort(ag_handle candidate);
    AG_API ag_result ag_display_constants(ag_handle display, uint32_t pass, uint32_t slot, const void *data,
                                          uint32_t bytes);
    AG_API ag_result ag_display_history_clear(ag_handle display);
    enum { AG_SHORTCUT_REPEAT = 256 };
    /* Keys are VK codes; modifiers bits: Shift=1,Control=2,Alt=4; AG_SHORTCUT_REPEAT opts into key repeat. Replaces the
       same key/modifier registration; id=0 removes it. Emits AG_SHORTCUT/id. */
    AG_API ag_result ag_window_shortcut(ag_handle window, uint32_t key, uint32_t modifiers, uint32_t id);

    typedef struct ag_context_request
    {
        uint32_t size, version;
        ag_handle source;
        ag_point position; /* source client DIPs; keyboard uses client centre */
        uint32_t keyboard;
    } ag_context_request;
    typedef void(__cdecl *ag_context_fn)(void *user, const ag_context_request *request);
    AG_API ag_result ag_context_requests(ag_handle object, ag_context_fn callback, void *user);
    /* Owned dialogs disable only their owner while shown. Destroy before owner.
       Enter/Escape emit AG_DIALOG_ACCEPT/AG_DIALOG_CANCEL; application decides close. */
    AG_API ag_result ag_dialog_create(ag_handle owner, const wchar_t *title, uint32_t width, uint32_t height,
                                      ag_event_fn callback, void *user, ag_handle *out);
    AG_API ag_result ag_dialog_show(ag_handle dialog, uint32_t visible, uint32_t activate);
    typedef struct ag_monitor_info
    {
        uint32_t size, version;
        wchar_t identity[128], device[32];
        int32_t left, top, right, bottom, work_left, work_top, work_right, work_bottom;
        uint32_t primary;
    } ag_monitor_info;
    typedef void(__cdecl *ag_monitor_fn)(void *user, const ag_monitor_info *info);
    typedef struct ag_window_placement
    {
        uint32_t size, version;
        wchar_t monitor[128];
        int32_t left, top, right, bottom; /* restored physical desktop pixels */
        uint32_t dpi, mode;               /* 0 normal,1 maximized,2 borderless; minimize is transient */
    } ag_window_placement;
    AG_API ag_result ag_monitors(ag_handle context, ag_monitor_fn callback, void *user);
    AG_API ag_result ag_window_placement_get(ag_handle window, ag_window_placement *out);
    AG_API ag_result ag_window_placement_set(ag_handle window, const ag_window_placement *placement);
    AG_API ag_result ag_window_borderless(ag_handle window, uint32_t enabled, const wchar_t *monitor);
#ifdef __cplusplus
}
#endif

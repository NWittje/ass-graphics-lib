#pragma once
/* ass_graphics_lib ABI 1. All lengths count elements; strings are UTF-16.
   Handles are owned: destroy children before their context. No concurrent destroy.
   UI handles must be created/destroyed on their context's thread. Canvas/chart/status
   submission is worker-safe; Scene operations require the UI thread.
   Functions return HRESULT-compatible int32 values; diagnostics are thread-local.
   DLL-owned memory is released inside the DLL, never with caller free/delete.
   The library borrows caller buffers only during a call and copies submitted data. */
#include <stddef.h>
#include <stdint.h>
#if defined(ASS_GRAPHICS_BUILD)
#define AG_API __declspec(dllexport)
#else
#define AG_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    typedef void *ag_handle;
    typedef int32_t ag_result;
    typedef struct ag_point
    {
        float x, y;
    } ag_point;
    typedef struct ag_rect
    {
        float left, top, right, bottom;
    } ag_rect;
    typedef struct ag_color
    {
        float r, g, b, a;
    } ag_color;
    typedef struct ag_vec3
    {
        float x, y, z;
    } ag_vec3;
    typedef struct ag_theme
    {
        ag_color background, foreground, accent, muted, error, warning, success;
        float font_size, spacing; /* DIPs; font size (0,1000], colors in [0,1]. */
        wchar_t font_family[64];
    } ag_theme;
    typedef struct ag_viewport
    {
        double left, top, right, bottom;
        ag_rect screen;
    } ag_viewport;
    typedef struct ag_camera
    {
        ag_vec3 eye, target, up;
        float fov_y, near_z, far_z; /* radians; left-handed world with explicit up. */
    } ag_camera;
    typedef struct ag_vertex
    {
        ag_vec3 position, normal;
        ag_color color;
    } ag_vertex;
    enum ag_draw_kind
    {
        AG_LINE = 1,
        AG_RECT,
        AG_FILL_RECT,
        AG_ELLIPSE,
        AG_FILL_ELLIPSE,
        AG_TEXT,
        AG_POLYLINE,
        AG_POLYGON,
        AG_PUSH_CLIP,
        AG_POP_CLIP,
        AG_TRANSFORM,
        AG_IMAGE,
        AG_SEGMENTS,
        AG_PATH
    };
    /* rect is DIP coordinates; LINE uses (left,top)->(right,bottom).
       points index the submission's point array. TRANSFORM uses matrix[6].
       TEXT/IMAGE/SEGMENTS use text, a bounded NUL-terminated string.
       IMAGE loads a file when the snapshot is adopted, never on every paint. */
    typedef struct ag_draw
    {
        uint32_t kind, flags;
        ag_rect rect;
        ag_color color;
        float width, font_size;
        uint32_t first_point, point_count;
        float matrix[6];
        wchar_t text[256];
    } ag_draw;
    /* Additive ABI1 cached geometry API. All path calls and submit_paths require
       the owning UI thread. MOVE starts a figure; CLOSE ends it. An unclosed
       figure ends open at the next MOVE/end. Coordinates are copied. Even-odd fill.
       D2D factory geometry is device independent and survives target recreation. */
    enum ag_path_kind { AG_PATH_MOVE=1, AG_PATH_LINE, AG_PATH_QUADRATIC, AG_PATH_CUBIC, AG_PATH_CLOSE };
    enum ag_path_cap { AG_CAP_FLAT=0, AG_CAP_ROUND=1, AG_CAP_SQUARE=2 };
    enum ag_path_join { AG_JOIN_MITER=0, AG_JOIN_ROUND=1, AG_JOIN_BEVEL=2 };
    typedef struct ag_path_command { uint32_t kind; ag_point p1, p2, p3; } ag_path_command;
    typedef struct ag_path_draw { ag_handle path; uint32_t fill, cap, join; } ag_path_draw;
    AG_API ag_result ag_path_create(ag_handle context, const ag_path_command *commands,
                                    uint32_t count, ag_handle *out);
    /* AG_PATH first_point indexes paths; point_count must be zero. Color/width
       come from ag_draw. Submitted snapshots retain geometry independently of
       path handle lifetime; destroy all surfaces before their context. */
    AG_API ag_result ag_canvas_submit_paths(ag_handle canvas, const ag_draw *commands,
            uint32_t count, const ag_point *points, uint32_t point_count,
            const ag_path_draw *paths, uint32_t path_count);
    enum ag_control_kind
    {
        AG_LABEL = 1,
        AG_BUTTON,
        AG_CHECKBOX,
        AG_EDIT,
        AG_NUMERIC,
        AG_SLIDER,
        AG_STATUS,
        AG_PANEL,
        AG_TABS,
        AG_INDICATOR,
        AG_CHOICE
    };
    enum ag_event_kind
    {
        AG_CLICK = 1,
        AG_VALUE_CHANGED,
        AG_FOCUS_LOST,
        AG_CLOSE,
        AG_RESIZE,
        AG_TIMER,
        AG_REFRESH,
        AG_DISPATCH,
        AG_VIEWPORT_CHANGED,
        AG_NAVIGATE,
        AG_TEXT_CHANGED,
        AG_FOCUS_GAINED,
        AG_DROPDOWN_CHANGED,
        AG_DIALOG_ACCEPT, AG_DIALOG_CANCEL,
        AG_BUTTON_PRESS = 16, AG_BUTTON_RELEASE = 17, AG_BUTTON_CANCEL = 18,
        /* Opt-in: accepted index / cancelled opening index. */
        AG_CHOICE_ACCEPTED = 19, AG_CHOICE_CANCELLED = 20,
        AG_SHORTCUT = 1000
    };
    typedef enum ag_control_event_flags
    {
        AG_CONTROL_NAV_TAB = 1,
        AG_CONTROL_NAV_VERTICAL = 2,
        AG_CONTROL_NAV_ACTIVATE = 4,
        AG_CONTROL_NOTIFY_TEXT = 8,
        AG_CONTROL_NOTIFY_FOCUS = 16,
        AG_CONTROL_NOTIFY_DROPDOWN = 32,
        AG_CONTROL_NOTIFY_BUTTON_EDGES = 64,
        /* AG_CHOICE only; existing AG_VALUE_CHANGED previews remain. */
        AG_CONTROL_NOTIFY_CHOICE_DECISION = 128
    } ag_control_event_flags;
    typedef enum ag_navigation
    {
        AG_NAV_TAB_NEXT = 1,
        AG_NAV_TAB_PREVIOUS,
        AG_NAV_UP,
        AG_NAV_DOWN,
        AG_NAV_PAGE_UP,
        AG_NAV_PAGE_DOWN,
        AG_NAV_HOME,
        AG_NAV_END,
        AG_NAV_ENTER,
        AG_NAV_ESCAPE
    } ag_navigation;
    typedef enum ag_control_activity_flags
    {
        AG_ACTIVITY_FOCUSED = 1,
        AG_ACTIVITY_DROPDOWN = 2
    } ag_control_activity_flags;
    enum ag_control_style_flags
    {
        AG_STYLE_FLAT = 1,
        AG_STYLE_SELECTED = 2,
        AG_STYLE_FOLDOUT = 4,
        AG_STYLE_EXPANDED = 8,
        AG_STYLE_ALIGN_CENTER = 16,
        AG_STYLE_ALIGN_RIGHT = 32
    };
    typedef struct ag_refresh_info
    {
        double monitor_hz, target_hz;
        uint32_t known, high_resolution, suspended, error;
        uint64_t posted, coalesced, callbacks;
    } ag_refresh_info;
    typedef void(__cdecl *ag_event_fn)(void *user, uint32_t id, uint32_t event_kind, double value);
    typedef struct ag_control_desc
    {
        uint32_t kind, id;
        ag_rect bounds;
        const wchar_t *text;
        const wchar_t *tooltip;
        double minimum, maximum, step, value;
    } ag_control_desc;
    enum ag_pattern
    {
        AG_SOLID,
        AG_DASHED,
        AG_LONG_DASH,
        AG_SHORT_DASH,
        AG_DASH_DOT,
        AG_TEN_PIXEL_DASH,
        AG_DOTTED
    };
    typedef struct ag_series
    {
        wchar_t label[64];
        uint32_t color_index, pattern;
        float width;
    } ag_series;
    typedef struct ag_sample
    {
        double age_seconds;
        float values[18];
        uint32_t valid_mask; /* Bit i validates values[i]; NaN/invalid samples break traces. */
    } ag_sample;
    typedef struct ag_chart_panel
    {
        wchar_t title[128], unit[32], horizontal_label[64], annotation[128];
        uint32_t first_sample, sample_count, first_series, series_count, column, flags;
        double time_window;
        float default_scale, maximum_scale, symlog_threshold, height_weight;
    } ag_chart_panel;
    /* chart flags: bit0 symmetric log, bit1 unipolar, bit2 no-signal text,
       bit3 axis arrows, bit4 reverse drawing order. Maximum 18 series per panel. */
    typedef struct ag_metrics
    {
        uint64_t frames, submissions, coalesced;
        double last_render_ms;
        uint32_t pending, device_recreations;
        uint64_t last_cpp_allocations;
    } ag_metrics;
    /* Lifetime / diagnostics. Null destruction is allowed. Other handles must be
       live and of the expected type. Do not free a handle from its own callback. */
    AG_API uint32_t ag_abi_version(void);
    AG_API ag_result ag_last_error(wchar_t *buffer, uint32_t capacity, uint32_t *required);
    AG_API ag_result ag_context_create(ag_handle *out);
    AG_API ag_result ag_destroy(ag_handle object);
    /* Managed finalizer support: queue destruction on the context's UI thread.
       The owner must keep pumping messages until all child releases are processed. */
    AG_API ag_result ag_release(ag_handle object);
    typedef void(__cdecl *ag_released_fn)(void *user);
    AG_API ag_result ag_release_notify(ag_handle object, ag_released_fn callback, void *user);
    /* Theme cannot change while children exist. Returned resources are borrowed:
       HFONTs last until a permitted theme change or context destruction; font
       bytes last until context destruction. Never DeleteObject/free them. */
    AG_API ag_result ag_theme_get(ag_handle context, ag_theme *theme);
    AG_API ag_result ag_theme_set(ag_handle context, const ag_theme *theme);
    AG_API ag_result ag_font_data(ag_handle context, uint32_t bold, const void **data, uint32_t *bytes);
    AG_API ag_result ag_font_create(ag_handle context, float points, uint32_t bold, uint32_t dpi,
                                    void **hfont);
    /* Library windows own their HWND. Child controls/surfaces can instead use an
       existing caller-owned HWND on this context's UI thread. Dimensions are DIPs. */
    AG_API ag_result ag_window_create(ag_handle context, const wchar_t *title, int32_t width, int32_t height,
                                      ag_event_fn callback, void *user, ag_handle *out);
    AG_API ag_result ag_window_show(ag_handle window, uint32_t show);
    AG_API ag_result ag_window_run(ag_handle window, int32_t *exit_code);
    AG_API ag_result ag_window_close(ag_handle window);
    AG_API ag_result ag_window_fit(ag_handle window);
    AG_API ag_result ag_window_work_area(ag_handle window, ag_rect *usable_client);
    AG_API ag_result ag_window_client_size(ag_handle window, uint32_t width, uint32_t height);
    /* Deferred close emits AG_CLOSE and keeps pumping until defer is disabled
       and ag_window_close is called again. Existing default behavior is unchanged. */
    AG_API ag_result ag_window_defer_close(ag_handle window, uint32_t defer);
    AG_API ag_result ag_window_timer(ag_handle window, uint32_t id, uint32_t milliseconds);
    /* 0 disables; positive multiplier in [0.25,4], capped at 1000 update Hz.
       AG_REFRESH is bounded/coalesced and stops while hidden or minimized. */
    AG_API ag_result ag_window_refresh(ag_handle window, double updates_per_refresh);
    AG_API ag_result ag_window_refresh_info(ag_handle window, ag_refresh_info *info);
    /* Post AG_DISPATCH after the current callback; suitable for rebuilding child controls. */
    AG_API ag_result ag_window_post(ag_handle window, uint32_t id);
    AG_API ag_result ag_window_title(ag_handle window, const wchar_t *title);
    AG_API ag_result ag_window_minimum(ag_handle window, uint32_t width, uint32_t height);
    /* Native-control windows only; use surface PNG APIs for DXGI children. */
    AG_API ag_result ag_window_export_png(ag_handle window, const wchar_t *path);
    AG_API ag_result ag_client_bounds(ag_handle object, ag_rect *bounds);
    AG_API ag_result ag_control_enabled(ag_handle control, uint32_t enabled);
    AG_API ag_result ag_control_text_get(ag_handle control, wchar_t *text, uint32_t capacity,
                                         uint32_t *required);
    /* UI-thread layout query for AG_LABEL only, using its actual HWND font and
       native wrapping/prefix rules. width_dips is the available client width.
       Returns text extent in DIPs; no outer row spacing is added. Empty text
       returns {0,0}; a word wider than the constraint may return a wider x.
       Call after text/font/DPI changes, not on the acquisition/render hot path. */
    AG_API ag_result ag_control_text_extent(ag_handle control, float width_dips, ag_point *size_dips);
    AG_API ag_result ag_choice_add(ag_handle control, const wchar_t *text);
    /* Optional native events. Defaults remain unchanged; see ag_control_event_flags.
       Never destroy the source control inside its callback. Library text/value
       setters do not emit AG_TEXT_CHANGED. Open choices retain native keys. */
    AG_API ag_result ag_control_events(ag_handle control, uint32_t flags);
    /* BUTTON_EDGES is opt-in for AG_BUTTON only. Physical mouse/Space emits one
       PRESS followed by RELEASE (normal inside release) or CANCEL. Capture,
       focus, disable, hide/ancestor hide and disposal cancel synchronously.
       value=1 mouse, 2 Space. BM_CLICK emits only the existing AG_CLICK.
       Normal release precedes AG_CLICK; cancellation never implies release.
       No timer/command lease is supplied: clients own their bounded hold logic. */
    AG_API ag_result ag_control_activity(ag_handle control, uint32_t *flags);
    /* Nestable UI-thread repaint batch. Preserve own visibility, including hidden
       controls. Balance calls; do not pump messages during a layout batch. */
    AG_API ag_result ag_control_update_begin(ag_handle control);
    AG_API ag_result ag_control_update_end(ag_handle control);
    /* At most 65535 strings / 16 MiB UTF-16 total. selected=-1 means none.
       Reject an open dropdown before changes. Resource failure can leave an
       empty list; validation failures leave the old contents unchanged. */
    AG_API ag_result ag_choice_replace(ag_handle control, const wchar_t *const *items, uint32_t count,
                                       int32_t selected);
    AG_API ag_result ag_choice_preferred_width(ag_handle control, float *width_dips);

    /* Standard Windows modal file dialogs. mode: 0 open, 1 save, 2 folder,
       4 multi-open. accepted is zero on cancellation. Callback strings are
       borrowed for the callback only; callbacks must not throw. */
    typedef void(__cdecl *ag_file_fn)(void *user, const wchar_t *path);
    AG_API ag_result ag_file_dialog(ag_handle context, void *parent, uint32_t mode, const wchar_t *title,
                                    const wchar_t *filter_name, const wchar_t *filter_pattern,
                                    const wchar_t *default_name, ag_file_fn selected, void *user,
                                    uint32_t *accepted);
    AG_API ag_result ag_message_box(ag_handle context, void *parent, const wchar_t *title,
                                    const wchar_t *text, uint32_t error);
    AG_API ag_result ag_native_window(ag_handle object, void **hwnd);
    AG_API ag_result ag_bounds_set(ag_handle object, ag_rect bounds);
    /* Configure the HWND's application icons with WM_SETICON before creation.
       Tray owns an independent copy (small, then large; class/system fallback)
       retained across Explorer restarts. The caller retains its original icons. */
    AG_API ag_result ag_tray_create(ag_handle window, const wchar_t *tooltip, ag_handle *out);
    /* Opt-in popup: two non-command coloured text segments, followed only by Exit.
       States use the same 0 normal, 1 warning, 2 error, 3 success convention as status. */
    AG_API ag_result ag_tray_status(ag_handle tray, const wchar_t *left_text, uint32_t left_state,
                                    const wchar_t *right_text, uint32_t right_state);
    AG_API ag_result ag_control_style(ag_handle control, uint32_t flags);
    AG_API ag_result ag_control_indeterminate(ag_handle control, uint32_t indeterminate);
    /* Owning UI thread. Window/control text is copied (max 4096 UTF-16 units).
       Tray text is copied into its icon (max 127), retained across TaskbarCreated.
       Validation/update failure preserves the previous tooltip. */
    AG_API ag_result ag_tooltip_set(ag_handle object, const wchar_t *text);
    AG_API ag_result ag_visible(ag_handle object, uint32_t visible);
    AG_API ag_result ag_focus(ag_handle object);
    AG_API ag_result ag_pointer_position(ag_handle object, ag_point *position, uint32_t *inside);
    AG_API ag_result ag_choice_clear(ag_handle control);
    AG_API ag_result ag_choice_open(ag_handle control, uint32_t *open);
    /* Opt-in coalesced posted notification, ahead of ordinary paint delivery.
       Query current visible CONTENT coordinates: {0,scroll,width,scroll+height}. */
    AG_API ag_result ag_panel_viewport_notify(ag_handle panel, uint32_t enabled);
    AG_API ag_result ag_panel_viewport_get(ag_handle panel, ag_rect *content_viewport);
    AG_API ag_result ag_panel_scroll_get(ag_handle panel, float *position);
    AG_API ag_result ag_panel_scroll_set(ag_handle panel, float position);
    AG_API ag_result ag_panel_reveal(ag_handle panel, void *child_window);
    AG_API ag_result ag_single_instance(ag_handle window, const wchar_t *application_id, uint32_t *primary);
    /* Control text is a caption, except sliders where it supplies the unit suffix.
       Reserve 24 additional DIPs below a slider for the value/unit label. */
    AG_API ag_result ag_control_create(ag_handle context, void *parent, const ag_control_desc *desc,
                                       ag_event_fn callback, void *user, ag_handle *out);
    AG_API ag_result ag_control_value_set(ag_handle control, double value);
    AG_API ag_result ag_control_value_get(ag_handle control, double *value);
    AG_API ag_result ag_control_text_set(ag_handle control, const wchar_t *text);
    AG_API ag_result ag_panel_extent(ag_handle panel, float height);
    AG_API ag_result ag_tab_add(ag_handle tabs, const wchar_t *title, ag_handle *page);
    /* Optional equal-width headers filling the client width; owning UI thread only. */
    AG_API ag_result ag_tab_equal_width(ag_handle tabs, uint32_t enabled);
    AG_API ag_result ag_status_submit(ag_handle control, const wchar_t *text, uint32_t state);
    /* status state: 0 normal, 1 stale/warning, 2 unavailable/error, 3 success. */
    /* Bulk 2D snapshots: <=65,536 commands and <=1,048,576 points. One newest
       pending snapshot is retained. PNG dimensions are pixels, coordinates DIPs. */
    AG_API ag_result ag_canvas_create(ag_handle context, void *parent, ag_rect bounds, ag_handle *out);
    /* Optional UI-thread pointer input for a canvas. Positions are canvas DIPs.
       Press captures the pointer; release is emitted only inside the canvas.
       Leaving it, capture/focus loss, disable/hide (including ancestors), or
       replacing the callback cancels a press. Failed capture emits no PRESS.
       One delivered PRESS has exactly one RELEASE or CANCEL. CANCEL uses the
       last observed position. Callbacks must not throw, pump messages, destroy
       source/ancestors, or replace input registration (ERROR_BUSY). Disposal
       synchronously cancels before user state may be freed. Client code retains
       its view/gesture identity from press through release. UI thread only. */
    enum ag_canvas_pointer_kind {AG_CANVAS_PRESS=1,AG_CANVAS_RELEASE=2,AG_CANVAS_CANCEL=3};
    typedef void(__cdecl *ag_canvas_pointer_fn)(void *user,uint32_t kind,ag_point position);
    AG_API ag_result ag_canvas_input(ag_handle canvas,ag_canvas_pointer_fn callback,void *user);
    /* EndDraw-success evidence, not physical-display/occlusion certification.
       Initialize size=sizeof and version=1. ready requires a submitted snapshot
       successfully drawn on the current target, with no newer pending snapshot.
       Resize/reset invalidates readiness until another successful on-window draw.
       PNG export does not make a window ready. UI thread only. */
    typedef struct ag_canvas_render_state {
        uint32_t size, version, ready, pending;
        uint64_t submitted_snapshot, rendered_snapshot, target_generation, successful_frames;
        ag_result last_result;
        uint32_t reserved;
    } ag_canvas_render_state;
    AG_API ag_result ag_canvas_state(ag_handle canvas, ag_canvas_render_state *state);
    /* 0 retains refresh-synchronized D2D presentation; 1 does not wait for
       display refresh. Set on externally paced telemetry canvases. */
    AG_API ag_result ag_canvas_present_mode(ag_handle canvas, uint32_t immediate);
    AG_API ag_result ag_canvas_submit(ag_handle canvas, const ag_draw *commands, uint32_t command_count,
                                      const ag_point *points, uint32_t point_count);
    AG_API ag_result ag_canvas_export_png(ag_handle canvas, const wchar_t *file, uint32_t width,
                                          uint32_t height, float dpi);
    /* Chart labels/units are caller-supplied. Nonempty annotation selects a text
       panel. Samples are ordered by the caller; gaps are not interpolated. */
    AG_API ag_result ag_chart_create(ag_handle context, void *parent, ag_rect bounds, ag_handle *out);
    AG_API ag_result ag_chart_submit(ag_handle chart, const ag_chart_panel *panels, uint32_t panel_count,
                                     const ag_series *series, uint32_t series_count, const ag_sample *samples,
                                     uint32_t sample_count);
    AG_API ag_result ag_chart_export_png(ag_handle chart, const wchar_t *file, uint32_t width,
                                         uint32_t height, float dpi);
    /* Direct3D11 indexed triangle mesh + D2D overlay. Hardware requires FL11.0;
       warp!=0 explicitly selects software rendering. No implicit scene/axis model. */
    AG_API ag_result ag_scene_create(ag_handle context, void *parent, ag_rect bounds, uint32_t warp,
                                     ag_handle *out);
    AG_API ag_result ag_scene_mesh(ag_handle scene, const ag_vertex *vertices, uint32_t vertex_count,
                                   const uint32_t *indices, uint32_t index_count);
    AG_API ag_result ag_scene_camera(ag_handle scene, const ag_camera *camera);
    AG_API ag_result ag_scene_options(ag_handle scene, float blur_pixels, uint32_t wireframe);
    AG_API ag_result ag_scene_overlay(ag_handle scene, const ag_draw *commands, uint32_t count,
                                      const ag_point *points, uint32_t point_count);
    AG_API ag_result ag_scene_export_png(ag_handle scene, const wchar_t *file);
    AG_API ag_result ag_device_reset(ag_handle surface);
    AG_API ag_result ag_surface_metrics(ag_handle surface, ag_metrics *metrics);
    /* Null/empty path means UTF-8 settings.cfg beside the EXE. Missing files keep
       declared defaults; malformed loads do not replace current values. Save
       replaces via a flushed temporary file and preserves the prior file in .bak. */
    AG_API ag_result ag_settings_create(const wchar_t *path, ag_handle *out);
    AG_API ag_result ag_settings_load(ag_handle settings);
    AG_API ag_result ag_settings_save(ag_handle settings);
    AG_API ag_result ag_settings_set(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                     const wchar_t *value);
    AG_API ag_result ag_settings_get(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                     wchar_t *value, uint32_t capacity, uint32_t *required);
    AG_API ag_result ag_settings_set_number(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                            double value);
    AG_API ag_result ag_settings_get_number(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                            double fallback, double minimum, double maximum, double *value);
    AG_API ag_result ag_settings_set_bool(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                          uint32_t value);
    AG_API ag_result ag_settings_get_bool(ag_handle settings, const wchar_t *section, const wchar_t *key,
                                          uint32_t fallback, uint32_t *value);
    AG_API ag_result ag_viewport_project(const ag_viewport *view, double x, double y, ag_point *screen);
    AG_API ag_result ag_viewport_unproject(const ag_viewport *view, ag_point screen, double *x, double *y);
    AG_API ag_result ag_viewport_zoom(ag_viewport *view, ag_point anchor, double factor);
    AG_API int32_t ag_clip_line(ag_rect clip, ag_point *a, ag_point *b);
    AG_API int32_t ag_hit_rect(ag_rect rect, ag_point point);
    AG_API ag_result ag_project_wire(const ag_vec3 *vertices, uint32_t count, float yaw, float pitch,
                                     float scale, ag_point center, ag_point *output);
    AG_API double ag_slider_quantize(double value, double minimum, double maximum, double step);
    AG_API double ag_symlog(double value, double threshold);
#ifdef __cplusplus
}
#endif

#include "display.h"

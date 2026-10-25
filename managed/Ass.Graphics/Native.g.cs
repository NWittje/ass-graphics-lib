// Generated from ass_graphics.h by tools/generate-bindings.ps1. Do not edit.
using System.Runtime.InteropServices;
namespace Ass.Graphics;
public static unsafe class Native {
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_path_create(GraphicsHandle context, PathCommand* commands, uint count, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_canvas_submit_paths(GraphicsHandle canvas, Draw* commands, uint count, Point* points, uint point_count, PathDraw* paths, uint path_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern uint ag_abi_version();
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_last_error(char* buffer, uint capacity, uint* required);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_context_create(nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_destroy(nint @object);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_release(nint @object);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_release_notify(nint @object, nint callback, nint user);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_theme_get(GraphicsHandle context, Theme* theme);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_theme_set(GraphicsHandle context, Theme* theme);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_font_data(GraphicsHandle context, uint bold, nint* data, uint* bytes);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_font_create(GraphicsHandle context, float points, uint bold, uint dpi, nint* hfont);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_create(GraphicsHandle context, char* title, int width, int height, nint callback, nint user, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_show(GraphicsHandle window, uint show);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_run(GraphicsHandle window, int* exit_code);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_close(GraphicsHandle window);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_fit(GraphicsHandle window);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_work_area(GraphicsHandle window, Rect* usable_client);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_client_size(GraphicsHandle window, uint width, uint height);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_defer_close(GraphicsHandle window, uint defer);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_timer(GraphicsHandle window, uint id, uint milliseconds);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_refresh(GraphicsHandle window, double updates_per_refresh);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_refresh_info(GraphicsHandle window, RefreshInfo* info);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_post(GraphicsHandle window, uint id);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_title(GraphicsHandle window, char* title);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_minimum(GraphicsHandle window, uint width, uint height);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_export_png(GraphicsHandle window, char* path);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_client_bounds(GraphicsHandle @object, Rect* bounds);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_enabled(GraphicsHandle control, uint enabled);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_text_get(GraphicsHandle control, char* text, uint capacity, uint* required);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_text_extent(GraphicsHandle control, float width_dips, Point* size_dips);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_choice_add(GraphicsHandle control, char* text);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_events(GraphicsHandle control, uint flags);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_activity(GraphicsHandle control, uint* flags);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_update_begin(GraphicsHandle control);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_update_end(GraphicsHandle control);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_choice_replace(GraphicsHandle control, char** items, uint count, int selected);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_choice_preferred_width(GraphicsHandle control, float* width_dips);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_file_dialog(GraphicsHandle context, nint parent, uint mode, char* title, char* filter_name, char* filter_pattern, char* default_name, nint selected, nint user, uint* accepted);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_message_box(GraphicsHandle context, nint parent, char* title, char* text, uint error);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_native_window(GraphicsHandle @object, nint* hwnd);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_bounds_set(GraphicsHandle @object, Rect bounds);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_tray_create(GraphicsHandle window, char* tooltip, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_tray_status(GraphicsHandle tray, char* left_text, uint left_state, char* right_text, uint right_state);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_style(GraphicsHandle control, uint flags);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_indeterminate(GraphicsHandle control, uint indeterminate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_tooltip_set(GraphicsHandle @object, char* text);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_visible(GraphicsHandle @object, uint visible);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_focus(GraphicsHandle @object);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_pointer_position(GraphicsHandle @object, Point* position, uint* inside);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_choice_clear(GraphicsHandle control);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_choice_open(GraphicsHandle control, uint* open);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_viewport_notify(GraphicsHandle panel, uint enabled);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_viewport_get(GraphicsHandle panel, Rect* content_viewport);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_scroll_get(GraphicsHandle panel, float* position);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_scroll_set(GraphicsHandle panel, float position);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_reveal(GraphicsHandle panel, nint child_window);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_single_instance(GraphicsHandle window, char* application_id, uint* primary);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_create(GraphicsHandle context, nint parent, ControlDesc* desc, nint callback, nint user, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_value_set(GraphicsHandle control, double value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_value_get(GraphicsHandle control, double* value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_control_text_set(GraphicsHandle control, char* text);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_panel_extent(GraphicsHandle panel, float height);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_tab_add(GraphicsHandle tabs, char* title, nint* page);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_tab_equal_width(GraphicsHandle tabs, uint enabled);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_status_submit(GraphicsHandle control, char* text, uint state);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_canvas_create(GraphicsHandle context, nint parent, Rect bounds, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_canvas_present_mode(GraphicsHandle canvas, uint immediate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_canvas_submit(GraphicsHandle canvas, Draw* commands, uint command_count, Point* points, uint point_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_canvas_export_png(GraphicsHandle canvas, char* file, uint width, uint height, float dpi);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_chart_create(GraphicsHandle context, nint parent, Rect bounds, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_chart_submit(GraphicsHandle chart, ChartPanel* panels, uint panel_count, Series* series, uint series_count, Sample* samples, uint sample_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_chart_export_png(GraphicsHandle chart, char* file, uint width, uint height, float dpi);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_create(GraphicsHandle context, nint parent, Rect bounds, uint warp, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_mesh(GraphicsHandle scene, Vertex* vertices, uint vertex_count, uint* indices, uint index_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_camera(GraphicsHandle scene, Camera* camera);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_options(GraphicsHandle scene, float blur_pixels, uint wireframe);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_overlay(GraphicsHandle scene, Draw* commands, uint count, Point* points, uint point_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_scene_export_png(GraphicsHandle scene, char* file);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_device_reset(GraphicsHandle surface);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_surface_metrics(GraphicsHandle surface, Metrics* metrics);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_create(char* path, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_load(GraphicsHandle settings);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_save(GraphicsHandle settings);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_set(GraphicsHandle settings, char* section, char* key, char* value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_get(GraphicsHandle settings, char* section, char* key, char* value, uint capacity, uint* required);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_set_number(GraphicsHandle settings, char* section, char* key, double value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_get_number(GraphicsHandle settings, char* section, char* key, double fallback, double minimum, double maximum, double* value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_set_bool(GraphicsHandle settings, char* section, char* key, uint value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_settings_get_bool(GraphicsHandle settings, char* section, char* key, uint fallback, uint* value);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_viewport_project(Viewport* view, double x, double y, Point* screen);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_viewport_unproject(Viewport* view, Point screen, double* x, double* y);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_viewport_zoom(Viewport* view, Point anchor, double factor);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_clip_line(Rect clip, Point* a, Point* b);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_hit_rect(Rect rect, Point point);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_project_wire(Vector3* vertices, uint count, float yaw, float pitch, float scale, Point center, Point* output);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern double ag_slider_quantize(double value, double minimum, double maximum, double step);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern double ag_symlog(double value, double threshold);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_create(GraphicsHandle context, nint parent, Rect bounds, DisplayDesc* desc, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_configure(GraphicsHandle display, DisplayDesc* desc);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_output_size(GraphicsHandle display, uint width, uint height);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_submit(GraphicsHandle display, Draw* commands, uint count, Point* points, uint point_count, PathDraw* paths, uint path_count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_effects(GraphicsHandle display, EffectGraph* graph);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_parameters(GraphicsHandle display, uint pass, nint data, uint bytes);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_render(GraphicsHandle display, double elapsed_seconds);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_export_png(GraphicsHandle display, char* path);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_metrics_get(GraphicsHandle display, DisplayMetrics* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_capabilities(GraphicsHandle display, DisplayCaps* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_prepare(GraphicsHandle display, DisplayDesc* desc, EffectGraph* graph, nint* candidate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_candidate_constants(GraphicsHandle candidate, uint pass, uint slot, nint data, uint bytes, ConstantBinding* bindings, uint count);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_commit(GraphicsHandle display, GraphicsHandle candidate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_abort(GraphicsHandle candidate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_constants(GraphicsHandle display, uint pass, uint slot, nint data, uint bytes);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_display_history_clear(GraphicsHandle display);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_shortcut(GraphicsHandle window, uint key, uint modifiers, uint id);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_context_requests(GraphicsHandle @object, nint callback, nint user);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_dialog_create(GraphicsHandle owner, char* title, uint width, uint height, nint callback, nint user, nint* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_dialog_show(GraphicsHandle dialog, uint visible, uint activate);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_monitors(GraphicsHandle context, nint callback, nint user);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_placement_get(GraphicsHandle window, WindowPlacement* @out);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_placement_set(GraphicsHandle window, WindowPlacement* placement);
 [DllImport("ass_graphics_lib", ExactSpelling=true, CallingConvention=CallingConvention.Cdecl)]
 public static extern int ag_window_borderless(GraphicsHandle window, uint enabled, char* monitor);
}

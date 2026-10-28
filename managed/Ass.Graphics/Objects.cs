using System.Runtime.InteropServices;
namespace Ass.Graphics;
public static unsafe class GraphicsError
{
    public static void Check(int hr)
    {
        if (hr >= 0)
            return;
        char *buffer = stackalloc char[1024];
        buffer[0] = '\0';
        Native.ag_last_error(buffer, 1024, null);
        throw new GraphicsException(new string(buffer), hr);
    }
}
public sealed class GraphicsException(string message, int result) : Exception
(message)
{
    public int Result { get; } = result;
}
public sealed class GraphicsHandle : SafeHandle
{
    private readonly GraphicsHandle? owner;
    private bool ownerHeld;
    internal object? CallbackRoot;
    internal object? ContextCallbackRoot;
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void Released(nint user);
    private sealed record ReleaseState(GraphicsHandle? Owner, object? Callback, object? ContextCallback);
    private static readonly Released released = user =>
    {
        var root = GCHandle.FromIntPtr(user);
        var state = (ReleaseState)root.Target!;
        try
        {
            state.Owner?.DangerousRelease();
            GC.KeepAlive(state.Callback);
            GC.KeepAlive(state.ContextCallback);
        }
        finally
        {
            root.Free();
        }
    };
    internal GraphicsHandle(nint value, GraphicsHandle? parent = null) : base(0, true)
    {
        owner = parent;
        owner?.DangerousAddRef(ref ownerHeld);
        SetHandle(value);
    }
    public override bool IsInvalid => handle == 0;
    protected override bool ReleaseHandle()
    {
        var root = GCHandle.Alloc(new ReleaseState(ownerHeld ? owner : null, CallbackRoot,ContextCallbackRoot));
        int result = Native.ag_release_notify(handle, Marshal.GetFunctionPointerForDelegate(released),
                                              GCHandle.ToIntPtr(root));
        if (result < 0)
            root.Free();
        else
            ownerHeld = false;
        return result >= 0;
    }
}
public abstract class GraphicsObject : IDisposable
{
    public GraphicsHandle Handle { get; protected set; } = null!;
    // SafeHandle parameters make the P/Invoke marshaler retain the object for
    // the complete native call, including callbacks and blocking message loops.
    internal GraphicsHandle Raw => H;
    protected GraphicsHandle H =>
        Handle.IsClosed ? throw new ObjectDisposedException(GetType().Name) : Handle;
    public unsafe nint WindowHandle
    {
        get {
            nint h;
            GraphicsError.Check(Native.ag_native_window(H, &h));
            return h;
        }
    }
    public void SetBounds(Rect bounds) => GraphicsError.Check(Native.ag_bounds_set(H, bounds));
    public virtual void Dispose()
    {
        Handle.Dispose();
        GC.SuppressFinalize(this);
    }
}
/// <summary>UI-thread context owning native factories and the selected external B612 file bytes.</summary>
public sealed unsafe class GraphicsContext : GraphicsObject
{
    public GraphicsContext()
    {
        if (Native.ag_abi_version() != 1)
            throw new NotSupportedException("Native ABI mismatch");
        nint h;
        GraphicsError.Check(Native.ag_context_create(&h));
        Handle = new(h);
    }
    public Theme Theme
    {
        get {
            Theme t;
            GraphicsError.Check(Native.ag_theme_get(H, &t));
            return t;
        }
        set {
            GraphicsError.Check(Native.ag_theme_set(H, &value));
        }
    }
    public (nint Data, uint Length) FontData(bool bold = false)
    {
        nint data;
        uint length;
        GraphicsError.Check(Native.ag_font_data(H, bold ? 1u : 0u, &data, &length));
        return (data, length);
    }
}
[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void EventCallback(nint user, uint id, EventKind kind, double value);
/// <summary>Native application window with optional message loop and single-instance activation.</summary>
public sealed unsafe class Window : GraphicsObject
{
    private readonly EventCallback callback;
    public event Action<uint, EventKind, double>? Event;
    public Exception? LastCallbackError { get; private set; }
    public Window(GraphicsContext context, string title, int width = 1000, int height = 700)
    {
        callback = (_, id, kind, value) =>
        {
            try
            {
                Event?.Invoke(id, kind, value);
            }
            catch (Exception e)
            {
                LastCallbackError = e;
            }
        };
        nint h;
        fixed(char *text = title) GraphicsError.Check(Native.ag_window_create(
            context.Raw, text, width, height, Marshal.GetFunctionPointerForDelegate(callback), 0, &h));
        Handle = new(h, context.Handle);
        Handle.CallbackRoot = callback;
    }
    public void Show(bool show = true) => GraphicsError.Check(Native.ag_window_show(H, show? 1u: 0));
    public void Close() => GraphicsError.Check(Native.ag_window_close(H));
    public int Run()
    {
        int code;
        GraphicsError.Check(Native.ag_window_run(H, &code));
        GC.KeepAlive(callback);
        return code;
    }
    public void Fit() => GraphicsError.Check(Native.ag_window_fit(H));
    public bool SingleInstance(string id)
    {
        uint primary;
        fixed(char *s = id) GraphicsError.Check(Native.ag_single_instance(H, s, &primary));
        return primary != 0;
    }
}
public sealed unsafe class Tray : GraphicsObject
{
    public Tray(Window window, string tooltip)
    {
        nint h;
        fixed(char *text = tooltip) GraphicsError.Check(Native.ag_tray_create(window.Raw, text, &h));
        Handle = new(h, window.Handle);
    }
}
/// <summary>Native settings/readout control; SubmitStatus accepts bounded worker updates.</summary>
public sealed unsafe class NativeControl : GraphicsObject
{
    private readonly EventCallback? callback;
    public event Action<uint, EventKind, double>? Event;
    public Exception? LastCallbackError { get; private set; }
    public NativeControl(GraphicsContext context, nint parent, ControlKind kind, Rect bounds,
                         string text = "", uint id = 0, string tooltip = "", double minimum = 0,
                         double maximum = 1, double step = .01, double value = 0)
    {
        callback = (_, i, k, v) =>
        {
            try
            {
                Event?.Invoke(i, k, v);
            }
            catch (Exception e)
            {
                LastCallbackError = e;
            }
        };
        nint h;
        fixed(char *t = text, tip = tooltip)
        {
            ControlDesc d =
                new() { Kind = kind,       Id = id,           Bounds = bounds, Text = t,     Tooltip = tip,
                        Minimum = minimum, Maximum = maximum, Step = step,     Value = value };
            GraphicsError.Check(Native.ag_control_create(
                context.Raw, parent, &d, Marshal.GetFunctionPointerForDelegate(callback), 0, &h));
        }
        Handle = new(h, context.Handle);
        Handle.CallbackRoot = callback;
    }
    private NativeControl(nint handle, GraphicsHandle owner)
    {
        Handle = new(handle, owner);
    }
    public double Value
    {
        get {
            double v;
            GraphicsError.Check(Native.ag_control_value_get(H, &v));
            return v;
        }
        set => GraphicsError.Check(Native.ag_control_value_set(H, value));
    }
    public void SetText(string text)
    {
        fixed(char *s = text) GraphicsError.Check(Native.ag_control_text_set(H, s));
    }
    public void SubmitStatus(string text, StatusState state = StatusState.Normal)
    {
        fixed(char *s = text) GraphicsError.Check(Native.ag_status_submit(H, s, (uint)state));
    }
    public void SetExtent(float height) => GraphicsError.Check(Native.ag_panel_extent(H, height));
    public NativeControl AddTab(string name)
    {
        nint h;
        fixed(char *s = name) GraphicsError.Check(Native.ag_tab_add(H, s, &h));
        return new(h, Handle);
    }
    public void SetEqualTabWidth(bool enabled) => GraphicsError.Check(Native.ag_tab_equal_width(H, enabled ? 1u : 0u));
    /// <summary>Opt-in UI-thread events. Button edges do not renew an application command lease.</summary>
    public void SetEvents(ControlEventFlags flags) => GraphicsError.Check(Native.ag_control_events(H, (uint)flags));
}
public abstract unsafe class Surface : GraphicsObject
{
    public Metrics Metrics
    {
        get {
            Metrics m;
            GraphicsError.Check(Native.ag_surface_metrics(H, &m));
            return m;
        }
    }
    public void ResetDevice() => GraphicsError.Check(Native.ag_device_reset(H));
}
/// <summary>Bulk native Direct2D drawing; submit complete snapshots instead of individual primitives.</summary>
public sealed unsafe class Canvas : Surface
{
    public Canvas(GraphicsContext context, nint parent, Rect bounds)
    {
        nint h;
        GraphicsError.Check(Native.ag_canvas_create(context.Raw, parent, bounds, &h));
        Handle = new(h, context.Handle);
    }
    public void Submit(ReadOnlySpan<Draw> commands, ReadOnlySpan<Point> points = default)
    {
        fixed(Draw *c = commands) fixed(Point *p = points)
            GraphicsError.Check(Native.ag_canvas_submit(H, c, (uint)commands.Length, p, (uint)points.Length));
    }
    public void ExportPng(string path, uint width, uint height, float dpi = 96)
    {
        fixed(char *p = path) GraphicsError.Check(Native.ag_canvas_export_png(H, p, width, height, dpi));
    }
}
/// <summary>Native time-history charts with units, patterns, scaling and explicit validity gaps.</summary>
public sealed unsafe class Chart : Surface
{
    public void ExportPng(string path, uint width, uint height, float dpi = 96)
    {
        fixed(char *p = path) GraphicsError.Check(Native.ag_chart_export_png(H, p, width, height, dpi));
    }
    public Chart(GraphicsContext context, nint parent, Rect bounds)
    {
        nint h;
        GraphicsError.Check(Native.ag_chart_create(context.Raw, parent, bounds, &h));
        Handle = new(h, context.Handle);
    }
    public void Submit(ReadOnlySpan<ChartPanel> panels, ReadOnlySpan<Series> series,
                       ReadOnlySpan<Sample> samples)
    {
        fixed(ChartPanel *p = panels) fixed(Series *s = series) fixed(Sample *v = samples)
            GraphicsError.Check(Native.ag_chart_submit(H, p, (uint)panels.Length, s, (uint)series.Length, v,
                                                       (uint)samples.Length));
    }
}
/// <summary>UI-thread D3D11 mesh renderer with camera and a shared Direct2D overlay.</summary>
public sealed unsafe class Scene : Surface
{
    public Scene(GraphicsContext context, nint parent, Rect bounds, bool warp = false)
    {
        nint h;
        GraphicsError.Check(Native.ag_scene_create(context.Raw, parent, bounds, warp ? 1u : 0, &h));
        Handle = new(h, context.Handle);
    }
    public void Mesh(ReadOnlySpan<Vertex> vertices, ReadOnlySpan<uint> indices)
    {
        fixed(Vertex *v = vertices) fixed(uint *i = indices)
            GraphicsError.Check(Native.ag_scene_mesh(H, v, (uint)vertices.Length, i, (uint)indices.Length));
    }
    public void ExportPng(string path)
    {
        fixed(char *p = path) GraphicsError.Check(Native.ag_scene_export_png(H, p));
    }
    public void SetCamera(Camera camera) => GraphicsError.Check(Native.ag_scene_camera(H, &camera));
    public void Options(float blurPixels = 0, bool wireframe = false) =>
        GraphicsError.Check(Native.ag_scene_options(H, blurPixels, wireframe? 1u: 0));
    public void Overlay(ReadOnlySpan<Draw> commands, ReadOnlySpan<Point> points = default)
    {
        fixed(Draw *c = commands) fixed(Point *p = points)
            GraphicsError.Check(Native.ag_scene_overlay(H, c, (uint)commands.Length, p, (uint)points.Length));
    }
}
/// <summary>Typed UTF-8 settings with explicit diagnostics and replacement/backup on save.</summary>
public sealed unsafe class Settings : GraphicsObject
{
    public Settings(string? path = null)
    {
        nint h;
        fixed(char *p = path) GraphicsError.Check(Native.ag_settings_create(p, &h));
        Handle = new(h);
    }
    public void Load() => GraphicsError.Check(Native.ag_settings_load(H));
    public void Save() => GraphicsError.Check(Native.ag_settings_save(H));
    public void Set(string section, string key, string value)
    {
        fixed(char *s = section, k = key, v = value) GraphicsError.Check(Native.ag_settings_set(H, s, k, v));
    }
    public string Get(string section, string key)
    {
        fixed(char *s = section, k = key)
        {
            uint count;
            GraphicsError.Check(Native.ag_settings_get(H, s, k, null, 0, &count));
            char[] buffer = new char[count];
            fixed(char *v = buffer)
            {
                GraphicsError.Check(Native.ag_settings_get(H, s, k, v, count, &count));
                return new string(v);
            }
        }
    }
    public void SetNumber(string section, string key, double value)
    {
        fixed(char *s = section, k = key) GraphicsError.Check(Native.ag_settings_set_number(H, s, k, value));
    }
    public double GetNumber(string section, string key, double fallback, double minimum, double maximum)
    {
        double value;
        fixed(char *s = section, k = key)
            GraphicsError.Check(Native.ag_settings_get_number(H, s, k, fallback, minimum, maximum, &value));
        return value;
    }
    public void SetBoolean(string section, string key, bool value)
    {
        fixed(char *s = section, k = key)
            GraphicsError.Check(Native.ag_settings_set_bool(H, s, k, value ? 1u : 0));
    }
    public bool GetBoolean(string section, string key, bool fallback = false)
    {
        uint value;
        fixed(char *s = section, k = key)
            GraphicsError.Check(Native.ag_settings_get_bool(H, s, k, fallback ? 1u : 0, &value));
        return value != 0;
    }
}


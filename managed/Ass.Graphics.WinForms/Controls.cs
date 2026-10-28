using System.Drawing.Text;
using Forms = System.Windows.Forms;
namespace Ass.Graphics.WinForms;
public sealed class ApplicationTheme : IDisposable
{
    private readonly PrivateFontCollection fonts = new();
    private readonly List<nint> fontMemory = new();
    private readonly GraphicsContext context;
    public System.Drawing.Font Regular { get; }
    public System.Drawing.Font Bold { get; }
    public ApplicationTheme(GraphicsContext context, float points = 10)
    {
        this.context = context;
        try
        {
            foreach (bool bold in new[] { false, true })
            {
                var data = context.FontData(bold);
                int length = checked((int)data.Length);
                nint memory = System.Runtime.InteropServices.Marshal.AllocHGlobal(length);
                try { fontMemory.Add(memory); }
                catch { System.Runtime.InteropServices.Marshal.FreeHGlobal(memory); throw; }
                // GDI+ may retain the pointer. Own a copy of the already validated
                // external bytes so disposing the native Context cannot invalidate it.
                unsafe { Buffer.MemoryCopy(data.Data.ToPointer(), memory.ToPointer(), length, length); }
                fonts.AddMemoryFont(memory, length);
            }
            Regular = new(fonts.Families[0], points, System.Drawing.FontStyle.Regular);
            Bold = new(fonts.Families[0], points, System.Drawing.FontStyle.Bold);
        }
        catch
        {
            Regular?.Dispose();
            Bold?.Dispose();
            fonts.Dispose();
            foreach (nint memory in fontMemory)
                System.Runtime.InteropServices.Marshal.FreeHGlobal(memory);
            fontMemory.Clear();
            throw;
        }
    }
    public void Apply(Forms.Control control)
    {
        control.Font = Regular;
        foreach (Forms.Control child in control.Controls)
            Apply(child);
    }
    public void Dispose()
    {
        Regular.Dispose();
        Bold.Dispose();
        fonts.Dispose();
        foreach (nint memory in fontMemory)
            System.Runtime.InteropServices.Marshal.FreeHGlobal(memory);
        fontMemory.Clear();
        GC.KeepAlive(context);
    }
}
public abstract class SurfaceControl : Forms.Control
{
    protected GraphicsObject? NativeObject;
    public GraphicsContext Context { get; }
    protected SurfaceControl(GraphicsContext context)
    {
        Context = context;
        SetStyle(Forms.ControlStyles.Opaque | Forms.ControlStyles.UserPaint, true);
    }
    protected virtual Rect NativeBounds => new(0, 0, ClientSize.Width * 96f / DeviceDpi,
                                               ClientSize.Height * 96f / DeviceDpi);
    protected abstract GraphicsObject CreateNative();
    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        NativeObject = CreateNative();
    }
    protected override void OnHandleDestroyed(EventArgs e)
    {
        NativeObject?.Dispose();
        NativeObject = null;
        base.OnHandleDestroyed(e);
    }
    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        NativeObject?.SetBounds(NativeBounds);
    }
    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        NativeObject?.SetBounds(NativeBounds);
    }
    protected override void OnPaintBackground(Forms.PaintEventArgs e) {}
    protected override void OnPaint(Forms.PaintEventArgs e) {}
    public Metrics Metrics => (NativeObject as Surface)?.Metrics ?? default;
}
public sealed class CanvasControl : SurfaceControl
{
    private Draw[] commands = [];
    private Point[] points = [];
    public CanvasControl(GraphicsContext context) : base(context) {}
    protected override GraphicsObject CreateNative()
    {
        var canvas = new Canvas(Context, Handle, NativeBounds);
        try { canvas.Submit(commands, points); return canvas; }
        catch { canvas.Dispose(); throw; }
    }
    public void Submit(ReadOnlySpan<Draw> commands, ReadOnlySpan<Point> points = default)
    {
        var nextCommands = commands.ToArray();
        var nextPoints = points.ToArray();
        (NativeObject as Canvas)?.Submit(nextCommands, nextPoints);
        this.commands = nextCommands;
        this.points = nextPoints;
    }
    public void ExportPng(string path, uint width, uint height, float dpi = 96)
    {
        if (NativeObject is not Canvas canvas)
            throw new InvalidOperationException("Create the control handle first");
        canvas.ExportPng(path, width, height, dpi);
    }
}
public sealed class ChartControl : SurfaceControl
{
    private ChartPanel[] panels = [];
    private Series[] series = [];
    private Sample[] samples = [];
    public ChartControl(GraphicsContext context) : base(context) {}
    protected override GraphicsObject CreateNative()
    {
        var chart = new Chart(Context, Handle, NativeBounds);
        try { chart.Submit(panels, series, samples); return chart; }
        catch { chart.Dispose(); throw; }
    }
    public void Submit(ReadOnlySpan<ChartPanel> panels, ReadOnlySpan<Series> series,
                       ReadOnlySpan<Sample> samples)
    {
        var nextPanels = panels.ToArray();
        var nextSeries = series.ToArray();
        var nextSamples = samples.ToArray();
        (NativeObject as Chart)?.Submit(nextPanels, nextSeries, nextSamples);
        this.panels = nextPanels;
        this.series = nextSeries;
        this.samples = nextSamples;
    }
}
public sealed class SceneControl : SurfaceControl
{
    private Vertex[] vertices = [];
    private uint[] indices = [];
    private Camera? camera;
    private float blur;
    private bool wireframe;
    private Draw[] overlay = [];
    private Point[] overlayPoints = [];
    public bool UseWarp { get; }
    public SceneControl(GraphicsContext context, bool warp = false) : base(context)
    {
        UseWarp = warp;
    }
    protected override GraphicsObject CreateNative()
    {
        var scene = new Scene(Context, Handle, NativeBounds, UseWarp);
        try
        {
            scene.Mesh(vertices, indices);
            if (camera is Camera savedCamera) scene.SetCamera(savedCamera);
            scene.Options(blur, wireframe);
            scene.Overlay(overlay, overlayPoints);
            return scene;
        }
        catch { scene.Dispose(); throw; }
    }
    public void SetMesh(ReadOnlySpan<Vertex> vertices, ReadOnlySpan<uint> indices)
    {
        var nextVertices = vertices.ToArray();
        var nextIndices = indices.ToArray();
        (NativeObject as Scene)?.Mesh(nextVertices, nextIndices);
        this.vertices = nextVertices;
        this.indices = nextIndices;
    }
    public void SetCamera(Camera camera)
    {
        if (NativeObject is Scene scene)
            scene.SetCamera(camera);
        this.camera = camera;
    }
    public void Options(float blur = 0, bool wire = false)
    {
        (NativeObject as Scene)?.Options(blur, wire);
        this.blur = blur;
        wireframe = wire;
    }
    public void Overlay(ReadOnlySpan<Draw> commands, ReadOnlySpan<Point> points = default)
    {
        var nextCommands = commands.ToArray();
        var nextPoints = points.ToArray();
        (NativeObject as Scene)?.Overlay(nextCommands, nextPoints);
        overlay = nextCommands;
        overlayPoints = nextPoints;
    }
    public void ExportPng(string path)
    {
        if (NativeObject is not Scene scene) throw new InvalidOperationException("Create the control handle first");
        scene.ExportPng(path);
    }
}
public class NativeControlHost : SurfaceControl
{
    private double? savedValue;
    protected override Rect NativeBounds =>
        new(0, 0, ClientSize.Width * 96f / DeviceDpi,
            Math.Max(1, ClientSize.Height * 96f / DeviceDpi - (Kind == ControlKind.Slider ? 24 : 0)));
    public ControlKind Kind { get; }
    public double Minimum { get; }
    public double Maximum { get; }
    public double Step { get; }
    public event Action<EventKind, double>? ValueEvent;
    public NativeControlHost(GraphicsContext context, ControlKind kind, double minimum = 0,
                             double maximum = 1, double step = .01)
        : base(context)
    {
        Kind = kind;
        Minimum = minimum;
        Maximum = maximum;
        Step = step;
        TabStop = true;
    }
    protected override GraphicsObject CreateNative()
    {
        var control = new NativeControl(Context, Handle, Kind, NativeBounds, Text, minimum: Minimum,
                                        maximum: Maximum, step: Step, value: savedValue ?? Minimum);
        control.Event += (_, kind, value) =>
        {
            if (kind == EventKind.ValueChanged || (kind == EventKind.Click && Kind == ControlKind.Checkbox))
                savedValue = value;
            ValueEvent?.Invoke(kind, value);
        };
        return control;
    }
    [System.ComponentModel.DesignerSerializationVisibility(
        System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    public double Value
    {
        get => (NativeObject as NativeControl)?.Value ?? savedValue ?? Minimum;
        set {
            if (NativeObject is NativeControl control)
                control.Value = value;
            savedValue = value;
        }
    }
    protected override void OnTextChanged(EventArgs e)
    {
        base.OnTextChanged(e);
        (NativeObject as NativeControl)?.SetText(Text);
    }
    protected override void OnGotFocus(EventArgs e)
    {
        base.OnGotFocus(e);
        if (NativeObject is not null)
            NativeFocus.SetFocus(NativeObject.WindowHandle);
    }
    private static class NativeFocus
    {
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        public static extern nint SetFocus(nint hwnd);
    }
}
public sealed class StatusTextBox : NativeControlHost
{
    private string latest = "";
    private StatusState state;
    public StatusTextBox(GraphicsContext context) : base(context, ControlKind.Status) {}
    protected override GraphicsObject CreateNative()
    {
        var control = (NativeControl)base.CreateNative();
        control.SubmitStatus(latest, state);
        return control;
    }
    public void Submit(string text, StatusState state = StatusState.Normal)
    {
        (NativeObject as NativeControl)?.SubmitStatus(text, state);
        latest = text;
        this.state = state;
    }
}
// Uses the native tray/window behavior; WinForms supplies the hosting message loop.
public sealed class TrayApplicationContext : Forms.ApplicationContext
{
    public Window Window { get; }
    private readonly Tray tray;
    public TrayApplicationContext(GraphicsContext context, string title)
    {
        Window = new(context, title);
        tray = new(Window, title);
        Window.Event += (_, kind, _) =>
        {
            if (kind == EventKind.Close)
                ExitThread();
        };
        Window.Show();
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            tray.Dispose();
            Window.Dispose();
        }
        base.Dispose(disposing);
    }
}

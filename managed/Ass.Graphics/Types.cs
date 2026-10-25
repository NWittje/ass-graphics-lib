using System.Runtime.InteropServices;
namespace Ass.Graphics;
[StructLayout(LayoutKind.Sequential)]
public struct Point(float x, float y)
{
    public float X = x, Y = y;
}
[StructLayout(LayoutKind.Sequential)]
public struct Rect(float left, float top, float right, float bottom)
{
    public float Left = left, Top = top, Right = right, Bottom = bottom;
}
[StructLayout(LayoutKind.Sequential)]
public struct Color(float r, float g, float b, float a = 1)
{
    public float R = r, G = g, B = b, A = a;
}
[StructLayout(LayoutKind.Sequential)]
public struct Vector3(float x, float y, float z)
{
    public float X = x, Y = y, Z = z;
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct Theme
{
    public Color Background, Foreground, Accent, Muted, Error, Warning, Success;
    public float FontSize, Spacing;
    public fixed char FontFamily[64];
}
[StructLayout(LayoutKind.Sequential)]
public struct Viewport
{
    public double Left, Top, Right, Bottom;
    public Rect Screen;
}
[StructLayout(LayoutKind.Sequential)]
public struct Camera
{
    public Vector3 Eye, Target, Up;
    public float FovY, Near, Far;
}
[StructLayout(LayoutKind.Sequential)]
public struct Vertex
{
    public Vector3 Position, Normal;
    public Color Color;
}
public enum DrawKind : uint
{
    Line = 1,
    Rectangle,
    FillRectangle,
    Ellipse,
    FillEllipse,
    Text,
    Polyline,
    Polygon,
    PushClip,
    PopClip,
    Transform,
    Image,
    Segments,
    Path
}
public enum PathKind : uint { Move=1, Line, Quadratic, Cubic, Close }
public enum PathCap : uint { Flat, Round, Square }
public enum PathJoin : uint { Miter, Round, Bevel }
[StructLayout(LayoutKind.Sequential)]
public struct PathCommand { public PathKind Kind; public Point P1, P2, P3; }
[StructLayout(LayoutKind.Sequential)]
public struct PathDraw { public nint Path; public uint Fill; public PathCap Cap; public PathJoin Join; }
public enum ControlKind : uint
{
    Label = 1,
    Button,
    Checkbox,
    Edit,
    Numeric,
    Slider,
    Status,
    Panel,
    Tabs,
    Indicator,
    Choice
}
public enum EventKind : uint
{
    Click = 1,
    ValueChanged,
    FocusLost,
    Close,
    Resize,
    Timer,
    Refresh,
    Dispatch, ViewportChanged, Navigate, TextChanged, FocusGained, DropdownChanged, DialogAccept, DialogCancel,
    ButtonPress=16, ButtonRelease=17, ButtonCancel=18, ChoiceAccepted=19, ChoiceCancelled=20, Shortcut=1000
}
[Flags]
public enum ControlEventFlags : uint
{
    None=0, NavigateTab=1, NavigateVertical=2, NavigateActivate=4,
    NotifyText=8, NotifyFocus=16, NotifyDropdown=32, NotifyButtonEdges=64, NotifyChoiceDecision=128
}
public enum LinePattern : uint
{
    Solid,
    Dashed,
    LongDash,
    ShortDash,
    DashDot,
    TenPixelDash,
    Dotted
}
public enum StatusState : uint
{
    Normal,
    Stale,
    Unavailable,
    Success
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct Draw
{
    public DrawKind Kind;
    public uint Flags;
    public Rect Bounds;
    public Color Color;
    public float Width, FontSize;
    public uint FirstPoint, PointCount;
    public fixed float Matrix[6];
    public fixed char Text[256];
    public void SetText(string value)
    {
        if (value.Length > 255)
            throw new ArgumentOutOfRangeException(nameof(value));
        fixed(char *p = Text)
        {
            value.AsSpan().CopyTo(new Span<char>(p, 256));
            p[value.Length] = '\0';
        }
    }
    public static Draw Label(string text, Rect bounds, Color color, float size = 0)
    {
        Draw d = new() { Kind = DrawKind.Text, Bounds = bounds, Color = color, FontSize = size };
        d.SetText(text);
        return d;
    }
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ControlDesc
{
    public ControlKind Kind;
    public uint Id;
    public Rect Bounds;
    public char *Text;
    public char *Tooltip;
    public double Minimum, Maximum, Step, Value;
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct Series
{
    public fixed char Label[64];
    public uint ColorIndex;
    public LinePattern Pattern;
    public float Width;
    public void SetLabel(string label)
    {
        if (label.Length > 63)
            throw new ArgumentOutOfRangeException(nameof(label));
        fixed(char *p = Label)
        {
            label.AsSpan().CopyTo(new Span<char>(p, 64));
            p[label.Length] = '\0';
        }
    }
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct Sample
{
    public double AgeSeconds;
    public fixed float Values[18];
    public uint ValidMask;
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ChartPanel
{
    public fixed char Title[128];
    public fixed char Unit[32];
    public fixed char HorizontalLabel[64];
    public fixed char Annotation[128];
    public uint FirstSample, SampleCount, FirstSeries, SeriesCount, Column, Flags;
    public double TimeWindow;
    public float DefaultScale, MaximumScale, SymlogThreshold, HeightWeight;
    public static ChartPanel Create(string title, uint samples, uint series, double seconds = 15)
    {
        if (title.Length > 127)
            throw new ArgumentOutOfRangeException(nameof(title));
        ChartPanel p =
            new() { SampleCount = samples, SeriesCount = series,   TimeWindow = seconds, DefaultScale = 1,
                    MaximumScale = 10,     SymlogThreshold = .01f, HeightWeight = 1 };
        char *t = p.Title;
        title.AsSpan().CopyTo(new Span<char>(t, 128));
        return p;
    }
}
[StructLayout(LayoutKind.Sequential)]
public struct Metrics
{
    public ulong Frames, Submissions, Coalesced;
    public double LastRenderMilliseconds;
    public uint Pending, DeviceRecreations;
    public ulong LastCppAllocations;
}

[StructLayout(LayoutKind.Sequential)]
public struct RefreshInfo
{
    public double MonitorHz, TargetHz;
    public uint Known, HighResolution, Suspended, Error;
    public ulong Posted, Coalesced, Callbacks;
}
[Flags]
public enum ControlStyle : uint
{
    Flat = 1, Selected = 2, Foldout = 4, Expanded = 8, AlignCenter = 16, AlignRight = 32
}

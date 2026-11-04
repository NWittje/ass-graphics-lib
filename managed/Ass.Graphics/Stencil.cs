using System.Runtime.InteropServices;
namespace Ass.Graphics;

public enum StencilKind : uint
{
    Replay = 1,
    Clear,
    WriteFill,
    WriteStroke,
    Test,
    DisableTest,
    PaintTile
}
public enum StencilComparison : uint
{
    Equal = 1,
    NotEqual = 2
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReplaySegment
{
    public uint Kind, Reserved;
    public fixed double Points[6];
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReplayResource
{
    public uint Size, Version, Id, Kind, Winding, SegmentCount;
    public ReplaySegment *Segments;
    public float Width, Miter, Phase;
    public uint Cap, Join, DashCount;
    public float *Dashes;
    public uint ImageWidth, ImageHeight, Stride, PixelBytes;
    public void *Pixels;
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReplayCommand
{
    public uint Size, Version, Kind, Context, Resource, Value;
    public fixed double Matrix[6];
    public float Opacity;
    public uint Argb;
}
[StructLayout(LayoutKind.Sequential)]
public struct StencilCommand
{
    public uint Size, Version;
    public StencilKind Kind;
    public uint Context, MaskIndex, Path, ClearBits, SetBits, TestBits, Reference;
    public StencilComparison Comparison;
    public uint Reserved;
    public ReplayCommand Replay;
}
/// <summary>Borrowed descriptors, copied during SubmitStencil. Pin all referenced arrays for that
/// call.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct StencilSnapshot
{
    public uint Size, Version, CommandCount, ResourceCount;
    public StencilCommand *Commands;
    public ReplayResource *Resources;
    public uint MaskCount;
    public fixed uint InitialValues[2];
    public uint Flags;
    public float FlatteningTolerance;
    public uint MeshVertexLimit;
    public fixed uint Reserved[2];
    public static StencilSnapshot Create() => new() { Size = (uint)sizeof(StencilSnapshot), Version = 1,
                                                      Flags = 1, FlatteningTolerance = .25f,
                                                      MeshVertexLimit = 1048576 };
}
[StructLayout(LayoutKind.Sequential)]
public struct StencilMetrics
{
    public uint Size, Version;
    public ulong CpuBytes, MeshVertices, MeshBuilds, MeshCacheHits, TextureBytes, BufferBytes,
        ResourceCreations, SourceDraws, GpuDraws, MaskClears;
    public double AdmissionMs;
}
internal static unsafe class StencilNative
{
    [DllImport("ass_graphics_lib", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ag_display_submit_stencil(GraphicsHandle display, StencilSnapshot *snapshot);
    [DllImport("ass_graphics_lib", ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ag_display_stencil_metrics(GraphicsHandle display, StencilMetrics *metrics);
}
public sealed unsafe partial class Display
{
    /// <summary>UI-thread complete ordered scene. One selected mask per opaque aliased paint; no
    /// readback.</summary>
    public void SubmitStencil(StencilSnapshot snapshot) =>
        GraphicsError.Check(StencilNative.ag_display_submit_stencil(H, &snapshot));
    public StencilMetrics StencilMetrics
    {
        get {
            StencilMetrics m = new() { Size = (uint)sizeof(StencilMetrics), Version = 1 };
            GraphicsError.Check(StencilNative.ag_display_stencil_metrics(H, &m));
            return m;
        }
    }
}

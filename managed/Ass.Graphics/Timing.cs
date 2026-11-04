using System.Runtime.InteropServices;
namespace Ass.Graphics;
[Flags]
public enum TimingFlags:uint
{
    OrderedSubmitted=1,SourceUpdated=2,Composited=4,PresentCalled=8,Failed=16
}
[StructLayout(LayoutKind.Sequential)]
public struct DisplayTiming
{
    public uint Size,Version;
    public ulong SampleId,DeviceGeneration,OrderedSequence,CompositedSequence,AdmissionSequence,DroppedSamples;
    public uint GpuKnown,GpuDisjoint,GpuPending;
    public TimingFlags Flags;
    public double GpuTotalMs,GpuSourceMs,GpuGraphMs,CpuRenderMs,CpuPresentMs,CpuAdmissionMs;
}
internal static unsafe class TimingNative
{
    [DllImport("ass_graphics_lib",ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_display_timing_get(GraphicsHandle display,DisplayTiming* result);
    internal static DisplayTiming Read(GraphicsHandle display)
    {
        DisplayTiming result=new(){Size=(uint)sizeof(DisplayTiming),Version=1};
        GraphicsError.Check(ag_display_timing_get(display,&result));return result;
    }
}
/// <summary>Sample-correlated GPU/render CPU timing; admission fields are independent latest CPU preparation.</summary>
public static class DisplayTimingExtensions
{
    public static DisplayTiming Timing(this Display display)=>TimingNative.Read(display.Raw);
    public static DisplayTiming Timing(this OrderedDisplay display)=>TimingNative.Read(display.Raw);
}

using System.Runtime.InteropServices;
namespace Ass.Graphics;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedCaps
{
    public uint Size,Version,DeviceKnown,FeatureLevel,AdapterKnown,LuidLow;
    public int LuidHigh;
    public uint Vendor,Device,Software,OptionsKnown;
    public int OptionsResult;
    public uint RovSupported,FormatKnown;
    public int FormatResult;
    public uint R32Load,R32Store,Available;
    public fixed char AdapterName[128];
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedProgramDesc
{
    public uint Size,Version,Contract,Trusted;
    public void* Material;
    public uint MaterialBytes,Reserved0;
    public void* Resolve;
    public uint ResolveBytes,ConstantBytes;
    public void* Constants;
    public fixed uint Reserved[2];
    public static OrderedProgramDesc Create()=>new(){Size=(uint)sizeof(OrderedProgramDesc),Version=1,Contract=1,Trusted=1};
}
public enum OrderedKind:uint { State=1,Fill,Stroke }
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedRecord { public fixed uint Value[4]; }
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedCommand
{
    public uint Size,Version;
    public OrderedKind Kind;
    public uint Context,Path,Record;
    public fixed uint Reserved[2];
    public StencilCommand State;
}
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedSnapshot
{
    public uint Size,Version,CommandCount,ResourceCount;
    public OrderedCommand* Commands;
    public ReplayResource* Resources;
    public OrderedRecord* Records;
    public uint RecordCount;
    public fixed uint Initial[2];
    public uint Flags;
    public ulong Sequence;
    public nint Program;
    public float FlatteningTolerance;
    public uint MeshVertexLimit;
    public fixed uint Reserved[2];
    public static OrderedSnapshot Create()=>new(){Size=(uint)sizeof(OrderedSnapshot),Version=1,FlatteningTolerance=.25f,MeshVertexLimit=1048576};
}
[StructLayout(LayoutKind.Sequential)]
public struct OrderedState
{
    public uint Size,Version;
    public ulong Accepted,Submitted,Completed,DeviceEpoch,Submissions,MaterialDraws,MaskDraws,Resolves,
        ResourceCreations,CpuBytes,VertexBytes,RecordBytes;
    public uint Pending,Available,DeviceLost;
    public int Failure;
}
internal static unsafe class OrderedNative
{
    const string Dll="ass_graphics_lib";
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_display_create(GraphicsHandle context,nint parent,Rect bounds,DisplayDesc* desc,out nint result,OrderedCaps* observed);
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_capabilities(GraphicsHandle display,OrderedCaps* result);
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_program_create(GraphicsHandle display,OrderedProgramDesc* desc,out nint result);
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_submit(GraphicsHandle display,OrderedSnapshot* snapshot);
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_replay(GraphicsHandle display,ulong sequence);
    [DllImport(Dll,ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_state_get(GraphicsHandle display,OrderedState* result);
}
public sealed class OrderedProgram:GraphicsObject
{
    internal OrderedProgram(OrderedDisplay owner,nint value){Handle=new(value,owner.Handle);}
}
/// <summary>UI-thread opt-in FL11.1/ROV renderer; no automatic software fallback.</summary>
public sealed unsafe class OrderedDisplay:GraphicsObject
{
    public OrderedDisplay(GraphicsContext context,DisplayDesc desc,nint parent=0,Rect bounds=default)
    {
        GraphicsError.Check(OrderedNative.ag_ordered_display_create(context.Raw,parent,bounds,&desc,out nint value,null));
        Handle=new(value,context.Handle);
    }
    public OrderedProgram Program(OrderedProgramDesc desc)
    {
        GraphicsError.Check(OrderedNative.ag_ordered_program_create(H,&desc,out nint value));return new(this,value);
    }
    /// <summary>Pin referenced arrays through this call. Program SafeHandle must remain alive through admission.</summary>
    public void Submit(OrderedSnapshot snapshot,OrderedProgram program)
    {
        var handle=program.Raw;
        bool held=false;
        try
        {
            handle.DangerousAddRef(ref held);
            snapshot.Program=handle.DangerousGetHandle();
            GraphicsError.Check(OrderedNative.ag_ordered_submit(H,&snapshot));
        }
        finally { if(held)handle.DangerousRelease(); }
    }
    public void Replay(ulong sequence)=>GraphicsError.Check(OrderedNative.ag_ordered_replay(H,sequence));
    public void Render(double seconds)=>GraphicsError.Check(Native.ag_display_render(H,seconds));
    public void OutputSize(uint w,uint h)=>GraphicsError.Check(Native.ag_display_output_size(H,w,h));
    public void Effects(EffectGraph* graph=null)=>GraphicsError.Check(Native.ag_display_effects(H,graph));
    public void ExportPng(string file){fixed(char* path=file)GraphicsError.Check(Native.ag_display_export_png(H,path));}
    public void Reset()=>GraphicsError.Check(Native.ag_device_reset(H));
    public OrderedState State{get{OrderedState s=new(){Size=(uint)sizeof(OrderedState),Version=1};GraphicsError.Check(OrderedNative.ag_ordered_state_get(H,&s));return s;}}
    public OrderedCaps Capabilities{get{OrderedCaps c=new(){Size=(uint)sizeof(OrderedCaps),Version=1};GraphicsError.Check(OrderedNative.ag_ordered_capabilities(H,&c));return c;}}
}

using System.Runtime.InteropServices;
namespace Ass.Graphics;
[StructLayout(LayoutKind.Sequential)] public struct DisplayDesc {
 public uint Size,Version,Warp,RasterPolicy; public float LogicalWidth,LogicalHeight;
 public uint SourceWidth,SourceHeight,OutputWidth,OutputHeight; public ulong TextureBudget;
 public static DisplayDesc Create(float width,float height,uint outputWidth,uint outputHeight,bool warp=false) =>
  new(){Size=(uint)Marshal.SizeOf<DisplayDesc>(),Version=1,LogicalWidth=width,LogicalHeight=height,OutputWidth=outputWidth,OutputHeight=outputHeight,Warp=warp?1u:0};
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct EffectInput {
 public uint Size,Version,Kind,Index,Slot,Linear,Repeat,Mipmaps,Srgb;public fixed byte Name[64];
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct EffectPass {
 public uint Size,Version;public fixed byte Name[64];public nint VertexBytecode;public uint VertexBytes;
 public nint PixelBytecode;public uint PixelBytes;public EffectInput* Inputs;public uint InputCount;
 public uint ScaleXKind,ScaleYKind,ScaleInput;public float ScaleX,ScaleY;public uint Format,Srgb,Mipmaps;
 public nint Parameters;public uint ParameterBytes,ParameterSlot;
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct LookupTexture {
 public uint Size,Version;public fixed byte Name[64];public uint Width,Height,Format,Stride,Mipmaps;
 public nint Pixels;public uint Bytes;
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct EffectGraph {
 public uint Size,Version;public EffectPass* Passes;public uint PassCount;
 public LookupTexture* Lookups;public uint LookupCount,HistoryCount,Temporal;
}
[StructLayout(LayoutKind.Sequential)] public struct DisplayMetrics {
 public uint Size,Version,SourceWidth,SourceHeight,ViewportWidth,ViewportHeight;
 public ulong Frames,SourceDraws,TextureBytes,Readbacks,DeviceRecreations,CppAllocations;
 public double CpuMs,GpuMs;public uint GpuKnown,Temporal,Suspended,Error;
}
[StructLayout(LayoutKind.Sequential)] public struct DisplayCaps {
 public uint Size,Version,FeatureLevel,MaxTextureDimension,MaxPasses,MaxInputs,MaxLookups,MaxHistory,Bgra8,Rgba8,Rgba16Float,Mipmaps;
}
[StructLayout(LayoutKind.Sequential)] public struct ContextRequest {
 public uint Size,Version;public nint Source;public Point Position;public uint Keyboard;
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct MonitorInfo {
 public uint Size,Version;public fixed char Identity[128];public fixed char Device[32];
 public int Left,Top,Right,Bottom,WorkLeft,WorkTop,WorkRight,WorkBottom;public uint Primary;
}
[StructLayout(LayoutKind.Sequential)] public unsafe struct WindowPlacement {
 public uint Size,Version;public fixed char Monitor[128];public int Left,Top,Right,Bottom;public uint Dpi,Mode;
}
/// <summary>UI-thread D2D source with D3D11 effects. Descriptors are copied during calls.</summary>
public sealed unsafe class Display : GraphicsObject {
 public Display(GraphicsContext context,DisplayDesc desc,nint parent=0,Rect bounds=default){nint h;GraphicsError.Check(Native.ag_display_create(context.Raw,parent,bounds,&desc,&h));Handle=new(h,context.Handle);}
 public DisplayCandidate Prepare(DisplayDesc? desc=null,EffectGraph* graph=null){nint handle;DisplayDesc value=desc.GetValueOrDefault();GraphicsError.Check(Native.ag_display_prepare(H,desc.HasValue?&value:null,graph,&handle));return new DisplayCandidate(this,handle);}
 public void Commit(DisplayCandidate candidate)=>GraphicsError.Check(Native.ag_display_commit(H,candidate.Raw));
 public void Constants(uint pass,uint slot,ReadOnlySpan<byte> bytes){fixed(byte* data=bytes)GraphicsError.Check(Native.ag_display_constants(H,pass,slot,(nint)data,(uint)bytes.Length));}
 public void ClearHistory()=>GraphicsError.Check(Native.ag_display_history_clear(H));
 public void Configure(DisplayDesc desc)=>GraphicsError.Check(Native.ag_display_configure(H,&desc));
 public void OutputSize(uint w,uint h)=>GraphicsError.Check(Native.ag_display_output_size(H,w,h));
 public void Submit(ReadOnlySpan<Draw> commands,ReadOnlySpan<Point> points=default,ReadOnlySpan<PathDraw> paths=default){fixed(Draw* d=commands)fixed(Point* p=points)fixed(PathDraw* g=paths)GraphicsError.Check(Native.ag_display_submit(H,d,(uint)commands.Length,p,(uint)points.Length,g,(uint)paths.Length));}
 public void Effects(EffectGraph* graph=null)=>GraphicsError.Check(Native.ag_display_effects(H,graph));
 public void Parameters(uint pass,ReadOnlySpan<byte> bytes){fixed(byte* data=bytes)GraphicsError.Check(Native.ag_display_parameters(H,pass,(nint)data,(uint)bytes.Length));}
 public void Render(double seconds)=>GraphicsError.Check(Native.ag_display_render(H,seconds));
 public void ExportPng(string file){fixed(char* path=file)GraphicsError.Check(Native.ag_display_export_png(H,path));}
 public void Reset()=>GraphicsError.Check(Native.ag_device_reset(H));
 public DisplayMetrics Metrics {get{DisplayMetrics m=new(){Size=(uint)sizeof(DisplayMetrics),Version=1};GraphicsError.Check(Native.ag_display_metrics_get(H,&m));return m;}}
 public DisplayCaps Capabilities {get{DisplayCaps c=new(){Size=(uint)sizeof(DisplayCaps),Version=1};GraphicsError.Check(Native.ag_display_capabilities(H,&c));return c;}}
}
public sealed unsafe class Dialog : GraphicsObject {
 private readonly EventCallback callback;
 public event Action<uint,EventKind,double>? Event;
 public Exception? LastCallbackError {get;private set;}
 public Dialog(Window owner,string title,uint width,uint height){callback=(_,id,kind,value)=>{try{Event?.Invoke(id,kind,value);}catch(Exception e){LastCallbackError=e;}};nint h;fixed(char* text=title)GraphicsError.Check(Native.ag_dialog_create(owner.Raw,text,width,height,Marshal.GetFunctionPointerForDelegate(callback),0,&h));Handle=new(h,owner.Handle);Handle.CallbackRoot=callback;}
 public void Show(bool visible=true,bool activate=true)=>GraphicsError.Check(Native.ag_dialog_show(H,visible?1u:0,activate?1u:0));
 public void Close()=>GraphicsError.Check(Native.ag_window_close(H));
 public void DeferClose(bool enabled)=>GraphicsError.Check(Native.ag_window_defer_close(H,enabled?1u:0));
}
public static unsafe class DisplayWindows {
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)]private delegate void RequestCallback(nint user,ContextRequest* request);
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)]private delegate void MonitorCallback(nint user,MonitorInfo* monitor);
 public static void ContextRequests(this GraphicsObject obj,Action<ContextRequest>? callback){RequestCallback? bridge=callback is null?null:(_,r)=>{try{callback(*r);}catch{/* callbacks cannot cross native ABI */}};GraphicsError.Check(Native.ag_context_requests(obj.Raw,bridge is null?0:Marshal.GetFunctionPointerForDelegate(bridge),0));obj.Handle.ContextCallbackRoot=bridge;}
 public static MonitorInfo[] Monitors(this GraphicsContext context){List<MonitorInfo> found=[];MonitorCallback bridge=(_,m)=>found.Add(*m);GraphicsError.Check(Native.ag_monitors(context.Raw,Marshal.GetFunctionPointerForDelegate(bridge),0));GC.KeepAlive(bridge);return found.ToArray();}
 public static WindowPlacement Placement(this Window window){WindowPlacement p=new(){Size=(uint)sizeof(WindowPlacement),Version=1};GraphicsError.Check(Native.ag_window_placement_get(window.Raw,&p));return p;}
 public static void Placement(this Window window,WindowPlacement placement)=>GraphicsError.Check(Native.ag_window_placement_set(window.Raw,&placement));
 public static void Borderless(this Window window,bool enabled,string? monitor=null){fixed(char* id=monitor)GraphicsError.Check(Native.ag_window_borderless(window.Raw,enabled?1u:0,id));}
}

[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct ConstantBinding { public uint Size,Version,Offset,Kind,Index; }

public sealed unsafe class DisplayCandidate:GraphicsObject {
 internal DisplayCandidate(Display owner,nint handle){Handle=new(handle,owner.Handle);}
 public void Constants(uint pass,uint slot,ReadOnlySpan<byte> data,ReadOnlySpan<ConstantBinding> bindings=default){fixed(byte* d=data)fixed(ConstantBinding* b=bindings)GraphicsError.Check(Native.ag_display_candidate_constants(H,pass,slot,(nint)d,(uint)data.Length,b,(uint)bindings.Length));}
}
public static class DisplayShortcuts {public const uint Repeat=256;public static void Shortcut(this Window window,uint key,uint modifiers,uint id)=>GraphicsError.Check(Native.ag_window_shortcut(window.Raw,key,modifiers,id));}

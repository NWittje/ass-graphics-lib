using System.Runtime.InteropServices;
namespace Ass.Graphics;
[StructLayout(LayoutKind.Sequential)]
public unsafe struct OrderedImageDesc
{
    public uint Size,Version,Width,Height,Format,Stride;
    public ulong Bytes;
    public void* Pixels;
    public fixed uint Reserved[2];
    public static OrderedImageDesc Create()=>new(){Size=(uint)sizeof(OrderedImageDesc),Version=1,Format=1};
}
[StructLayout(LayoutKind.Sequential)]
public struct OrderedImageState
{
    public uint Size,Version,Width,Height,Format,Resident;
    public ulong PackedBytes,Uploads,ResourceCreations,Generation;
}
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct OrderedImageBindings
{
    internal uint Size,Version;
    internal nint First,Second;
    internal fixed uint Reserved[2];
}
internal static unsafe class OrderedImagesNative
{
    [DllImport("ass_graphics_lib",ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_image_create(GraphicsHandle display,OrderedImageDesc* desc,out nint value);
    [DllImport("ass_graphics_lib",ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_image_state_get(GraphicsHandle image,OrderedImageState* state);
    [DllImport("ass_graphics_lib",ExactSpelling=true,CallingConvention=CallingConvention.Cdecl)]
    internal static extern int ag_ordered_submit_images(GraphicsHandle display,OrderedSnapshot* snapshot,OrderedImageBindings* bindings);
}
/// <summary>UI-thread immutable packed uint image. Creation copies the pinned input before returning.</summary>
public sealed unsafe class OrderedImage:GraphicsObject
{
    public OrderedImage(OrderedDisplay display,OrderedImageDesc desc)
    {
        GraphicsError.Check(OrderedImagesNative.ag_ordered_image_create(display.Raw,&desc,out nint value));
        try { Handle=new(value,display.Handle); }
        catch { Native.ag_release(value); throw; }
    }
    public OrderedImageState State{get{OrderedImageState s=new(){Size=(uint)sizeof(OrderedImageState),Version=1};GraphicsError.Check(OrderedImagesNative.ag_ordered_image_state_get(H,&s));return s;}}
}
public static unsafe class OrderedImageExtensions
{
    /// <summary>Pin snapshot arrays through admission. Native recipes retain image/program data after return.</summary>
    public static void SubmitImages(this OrderedDisplay display,OrderedSnapshot snapshot,OrderedProgram program,OrderedImage? first,OrderedImage? second=null)
    {
        // Raw snapshot pointers are borrowed. SafeHandles referenced inside POD
        // descriptors require explicit holds through the native admission call.
        var p=program.Raw;var a=first?.Raw;var b=second?.Raw;
        bool ph=false,ah=false,bh=false;
        try
        {
            p.DangerousAddRef(ref ph);a?.DangerousAddRef(ref ah);b?.DangerousAddRef(ref bh);
            snapshot.Program=p.DangerousGetHandle();
            OrderedImageBindings inputs=new(){Size=(uint)sizeof(OrderedImageBindings),Version=1,
                First=a?.DangerousGetHandle()??0,Second=b?.DangerousGetHandle()??0};
            GraphicsError.Check(OrderedImagesNative.ag_ordered_submit_images(display.Raw,&snapshot,&inputs));
        }
        finally {if(bh)b!.DangerousRelease();if(ah)a!.DangerousRelease();if(ph)p.DangerousRelease();}
    }
}

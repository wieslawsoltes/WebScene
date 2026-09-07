using System;
using System.Runtime.InteropServices;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal enum NativeSceneAcquireStatus : uint
{
    Success, Empty, InvalidArgument, UnsupportedVersion, UnsupportedCapabilities,
    OutOfMemory, InternalError, Backpressure
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneAcquireOptionsV3
{
    public uint StructSize, SceneVersion;
    public ulong ConsumerCapabilities;
    public static NativeSceneAcquireOptionsV3 CpuOnly => new() { StructSize = 16, SceneVersion = 3 };
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneViewV3
{
    public uint StructSize, SceneVersion;
    public ulong RequiredCapabilities;
    // Borrowed until SceneReleaseV3; never release this CPU view separately.
    public IntPtr CpuView, LeaseToken;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeGpuImageInfoV3
{
    public uint StructSize, Version;
    public ulong Canvas, Allocation, AllocationGeneration, ContentSerial;
    public ulong ProducerTimeline, ProducerValue;
    public uint Width, Height, Format, Alpha, ColorSpace, Orientation;
    public static NativeGpuImageInfoV3 Empty => new() { StructSize = 80, Version = 3 };
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeGpuIOSurfaceViewV3
{
    public uint StructSize, Version;
    public IntPtr BorrowedIOSurface;
    public ulong AllocationBytes;
    public static NativeGpuIOSurfaceViewV3 Empty => new()
    {
        StructSize = (uint)Marshal.SizeOf<NativeGpuIOSurfaceViewV3>(), Version = 3
    };
}

public static unsafe partial class NativeWebSceneApi
{
    // These declarations do not opt the existing renderer into GPU scenes.
    internal const ulong GpuImageCapability = 1;
    internal const uint GpuImagePaintCommand = 256;
    internal const ulong OrderedCanvasCapability = 2;
    internal const uint OrderedCanvasPaintCommand = 257;

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_latest_scene_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus AcquireLatestSceneV3(IntPtr engine, in NativeSceneAcquireOptionsV3 options, out IntPtr scene);
    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_next_scene_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus AcquireNextSceneV3(IntPtr engine, in NativeSceneAcquireOptionsV3 options, out IntPtr scene);
    [DllImport(LibraryName, EntryPoint = "webscene_scene_acknowledge_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte SceneAcknowledgeV3(IntPtr scene);
    [DllImport(LibraryName, EntryPoint = "webscene_scene_release_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SceneReleaseV3(IntPtr scene);
    [DllImport(LibraryName, EntryPoint = "webscene_scene_gpu_image_count_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint SceneGpuImageCountV3(IntPtr scene);
    [DllImport(LibraryName, EntryPoint = "webscene_scene_retain_gpu_image_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus SceneRetainGpuImageV3(IntPtr scene, uint index, out IntPtr image);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_retain_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus GpuImageRetainV3(IntPtr image, out IntPtr retained);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_describe_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte GpuImageDescribeV3(IntPtr image, ref NativeGpuImageInfoV3 info);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_release_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void GpuImageReleaseV3(IntPtr image);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_begin_consumer_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus GpuImageBeginConsumerV3(IntPtr image, out IntPtr consumer);
    // Completion consumes the handle. Call only from a GPU completion path;
    // Dispose/finalization of a CPU wrapper is not a GPU completion event.
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_complete_consumer_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void GpuImageCompleteConsumerV3(IntPtr consumer);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_get_iosurface_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte GpuImageGetIOSurfaceV3(IntPtr consumer, ref NativeGpuIOSurfaceViewV3 view);

}

// CPU scene retention only: disposing this handle does not complete GPU work.
internal sealed class NativeSceneLeaseV3 : SafeHandle
{
    private NativeSceneLeaseV3() : base(IntPtr.Zero, ownsHandle: true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;

    internal static NativeSceneAcquireStatus Acquire(IntPtr engine, in NativeSceneAcquireOptionsV3 options,
        bool ordered, out NativeSceneLeaseV3? lease)
    {
        lease = null;
        // Allocate managed ownership before native acquisition, so an allocation
        // failure cannot strand a newly acquired native lease.
        var candidate = new NativeSceneLeaseV3();
        try
        {
            var status = ordered
                ? NativeWebSceneApi.AcquireNextSceneV3(engine, in options, out var pointer)
                : NativeWebSceneApi.AcquireLatestSceneV3(engine, in options, out pointer);
            candidate.SetHandle(pointer);
            if (status != NativeSceneAcquireStatus.Success)
            {
                candidate.Dispose();
                return status;
            }
            if (candidate.IsInvalid) throw new InvalidOperationException("Native acquisition returned an empty successful lease.");
            lease = candidate;
            return status;
        }
        catch
        {
            candidate.Dispose();
            throw;
        }
    }

    internal bool Acknowledge() => NativeWebSceneApi.SceneAcknowledgeV3(this) != 0;
    internal uint ImageCount => NativeWebSceneApi.SceneGpuImageCountV3(this);

    // All borrowed pointers are valid only during this callback. The SafeHandle
    // reference protects the native lease even if another thread calls Dispose.
    internal void WithView(Action<NativeSceneViewV3> read)
    {
        ArgumentNullException.ThrowIfNull(read);
        var added = false;
        try
        {
            DangerousAddRef(ref added);
            read(Marshal.PtrToStructure<NativeSceneViewV3>(handle));
        }
        finally
        {
            if (added) DangerousRelease();
        }
    }

    protected override bool ReleaseHandle()
    {
        NativeWebSceneApi.SceneReleaseV3(handle);
        return true;
    }
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_scene_acknowledge_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte SceneAcknowledgeV3(NativeSceneLeaseV3 scene);
    [DllImport(LibraryName, EntryPoint = "webscene_scene_gpu_image_count_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint SceneGpuImageCountV3(NativeSceneLeaseV3 scene);
}

internal sealed class NativeGpuImageLeaseV3 : SafeHandle
{
    private NativeGpuImageLeaseV3() : base(IntPtr.Zero, ownsHandle: true) { }
    public override bool IsInvalid => handle == IntPtr.Zero;

    internal static NativeSceneAcquireStatus Acquire(NativeSceneLeaseV3 scene, uint index,
        out NativeGpuImageLeaseV3? image)
    {
        ArgumentNullException.ThrowIfNull(scene);
        image = null;
        var candidate = new NativeGpuImageLeaseV3();
        try
        {
            var status = NativeWebSceneApi.SceneRetainGpuImageV3(scene, index, out var pointer);
            return Adopt(candidate, status, pointer, out image);
        }
        catch { candidate.Dispose(); throw; }
    }

    internal NativeSceneAcquireStatus Retain(out NativeGpuImageLeaseV3? retained)
    {
        retained = null;
        var candidate = new NativeGpuImageLeaseV3();
        try
        {
            var status = NativeWebSceneApi.GpuImageRetainV3(this, out var pointer);
            return Adopt(candidate, status, pointer, out retained);
        }
        catch { candidate.Dispose(); throw; }
    }

    private static NativeSceneAcquireStatus Adopt(NativeGpuImageLeaseV3 candidate,
        NativeSceneAcquireStatus status, IntPtr pointer, out NativeGpuImageLeaseV3? image)
    {
        image = null;
        candidate.SetHandle(pointer);
        if (status != NativeSceneAcquireStatus.Success)
        {
            candidate.Dispose();
            return status;
        }
        if (candidate.IsInvalid) throw new InvalidOperationException("Native retain returned an empty successful lease.");
        image = candidate;
        return status;
    }

    internal NativeGpuImageInfoV3 Describe()
    {
        var info = NativeGpuImageInfoV3.Empty;
        if (NativeWebSceneApi.GpuImageDescribeV3(this, ref info) == 0)
            throw new InvalidOperationException("Native image metadata is unavailable.");
        return info;
    }

    protected override bool ReleaseHandle()
    {
        NativeWebSceneApi.GpuImageReleaseV3(handle);
        return true;
    }
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_scene_retain_gpu_image_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus SceneRetainGpuImageV3(NativeSceneLeaseV3 scene, uint index, out IntPtr image);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_retain_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus GpuImageRetainV3(NativeGpuImageLeaseV3 image, out IntPtr retained);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_describe_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern byte GpuImageDescribeV3(NativeGpuImageLeaseV3 image, ref NativeGpuImageInfoV3 info);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_image_begin_consumer_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeSceneAcquireStatus GpuImageBeginConsumerV3(NativeGpuImageLeaseV3 image, out IntPtr consumer);
}


// Explicit GPU completion ownership: deliberately neither IDisposable nor a
// finalizable SafeHandle. The presenter must retain this wrapper until its GPU
// completion path calls Complete; GC cannot certify that GPU use has ended.
internal sealed class NativeGpuImageConsumerV3
{
    private readonly object _gate = new();
    private IntPtr _handle;
    private int _borrows;
    private bool _completionRequested;
    private NativeGpuImageConsumerV3() { }

    internal static NativeSceneAcquireStatus Acquire(NativeGpuImageLeaseV3 image,
        out NativeGpuImageConsumerV3? consumer)
    {
        ArgumentNullException.ThrowIfNull(image);
        consumer = null;
        var candidate = new NativeGpuImageConsumerV3();
        var status = NativeWebSceneApi.GpuImageBeginConsumerV3(image, out candidate._handle);
        if (status != NativeSceneAcquireStatus.Success) return status;
        if (candidate._handle == IntPtr.Zero)
            throw new InvalidOperationException("Native consumer acquisition returned an empty handle.");
        consumer = candidate;
        return status;
    }

    // Borrow is synchronous. Importers must take their own required native
    // references and preserve this consumer until the associated GPU fence.
    internal bool WithIOSurface(Action<NativeGpuIOSurfaceViewV3> import)
    {
        ArgumentNullException.ThrowIfNull(import);
        IntPtr pointer;
        lock (_gate)
        {
            if (_completionRequested) throw new InvalidOperationException("GPU consumer already completed.");
            _borrows++;
            pointer = _handle;
        }
        try
        {
            var view = NativeGpuIOSurfaceViewV3.Empty;
            try
            {
                if (NativeWebSceneApi.GpuImageGetIOSurfaceV3(pointer, ref view) == 0) return false;
            }
            catch (EntryPointNotFoundException) { return false; } // Older v3 runtime lacks this optional hook.
            if (view.BorrowedIOSurface == IntPtr.Zero || view.AllocationBytes == 0)
                throw new InvalidOperationException("Native IOSurface lookup returned an invalid view.");
            import(view);
            return true;
        }
        finally
        {
            IntPtr retired = IntPtr.Zero;
            lock (_gate)
            {
                _borrows--;
                if (_completionRequested && _borrows == 0) { retired = _handle; _handle = IntPtr.Zero; }
            }
            if (retired != IntPtr.Zero) NativeWebSceneApi.GpuImageCompleteConsumerV3(retired);
        }
    }

    internal void Complete()
    {
        IntPtr retired = IntPtr.Zero;
        lock (_gate)
        {
            if (_completionRequested) throw new InvalidOperationException("Duplicate GPU consumer completion.");
            _completionRequested = true;
            if (_borrows == 0) { retired = _handle; _handle = IntPtr.Zero; }
        }
        // Outstanding synchronous imports defer deletion without blocking a
        // completion thread or freeing a pointer still borrowed by an importer.
        if (retired != IntPtr.Zero) NativeWebSceneApi.GpuImageCompleteConsumerV3(retired);
    }
}

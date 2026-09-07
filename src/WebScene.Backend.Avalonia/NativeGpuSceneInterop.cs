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

public static unsafe partial class NativeWebSceneApi
{
    // These declarations do not opt the existing renderer into GPU scenes.
    internal const ulong GpuImageCapability = 1;
    internal const uint GpuImagePaintCommand = 256;

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
}

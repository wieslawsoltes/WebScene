using System.Runtime.InteropServices;
using Avalonia.OpenGL.Egl;
using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Imports on the actual ANGLE/D3D11 device behind the active Skia lease.
// Native queue waits order producer writes; a native fence retires final reads.
internal sealed class NativeWindowsRetainedGpuImage : INativeRetainedGpuImage
{
    private readonly EglContext _host;
    private readonly GRContext _skia;
    private NativeGpuImageConsumerV3? _consumer;
    private IntPtr _bridge;
    private EglSurface? _surface;
    private SKImage? _image;
    private int _texture;
    private bool _retiring, _sealed;
    private NativeWindowsRetainedGpuImage(EglContext host, GRContext skia) { _host = host; _skia = skia; }

    internal static bool Supports(ISkiaSharpApiLease lease)
    {
        if (!OperatingSystem.IsWindows() || lease.GrContext is null) return false;
        using var platform = lease.TryLeasePlatformGraphicsApi();
        return platform?.Context is EglContext context && TryGetDevice(context, out var device)
            && NativeWebSceneApi.GpuD3D11SupportedV3(device) == 0;
    }
    private static bool TryGetDevice(EglContext host, out IntPtr device)
    {
        device = IntPtr.Zero;
        return host.EglInterface.QueryDisplayAttribExt(host.Display.Handle, 0x322C, out var eglDevice)
            && host.EglInterface.QueryDeviceAttribExt(eglDevice, 0x33A1, out device) && device != IntPtr.Zero;
    }
    internal static NativeWindowsRetainedGpuImage? Import(NativeGpuImageLeaseV3 source,
        ISkiaSharpApiLease lease, GRSurfaceOrigin origin, SKAlphaType alpha)
    {
        var skia = lease.GrContext ?? throw new NotSupportedException("Windows GPU composition requires a Skia GPU context.");
        using var platform = lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("Windows GPU composition requires a platform graphics lease.");
        if (platform.Context is not EglContext host || !TryGetDevice(host, out var device))
            throw new NotSupportedException("Windows GPU composition requires an ANGLE D3D11 host.");
        var result = new NativeWindowsRetainedGpuImage(host, skia);
        var status = NativeGpuImageConsumerV3.Acquire(source, out result._consumer);
        if (status == NativeSceneAcquireStatus.Backpressure) return null;
        if (status != NativeSceneAcquireStatus.Success) throw new InvalidOperationException($"DXGI consumer acquisition failed: {status}");
        try
        {
            var metadata = source.Describe();
            IntPtr texture = IntPtr.Zero;
            result._consumer!.WithNativeHandle(pointer =>
            {
                Marshal.ThrowExceptionForHR(NativeWebSceneApi.GpuD3D11ImportV3(pointer, device, out result._bridge, out texture));
                return true;
            });
            result._surface = host.Display.CreatePBufferFromClientBuffer(0x33A3, texture, new[]
            {
                0x3057, checked((int)metadata.Width), 0x3056, checked((int)metadata.Height),
                0x3080, 0x305E, 0x3081, 0x305F, 0x345D, 0x1908, 0x3038
            });
            result._texture = host.GlInterface.GenTexture();
            if (result._texture == 0) throw new InvalidOperationException("ANGLE texture allocation failed.");
            host.GlInterface.BindTexture(0x0DE1, result._texture);
            if (host.EglInterface.BindTexImage(host.Display.Handle, result._surface.DangerousGetHandle(), 0x3084) == 0)
                throw new InvalidOperationException($"ANGLE shared texture binding failed: 0x{host.EglInterface.GetError():X}");
            using var backend = new GRBackendTexture(checked((int)metadata.Width), checked((int)metadata.Height), false,
                new GRGlTextureInfo(0x0DE1, (uint)result._texture, 0x8058));
            result._image = SKImage.FromTexture(skia, backend, origin, SKColorType.Rgba8888, alpha)
                ?? throw new InvalidOperationException("Skia could not wrap the ANGLE shared image.");
            return result;
        }
        catch
        {
            result.ReleaseHostObjects(); // No drawing has sampled this image.
            if (result._bridge != IntPtr.Zero) NativeWebSceneApi.GpuD3D11DestroyV3(result._bridge);
            result._consumer!.Complete();
            throw;
        }
    }
    private void Check(ISkiaSharpApiLease lease)
    {
        if (!ReferenceEquals(lease.GrContext, _skia) || !_host.IsCurrent || _host.IsLost)
            throw new InvalidOperationException("DXGI image requires its owning Skia and ANGLE contexts.");
    }
    public void Draw(ISkiaSharpApiLease lease, SKRect destination, SKPaint? paint = null)
    {
        Check(lease);
        if (_retiring) throw new InvalidOperationException("Retiring DXGI image cannot be drawn.");
        lease.SkCanvas.DrawImage(_image!, destination, paint);
    }
    private void ReleaseHostObjects()
    {
        _image?.Dispose(); _image = null;
        if (_texture != 0) { _host.GlInterface.DeleteTexture(_texture); _texture = 0; }
        _surface?.Dispose(); _surface = null;
    }
    public void Retire(ISkiaSharpApiLease lease)
    {
        Check(lease); _retiring = true;
        if (_sealed || _consumer is null) return;
        _image?.Dispose(); _image = null;
        using var platform = lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("DXGI retirement requires a platform lease.");
        if (!ReferenceEquals(platform.Context, _host)) throw new InvalidOperationException("ANGLE host changed during retirement.");
        // Entering the platform lease submits Skia's final reads. GL deletion
        // retains in-flight driver references; the native bridge owns the D3D
        // texture until its signal completes, so background polling needs no GL.
        ReleaseHostObjects();
        _host.GlInterface.Flush();
        Marshal.ThrowExceptionForHR(NativeWebSceneApi.GpuD3D11SealV3(_bridge));
        _sealed = true;
    }
    private bool Complete()
    {
        if (_consumer is null) return true;
        if (!_sealed) return false;
        var status = NativeWebSceneApi.GpuD3D11PollV3(_bridge);
        Marshal.ThrowExceptionForHR(status);
        if (status != 0) return false;
        NativeWebSceneApi.GpuD3D11DestroyV3(_bridge); _bridge = IntPtr.Zero;
        _consumer.Complete(); _consumer = null;
        return true;
    }
    public bool TryComplete(ISkiaSharpApiLease lease) { Check(lease); return Complete(); }
    public bool TryRetireWithoutVisual() => Complete();
    public void SealForDetachedRetirement()
    {
        if (_sealed || _consumer is null) return;
        using var current = _host.EnsureCurrent();
        lock (_skia)
        {
            if (_skia.IsAbandoned || _host.IsLost) throw new InvalidOperationException("DXGI host lost before retirement.");
            _retiring = true;
            _image?.Dispose(); _image = null;
            _skia.Flush();
            try
            {
                ReleaseHostObjects();
                _host.GlInterface.Flush();
                Marshal.ThrowExceptionForHR(NativeWebSceneApi.GpuD3D11SealV3(_bridge));
                _sealed = true;
            }
            finally { _skia.ResetContext(); }
        }
    }
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_d3d11_supported_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GpuD3D11SupportedV3(IntPtr device);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_d3d11_import_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GpuD3D11ImportV3(IntPtr consumer, IntPtr device, out IntPtr owner, out IntPtr texture);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_d3d11_seal_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GpuD3D11SealV3(IntPtr owner);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_d3d11_poll_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GpuD3D11PollV3(IntPtr owner);
    [DllImport(LibraryName, EntryPoint = "webscene_gpu_d3d11_destroy_v3", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void GpuD3D11DestroyV3(IntPtr owner);
}

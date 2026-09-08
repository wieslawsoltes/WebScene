using System;
using Avalonia.OpenGL;
using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Host-context ownership of one imported image version. The scene cache must
// retain this object through Retire/TryComplete; GC is never GPU completion.
internal sealed class NativeMacOSRetainedGpuImage : INativeRetainedGpuImage
{
    private readonly IGlContext _host;
    private readonly GRContext _skia;
    private readonly IntPtr _nativeContext = NativeMacOSGpuImageImport.CurrentContext;
    private NativeGpuImageConsumerV3? _consumer;
    private SKImage? _image;
    private NativeMacOSGpuConsumerFence? _fence;
    private int _texture;
    internal bool IsRetiring { get; private set; }
    private NativeMacOSRetainedGpuImage(IGlContext host, GRContext skia)
    { _host = host; _skia = skia; }

    // The producer must already have certified readiness. Admission failure
    // leaves the scene's retained lease untouched so the caller can retry.
    internal static NativeMacOSRetainedGpuImage? Import(NativeGpuImageLeaseV3 image,
        ISkiaSharpApiLease lease, GRSurfaceOrigin origin, SKAlphaType alpha)
    {
        ArgumentNullException.ThrowIfNull(image);
        ArgumentNullException.ThrowIfNull(lease);
        var skia = lease.GrContext ?? throw new NotSupportedException("A GPU Skia context is required.");
        using var platform = lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("A host graphics API lease is required.");
        if (platform.Context is not IGlContext host || NativeMacOSGpuImageImport.CurrentContext == IntPtr.Zero)
            throw new NotSupportedException("The macOS IOSurface route requires a current host CGL context.");
        var result = new NativeMacOSRetainedGpuImage(host, skia);
        var status = NativeGpuImageConsumerV3.Acquire(image, out result._consumer);
        if (status == NativeSceneAcquireStatus.Backpressure) return null;
        if (status != NativeSceneAcquireStatus.Success)
            throw new InvalidOperationException($"GPU image consumer acquisition failed: {status}.");
        try
        {
            result._texture = host.GlInterface.GenTexture();
            if (result._texture == 0) throw new InvalidOperationException("Host texture allocation failed.");
            host.GlInterface.BindTexture(0x84F5, result._texture);
            if (!NativeMacOSGpuImageImport.TryBindCurrentRectangleTexture(result._consumer!))
                throw new NotSupportedException("Host IOSurface import failed.");
            result._image = NativeMacOSGpuImageImport.TryWrapRectangleTexture(result._consumer!, skia,
                (uint)result._texture, origin, alpha)
                ?? throw new NotSupportedException("Host Ganesh rectangle wrapping failed.");
            return result;
        }
        catch
        {
            // Import has not issued a draw: no consumer GPU read needs fencing.
            result._image?.Dispose();
            if (result._texture != 0) host.GlInterface.DeleteTexture(result._texture);
            result._consumer!.Complete();
            throw;
        }
    }
    private void Check(ISkiaSharpApiLease lease)
    {
        // Avalonia may migrate the same context between its UI and render
        // threads. Its active drawing lease serializes access to the GRContext.
        if (!ReferenceEquals(lease.GrContext, _skia) ||
            NativeMacOSGpuImageImport.CurrentContext != _nativeContext)
            throw new InvalidOperationException("GPU image use requires its owning leased Skia and CGL contexts.");
    }
    public void Draw(ISkiaSharpApiLease lease, SKRect destination, SKPaint? paint = null)
    {
        Check(lease);
        if (IsRetiring) throw new InvalidOperationException("A retiring GPU image cannot be drawn again.");
        lease.SkCanvas.DrawImage(_image!, destination, paint);
    }
    public void Retire(ISkiaSharpApiLease lease)
    {
        Check(lease);
        if (_fence is not null || _consumer is null) return;
        IsRetiring = true;
        _image?.Dispose(); _image = null;
        using var platform = lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("Host graphics API lease disappeared during retirement.");
        if (!ReferenceEquals(platform.Context, _host)) throw new InvalidOperationException("Host graphics context changed.");
        // Entering Avalonia's platform lease flushes Skia's final reads. On fence
        // failure retain ownership and allow retry; never fabricate completion.
        _fence = NativeMacOSGpuConsumerFence.Create(_host.GlInterface.GetProcAddress, _consumer);
    }
    public bool TryComplete(ISkiaSharpApiLease lease)
    {
        Check(lease);
        if (!IsRetiring) throw new InvalidOperationException("Retire the GPU image before polling completion.");
        if (_consumer is null) return true;
        if (_fence is null) return false;
        using var platform = lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("Host graphics API lease disappeared during completion.");
        if (!ReferenceEquals(platform.Context, _host)) throw new InvalidOperationException("Host graphics context changed.");
        if (!_fence.TryCompleteInHostLease(platform)) return false;
        _consumer = null;
        _host.GlInterface.DeleteTexture(_texture); _texture = 0;
        return true;
    }
    // Avalonia 11.3.4 takes the CGL lock via EnsureCurrent before the GRContext
    // monitor during drawing. Use the same order after a visual is detached;
    // never touch a live drawing lease or wait for GPU completion on the CPU.
    public bool TryRetireWithoutVisual()
    {
        using var current = _host.EnsureCurrent();
        lock (_skia)
        {
            if (NativeMacOSGpuImageImport.CurrentContext != _nativeContext || _skia.IsAbandoned)
                throw new InvalidOperationException("Detached retirement lost its host context.");
            IsRetiring = true;
            try
            {
                _image?.Dispose(); _image = null;
                if (_consumer is null) return true;
                _skia.Flush();
                _fence ??= NativeMacOSGpuConsumerFence.Create(_host.GlInterface.GetProcAddress, _consumer);
                if (!_fence.TryCompleteInSerializedHostContext(_host)) return false;
                _consumer = null;
                _host.GlInterface.DeleteTexture(_texture); _texture = 0;
                return true;
            }
            finally { _skia.ResetContext(); }
        }
    }

}

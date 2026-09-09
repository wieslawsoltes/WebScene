using System;
using Avalonia.Platform;
using Avalonia.Skia;
using SkiaSharp;
namespace WebScene.Backends.Avalonia.Native;

// Completion-certified producer route. Early producer admission remains disabled.
internal sealed class NativeMetalRetainedGpuImage : INativeRetainedGpuImage
{
    private readonly IPlatformGraphicsContext _host;
    private readonly GRContext _skia;
    private readonly IntPtr _queue;
    private NativeGpuImageConsumerV3? _consumer;
    private NativeMetalIOSurfaceTexture? _texture;
    private SKImage? _image;
    private NativeMetalConsumerFence? _fence;
    private bool _retiring;
    private NativeMetalRetainedGpuImage(IPlatformGraphicsContext host, GRContext skia, IntPtr queue)
    { _host=host; _skia=skia; _queue=queue; }
    internal static bool Supports(ISkiaSharpApiLease lease)
    {
        using var platform=lease.TryLeasePlatformGraphicsApi();
        return platform?.Context is global::Avalonia.Metal.IMetalDevice;
    }
    internal static NativeMetalRetainedGpuImage? Import(NativeGpuImageLeaseV3 source,
        ISkiaSharpApiLease lease, GRSurfaceOrigin origin, SKAlphaType alpha)
    {
        var skia=lease.GrContext ?? throw new NotSupportedException("GPU Skia context required");
        NativeMetalRetainedGpuImage result;
        using(var platform=lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("Metal platform lease required"))
        {
            var host=platform.Context;
            var metal=host as global::Avalonia.Metal.IMetalDevice
                ?? throw new NotSupportedException("Metal host required");
            // Avalonia hides these members in its reference assembly. Reflect on
            // the known interface type so NativeAOT preserves the accessors.
            var type=typeof(global::Avalonia.Metal.IMetalDevice);
            var device=(IntPtr)type.GetProperty("Device")!.GetValue(metal)!;
            var queue=(IntPtr)type.GetProperty("CommandQueue")!.GetValue(metal)!;
            result=new(host,skia,queue);
            var status=NativeGpuImageConsumerV3.Acquire(source,out result._consumer);
            if(status==NativeSceneAcquireStatus.Backpressure) return null;
            if(status!=NativeSceneAcquireStatus.Success) throw new InvalidOperationException($"Metal consumer acquisition failed: {status}");
            try {
                result._texture=NativeMetalIOSurfaceTexture.Import(device,result._consumer!);
                NativeMetalProducerWait.Submit(queue,result._consumer!);
            }
            catch { result._texture?.Dispose(); result._consumer!.Complete(); throw; }
        }
        try {
            using var backend=NativeMetalBackendTexture.Create(result._texture.Width,result._texture.Height,result._texture.Handle);
            result._image=SKImage.FromTexture(skia,backend,origin,SKColorType.Bgra8888,alpha)
                ?? throw new InvalidOperationException("Metal Skia image wrapping failed");
            return result;
        } catch { result._texture.Dispose(); result._consumer!.Complete(); throw; }
    }
    private void Check(ISkiaSharpApiLease lease)
    {
        if(!ReferenceEquals(lease.GrContext,_skia)) throw new InvalidOperationException("Metal image belongs to another Skia context");
        // The GRContext identifies the owning device/queue. The active Skia lease
        // serializes it; opening a platform lease here would flush every draw.
    }
    public void Draw(ISkiaSharpApiLease lease,SKRect destination,SKPaint? paint=null)
    {
        Check(lease);
        if(_retiring) throw new InvalidOperationException("Retiring image cannot be drawn");
        lease.SkCanvas.DrawImage(_image!,destination,paint);
    }
    public void Retire(ISkiaSharpApiLease lease)
    {
        Check(lease);
        _retiring=true;
        if(_fence is not null || _consumer is null) return;
        _image?.Dispose(); _image=null;
        _skia.Flush(true,false);
        using var platform=lease.TryLeasePlatformGraphicsApi()
            ?? throw new NotSupportedException("Metal retirement requires a host lease");
        _fence=NativeMetalConsumerFence.Insert(_queue);
    }
    public bool TryComplete(ISkiaSharpApiLease lease)
    {
        Check(lease);
        if(!_retiring) throw new InvalidOperationException("Retirement not started");
        return Complete();
    }
    private bool Complete()
    {
        if(_consumer is null) return true;
        if(_fence is null || !_fence.TryComplete()) return false;
        _texture!.Dispose(); _texture=null;
        _consumer.Complete(); _consumer=null;
        return true;
    }
    public bool TryRetireWithoutVisual()
    {
        // Once sealed on the composition owner, background retirement only
        // observes the command buffer. Never flush a live Skia session there.
        if (_fence is not null || _consumer is null) return Complete();
        using var current=_host.EnsureCurrent();
        lock(_skia)
        {
            if(_skia.IsAbandoned) throw new InvalidOperationException("Metal context abandoned before retirement");
            _retiring=true;
            if(_consumer is null) return true;
            _image?.Dispose(); _image=null;
            _skia.Flush(true,false);
            _fence ??= NativeMetalConsumerFence.Insert(_queue);
            return Complete();
        }
    }
}

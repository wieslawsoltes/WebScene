using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Serialized by the composition owner. Replacement is CPU-only; all imported
// resources stay here until retirement completes under the host graphics lease.
internal enum NativeGpuSceneApplyResult { Applied, Backpressure, InvalidScene, RejectedDiff, AcknowledgementFailed }

internal sealed class NativeMacOSGpuScenePresenter
{
    private NativeMacOSGpuSceneImages? _current;
    private readonly NativeMacOSGpuSceneImages?[] _retiring = new NativeMacOSGpuSceneImages?[2];
    private bool _prepared;
    internal bool IsStopping { get; private set; }
    internal int ImportedCount { get; private set; }
    internal bool HasPendingRetirements => Array.Exists(_retiring, image => image is not null) || (IsStopping && _current is not null);

    // On false, ownership remains with the caller. Never replace a visible
    // group's ownership until there is bounded space to retire it safely.
    internal bool TryReplace(NativeMacOSGpuSceneImages images)
    {
        ArgumentNullException.ThrowIfNull(images);
        if (IsStopping) return false;
        if (images.IsRetiring || Array.Exists(_retiring, image => ReferenceEquals(image, images)))
            throw new InvalidOperationException("A retiring scene cannot become current.");
        if (ReferenceEquals(_current, images)) return true;
        if (_current is not null)
        {
            var slot = Array.FindIndex(_retiring, image => image is null);
            if (slot < 0) return false;
            _retiring[slot] = _current;
        }
        _current = images;
        _prepared = false;
        return true;
    }

    // Apply under the composition owner's serialization. Image retention is
    // completed before mutating the renderer; acknowledge only after both the
    // renderer and its indexed image bindings have accepted the same version.
    internal unsafe NativeGpuSceneApplyResult ApplyScene(NativeSceneLeaseV3 scene, NativeCanvasSceneRenderer renderer)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(renderer);
        if (IsStopping || (_current is not null && Array.TrueForAll(_retiring, image => image is not null)))
            return NativeGpuSceneApplyResult.Backpressure;
        var result = NativeGpuSceneApplyResult.InvalidScene;
        scene.WithView(view =>
        {
            const ulong supported = NativeWebSceneApi.GpuImageCapability | NativeWebSceneApi.OrderedCanvasCapability;
            if (view.SceneVersion != 3 || view.StructSize != System.Runtime.InteropServices.Marshal.SizeOf<NativeSceneViewV3>() ||
                (view.RequiredCapabilities & ~supported) != 0 || !NativeSceneViewValidation.IsValid((NativeSceneView*)view.CpuView)) return;
            var status = NativeMacOSGpuSceneImages.Acquire(scene, out var images);
            if (status == NativeSceneAcquireStatus.Backpressure) { result = NativeGpuSceneApplyResult.Backpressure; return; }
            if (status != NativeSceneAcquireStatus.Success || images is null)
                throw new InvalidOperationException($"Scene image retention failed: {status}");
            try
            {
                var cpu = (NativeSceneView*)view.CpuView;
                foreach (var command in new ReadOnlySpan<SceneCommand>(cpu->Commands, checked((int)cpu->Header.CommandCount)))
                    if (command.Kind == NativeWebSceneApi.GpuImagePaintCommand &&
                        ((view.RequiredCapabilities & NativeWebSceneApi.GpuImageCapability) == 0 || command.Rgba >= images.ImageCount)) return;
                if (!renderer.ApplyDiff((NativeSceneView*)view.CpuView, orderedGpuImages: (view.RequiredCapabilities & supported) != 0))
                { result = NativeGpuSceneApplyResult.RejectedDiff; return; }
                if (!TryReplace(images)) throw new InvalidOperationException("Scene replacement lost its serialized admission slot.");
                images = null; // Presenter now owns the bindings used by this renderer version.
                result = scene.Acknowledge() ? NativeGpuSceneApplyResult.Applied : NativeGpuSceneApplyResult.AcknowledgementFailed;
            }
            finally { images?.DiscardUnprepared(); }
        });
        return result;
    }

    // A visual that never imported an image can stop synchronously. Once an
    // import exists, shutdown must retain the visual's graphics retirement path.
    internal bool TryDiscardUnprepared()
    {
        if ((_current?.ImportedCount ?? 0) != 0 || Array.Exists(_retiring, image => (image?.ImportedCount ?? 0) != 0)) return false;
        IsStopping = true;
        _current?.DiscardUnprepared(); _current = null;
        for (var index = 0; index < _retiring.Length; ++index)
        { _retiring[index]?.DiscardUnprepared(); _retiring[index] = null; }
        _prepared = false;
        return true;
    }

    private void DrainRetirements(ISkiaSharpApiLease lease)
    {
        for (var index = 0; index < _retiring.Length; ++index)
        {
            if (_retiring[index] is not { } image) continue;
            image.Retire(lease);
            if (image.TryComplete(lease)) _retiring[index] = null;
        }
    }

    internal bool TryPrepare(ISkiaSharpApiLease lease)
    {
        if (IsStopping) throw new InvalidOperationException("Scene presenter is stopping.");
        DrainRetirements(lease);
        if (_current is null) return false;
        var before = _current.ImportedCount;
        try { return _prepared = _current.TryPrepare(lease); }
        finally { ImportedCount += _current.ImportedCount - before; }
    }

    internal void Draw(ISkiaSharpApiLease lease, uint index, SKRect destination)
    {
        if (IsStopping || !_prepared || _current is null)
            throw new InvalidOperationException("Scene presenter is not ready to draw.");
        _current.Draw(lease, index, destination);
    }

    // The host must continue graphics callbacks until TryComplete returns true,
    // including when ordinary scene rendering has stopped or is hidden.
    internal void BeginShutdown() => IsStopping = true;
    internal bool TryComplete(ISkiaSharpApiLease lease)
    {
        if (!IsStopping) throw new InvalidOperationException("Scene presenter has not begun shutdown.");
        DrainRetirements(lease);
        if (_current is not null)
        {
            _current.Retire(lease);
            if (_current.TryComplete(lease)) _current = null;
        }
        return !HasPendingRetirements;
    }
}

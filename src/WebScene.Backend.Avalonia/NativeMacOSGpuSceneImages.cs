using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Indexed image ownership for one immutable scene version. Retain/Acquire are
// CPU-only; preparation, drawing and retirement require the presenter's lease.
// Keep this owner until TryComplete succeeds. GC cannot certify GPU completion.
internal sealed class NativeMacOSGpuSceneImages
{
    private readonly NativeGpuImageLeaseV3?[] _sources;
    private readonly NativeMacOSRetainedGpuImage?[] _images;
    private readonly NativeGpuImageInfoV3[] _metadata;
    internal bool IsRetiring { get; private set; }
    internal int ImportedCount { get; private set; }
    private NativeMacOSGpuSceneImages(int count)
    {
        _sources = new NativeGpuImageLeaseV3?[count];
        _images = new NativeMacOSRetainedGpuImage?[count];
        _metadata = new NativeGpuImageInfoV3[count];
    }

    internal static NativeSceneAcquireStatus Acquire(NativeSceneLeaseV3 scene, out NativeMacOSGpuSceneImages? images)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return Capture(checked((int)scene.ImageCount),
            (int index, out NativeGpuImageLeaseV3? image) => NativeGpuImageLeaseV3.Acquire(scene, (uint)index, out image), out images);
    }

    internal static NativeSceneAcquireStatus Retain(IReadOnlyList<NativeGpuImageLeaseV3> sources,
        out NativeMacOSGpuSceneImages? images)
    {
        ArgumentNullException.ThrowIfNull(sources);
        return Capture(sources.Count, (int index, out NativeGpuImageLeaseV3? image) => sources[index].Retain(out image), out images);
    }

    private delegate NativeSceneAcquireStatus CaptureImage(int index, out NativeGpuImageLeaseV3? image);
    private static NativeSceneAcquireStatus Capture(int count, CaptureImage capture, out NativeMacOSGpuSceneImages? images)
    {
        images = null;
        var candidate = new NativeMacOSGpuSceneImages(count);
        try
        {
            for (var index = 0; index < count; ++index)
            {
                var status = capture(index, out candidate._sources[index]);
                if (status != NativeSceneAcquireStatus.Success) return status;
                var metadata = candidate._sources[index]!.Describe();
                if (metadata.Format != 2 || metadata.ColorSpace != 1 ||
                    metadata.Alpha is not (1 or 2) || metadata.Orientation is not (1 or 2))
                    throw new NotSupportedException("The macOS scene importer requires BGRA8 sRGB with opaque or premultiplied alpha.");
                candidate._metadata[index] = metadata;
            }
            images = candidate;
            return NativeSceneAcquireStatus.Success;
        }
        finally
        {
            if (images is null) candidate.ReleaseSources();
        }
    }

    private void ReleaseSources()
    {
        for (var index = 0; index < _sources.Length; ++index)
        {
            _sources[index]?.Dispose();
            _sources[index] = null;
        }
    }

    // Prepare every image before replaying the scene, so admission backpressure
    // cannot leave only part of its GPU content drawn. Completed imports survive
    // a retry and are never imported again for unchanged frames.
    internal bool TryPrepare(ISkiaSharpApiLease lease)
    {
        if (IsRetiring) throw new InvalidOperationException("A retiring scene cannot be prepared.");
        for (var index = 0; index < _sources.Length; ++index)
        {
            if (_images[index] is not null) continue;
            var metadata = _metadata[index];
            var image = NativeMacOSRetainedGpuImage.Import(_sources[index]!, lease,
                metadata.Orientation == 1 ? GRSurfaceOrigin.TopLeft : GRSurfaceOrigin.BottomLeft,
                metadata.Alpha == 1 ? SKAlphaType.Opaque : SKAlphaType.Premul);
            if (image is null) return false;
            _images[index] = image;
            _sources[index]!.Dispose(); _sources[index] = null;
            ++ImportedCount;
        }
        return true;
    }

    internal void Draw(ISkiaSharpApiLease lease, uint index, SKRect destination)
    {
        if (IsRetiring || index >= _images.Length || _images[index] is not { } image)
            throw new InvalidOperationException("Scene image is unavailable or retiring.");
        image.Draw(lease, destination);
    }

    internal void Retire(ISkiaSharpApiLease lease)
    {
        IsRetiring = true;
        ReleaseSources();
        foreach (var image in _images) image?.Retire(lease);
    }

    internal bool TryComplete(ISkiaSharpApiLease lease)
    {
        if (!IsRetiring) throw new InvalidOperationException("Scene retirement has not started.");
        var complete = true;
        for (var index = 0; index < _images.Length; ++index)
        {
            if (_images[index] is not { } image) continue;
            // Retry a retirement that was interrupted by a host lease failure.
            image.Retire(lease);
            if (image.TryComplete(lease)) _images[index] = null;
            else complete = false;
        }
        return complete;
    }
}

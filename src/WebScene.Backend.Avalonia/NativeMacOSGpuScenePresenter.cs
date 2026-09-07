using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Serialized by the composition owner. Replacement is CPU-only; all imported
// resources stay here until retirement completes under the host graphics lease.
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

using Avalonia.Skia;
using SkiaSharp;
namespace WebScene.Backends.Avalonia.Native;
internal interface INativeRetainedGpuImage
{
    void Draw(ISkiaSharpApiLease lease, SKRect destination, SKPaint? paint = null);
    void Retire(ISkiaSharpApiLease lease);
    bool TryComplete(ISkiaSharpApiLease lease);
    bool TryRetireWithoutVisual();
    // Called synchronously on the composition owner before detached polling.
    void SealForDetachedRetirement() { TryRetireWithoutVisual(); }
}

using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

internal static class NativeGpuImageSampling
{
#if !WEBSCENE_AVALONIA12
    // Immutable shared default: no paint allocation for each video frame.
    private static readonly SKPaint LinearPaint = new() { FilterQuality = SKFilterQuality.Low };
#endif

    internal static void Draw(SKCanvas canvas, SKImage image, SKRect destination, SKPaint? paint = null)
    {
        // Imported textures have no mip chain. Bilinear sampling smooths Retina
        // enlargement without CPU resizing, texture copies or per-frame mipmaps.
#if WEBSCENE_AVALONIA12
        canvas.DrawImage(image, destination, new SKSamplingOptions(SKFilterMode.Linear, SKMipmapMode.None), paint);
#else
        if (paint is null)
        {
            canvas.DrawImage(image, destination, LinearPaint);
            return;
        }
        // The active drawing lease owns the paint on this thread. Preserve its
        // opacity/blend settings and restore it after the synchronous draw.
        var quality = paint.FilterQuality;
        try
        {
            paint.FilterQuality = SKFilterQuality.Low;
            canvas.DrawImage(image, destination, paint);
        }
        finally { paint.FilterQuality = quality; }
#endif
    }
}

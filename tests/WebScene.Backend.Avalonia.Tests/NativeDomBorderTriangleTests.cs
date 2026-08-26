using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeDomBorderTriangleTests
{
    [Fact]
    public void TradingViewGeneratedBorderTrianglePreservesBothSwatchColors()
    {
        using var bitmap = new SKBitmap(24, 24, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        var baseColor = new SKColor(180, 67, 83);
        var triangleColor = new SKColor(53, 132, 120);
        canvas.Clear(baseColor);

        var command = new SceneCommand
        {
            Kind = 34,
            Flags = 1,
            X = 0,
            Y = 0,
            Width = 24,
            Height = 24,
            Rgba = 0x358478FF,
            RadiusTopLeft = 24,
            RadiusTopRight = 0,
            RadiusBottomRight = 0,
            RadiusBottomLeft = 24
        };

        NativeCanvasSceneRenderer.DrawDomBorderSidePolygonForTest(
            canvas,
            command);
        canvas.Flush();

        Assert.Equal(triangleColor, bitmap.GetPixel(20, 4));
        Assert.Equal(baseColor, bitmap.GetPixel(4, 20));
        Assert.NotEqual(bitmap.GetPixel(20, 4), bitmap.GetPixel(4, 20));
    }
}

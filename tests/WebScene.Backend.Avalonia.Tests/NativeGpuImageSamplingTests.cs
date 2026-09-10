using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeGpuImageSamplingTests
{
    [Theory]
    [InlineData(8, 1f)]
    [InlineData(8, 2f)]
    [InlineData(3, 1f)]
    public void ScaledTextureInterpolatesInsteadOfRepeatingNearestPixels(int width, float scale)
    {
        using var source = new SKBitmap(2, 2);
        source.Erase(SKColors.Black);
        source.SetPixel(1, 0, SKColors.White);
        source.SetPixel(1, 1, SKColors.White);
        using var image = SKImage.FromBitmap(source);
        using var result = new SKBitmap((int)(width * scale), (int)(width * scale));
        using var canvas = new SKCanvas(result);
        canvas.Scale(scale);
        NativeGpuImageSampling.Draw(canvas, image, new SKRect(0, 0, width, width));
        var middle = result.GetPixel((int)(width * scale / 2), 1).Red;
        Assert.InRange((int)middle, 32, 223);
    }

    [Fact]
    public void SamplingPreservesCallerOpacity()
    {
        using var source = new SKBitmap(2, 2);
        source.Erase(SKColors.White);
        using var image = SKImage.FromBitmap(source);
        using var result = new SKBitmap(8, 8);
        using var canvas = new SKCanvas(result);
        canvas.Clear(SKColors.Transparent);
        using var paint = new SKPaint { Color = new SKColor(255, 255, 255, 128) };
        NativeGpuImageSampling.Draw(canvas, image, new SKRect(0, 0, 8, 8), paint);
        Assert.InRange((int)result.GetPixel(4, 4).Alpha, 127, 129);
        Assert.Equal((byte)128, paint.Color.Alpha);
    }
}

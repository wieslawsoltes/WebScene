using WebScene.Backends.Avalonia.Native;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class NativeTextShapingTests
{
    [Fact]
    public void EmptyTextSkipsShapingAndDrawing()
    {
        using var bitmap = new SKBitmap(32, 32);
        using var canvas = new SKCanvas(bitmap);
        using var paint = new SKPaint { TextSize = 13 };
        using var shaper = new SKShaper(SKTypeface.Default);

        Assert.Equal(
            0,
            NativeTextShaping.MeasureShapedWidth(
                shaper,
                string.Empty,
                paint,
                featureFlags: 0));
        NativeTextShaping.DrawShapedText(
            canvas,
            shaper,
            string.Empty,
            x: 0,
            baseline: 16,
            paint,
            featureFlags: 0);
    }

    [Theory]
    [InlineData("\n")]
    [InlineData("\r\n")]
    [InlineData("\u200B")]
    public void TextWithoutDrawableGlyphsSkipsDrawing(string text)
    {
        using var bitmap = new SKBitmap(32, 32);
        using var canvas = new SKCanvas(bitmap);
        using var paint = new SKPaint { TextSize = 13 };
        using var shaper = new SKShaper(SKTypeface.Default);

        NativeTextShaping.DrawShapedText(
            canvas,
            shaper,
            text,
            x: 0,
            baseline: 16,
            paint,
            featureFlags: 0);
    }

    [Fact]
    public void MacSystemUiWidthScaleMatchesManagedRendererProfile()
    {
        var actual = NativeTextShaping.ResolveWidthScale(
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400);
        var expected = OperatingSystem.IsMacOS() ? 1.0408f : 1f;

        Assert.Equal(expected, actual, precision: 4);
        Assert.Equal(1f, NativeTextShaping.ResolveWidthScale("sans-serif", 13, 400));
    }

    [Fact]
    public void MacSystemUiNumericRunsUseEqualTabularAdvances()
    {
        var first = NativeTextShaping.Measure(
            "189.39",
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400,
            0,
            0);
        var second = NativeTextShaping.Measure(
            "190.79",
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400,
            0,
            0);

        if (OperatingSystem.IsMacOS())
        {
            Assert.Equal(first.AdvanceWidth, second.AdvanceWidth, precision: 3);
        }
    }
}

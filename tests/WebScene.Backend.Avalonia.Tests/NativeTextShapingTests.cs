using WebScene.Backends.Avalonia.Native;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

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

    [Fact]
    public void WebTypefacesAreSharedByContentButIsolatedByDocumentFamilyMap()
    {
        var (firstData, secondData) = FindDistinctPlatformFonts();
        var before = NativeTextShaping.GetWebTypefaceCacheMetrics();
        using var firstDocument = NativeTextShaping.CreateWebTypefaceRegistry();
        using var sameContentDocument = NativeTextShaping.CreateWebTypefaceRegistry();
        using var secondDocument = NativeTextShaping.CreateWebTypefaceRegistry();

        Assert.True(firstDocument.Register("Shared Family", firstData));
        Assert.True(sameContentDocument.Register("Shared Family", firstData));
        Assert.True(secondDocument.Register("Shared Family", secondData));

        var first = NativeTextShaping.ResolveTypeface(
            "Shared Family",
            400,
            firstDocument);
        var shared = NativeTextShaping.ResolveTypeface(
            "Shared Family",
            400,
            sameContentDocument);
        var isolated = NativeTextShaping.ResolveTypeface(
            "Shared Family",
            400,
            secondDocument);
        var current = NativeTextShaping.GetWebTypefaceCacheMetrics();

        Assert.Same(first, shared);
        Assert.NotSame(first, isolated);
        Assert.NotEqual(first.FamilyName, isolated.FamilyName);
        Assert.True(current.Hits > before.Hits);
        Assert.True(current.Entries >= before.Entries + 2);
        Assert.True(current.References >= before.References + 3);

        firstDocument.Dispose();
        sameContentDocument.Dispose();
        secondDocument.Dispose();
        var released = NativeTextShaping.GetWebTypefaceCacheMetrics();
        Assert.True(released.Entries <= current.Entries - 2);
        Assert.True(released.References <= current.References - 3);
    }

    private static (byte[] First, byte[] Second) FindDistinctPlatformFonts()
    {
        string[] roots = OperatingSystem.IsWindows()
            ? [Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                "Fonts")]
            : OperatingSystem.IsMacOS()
                ? ["/System/Library/Fonts", "/Library/Fonts"]
                : ["/usr/share/fonts", "/usr/local/share/fonts"];
        byte[]? first = null;
        string? firstFamily = null;
        foreach (var path in roots
                     .Where(Directory.Exists)
                     .SelectMany(root => Directory.EnumerateFiles(
                         root,
                         "*.*",
                         SearchOption.AllDirectories))
                     .Where(path => path.EndsWith(".ttf", StringComparison.OrdinalIgnoreCase)
                                    || path.EndsWith(".otf", StringComparison.OrdinalIgnoreCase)))
        {
            var data = File.ReadAllBytes(path);
            using var skData = SKData.CreateCopy(data);
            using var typeface = SKTypeface.FromData(skData);
            if (typeface is null) continue;
            if (first is null)
            {
                first = data;
                firstFamily = typeface.FamilyName;
                continue;
            }
            if (!string.Equals(
                    firstFamily,
                    typeface.FamilyName,
                    StringComparison.Ordinal))
            {
                return (first, data);
            }
        }
        throw new InvalidOperationException(
            "The test platform does not expose two distinct OpenType font fixtures.");
    }
}

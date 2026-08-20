using WebScene.Backends.Avalonia.Native;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeTextShapingTests
{
    [Theory]
    [InlineData(0, false, true)]
    [InlineData(1, false, true)]
    [InlineData(1.49, false, true)]
    [InlineData(1.5, true, false)]
    [InlineData(2, true, false)]
    public void FontRasterizationProfileTracksPresenterScale(
        float deviceScaleFactor,
        bool expectedSubpixel,
        bool expectedBaselineSnap)
    {
        var profile = NativeTextShaping.ResolveFontRasterizationProfile(deviceScaleFactor);

        Assert.Equal(expectedSubpixel, profile.Subpixel);
        Assert.Equal(expectedBaselineSnap, profile.BaselineSnap);
    }

    [Fact]
    public void CanvasMiddleBaselineUsesPositionedRunOrigin()
    {
        using var paint = new SKPaint { TextSize = 12, Typeface = SKTypeface.Default };
        var metrics = paint.FontMetrics;

        Assert.Equal(
            0,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("middle", metrics));
        Assert.Equal(
            -metrics.Top,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("top", metrics));
        Assert.Equal(
            -metrics.Bottom,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("bottom", metrics));
    }

    [Theory]
    [InlineData("10px sans-serif", 10, 400, SKFontStyleSlant.Upright, "sans-serif")]
    [InlineData("bold 12px -apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif", 12, 700, SKFontStyleSlant.Upright, "-apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif")]
    [InlineData("italic 650 14.5px/20px 'Example Sans', serif", 14.5, 650, SKFontStyleSlant.Italic, "'Example Sans', serif")]
    [InlineData("oblique lighter 16px / 24px monospace", 16, 300, SKFontStyleSlant.Oblique, "monospace")]
    public void CanvasFontParserRetainsRenderingAxesAndFallbackList(
        string shorthand,
        float size,
        int weight,
        SKFontStyleSlant slant,
        string familyList)
    {
        Assert.True(NativeTextShaping.TryParseCanvasFont(shorthand, out var parsed));
        Assert.Equal(size, parsed.Size);
        Assert.Equal(weight, parsed.Weight);
        Assert.Equal(slant, parsed.Slant);
        Assert.Equal(familyList, parsed.FamilyList);
    }

    [Theory]
    [InlineData("")]
    [InlineData("bold sans-serif")]
    [InlineData("12px")]
    [InlineData("0px sans-serif")]
    public void CanvasFontParserRejectsIncompleteOrInvalidShorthands(string shorthand)
    {
        Assert.False(NativeTextShaping.TryParseCanvasFont(shorthand, out _));
    }

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
    public void MacSystemUiLatinRunsRetainPlatformAdvanceAndSelectedWeight()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        var normal = NativeTextShaping.Measure(
            "Navigation",
            family,
            14,
            400,
            0,
            0);
        var bold = NativeTextShaping.Measure(
            "Navigation",
            family,
            14,
            700,
            0,
            0);
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = 14
        };
        var platformAdvance = paint.MeasureText("Navigation");

        Assert.InRange(normal.AdvanceWidth / platformAdvance, 0.94f, 1.01f);
        Assert.True(
            bold.AdvanceWidth > normal.AdvanceWidth * 1.05f,
            $"Selected weight did not affect the system-font advance: {normal.AdvanceWidth}/{bold.AdvanceWidth}");
    }

    [Fact]
    public void MacSystemUiMixedNumericTextUsesPlatformAdvance()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 12 };
        using var shaper = new SKShaper(typeface);
        const string text = "Sample 100.00 Status";
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            text,
            paint,
            featureFlags: 0);

        var widthScale = NativeTextShaping.ResolveShapedWidthScale(
            text,
            family,
            12,
            400,
            paint,
            shapedWidth,
            featureFlags: 0);
        var platformWidth = paint.MeasureText(text);

        Assert.NotEqual(1f, widthScale);
        Assert.InRange(shapedWidth * widthScale / platformWidth, .96f, 1.01f);
    }

    [Fact]
    public void MacSystemUiLatinRunWithTypographicApostropheUsesPlatformAdvance()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        const string text = "It’ll open fully in 25 minutes.";
        var typeface = NativeTextShaping.ResolveTypeface(family, 700);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            text,
            paint,
            featureFlags: 0);

        var widthScale = NativeTextShaping.ResolveShapedWidthScale(
            text,
            family,
            14,
            700,
            paint,
            shapedWidth,
            featureFlags: 0);
        var platformWidth = paint.MeasureText(text);

        Assert.NotEqual(1f, widthScale);
        Assert.InRange(shapedWidth * widthScale / platformWidth, .96f, 1.01f);
    }

    [Fact]
    public void MacSystemUiCollapsedSpaceUsesPlatformAdvance()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            " ",
            paint,
            featureFlags: 0);

        var widthScale = NativeTextShaping.ResolveShapedWidthScale(
            " ",
            family,
            14,
            400,
            paint,
            shapedWidth,
            featureFlags: 0);
        var platformWidth = paint.MeasureText(" ");

        Assert.True(widthScale > 1.2f, $"Collapsed-space scale was {widthScale}.");
        Assert.InRange(shapedWidth * widthScale / platformWidth, .96f, 1.01f);
    }

    [Fact]
    public void MacSystemUiNonLatinRunDoesNotUseLatinAdvanceCalibration()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        const string text = "مرحبا";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            text,
            paint,
            featureFlags: 0);

        var widthScale = NativeTextShaping.ResolveShapedWidthScale(
            text,
            family,
            14,
            400,
            paint,
            shapedWidth,
            featureFlags: 0);

        Assert.Equal(1f, widthScale);
    }

    [Fact]
    public void TextMetricsInkBoundsFollowTheShapedGlyphRun()
    {
        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        const string text = "Ex Date: Mon Aug 10 2026";
        var measured = NativeTextShaping.Measure(text, family, 12, 400, 0, 0);

        Assert.True(float.IsFinite(measured.ActualBoundingBoxLeft));
        Assert.True(float.IsFinite(measured.ActualBoundingBoxRight));
        Assert.InRange(
            measured.ActualBoundingBoxRight,
            measured.AdvanceWidth * .85f,
            measured.AdvanceWidth * 1.15f);
    }

    [Fact]
    public void AdvanceCalibrationDoesNotStretchGlyphOutlines()
    {
        using var paint = new SKPaint { TextSize = 16, Typeface = SKTypeface.Default };
        using var shaper = new SKShaper(paint.Typeface);
        var natural = NativeTextShaping.MeasureShapedInkBounds(
            shaper, "II", paint, 0);
        var expanded = NativeTextShaping.MeasureShapedInkBounds(
            shaper, "II", paint, 0, horizontalAdvanceScale: 1.5f);

        Assert.Equal(natural.Height, expanded.Height, precision: 3);
        Assert.True(expanded.Width > natural.Width);
        Assert.True(expanded.Width < natural.Width * 1.5f);
    }

    [Fact]
    public void CanvasTextMaxWidthCondensesOnlyOversizedRuns()
    {
        const uint textMaxWidthFlag = 1u << 17;

        Assert.Equal(
            .5f,
            NativeCanvasSceneRenderer.ConstrainCanvasTextWidth(
                1f,
                100f,
                textMaxWidthFlag,
                50),
            precision: 4);
        Assert.Equal(
            1f,
            NativeCanvasSceneRenderer.ConstrainCanvasTextWidth(
                1f,
                40f,
                textMaxWidthFlag,
                50));
        Assert.Equal(
            1f,
            NativeCanvasSceneRenderer.ConstrainCanvasTextWidth(
                1f,
                100f,
                0,
                10));
        Assert.Equal(
            0f,
            NativeCanvasSceneRenderer.ConstrainCanvasTextWidth(
                1f,
                100f,
                textMaxWidthFlag,
                0));
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

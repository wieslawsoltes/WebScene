using System.Globalization;
using WebScene.Backends.Avalonia.Native;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

[Collection("Native web-font cache")]
public sealed class NativeTextShapingTests
{
    [Fact]
    public void WindowsGenericFamiliesKeepSystemUiAndSansSerifDistinct()
    {
        if (!OperatingSystem.IsWindows()) return;

        var systemUi = NativeTextShaping.ResolveTypeface("system-ui", 400);
        var sansSerif = NativeTextShaping.ResolveTypeface("sans-serif", 400);
        var appleFallback = NativeTextShaping.ResolveTypeface(
            "-apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif",
            400);

        Assert.Equal("Segoe UI", systemUi.FamilyName, ignoreCase: true);
        Assert.Equal("Arial", sansSerif.FamilyName, ignoreCase: true);
        Assert.NotEqual(systemUi.FamilyName, sansSerif.FamilyName);
        Assert.Equal("Trebuchet MS", appleFallback.FamilyName, ignoreCase: true);
    }

    [Theory]
    [InlineData(400, SKFontStyleSlant.Upright)]
    [InlineData(600, SKFontStyleSlant.Upright)]
    [InlineData(700, SKFontStyleSlant.Upright)]
    [InlineData(400, SKFontStyleSlant.Italic)]
    public void WindowsSystemUiResolvesRequestedWeightAndStyle(
        int weight,
        SKFontStyleSlant slant)
    {
        if (!OperatingSystem.IsWindows()) return;

        var typeface = NativeTextShaping.ResolveTypeface(
            "missing-webscene-family, system-ui",
            weight,
            slant,
            null);

        Assert.Equal("Segoe UI", typeface.FamilyName, ignoreCase: true);
        Assert.Equal(slant, typeface.FontSlant);
        Assert.InRange(typeface.FontWeight, weight - 100, weight + 100);
    }

    [Fact]
    public void WindowsDirectWritePositionerReturnsVerifiedCachedWholeRun()
    {
        if (!OperatingSystem.IsWindows()) return;

        const string family = "system-ui";
        const string text = "AAPL data is delayed by 15 minutes.";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(text, 0, 0, paint);
        var request = new NativeTextRunPositionRequest(
            text,
            family,
            14,
            400,
            SKFontStyleSlant.Upright,
            0,
            shaped.Codepoints,
            null,
            typeface);
        var positioner = new WindowsDirectWriteRunPositioner();

        Assert.True(positioner.TryPosition(in request, out var first));
        Assert.Equal(shaped.Codepoints, first.Glyphs.Select(static glyph => (uint)glyph));
        Assert.Equal(first.Glyphs.Length, first.Positions.Length);
        Assert.Equal(first.Glyphs.Length, first.Clusters?.Length);
        Assert.Equal(first.Glyphs.Length, first.Advances?.Length);
        Assert.Equal("Segoe UI", first.FaceIdentity?.FamilyName, ignoreCase: true);
        Assert.False(string.IsNullOrWhiteSpace(first.FaceIdentity?.FontTableFingerprint));
        Assert.True(float.IsFinite(first.AdvanceWidth));
        Assert.True(first.AdvanceWidth > 0);
        Assert.True(positioner.TryPosition(in request, out var second));
        Assert.Same(first, second);
        var mismatched = request with
        {
            ExpectedGlyphs = request.ExpectedGlyphs
                .Select((glyph, index) => index == 0 ? glyph + 1 : glyph)
                .ToArray()
        };
        Assert.False(positioner.TryPosition(in mismatched, out _));
    }

    [Fact]
    public void WindowsDirectWriteIsAutomaticAndHarfBuzzRemainsRollback()
    {
        if (!OperatingSystem.IsWindows()) return;

        const string text = "AV To ffi";
        var typeface = NativeTextShaping.ResolveTypeface("system-ui", 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 18 };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(text, 0, 0, paint);
        var request = new NativeTextRunPositionRequest(
            text, "system-ui", 18, 400, SKFontStyleSlant.Upright,
            0, shaped.Codepoints, null, typeface);
        var automatic = new DefaultNativeTextRunPositioner(null);
        var explicitCandidate = new DefaultNativeTextRunPositioner("directwrite");
        var rollback = new DefaultNativeTextRunPositioner("harfbuzz");

        Assert.True(automatic.IsEligible(in request));
        Assert.True(explicitCandidate.IsEligible(in request));
        Assert.True(automatic.TryPosition(in request, out var positioned));
        Assert.Equal(positioned.AdvanceWidth, positioned.Advances!.Sum(), precision: 4);
        if (string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable(
                NativeTextShaping.RasterizationModeEnvironmentVariable)))
        {
            Assert.Equal(
                NativeTextShaping.NativeFontRasterizationMode.ChromiumGrayscale,
                NativeTextShaping.ResolvePositionedRunRasterizationMode(
                    positioned,
                    1,
                    null));
            Assert.Null(NativeTextShaping.ResolvePositionedRunRasterizationMode(
                positioned,
                1.25f,
                null));
        }
        Assert.Equal(
            NativeTextShaping.NativeFontRasterizationMode.Current,
            NativeTextShaping.ResolvePositionedRunRasterizationMode(
                positioned,
                1,
                NativeTextShaping.NativeFontRasterizationMode.Current));
        Assert.False(rollback.IsEligible(in request));
        Assert.False(rollback.TryPosition(in request, out _));
    }

    [Fact]
    public void WindowsDirectWritePositionerPreservesPairKerning()
    {
        if (!OperatingSystem.IsWindows()) return;

        var typeface = NativeTextShaping.ResolveTypeface("system-ui", 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 24 };
        using var shaper = new SKShaper(typeface);
        var positioner = new WindowsDirectWriteRunPositioner();

        NativePositionedTextRun Position(string text)
        {
            var shaped = shaper.Shape(text, 0, 0, paint);
            var request = new NativeTextRunPositionRequest(
                text, "system-ui", 24, 400, SKFontStyleSlant.Upright,
                0, shaped.Codepoints, null, typeface);
            Assert.True(positioner.TryPosition(in request, out var run));
            return run;
        }

        var pair = Position("AV");
        var isolated = Position("A").AdvanceWidth + Position("V").AdvanceWidth;
        var space = Position(" ");

        Assert.True(pair.AdvanceWidth < isolated - .05f);
        Assert.True(space.AdvanceWidth > 0);
    }

    [Fact]
    public void WindowsDirectWritePositionerRejectsUnsupportedRuns()
    {
        if (!OperatingSystem.IsWindows()) return;

        var typeface = NativeTextShaping.ResolveTypeface("system-ui", 400);
        var positioner = new WindowsDirectWriteRunPositioner();
        var italic = new NativeTextRunPositionRequest(
            "text", "system-ui", 14, 400, SKFontStyleSlant.Italic, 0, [], null, typeface);
        var tabular = new NativeTextRunPositionRequest(
            "123", "system-ui", 14, 400, SKFontStyleSlant.Upright,
            NativeTextShaping.TabularNumerals, [], null, typeface);
        var emoji = new NativeTextRunPositionRequest(
            "emoji 😀", "system-ui", 14, 400, SKFontStyleSlant.Upright,
            0, [], null, typeface);
        var namedFamily = new NativeTextRunPositionRequest(
            "text", "Arial", 14, 400, SKFontStyleSlant.Upright,
            0, [], null, typeface);
        var semibold = new NativeTextRunPositionRequest(
            "text", "system-ui", 14, 600, SKFontStyleSlant.Upright,
            0, [], null, typeface);

        Assert.False(positioner.IsEligible(in italic));
        Assert.False(positioner.IsEligible(in tabular));
        Assert.False(positioner.IsEligible(in emoji));
        Assert.False(positioner.IsEligible(in namedFamily));
        Assert.False(positioner.IsEligible(in semibold));
    }

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
        var profile = NativeTextShaping.ResolveFontRasterizationProfile(
            deviceScaleFactor,
            NativeTextShaping.NativeFontRasterizationMode.Current);

        Assert.Equal(expectedSubpixel, profile.Subpixel);
        Assert.Equal(expectedBaselineSnap, profile.BaselineSnap);
    }

    [Fact]
    public void ChromiumRasterizationProfilesMatchBlinkMacSettings()
    {
        var automatic = NativeTextShaping.ResolveFontRasterizationProfile(
            2,
            NativeTextShaping.NativeFontRasterizationMode.Chromium);
        Assert.True(automatic.Subpixel);
        Assert.False(automatic.BaselineSnap);
        Assert.Equal(SKFontEdging.SubpixelAntialias, automatic.Edging);
        Assert.Equal(SKFontHinting.Normal, automatic.Hinting);
        Assert.True(automatic.LinearMetrics);
        Assert.False(automatic.EmbeddedBitmaps);

        var antialiased = NativeTextShaping.ResolveFontRasterizationProfile(
            2,
            NativeTextShaping.NativeFontRasterizationMode.ChromiumAntialiased);
        Assert.Equal(SKFontEdging.Antialias, antialiased.Edging);
        Assert.Equal(SKFontHinting.None, antialiased.Hinting);
    }

    [Theory]
    [InlineData("chromium", "Chromium")]
    [InlineData("chrome-grayscale", "ChromiumGrayscale")]
    [InlineData("no-hint", "ChromiumAntialiased")]
    [InlineData("unknown", "Current")]
    public void RasterizationModeParsingIsStable(
        string value,
        string expected)
        => Assert.Equal(
            expected,
            NativeTextShaping.ParseFontRasterizationMode(value).ToString());

    [Theory]
    [InlineData("antialiased", "ChromiumAntialiased")]
    [InlineData("subpixel-antialiased", "Chromium")]
    [InlineData("auto", null)]
    [InlineData("inherit", null)]
    public void CssFontSmoothingSelectsPerRunRasterization(
        string value,
        string? expected)
        => Assert.Equal(
            expected,
            NativeTextShaping.ResolveCssFontSmoothingRasterizationMode(value, isMacOS: true)
                ?.ToString());

    [Fact]
    public void CssFontSmoothingDoesNotOverrideOtherPlatformProfiles()
        => Assert.Null(
            NativeTextShaping.ResolveCssFontSmoothingRasterizationMode(
                "antialiased",
                isMacOS: false));

    [Fact]
    public void CanvasMiddleBaselineCentersRotatedTrendlineLabelsAtTheirAuthoredOffset()
    {
        using var paint = new SKPaint { TextSize = 12, Typeface = SKTypeface.Default };
        var metrics = paint.FontMetrics;

        Assert.Equal(
            -(metrics.Ascent + metrics.Descent) * 0.5f,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("middle", metrics));
        Assert.Equal(
            -metrics.Top,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("top", metrics));
        Assert.Equal(
            -metrics.Bottom,
            NativeCanvasSceneRenderer.ResolveCanvasTextBaselineOffset("bottom", metrics));
    }

    [Theory]
    [InlineData("", 1, 1, 9, 0)]
    [InlineData("xMidYMid meet", 1, 1, 9, 0)]
    [InlineData("xMinYMin meet", 1, 1, 0, 0)]
    [InlineData("xMaxYMax meet", 1, 1, 18, 0)]
    [InlineData("none", 2, 1, 0, 0)]
    [InlineData("xMidYMid slice", 2, 2, 0, -9)]
    public void SvgViewportMappingPreservesOrExplicitlyStretchesTheViewBox(
        string preserveAspectRatio,
        float scaleX,
        float scaleY,
        float offsetX,
        float offsetY)
    {
        var mapping = NativeCanvasSceneRenderer.ResolveSvgViewportTransform(
            viewportWidth: 36,
            viewportHeight: 18,
            viewBoxWidth: 18,
            viewBoxHeight: 18,
            preserveAspectRatio);

        Assert.Equal(scaleX, mapping.ScaleX);
        Assert.Equal(scaleY, mapping.ScaleY);
        Assert.Equal(offsetX, mapping.OffsetX);
        Assert.Equal(offsetY, mapping.OffsetY);
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
    [InlineData("II")]
    [InlineData("TSLA")]
    [InlineData("tsla")]
    public void CssLetterSpacingMovesWholeShapedRunAndMatchesMeasuredAdvance(string text)
    {
        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        const float fontSize = 24;
        const float letterSpacing = 12;
        using var normalBitmap = new SKBitmap(160, 40);
        using var spacedBitmap = new SKBitmap(160, 40);
        using var normalCanvas = new SKCanvas(normalBitmap);
        using var spacedCanvas = new SKCanvas(spacedBitmap);
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Color = SKColors.White,
            TextSize = fontSize
        };

        normalCanvas.Clear(SKColors.Transparent);
        var normalAdvance = NativeTextShaping.DrawCssSpacedText(
            normalCanvas,
            text,
            4,
            30,
            family,
            fontSize,
            400,
            0,
            0,
            0,
            paint,
            registry: null,
            deviceScaleFactor: 1,
            rasterizationMode: null);
        spacedCanvas.Clear(SKColors.Transparent);
        var spacedAdvance = NativeTextShaping.DrawCssSpacedText(
            spacedCanvas,
            text,
            4,
            30,
            family,
            fontSize,
            400,
            letterSpacing,
            0,
            0,
            paint,
            registry: null,
            deviceScaleFactor: 1,
            rasterizationMode: null);
        var measured = NativeTextShaping.Measure(
            text,
            family,
            fontSize,
            400,
            letterSpacing,
            0);

        var expectedPaintShift = letterSpacing
            * (StringInfo.ParseCombiningCharacters(text).Length - 1);
        Assert.Equal(expectedPaintShift, spacedAdvance - normalAdvance, precision: 3);
        Assert.Equal(measured.AdvanceWidth, spacedAdvance, precision: 3);
        Assert.True(
            RightmostInk(spacedBitmap) - RightmostInk(normalBitmap)
                >= expectedPaintShift - 4,
            $"letter-spacing did not move the whole shaped '{text}' run");
        if (OperatingSystem.IsMacOS())
        {
            var typeface = NativeTextShaping.ResolveTypeface(family, 400);
            using var shaper = new SKShaper(typeface);
            paint.Typeface = typeface;
            Assert.True(NativeTextShaping.TryPositionTextRun(
                shaper,
                text,
                family,
                fontSize,
                400,
                SKFontStyleSlant.Upright,
                0,
                paint,
                registry: null,
                out var positioned));
            Assert.NotNull(positioned.Clusters);
            Assert.Equal(positioned.Glyphs.Length, positioned.Clusters!.Length);
        }

        static int RightmostInk(SKBitmap bitmap)
        {
            for (var x = bitmap.Width - 1; x >= 0; x--)
            {
                for (var y = 0; y < bitmap.Height; y++)
                {
                    if (bitmap.GetPixel(x, y).Alpha != 0) return x;
                }
            }
            return -1;
        }
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
    public void MacSystemUiCommandKeyShortcutUsesPlatformAdvance()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family =
            "-apple-system, system-ui, 'Trebuchet MS', Roboto, Ubuntu, sans-serif";
        const string text =
            "Press and hold ⌘ while zooming to maintain the chart position";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        var measured = NativeTextShaping.Measure(
            text,
            family,
            14,
            400,
            0,
            0);
        var platformAdvance = paint.MeasureText(text);

        Assert.True(NativeTextShaping.UsesMacSystemUiPlatformAdvances(text));
        Assert.InRange(measured.AdvanceWidth / platformAdvance, .93f, 1.01f);
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
    public void MacCoreTextPositionerReturnsVerifiedCachedSystemFontRun()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        const string text = "AAPL data is delayed by 15 minutes.";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(text, 0, 0, paint);
        var request = new NativeTextRunPositionRequest(
            text,
            family,
            14,
            400,
            SKFontStyleSlant.Upright,
            0,
            shaped.Codepoints,
            null);
        var positioner = new MacCoreTextRunPositioner();

        Assert.True(positioner.TryPosition(in request, out var first));
        Assert.Equal(shaped.Codepoints.Length, first.Glyphs.Length);
        Assert.Equal(first.Glyphs.Length, first.Positions.Length);
        Assert.True(float.IsFinite(first.AdvanceWidth));
        Assert.True(first.AdvanceWidth > 0);
        Assert.True(positioner.TryPosition(in request, out var second));
        Assert.Same(first, second);
    }

    [Fact]
    public void MacCoreTextPositionerPreservesDefaultPairKerning()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 24 };
        using var shaper = new SKShaper(typeface);
        var positioner = new MacCoreTextRunPositioner();

        NativePositionedTextRun Position(string text)
        {
            var shaped = shaper.Shape(text, 0, 0, paint);
            var request = new NativeTextRunPositionRequest(
                text,
                family,
                24,
                400,
                SKFontStyleSlant.Upright,
                0,
                shaped.Codepoints,
                null);
            Assert.True(positioner.TryPosition(in request, out var run));
            return run;
        }

        var pair = Position("AV");
        var isolatedWidth = Position("A").AdvanceWidth + Position("V").AdvanceWidth;

        Assert.True(
            pair.AdvanceWidth < isolatedWidth - .05f,
            $"Expected the AV pair ({pair.AdvanceWidth}) to kern below isolated width "
            + $"({isolatedWidth}).");
    }

    [Fact]
    public void MacCoreTextPositionerFallsBackForTabularAndUnsupportedRuns()
    {
        if (!OperatingSystem.IsMacOS()) return;

        const string family = "-apple-system, BlinkMacSystemFont, sans-serif";
        var typeface = NativeTextShaping.ResolveTypeface(family, 400);
        using var paint = new SKPaint { Typeface = typeface, TextSize = 14 };
        using var shaper = new SKShaper(typeface);
        var positioner = new MacCoreTextRunPositioner();
        var digits = shaper.Shape("189.39", 0, 0, paint);
        var tabular = new NativeTextRunPositionRequest(
            "189.39",
            family,
            14,
            400,
            SKFontStyleSlant.Upright,
            NativeTextShaping.TabularNumerals,
            digits.Codepoints,
            null);
        var arabic = shaper.Shape("مرحبا", 0, 0, paint);
        var unsupported = new NativeTextRunPositionRequest(
            "مرحبا",
            family,
            14,
            400,
            SKFontStyleSlant.Upright,
            0,
            arabic.Codepoints,
            null);

        Assert.False(positioner.TryPosition(in tabular, out _));
        Assert.False(positioner.TryPosition(in unsupported, out _));
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
    public void ClipboardToastEmojiUsesCoveredFallbackRunForMeasureAndPaint()
    {
        const string text = "Chart image copied to clipboard 👍";
        const string family = "-apple-system, BlinkMacSystemFont, 'Trebuchet MS', sans-serif";
        Assert.True(NativeTextShaping.TryResolveFallbackTextRuns(
            text,
            family,
            400,
            SKFontStyleSlant.Upright,
            null,
            out var runs));
        Assert.Equal(text, string.Concat(runs.Select(static run => run.Text)));
        var emojiRun = Assert.Single(runs.Where(static run =>
            run.Text.Contains("👍", StringComparison.Ordinal)));
        Assert.True(emojiRun.Typeface.ContainsGlyphs("👍"));
        if (OperatingSystem.IsMacOS())
        {
            Assert.Contains("Emoji", emojiRun.Typeface.FamilyName, StringComparison.OrdinalIgnoreCase);
        }

        using var paint = new SKPaint
        {
            IsAntialias = true,
            Color = SKColors.White,
            TextSize = 24
        };
        var layout = NativeTextShaping.LayoutFallbackTextRuns(
            runs,
            family,
            24,
            400,
            0,
            paint,
            null);
        var measured = NativeTextShaping.Measure(text, family, 24, 400, 0, 0);
        Assert.Equal(measured.AdvanceWidth, layout.AdvanceWidth, precision: 2);

        using var bitmap = new SKBitmap(420, 52, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        var cursor = 2f;
        var emojiLeft = 0f;
        var emojiRight = 0f;
        foreach (var run in layout.Runs)
        {
            if (run.Text.Contains("👍", StringComparison.Ordinal))
            {
                emojiLeft = cursor;
                emojiRight = cursor + run.AdvanceWidth;
            }
            paint.Typeface = run.Typeface;
            using var shaper = new SKShaper(run.Typeface);
            NativeTextShaping.DrawShapedText(
                canvas,
                shaper,
                run.Text,
                cursor,
                36,
                paint,
                0,
                horizontalAdvanceScale: run.WidthScale,
                measuredWidth: run.AdvanceWidth / run.WidthScale);
            cursor += run.AdvanceWidth;
        }
        canvas.Flush();
        var paintedEmojiPixels = 0;
        for (var y = 0; y < bitmap.Height; y++)
        {
            for (var x = Math.Max(0, (int)MathF.Floor(emojiLeft));
                 x < Math.Min(bitmap.Width, (int)MathF.Ceiling(emojiRight)); x++)
            {
                if (bitmap.GetPixel(x, y).Alpha > 0) paintedEmojiPixels++;
            }
        }
        Assert.True(paintedEmojiPixels > 20,
            $"emoji fallback run painted only {paintedEmojiPixels} pixels");
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

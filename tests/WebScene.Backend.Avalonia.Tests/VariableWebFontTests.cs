using SkiaSharp;
using SkiaSharp.HarfBuzz;
#if WEBSCENE_UNO
using WebScene.Backends.Uno.Native;
// Exercise the same legacy Skia APIs used by the shared production renderer.
#pragma warning disable CS0618
#else
using WebScene.Backends.Avalonia.Native;
#endif
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

[Collection("Native web-font cache")]
public sealed class VariableWebFontTests
{
    [Theory]
    [InlineData(null, true)]
    [InlineData("", true)]
    [InlineData("1", true)]
    [InlineData("0", false)]
    public void InstancingIsEnabledByDefaultWithAnExplicitOptOut(string? value, bool expected)
        => Assert.Equal(expected, NativeTextShaping.ResolveVariableFontInstancingEnabled(value));

    [Fact]
    public void DefaultRegistryHonorsTheProcessOptOut()
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry();
        registry.Register("Default policy", FontData());
        var expected = Environment.GetEnvironmentVariable("WEBSCENE_VARIABLE_FONT_INSTANCING") == "0" ? 400 : 700;
        Assert.Equal(expected, Weight(NativeTextShaping.ResolveTypeface("Default policy", 700, registry)));
    }

    [Theory]
    [InlineData("Roboto-Variable.ttf", 400)]
    [InlineData("Roboto-Variable.ttf", 550)]
    [InlineData("Roboto-Variable.ttf", 700)]
    [InlineData("Roboto-Variable.woff2", 400)]
    [InlineData("Roboto-Variable.woff2", 550)]
    [InlineData("Roboto-Variable.woff2", 700)]
    public void OutlinesAndMetricsMatchIndependentFontToolsInstances(string file, int weight)
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
        registry.Register("Variable", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", file)));
        var actual = NativeTextShaping.ResolveTypeface("Variable", weight, registry);
        using var data = SKData.CreateCopy(File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", $"Roboto-{weight}.ttf")));
        using var reference = SKTypeface.FromData(data);
        using var actualPaint = new SKPaint { Typeface = actual, TextSize = 28 };
        using var referencePaint = new SKPaint { Typeface = reference, TextSize = 28 };
        Assert.Equal(Weight(reference), Weight(actual));
        Assert.InRange(Math.Abs(Ink(actualPaint) - Ink(referencePaint)), 0, 100);
        using var actualOutline = actualPaint.GetTextPath("AV To ffi Release Notes", 0, 0);
        using var referenceOutline = referencePaint.GetTextPath("AV To ffi Release Notes", 0, 0);
        Assert.Equal(referenceOutline.PointCount, actualOutline.PointCount);
        var actualPoints = actualOutline.Points;
        var referencePoints = referenceOutline.Points;
        for (var i = 0; i < actualPoints.Length; i++)
        {
            Assert.InRange(Math.Abs(actualPoints[i].X - referencePoints[i].X), 0, 0.02f);
            Assert.InRange(Math.Abs(actualPoints[i].Y - referencePoints[i].Y), 0, 0.02f);
        }
        using var a = new SKShaper(actual);
        using var b = new SKShaper(reference);
        Assert.Equal(b.Shape("AV To ffi Release Notes", referencePaint).Width,
            a.Shape("AV To ffi Release Notes", actualPaint).Width, precision: 2);
    }

    [Fact]
    public void ConcurrentAndRepeatedRequestsConvertOnceAndRendererLeaseKeepsFaceAlive()
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
        registry.Register("Concurrent", FontData());
        using var rendererReference = registry.Retain();
        var before = NativeTextShaping.GetVariableFontMetrics();
        var faces = new SKTypeface[64];
        Parallel.For(0, faces.Length, i => faces[i] = NativeTextShaping.ResolveTypeface("Concurrent", 700, registry));
        Assert.All(faces, face => Assert.Same(faces[0], face));
        Assert.Equal(before.Conversions + 1, NativeTextShaping.GetVariableFontMetrics().Conversions);
        for (var i = 0; i < 10000; i++) NativeTextShaping.ResolveTypeface("Concurrent", 700, registry);
        Assert.Equal(before.Conversions + 1, NativeTextShaping.GetVariableFontMetrics().Conversions);
        registry.Dispose();
        Assert.NotEqual(IntPtr.Zero, faces[0].Handle);
        rendererReference.Dispose();
        Assert.Equal(IntPtr.Zero, faces[0].Handle);
        Assert.Equal(before.Bytes, NativeTextShaping.GetVariableFontMetrics().Bytes);
    }

    [Theory]
    [InlineData(400)]
    [InlineData(550)]
    [InlineData(700)]
    public unsafe void CanvasRendererUsesTheMeasuredInstanceAndRetainsItsLease(int weight)
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
        registry.Register("Variable", FontData());
        registry.Register("Reference", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", $"Roboto-{weight}.ttf")), weight, weight);
        const string text = "AV To ffi Release Notes";
        var measured = NativeTextShaping.Measure(text, "Variable", 28, weight, 0, 0, 0, registry);
        var expected = NativeTextShaping.Measure(text, "Reference", 28, weight, 0, 0, 0, registry);
        Assert.Equal(expected.AdvanceWidth, measured.AdvanceWidth, precision: 2);
        var measuredFace = NativeTextShaping.ResolveTypeface("Variable", weight, registry);
        var before = NativeTextShaping.GetVariableFontMetrics().Conversions;
        using var actual = Render("Variable");
        using var reference = Render("Reference");
        Assert.InRange(actual.Pixels.Zip(reference.Pixels).Sum(p => Math.Abs((long)p.First.Alpha - p.Second.Alpha)), 0, 500);
        Assert.True(actual.Pixels.Sum(p => (long)p.Alpha) > 10000);
        Assert.Equal(before, NativeTextShaping.GetVariableFontMetrics().Conversions);
        Assert.Equal(IntPtr.Zero, measuredFace.Handle); // Last renderer released the disposed document.

        SKBitmap Render(string family)
        {
            var renderer = new NativeCanvasSceneRenderer();
            renderer.SetWebTypefaceRegistry(registry);
            var font = System.Text.Encoding.UTF8.GetBytes($"{weight} 28px {family}");
            var label = System.Text.Encoding.UTF8.GetBytes(text);
            byte[] bytes = [.. font, .. label];
            NativeSceneString[] strings = [new() { ByteLength = (uint)font.Length }, new() { ByteOffset = (uint)font.Length, ByteLength = (uint)label.Length }];
            NativeCanvasCommand[] commands = [new() { Kind = 48, ResourceId = 0 }, new() { Kind = 25, ResourceId = 1, V0 = 2, V1 = 36 }];
            var layer = new NativeCanvasLayer { NodeId = 709, Flags = 1, CommandCount = 2, StringCount = 2, Width = 500, Height = 50, BitmapWidth = 500, BitmapHeight = 50, Generation = 1 };
            var bitmap = new SKBitmap(500, 50);
            try
            {
                using var canvas = new SKCanvas(bitmap);
                canvas.Clear(SKColors.Transparent);
                fixed (byte* bytesPointer = bytes)
                fixed (NativeSceneString* stringsPointer = strings)
                fixed (NativeCanvasCommand* commandsPointer = commands)
                {
                    var view = new NativeSceneView
                    {
                        Header = new SceneHeader { Revision = 1, CanvasLayerCount = 1, Flags = 1, ViewportWidth = 500, ViewportHeight = 50 },
                        CanvasLayers = &layer, CanvasCommands = commandsPointer, CanvasCommandCount = 2,
                        Strings = stringsPointer, StringCount = 2, StringBytes = bytesPointer, StringByteCount = (uint)bytes.Length
                    };
                    Assert.True(renderer.ApplyDiffAndRender(canvas, &view));
                }
                if (family == "Reference")
                {
                    registry.Dispose();
                    Assert.NotEqual(IntPtr.Zero, measuredFace.Handle); // Retained scene still owns the registry.
                }
                return bitmap;
            }
            catch { bitmap.Dispose(); throw; }
            finally { renderer.Reset(); }
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void FailedOrUnavailableConversionIsNotRetriedEveryFrame(bool unavailable)
    {
        var factory = NativeTextShaping.InstanceFactory;
        var calls = 0;
        NativeTextShaping.InstanceFactory = (_, _) =>
        {
            calls++;
            throw unavailable ? new EntryPointNotFoundException("Test missing export") : new InvalidOperationException("Test conversion failure");
        };
        try
        {
            using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
            registry.Register("Failure", FontData());
            for (var i = 0; i < 20; i++)
                Assert.Equal(400, Weight(NativeTextShaping.ResolveTypeface("Failure", 700, registry)));
            if (unavailable) NativeTextShaping.ResolveTypeface("Failure", 800, registry);
            Assert.Equal(1, calls);
        }
        finally { NativeTextShaping.InstanceFactory = factory; }
    }

    [Theory]
    [InlineData(1, 256, 67108864)]
    [InlineData(64, 1, 67108864)]
    [InlineData(64, 256, 1)]
    public void CacheLimitsRetainExistingFacesAndReleaseAllMemory(int perFont, int total, long bytes)
    {
        var limits = NativeTextShaping.InstanceLimits;
        var before = NativeTextShaping.GetVariableFontMetrics();
        NativeTextShaping.InstanceLimits = (perFont, total, bytes);
        try
        {
            using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
            registry.Register("Limited", FontData());
            var first = NativeTextShaping.ResolveTypeface("Limited", 700, registry);
            var second = NativeTextShaping.ResolveTypeface("Limited", 800, registry);
            Assert.Equal(400, Weight(second));
            Assert.Same(first, NativeTextShaping.ResolveTypeface("Limited", 700, registry));
            Assert.NotEqual(IntPtr.Zero, first.Handle);
            var attempts = NativeTextShaping.GetVariableFontMetrics().Conversions;
            for (var i = 0; i < 100; i++) NativeTextShaping.ResolveTypeface("Limited", 900, registry);
            Assert.Equal(attempts, NativeTextShaping.GetVariableFontMetrics().Conversions);
        }
        finally { NativeTextShaping.InstanceLimits = limits; }
        Assert.Equal(before.Bytes, NativeTextShaping.GetVariableFontMetrics().Bytes);
        Assert.Equal(before.Instances, NativeTextShaping.GetVariableFontMetrics().Instances);
    }

    [Fact]
    public void DisabledAndMalformedFontsDoNotConvert()
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(false);
        Assert.False(registry.Register("Broken", [0, 1, 2, 3]));
        registry.Register("Disabled", FontData());
        var before = NativeTextShaping.GetVariableFontMetrics();
        Assert.Equal(400, Weight(NativeTextShaping.ResolveTypeface("Disabled", 700, registry)));
        Assert.Equal(before.Conversions, NativeTextShaping.GetVariableFontMetrics().Conversions);
    }

#if !WEBSCENE_UNO
    [Theory]
    [InlineData(null, 400, 400)]
    [InlineData("normal", 400, 400)]
    [InlineData("bold", 700, 700)]
    [InlineData("200 800", 200, 800)]
    [InlineData("900 100", 400, 400)]
    public void CssWeightDescriptorsArePreserved(string? value, int minimum, int maximum)
        => Assert.Equal((minimum, maximum), NativeWebSceneApi.ResourceBridge.ParseFontFaceWeight(value));
#endif

    [Fact]
    public void StaticFacesUseCssMatchingAndDeclaredVariableRangeIsRespected()
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
        registry.Register("Static", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", "Roboto-700.ttf")), 700, 700);
        registry.Register("Static", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", "Roboto-400.ttf")), 400, 400);
        Assert.Equal(400, Weight(NativeTextShaping.ResolveTypeface("Static", 450, registry)));
        Assert.Equal(700, Weight(NativeTextShaping.ResolveTypeface("Static", 600, registry)));
        registry.Register("Range", FontData(), 300, 600);
        Assert.Equal(600, Weight(NativeTextShaping.ResolveTypeface("Range", 900, registry)));
        Assert.Equal(300, Weight(NativeTextShaping.ResolveTypeface("Range", 100, registry)));
    }

    [Theory]
    [InlineData(400)]
    [InlineData(550)]
    [InlineData(700)]
    public void ExistingHarfBuzzCanProduceACompleteStaticFont(int weight)
    {
        using var data = SKData.CreateCopy(FontData());
        using var source = SKTypeface.FromData(data);
        Assert.NotNull(NativeVariableFontInstancer.ReadWeightAxis(source));
        var timer = System.Diagnostics.Stopwatch.StartNew();
        var bytes = NativeVariableFontInstancer.Instantiate(source, weight);
        using var resultData = SKData.CreateCopy(bytes);
        using var result = SKTypeface.FromData(resultData);
        Assert.NotNull(result);
        Assert.Equal(weight, result.FontWeight);
        Assert.Null(NativeVariableFontInstancer.ReadWeightAxis(result));
        Assert.Equal(source.GlyphCount, result.GlyphCount);
        Assert.Equal(source.GetTableSize(0x47535542) > 0, result.GetTableSize(0x47535542) > 0); // GSUB
        Assert.Equal(source.GetTableSize(0x47504f53) > 0, result.GetTableSize(0x47504f53) > 0); // GPOS (absent in Roboto Mono)
        Console.WriteLine($"FONT INSTANCE {weight}: {timer.Elapsed.TotalMilliseconds:F2} ms, {bytes.Length} bytes");
    }

    internal static byte[] FontData() => File.ReadAllBytes(
        Environment.GetEnvironmentVariable("WEBSCENE_TEST_VARIABLE_FONT")
        ?? Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", "Roboto-Variable.ttf"));

    [Fact]
    public void RequestedWeightChangesTheActualFontAxisShapingAndRaster()
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(instantiate: true);
        Assert.True(registry.Register("Release notes", FontData()));
        var regular = NativeTextShaping.ResolveTypeface("Release notes", 400, registry);
        var bold = NativeTextShaping.ResolveTypeface("Release notes", 700, registry);
        Assert.Equal(400, Weight(regular));
        Assert.Equal(700, Weight(bold));
        Assert.NotSame(regular, bold);
        Assert.Same(bold, NativeTextShaping.ResolveTypeface("Release notes", 700, registry));

        const string heading = "Release Notes: Version 2.2.2";
        using var regularFont = new SKPaint { Typeface = regular, TextSize = 28 };
        using var boldFont = new SKPaint { Typeface = bold, TextSize = 28 };
        using var regularShaper = new SKShaper(regular);
        using var boldShaper = new SKShaper(bold);
        Assert.True(boldShaper.Shape(heading, boldFont).Width > regularShaper.Shape(heading, regularFont).Width);
        Assert.True(Ink(boldFont) > Ink(regularFont));
        var regularMetrics = NativeTextShaping.Measure(heading, "Release notes", 28, 400, 0, 0, 0, registry);
        var boldMetrics = NativeTextShaping.Measure(heading, "Release notes", 28, 700, 0, 0, 0, registry);
        Assert.True(boldMetrics.AdvanceWidth > regularMetrics.AdvanceWidth);
    }

    [Fact]
    public void VariantsAreSharedAcrossDocumentsAndReleasedWithTheLastLease()
    {
        var before = NativeTextShaping.GetWebTypefaceCacheMetrics();
        using var first = NativeTextShaping.CreateWebTypefaceRegistry(instantiate: true);
        using var second = NativeTextShaping.CreateWebTypefaceRegistry(instantiate: true);
        first.Register("First alias", FontData());
        second.Register("Second alias", FontData());
        var bold = NativeTextShaping.ResolveTypeface("First alias", 700, first);
        Assert.Same(bold, NativeTextShaping.ResolveTypeface("Second alias", 700, second));
        first.Dispose();
        Assert.Equal(700, Weight(NativeTextShaping.ResolveTypeface("Second alias", 700, second)));
        second.Dispose();
        Assert.Equal(before.Entries, NativeTextShaping.GetWebTypefaceCacheMetrics().Entries);
        Assert.Equal(IntPtr.Zero, bold.Handle);
    }

    [Fact]
    public void SameFamilyInDifferentDocumentsRemainsIsolatedAndRegistrationChangesSelection()
    {
        using var first = NativeTextShaping.CreateWebTypefaceRegistry(true);
        using var second = NativeTextShaping.CreateWebTypefaceRegistry(true);
        first.Register("Scoped", FontData());
        second.Register("Scoped", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", "Roboto-400.ttf")), 400, 400);
        var firstBold = NativeTextShaping.ResolveTypeface("Scoped", 700, first);
        Assert.Equal(700, Weight(firstBold));
        Assert.Equal(400, Weight(NativeTextShaping.ResolveTypeface("Scoped", 700, second)));
        second.Register("Scoped", File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "Fonts", "Roboto", "Roboto-700.ttf")), 700, 700);
        Assert.Equal(700, Weight(NativeTextShaping.ResolveTypeface("Scoped", 700, second)));
        Assert.Same(firstBold, NativeTextShaping.ResolveTypeface("Scoped", 700, first));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void InvalidConvertedFontFallsBackWithoutRepeatingTheAttempt(bool stillVariable)
    {
        var factory = NativeTextShaping.InstanceFactory;
        var calls = 0;
        NativeTextShaping.InstanceFactory = (_, _) => { calls++; return stillVariable ? FontData() : [0, 1, 2, 3]; };
        try
        {
            using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
            registry.Register("Invalid result", FontData());
            for (var i = 0; i < 10; i++)
                Assert.Equal(400, Weight(NativeTextShaping.ResolveTypeface("Invalid result", 700, registry)));
            Assert.Equal(1, calls);
        }
        finally { NativeTextShaping.InstanceFactory = factory; }
    }

    [Theory]
    [InlineData(1, 100)]
    [InlineData(1000, 900)]
    [InlineData(550, 550)]
    public void WeightIsClampedToTheFontAxisNotToNamedInstances(int requested, int expected)
    {
        using var registry = NativeTextShaping.CreateWebTypefaceRegistry(instantiate: true);
        registry.Register("Variable", FontData());
        Assert.Equal(expected, Weight(NativeTextShaping.ResolveTypeface("Variable", requested, registry)));
    }

    private static float Weight(SKTypeface face) => System.Buffers.Binary.BinaryPrimitives.ReadUInt16BigEndian(face.GetTableData(0x4f532f32).AsSpan(4));

    private static long Ink(SKPaint font)
    {
        using var bitmap = new SKBitmap(500, 50);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        font.Color = SKColors.White;
        font.IsAntialias = true;
        canvas.DrawText("Release Notes: Version 2.2.2", 2, 36, font);
        return bitmap.Pixels.Sum(pixel => (long)pixel.Alpha);
    }
}

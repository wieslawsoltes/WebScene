using SkiaSharp;
using SkiaSharp.HarfBuzz;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class ShapedRunCacheTests
{
    [Theory]
    [InlineData("Courtyard House 123")]
    [InlineData("office a\u0301 العربية")]
    public void CachedRunsMatchShaperAndSeparateSizeScaleAndEncoding(string text)
    {
        var cache = new NativeTextShaping.ShapedRunCache(8, 32768);
        using var face = SKTypeface.FromFamilyName("sans-serif");
        using var shaper = new SKShaper(face);
        using var paint = new SKPaint { Typeface = face, TextSize = 14 };
        var first = cache.Shape(shaper, text, paint);
        using var anotherShaper = new SKShaper(face);
        paint.Color = SKColors.Red;
        Assert.Same(first, cache.Shape(anotherShaper, text, paint));
        foreach (var settings in new[] { (14f, 1f, SKTextEncoding.Utf8), (18f, 1f, SKTextEncoding.Utf8),
                     (18f, 1.3f, SKTextEncoding.Utf8), (18f, 1.3f, SKTextEncoding.Utf16) })
        {
            paint.TextSize = settings.Item1; paint.TextScaleX = settings.Item2; paint.TextEncoding = settings.Item3;
            var expected = shaper.Shape(text, 0, 0, paint);
            var actual = cache.Shape(shaper, text, paint);
            Assert.Equal(expected.Width, actual.Width);
            Assert.Equal(expected.Codepoints, actual.Codepoints);
            Assert.Equal(expected.Clusters, actual.Clusters);
            Assert.Equal(expected.Points, actual.Points);
        }
        Assert.Equal(4, cache.Occupancy.Count);
    }

    [Theory]
    [InlineData(0, false, 0, false)]
    [InlineData(13.25f, false, 0, false)]
    [InlineData(13.25f, true, -0.2f, false)]
    [InlineData(13.25f, false, 0, true)]
    public void CachedDrawingMatchesUncachedGlyphPositionsAtDifferentBaselines(float x, bool bold, float skew, bool autoHint)
    {
        const string text = "office a\u0301";
        using var shaper = new SKShaper(SKTypeface.Default);
        using var paint = new SKPaint { Typeface = shaper.Typeface, TextSize = 22, Color = SKColors.Black,
            FakeBoldText = bold, TextSkewX = skew, IsAutohinted = autoHint };
        using var expected = new SKBitmap(300, 100);
        using var actual = new SKBitmap(300, 100);
        using var expectedCanvas = new SKCanvas(expected);
        using var actualCanvas = new SKCanvas(actual);
        expectedCanvas.Clear(SKColors.White);
        actualCanvas.Clear(SKColors.White);
        foreach (var baseline in new[] { 30.125f, 70.125f })
        {
            var shaped = shaper.Shape(text, 0, baseline, paint);
            using var font = paint.ToFont();
            NativeTextShaping.ApplyFontRasterizationProfile(font, 1,
                NativeTextShaping.NativeFontRasterizationMode.Current);
            using var builder = new SKTextBlobBuilder();
            var run = builder.AllocatePositionedRun(font, shaped.Codepoints.Length);
            for (var index = 0; index < shaped.Codepoints.Length; index++)
            {
                run.GetGlyphSpan()[index] = (ushort)shaped.Codepoints[index];
                run.GetPositionSpan()[index] = new SKPoint(x + shaped.Points[index].X, shaped.Points[index].Y);
            }
            using var blob = builder.Build();
            expectedCanvas.DrawText(blob, 0, 0, paint);
            NativeTextShaping.DrawShapedText(actualCanvas, shaper, text, x, baseline, paint, 0,
                rasterizationMode: NativeTextShaping.NativeFontRasterizationMode.Current);
        }
        Assert.Equal(expected.Pixels, actual.Pixels);
    }

    [Fact]
    public void ReplacingTypefaceCannotReuseThePreviousFontsGlyphs()
    {
        var cache = new NativeTextShaping.ShapedRunCache(8, 32768);
        var root = new DirectoryInfo(AppContext.BaseDirectory);
        while (root is not null && !File.Exists(Path.Combine(root.FullName, "tests/Fonts/Roboto/Roboto-400.ttf")))
            root = root.Parent;
        Assert.NotNull(root);
        using var firstFace = SKTypeface.FromFile(Path.Combine(root.FullName, "tests/Fonts/Roboto/Roboto-400.ttf"));
        using var secondFace = SKTypeface.FromFile(Path.Combine(root.FullName, "tests/Fonts/Roboto/Roboto-700.ttf"));
        using var firstShaper = new SKShaper(firstFace);
        using var secondShaper = new SKShaper(secondFace);
        using var paint = new SKPaint { TextSize = 16 };
        var first = cache.Shape(firstShaper, "WWiii", paint);
        var second = cache.Shape(secondShaper, "WWiii", paint);
        Assert.NotSame(first, second);
        Assert.Equal(secondShaper.Shape("WWiii", paint).Points, second.Points);
    }

    [Fact]
    public void EvictionAndOversizedRunsRespectBothBounds()
    {
        var cache = new NativeTextShaping.ShapedRunCache(2, 1024);
        using var shaper = new SKShaper(SKTypeface.Default);
        using var paint = new SKPaint { TextSize = 14 };
        var oldest = cache.Shape(shaper, "one", paint);
        cache.Shape(shaper, "two", paint);
        cache.Shape(shaper, "three", paint);
        Assert.Equal(2, cache.Occupancy.Count);
        Assert.NotSame(oldest, cache.Shape(shaper, "one", paint));
        var before = cache.Occupancy;
        cache.Shape(shaper, new string('W', 1000), paint);
        Assert.Equal(before, cache.Occupancy);
        Assert.InRange(cache.Occupancy.Bytes, 1, 1024);
    }
    [Fact]
    public void BlobEvictionHonorsLruAndPinsActiveReaders()
    {
        using var cache = new NativeTextShaping.TextBlobCache(2);
        using var shaper = new SKShaper(SKTypeface.Default);
        using var paint = new SKPaint { TextSize = 18 };
        NativeTextShaping.TextBlobCache.BorrowedBlob Acquire(string text)
            => cache.Acquire(shaper, text, shaper.Shape(text, paint), paint, 1, null)!;
        var first = Acquire("one");
        cache.Release(first);
        var second = Acquire("two");
        cache.Release(second);
        var reader = Acquire("one");
        Assert.Same(first, reader);
        var third = Acquire("three");
        cache.Release(third);
        Assert.Equal(IntPtr.Zero, second.Blob.Handle);
        var fourth = Acquire("four");
        cache.Release(fourth);
        Assert.NotEqual(IntPtr.Zero, first.Blob.Handle);
        cache.Release(reader);
        Assert.Equal(IntPtr.Zero, first.Blob.Handle);
        cache.Dispose();
        Assert.Equal(IntPtr.Zero, third.Blob.Handle);
        Assert.Equal(IntPtr.Zero, fourth.Blob.Handle);
    }

    [Fact]
    public void DisposingCacheDefersDisposalUntilLastReaderReturns()
    {
        using var cache = new NativeTextShaping.TextBlobCache();
        using var shaper = new SKShaper(SKTypeface.Default);
        using var paint = new SKPaint { TextSize = 18 };
        var shaped = shaper.Shape("same", paint);
        var first = cache.Acquire(shaper, "same", shaped, paint, 1, null)!;
        var second = cache.Acquire(shaper, "same", shaped, paint, 1, null)!;
        Assert.Same(first, second);
        cache.Dispose();
        cache.Release(first);
        Assert.NotEqual(IntPtr.Zero, second.Blob.Handle);
        cache.Release(second);
        Assert.Equal(IntPtr.Zero, second.Blob.Handle);
    }

    [Fact]
    public void BlobCacheRejectsOversizedRunsAndSeparatesRasterizationProfiles()
    {
        using var cache = new NativeTextShaping.TextBlobCache();
        using var shaper = new SKShaper(SKTypeface.Default);
        using var paint = new SKPaint { TextSize = 18 };
        var text = new string('W', 257);
        Assert.Null(cache.Acquire(shaper, text, shaper.Shape(text, paint), paint, 1, null));
        var shaped = shaper.Shape("label", paint);
        var first = cache.Acquire(shaper, "label", shaped, paint, 1,
            NativeTextShaping.NativeFontRasterizationMode.Current)!;
        var second = cache.Acquire(shaper, "label", shaped, paint, 2,
            NativeTextShaping.NativeFontRasterizationMode.Current)!;
        Assert.NotSame(first, second);
        paint.Color = SKColors.Red;
        var colored = cache.Acquire(shaper, "label", shaped, paint, 1,
            NativeTextShaping.NativeFontRasterizationMode.Current)!;
        Assert.Same(first, colored);
        cache.Release(first);
        cache.Release(second);
        cache.Release(colored);
    }

    [Fact]
    public void ConcurrentDrawingDuringEvictionMatchesUncachedPixels()
    {
        using var cache = new NativeTextShaping.TextBlobCache(2);
        Parallel.For(0, 8, worker =>
        {
            using var shaper = new SKShaper(SKTypeface.Default);
            using var paint = new SKPaint { TextSize = 20, Color = SKColors.Black };
            using var expected = new SKBitmap(300, 60);
            using var actual = new SKBitmap(300, 60);
            using var expectedCanvas = new SKCanvas(expected);
            using var actualCanvas = new SKCanvas(actual);
            for (var i = 0; i < 100; i++)
            {
                var text = $"office a\u0301 {worker} {i % 4}";
                var shaped = shaper.Shape(text, paint);
                expectedCanvas.Clear(SKColors.White);
                actualCanvas.Clear(SKColors.White);
                using var font = paint.ToFont();
                NativeTextShaping.ApplyFontRasterizationProfile(font, 2,
                    NativeTextShaping.NativeFontRasterizationMode.Current);
                using var builder = new SKTextBlobBuilder();
                var run = builder.AllocatePositionedRun(font, shaped.Codepoints.Length);
                for (var j = 0; j < shaped.Codepoints.Length; j++)
                {
                    run.GetGlyphSpan()[j] = (ushort)shaped.Codepoints[j];
                    run.GetPositionSpan()[j] = shaped.Points[j];
                }
                using var blob = builder.Build();
                expectedCanvas.DrawText(blob, 13.25f, 30.125f, paint);
                Assert.True(cache.Draw(actualCanvas, shaper, text, shaped, paint, 13.25f, 30.125f, 2,
                    NativeTextShaping.NativeFontRasterizationMode.Current));
                Assert.Equal(expected.Pixels, actual.Pixels);
            }
        });
    }
}

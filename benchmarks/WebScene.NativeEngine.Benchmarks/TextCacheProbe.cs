using System.Diagnostics;
using System.Text.Json;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using WebScene.Backends.Avalonia.Native;

namespace WebScene.NativeEngine.Benchmarks;

// Warm repeated-label microbenchmark. Uses a CPU bitmap, not a GPU context.
internal static class TextCacheProbe
{
    internal static int Run(string[] args)
    {
        if (args.Length != 0) throw new ArgumentException("text-cache takes no arguments");
        const int iterations = 10000;
        const string text = "Courtyard House 123 office a\u0301";
        using var face = SKTypeface.FromFamilyName("sans-serif");
        using var shaper = new SKShaper(face);
        using var paint = new SKPaint { Typeface = face, TextSize = 18, Color = SKColors.Black };
        using var bitmap = new SKBitmap(400, 64);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.White);
        var cache = new NativeTextShaping.ShapedRunCache(2048, 4 * 1024 * 1024);
        void DrawUncached()
        {
            var shaped = shaper.Shape(text, 0, 32, paint);
            using var font = paint.ToFont();
            NativeTextShaping.ApplyFontRasterizationProfile(font, 1,
                NativeTextShaping.NativeFontRasterizationMode.Current);
            using var builder = new SKTextBlobBuilder();
            var run = builder.AllocatePositionedRun(font, shaped.Codepoints.Length);
            for (var i = 0; i < shaped.Codepoints.Length; i++)
            {
                run.GetGlyphSpan()[i] = (ushort)shaped.Codepoints[i];
                run.GetPositionSpan()[i] = shaped.Points[i];
            }
            using var blob = builder.Build();
            canvas.DrawText(blob, 0, 0, paint);
        }
        void DrawCached() => NativeTextShaping.DrawShapedText(canvas, shaper, text, 0, 32,
            paint, 0, rasterizationMode: NativeTextShaping.NativeFontRasterizationMode.Current);
        Action[] operations = [() => shaper.Shape(text, 0, 0, paint),
            () => cache.Shape(shaper, text, paint), DrawUncached, DrawCached];
        string[] names = ["shape_uncached", "shape_cached", "draw_uncached", "draw_cached"];
        foreach (var operation in operations)
            for (var i = 0; i < 1000; i++) operation();
        // Reverse each alternate round to reduce simple ordering bias.
        for (var round = 0; round < 6; round++)
        {
            for (var step = 0; step < operations.Length; step++)
            {
                var index = round % 2 == 0 ? step : operations.Length - step - 1;
                var allocated = GC.GetAllocatedBytesForCurrentThread();
                var start = Stopwatch.GetTimestamp();
                for (var i = 0; i < iterations; i++) operations[index]();
                var elapsed = Stopwatch.GetTimestamp() - start;
                var bytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
                Console.WriteLine(JsonSerializer.Serialize(new
                {
                    round, operation = names[index], iterations, family = face.FamilyName,
                    microsecondsPerOperation = elapsed * 1_000_000.0 / Stopwatch.Frequency / iterations,
                    managedBytesPerOperation = (double)bytes / iterations,
                    cpuBitmap = true
                }));
            }
        }
        return 0;
    }
}

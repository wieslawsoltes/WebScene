using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using WebScene.Backends.Avalonia.Native;

internal sealed class WindowsGlyphDiagnosticApp : Application;

internal static class WindowsGlyphDiagnostics
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    internal static void Run(
        string[] arguments,
        string configurationPath,
        string chromeDocumentPath,
        string chromeCapturePath,
        string outputDirectory)
    {
        var requestedPlatform = Option(arguments, "--platform");
        if (requestedPlatform is not null
            && !requestedPlatform.Equals("windows", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException(
                $"This host cannot run the requested '{requestedPlatform}' diagnostic.");
        }
        var scales = ParseScales(Option(arguments, "--scales"));
        var configuration = JsonSerializer.Deserialize<WindowsConfiguration>(
            File.ReadAllText(configurationPath),
            JsonOptions)
            ?? throw new InvalidDataException("Could not read glyph diagnostic cases.");
        var chrome = ResolveChromeExecutable();
        var node = ResolveNodeExecutable();
        var browserVersion = FileVersionInfo.GetVersionInfo(chrome);
        var chromiumVersion = $"{browserVersion.ProductName ?? Path.GetFileName(chrome)} "
            + $"{browserVersion.ProductVersion ?? "unknown"}";
        var performance = Benchmark(configuration);

        AppBuilder.Configure<WindowsGlyphDiagnosticApp>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions
            {
                UseHeadlessDrawing = false
            })
            .SetupWithoutStarting();

        var scaleReports = new List<WindowsScaleReport>();
        foreach (var scale in scales)
        {
            var suffix = ScaleSuffix(scale);
            var harfBuzzImage = Path.Combine(outputDirectory, $"harfbuzz-{suffix}.png");
            var directWriteImage = Path.Combine(outputDirectory, $"directwrite-skia-{suffix}.png");
            var frameworkImage = Path.Combine(outputDirectory, $"avalonia-text-{suffix}.png");
            var chromeImage = Path.Combine(outputDirectory, $"chrome-{suffix}.png");
            var chromeMetrics = Path.Combine(outputDirectory, $"chrome-{suffix}.metrics.json");
            var harfBuzzRuns = RenderSkia(
                configuration,
                scale,
                directWrite: false,
                harfBuzzImage);
            var directWriteRuns = RenderSkia(
                configuration,
                scale,
                directWrite: true,
                directWriteImage);
            RenderAvalonia(configuration, scale, frameworkImage);
            RunProcess(node,
            [
                chromeCapturePath,
                "--chrome", chrome,
                "--document", chromeDocumentPath,
                "--output", chromeImage,
                "--metrics", chromeMetrics,
                "--scale", scale.ToString(CultureInfo.InvariantCulture),
                "--width", configuration.Width.ToString(CultureInfo.InvariantCulture),
                "--height", (configuration.Height * 3).ToString(CultureInfo.InvariantCulture)
            ]);

            File.WriteAllText(
                Path.Combine(outputDirectory, $"harfbuzz-{suffix}.metrics.json"),
                JsonSerializer.Serialize(harfBuzzRuns, JsonOptions));
            File.WriteAllText(
                Path.Combine(outputDirectory, $"directwrite-skia-{suffix}.metrics.json"),
                JsonSerializer.Serialize(directWriteRuns, JsonOptions));
            scaleReports.Add(AnalyzeScale(
                configuration,
                scale,
                harfBuzzImage,
                directWriteImage,
                frameworkImage,
                chromeImage,
                directWriteRuns));
        }

        var report = new WindowsDiagnosticReport(
            DateTimeOffset.UtcNow,
            Environment.OSVersion.VersionString,
            RuntimeInformation.RuntimeIdentifier,
            chromiumVersion,
            Environment.CommandLine,
            performance,
            scaleReports);
        File.WriteAllText(
            Path.Combine(outputDirectory, "report.json"),
            JsonSerializer.Serialize(report, JsonOptions));
        File.WriteAllText(
            Path.Combine(outputDirectory, "report.md"),
            FormatReport(report));
        Console.WriteLine(Path.Combine(outputDirectory, "report.md"));
    }

    private static List<WindowsRunMetrics> RenderSkia(
        WindowsConfiguration configuration,
        float scale,
        bool directWrite,
        string outputPath)
    {
        var width = checked((int)MathF.Round(configuration.Width * scale));
        var height = checked((int)MathF.Round(configuration.Height * scale));
        using var bitmap = new SKBitmap(new SKImageInfo(width, height, SKColorType.Bgra8888));
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColor.Parse(configuration.Background));
        canvas.Scale(scale);
        var metrics = new List<WindowsRunMetrics>(configuration.Cases.Length);
        var positioner = directWrite ? new WindowsDirectWriteRunPositioner() : null;
        foreach (var item in configuration.Cases)
        {
            var typeface = NativeTextShaping.ResolveTypeface(
                configuration.Family,
                item.Weight);
            using var paint = new SKPaint
            {
                Typeface = typeface,
                TextSize = item.Size,
                Color = SKColor.Parse(configuration.Foreground),
                IsAntialias = true
            };
            using var shaper = new SKShaper(typeface);
            var shaped = shaper.Shape(item.Text, 0, 0, paint);
            NativePositionedTextRun? positioned = null;
            if (positioner is not null)
            {
                var request = new NativeTextRunPositionRequest(
                    item.Text,
                    configuration.Family,
                    item.Size,
                    item.Weight,
                    SKFontStyleSlant.Upright,
                    0,
                    shaped.Codepoints,
                    null,
                    typeface);
                positioner.TryPosition(in request, out positioned);
            }
            NativeTextShaping.DrawShapedText(
                canvas,
                shaper,
                item.Text,
                item.X,
                item.Baseline,
                paint,
                0,
                measuredWidth: positioned?.AdvanceWidth ?? shaped.Width,
                deviceScaleFactor: scale,
                positionedRun: positioned);
            metrics.Add(new WindowsRunMetrics(
                item.Id,
                (positioned?.Glyphs.Select(static glyph => (uint)glyph).ToArray()
                    ?? shaped.Codepoints),
                positioned?.Clusters ?? [],
                positioned?.Positions.Select(static point => new[] { point.X, point.Y }).ToArray()
                    ?? shaped.Points.Select(static point => new[] { point.X, point.Y }).ToArray(),
                positioned?.Advances ?? [],
                positioned?.Offsets?.Select(static point => new[] { point.X, point.Y }).ToArray()
                    ?? [],
                positioned?.AdvanceWidth ?? shaped.Width,
                positioned?.FaceIdentity,
                scale,
                new[] { scale, 0f, 0f, scale, 0f, 0f }));
        }
        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        using var stream = File.Create(outputPath);
        data.SaveTo(stream);
        return metrics;
    }

    private static void RenderAvalonia(
        WindowsConfiguration configuration,
        float scale,
        string outputPath)
    {
        var width = checked((int)MathF.Round(configuration.Width * scale));
        var height = checked((int)MathF.Round(configuration.Height * scale));
        var panel = new Canvas
        {
            Width = width,
            Height = height,
            Background = Brush.Parse(configuration.Background)
        };
        foreach (var item in configuration.Cases)
        {
            var text = new TextBlock
            {
                Text = item.Text,
                FontFamily = new FontFamily("Segoe UI"),
                FontSize = item.Size * scale,
                FontWeight = (FontWeight)item.Weight,
                Foreground = Brush.Parse(configuration.Foreground),
                LineHeight = item.Size * scale
            };
            Canvas.SetLeft(text, item.X * scale);
            Canvas.SetTop(text, (item.Baseline - item.Size) * scale);
            panel.Children.Add(text);
        }
        var window = new Window
        {
            Width = width,
            Height = height,
            Content = panel,
            Background = Brush.Parse(configuration.Background),
            CanResize = false,
            SystemDecorations = SystemDecorations.None
        };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = window.CaptureRenderedFrame()
                ?? throw new InvalidOperationException("Avalonia returned no rendered frame.");
            frame.Save(outputPath);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static WindowsScaleReport AnalyzeScale(
        WindowsConfiguration configuration,
        float scale,
        string harfBuzzPath,
        string directWritePath,
        string frameworkPath,
        string chromePath,
        IReadOnlyList<WindowsRunMetrics> directWriteRuns)
    {
        using var harfBuzz = SKBitmap.Decode(harfBuzzPath);
        using var directWrite = SKBitmap.Decode(directWritePath);
        using var framework = SKBitmap.Decode(frameworkPath);
        using var chrome = SKBitmap.Decode(chromePath);
        var expectedHeight = checked((int)MathF.Round(configuration.Height * scale));
        var sources = new Dictionary<string, ImageSlice>
        {
            ["harfbuzz-skia"] = new(harfBuzz, 0),
            ["directwrite-skia"] = new(directWrite, 0),
            ["avalonia-text"] = new(framework, 0),
            ["chrome-canvas"] = new(chrome, 0),
            ["chrome-dom"] = new(chrome, expectedHeight)
        };
        var cases = new List<WindowsCaseReport>();
        foreach (var item in configuration.Cases)
        {
            var region = ResolveRegion(configuration, item, scale);
            var comparisons = new List<WindowsPixelComparison>();
            foreach (var candidate in new[] { "harfbuzz-skia", "directwrite-skia", "avalonia-text" })
            {
                comparisons.Add(Compare(
                    candidate,
                    sources[candidate],
                    sources["chrome-canvas"],
                    region,
                    checked((int)MathF.Ceiling(2 * scale)),
                    "chrome-canvas"));
                comparisons.Add(Compare(
                    candidate + "-vs-dom",
                    sources[candidate],
                    sources["chrome-dom"],
                    region,
                    checked((int)MathF.Ceiling(2 * scale)),
                    "chrome-dom"));
            }
            cases.Add(new WindowsCaseReport(
                item.Id,
                item.Text.EnumerateRunes().Count() == 1,
                comparisons,
                directWriteRuns.Single(run => run.Id == item.Id)));
        }
        var eligible = cases.Where(item => !item.IsolatedGlyph
            && item.DirectWrite.FaceIdentity is not null).ToArray();
        var harfBuzzComparisons = eligible.Select(item => item.Comparisons.Single(
            comparison => comparison.Source == "harfbuzz-skia")).ToArray();
        var directWriteComparisons = eligible.Select(item => item.Comparisons.Single(
            comparison => comparison.Source == "directwrite-skia")).ToArray();
        var harfBuzzErrors = harfBuzzComparisons
            .Select(comparison => comparison.MeanAbsoluteError).ToArray();
        var directWriteErrors = directWriteComparisons
            .Select(comparison => comparison.MeanAbsoluteError).ToArray();
        var totalSamples = harfBuzzComparisons.Sum(comparison => (long)comparison.Samples);
        var harfBuzzCorpusError = totalSamples == 0
            ? 0
            : harfBuzzComparisons.Sum(
                comparison => comparison.MeanAbsoluteError * comparison.Samples)
                / totalSamples;
        var directWriteCorpusError = totalSamples == 0
            ? 0
            : directWriteComparisons.Sum(
                comparison => comparison.MeanAbsoluteError * comparison.Samples)
                / totalSamples;
        var regressions = directWriteErrors.Zip(
            harfBuzzErrors,
            static (directWrite, harfBuzz) => directWrite - harfBuzz).ToArray();
        var isolatedRegressions = cases.Where(item => item.IsolatedGlyph
                && item.DirectWrite.FaceIdentity is not null)
            .Select(item =>
                item.Comparisons.Single(comparison => comparison.Source == "directwrite-skia")
                    .MeanAbsoluteError
                - item.Comparisons.Single(comparison => comparison.Source == "harfbuzz-skia")
                    .MeanAbsoluteError)
            .ToArray();
        var maximumIsolatedRegression = isolatedRegressions.Length == 0
            ? 0
            : isolatedRegressions.Max();
        var summary = new WindowsScaleSummary(
            harfBuzzCorpusError,
            directWriteCorpusError,
            regressions.Length == 0 ? 0 : regressions.Max(),
            maximumIsolatedRegression,
            directWriteErrors.Length > 0
                && directWriteCorpusError < harfBuzzCorpusError
                && regressions.All(regression => regression <= .001)
                && maximumIsolatedRegression <= .001);
        return new WindowsScaleReport(scale, cases, summary);
    }

    private static WindowsPerformanceReport Benchmark(WindowsConfiguration configuration)
    {
        var item = configuration.Cases.First(candidate =>
            candidate.Weight == 400 && candidate.Text.Length > 8
            && candidate.Text.All(character => character <= '\u024f'));
        var typeface = NativeTextShaping.ResolveTypeface(configuration.Family, item.Weight);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = item.Size,
            IsAntialias = true,
            Color = SKColors.White
        };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(item.Text, 0, 0, paint);
        var request = new NativeTextRunPositionRequest(
            item.Text,
            configuration.Family,
            item.Size,
            item.Weight,
            SKFontStyleSlant.Upright,
            0,
            shaped.Codepoints,
            null,
            typeface);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        var managedBefore = GC.GetTotalMemory(forceFullCollection: true);
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.GetTimestamp();
        var positioner = new WindowsDirectWriteRunPositioner();
        if (!positioner.TryPosition(in request, out var positioned))
            throw new InvalidOperationException("DirectWrite rejected the benchmark run.");
        var coldElapsed = Stopwatch.GetElapsedTime(started);
        var coldAllocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        var managedAfterCold = GC.GetTotalMemory(forceFullCollection: true);

        const int shapeIterations = 10_000;
        allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        started = Stopwatch.GetTimestamp();
        for (var index = 0; index < shapeIterations; index++)
        {
            if (!positioner.TryPosition(in request, out _))
                throw new InvalidOperationException("A cached DirectWrite run was rejected.");
        }
        var warmElapsed = Stopwatch.GetElapsedTime(started);
        var warmAllocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;

        const int drawIterations = 10_000;
        using var bitmap = new SKBitmap(400, 80);
        using var canvas = new SKCanvas(bitmap);
        for (var index = 0; index < 100; index++)
        {
            NativeTextShaping.DrawShapedText(
                canvas, shaper, item.Text, 0, 40, paint, 0,
                measuredWidth: positioned.AdvanceWidth,
                positionedRun: positioned);
            NativeTextShaping.DrawShapedText(
                canvas, shaper, item.Text, 0, 40, paint, 0,
                measuredWidth: shaped.Width);
        }
        allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        started = Stopwatch.GetTimestamp();
        for (var index = 0; index < drawIterations; index++)
        {
            NativeTextShaping.DrawShapedText(
                canvas,
                shaper,
                item.Text,
                0,
                40,
                paint,
                0,
                measuredWidth: positioned.AdvanceWidth,
                positionedRun: positioned);
        }
        var drawElapsed = Stopwatch.GetElapsedTime(started);
        var drawAllocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        started = Stopwatch.GetTimestamp();
        for (var index = 0; index < drawIterations; index++)
        {
            NativeTextShaping.DrawShapedText(
                canvas,
                shaper,
                item.Text,
                0,
                40,
                paint,
                0,
                measuredWidth: shaped.Width);
        }
        var harfBuzzDrawElapsed = Stopwatch.GetElapsedTime(started);
        var harfBuzzDrawAllocated =
            GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        var warmShapeNanoseconds = warmElapsed.TotalNanoseconds / shapeIterations;
        var warmShapeAllocated = warmAllocated / (double)shapeIterations;
        var drawNanoseconds = drawElapsed.TotalNanoseconds / drawIterations;
        var drawAllocatedPerIteration = drawAllocated / (double)drawIterations;
        var harfBuzzDrawNanoseconds =
            harfBuzzDrawElapsed.TotalNanoseconds / drawIterations;
        var harfBuzzDrawAllocatedPerIteration =
            harfBuzzDrawAllocated / (double)drawIterations;
        return new WindowsPerformanceReport(
            item.Id,
            coldElapsed.TotalMilliseconds,
            coldAllocated,
            managedAfterCold - managedBefore,
            warmShapeNanoseconds,
            warmShapeAllocated,
            drawNanoseconds,
            drawAllocatedPerIteration,
            harfBuzzDrawNanoseconds,
            harfBuzzDrawAllocatedPerIteration,
            coldElapsed.TotalMilliseconds <= 50
                && warmShapeNanoseconds <= 10_000
                && warmShapeAllocated <= 1
                && drawNanoseconds <= harfBuzzDrawNanoseconds * 1.1
                && drawAllocatedPerIteration <= harfBuzzDrawAllocatedPerIteration,
            2048,
            64);
    }

    private static WindowsPixelComparison Compare(
        string sourceName,
        ImageSlice source,
        ImageSlice reference,
        PixelBox region,
        int radius,
        string referenceName)
    {
        WindowsPixelComparison? best = null;
        for (var dy = -radius; dy <= radius; dy++)
        for (var dx = -radius; dx <= radius; dx++)
        {
            double absolute = 0;
            double squared = 0;
            var samples = 0;
            var differing = 0;
            for (var y = region.Top; y < region.Bottom; y++)
            for (var x = region.Left; x < region.Right; x++)
            {
                var left = source.GetPixel(x + dx, y);
                var right = reference.GetPixel(x, y);
                var difference = (
                    Math.Abs(left.Red - right.Red)
                    + Math.Abs(left.Green - right.Green)
                    + Math.Abs(left.Blue - right.Blue)) / (3d * 255d);
                absolute += difference;
                squared += difference * difference;
                samples++;
                if (difference > 0) differing++;
            }
            var comparison = new WindowsPixelComparison(
                sourceName,
                referenceName,
                dx,
                dy,
                absolute / samples,
                Math.Sqrt(squared / samples),
                differing,
                samples);
            if (best is null || comparison.MeanAbsoluteError < best.MeanAbsoluteError)
                best = comparison;
        }
        return best!;
    }

    private static PixelBox ResolveRegion(
        WindowsConfiguration configuration,
        WindowsGlyphCase item,
        float scale)
        => new(
            Math.Max(0, (int)MathF.Floor((item.X - 4) * scale)),
            Math.Max(0, (int)MathF.Floor((item.Baseline - item.Size * 1.6f) * scale)),
            Math.Min(
                checked((int)MathF.Round(configuration.Width * scale)),
                (int)MathF.Ceiling((item.X + Math.Max(80, item.Text.Length * item.Size * .8f + 20)) * scale)),
            Math.Min(
                checked((int)MathF.Round(configuration.Height * scale)),
                (int)MathF.Ceiling((item.Baseline + item.Size * .65f) * scale)));

    private static string FormatReport(WindowsDiagnosticReport report)
    {
        var builder = new StringBuilder();
        builder.AppendLine("# Windows glyph diagnostic");
        builder.AppendLine();
        builder.AppendLine($"- Generated: {report.GeneratedUtc:O}");
        builder.AppendLine($"- OS: {report.OperatingSystem}");
        builder.AppendLine($"- RID: {report.RuntimeIdentifier}");
        builder.AppendLine($"- Chromium oracle: {report.ChromiumVersion}");
        builder.AppendLine($"- Command: `{report.Command}`");
        builder.AppendLine(
            $"- Performance ({report.Performance.CaseId}): cold shape "
            + $"{report.Performance.ColdShapeMilliseconds:F3} ms / "
            + $"{report.Performance.ColdAllocatedBytes} B allocated; warm cache hit "
            + $"{report.Performance.WarmShapeNanoseconds:F1} ns / "
            + $"{report.Performance.WarmAllocatedBytes:F1} B; positioned draw "
            + $"{report.Performance.DrawNanoseconds:F1} ns / "
            + $"{report.Performance.DrawAllocatedBytes:F1} B; HarfBuzz draw "
            + $"{report.Performance.HarfBuzzDrawNanoseconds:F1} ns / "
            + $"{report.Performance.HarfBuzzDrawAllocatedBytes:F1} B; budget "
            + $"{(report.Performance.PassesBudget ? "pass" : "fail")}");
        foreach (var scale in report.Scales)
        {
            builder.AppendLine();
            builder.AppendLine($"## Scale {scale.DeviceScaleFactor:P0}");
            builder.AppendLine();
            builder.AppendLine(
                $"Eligible multi-glyph canvas MAE: HarfBuzz {scale.Summary.HarfBuzzMeanAbsoluteError:F6}, "
                + $"DirectWrite {scale.Summary.DirectWriteMeanAbsoluteError:F6}; "
                + $"maximum per-case regression {scale.Summary.MaximumPerCaseRegression:F6}; "
                + $"maximum isolated-glyph regression "
                + $"{scale.Summary.MaximumIsolatedGlyphRegression:F6}; "
                + $"rollout gate: {(scale.Summary.PassesPixelGate ? "pass" : "fail") }.");
            builder.AppendLine();
            builder.AppendLine("| Case | Source | shift | MAE | RMSE | differing | face |");
            builder.AppendLine("|---|---|---:|---:|---:|---:|---|");
            foreach (var item in scale.Cases)
            foreach (var comparison in item.Comparisons)
            {
                builder.AppendLine(
                    $"| {item.Id} | {comparison.Source} | {comparison.OffsetX},{comparison.OffsetY} "
                    + $"| {comparison.MeanAbsoluteError:F6} | {comparison.RootMeanSquareError:F6} "
                    + $"| {comparison.DifferingPixels}/{comparison.Samples} "
                    + $"| {item.DirectWrite.FaceIdentity?.FamilyName ?? "fallback"} |");
            }
        }
        return builder.ToString();
    }

    private static float[] ParseScales(string? value)
    {
        var scales = string.IsNullOrWhiteSpace(value)
            ? [1f, 1.25f, 1.5f, 2f]
            : value.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
                .Select(item => float.Parse(item, CultureInfo.InvariantCulture))
                .ToArray();
        if (scales.Length == 0 || scales.Any(scale => !float.IsFinite(scale) || scale <= 0))
            throw new ArgumentException("--scales must contain positive finite values.");
        return scales;
    }

    private static string? Option(IReadOnlyList<string> arguments, string name)
    {
        for (var index = 0; index < arguments.Count; index++)
        {
            if (!arguments[index].Equals(name, StringComparison.OrdinalIgnoreCase)) continue;
            if (index + 1 >= arguments.Count)
                throw new ArgumentException($"Missing value for {name}.");
            return arguments[index + 1];
        }
        return null;
    }

    private static string ScaleSuffix(float scale)
        => $"{scale.ToString("0.##", CultureInfo.InvariantCulture)}x";

    private static string ResolveChromeExecutable()
    {
        var configured = Environment.GetEnvironmentVariable("WEBSCENE_CHROMIUM_EXECUTABLE");
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured)) return configured;
        var candidates = new[]
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Google", "Chrome", "Application", "chrome.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Google", "Chrome", "Application", "chrome.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                "Google", "Chrome", "Application", "chrome.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "Microsoft", "Edge", "Application", "msedge.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                "Microsoft", "Edge", "Application", "msedge.exe")
        };
        return candidates.FirstOrDefault(File.Exists)
            ?? throw new FileNotFoundException(
                "Install Chrome or set WEBSCENE_CHROMIUM_EXECUTABLE to a Chromium-compatible executable.");
    }

    private static string ResolveNodeExecutable()
    {
        var configured = Environment.GetEnvironmentVariable("WEBSCENE_NODE_EXECUTABLE");
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured)) return configured;
        return "node";
    }

    private static string RunProcess(string executable, IReadOnlyList<string> arguments)
    {
        var startInfo = new ProcessStartInfo(executable)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);
        using var process = Process.Start(startInfo)
            ?? throw new InvalidOperationException($"Could not start {executable}.");
        var standardOutput = process.StandardOutput.ReadToEndAsync();
        var standardError = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(120_000))
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException($"{executable} did not complete within 120 seconds.");
        }
        Task.WaitAll(standardOutput, standardError);
        if (process.ExitCode != 0)
            throw new InvalidOperationException(
                $"{executable} failed with exit code {process.ExitCode}: {standardError.Result.Trim()}");
        return standardOutput.Result;
    }

    private sealed record WindowsConfiguration(
        int Width,
        int Height,
        string Background,
        string Foreground,
        string Family,
        WindowsGlyphCase[] Cases);

    private sealed record WindowsGlyphCase(
        string Id,
        string Text,
        float Size,
        int Weight,
        float X,
        float Baseline);

    private sealed record WindowsRunMetrics(
        string Id,
        uint[] Glyphs,
        uint[] Clusters,
        float[][] Positions,
        float[] Advances,
        float[][] Offsets,
        float Width,
        NativeTextRunFaceIdentity? FaceIdentity,
        float DeviceScale,
        float[] CanvasMatrix);

    private sealed record WindowsPixelComparison(
        string Source,
        string Reference,
        int OffsetX,
        int OffsetY,
        double MeanAbsoluteError,
        double RootMeanSquareError,
        int DifferingPixels,
        int Samples);

    private sealed record WindowsCaseReport(
        string Id,
        bool IsolatedGlyph,
        List<WindowsPixelComparison> Comparisons,
        WindowsRunMetrics DirectWrite);

    private sealed record WindowsScaleReport(
        float DeviceScaleFactor,
        List<WindowsCaseReport> Cases,
        WindowsScaleSummary Summary);

    private sealed record WindowsScaleSummary(
        double HarfBuzzMeanAbsoluteError,
        double DirectWriteMeanAbsoluteError,
        double MaximumPerCaseRegression,
        double MaximumIsolatedGlyphRegression,
        bool PassesPixelGate);

    private sealed record WindowsDiagnosticReport(
        DateTimeOffset GeneratedUtc,
        string OperatingSystem,
        string RuntimeIdentifier,
        string ChromiumVersion,
        string Command,
        WindowsPerformanceReport Performance,
        List<WindowsScaleReport> Scales);

    private sealed record WindowsPerformanceReport(
        string CaseId,
        double ColdShapeMilliseconds,
        long ColdAllocatedBytes,
        long ColdManagedMemoryDelta,
        double WarmShapeNanoseconds,
        double WarmAllocatedBytes,
        double DrawNanoseconds,
        double DrawAllocatedBytes,
        double HarfBuzzDrawNanoseconds,
        double HarfBuzzDrawAllocatedBytes,
        bool PassesBudget,
        int MaximumCachedRuns,
        int MaximumCachedFaces);

    private readonly record struct PixelBox(int Left, int Top, int Right, int Bottom);

    private readonly record struct ImageSlice(SKBitmap Bitmap, int OffsetY)
    {
        internal SKColor GetPixel(int x, int y)
        {
            var resolvedY = y + OffsetY;
            return x >= 0 && x < Bitmap.Width && resolvedY >= 0 && resolvedY < Bitmap.Height
                ? Bitmap.GetPixel(x, resolvedY)
                : SKColors.Transparent;
        }
    }
}

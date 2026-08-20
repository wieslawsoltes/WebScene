using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using WebScene.Backends.Avalonia.Native;

var configurationPath = Path.Combine(AppContext.BaseDirectory, "cases.json");
var chromeDocumentPath = Path.Combine(AppContext.BaseDirectory, "chrome.html");
var chromeCapturePath = Path.Combine(AppContext.BaseDirectory, "ChromeCapture.mjs");
var coreTextSourcePath = Path.Combine(AppContext.BaseDirectory, "CoreTextRenderer.swift");
var outputDirectory = ResolveOutputDirectory(args);
Directory.CreateDirectory(outputDirectory);

if (!OperatingSystem.IsMacOS())
{
    throw new PlatformNotSupportedException(
        "The glyph diagnostic currently compares the macOS system face through CoreText.");
}

var configuration = JsonSerializer.Deserialize<Configuration>(
    File.ReadAllText(configurationPath),
    new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
    ?? throw new InvalidDataException("Could not read glyph diagnostic cases.");
var chromeExecutable = ResolveChromeExecutable();
var coreTextExecutable = Path.Combine(outputDirectory, "coretext-renderer");

RunProcess("swiftc", [coreTextSourcePath, "-o", coreTextExecutable]);
foreach (var scale in new[] { 1f, 2f })
{
    RenderSkia(configuration, scale, RenderMode.Production,
        Path.Combine(outputDirectory, $"skia-production-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"skia-production-{scale:0}x.metrics.json"));
    RenderSkia(configuration, scale, RenderMode.ShapedDefault,
        Path.Combine(outputDirectory, $"skia-shaped-default-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"skia-shaped-default-{scale:0}x.metrics.json"));
    RenderSkia(configuration, scale, RenderMode.PlatformAdvances,
        Path.Combine(outputDirectory, $"skia-platform-advances-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"skia-platform-advances-{scale:0}x.metrics.json"));
    RenderSkia(configuration, scale, RenderMode.PlatformAdvancesWithHarfBuzzKerning,
        Path.Combine(outputDirectory, $"skia-platform-hb-kerning-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"skia-platform-hb-kerning-{scale:0}x.metrics.json"));
    RenderSkia(configuration, scale, RenderMode.HarfBuzzVariations,
        Path.Combine(outputDirectory, $"skia-hb-variations-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"skia-hb-variations-{scale:0}x.metrics.json"));
    RenderManagedSkiaHarfBuzz(configuration, scale, propagateRequestedVariations: false,
        Path.Combine(outputDirectory, $"managed-skia-hb-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"managed-skia-hb-{scale:0}x.metrics.json"));
    RenderManagedSkiaHarfBuzz(configuration, scale, propagateRequestedVariations: true,
        Path.Combine(outputDirectory, $"managed-skia-hb-variations-{scale:0}x.png"),
        Path.Combine(outputDirectory, $"managed-skia-hb-variations-{scale:0}x.metrics.json"));
    var managedCoreTextMetricsPath = Path.Combine(
        outputDirectory, $"managed-coretext-{scale:0}x.metrics.json");
    File.WriteAllText(
        managedCoreTextMetricsPath,
        JsonSerializer.Serialize(
            ManagedCoreTextPositioner.Shape(configuration),
            new JsonSerializerOptions { WriteIndented = true }));
    RenderSkiaAtCoreTextPositions(
        configuration,
        scale,
        managedCoreTextMetricsPath,
        Path.Combine(outputDirectory, $"managed-coretext-skia-{scale:0}x.png"));

    var coreTextMetricsPath = Path.Combine(
        outputDirectory, $"coretext-{scale:0}x.metrics.json");
    RunProcess(coreTextExecutable,
    [
        configurationPath,
        scale.ToString(CultureInfo.InvariantCulture),
        Path.Combine(outputDirectory, $"coretext-{scale:0}x.png"),
        coreTextMetricsPath
    ]);
    RenderSkiaAtCoreTextPositions(
        configuration,
        scale,
        coreTextMetricsPath,
        Path.Combine(outputDirectory, $"skia-coretext-positions-{scale:0}x.png"));

    var chromeScreenshot = Path.Combine(outputDirectory, $"chrome-{scale:0}x.png");
    RunProcess("node",
    [
        chromeCapturePath,
        "--chrome", chromeExecutable,
        "--document", chromeDocumentPath,
        "--output", chromeScreenshot,
        "--metrics", Path.Combine(outputDirectory, $"chrome-{scale:0}x.metrics.json"),
        "--scale", scale.ToString(CultureInfo.InvariantCulture),
        "--width", configuration.Width.ToString(CultureInfo.InvariantCulture),
        "--height", (configuration.Height * 2).ToString(CultureInfo.InvariantCulture)
    ]);
    RenderSkiaAtChromePrefixPositions(
        configuration,
        scale,
        Path.Combine(outputDirectory, $"chrome-{scale:0}x.metrics.json"),
        Path.Combine(outputDirectory, $"skia-chrome-positions-{scale:0}x.png"));
}

var report = Analyze(configuration, outputDirectory);
var jsonOptions = new JsonSerializerOptions { WriteIndented = true };
File.WriteAllText(
    Path.Combine(outputDirectory, "report.json"),
    JsonSerializer.Serialize(report, jsonOptions));
File.WriteAllText(
    Path.Combine(outputDirectory, "report.md"),
    FormatReport(report));
Console.WriteLine(Path.Combine(outputDirectory, "report.md"));

static string ResolveOutputDirectory(string[] arguments)
{
    var index = Array.IndexOf(arguments, "--output");
    if (index >= 0 && index + 1 < arguments.Length)
    {
        return Path.GetFullPath(arguments[index + 1]);
    }
    return Path.GetFullPath(Path.Combine(
        Directory.GetCurrentDirectory(),
        "TestResults",
        "GlyphDiagnostics"));
}

static string ResolveChromeExecutable()
{
    var configured = Environment.GetEnvironmentVariable("WEBSCENE_CHROMIUM_EXECUTABLE");
    if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured)) return configured;
    const string macChrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    if (File.Exists(macChrome)) return macChrome;
    throw new FileNotFoundException(
        "Set WEBSCENE_CHROMIUM_EXECUTABLE to a Chromium-compatible executable.");
}

static string RunProcess(string executable, IReadOnlyList<string> arguments)
{
    var startInfo = new ProcessStartInfo(executable)
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false
    };
    foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);
    return RunProcessInfo(startInfo);
}

static string RunProcessInfo(ProcessStartInfo startInfo)
{
    using var process = Process.Start(startInfo)
        ?? throw new InvalidOperationException($"Could not start {startInfo.FileName}.");
    var outputTask = process.StandardOutput.ReadToEndAsync();
    var errorTask = process.StandardError.ReadToEndAsync();
    if (!process.WaitForExit(60_000))
    {
        process.Kill(entireProcessTree: true);
        throw new TimeoutException($"{startInfo.FileName} did not complete within 60 seconds.");
    }
    Task.WaitAll(outputTask, errorTask);
    if (process.ExitCode != 0)
    {
        throw new InvalidOperationException(
            $"{startInfo.FileName} failed with exit code {process.ExitCode}: {errorTask.Result.Trim()}");
    }
    return outputTask.Result;
}

static void RenderSkia(
    Configuration configuration,
    float scale,
    RenderMode mode,
    string imagePath,
    string metricsPath)
{
    var info = new SKImageInfo(
        checked((int)(configuration.Width * scale)),
        checked((int)(configuration.Height * scale)),
        SKColorType.Bgra8888,
        SKAlphaType.Premul,
        SKColorSpace.CreateSrgb());
    using var surface = SKSurface.Create(info)
        ?? throw new InvalidOperationException("Could not create the Skia diagnostic surface.");
    var canvas = surface.Canvas;
    canvas.Clear(SKColor.Parse(configuration.Background));
    canvas.Scale(scale);
    var metrics = new List<SkiaRunMetrics>();
    foreach (var item in configuration.Cases)
    {
        var typeface = NativeTextShaping.ResolveTypeface(configuration.Family, item.Weight);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = item.Size,
            Color = SKColor.Parse(configuration.Foreground),
            IsAntialias = true
        };
        using var shaper = new SKShaper(typeface);
        if (mode == RenderMode.HarfBuzzVariations)
        {
            ConfigureHarfBuzzVariations(shaper, item.Size, item.Weight);
        }
        var features = NativeTextShaping.ResolveFeatureFlags(
            item.Text, configuration.Family, 0);
        var shaped = shaper.Shape(item.Text, 0, item.Baseline, paint);
        var width = NativeTextShaping.MeasureShapedWidth(shaper, item.Text, paint, features);
        var widthScale = mode == RenderMode.Production
            ? NativeTextShaping.ResolveShapedWidthScale(
                item.Text,
                configuration.Family,
                item.Size,
                item.Weight,
                paint,
                width,
                features)
            : 1f;
        if (mode == RenderMode.Production)
        {
            NativeTextShaping.DrawShapedText(
                canvas,
                shaper,
                item.Text,
                item.X,
                item.Baseline,
                paint,
                features,
                NativeTextShaping.ResolveTabularDigitScale(configuration.Family),
                widthScale,
                width,
                scale);
        }
        else if (mode == RenderMode.PlatformAdvances)
        {
            DrawPlatformAdvanceRun(canvas, shaper, shaped, item.X, paint, scale);
        }
        else if (mode == RenderMode.PlatformAdvancesWithHarfBuzzKerning)
        {
            DrawPlatformAdvanceRun(
                canvas, shaper, shaped, item.X, paint, scale, item.Text,
                preserveHarfBuzzKerning: true);
        }
        else
        {
            DrawDefaultShapedRun(canvas, shaper, shaped, item.X, paint);
        }
        metrics.Add(new SkiaRunMetrics(
            item.Id,
            shaped.Codepoints,
            shaped.Points.Select(point => new[] { point.X, point.Y }).ToArray(),
            width,
            widthScale,
            paint.MeasureText(item.Text)));
    }
    canvas.Flush();
    using var image = surface.Snapshot();
    using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
    using var stream = File.Create(imagePath);
    encoded.SaveTo(stream);
    File.WriteAllText(metricsPath, JsonSerializer.Serialize(
        metrics,
        new JsonSerializerOptions { WriteIndented = true }));
}

static void ConfigureHarfBuzzVariations(SKShaper shaper, float size, int weight)
{
    var field = typeof(SKShaper).GetField(
        "font",
        BindingFlags.Instance | BindingFlags.NonPublic)
        ?? typeof(SKShaper).GetField(
            "hbFont",
            BindingFlags.Instance | BindingFlags.NonPublic)
        ?? throw new MissingFieldException(
            typeof(SKShaper).FullName,
            "font/hbFont");
    if (field.GetValue(shaper) is not HarfBuzzSharp.Font font)
    {
        throw new InvalidOperationException("Could not access the diagnostic HarfBuzz font.");
    }
    var variations = new[]
    {
        new HarfBuzzVariation(Tag("opsz"), size),
        new HarfBuzzVariation(Tag("wght"), weight)
    };
    HarfBuzzNative.SetVariations(font.Handle, variations, (uint)variations.Length);

    static uint Tag(string value)
        => ((uint)value[0] << 24)
           | ((uint)value[1] << 16)
           | ((uint)value[2] << 8)
           | value[3];
}

static void RenderManagedSkiaHarfBuzz(
    Configuration configuration,
    float scale,
    bool propagateRequestedVariations,
    string imagePath,
    string metricsPath)
{
    var info = new SKImageInfo(
        checked((int)(configuration.Width * scale)),
        checked((int)(configuration.Height * scale)),
        SKColorType.Bgra8888,
        SKAlphaType.Premul,
        SKColorSpace.CreateSrgb());
    using var surface = SKSurface.Create(info)
        ?? throw new InvalidOperationException("Could not create the managed HarfBuzz diagnostic surface.");
    var canvas = surface.Canvas;
    canvas.Clear(SKColor.Parse(configuration.Background));
    canvas.Scale(scale);
    var metrics = new List<SkiaRunMetrics>();
    foreach (var item in configuration.Cases)
    {
        var typeface = NativeTextShaping.ResolveTypeface(configuration.Family, item.Weight);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = item.Size,
            Color = SKColor.Parse(configuration.Foreground),
            IsAntialias = true
        };
        using var font = paint.ToFont();
        font.Typeface = typeface;
        var rasterization = NativeTextShaping.ResolveFontRasterizationProfile(scale);
        font.Subpixel = rasterization.Subpixel;
        font.BaselineSnap = rasterization.BaselineSnap;
        using var shaper = new ManagedSkiaHarfBuzzShaper(
            typeface,
            font,
            propagateRequestedVariations,
            item.Size,
            item.Weight);
        var shaped = shaper.Shape(item.Text, item.Baseline);
        DrawManagedShapedRun(canvas, shaped, item.X, paint, font);
        metrics.Add(new SkiaRunMetrics(
            item.Id,
            shaped.Codepoints,
            shaped.Points.Select(point => new[] { point.X, point.Y }).ToArray(),
            shaped.Width,
            1,
            paint.MeasureText(item.Text)));
    }
    canvas.Flush();
    using var image = surface.Snapshot();
    using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
    using var stream = File.Create(imagePath);
    encoded.SaveTo(stream);
    File.WriteAllText(metricsPath, JsonSerializer.Serialize(
        metrics,
        new JsonSerializerOptions { WriteIndented = true }));
}

static void DrawManagedShapedRun(
    SKCanvas canvas,
    ManagedShapeResult shaped,
    float x,
    SKPaint paint,
    SKFont font)
{
    using var builder = new SKTextBlobBuilder();
    var count = Math.Min(shaped.Codepoints.Length, shaped.Points.Length);
    var run = builder.AllocatePositionedRun(font, count);
    var glyphs = run.GetGlyphSpan();
    var positions = run.GetPositionSpan();
    for (var index = 0; index < count; index++)
    {
        glyphs[index] = checked((ushort)shaped.Codepoints[index]);
        positions[index] = new SKPoint(x + shaped.Points[index].X, shaped.Points[index].Y);
    }
    using var blob = builder.Build();
    if (blob is not null) canvas.DrawText(blob, 0, 0, paint);
}

static void RenderSkiaAtCoreTextPositions(
    Configuration configuration,
    float scale,
    string metricsPath,
    string imagePath)
{
    var coreTextMetrics = JsonSerializer.Deserialize<CoreTextRunMetrics[]>(
        File.ReadAllText(metricsPath),
        new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
        ?? throw new InvalidDataException("Could not read CoreText run positions.");
    var positionsById = coreTextMetrics.ToDictionary(item => item.Id, StringComparer.Ordinal);
    var info = new SKImageInfo(
        checked((int)(configuration.Width * scale)),
        checked((int)(configuration.Height * scale)),
        SKColorType.Bgra8888,
        SKAlphaType.Premul,
        SKColorSpace.CreateSrgb());
    using var surface = SKSurface.Create(info)
        ?? throw new InvalidOperationException("Could not create the positioned Skia surface.");
    var canvas = surface.Canvas;
    canvas.Clear(SKColor.Parse(configuration.Background));
    canvas.Scale(scale);
    foreach (var item in configuration.Cases)
    {
        var typeface = NativeTextShaping.ResolveTypeface(configuration.Family, item.Weight);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = item.Size,
            Color = SKColor.Parse(configuration.Foreground),
            IsAntialias = true
        };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(item.Text, 0, item.Baseline, paint);
        var coreText = positionsById[item.Id];
        if (!shaped.Codepoints.SequenceEqual(coreText.Glyphs)
            || shaped.Codepoints.Length != coreText.Positions.Length)
        {
            throw new InvalidDataException(
                $"CoreText and Skia selected different glyphs for '{item.Id}'.");
        }
        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        var rasterization = NativeTextShaping.ResolveFontRasterizationProfile(scale);
        font.Subpixel = rasterization.Subpixel;
        font.BaselineSnap = rasterization.BaselineSnap;
        using var builder = new SKTextBlobBuilder();
        var run = builder.AllocatePositionedRun(font, shaped.Codepoints.Length);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        for (var index = 0; index < shaped.Codepoints.Length; index++)
        {
            glyphs[index] = checked((ushort)shaped.Codepoints[index]);
            positions[index] = new SKPoint(
                item.X + coreText.Positions[index][0],
                item.Baseline + coreText.Positions[index][1]);
        }
        using var blob = builder.Build();
        if (blob is not null) canvas.DrawText(blob, 0, 0, paint);
    }
    canvas.Flush();
    using var image = surface.Snapshot();
    using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
    using var stream = File.Create(imagePath);
    encoded.SaveTo(stream);
}

static void RenderSkiaAtChromePrefixPositions(
    Configuration configuration,
    float scale,
    string metricsPath,
    string imagePath)
{
    var chromeMetrics = JsonSerializer.Deserialize<ChromeRunMetrics[]>(
        File.ReadAllText(metricsPath),
        new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
        ?? throw new InvalidDataException("Could not read Chromium prefix positions.");
    var positionsById = chromeMetrics.ToDictionary(item => item.Id, StringComparer.Ordinal);
    var info = new SKImageInfo(
        checked((int)(configuration.Width * scale)),
        checked((int)(configuration.Height * scale)),
        SKColorType.Bgra8888,
        SKAlphaType.Premul,
        SKColorSpace.CreateSrgb());
    using var surface = SKSurface.Create(info)
        ?? throw new InvalidOperationException("Could not create the Chromium-positioned Skia surface.");
    var canvas = surface.Canvas;
    canvas.Clear(SKColor.Parse(configuration.Background));
    canvas.Scale(scale);
    foreach (var item in configuration.Cases)
    {
        var typeface = NativeTextShaping.ResolveTypeface(configuration.Family, item.Weight);
        using var paint = new SKPaint
        {
            Typeface = typeface,
            TextSize = item.Size,
            Color = SKColor.Parse(configuration.Foreground),
            IsAntialias = true
        };
        using var shaper = new SKShaper(typeface);
        var shaped = shaper.Shape(item.Text, 0, item.Baseline, paint);
        var chrome = positionsById[item.Id];
        if (shaped.Codepoints.Length != chrome.PrefixPositions.Length)
        {
            throw new InvalidDataException(
                $"Chromium prefix count and Skia glyph count differ for '{item.Id}'.");
        }
        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        var rasterization = NativeTextShaping.ResolveFontRasterizationProfile(scale);
        font.Subpixel = rasterization.Subpixel;
        font.BaselineSnap = rasterization.BaselineSnap;
        using var builder = new SKTextBlobBuilder();
        var run = builder.AllocatePositionedRun(font, shaped.Codepoints.Length);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        for (var index = 0; index < shaped.Codepoints.Length; index++)
        {
            glyphs[index] = checked((ushort)shaped.Codepoints[index]);
            positions[index] = new SKPoint(
                item.X + chrome.PrefixPositions[index],
                shaped.Points[index].Y);
        }
        using var blob = builder.Build();
        if (blob is not null) canvas.DrawText(blob, 0, 0, paint);
    }
    canvas.Flush();
    using var image = surface.Snapshot();
    using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
    using var stream = File.Create(imagePath);
    encoded.SaveTo(stream);
}

static void DrawPlatformAdvanceRun(
    SKCanvas canvas,
    SKShaper shaper,
    SKShaper.Result shaped,
    float x,
    SKPaint paint,
    float deviceScaleFactor,
    string? text = null,
    bool preserveHarfBuzzKerning = false)
{
    using var font = paint.ToFont();
    font.Typeface = shaper.Typeface;
    var rasterization = NativeTextShaping.ResolveFontRasterizationProfile(deviceScaleFactor);
    font.Subpixel = rasterization.Subpixel;
    font.BaselineSnap = rasterization.BaselineSnap;
    var count = Math.Min(shaped.Codepoints.Length, shaped.Points.Length);
    var glyphs = new ushort[count];
    for (var index = 0; index < count; index++)
    {
        glyphs[index] = checked((ushort)shaped.Codepoints[index]);
    }
    var positions = new SKPoint[count];
    font.GetGlyphPositions(
        glyphs,
        positions,
        new SKPoint(x, count == 0 ? 0 : shaped.Points[0].Y));
    if (preserveHarfBuzzKerning
        && text is not null
        && TryResolveHarfBuzzKerningOffsets(
            shaper, shaped, text, paint, out var kerningOffsets))
    {
        for (var index = 0; index < count; index++)
        {
            positions[index].X += kerningOffsets[index];
            positions[index].Y += shaped.Points[index].Y - shaped.Points[0].Y;
        }
    }
    using var builder = new SKTextBlobBuilder();
    var run = builder.AllocatePositionedRun(font, count);
    glyphs.CopyTo(run.GetGlyphSpan());
    positions.CopyTo(run.GetPositionSpan());
    using var blob = builder.Build();
    if (blob is not null) canvas.DrawText(blob, 0, 0, paint);
}

static bool TryResolveHarfBuzzKerningOffsets(
    SKShaper shaper,
    SKShaper.Result shaped,
    string text,
    SKPaint paint,
    out float[] offsets)
{
    offsets = new float[shaped.Codepoints.Length];
    var runes = text.EnumerateRunes().ToArray();
    if (runes.Length != shaped.Codepoints.Length) return false;

    var nominalCursor = 0f;
    for (var index = 0; index < runes.Length; index++)
    {
        var isolated = shaper.Shape(runes[index].ToString(), paint);
        if (isolated.Codepoints.Length != 1
            || isolated.Codepoints[0] != shaped.Codepoints[index])
        {
            return false;
        }
        offsets[index] = shaped.Points[index].X - nominalCursor;
        nominalCursor += isolated.Width;
    }
    return true;
}

static void DrawDefaultShapedRun(
    SKCanvas canvas,
    SKShaper shaper,
    SKShaper.Result shaped,
    float x,
    SKPaint paint)
{
    using var font = paint.ToFont();
    font.Typeface = shaper.Typeface;
    using var builder = new SKTextBlobBuilder();
    var count = Math.Min(shaped.Codepoints.Length, shaped.Points.Length);
    var run = builder.AllocatePositionedRun(font, count);
    var glyphs = run.GetGlyphSpan();
    var positions = run.GetPositionSpan();
    for (var index = 0; index < count; index++)
    {
        glyphs[index] = checked((ushort)shaped.Codepoints[index]);
        positions[index] = new SKPoint(x + shaped.Points[index].X, shaped.Points[index].Y);
    }
    using var blob = builder.Build();
    if (blob is not null) canvas.DrawText(blob, 0, 0, paint);
}

static DiagnosticReport Analyze(Configuration configuration, string outputDirectory)
{
    var scales = new List<ScaleReport>();
    foreach (var scale in new[] { 1, 2 })
    {
        var productionMetrics = JsonSerializer.Deserialize<SkiaRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"skia-production-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        var managedMetrics = JsonSerializer.Deserialize<SkiaRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"managed-skia-hb-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        var managedVariationMetrics = JsonSerializer.Deserialize<SkiaRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"managed-skia-hb-variations-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        var managedCoreTextMetrics = JsonSerializer.Deserialize<CoreTextRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"managed-coretext-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        var coreTextRunMetrics = JsonSerializer.Deserialize<CoreTextRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"coretext-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        var chromeRunMetrics = JsonSerializer.Deserialize<ChromeRunMetrics[]>(
            File.ReadAllText(Path.Combine(
                outputDirectory, $"chrome-{scale}x.metrics.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!
            .ToDictionary(item => item.Id, StringComparer.Ordinal);
        using var production = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-production-{scale}x.png"));
        using var shapedDefault = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-shaped-default-{scale}x.png"));
        using var platformAdvances = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-platform-advances-{scale}x.png"));
        using var platformHarfBuzzKerning = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-platform-hb-kerning-{scale}x.png"));
        using var harfBuzzVariations = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-hb-variations-{scale}x.png"));
        using var managedSkiaHarfBuzz = SKBitmap.Decode(Path.Combine(outputDirectory, $"managed-skia-hb-{scale}x.png"));
        using var managedSkiaHarfBuzzVariations = SKBitmap.Decode(Path.Combine(outputDirectory, $"managed-skia-hb-variations-{scale}x.png"));
        using var managedCoreTextSkia = SKBitmap.Decode(Path.Combine(outputDirectory, $"managed-coretext-skia-{scale}x.png"));
        using var coreTextPositions = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-coretext-positions-{scale}x.png"));
        using var chromePositions = SKBitmap.Decode(Path.Combine(outputDirectory, $"skia-chrome-positions-{scale}x.png"));
        using var coreText = SKBitmap.Decode(Path.Combine(outputDirectory, $"coretext-{scale}x.png"));
        using var chrome = SKBitmap.Decode(Path.Combine(outputDirectory, $"chrome-{scale}x.png"));
        var expectedWidth = configuration.Width * scale;
        var expectedHeight = configuration.Height * scale;
        ValidateDimensions("Skia production", production, expectedWidth, expectedHeight);
        ValidateDimensions("Skia shaped default", shapedDefault, expectedWidth, expectedHeight);
        ValidateDimensions("Skia platform advances", platformAdvances, expectedWidth, expectedHeight);
        ValidateDimensions("Skia platform advances with HarfBuzz kerning", platformHarfBuzzKerning, expectedWidth, expectedHeight);
        ValidateDimensions("Skia HarfBuzz variations", harfBuzzVariations, expectedWidth, expectedHeight);
        ValidateDimensions("Managed Skia-backed HarfBuzz", managedSkiaHarfBuzz, expectedWidth, expectedHeight);
        ValidateDimensions("Managed Skia-backed HarfBuzz with variations", managedSkiaHarfBuzzVariations, expectedWidth, expectedHeight);
        ValidateDimensions("Managed CoreText positions with Skia raster", managedCoreTextSkia, expectedWidth, expectedHeight);
        ValidateDimensions("Skia at CoreText positions", coreTextPositions, expectedWidth, expectedHeight);
        ValidateDimensions("Skia at Chromium prefix positions", chromePositions, expectedWidth, expectedHeight);
        ValidateDimensions("CoreText", coreText, expectedWidth, expectedHeight);
        if (chrome.Width < expectedWidth || chrome.Height < expectedHeight * 2)
        {
            throw new InvalidDataException(
                $"Chromium image was {chrome.Width}x{chrome.Height}; expected at least "
                + $"{expectedWidth}x{expectedHeight * 2}.");
        }

        var sources = new Dictionary<string, ImageView>
        {
            ["skia-production"] = new(production, 0, 0),
            ["skia-shaped-default"] = new(shapedDefault, 0, 0),
            ["skia-platform-advances"] = new(platformAdvances, 0, 0),
            ["skia-platform-hb-kerning"] = new(platformHarfBuzzKerning, 0, 0),
            ["skia-hb-variations"] = new(harfBuzzVariations, 0, 0),
            ["managed-skia-hb"] = new(managedSkiaHarfBuzz, 0, 0),
            ["managed-skia-hb-variations"] = new(managedSkiaHarfBuzzVariations, 0, 0),
            ["managed-coretext-skia"] = new(managedCoreTextSkia, 0, 0),
            ["skia-coretext-positions"] = new(coreTextPositions, 0, 0),
            ["skia-chrome-positions"] = new(chromePositions, 0, 0),
            ["coretext"] = new(coreText, 0, 0),
            ["chrome-canvas"] = new(chrome, 0, 0),
            ["chrome-dom"] = new(chrome, 0, expectedHeight)
        };
        var cases = new List<CaseReport>();
        foreach (var item in configuration.Cases)
        {
            var top = Math.Max(0, (int)Math.Floor((item.Baseline - item.Size * 1.6f) * scale));
            var bottom = Math.Min(expectedHeight, (int)Math.Ceiling((item.Baseline + item.Size * .65f) * scale));
            var left = Math.Max(0, (int)Math.Floor((item.X - 4) * scale));
            var estimatedWidth = Math.Max(80, item.Text.Length * item.Size * .8f + 20);
            var right = Math.Min(
                expectedWidth,
                (int)Math.Ceiling((item.X + estimatedWidth) * scale));
            var region = new PixelRegion(left, top, right, bottom);
            var ink = sources.ToDictionary(
                pair => pair.Key,
                pair => MeasureInk(pair.Value, region, configuration));
            var comparisons = new List<PixelComparison>();
            foreach (var candidate in new[]
                     {
                         "skia-production", "skia-shaped-default", "skia-platform-advances",
                         "skia-platform-hb-kerning",
                         "skia-hb-variations",
                         "managed-skia-hb",
                         "managed-skia-hb-variations",
                         "managed-coretext-skia",
                         "skia-coretext-positions",
                         "skia-chrome-positions",
                         "coretext", "chrome-dom"
                     })
            {
                comparisons.Add(Compare(
                    candidate,
                    sources[candidate],
                    sources["chrome-canvas"],
                    region,
                    configuration,
                    ink[candidate].Bounds,
                    ink["chrome-canvas"].Bounds,
                    refinementRadius: 2 * scale));
            }
            var productionRun = productionMetrics[item.Id];
            var managedRun = managedMetrics[item.Id];
            var managedVariationRun = managedVariationMetrics[item.Id];
            var managedCoreTextRun = managedCoreTextMetrics[item.Id];
            var coreTextRun = coreTextRunMetrics[item.Id];
            var chromeRun = chromeRunMetrics[item.Id];
            var positionComparisons = new List<PositionComparison>
            {
                ComparePositions(
                    "skia-production",
                    productionRun.Positions.Select(
                        position => position[0] * productionRun.WidthScale),
                    chromeRun.PrefixPositions),
                ComparePositions(
                    "managed-skia-hb",
                    managedRun.Positions.Select(position => position[0]),
                    chromeRun.PrefixPositions),
                ComparePositions(
                    "managed-skia-hb-variations",
                    managedVariationRun.Positions.Select(position => position[0]),
                    chromeRun.PrefixPositions),
                ComparePositions(
                    "managed-coretext",
                    managedCoreTextRun.Positions.Select(position => position[0]),
                    chromeRun.PrefixPositions),
                ComparePositions(
                    "coretext",
                    coreTextRun.Positions.Select(position => position[0]),
                    chromeRun.PrefixPositions)
            };
            cases.Add(new CaseReport(
                item.Id,
                item.Text.EnumerateRunes().Count() == 1,
                ink,
                comparisons,
                positionComparisons));
        }
        scales.Add(new ScaleReport(scale, cases));
    }
    return new DiagnosticReport(
        DateTimeOffset.UtcNow,
        Environment.OSVersion.VersionString,
        scales);
}

static PositionComparison ComparePositions(
    string source,
    IEnumerable<float> sourcePositions,
    IReadOnlyList<float> chromePrefixPositions)
{
    var positions = sourcePositions.ToArray();
    var count = Math.Min(positions.Length, chromePrefixPositions.Count);
    if (count == 0) return new PositionComparison(source, 0, 0, 0);
    var total = 0d;
    var maximum = 0d;
    for (var index = 0; index < count; index++)
    {
        var delta = Math.Abs(positions[index] - chromePrefixPositions[index]);
        total += delta;
        maximum = Math.Max(maximum, delta);
    }
    return new PositionComparison(source, total / count, maximum, count);
}

static void ValidateDimensions(string source, SKBitmap bitmap, int width, int height)
{
    if (bitmap.Width != width || bitmap.Height != height)
    {
        throw new InvalidDataException(
            $"{source} image was {bitmap.Width}x{bitmap.Height}; expected {width}x{height}.");
    }
}

static InkMetrics MeasureInk(
    ImageView image,
    PixelRegion region,
    Configuration configuration)
{
    var background = SKColor.Parse(configuration.Background);
    var foreground = SKColor.Parse(configuration.Foreground);
    var left = region.Right;
    var top = region.Bottom;
    var right = -1;
    var bottom = -1;
    var coverageSum = 0d;
    var edgePixels = 0;
    var solidPixels = 0;
    var visiblePixels = 0;
    for (var y = region.Top; y < region.Bottom; y++)
    {
        for (var x = region.Left; x < region.Right; x++)
        {
            var coverage = Coverage(image.GetPixel(x, y), background, foreground);
            if (coverage < .02) continue;
            visiblePixels++;
            coverageSum += coverage;
            if (coverage >= .95) solidPixels++;
            else edgePixels++;
            left = Math.Min(left, x);
            top = Math.Min(top, y);
            right = Math.Max(right, x);
            bottom = Math.Max(bottom, y);
        }
    }
    return new InkMetrics(
        visiblePixels,
        edgePixels,
        solidPixels,
        coverageSum,
        right < 0 ? null : new PixelRegion(left, top, right + 1, bottom + 1));
}

static PixelComparison Compare(
    string sourceName,
    ImageView source,
    ImageView reference,
    PixelRegion region,
    Configuration configuration,
    PixelRegion? sourceBounds,
    PixelRegion? referenceBounds,
    int refinementRadius)
{
    var background = SKColor.Parse(configuration.Background);
    var foreground = SKColor.Parse(configuration.Foreground);
    var initialX = sourceBounds.HasValue && referenceBounds.HasValue
        ? referenceBounds.Value.Left - sourceBounds.Value.Left
        : 0;
    var initialY = sourceBounds.HasValue && referenceBounds.HasValue
        ? referenceBounds.Value.Top - sourceBounds.Value.Top
        : 0;
    var best = (Dx: initialX, Dy: initialY, Error: double.MaxValue,
        Rmse: 0d, Different: 0, Samples: 0);
    for (var dy = initialY - refinementRadius; dy <= initialY + refinementRadius; dy++)
    {
        for (var dx = initialX - refinementRadius; dx <= initialX + refinementRadius; dx++)
        {
            var absolute = 0d;
            var squared = 0d;
            var different = 0;
            var samples = 0;
            for (var y = region.Top; y < region.Bottom; y++)
            {
                for (var x = region.Left; x < region.Right; x++)
                {
                    var sourceCoverage = Coverage(source.GetPixel(x, y), background, foreground);
                    var referenceCoverage = Coverage(reference.GetPixel(x + dx, y + dy), background, foreground);
                    var delta = Math.Abs(sourceCoverage - referenceCoverage);
                    absolute += delta;
                    squared += delta * delta;
                    if (delta >= .05) different++;
                    samples++;
                }
            }
            var error = absolute / samples;
            if (error < best.Error)
            {
                best = (dx, dy, error, Math.Sqrt(squared / samples), different, samples);
            }
        }
    }
    return new PixelComparison(
        sourceName,
        "chrome-canvas",
        best.Dx,
        best.Dy,
        best.Error,
        best.Rmse,
        best.Different,
        best.Samples);
}

static double Coverage(SKColor pixel, SKColor background, SKColor foreground)
{
    static double Channel(byte value, byte background, byte foreground)
        => foreground == background
            ? 0
            : Math.Clamp((value - background) / (double)(foreground - background), 0, 1);
    return (Channel(pixel.Red, background.Red, foreground.Red)
            + Channel(pixel.Green, background.Green, foreground.Green)
            + Channel(pixel.Blue, background.Blue, foreground.Blue)) / 3;
}

static string FormatReport(DiagnosticReport report)
{
    static string Number(double value, string format = "F5")
        => value.ToString(format, CultureInfo.InvariantCulture);

    var builder = new StringBuilder();
    builder.AppendLine("# WebScene glyph diagnostic");
    builder.AppendLine();
    builder.AppendLine($"Generated: {report.GeneratedUtc:O}");
    builder.AppendLine();
    builder.AppendLine("Pixel error is measured after finding the best whole-device-pixel translation. "
        + "Lower MAE is closer to Chromium canvas rasterization; the reported shift isolates placement from shape.");
    builder.AppendLine();
    builder.AppendLine("Chromium prefix widths are reported as a glyph-origin proxy. They expose cumulative advance "
        + "drift but cannot represent every full-run kerning decision.");
    foreach (var scale in report.Scales)
    {
        builder.AppendLine();
        builder.AppendLine($"## {scale.DeviceScaleFactor}x");
        builder.AppendLine();
        var isolated = scale.Cases.Where(item => item.IsolatedGlyph).ToArray();
        var isolatedMatches = isolated.Count(item => item.Comparisons.Any(comparison =>
            comparison.Source == "skia-production"
            && comparison.DifferingPixels == 0));
        builder.AppendLine(
            $"WebScene production glyph masks are pixel-identical to Chromium for "
            + $"{isolatedMatches}/{isolated.Length} isolated glyphs.");
        builder.AppendLine();
        builder.AppendLine("| Multi-glyph source | Mean case MAE | Mean differing pixels |");
        builder.AppendLine("|---|---:|---:|");
        var multiGlyph = scale.Cases.Where(item => !item.IsolatedGlyph).ToArray();
        foreach (var source in new[]
                 {
                     "skia-production", "skia-hb-variations", "managed-skia-hb",
                     "managed-skia-hb-variations", "managed-coretext-skia",
                     "skia-coretext-positions", "skia-chrome-positions",
                     "coretext"
                 })
        {
            var sourceComparisons = multiGlyph.Select(item =>
                item.Comparisons.Single(comparison => comparison.Source == source)).ToArray();
            builder.AppendLine(
                $"| {source} | {Number(sourceComparisons.Average(item => item.MeanAbsoluteError))} "
                + $"| {Number(sourceComparisons.Average(item => item.DifferingPixels), "F1")} |");
        }
        builder.AppendLine();
        builder.AppendLine("| Case | Position source | prefix-origin MAE (CSS px) | maximum drift (CSS px) |");
        builder.AppendLine("|---|---|---:|---:|");
        foreach (var item in multiGlyph)
        {
            foreach (var position in item.PositionComparisons)
            {
                builder.AppendLine(
                    $"| {item.Id} | {position.Source} | {Number(position.MeanAbsoluteError, "F3")} "
                    + $"| {Number(position.MaximumAbsoluteError, "F3")} |");
            }
        }
        builder.AppendLine();
        builder.AppendLine("| Case | Source | shift | MAE | RMSE | differing | coverage | edge pixels |");
        builder.AppendLine("|---|---|---:|---:|---:|---:|---:|---:|");
        foreach (var item in scale.Cases)
        {
            foreach (var comparison in item.Comparisons)
            {
                var ink = item.Ink[comparison.Source];
                builder.AppendLine(
                    $"| {item.Id} | {comparison.Source} | {comparison.OffsetX},{comparison.OffsetY} "
                    + $"| {Number(comparison.MeanAbsoluteError)} | {Number(comparison.RootMeanSquareError)} "
                    + $"| {comparison.DifferingPixels}/{comparison.Samples} "
                    + $"| {Number(ink.CoverageSum, "F1")} | {ink.EdgePixels} |");
            }
        }
    }
    return builder.ToString();
}

sealed record Configuration(
    int Width,
    int Height,
    string Background,
    string Foreground,
    string Family,
    GlyphCase[] Cases);

sealed record GlyphCase(
    string Id,
    string Text,
    float Size,
    int Weight,
    float X,
    float Baseline);

sealed record SkiaRunMetrics(
    string Id,
    uint[] Glyphs,
    float[][] Positions,
    float ShapedWidth,
    float WidthScale,
    float PlatformWidth);

sealed record CoreTextRunMetrics(
    string Id,
    uint[] Glyphs,
    float[][] Positions);

sealed record ChromeRunMetrics(
    string Id,
    float[] PrefixPositions);

readonly record struct ImageView(SKBitmap Bitmap, int OffsetX, int OffsetY)
{
    public SKColor GetPixel(int x, int y)
    {
        var resolvedX = x + OffsetX;
        var resolvedY = y + OffsetY;
        return resolvedX >= 0 && resolvedX < Bitmap.Width
            && resolvedY >= 0 && resolvedY < Bitmap.Height
            ? Bitmap.GetPixel(resolvedX, resolvedY)
            : SKColors.Transparent;
    }
}

readonly record struct PixelRegion(int Left, int Top, int Right, int Bottom);
sealed record InkMetrics(
    int VisiblePixels,
    int EdgePixels,
    int SolidPixels,
    double CoverageSum,
    PixelRegion? Bounds);
sealed record PixelComparison(
    string Source,
    string Reference,
    int OffsetX,
    int OffsetY,
    double MeanAbsoluteError,
    double RootMeanSquareError,
    int DifferingPixels,
    int Samples);
sealed record PositionComparison(
    string Source,
    double MeanAbsoluteError,
    double MaximumAbsoluteError,
    int Samples);
sealed record CaseReport(
    string Id,
    bool IsolatedGlyph,
    Dictionary<string, InkMetrics> Ink,
    List<PixelComparison> Comparisons,
    List<PositionComparison> PositionComparisons);
sealed record ScaleReport(int DeviceScaleFactor, List<CaseReport> Cases);
sealed record DiagnosticReport(
    DateTimeOffset GeneratedUtc,
    string OperatingSystem,
    List<ScaleReport> Scales);

[StructLayout(LayoutKind.Sequential)]
readonly record struct HarfBuzzVariation(uint Tag, float Value);

static class HarfBuzzNative
{
    [DllImport("libHarfBuzzSharp", EntryPoint = "hb_font_set_variations")]
    internal static extern void SetVariations(
        IntPtr font,
        [In] HarfBuzzVariation[] variations,
        uint count);
}

enum RenderMode
{
    Production,
    ShapedDefault,
    PlatformAdvances,
    PlatformAdvancesWithHarfBuzzKerning,
    HarfBuzzVariations
}

using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Avalonia;
using Avalonia.Platform;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Avalonia;
using BackendNativeSceneView = WebScene.Backends.Avalonia.Native.NativeSceneView;

namespace WebScene.WebPlatformSubset.Runner;

internal interface IWptEngineEnvironment : IDisposable
{
    void PumpInputAction();
    string? ReadState();
    bool IsFrameComplete();
    void SettleFrame();
    WptRenderSnapshot CaptureSnapshot(string documentName);
}

internal sealed record WptRenderSnapshot(
    PixelSize PixelSize,
    Vector Dpi,
    PixelFormat Format,
    byte[] Pixels);

/// <summary>
/// Adapts the native V8/DOM engine to the observable WPT subset contract.
/// Missing native capabilities are reported directly; no fallback is available.
/// </summary>
internal sealed unsafe class NativeWptEngineEnvironment : IWptEngineEnvironment
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = true
    };
    private static readonly Regex ScriptRegex = new(
        "<script\\b(?<attributes>[^>]*)>(?<source>[\\s\\S]*?)</script\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex StyleRegex = new(
        "<style\\b[^>]*>[\\s\\S]*?</style\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex BodyRegex = new(
        "<body\\b(?<attributes>[^>]*)>(?<body>[\\s\\S]*?)</body\\s*>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex HtmlRegex = new(
        "<html\\b(?<attributes>[^>]*)>",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
    private static readonly Regex HtmlAttributeRegex = new(
        "(?<name>[^\\s=/>]+)(?:\\s*=\\s*(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+)))?",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private readonly IntPtr _engine;
    private readonly NativeInteropInvoker _interop;
    private readonly ViewportSettings _viewport;
    private readonly NativeSceneSnapshotRenderer _renderer;
    private ulong _sequence;
    private double _frameTimestampMs;
    private bool _loaded;
    private bool _disposed;
    private readonly bool _managedFontEngine;

    internal NativeWptEngineEnvironment(
        RunnerOptions options,
        ViewportSettings viewport,
        string upstreamRoot,
        string documentPath,
        string html,
        string? fontBaseDirectory = null)
    {
        _viewport = viewport;
        _renderer = new NativeSceneSnapshotRenderer(viewport.DeviceScaleFactor);
        var libraryPath = options.NativeLibraryPath;
        if (string.IsNullOrWhiteSpace(libraryPath))
        {
            throw new ArgumentException(
                "Native WPT mode requires --native-library <path>.");
        }
        if (!File.Exists(libraryPath))
        {
            throw new FileNotFoundException("Native WebScene engine library was not found.", libraryPath);
        }

        NativeApi.Configure(libraryPath);
        _managedFontEngine = html.Contains("@font-face", StringComparison.OrdinalIgnoreCase);
        if (_managedFontEngine)
        {
            // Font contracts must exercise the product stylesheet-consumption,
            // registration and measurement path, not a separate harness font map.
            NativeWebSceneApi.ConfigureLibraryPath(libraryPath);
            _engine = NativeWebSceneApi.EngineCreate(0, options.NativeCacheDirectory,
                new AvaloniaResourceLoader { ScriptBaseDirectory = fontBaseDirectory ?? upstreamRoot }, _ => { });
            _renderer.SetWebTypefaceRegistry(NativeWebSceneApi.GetWebTypefaceRegistry(_engine));
        }
        else _engine = NativeApi.Create(options.NativeCacheDirectory);
        if (_engine == IntPtr.Zero)
        {
            throw new InvalidOperationException("The native WebScene engine could not be created.");
        }
        _interop = new NativeInteropInvoker(_engine);

        try
        {
            if (!NativeApi.TrySetResourceRoot(_engine, upstreamRoot))
            {
                throw new InvalidOperationException(
                    $"The native WPT adapter could not set resource root '{upstreamRoot}'.");
            }
            Enqueue(new NativeInputEvent
            {
                Kind = 6,
                Sequence = ++_sequence,
                X = viewport.Width,
                Y = viewport.Height,
                // DOM device-pixel measurements must use the same scale as
                // screenshot rasterization, including before document scripts.
                DeltaX = viewport.DeviceScaleFactor
            });
            LoadPreparedDocument(html, upstreamRoot, documentPath);
            _loaded = true;
            for (var index = 0; index < 4; index++) SettleFrame();
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    public string? ReadState()
    {
        var json = Evaluate("window.__webSceneWptState || null", "webscene-wpt-read-state.js");
        return string.Equals(json, "null", StringComparison.Ordinal) ? null : json;
    }

    public bool IsFrameComplete() => _loaded;

    public void PumpInputAction()
    {
        var actionJson = Evaluate("""
            (function () {
              const queue = window.__webSceneWptInputActions;
              return queue && queue.length ? queue.shift() : null;
            })()
            """, "webscene-wpt-read-input.js");
        if (string.Equals(actionJson, "null", StringComparison.Ordinal)) return;

        var action = JsonSerializer.Deserialize<NativeInputAction>(actionJson, JsonOptions)
                     ?? throw new InvalidDataException("WPT input action was empty.");
        string? error = null;
        try
        {
            var target = ResolveTarget(action.TargetId);
            switch (action.Type)
            {
                case "pointerMove":
                    EnqueuePointer(1, target.X, target.Y);
                    break;
                case "click":
                    EnqueuePointer(1, target.X, target.Y);
                    EnqueuePointer(2, target.X, target.Y, flags: 1);
                    EnqueuePointer(3, target.X, target.Y);
                    break;
                case "contextClick":
                    EnqueuePointer(1, target.X, target.Y);
                    EnqueuePointer(2, target.X, target.Y, flags: 2U | (3U << 8));
                    EnqueuePointer(3, target.X, target.Y, flags: 3U << 8);
                    break;
                case "wheel":
                    if (!double.TryParse(
                            action.Value,
                            NumberStyles.Float,
                            CultureInfo.InvariantCulture,
                            out var deltaY))
                    {
                        throw new NotSupportedException(
                            $"WPT wheel delta '{action.Value}' was not numeric.");
                    }
                    EnqueuePointer(1, target.X, target.Y);
                    EnqueueWheel(target.X, target.Y, deltaY);
                    break;
                case "resize":
                    var size = JsonSerializer.Deserialize<double[]>(action.Value ?? "[]") ?? [];
                    if (size.Length != 2 || size[0] <= 1 || size[1] <= 1)
                    {
                        throw new NotSupportedException(
                            $"WPT viewport size '{action.Value}' was invalid.");
                    }
                    EnqueueResize(size[0], size[1]);
                    break;
                case "sendKeys":
                    if (string.Equals(action.Value, "\uE004", StringComparison.Ordinal))
                    {
                        EnqueueKey(7, 9);
                        EnqueueKey(8, 9);
                        break;
                    }
                    if (string.IsNullOrEmpty(action.Value))
                    {
                        throw new NotSupportedException("WPT send_keys requires a non-empty value.");
                    }
                    Execute(
                        $"document.getElementById({JsonSerializer.Serialize(action.TargetId)})?.focus();",
                        "webscene-wpt-send-keys-focus.js");
                    SettleFrame();
                    foreach (var rune in action.Value.EnumerateRunes())
                    {
                        var keyCode = PrintableAsciiDomKeyCode(rune);
                        EnqueueKey(7, keyCode);
                        EnqueueText(rune);
                        EnqueueKey(8, keyCode);
                    }
                    break;
                default:
                    throw new NotSupportedException(
                        $"WPT input action '{action.Type}' is not supported by the native adapter.");
            }
            SettleFrame();
            if (action.Type == "click")
            {
                // WebDriver click targets the requested element even when the
                // compact native UA stylesheet gives an unstyled form control
                // no useful hit-test box. The preceding pointer sequence sets
                // pointer modality; this fallback supplies only the mandated
                // focus target, matching the managed adapter's contract.
                Execute(
                    $"document.getElementById({JsonSerializer.Serialize(action.TargetId)})?.focus();",
                    "webscene-wpt-click-focus.js");
                SettleFrame();
            }
        }
        catch (Exception exception)
        {
            error = exception.Message;
        }

        Execute(
            $"window.__webSceneCompleteInputAction({action.Id}, {JsonSerializer.Serialize(error)});",
            "webscene-wpt-complete-input.js");
    }

    public void SettleFrame()
    {
        if (_disposed) return;
        _frameTimestampMs += 1000.0 / 60.0;
        Enqueue(new NativeInputEvent
        {
            Kind = 5,
            Sequence = ++_sequence,
            X = _frameTimestampMs
        });
        // A native frame consumes animation work; the no-op script then gives
        // the runtime a task-drain turn for zero-delay WPT completion timers.
        Execute("void 0", "webscene-wpt-pump.js");
        AcquireLatestScene(waitForSequence: _sequence);
    }

    public WptRenderSnapshot CaptureSnapshot(string documentName)
    {
        AcquireLatestScene(waitForSequence: 0);
        return _renderer.Capture(
            _viewport.Width,
            _viewport.Height,
            new Vector(96 * _viewport.DeviceScaleFactor, 96 * _viewport.DeviceScaleFactor));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _renderer.Dispose();
        _interop.Dispose();
        if (_engine != IntPtr.Zero)
        {
            if (_managedFontEngine) NativeWebSceneApi.EngineDestroy(_engine);
            else NativeApi.EngineDestroy(_engine);
        }
    }

    private void LoadPreparedDocument(string html, string upstreamRoot, string documentPath)
    {
        var scripts = ScriptRegex.Matches(html)
            .Select(match => new
            {
                Attributes = match.Groups["attributes"].Value,
                Source = match.Groups["source"].Value
            })
            .Where(script => !HtmlScriptSemantics.IsInertScript(script.Attributes))
            .ToList();

        var htmlWithoutScripts = HtmlScriptSemantics.RemoveAllScripts(html, ScriptRegex);
        var styles = string.Concat(StyleRegex.Matches(htmlWithoutScripts).Select(match => match.Value));
        var bodyMatch = BodyRegex.Match(html);
        var htmlMatch = HtmlRegex.Match(html);
        var body = bodyMatch.Success ? bodyMatch.Groups["body"].Value : html;
        var htmlAttributes = htmlMatch.Success
            ? HtmlAttributeRegex.Matches(htmlMatch.Groups["attributes"].Value)
                .Select(match => new[]
                {
                    match.Groups["name"].Value,
                    match.Groups["double"].Success ? match.Groups["double"].Value
                        : match.Groups["single"].Success ? match.Groups["single"].Value
                        : match.Groups["bare"].Value
                })
                .ToArray()
            : [];
        var bodyAttributes = bodyMatch.Success
            ? HtmlAttributeRegex.Matches(bodyMatch.Groups["attributes"].Value)
                .Select(match => new[]
                {
                    match.Groups["name"].Value,
                    match.Groups["double"].Success ? match.Groups["double"].Value
                        : match.Groups["single"].Success ? match.Groups["single"].Value
                        : match.Groups["bare"].Value
                })
                .ToArray()
            : [];
        body = HtmlScriptSemantics.RemoveExecutableScriptsAndStyles(body, ScriptRegex, StyleRegex);
        var markup = styles + body;
        var documentDirectory = Path.GetDirectoryName(documentPath)?.Replace('\\', '/') ?? string.Empty;
        if (documentDirectory.Length > 0) documentDirectory += "/";

        Execute($$"""
            globalThis.__webSceneDocumentBasePath = {{JsonSerializer.Serialize(documentDirectory)}};
            globalThis.__webSceneWptExpectedDeviceScaleFactor = {{JsonSerializer.Serialize(_viewport.DeviceScaleFactor)}};
            const webSceneViewportRoot = document.body;
            const webSceneDocumentElement = document.createElement('html');
            const webSceneHead = document.createElement('head');
            const webSceneBody = document.createElement('body');
            webSceneViewportRoot.appendChild(webSceneDocumentElement);
            webSceneDocumentElement.appendChild(webSceneHead);
            webSceneDocumentElement.appendChild(webSceneBody);
            for (const [name, value] of {{JsonSerializer.Serialize(htmlAttributes)}}) {
              webSceneDocumentElement.setAttribute(name, value);
            }
            for (const [name, value] of {{JsonSerializer.Serialize(bodyAttributes)}}) {
              webSceneBody.setAttribute(name, value);
            }
            // Adapter viewport normalization belongs to the internal native
            // viewport box, not the authored BODY. Keeping it there preserves
            // browser CSS inheritance/cascade and prevents a zero-height BODY
            // with only positioned children from clipping WPT hit testing.
            webSceneViewportRoot.style.margin = '0';
            webSceneViewportRoot.style.padding = '0';
            webSceneViewportRoot.style.overflow = 'hidden';
            webSceneViewportRoot.style.background = '#ffffff';
            webSceneBody.innerHTML = {{JsonSerializer.Serialize(markup)}};
            """, Path.Combine(upstreamRoot, "webscene-wpt-document.js"));

        for (var index = 0; index < scripts.Count; index++)
        {
            if (string.IsNullOrWhiteSpace(scripts[index].Source)) continue;
            Execute(
                scripts[index].Source,
                Path.Combine(upstreamRoot, $"webscene-wpt-inline-{index}.js"));
        }
        Execute(
            """
            document.readyState = 'interactive';
            document.dispatchEvent(new Event('DOMContentLoaded'));
            document.readyState = 'complete';
            const webSceneLoadEvent = new Event('load');
            const webSceneOnLoad = window.onload;
            window.onload = null;
            window.dispatchEvent(webSceneLoadEvent);
            if (typeof webSceneOnLoad === 'function') {
              webSceneOnLoad.call(window, webSceneLoadEvent);
            }
            """,
            Path.Combine(upstreamRoot, "webscene-wpt-load.js"));
    }

    private NativePoint ResolveTarget(string id)
    {
        var json = Evaluate($$"""
            (function () {
              const target = document.getElementById({{JsonSerializer.Serialize(id)}});
              if (!target) return null;
              const rect = target.getBoundingClientRect();
              return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
            })()
            """, "webscene-wpt-target.js");
        if (string.Equals(json, "null", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"WPT input target '#{id}' was not found.");
        }
        return JsonSerializer.Deserialize<NativePoint>(json, JsonOptions)
               ?? throw new InvalidDataException($"WPT input target '#{id}' had no bounds.");
    }

    private void EnqueuePointer(uint kind, double x, double y, uint flags = 0)
        => Enqueue(new NativeInputEvent
        {
            Kind = kind,
            Flags = flags,
            Sequence = ++_sequence,
            X = x,
            Y = y
        });

    private void EnqueueKey(uint kind, int domKeyCode, uint flags = 0)
        => Enqueue(new NativeInputEvent
        {
            Kind = kind,
            Flags = flags,
            Sequence = ++_sequence,
            X = domKeyCode
        });

    private void EnqueueWheel(double x, double y, double deltaY)
        => Enqueue(new NativeInputEvent
        {
            Kind = 4,
            Sequence = ++_sequence,
            X = x,
            Y = y,
            DeltaY = deltaY
        });

    private void EnqueueResize(double width, double height)
        => Enqueue(new NativeInputEvent
        {
            Kind = 6,
            Sequence = ++_sequence,
            X = width,
            Y = height,
            DeltaX = _viewport.DeviceScaleFactor
        });

    private void EnqueueText(Rune rune)
        => Enqueue(new NativeInputEvent
        {
            Kind = 9,
            Sequence = ++_sequence,
            X = rune.Value
        });

    private static int PrintableAsciiDomKeyCode(Rune rune)
    {
        var scalar = rune.Value;
        if (scalar is >= 'a' and <= 'z') return scalar - ('a' - 'A');
        if (scalar is >= 'A' and <= 'Z' or >= '0' and <= '9' || scalar == ' ') return scalar;
        throw new NotSupportedException(
            $"WPT send_keys currently supports printable ASCII letters, digits, and space, not '{rune}'.");
    }

    private void Enqueue(NativeInputEvent input)
    {
        if (NativeApi.EngineEnqueue(_engine, in input) == 0)
        {
            throw new InvalidOperationException("The native input queue rejected a WPT event.");
        }
    }

    private void Execute(string source, string documentName)
    {
        if (!NativeApi.TryExecute(_engine, source, documentName))
        {
            throw new InvalidOperationException(
                $"Native script failed in '{documentName}': {NativeApi.GetLastError(_engine)}");
        }

        // Script submission is asynchronous. A synchronous evaluation queued
        // immediately behind it is the task barrier that proves execution
        // completed. The engine intentionally retains the preceding script's
        // error across a successful evaluation, so read it before another
        // script can clear it. Without this barrier, an exception before WPT
        // registers its tests is misreported ten seconds later as a timeout.
        try
        {
            using var result = _interop.InvokeAsync(
                    "null",
                    $"{documentName}.webscene-task-barrier.js",
                    CancellationToken.None)
                .AsTask()
                .GetAwaiter()
                .GetResult();
        }
        catch (Exception exception)
        {
            throw new InvalidOperationException(
                $"Native script barrier failed after '{documentName}': " +
                NativeApi.GetLastError(_engine),
                exception);
        }
        var error = NativeApi.GetLastError(_engine);
        if (!string.IsNullOrWhiteSpace(error))
        {
            throw new InvalidOperationException(
                $"Native script failed in '{documentName}': {error}");
        }
    }

    private string Evaluate(string source, string documentName)
    {
        try
        {
            using var result = _interop.InvokeAsync(
                    source,
                    documentName,
                    CancellationToken.None)
                .AsTask()
                .GetAwaiter()
                .GetResult();
            return result.ToJsonText();
        }
        catch (Exception error)
        {
            throw new InvalidOperationException(
                $"Native evaluation failed in '{documentName}': " +
                NativeApi.GetLastError(_engine),
                error);
        }
    }

    private void AcquireLatestScene(ulong waitForSequence)
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromMilliseconds(250))
        {
            var scene = NativeApi.EngineAcquireLatestScene(_engine);
            if (scene == IntPtr.Zero)
            {
                Thread.Yield();
                continue;
            }
            try
            {
                var view = (NativeSceneView*)scene;
                _renderer.Apply(view);
                NativeApi.SceneAcknowledge(scene);
                if (waitForSequence == 0 || view->Header.ConsumedInputSequence >= waitForSequence)
                {
                    return;
                }
            }
            finally
            {
                NativeApi.SceneRelease(scene);
            }
            Thread.Yield();
        }
    }

    private sealed class NativeInputAction
    {
        public int Id { get; init; }
        public required string Type { get; init; }
        public required string TargetId { get; init; }
        public string? Value { get; init; }
    }

    private sealed class NativePoint
    {
        public double X { get; init; }
        public double Y { get; init; }
    }
}

internal sealed unsafe class NativeSceneSnapshotRenderer : IDisposable
{
    private readonly NativeCanvasSceneRenderer _renderer = new();

    internal NativeSceneSnapshotRenderer(double deviceScaleFactor)
        => _renderer.SetPresenterDeviceScaleFactor(deviceScaleFactor);

    internal void SetWebTypefaceRegistry(NativeTextShaping.WebTypefaceRegistry? registry)
        => _renderer.SetWebTypefaceRegistry(registry);

    internal void Apply(NativeSceneView* view)
    {
        // WPT snapshots must use the product renderer. A previous DOM-only
        // replay omitted retained canvas layers and could false-pass blank
        // canvas test/reference pairs.
        if (!_renderer.ApplyDiff((BackendNativeSceneView*)view))
        {
            throw new InvalidDataException(
                $"Native retained-scene diff {view->Header.Revision} was rejected.");
        }
    }

    internal WptRenderSnapshot Capture(int width, int height, Vector dpi)
    {
        var scaleX = Math.Max(1, dpi.X / 96.0);
        var scaleY = Math.Max(1, dpi.Y / 96.0);
        var pixelWidth = Math.Max(1, (int)Math.Ceiling(width * scaleX));
        var pixelHeight = Math.Max(1, (int)Math.Ceiling(height * scaleY));
        using var bitmap = new SKBitmap(
            pixelWidth,
            pixelHeight,
            SKColorType.Bgra8888,
            SKAlphaType.Unpremul);
        using (var canvas = new SKCanvas(bitmap))
        {
            canvas.Clear(SKColors.Transparent);
            canvas.Scale((float)scaleX, (float)scaleY);
            _renderer.RenderRetained(canvas, width, height, null);
            canvas.Flush();
        }

        var pixels = new byte[checked(bitmap.RowBytes * pixelHeight)];
        Marshal.Copy(bitmap.GetPixels(), pixels, 0, pixels.Length);
        if (bitmap.RowBytes != pixelWidth * 4)
        {
            var compact = new byte[checked(pixelWidth * pixelHeight * 4)];
            for (var row = 0; row < pixelHeight; row++)
            {
                Buffer.BlockCopy(
                    pixels,
                    row * bitmap.RowBytes,
                    compact,
                    row * pixelWidth * 4,
                    pixelWidth * 4);
            }
            pixels = compact;
        }
        return new WptRenderSnapshot(
            new PixelSize(pixelWidth, pixelHeight),
            dpi,
            PixelFormat.Bgra8888,
            pixels);
    }

    public void Dispose() => _renderer.Reset();
}
internal static unsafe class NativeApi
{
    private const string LibraryName = "webscene_native_engine";
    private static readonly object Gate = new();
    private static string? _libraryPath;
    private static bool _resolverInstalled;
    private static readonly TextMeasureCallback TextMeasure = MeasureText;
    private static readonly IntPtr TextMeasureAddress =
        Marshal.GetFunctionPointerForDelegate(TextMeasure);

    internal static void Configure(string libraryPath)
    {
        lock (Gate)
        {
            var fullPath = Path.GetFullPath(libraryPath);
            if (_libraryPath is not null && !string.Equals(_libraryPath, fullPath, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"The native test adapter is already bound to '{_libraryPath}'.");
            }
            _libraryPath = fullPath;
            if (_resolverInstalled) return;
            NativeLibrary.SetDllImportResolver(
                typeof(NativeApi).Assembly,
                (name, _, _) => name == LibraryName
                    ? NativeLibrary.Load(_libraryPath!)
                    : IntPtr.Zero);
            _resolverInstalled = true;
        }
    }

    internal static IntPtr Create(string? cacheDirectory)
    {
        if (!string.IsNullOrWhiteSpace(cacheDirectory)) Directory.CreateDirectory(cacheDirectory);
        var bytes = string.IsNullOrWhiteSpace(cacheDirectory)
            ? []
            : Encoding.UTF8.GetBytes(cacheDirectory);
        fixed (byte* pointer = bytes)
        {
            var options = new NativeEngineOptions
            {
                StructSize = (uint)Marshal.SizeOf<NativeEngineOptions>(),
                CompilationCacheDirectory = bytes.Length == 0 ? IntPtr.Zero : (IntPtr)pointer,
                CompilationCacheDirectoryLength = (nuint)bytes.Length,
                TextMeasureCallback = TextMeasureAddress
            };
            return EngineCreateWithOptions(in options);
        }
    }

    private static byte MeasureText(
        IntPtr userData,
        IntPtr text,
        nuint textLength,
        IntPtr fontFamily,
        nuint fontFamilyLength,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        ref NativeTextMetrics metrics)
    {
        try
        {
            const uint legacyMetricsSize = 20;
            const uint shapedInkMetricsSize = 36;
            var availableMetricsSize = metrics.StructSize;
            if (availableMetricsSize < legacyMetricsSize || fontSize <= 0) return 0;
            var value = Marshal.PtrToStringUTF8(text, checked((int)textLength)) ?? string.Empty;
            var family = Marshal.PtrToStringUTF8(fontFamily, checked((int)fontFamilyLength))
                ?? "sans-serif";
            var measured = NativeTextShaping.Measure(
                value,
                family,
                fontSize,
                fontWeight,
                letterSpacing,
                wordSpacing);
            metrics.AdvanceWidth = measured.AdvanceWidth;
            metrics.Ascent = measured.Ascent;
            metrics.Descent = measured.Descent;
            metrics.Leading = measured.Leading;
            if (availableMetricsSize >= shapedInkMetricsSize)
            {
                metrics.ActualBoundingBoxLeft = measured.ActualBoundingBoxLeft;
                metrics.ActualBoundingBoxRight = measured.ActualBoundingBoxRight;
                metrics.ActualBoundingBoxAscent = measured.ActualBoundingBoxAscent;
                metrics.ActualBoundingBoxDescent = measured.ActualBoundingBoxDescent;
            }
            return 1;
        }
        catch
        {
            return 0;
        }
    }

    internal static bool TryExecute(IntPtr engine, string source, string documentName)
    {
        var sourceBytes = Encoding.UTF8.GetBytes(source);
        var nameBytes = Encoding.UTF8.GetBytes(documentName);
        return EngineExecuteScript(
            engine,
            sourceBytes,
            (nuint)sourceBytes.Length,
            nameBytes,
            (nuint)nameBytes.Length) != 0;
    }

    internal static bool TrySetResourceRoot(IntPtr engine, string root)
    {
        var bytes = Encoding.UTF8.GetBytes(root);
        return EngineSetResourceRoot(engine, bytes, (nuint)bytes.Length) != 0;
    }

    internal static string GetLastError(IntPtr engine)
    {
        var required = EngineCopyLastError(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyLastError(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    internal static uint GetAbiVersion() => EngineGetAbiVersion();

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_abi_version")]
    private static extern uint EngineGetAbiVersion();

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create")]
    private static extern IntPtr EngineCreate(uint simulatedChartCommandCount);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create_with_options")]
    private static extern IntPtr EngineCreateWithOptions(in NativeEngineOptions options);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_execute_script")]
    private static extern byte EngineExecuteScript(
        IntPtr engine,
        byte[] source,
        nuint sourceLength,
        byte[] documentName,
        nuint documentNameLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_resource_root")]
    private static extern byte EngineSetResourceRoot(
        IntPtr engine,
        byte[] root,
        nuint rootLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_last_error")]
    private static extern nuint EngineCopyLastError(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_destroy")]
    internal static extern void EngineDestroy(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_enqueue")]
    internal static extern byte EngineEnqueue(IntPtr engine, in NativeInputEvent input);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_latest_scene")]
    internal static extern IntPtr EngineAcquireLatestScene(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_acknowledge")]
    internal static extern byte SceneAcknowledge(IntPtr scene);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_release")]
    internal static extern void SceneRelease(IntPtr scene);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte TextMeasureCallback(
        IntPtr userData,
        IntPtr text,
        nuint textLength,
        IntPtr fontFamily,
        nuint fontFamilyLength,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        ref NativeTextMetrics metrics);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeEngineOptions
{
    public uint StructSize;
    public uint SimulatedChartCommandCount;
    public IntPtr CompilationCacheDirectory;
    public nuint CompilationCacheDirectoryLength;
    public IntPtr ResourceLoadCallback;
    public IntPtr ResourceLoadUserData;
    public IntPtr ScenePublishedCallback;
    public IntPtr ScenePublishedUserData;
    public IntPtr TextMeasureCallback;
    public IntPtr TextMeasureUserData;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTextMetrics
{
    public uint StructSize;
    public float AdvanceWidth;
    public float Ascent;
    public float Descent;
    public float Leading;
    public float ActualBoundingBoxLeft;
    public float ActualBoundingBoxRight;
    public float ActualBoundingBoxAscent;
    public float ActualBoundingBoxDescent;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInputEvent
{
    public uint Kind;
    public uint Flags;
    public ulong Sequence;
    public double X;
    public double Y;
    public double DeltaX;
    public double DeltaY;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneHeader
{
    public ulong Revision;
    public ulong BaseRevision;
    public ulong ConsumedInputSequence;
    public float ViewportWidth;
    public float ViewportHeight;
    public uint CommandCount;
    public uint CanvasLayerCount;
    public uint DamageRectCount;
    public uint Flags;
    public ulong ContentHash;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneCommand
{
    public uint Kind;
    public uint Flags;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint Rgba;
    public uint NodeId;
    public float RadiusTopLeft;
    public float RadiusTopRight;
    public float RadiusBottomRight;
    public float RadiusBottomLeft;
    public float StrokeWidth;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneString
{
    public uint ByteOffset;
    public uint ByteLength;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSceneView
{
    public uint StructSize;
    public uint AbiVersion;
    public NativeSceneHeader Header;
    public NativeSceneCommand* Commands;
    public void* CanvasLayers;
    public void* CanvasCommands;
    public NativeSceneString* Strings;
    public byte* StringBytes;
    public void* DamageRects;
    public void* LeaseToken;
    public uint CanvasCommandCount;
    public uint StringCount;
    public uint StringByteCount;
    public uint Reserved;
}

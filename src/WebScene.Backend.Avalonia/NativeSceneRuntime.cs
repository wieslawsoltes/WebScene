using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using WebScene.Backends.Native;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

public interface INativeWebSceneRenderDiagnostics
{
    long RenderedSceneCount { get; }

    long FirstRenderedSceneTimestamp { get; }

    long FirstReadySceneTimestamp { get; }

    ulong PublishedSceneCount { get; }

    void SubmitAnimationFrame(double timestampMilliseconds);

    void RequestRender();
}

#if !WEBSCENE_UNO
public interface INativeWebSceneFrozenPresentation : IDisposable
{
    Control View { get; }

    ulong EstimatedBytes { get; }
}
#endif

internal sealed class NativePerformanceInstrumentation
{
    private int _enabled;
    private readonly object _rendererMetricsGate = new();
    private NativeRendererMemoryMetrics _rendererMetrics;

    internal bool IsEnabled => Volatile.Read(ref _enabled) != 0;

    internal void Enable() => Volatile.Write(ref _enabled, 1);

    internal void UpdateRendererMetrics(NativeRendererMemoryMetrics metrics)
    {
        if (!IsEnabled)
        {
            return;
        }
        lock (_rendererMetricsGate)
        {
            _rendererMetrics = metrics;
        }
    }

    internal NativeRendererMemoryMetrics ReadRendererMetrics()
    {
        lock (_rendererMetricsGate)
        {
            return _rendererMetrics;
        }
    }
}

internal sealed class NativeSceneRenderObserver
{
    private const uint SceneComponentReady = 4;
    private readonly object _viewportGate = new();
    private readonly List<int> _renderedViewportHeights = [];
    private readonly Queue<NativeSceneRenderSample> _renderedScenes = new(4096);
    private readonly Queue<long> _presentations = new(4096);
    private readonly NativePerformanceInstrumentation _instrumentation;
    private long _renderedSceneCount;
    private long _firstRenderedSceneTimestamp;
    private long _firstReadySceneTimestamp;

    internal NativeSceneRenderObserver(
        NativePerformanceInstrumentation? instrumentation = null)
    {
        _instrumentation = instrumentation ?? new NativePerformanceInstrumentation();
    }

    public long RenderedSceneCount => Volatile.Read(ref _renderedSceneCount);

    public long FirstRenderedSceneTimestamp
        => Volatile.Read(ref _firstRenderedSceneTimestamp);

    public long FirstReadySceneTimestamp
        => Volatile.Read(ref _firstReadySceneTimestamp);

    public int[] RenderedViewportHeights
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedViewportHeights.ToArray();
            }
        }
    }

    public long[] RenderedSceneTimestamps
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedScenes
                    .Select(sample => sample.Timestamp)
                    .ToArray();
            }
        }
    }

    public NativeSceneRenderSample[] RenderedScenes
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedScenes.ToArray();
            }
        }
    }

    public long[] Presentations
    {
        get
        {
            lock (_viewportGate)
            {
                return _presentations.ToArray();
            }
        }
    }

    public void RecordPresented()
    {
        if (!_instrumentation.IsEnabled)
        {
            return;
        }
        lock (_viewportGate)
        {
            if (_presentations.Count == 4096)
            {
                _presentations.Dequeue();
            }
            _presentations.Enqueue(Stopwatch.GetTimestamp());
        }
    }

    public void RecordRendered(in SceneHeader header)
    {
        var monitoring = _instrumentation.IsEnabled;
        var needsFirstRenderTimestamp =
            Volatile.Read(ref _firstRenderedSceneTimestamp) == 0;
        var needsReadyTimestamp = (header.Flags & SceneComponentReady) != 0
            && Volatile.Read(ref _firstReadySceneTimestamp) == 0;
        var timestamp = monitoring || needsFirstRenderTimestamp || needsReadyTimestamp
            ? Stopwatch.GetTimestamp()
            : 0;
        if (needsFirstRenderTimestamp)
        {
            Interlocked.CompareExchange(ref _firstRenderedSceneTimestamp, timestamp, 0);
        }
        if (needsReadyTimestamp)
        {
            Interlocked.CompareExchange(ref _firstReadySceneTimestamp, timestamp, 0);
        }
        Interlocked.Increment(ref _renderedSceneCount);
        if (!monitoring)
        {
            return;
        }
        var viewportHeight = (int)Math.Round(header.ViewportHeight);
        lock (_viewportGate)
        {
            if (_renderedScenes.Count == 4096)
            {
                _renderedScenes.Dequeue();
            }
            _renderedScenes.Enqueue(new NativeSceneRenderSample(
                timestamp,
                header.Revision,
                header.ConsumedInputSequence));
            if (_renderedViewportHeights.Count == 0
                || _renderedViewportHeights[^1] != viewportHeight)
            {
                _renderedViewportHeights.Add(viewportHeight);
            }
        }
    }
}

public static unsafe partial class NativeWebSceneApi
{

    private const string LibraryName = "webscene_native_engine";
    [DllImport(LibraryName, EntryPoint = "webscene_engine_configure_diagnostics", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ConfigureLegacyDiagnostics(IntPtr engine, uint flags, IntPtr callback, IntPtr data);

    /// <summary>Opts a raw engine into the legacy console pull queue. Do not use on an engine owned by a NativeWebSceneView.</summary>
    public static void SetLegacyConsoleCapture(IntPtr engine, bool enabled)
        => ConfigureLegacyDiagnostics(engine, enabled ? 4u : 0u, IntPtr.Zero, IntPtr.Zero);
    private static readonly object LibraryPathGate = new();
    private static readonly ConcurrentDictionary<IntPtr, GCHandle> EngineResourceBridges = new();
    private static readonly ResourceLoadCallback ResourceLoad = LoadResource;
    private static readonly IntPtr ResourceLoadAddress = Marshal.GetFunctionPointerForDelegate(ResourceLoad);
    private static readonly ResourceLoadCallbackV2 ResourceLoadV2 = LoadResourceV2;
    private static readonly IntPtr ResourceLoadV2Address =
        Marshal.GetFunctionPointerForDelegate(ResourceLoadV2);
    private static readonly ResourceLoadCallbackV3 ResourceLoadV3 = LoadResourceV3;
    private static readonly IntPtr ResourceLoadV3Address =
        Marshal.GetFunctionPointerForDelegate(ResourceLoadV3);
    private static readonly ScenePublishedCallback ScenePublished = NotifyScenePublished;
    private static readonly IntPtr ScenePublishedAddress =
        Marshal.GetFunctionPointerForDelegate(ScenePublished);
    private static readonly TextMeasureCallback TextMeasure = MeasureText;
    private static readonly IntPtr TextMeasureAddress =
        Marshal.GetFunctionPointerForDelegate(TextMeasure);
    private static readonly HostRequestAvailableCallback HostRequestAvailable =
        NotifyHostRequestAvailable;
    private static readonly IntPtr HostRequestAvailableAddress =
        Marshal.GetFunctionPointerForDelegate(HostRequestAvailable);
    private static readonly InteropCallbackAvailableCallback InteropCallbackAvailable =
        NotifyInteropCallbackAvailable;
    private static readonly IntPtr InteropCallbackAvailableAddress =
        Marshal.GetFunctionPointerForDelegate(InteropCallbackAvailable);
    private static readonly AnimationFrameRequestedCallback AnimationFrameRequested =
        NotifyAnimationFrameRequested;
    private static readonly IntPtr AnimationFrameRequestedAddress =
        Marshal.GetFunctionPointerForDelegate(AnimationFrameRequested);
    private static string? _libraryPath;

    static NativeWebSceneApi()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeWebSceneApi).Assembly, ResolveLibrary);
    }

    public static void ConfigureLibraryPath(string libraryPath)
    {
        var fullPath = Path.GetFullPath(libraryPath);
        lock (LibraryPathGate)
        {
            if (_libraryPath is not null
                && !string.Equals(_libraryPath, fullPath, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"The native WebScene runtime is already bound to '{_libraryPath}' and cannot be rebound to '{fullPath}' in the same process.");
            }
            _libraryPath = fullPath;
        }
    }

    private static IntPtr ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? path)
    {
        string? configuredPath;
        lock (LibraryPathGate)
        {
            configuredPath = _libraryPath;
        }
        if (libraryName == LibraryName && !string.IsNullOrWhiteSpace(configuredPath))
        {
            return NativeLibrary.Load(configuredPath);
        }
        return IntPtr.Zero;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_prewarm")]
    public static extern byte EnginePrewarm();

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create")]
    private static extern IntPtr EngineCreateDefault(uint simulatedChartCommandCount);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create_with_options")]
    private static extern IntPtr EngineCreateWithOptions(in EngineOptions options);

    public static IntPtr EngineCreate(
        uint simulatedChartCommandCount,
        string? compilationCacheDirectory,
        IWebSceneResourceLoader resourceLoader,
        Action<NativeScenePublished> scenePublished,
        Action? hostRequestAvailable = null,
        Action? interopCallbackAvailable = null,
        Action? animationFrameRequested = null)
    {
        ArgumentNullException.ThrowIfNull(resourceLoader);
        ArgumentNullException.ThrowIfNull(scenePublished);
        var directoryBytes = string.IsNullOrWhiteSpace(compilationCacheDirectory)
            ? []
            : Encoding.UTF8.GetBytes(compilationCacheDirectory);
        var bridgeHandle = GCHandle.Alloc(
            new ResourceBridge(
                resourceLoader,
                scenePublished,
                hostRequestAvailable,
                interopCallbackAvailable,
                animationFrameRequested));
        try
        {
            fixed (byte* directory = directoryBytes)
            {
                var options = new EngineOptions
                {
                    StructSize = (uint)Marshal.SizeOf<EngineOptions>(),
                    SimulatedChartCommandCount = simulatedChartCommandCount,
                    CompilationCacheDirectory = directoryBytes.Length == 0 ? IntPtr.Zero : (IntPtr)directory,
                    CompilationCacheDirectoryLength = (nuint)directoryBytes.Length,
                    ResourceLoadCallback = ResourceLoadAddress,
                    ResourceLoadUserData = GCHandle.ToIntPtr(bridgeHandle),
                    ScenePublishedCallback = ScenePublishedAddress,
                    ScenePublishedUserData = GCHandle.ToIntPtr(bridgeHandle),
                    TextMeasureCallback = TextMeasureAddress,
                    TextMeasureUserData = GCHandle.ToIntPtr(bridgeHandle),
                    HostRequestAvailableCallback = hostRequestAvailable is null
                        ? IntPtr.Zero
                        : HostRequestAvailableAddress,
                    HostRequestAvailableUserData = hostRequestAvailable is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle),
                    InteropCallbackAvailableCallback = interopCallbackAvailable is null
                        ? IntPtr.Zero
                        : InteropCallbackAvailableAddress,
                    InteropCallbackAvailableUserData = interopCallbackAvailable is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle),
                    AnimationFrameRequestedCallback = animationFrameRequested is null
                        ? IntPtr.Zero
                        : AnimationFrameRequestedAddress,
                    AnimationFrameRequestedUserData = animationFrameRequested is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle),
                    ResourceLoadCallbackV2 = ResourceLoadV2Address,
                    ResourceLoadV2UserData = GCHandle.ToIntPtr(bridgeHandle),
                    ResourceLoadCallbackV3 = ResourceLoadV3Address,
                    ResourceLoadV3UserData = GCHandle.ToIntPtr(bridgeHandle)
                };
                var engine = EngineCreateWithOptions(in options);
                if (engine == IntPtr.Zero) return IntPtr.Zero;
                EngineResourceBridges[engine] = bridgeHandle;
                bridgeHandle = default;
                return engine;
            }
        }
        finally
        {
            if (bridgeHandle.IsAllocated)
            {
                (bridgeHandle.Target as ResourceBridge)?.Dispose();
                bridgeHandle.Free();
            }
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_destroy")]
    private static extern void EngineDestroyNative(IntPtr engine);

    public static void EngineDestroy(IntPtr engine)
    {
        if (EngineResourceBridges.TryRemove(engine, out var existing)
            && existing.IsAllocated
            && existing.Target is ResourceBridge resourceBridge)
        {
            var inspectorRegistry = NativeInspectorRegistry.Current;
            if (inspectorRegistry is null)
            {
                EngineDestroyNative(engine);
            }
            else
            {
                lock (resourceBridge)
                {
                    inspectorRegistry.CloseEngine(engine);
                    EngineDestroyNative(engine);
                }
            }
            resourceBridge.Dispose();
            existing.Free();
            return;
        }
        EngineDestroyNative(engine);
    }

    internal static NativeTextShaping.WebTypefaceRegistry? GetWebTypefaceRegistry(
        IntPtr engine)
        => EngineResourceBridges.TryGetValue(engine, out var bridge)
            && bridge.IsAllocated
            ? (bridge.Target as ResourceBridge)?.WebTypefaces
            : null;

    [DllImport(LibraryName, EntryPoint = "webscene_engine_load_url")]
    private static extern byte EngineLoadUrl(IntPtr engine, byte[] url, nuint urlLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_load_url_with_options")]
    private static extern byte EngineLoadUrlWithOptions(
        IntPtr engine,
        byte[] url,
        nuint urlLength,
        in NavigationOptions options);

    public static bool TryLoadUrl(IntPtr engine, string url)
    {
        var bytes = Encoding.UTF8.GetBytes(url);
        return EngineLoadUrl(engine, bytes, (nuint)bytes.Length) != 0;
    }

    public static bool TryLoadUrl(
        IntPtr engine,
        string url,
        IReadOnlyList<WebSceneDocumentScript> documentStartScripts)
    {
        ArgumentNullException.ThrowIfNull(documentStartScripts);
        if (documentStartScripts.Count == 0)
        {
            return TryLoadUrl(engine, url);
        }

        var urlBytes = Encoding.UTF8.GetBytes(url);
        var descriptors = new DocumentScriptDescriptor[documentStartScripts.Count];
        var pinnedBuffers = new List<GCHandle>(documentStartScripts.Count * 2 + 1);
        try
        {
            for (var index = 0; index < documentStartScripts.Count; index++)
            {
                var script = documentStartScripts[index];
                var sourceBytes = Encoding.UTF8.GetBytes(script.Source);
                var nameBytes = Encoding.UTF8.GetBytes(script.Name);
                var sourceHandle = GCHandle.Alloc(sourceBytes, GCHandleType.Pinned);
                var nameHandle = GCHandle.Alloc(nameBytes, GCHandleType.Pinned);
                pinnedBuffers.Add(sourceHandle);
                pinnedBuffers.Add(nameHandle);
                descriptors[index] = new DocumentScriptDescriptor
                {
                    StructSize = (uint)Marshal.SizeOf<DocumentScriptDescriptor>(),
                    Flags = script.AllFrames
                        ? DocumentScriptFlags.AllFrames
                        : DocumentScriptFlags.None,
                    Source = sourceHandle.AddrOfPinnedObject(),
                    SourceLength = (nuint)sourceBytes.Length,
                    Name = nameHandle.AddrOfPinnedObject(),
                    NameLength = (nuint)nameBytes.Length
                };
            }

            var descriptorHandle = GCHandle.Alloc(descriptors, GCHandleType.Pinned);
            pinnedBuffers.Add(descriptorHandle);
            var options = new NavigationOptions
            {
                StructSize = (uint)Marshal.SizeOf<NavigationOptions>(),
                DocumentScriptCount = (uint)descriptors.Length,
                DocumentScripts = descriptorHandle.AddrOfPinnedObject()
            };
            try
            {
                return EngineLoadUrlWithOptions(
                    engine,
                    urlBytes,
                    (nuint)urlBytes.Length,
                    in options) != 0;
            }
            catch (EntryPointNotFoundException error)
            {
                throw new InvalidOperationException(
                    "The loaded WebScene native runtime does not support document-start " +
                    "scripts. Install the WebScene 1.0.19 or newer native runtime package.",
                    error);
            }
        }
        finally
        {
            foreach (var handle in pinnedBuffers)
            {
                if (handle.IsAllocated)
                {
                    handle.Free();
                }
            }
        }
    }

    internal static WebSceneDocumentScript[] ValidateLoadOptions(
        NativeWebSceneLoadOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentException.ThrowIfNullOrWhiteSpace(options.Source);
        ArgumentException.ThrowIfNullOrWhiteSpace(options.NativeLibraryPath);
        if (!Uri.TryCreate(options.Source, UriKind.Absolute, out _))
        {
            throw new ArgumentException(
                "The WebScene document source must be an absolute URI.",
                nameof(options));
        }
        ArgumentNullException.ThrowIfNull(options.DocumentStartScripts);
        var scripts = new WebSceneDocumentScript[options.DocumentStartScripts.Count];
        for (var index = 0; index < options.DocumentStartScripts.Count; index++)
        {
            var script = options.DocumentStartScripts[index]
                ?? throw new ArgumentException(
                    $"Document-start script {index} must not be null.",
                    nameof(options));
            ArgumentException.ThrowIfNullOrWhiteSpace(script.Source);
            ArgumentException.ThrowIfNullOrWhiteSpace(script.Name);
            scripts[index] = script;
        }
        return scripts;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_resource_root")]
    private static extern byte EngineSetResourceRoot(
        IntPtr engine,
        byte[] resourceRoot,
        nuint resourceRootLength);

    public static bool TrySetResourceRoot(IntPtr engine, string resourceRoot)
    {
        var bytes = Encoding.UTF8.GetBytes(resourceRoot);
        return EngineSetResourceRoot(engine, bytes, (nuint)bytes.Length) != 0;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nuint ResourceLoadCallback(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        IntPtr destination,
        nuint destinationCapacity);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nuint ResourceLoadCallbackV2(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        in NativeResourceRequestContext requestContext,
        IntPtr destination,
        nuint destinationCapacity);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nuint ResourceLoadCallbackV3(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        in NativeResourceRequestContextV3 requestContext,
        IntPtr destination,
        nuint destinationCapacity);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ScenePublishedCallback(
        IntPtr userData,
        ulong revision,
        ulong consumedInputSequence,
        float viewportWidth,
        float viewportHeight);

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

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void HostRequestAvailableCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void InteropCallbackAvailableCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void AnimationFrameRequestedCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void InspectorMessageAvailableCallback(
        IntPtr userData,
        ulong sessionId);

    private static class InspectorCallbackRegistration
    {
        internal static readonly InspectorMessageAvailableCallback Callback =
            NotifyInspectorMessageAvailable;
        internal static readonly IntPtr Address =
            Marshal.GetFunctionPointerForDelegate(Callback);

        // Suppress beforefieldinit so the delegate and thunk are created only
        // when the first Inspector session is actually opened.
        static InspectorCallbackRegistration()
        {
        }
    }

    internal static IntPtr GetInspectorMessageAvailableAddress()
        => InspectorCallbackRegistration.Address;

    private static void NotifyHostRequestAvailable(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyHostRequestAvailable();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native host request notification] {error}");
        }
    }

    private static void NotifyInteropCallbackAvailable(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyInteropCallbackAvailable();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native interop callback notification] {error}");
        }
    }

    private static void NotifyAnimationFrameRequested(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyAnimationFrameRequested();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native animation-frame notification] {error}");
        }
    }

    private static void NotifyInspectorMessageAvailable(
        IntPtr userData,
        ulong sessionId)
    {
        try
        {
            NativeInspectorRegistry.Current?.Notify(userData, sessionId);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene V8 inspector notification] {error}");
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
            if (availableMetricsSize < legacyMetricsSize || fontSize <= 0)
            {
                return 0;
            }
            var value = Marshal.PtrToStringUTF8(text, checked((int)textLength)) ?? string.Empty;
            var family = Marshal.PtrToStringUTF8(fontFamily, checked((int)fontFamilyLength))
                ?? "sans-serif";
            var registry = (GCHandle.FromIntPtr(userData).Target as ResourceBridge)
                ?.WebTypefaces;
            var measured = NativeTextShaping.Measure(
                value,
                family,
                fontSize,
                fontWeight,
                letterSpacing,
                wordSpacing,
                featureFlags: 0,
                registry: registry);
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
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native text shaping] {error}");
            return 0;
        }
    }

    private static void NotifyScenePublished(
        IntPtr userData,
        ulong revision,
        ulong consumedInputSequence,
        float viewportWidth,
        float viewportHeight)
    {
        try
        {
            ((ResourceBridge?)GCHandle.FromIntPtr(userData).Target)?.NotifyScenePublished(
                new NativeScenePublished(
                    revision,
                    consumedInputSequence,
                    viewportWidth,
                    viewportHeight));
        }
        catch (Exception error)
        {
            // Never allow a managed exception to unwind through the native
            // engine worker. The normal compositor loop remains a fallback.
            Console.Error.WriteLine($"[WebScene native scene publication] {error}");
        }
    }

    private static nuint LoadResource(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        IntPtr destination,
        nuint destinationCapacity)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            var address = Marshal.PtrToStringUTF8(url, checked((int)urlLength));
            var validator = entityTagLength == 0
                ? null
                : Marshal.PtrToStringUTF8(entityTag, checked((int)entityTagLength));
            return bridge is null || string.IsNullOrWhiteSpace(address)
                ? 0
                : bridge.Copy(
                    kind,
                    address,
                    validator,
                    lastModifiedUnixSeconds,
                    default,
                    destination,
                    destinationCapacity);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native resource loader] {error}");
            return 0;
        }
    }

    private static nuint LoadResourceV2(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        in NativeResourceRequestContext requestContext,
        IntPtr destination,
        nuint destinationCapacity)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            var address = Marshal.PtrToStringUTF8(url, checked((int)urlLength));
            var validator = entityTagLength == 0
                ? null
                : Marshal.PtrToStringUTF8(entityTag, checked((int)entityTagLength));
            var context = new WebSceneRequestContext(
                (WebSceneResourceInitiator)requestContext.Initiator,
                requestContext.OriginLength == 0
                    ? null
                    : Marshal.PtrToStringUTF8(
                        requestContext.Origin,
                        checked((int)requestContext.OriginLength)),
                requestContext.ReferrerLength == 0
                    ? null
                    : Marshal.PtrToStringUTF8(
                        requestContext.Referrer,
                        checked((int)requestContext.ReferrerLength)),
                (WebSceneFetchMode)requestContext.Mode,
                (WebSceneRequestDestination)requestContext.Destination);
            return bridge is null || string.IsNullOrWhiteSpace(address)
                ? 0
                : bridge.Copy(
                    kind,
                    address,
                    validator,
                    lastModifiedUnixSeconds,
                    context,
                    destination,
                    destinationCapacity);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native resource loader v2] {error}");
            return 0;
        }
    }

    private static nuint LoadResourceV3(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        in NativeResourceRequestContextV3 requestContext,
        IntPtr destination,
        nuint destinationCapacity)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            var address = Marshal.PtrToStringUTF8(url, checked((int)urlLength));
            var validator = entityTagLength == 0
                ? null
                : Marshal.PtrToStringUTF8(entityTag, checked((int)entityTagLength));
            var context = new WebSceneRequestContext(
                (WebSceneResourceInitiator)requestContext.Initiator,
                ReadUtf8(requestContext.Origin, requestContext.OriginLength),
                ReadUtf8(requestContext.Referrer, requestContext.ReferrerLength),
                (WebSceneFetchMode)requestContext.Mode,
                (WebSceneRequestDestination)requestContext.Destination);
            return bridge is null || string.IsNullOrWhiteSpace(address)
                ? 0
                : bridge.Copy(
                    kind,
                    address,
                    validator,
                    lastModifiedUnixSeconds,
                    context,
                    ReadUtf8(requestContext.Method, requestContext.MethodLength),
                    ReadUtf8(requestContext.Body, requestContext.BodyLength),
                    ReadUtf8(requestContext.ContentType, requestContext.ContentTypeLength),
                    destination,
                    destinationCapacity);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native resource loader v3] {error}");
            return 0;
        }

        static string? ReadUtf8(IntPtr value, nuint length)
            => length == 0
                ? null
                : Marshal.PtrToStringUTF8(value, checked((int)length));
    }

    internal sealed class ResourceBridge(
        IWebSceneResourceLoader loader,
        Action<NativeScenePublished> scenePublished,
        Action? hostRequestAvailable,
        Action? interopCallbackAvailable,
        Action? animationFrameRequested) : IDisposable
    {
        private const int EnvelopeHeaderSize = 2 + sizeof(uint) + sizeof(long) + sizeof(long);
        [ThreadStatic]
        private static PendingResourceCopy? _pendingCopy;
#if !WEBSCENE_UNO
        private readonly ConcurrentDictionary<string, byte> _registeredFontSources =
            new(StringComparer.Ordinal);
#endif
        internal NativeTextShaping.WebTypefaceRegistry WebTypefaces { get; } =
            NativeTextShaping.CreateWebTypefaceRegistry();

        public void Dispose()
        {
            if (ReferenceEquals(_pendingCopy?.Owner, this))
            {
                _pendingCopy = null;
            }
            WebTypefaces.Dispose();
        }

        public void NotifyScenePublished(NativeScenePublished scene)
            => scenePublished(scene);

        public void NotifyHostRequestAvailable()
            => hostRequestAvailable?.Invoke();

        public void NotifyInteropCallbackAvailable()
            => interopCallbackAvailable?.Invoke();

        public void NotifyAnimationFrameRequested()
            => animationFrameRequested?.Invoke();

        public nuint Copy(
            uint kind,
            string address,
            string? entityTag,
            long lastModifiedUnixSeconds,
            IntPtr destination,
            nuint capacity)
            => Copy(
                kind,
                address,
                entityTag,
                lastModifiedUnixSeconds,
                default,
                destination,
                capacity);

        public nuint Copy(
            uint kind,
            string address,
            string? entityTag,
            long lastModifiedUnixSeconds,
            WebSceneRequestContext requestContext,
            IntPtr destination,
            nuint capacity)
            => Copy(
                kind,
                address,
                entityTag,
                lastModifiedUnixSeconds,
                requestContext,
                null,
                null,
                null,
                destination,
                capacity);

        public nuint Copy(
            uint kind,
            string address,
            string? entityTag,
            long lastModifiedUnixSeconds,
            WebSceneRequestContext requestContext,
            string? method,
            string? body,
            string? contentType,
            IntPtr destination,
            nuint capacity)
        {
            var pending = _pendingCopy;
            if (pending is not null
                && pending.Matches(
                    this,
                    kind,
                    address,
                    entityTag,
                    lastModifiedUnixSeconds,
                    requestContext,
                    method,
                    body,
                    contentType))
            {
                if (destination == IntPtr.Zero || capacity < pending.RequiredLength)
                {
                    return pending.RequiredLength;
                }
                _pendingCopy = null;
                return WriteEnvelope(
                    pending.Resource,
                    pending.RequestIfModifiedSince,
                    destination);
            }
            if (ReferenceEquals(pending?.Owner, this))
            {
                _pendingCopy = null;
            }

            var resourceKind = kind switch
            {
                1 => WebSceneResourceKind.Script,
                2 => WebSceneResourceKind.StyleSheet,
                3 => WebSceneResourceKind.Image,
                4 => WebSceneResourceKind.Data,
                _ => WebSceneResourceKind.Markup
            };
            var request = new WebSceneResourceRequest(address, null, resourceKind)
            {
                Context = requestContext,
                Method = method,
                Body = body,
                ContentType = contentType,
                IfNoneMatch = entityTag,
                IfModifiedSince = lastModifiedUnixSeconds > 0
                    ? DateTimeOffset.FromUnixTimeSeconds(lastModifiedUnixSeconds)
                    : null
            };
            PreparedResource prepared;
            var resourceStarted = System.Diagnostics.Stopwatch.GetTimestamp();
            try
            {
#if !WEBSCENE_UNO
                if (loader is AvaloniaResourceLoader avaloniaLoader
                    && avaloniaLoader.TryLoadUtf8(request, out var utf8Resource))
                {
                    if (resourceKind == WebSceneResourceKind.StyleSheet)
                    {
                        RegisterWebFonts(
                            Encoding.UTF8.GetString(utf8Resource.Content.Span),
                            address,
                            avaloniaLoader);
                    }
                    prepared = PrepareResource(utf8Resource, entityTag);
                }
                else
#endif
                {
                    var resource = loader.LoadText(request);
#if !WEBSCENE_UNO
                    if (resourceKind == WebSceneResourceKind.StyleSheet
                        && loader is AvaloniaResourceLoader textAvaloniaLoader)
                    {
                        RegisterWebFonts(resource.Content, address, textAvaloniaLoader);
                    }
#endif
                    prepared = PrepareResource(resource, entityTag);
                }
            }
            catch (Exception error)
            {
                // Preserve transport metadata across the ABI, not exception text which
                // frequently contains credentials, query parameters or request bodies.
                var category = error switch
                {
                    System.Net.Http.HttpRequestException { StatusCode: not null } => "http",
                    System.Net.Http.HttpRequestException => "network",
                    TimeoutException or TaskCanceledException => "timeout",
                    OperationCanceledException => "cancelled",
                    FileNotFoundException or DirectoryNotFoundException => "not-found",
                    NotSupportedException => "unsupported",
                    _ => "loader"
                };
                var status = error is System.Net.Http.HttpRequestException { StatusCode: { } code } ? (int)code : 0;
                prepared = new PreparedResource(false, false, null, null, category,
                    category.Length, null, default, false, 0,
                    (nuint)(EnvelopeHeaderSize + category.Length), true, status,
                    (long)(System.Diagnostics.Stopwatch.GetElapsedTime(resourceStarted).TotalMilliseconds * 1000));
            }
            if (destination == IntPtr.Zero || capacity < prepared.RequiredLength)
            {
                _pendingCopy = new PendingResourceCopy(
                    this,
                    kind,
                    address,
                    entityTag,
                    lastModifiedUnixSeconds,
                    requestContext,
                    method,
                    body,
                    contentType,
                    prepared,
                    request.IfModifiedSince,
                    prepared.RequiredLength);
                return prepared.RequiredLength;
            }

            return WriteEnvelope(
                prepared,
                request.IfModifiedSince,
                destination);
        }

        private static PreparedResource PrepareResource(
            in WebSceneTextResource resource,
            string? requestEntityTag)
        {
            var responseEntityTag = resource.EntityTag ?? requestEntityTag ?? string.Empty;
            var responseEntityTagLength = Encoding.UTF8.GetByteCount(responseEntityTag);
            var contentLength = resource.NotModified
                ? 0
                : Encoding.UTF8.GetByteCount(resource.Content);
            return new PreparedResource(
                resource.NotModified,
                resource.IsCacheable,
                resource.LastModified,
                resource.FreshUntil,
                responseEntityTag,
                responseEntityTagLength,
                resource.Content,
                default,
                false,
                contentLength,
                checked((nuint)(
                    EnvelopeHeaderSize + responseEntityTagLength + contentLength)));
        }

#if !WEBSCENE_UNO
        private static PreparedResource PrepareResource(
            in AvaloniaUtf8Resource resource,
            string? requestEntityTag)
        {
            var responseEntityTag = resource.EntityTag ?? requestEntityTag ?? string.Empty;
            var responseEntityTagLength = Encoding.UTF8.GetByteCount(responseEntityTag);
            var contentLength = resource.Content.Length;
            return new PreparedResource(
                false,
                resource.IsCacheable,
                resource.LastModified,
                resource.FreshUntil,
                responseEntityTag,
                responseEntityTagLength,
                null,
                resource.Content,
                true,
                contentLength,
                checked((nuint)(
                    EnvelopeHeaderSize + responseEntityTagLength + contentLength)));
        }
#endif

        private static nuint WriteEnvelope(
            in PreparedResource resource,
            DateTimeOffset? requestIfModifiedSince,
            IntPtr destination)
        {
            var length = checked((int)resource.RequiredLength);
            var bytes = new Span<byte>((void*)destination, length);
            bytes[0] = resource.Failed ? (byte)3 : resource.NotModified ? (byte)2 : (byte)1;
            bytes[1] = resource.IsCacheable ? (byte)1 : (byte)0;
            BinaryPrimitives.WriteUInt32LittleEndian(
                bytes[2..],
                checked((uint)resource.ResponseEntityTagLength));
            BinaryPrimitives.WriteInt64LittleEndian(
                bytes[(2 + sizeof(uint))..],
                resource.Failed ? resource.HttpStatus : (resource.LastModified ?? requestIfModifiedSince)?.ToUnixTimeSeconds() ?? 0);
            BinaryPrimitives.WriteInt64LittleEndian(
                bytes[(2 + sizeof(uint) + sizeof(long))..],
                resource.Failed ? resource.DurationMicroseconds : resource.FreshUntil?.ToUnixTimeSeconds() ?? 0);
            Encoding.UTF8.GetBytes(
                resource.ResponseEntityTag,
                bytes.Slice(EnvelopeHeaderSize, resource.ResponseEntityTagLength));
            if (resource.ContentLength != 0)
            {
                var content = bytes.Slice(
                    EnvelopeHeaderSize + resource.ResponseEntityTagLength,
                    resource.ContentLength);
                if (resource.ContentIsUtf8)
                {
                    resource.Utf8Content.Span.CopyTo(content);
                }
                else
                {
                    Encoding.UTF8.GetBytes(resource.TextContent!, content);
                }
            }
            return checked((nuint)length);
        }

        private readonly record struct PreparedResource(
            bool NotModified,
            bool IsCacheable,
            DateTimeOffset? LastModified,
            DateTimeOffset? FreshUntil,
            string ResponseEntityTag,
            int ResponseEntityTagLength,
            string? TextContent,
            ReadOnlyMemory<byte> Utf8Content,
            bool ContentIsUtf8,
            int ContentLength,
            nuint RequiredLength,
            bool Failed = false,
            int HttpStatus = 0,
            long DurationMicroseconds = 0);

        private sealed record PendingResourceCopy(
            ResourceBridge Owner,
            uint Kind,
            string Address,
            string? EntityTag,
            long LastModifiedUnixSeconds,
            WebSceneRequestContext RequestContext,
            string? Method,
            string? Body,
            string? ContentType,
            PreparedResource Resource,
            DateTimeOffset? RequestIfModifiedSince,
            nuint RequiredLength)
        {
            internal bool Matches(
                ResourceBridge owner,
                uint kind,
                string address,
                string? entityTag,
                long lastModifiedUnixSeconds,
                WebSceneRequestContext requestContext,
                string? method,
                string? body,
                string? contentType)
                => ReferenceEquals(Owner, owner)
                    && Kind == kind
                    && LastModifiedUnixSeconds == lastModifiedUnixSeconds
                    && RequestContext == requestContext
                    && string.Equals(Method, method, StringComparison.Ordinal)
                    && string.Equals(Body, body, StringComparison.Ordinal)
                    && string.Equals(ContentType, contentType, StringComparison.Ordinal)
                    && string.Equals(Address, address, StringComparison.Ordinal)
                    && string.Equals(EntityTag, entityTag, StringComparison.Ordinal);
        }

#if !WEBSCENE_UNO
        private void RegisterWebFonts(
            string css,
            string stylesheetAddress,
            AvaloniaResourceLoader avaloniaLoader)
        {
            foreach (var rule in CssFontFaceRules(css))
            {
                var family = CssDeclarationValue(rule, "font-family")
                    ?.Trim().Trim('"', '\'');
                var source = FirstCssUrl(CssDeclarationValue(rule, "src"));
                if (string.IsNullOrWhiteSpace(family)
                    || string.IsNullOrWhiteSpace(source))
                {
                    continue;
                }

                var sourceKey = $"{family}\u001f{stylesheetAddress}\u001f{source}";
                if (!_registeredFontSources.TryAdd(sourceKey, 0)) continue;
                try
                {
                    var resource = avaloniaLoader
                        .LoadBytesAsync(source, stylesheetAddress, CancellationToken.None)
                        .GetAwaiter()
                        .GetResult();
                    if (!WebTypefaces.Register(family, resource.Content))
                    {
                        Console.Error.WriteLine(
                            $"[WebScene native web font] '{resource.DisplayName}' is not a supported font.");
                    }
                }
                catch (Exception error)
                {
                    Console.Error.WriteLine(
                        $"[WebScene native web font] Could not load '{source}' from "
                        + $"'{stylesheetAddress}': {error.Message}");
                }
            }
        }

        private static IEnumerable<string> CssFontFaceRules(string css)
        {
            var cursor = 0;
            while (cursor < css.Length)
            {
                var rule = css.IndexOf("@font-face", cursor, StringComparison.OrdinalIgnoreCase);
                if (rule < 0) yield break;
                var open = css.IndexOf('{', rule + 10);
                if (open < 0) yield break;
                var close = css.IndexOf('}', open + 1);
                if (close < 0) yield break;
                yield return css[(open + 1)..close];
                cursor = close + 1;
            }
        }

        private static string? CssDeclarationValue(string rule, string name)
        {
            var cursor = 0;
            while (cursor < rule.Length)
            {
                while (cursor < rule.Length
                    && (char.IsWhiteSpace(rule[cursor]) || rule[cursor] == ';'))
                {
                    cursor++;
                }
                var separator = rule.IndexOf(':', cursor);
                if (separator < 0) return null;
                var end = separator + 1;
                var parenthesisDepth = 0;
                var quote = '\0';
                for (; end < rule.Length; end++)
                {
                    var character = rule[end];
                    if (quote != '\0')
                    {
                        if (character == quote
                            && (end == 0 || rule[end - 1] != '\\'))
                        {
                            quote = '\0';
                        }
                        continue;
                    }
                    if (character is '\'' or '"') quote = character;
                    else if (character == '(') parenthesisDepth++;
                    else if (character == ')' && parenthesisDepth > 0) parenthesisDepth--;
                    else if (character == ';' && parenthesisDepth == 0) break;
                }
                if (string.Equals(
                        rule[cursor..separator].Trim(),
                        name,
                        StringComparison.OrdinalIgnoreCase))
                {
                    return rule[(separator + 1)..end].Trim();
                }
                cursor = end + 1;
            }
            return null;
        }

        private static string? FirstCssUrl(string? value)
        {
            if (string.IsNullOrWhiteSpace(value)) return null;
            var start = value.IndexOf("url(", StringComparison.OrdinalIgnoreCase);
            if (start < 0) return null;
            var end = value.IndexOf(')', start + 4);
            if (end < 0) return null;
            return value[(start + 4)..end].Trim().Trim('"', '\'');
        }
#endif
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_enqueue")]
    internal static extern byte EngineEnqueue(IntPtr engine, in InputEvent input);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_enqueue_resize_frame")]
    internal static extern byte EngineEnqueueResizeFrame(
        IntPtr engine,
        in InputEvent resize,
        in InputEvent frame);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_cursor")]
    public static extern uint EngineGetCursor(IntPtr engine);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_observe_host_timeline")]
    internal static extern void EngineObserveHostTimeline(
        IntPtr engine,
        double timestampMilliseconds);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_observe_compositor_frame")]
    internal static extern void EngineObserveCompositorFrame(
        IntPtr engine,
        double timestampMilliseconds);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_requires_animation_frame")]
    public static extern byte EngineRequiresAnimationFrame(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_execute_script")]
    private static extern byte EngineExecuteScript(
        IntPtr engine,
        byte[] source,
        nuint sourceLength,
        byte[] documentName,
        nuint documentNameLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_begin_evaluate_v3")]
    internal static extern ulong EngineBeginEvaluateV3(
        IntPtr engine,
        in NativeInteropEvaluateRequest request,
        IntPtr completed,
        IntPtr userData);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_begin_invoke_v3")]
    internal static extern ulong EngineBeginInvokeV3(
        IntPtr engine,
        in NativeInteropInvokeRequest request,
        IntPtr completed,
        IntPtr userData);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_invoke_result_v3")]
    internal static extern IntPtr EngineTakeInvokeResultV3(
        IntPtr engine,
        ulong operationId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_cancel_invoke_v3")]
    internal static extern byte EngineCancelInvokeV3(
        IntPtr engine,
        ulong operationId);

    [DllImport(LibraryName, EntryPoint = "webscene_interop_result_release_v3")]
    internal static extern void InteropResultReleaseV3(
        IntPtr result,
        ulong leaseId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_callback_v3")]
    internal static extern IntPtr EngineTakeCallbackV3(IntPtr engine);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_complete_callback_v3")]
    internal static extern byte EngineCompleteCallbackV3(
        IntPtr engine,
        in NativeInteropCallbackCompletion completion);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_cancel_callback_v3")]
    internal static extern byte EngineCancelCallbackV3(
        IntPtr engine,
        ulong callId);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_interop_callback_release_v3")]
    internal static extern void InteropCallbackReleaseV3(
        IntPtr callback,
        ulong leaseId);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_get_interop_pool_metrics_v3")]
    private static extern byte EngineGetInteropPoolMetricsV3(
        IntPtr engine,
        ref NativeInteropPoolMetrics metrics);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_host_request")]
    private static extern nuint EngineTakeHostRequest(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_console_message")]
    private static extern nuint EngineTakeConsoleMessage(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_input_dispatch_failure")]
    private static extern nuint EngineTakeInputDispatchFailure(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_last_error")]
    private static extern nuint EngineCopyLastError(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_first_iframe_html")]
    private static extern nuint EngineCopyFirstIframeHtml(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_scene_diagnostics")]
    private static extern nuint EngineCopySceneDiagnostics(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_feature_use")]
    private static extern nuint EngineCopyFeatureUse(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_event_listener_inventory")]
    private static extern nuint EngineCopyEventListenerInventory(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_canvas_layouts")]
    private static extern nuint EngineCopyCanvasLayouts(
        IntPtr engine,
        CanvasLayout* destination,
        nuint destinationCapacity);

    public static bool TryExecuteScript(IntPtr engine, string source, string documentName)
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

    public static NativeInteropPoolMetrics GetInteropPoolMetrics(IntPtr engine)
    {
        var metrics = new NativeInteropPoolMetrics
        {
            StructSize = (uint)Marshal.SizeOf<NativeInteropPoolMetrics>(),
            Version = 3
        };
        if (EngineGetInteropPoolMetricsV3(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The experimental native interop metrics ABI is unavailable.");
        }
        return metrics;
    }

    public static bool TryTakeHostRequest(IntPtr engine, out string request)
    {
        var required = EngineTakeHostRequest(engine, null, 0);
        if (required <= 1)
        {
            request = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeHostRequest(engine, destination, (nuint)destination.Length);
        if (copied != required)
        {
            request = string.Empty;
            return false;
        }
        request = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        return true;
    }

    public static bool TryTakeConsoleMessage(
        IntPtr engine,
        out string level,
        out string message)
    {
        var required = EngineTakeConsoleMessage(engine, null, 0);
        if (required <= 1)
        {
            level = string.Empty;
            message = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeConsoleMessage(engine, destination, (nuint)destination.Length);
        if (copied != required)
        {
            level = string.Empty;
            message = string.Empty;
            return false;
        }
        var payload = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        var separator = payload.IndexOf('\n');
        level = separator < 0 ? "log" : payload[..separator];
        message = separator < 0 ? payload : payload[(separator + 1)..];
        return true;
    }

    public static bool TryTakeInputDispatchFailure(
        IntPtr engine,
        out ulong sequence,
        out uint kind,
        out string error)
    {
        var required = EngineTakeInputDispatchFailure(engine, null, 0);
        if (required <= 1)
        {
            sequence = 0;
            kind = 0;
            error = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeInputDispatchFailure(
            engine,
            destination,
            (nuint)destination.Length);
        if (copied != required)
        {
            sequence = 0;
            kind = 0;
            error = string.Empty;
            return false;
        }
        var payload = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        var firstSeparator = payload.IndexOf('\n');
        var secondSeparator = firstSeparator < 0
            ? -1
            : payload.IndexOf('\n', firstSeparator + 1);
        if (firstSeparator <= 0
            || secondSeparator <= firstSeparator + 1
            || !ulong.TryParse(payload.AsSpan(0, firstSeparator), out sequence)
            || !uint.TryParse(
                payload.AsSpan(firstSeparator + 1, secondSeparator - firstSeparator - 1),
                out kind))
        {
            sequence = 0;
            kind = 0;
            error = "Malformed native input-dispatch failure payload: " + payload;
            return true;
        }
        error = payload[(secondSeparator + 1)..];
        return true;
    }

    public static string GetLastError(IntPtr engine)
    {
        var required = EngineCopyLastError(engine, null, 0);
        if (required <= 1)
        {
            return string.Empty;
        }
        var bytes = new byte[checked((int)required)];
        EngineCopyLastError(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetFirstIframeHtml(IntPtr engine)
    {
        var required = EngineCopyFirstIframeHtml(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyFirstIframeHtml(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetSceneDiagnostics(IntPtr engine)
    {
        var required = EngineCopySceneDiagnostics(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopySceneDiagnostics(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetFeatureUse(IntPtr engine)
    {
        var required = EngineCopyFeatureUse(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyFeatureUse(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetEventListenerInventory(IntPtr engine)
    {
        var required = EngineCopyEventListenerInventory(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyEventListenerInventory(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static CanvasLayout[] GetCanvasLayouts(IntPtr engine)
    {
        var required = EngineCopyCanvasLayouts(engine, null, 0);
        if (required == 0) return [];
        var layouts = new CanvasLayout[checked((int)required)];
        fixed (CanvasLayout* destination = layouts)
        {
            var actual = EngineCopyCanvasLayouts(
                engine,
                destination,
                (nuint)layouts.Length);
            if (actual > (nuint)layouts.Length)
            {
                throw new InvalidOperationException("Native canvas layout snapshot changed during copy.");
            }
        }
        return layouts;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_latest_scene")]
    public static extern IntPtr EngineAcquireLatestScene(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_next_scene")]
    public static extern IntPtr EngineAcquireNextScene(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_request_scene_checkpoint")]
    public static extern byte EngineRequestSceneCheckpoint(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_release_canvas_export")]
    internal static extern byte EngineReleaseCanvasExport(IntPtr engine, uint nodeId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_request_low_memory")]
    public static extern byte EngineRequestLowMemory(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_visible")]
    public static extern byte EngineSetVisible(IntPtr engine, byte visible);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_set_preferred_color_scheme")]
    internal static extern byte EngineSetPreferredColorScheme(
        IntPtr engine,
        NativePreferredColorScheme preferredColorScheme);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_release")]
    public static extern void SceneRelease(IntPtr scene);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_acknowledge")]
    public static extern byte SceneAcknowledge(IntPtr scene);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_get_header")]
    internal static extern byte SceneGetHeader(IntPtr scene, out SceneHeader header);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_get_commands")]
    internal static extern SceneCommand* SceneGetCommands(IntPtr scene, out uint count);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_metrics")]
    public static extern void EngineGetMetrics(IntPtr engine, out EngineMetrics metrics);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_input_dispatch_metrics")]
    private static extern byte EngineGetInputDispatchMetrics(
        IntPtr engine,
        ref InputDispatchMetrics metrics);

    public static InputDispatchMetrics GetInputDispatchMetrics(IntPtr engine)
    {
        var metrics = new InputDispatchMetrics
        {
            StructSize = (uint)Marshal.SizeOf<InputDispatchMetrics>()
        };
        if (EngineGetInputDispatchMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native input-dispatch metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_animation_frame_metrics")]
    private static extern byte EngineGetAnimationFrameMetrics(
        IntPtr engine,
        ref AnimationFrameMetrics metrics);

    public static AnimationFrameMetrics GetAnimationFrameMetrics(IntPtr engine)
    {
        var metrics = new AnimationFrameMetrics
        {
            StructSize = (uint)Marshal.SizeOf<AnimationFrameMetrics>()
        };
        if (EngineGetAnimationFrameMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native animation-frame metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_scene_flow_metrics")]
    private static extern byte EngineGetSceneFlowMetrics(
        IntPtr engine,
        ref SceneFlowMetrics metrics);

    public static SceneFlowMetrics GetSceneFlowMetrics(IntPtr engine)
    {
        var metrics = new SceneFlowMetrics
        {
            StructSize = (uint)Marshal.SizeOf<SceneFlowMetrics>()
        };
        if (EngineGetSceneFlowMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native scene-flow metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_resize_frame_metrics")]
    private static extern byte EngineGetResizeFrameMetrics(
        IntPtr engine,
        ref ResizeFrameMetrics metrics);

    public static ResizeFrameMetrics GetResizeFrameMetrics(IntPtr engine)
    {
        var metrics = new ResizeFrameMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ResizeFrameMetrics>()
        };
        if (EngineGetResizeFrameMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native resize/frame metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_resource_cache_metrics")]
    private static extern byte EngineGetResourceCacheMetrics(
        IntPtr engine,
        ref ResourceCacheMetrics metrics);

    public static ResourceCacheMetrics GetResourceCacheMetrics(IntPtr engine)
    {
        var metrics = new ResourceCacheMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ResourceCacheMetrics>()
        };
        if (EngineGetResourceCacheMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException("The native resource-cache metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_runtime_work_metrics")]
    private static extern byte EngineGetRuntimeWorkMetrics(
        IntPtr engine,
        ref RuntimeWorkMetrics metrics);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_set_runtime_work_metrics_enabled")]
    private static extern byte EngineSetRuntimeWorkMetricsEnabled(
        IntPtr engine,
        byte enabled);

    public static bool TryEnableRuntimeWorkMetrics(IntPtr engine)
    {
        try
        {
            return EngineSetRuntimeWorkMetricsEnabled(engine, 1) != 0;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
    }

    public static RuntimeWorkMetrics? TryGetRuntimeWorkMetrics(IntPtr engine)
    {
        var metrics = new RuntimeWorkMetrics
        {
            StructSize = (uint)Marshal.SizeOf<RuntimeWorkMetrics>()
        };
        try
        {
            return EngineGetRuntimeWorkMetrics(engine, ref metrics) == 0
                ? null
                : metrics;
        }
        catch (EntryPointNotFoundException)
        {
            return null;
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_process_cache_metrics")]
    private static extern byte EngineGetProcessCacheMetrics(
        IntPtr engine,
        ref ProcessCacheMetrics metrics);

    public static ProcessCacheMetrics? TryGetProcessCacheMetrics(IntPtr engine)
    {
        var metrics = new ProcessCacheMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ProcessCacheMetrics>()
        };
        try
        {
            return EngineGetProcessCacheMetrics(engine, ref metrics) == 0
                ? null
                : metrics;
        }
        catch (EntryPointNotFoundException)
        {
            return null;
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_memory_metrics")]
    private static extern byte EngineGetMemoryMetrics(
        IntPtr engine,
        ref EngineMemoryMetrics metrics);

    public static EngineMemoryMetrics? TryGetMemoryMetrics(IntPtr engine)
    {
        var metrics = new EngineMemoryMetrics
        {
            StructSize = (uint)Marshal.SizeOf<EngineMemoryMetrics>()
        };
        try
        {
            return EngineGetMemoryMetrics(engine, ref metrics) == 0
                ? null
                : metrics;
        }
        catch (EntryPointNotFoundException)
        {
            return null;
        }
    }
}

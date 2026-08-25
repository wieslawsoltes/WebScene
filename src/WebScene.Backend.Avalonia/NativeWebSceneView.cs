using System.Diagnostics;
using System.Runtime.CompilerServices;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using Avalonia.Threading;
using WebScene.Backends.Native;
using WebScene.JavaScript.Interop;

namespace WebScene.Backends.Avalonia.Native;

/// <summary>
/// Hosts an arbitrary HTML document in WebScene's ABI 3 native DOM/runtime.
/// The native engine owns navigation, DOM, JavaScript, CSS, layout, and scene
/// production; the attached Avalonia surface projects those scenes with Skia.
/// </summary>
public sealed class NativeWebSceneView : ContentControl, IAsyncDisposable
{
    private static long s_nextContextId;
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly NativeSceneSurface _surface;
    private IntPtr _engine;
    private long _contextId;
    private NativeInteropInvoker? _interop;
    private JavaScriptCallbackSignal? _interopCallbackSignal;
    private CancellationTokenSource? _navigationCancellation;
    private readonly SemaphoreSlim _hostRequestGate = new(1, 1);
    private static readonly HttpClient HostRequestHttpClient = new();

    public NativeWebSceneView()
        : this(useCompositionVisual: true)
    {
    }

    public NativeWebSceneView(bool useCompositionVisual)
    {
        _surface = new NativeSceneSurface(IntPtr.Zero, useCompositionVisual);
        Content = _surface;
        ActualThemeVariantChanged += OnActualThemeVariantChanged;
    }

    public string? Source { get; private set; }

    public INativeWebSceneRenderDiagnostics RenderDiagnostics => _surface;

    public string LastError
        => _engine == IntPtr.Zero
            ? string.Empty
            : NativeWebSceneApi.GetLastError(_engine);

    public string SceneDiagnostics
        => _engine == IntPtr.Zero
            ? string.Empty
            : NativeWebSceneApi.GetSceneDiagnostics(_engine);

    public string FirstIframeHtml
        => _engine == IntPtr.Zero
            ? string.Empty
            : NativeWebSceneApi.GetFirstIframeHtml(_engine);

    public string FeatureUseReport
        => _engine == IntPtr.Zero
            ? string.Empty
            : NativeWebSceneApi.GetFeatureUse(_engine);

    public string[] DrainConsoleMessages()
    {
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            return [];
        }
        var messages = new List<string>();
        while (NativeWebSceneApi.TryTakeConsoleMessage(
                   engine,
                   out var level,
                   out var message))
        {
            messages.Add($"{level}\n{message}");
        }
        return messages.ToArray();
    }

    /// <summary>
    /// Creates a generated-binding invoker backed only by the ABI 3 tagged
    /// transport. Dispose the returned invoker before unloading this view.
    /// </summary>
    public NativeJavaScriptInvoker CreateJavaScriptInvoker()
    {
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native WebScene document is not loaded.");
        }
        var callbackSignal = Volatile.Read(ref _interopCallbackSignal)
            ?? throw new InvalidOperationException(
                "The native callback signal is not available.");
        return new NativeJavaScriptInvoker(
            new NativeJavaScriptBinaryTransport(engine),
            callbackSignal.WaitAsync);
    }

    /// <summary>
    /// Opens a raw V8 Inspector Protocol session for this view's dedicated
    /// isolate. The session can be forwarded unchanged to a CDP host.
    /// </summary>
    public INativeV8InspectorSession OpenV8InspectorSession(
        bool waitForDebugger = false)
    {
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native WebScene document is not loaded.");
        }
        return NativeWebSceneApi.OpenInspectorSession(engine, waitForDebugger);
    }

    /// <summary>
    /// Waits until the engine worker has initialized the dedicated-isolate V8
    /// Inspector. Engine creation is asynchronous, so startup hosts should use
    /// this barrier before opening a pre-navigation session.
    /// </summary>
    public async ValueTask WaitForV8InspectorAvailableAsync(
        TimeSpan? timeout = null,
        CancellationToken cancellationToken = default)
    {
        var timeoutValue = timeout ?? TimeSpan.FromSeconds(10);
        using var deadline = new CancellationTokenSource(timeoutValue);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            deadline.Token);
        try
        {
            while (true)
            {
                var engine = Volatile.Read(ref _engine);
                if (engine == IntPtr.Zero)
                {
                    throw new InvalidOperationException(
                        "The native WebScene document is not loaded.");
                }
                if (NativeWebSceneApi.IsInspectorAvailable(engine)) return;
                await Task.Delay(10, linked.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (
            deadline.IsCancellationRequested
            && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                "The dedicated-isolate V8 Inspector did not become available. "
                + "Shared-isolate mode intentionally does not expose Inspector sessions.");
        }
    }

    internal static NativePreferredColorScheme ResolvePreferredColorScheme(
        ThemeVariant themeVariant)
        => themeVariant == ThemeVariant.Dark
            ? NativePreferredColorScheme.Dark
            : NativePreferredColorScheme.Light;

    private void OnActualThemeVariantChanged(object? sender, EventArgs args)
    {
        var engine = Volatile.Read(ref _engine);
        if (engine != IntPtr.Zero)
        {
            NativeWebSceneApi.EngineSetPreferredColorScheme(
                engine,
                ResolvePreferredColorScheme(ActualThemeVariant));
        }
    }

    /// <summary>
    /// Evaluates diagnostic JavaScript through the leased ABI 3 arena and
    /// materializes JSON-compatible text on the managed side. Generated APIs
    /// do not use this allocation-oriented diagnostic helper.
    /// </summary>
    public async Task<string> EvaluateTextAsync(
        string source,
        string documentName = "native-host-evaluation.js",
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(documentName);

        using var lease = await EvaluateAsync(
            source,
            documentName,
            cancellationToken).ConfigureAwait(false);
        using var borrowed = lease.Borrow();
        return NativeInteropJsonText.Serialize(borrowed.Root);
    }

    /// <summary>
    /// Evaluates JavaScript through the immutable ABI 3 result arena.
    /// Dispose the returned lease after materializing or borrowing its root.
    /// </summary>
    public ValueTask<NativeInteropResultLease> EvaluateAsync(
        string source,
        string documentName = "native-host-binary-evaluation.js",
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(documentName);
        var interop = Volatile.Read(ref _interop);
        if (interop is null)
        {
            throw new InvalidOperationException(
                "The native WebScene document is not loaded.");
        }
        return interop.InvokeAsync(source, documentName, cancellationToken);
    }

    /// <summary>
    /// Evaluates JavaScript and lets a generated struct codec materialize the
    /// final managed result directly from the immutable native arena.
    /// </summary>
    public async ValueTask<T> EvaluateAsync<T, TDecoder>(
        string source,
        TDecoder decoder,
        string documentName = "native-host-binary-evaluation.js",
        CancellationToken cancellationToken = default)
        where TDecoder : struct, INativeInteropValueDecoder<T>
    {
        using var lease = await EvaluateAsync(
            source,
            documentName,
            cancellationToken).ConfigureAwait(false);
        using var borrowed = lease.Borrow();
        return decoder.Decode(borrowed.Root);
    }

    public NativeInteropPoolMetrics InteropPoolMetrics
    {
        get
        {
            var engine = Volatile.Read(ref _engine);
            if (engine == IntPtr.Zero)
            {
                return default;
            }
            return NativeWebSceneApi.GetInteropPoolMetrics(engine);
        }
    }

    /// <summary>
    /// Captures native, interop, renderer, and compositor counters in one
    /// read-on-demand object. The first call opts this context into detailed
    /// runtime-work counters; contexts that are never sampled retain the default
    /// disabled path. Use <see cref="NativeWebScenePerformanceSnapshot.Since"/>
    /// with an earlier snapshot from this loaded context to establish a baseline
    /// without resetting counters.
    /// </summary>
    public NativeWebScenePerformanceSnapshot CapturePerformanceSnapshot()
    {
        var engine = Volatile.Read(ref _engine);
        var contextId = Volatile.Read(ref _contextId);
        if (engine == IntPtr.Zero || contextId == 0)
        {
            throw new InvalidOperationException(
                "The WebScene native document is not loaded.");
        }

        NativeWebSceneApi.TryEnableRuntimeWorkMetrics(engine);
        NativeWebSceneApi.EngineGetMetrics(engine, out var engineMetrics);
        return new NativeWebScenePerformanceSnapshot(
            ContextId: contextId,
            Timestamp: Stopwatch.GetTimestamp(),
            Engine: engineMetrics,
            InputDispatch: NativeWebSceneApi.GetInputDispatchMetrics(engine),
            AnimationFrames: NativeWebSceneApi.GetAnimationFrameMetrics(engine),
            SceneFlow: NativeWebSceneApi.GetSceneFlowMetrics(engine),
            ResizeFrames: NativeWebSceneApi.GetResizeFrameMetrics(engine),
            ResourceCache: NativeWebSceneApi.GetResourceCacheMetrics(engine),
            RuntimeWork: NativeWebSceneApi.TryGetRuntimeWorkMetrics(engine),
            ProcessCache: NativeWebSceneApi.TryGetProcessCacheMetrics(engine),
            Memory: NativeWebSceneApi.TryGetMemoryMetrics(engine),
            InteropPool: NativeWebSceneApi.GetInteropPoolMetrics(engine),
            RendererMemory: _surface.GetRendererMemoryMetrics(),
            Surface: _surface.CapturePerformanceMetrics(),
            ProcessWebTypefaces:
                NativeTextShaping.GetWebTypefaceCacheMetrics(),
            ProcessComposition: NativeSceneSurface.CompositionFlowMetrics);
    }

    public Task LoadAsync(
        string source,
        string nativeLibraryPath,
        string? compilationCacheDirectory = null,
        CancellationToken cancellationToken = default)
        => LoadAsync(
            new NativeWebSceneLoadOptions
            {
                Source = source,
                NativeLibraryPath = nativeLibraryPath,
                CompilationCacheDirectory = compilationCacheDirectory
            },
            cancellationToken);

    public Task LoadAsync(
        NativeWebSceneLoadOptions options,
        CancellationToken cancellationToken = default)
        => LoadCoreAsync(
            options,
            beforeNavigation: null,
            firstDocumentSceneTimeout: null,
            cancellationToken);

    /// <summary>
    /// Loads a document after allowing an asynchronous host hook to observe
    /// the initialized native engine. Inspector hosts use this hook to enter
    /// waiting-for-debugger mode before any document script is queued.
    /// </summary>
    public Task LoadAsync(
        NativeWebSceneLoadOptions options,
        Func<NativeWebSceneView, CancellationToken, ValueTask>? beforeNavigation,
        TimeSpan? firstDocumentSceneTimeout = null,
        CancellationToken cancellationToken = default)
        => LoadCoreAsync(
            options,
            beforeNavigation,
            firstDocumentSceneTimeout,
            cancellationToken);

    /// <summary>
    /// Loads a document after allowing an asynchronous host hook to observe
    /// the initialized native engine. Inspector hosts use this hook to enter
    /// waiting-for-debugger mode before any document script is queued.
    /// </summary>
    public Task LoadAsync(
        string source,
        string nativeLibraryPath,
        string? compilationCacheDirectory,
        Func<NativeWebSceneView, CancellationToken, ValueTask>? beforeNavigation,
        TimeSpan? firstDocumentSceneTimeout = null,
        CancellationToken cancellationToken = default)
        => LoadCoreAsync(
            new NativeWebSceneLoadOptions
            {
                Source = source,
                NativeLibraryPath = nativeLibraryPath,
                CompilationCacheDirectory = compilationCacheDirectory
            },
            beforeNavigation,
            firstDocumentSceneTimeout,
            cancellationToken);

    private async Task LoadCoreAsync(
        NativeWebSceneLoadOptions options,
        Func<NativeWebSceneView, CancellationToken, ValueTask>? beforeNavigation,
        TimeSpan? firstDocumentSceneTimeout,
        CancellationToken cancellationToken = default)
    {
        var documentStartScripts = NativeWebSceneApi.ValidateLoadOptions(options);
        var lifetime = NativeWebSceneViewLifetimeRegistry.TryGet(this);
        if (beforeNavigation is not null)
        {
            lifetime ??= NativeWebSceneViewLifetimeRegistry.GetOrCreate(this);
        }
        CancellationTokenSource? loadCancellation = null;
        if (lifetime is not null)
        {
            var (lifetimeToken, unloadToken) = lifetime.GetNavigationTokens();
            loadCancellation = NativeWebSceneViewLifecycle.CreateNavigationCancellation(
                cancellationToken,
                lifetimeToken,
                unloadToken);
        }
        using var disposeLoadCancellation = loadCancellation;

        await _lifecycleGate.WaitAsync(
                loadCancellation?.Token ?? cancellationToken)
            .ConfigureAwait(false);
        try
        {
            await UnloadCoreAsync().ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(options.CompilationCacheDirectory))
            {
                Directory.CreateDirectory(options.CompilationCacheDirectory);
            }

            if (lifetime is null)
            {
                _navigationCancellation =
                    CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            }
            else
            {
                var (lifetimeToken, unloadToken) = lifetime.GetNavigationTokens();
                _navigationCancellation =
                    NativeWebSceneViewLifecycle.CreateNavigationCancellation(
                        cancellationToken,
                        lifetimeToken,
                        unloadToken);
            }
            var navigationToken = _navigationCancellation.Token;
            await NativeWebSceneRuntime
                .PrewarmAsync(options.NativeLibraryPath, navigationToken)
                .ConfigureAwait(false);

            var resourceLoader = options.ResourceLoader
                                 ?? new AvaloniaResourceLoader();
            var callbackSignal = new JavaScriptCallbackSignal();
            var engine = NativeWebSceneApi.EngineCreate(
                0,
                options.CompilationCacheDirectory,
                resourceLoader,
                _surface.OnNativeScenePublished,
                hostRequestAvailable: OnNativeHostRequestAvailable,
                interopCallbackAvailable: callbackSignal.Notify,
                animationFrameRequested: _surface.OnNativeAnimationFrameRequested);
            if (engine == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "The WebScene native engine could not be created.");
            }

            _engine = engine;
            Volatile.Write(
                ref _contextId,
                Interlocked.Increment(ref s_nextContextId));
            _interopCallbackSignal = callbackSignal;
            _interop = new NativeInteropInvoker(engine);
            await Dispatcher.UIThread.InvokeAsync(
                () =>
                {
                    NativeWebSceneApi.EngineSetPreferredColorScheme(
                        engine,
                        ResolvePreferredColorScheme(ActualThemeVariant));
                    _surface.SetEngine(engine);
                },
                DispatcherPriority.Send);

            Source = options.Source;
            if (beforeNavigation is not null)
            {
                await beforeNavigation(this, navigationToken)
                    .ConfigureAwait(false);
            }

            NativeWebSceneApi.EngineGetMetrics(engine, out var beforeNavigationMetrics);
            if (!NativeWebSceneApi.TryLoadUrl(
                    engine,
                    options.Source,
                    documentStartScripts))
            {
                throw new InvalidOperationException(
                    $"Native WebScene rejected {options.Source}: " +
                    NativeWebSceneApi.GetLastError(engine));
            }

            using (await _interop.InvokeAsync(
                       "true",
                       "webscene-document-navigation-barrier.js",
                       navigationToken).ConfigureAwait(false))
            {
            }
            NativeWebSceneApi.EngineGetMetrics(engine, out var afterNavigation);
            if (afterNavigation.ScriptErrors > beforeNavigationMetrics.ScriptErrors)
            {
                throw new InvalidOperationException(
                    $"Native WebScene failed to load {options.Source}: " +
                    NativeWebSceneApi.GetLastError(engine));
            }
            await WaitForFirstDocumentSceneAsync(
                    engine,
                    beforeNavigationMetrics.PublishedScenes,
                    firstDocumentSceneTimeout ?? TimeSpan.FromSeconds(30),
                    navigationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            await UnloadCoreAsync().ConfigureAwait(false);
            throw;
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    private void OnNativeHostRequestAvailable()
    {
        Dispatcher.UIThread.Post(
            DrainHostRequestsAsync,
            DispatcherPriority.Normal);
    }

    private async void DrainHostRequestsAsync()
    {
        await _hostRequestGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var engine = Volatile.Read(ref _engine);
            while (engine != IntPtr.Zero
                && NativeWebSceneApi.TryTakeHostRequest(engine, out var request))
            {
                try
                {
                    await HandleHostRequestAsync(request).ConfigureAwait(true);
                }
                catch (Exception error)
                {
                    Console.Error.WriteLine($"[WebScene native host request] {error}");
                }
                if (Volatile.Read(ref _engine) != engine) break;
            }
        }
        finally
        {
            _hostRequestGate.Release();
        }
    }

    private async Task HandleHostRequestAsync(string request)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null) return;
        if (NativeHostRequest.TryGetExternalUri(request, out var externalUri))
        {
            await topLevel.Launcher.LaunchUriAsync(externalUri!).ConfigureAwait(true);
            return;
        }
        if (NativeHostRequest.TryGetClipboardWrite(request, out var clipboardWrite))
        {
            var clipboardBytes = clipboardWrite.Bytes;
            if (clipboardBytes is null && clipboardWrite.CanvasNodeId is not null)
            {
                clipboardBytes = _surface.CaptureRetainedScenePng();
            }
            if (clipboardBytes is null || topLevel.Clipboard is null) return;
            if (string.Equals(
                    clipboardWrite.ContentType,
                    "image/png",
                    StringComparison.OrdinalIgnoreCase))
            {
                var data = new DataObject();
                data.Set("image/png", clipboardBytes);
                data.Set("public.png", clipboardBytes);
                data.Set("PNG", clipboardBytes);
                await topLevel.Clipboard.SetDataObjectAsync(data).ConfigureAwait(true);
            }
            else if (clipboardWrite.ContentType.StartsWith(
                         "text/",
                         StringComparison.OrdinalIgnoreCase))
            {
                await topLevel.Clipboard.SetTextAsync(
                    System.Text.Encoding.UTF8.GetString(clipboardBytes)).ConfigureAwait(true);
            }
            else
            {
                var data = new DataObject();
                data.Set(clipboardWrite.ContentType, clipboardBytes);
                await topLevel.Clipboard.SetDataObjectAsync(data).ConfigureAwait(true);
            }
            return;
        }
        if (!NativeHostRequest.TryGetDownload(request, out var download)) return;
        var bytes = download.Bytes;
        if (bytes is null && download.CanvasNodeId is not null)
        {
            bytes = _surface.CaptureRetainedScenePng();
        }
        if (bytes is null && download.RemoteUri is not null)
        {
            bytes = await HostRequestHttpClient
                .GetByteArrayAsync(download.RemoteUri)
                .ConfigureAwait(true);
        }
        if (bytes is null) return;
        var extension = Path.GetExtension(download.SuggestedFileName);
        var file = await topLevel.StorageProvider.SaveFilePickerAsync(
            new FilePickerSaveOptions
            {
                Title = "Save download",
                SuggestedFileName = download.SuggestedFileName,
                DefaultExtension = extension.Length > 1 ? extension[1..] : null,
                ShowOverwritePrompt = true
            }).ConfigureAwait(true);
        if (file is null) return;
        await using var stream = await file.OpenWriteAsync().ConfigureAwait(true);
        stream.SetLength(0);
        await stream.WriteAsync(bytes).ConfigureAwait(true);
    }

    public async Task UnloadAsync()
    {
        var lifetime = NativeWebSceneViewLifetimeRegistry.TryGet(this);
        if (lifetime is null)
        {
            await _lifecycleGate.WaitAsync().ConfigureAwait(false);
            try
            {
                await UnloadCoreAsync().ConfigureAwait(false);
            }
            finally
            {
                _lifecycleGate.Release();
            }
            return;
        }

        Task? disposeTask;
        CancellationTokenSource? unloadCancellation;
        lock (lifetime)
        {
            disposeTask = lifetime.DisposeTask;
            if (disposeTask is null)
            {
                ++lifetime.PendingUnloadRequests;
                unloadCancellation = lifetime.UnloadCancellation;
            }
            else
            {
                unloadCancellation = null;
            }
        }
        if (disposeTask is not null)
        {
            await disposeTask.ConfigureAwait(false);
            return;
        }

        unloadCancellation!.Cancel();
        var gateAcquired = false;
        try
        {
            await _lifecycleGate.WaitAsync().ConfigureAwait(false);
            gateAcquired = true;
            await UnloadCoreAsync().ConfigureAwait(false);
        }
        finally
        {
            if (gateAcquired) _lifecycleGate.Release();
            CancellationTokenSource? completedGeneration = null;
            lock (lifetime)
            {
                --lifetime.PendingUnloadRequests;
                if (lifetime.PendingUnloadRequests == 0
                    && lifetime.DisposeTask is null
                    && ReferenceEquals(
                        lifetime.UnloadCancellation,
                        unloadCancellation))
                {
                    completedGeneration = lifetime.UnloadCancellation;
                    lifetime.UnloadCancellation = new CancellationTokenSource();
                }
            }
            completedGeneration?.Dispose();
        }
    }

    public ValueTask DisposeAsync()
    {
        var lifetime = NativeWebSceneViewLifetimeRegistry.TryGet(this);
        if (lifetime is null) return new ValueTask(UnloadAsync());
        lock (lifetime)
        {
            lifetime.DisposeTask ??= DisposeCoreAsync(lifetime);
            return new ValueTask(lifetime.DisposeTask);
        }
    }

    private async Task DisposeCoreAsync(NativeWebSceneViewLifetime lifetime)
    {
        try
        {
            await NativeWebSceneViewLifecycle.DisposeAsync(
                    lifetime.LifetimeCancellation,
                    _lifecycleGate,
                    UnloadCoreAsync)
                .ConfigureAwait(false);
        }
        finally
        {
            lifetime.LifetimeCancellation.Dispose();
            lifetime.UnloadCancellation.Dispose();
        }
    }

    private static async Task WaitForFirstDocumentSceneAsync(
        IntPtr engine,
        ulong previousSceneCount,
        TimeSpan timeoutValue,
        CancellationToken cancellationToken)
    {
        if (timeoutValue != Timeout.InfiniteTimeSpan && timeoutValue <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeoutValue));
        }
        using var timeout = timeoutValue == Timeout.InfiniteTimeSpan
            ? null
            : new CancellationTokenSource(timeoutValue);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            timeout?.Token ?? CancellationToken.None);
        try
        {
            while (true)
            {
                linked.Token.ThrowIfCancellationRequested();
                NativeWebSceneApi.EngineGetMetrics(engine, out var metrics);
                if (metrics.PublishedScenes > previousSceneCount)
                {
                    return;
                }
                await Task.Delay(16, linked.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (
            timeout?.IsCancellationRequested == true
            && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"WebScene did not publish the document's first native scene within {timeoutValue}.");
        }
    }

    private async Task UnloadCoreAsync()
    {
        _navigationCancellation?.Cancel();
        _navigationCancellation?.Dispose();
        _navigationCancellation = null;
        Source = null;

        var engine = _engine;
        var interop = Interlocked.Exchange(ref _interop, null);
        Interlocked.Exchange(ref _interopCallbackSignal, null);
        _engine = IntPtr.Zero;
        Volatile.Write(ref _contextId, 0);
        if (engine == IntPtr.Zero)
        {
            interop?.Dispose();
            return;
        }

        interop?.CancelAll();
        await Dispatcher.UIThread.InvokeAsync(
            () => _surface.SetEngine(IntPtr.Zero),
            DispatcherPriority.Send);
        try
        {
            await Task.Run(() => NativeWebSceneApi.EngineDestroy(engine)).ConfigureAwait(false);
        }
        finally
        {
            // Engine destruction joins its worker, so no native completion can
            // race the persistent pooled callback handles after this point.
            interop?.Dispose();
        }
    }
}

internal sealed class NativeWebSceneViewLifetime
{
    internal CancellationTokenSource LifetimeCancellation { get; } = new();
    internal CancellationTokenSource UnloadCancellation { get; set; } = new();
    internal int PendingUnloadRequests { get; set; }
    internal Task? DisposeTask { get; set; }

    internal (CancellationToken Lifetime, CancellationToken Unload)
        GetNavigationTokens()
    {
        lock (this)
        {
            if (DisposeTask is not null)
            {
                throw new ObjectDisposedException(nameof(NativeWebSceneView));
            }
            return (LifetimeCancellation.Token, UnloadCancellation.Token);
        }
    }
}

internal static class NativeWebSceneViewLifetimeRegistry
{
    private static ConditionalWeakTable<NativeWebSceneView, NativeWebSceneViewLifetime>?
        _lifetimes;

    internal static NativeWebSceneViewLifetime? TryGet(NativeWebSceneView view)
        => Volatile.Read(ref _lifetimes) is { } lifetimes
            && lifetimes.TryGetValue(view, out var lifetime)
                ? lifetime
                : null;

    internal static NativeWebSceneViewLifetime GetOrCreate(NativeWebSceneView view)
    {
        var lifetimes = Volatile.Read(ref _lifetimes);
        if (lifetimes is null)
        {
            var created = new ConditionalWeakTable<
                NativeWebSceneView,
                NativeWebSceneViewLifetime>();
            lifetimes = Interlocked.CompareExchange(
                ref _lifetimes,
                created,
                null) ?? created;
        }
        return lifetimes.GetValue(
            view,
            static _ => new NativeWebSceneViewLifetime());
    }
}

internal static class NativeWebSceneViewLifecycle
{
    public static CancellationTokenSource CreateNavigationCancellation(
        CancellationToken cancellationToken,
        CancellationToken lifetimeToken,
        CancellationToken unloadToken)
        => CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            lifetimeToken,
            unloadToken);

    public static async ValueTask DisposeAsync(
        CancellationTokenSource lifetimeCancellation,
        SemaphoreSlim lifecycleGate,
        Func<Task> unloadAsync)
    {
        lifetimeCancellation.Cancel();
        await lifecycleGate.WaitAsync().ConfigureAwait(false);
        try
        {
            await unloadAsync().ConfigureAwait(false);
        }
        finally
        {
            lifecycleGate.Release();
        }
    }
}

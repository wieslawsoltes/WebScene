using Avalonia.Controls;
using Avalonia.Styling;
using Avalonia.Threading;
using JavaScript.Avalonia;
using WebScene.JavaScript.Interop;

namespace WebScene.Backends.Avalonia.Native;

/// <summary>
/// Hosts an arbitrary HTML document in WebScene's ABI 3 native DOM/runtime.
/// The native engine owns navigation, DOM, JavaScript, CSS, layout, and scene
/// production; the attached Avalonia surface projects those scenes with Skia.
/// </summary>
public sealed class NativeWebSceneView : ContentControl, IAsyncDisposable
{
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly NativeSceneSurface _surface;
    private IntPtr _engine;
    private NativeInteropInvoker? _interop;
    private JavaScriptCallbackSignal? _interopCallbackSignal;
    private CancellationTokenSource? _navigationCancellation;

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

    public async Task LoadAsync(
        string source,
        string nativeLibraryPath,
        string? compilationCacheDirectory = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        if (!Uri.TryCreate(source, UriKind.Absolute, out _))
        {
            throw new ArgumentException(
                "The WebScene document source must be an absolute URI.",
                nameof(source));
        }

        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await UnloadCoreAsync().ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(compilationCacheDirectory))
            {
                Directory.CreateDirectory(compilationCacheDirectory);
            }

            _navigationCancellation =
                CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            var navigationToken = _navigationCancellation.Token;
            await NativeWebSceneRuntime
                .PrewarmAsync(nativeLibraryPath, navigationToken)
                .ConfigureAwait(false);

            var resourceLoader = new AvaloniaResourceLoader();
            var callbackSignal = new JavaScriptCallbackSignal();
            var engine = NativeWebSceneApi.EngineCreate(
                0,
                compilationCacheDirectory,
                resourceLoader,
                _surface.OnNativeScenePublished,
                interopCallbackAvailable: callbackSignal.Notify);
            if (engine == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "The WebScene native engine could not be created.");
            }

            _engine = engine;
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

            NativeWebSceneApi.EngineGetMetrics(engine, out var beforeNavigation);
            if (!NativeWebSceneApi.TryLoadUrl(engine, source))
            {
                throw new InvalidOperationException(
                    $"Native WebScene rejected {source}: {NativeWebSceneApi.GetLastError(engine)}");
            }

            await WaitForFirstDocumentSceneAsync(
                    engine,
                    beforeNavigation.PublishedScenes,
                    navigationToken)
                .ConfigureAwait(false);
            Source = source;
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

    public async Task UnloadAsync()
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
    }

    public ValueTask DisposeAsync() => new(UnloadAsync());

    private static async Task WaitForFirstDocumentSceneAsync(
        IntPtr engine,
        ulong previousSceneCount,
        CancellationToken cancellationToken)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            timeout.Token);
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
            timeout.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                "WebScene did not publish the document's first native scene within 30 seconds.");
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

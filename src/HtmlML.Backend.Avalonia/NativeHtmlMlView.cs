using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia;

namespace HtmlML.Backends.Avalonia.Native;

/// <summary>
/// Hosts an arbitrary HTML document in HtmlML's ABI 2 native DOM/runtime.
/// The native engine owns navigation, DOM, JavaScript, CSS, layout, and scene
/// production; the attached Avalonia surface projects those scenes with Skia.
/// </summary>
public sealed class NativeHtmlMlView : ContentControl, IAsyncDisposable
{
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly NativeSceneSurface _surface;
    private IntPtr _engine;
    private CancellationTokenSource? _navigationCancellation;

    public NativeHtmlMlView()
        : this(useCompositionVisual: true)
    {
    }

    public NativeHtmlMlView(bool useCompositionVisual)
    {
        _surface = new NativeSceneSurface(IntPtr.Zero, useCompositionVisual);
        Content = _surface;
    }

    public string? Source { get; private set; }

    public INativeHtmlMlRenderDiagnostics RenderDiagnostics => _surface;

    public Task<string> EvaluateJsonAsync(
        string source,
        string documentName = "native-host-evaluation.js",
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(documentName);
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native HtmlML document is not loaded.");
        }

        return Task.Run(() =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!NativeHtmlMlApi.TryEvaluateJson(
                    engine,
                    source,
                    documentName,
                    out var json))
            {
                throw new InvalidOperationException(
                    $"Native HtmlML evaluation failed: "
                    + NativeHtmlMlApi.GetLastError(engine));
            }
            return json;
        }, cancellationToken);
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
                "The HtmlML document source must be an absolute URI.",
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
            await NativeHtmlMlRuntime
                .PrewarmAsync(nativeLibraryPath, navigationToken)
                .ConfigureAwait(false);

            var resourceLoader = new AvaloniaResourceLoader();
            var engine = NativeHtmlMlApi.EngineCreate(
                0,
                compilationCacheDirectory,
                resourceLoader,
                _surface.OnNativeScenePublished);
            if (engine == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "The HtmlML native engine could not be created.");
            }

            _engine = engine;
            await Dispatcher.UIThread.InvokeAsync(
                () => _surface.SetEngine(engine),
                DispatcherPriority.Send);

            NativeHtmlMlApi.EngineGetMetrics(engine, out var beforeNavigation);
            if (!NativeHtmlMlApi.TryLoadUrl(engine, source))
            {
                throw new InvalidOperationException(
                    $"Native HtmlML rejected {source}: {NativeHtmlMlApi.GetLastError(engine)}");
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
                NativeHtmlMlApi.EngineGetMetrics(engine, out var metrics);
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
                "HtmlML did not publish the document's first native scene within 30 seconds.");
        }
    }

    private async Task UnloadCoreAsync()
    {
        _navigationCancellation?.Cancel();
        _navigationCancellation?.Dispose();
        _navigationCancellation = null;
        Source = null;

        var engine = _engine;
        _engine = IntPtr.Zero;
        if (engine == IntPtr.Zero)
        {
            return;
        }

        await Dispatcher.UIThread.InvokeAsync(
            () => _surface.SetEngine(IntPtr.Zero),
            DispatcherPriority.Send);
        await Task.Run(() => NativeHtmlMlApi.EngineDestroy(engine)).ConfigureAwait(false);
    }
}

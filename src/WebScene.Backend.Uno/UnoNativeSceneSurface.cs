using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Uno.WinUI.Graphics2DSK;
using SkiaSharp;
using WebScene.Core;
using WebScene.JavaScript.Interop;
using Windows.System;
using Windows.UI.Core;

namespace WebScene.Backends.Uno.Native;

/// <summary>
/// Uno's compositor-side Skia presenter for the ABI 2 immutable native scene.
/// The native engine publishes on its worker thread; this control only coalesces
/// a redraw and consumes at most one ordered diff in each Skia render callback.
/// </summary>
public sealed unsafe class UnoNativeSceneSurface : SKCanvasElement, INativeWebSceneRenderDiagnostics
{
    private readonly NativeCanvasSceneRenderer _renderer = new();
    private readonly NativeSceneRenderObserver _renderObserver = new();
    private readonly NativeScenePublicationMailbox _publicationMailbox = new();
    private readonly object _publicationGate = new();
    private readonly Queue<NativeScenePublicationSample> _publishedScenes = new(4096);
    private IntPtr _engine;
    private float _viewportWidth;
    private float _viewportHeight;
    private long _sequence = DateTime.UtcNow.Ticks;
    private long _renderCallbackCount;
    private long _routedInputEvents;
    private long _acceptedInputEvents;
    private float _lastRenderWidth;
    private float _lastRenderHeight;
    private double _lastPointerX;
    private double _lastPointerY;
    private bool _pointerDown;
    private ulong _appliedRevision;

    public UnoNativeSceneSurface()
    {
        IsTabStop = true;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
        SizeChanged += OnSizeChanged;
        PointerMoved += OnPointerMoved;
        PointerPressed += OnPointerPressed;
        PointerReleased += OnPointerReleased;
        PointerCanceled += OnPointerCanceled;
        PointerCaptureLost += OnPointerCaptureLost;
        PointerWheelChanged += OnPointerWheelChanged;
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
    }

    public long RenderedSceneCount => _renderObserver.RenderedSceneCount;

    public long RenderCallbackCount => Interlocked.Read(ref _renderCallbackCount);

    public long RoutedInputEvents => Interlocked.Read(ref _routedInputEvents);

    public long AcceptedInputEvents => Interlocked.Read(ref _acceptedInputEvents);

    public (float Width, float Height) LastRenderArea =>
        (_lastRenderWidth, _lastRenderHeight);

    public long FirstRenderedSceneTimestamp => _renderObserver.FirstRenderedSceneTimestamp;

    public long FirstReadySceneTimestamp => _renderObserver.FirstReadySceneTimestamp;

    public ulong PublishedSceneCount
        => _engine == IntPtr.Zero
            ? 0
            : GetPublishedSceneCount(_engine);

    public EngineMetrics EngineMetrics
    {
        get
        {
            if (_engine == IntPtr.Zero)
            {
                return default;
            }

            NativeWebSceneApi.EngineGetMetrics(_engine, out var metrics);
            return metrics;
        }
    }

    public ulong LastResizeSequence { get; private set; }

    public NativeSceneRenderSample[] RenderedScenes => _renderObserver.RenderedScenes;

    public NativeScenePublicationSample[] PublishedScenes
    {
        get
        {
            lock (_publicationGate)
            {
                return _publishedScenes.ToArray();
            }
        }
    }

    public void SetEngine(IntPtr engine)
    {
        if (_engine == engine)
        {
            return;
        }

        if (_engine != IntPtr.Zero)
        {
            NativeWebSceneApi.EngineSetVisible(_engine, 0);
        }
        _renderer.Reset();
        _publicationMailbox.Reset();
        _viewportWidth = 0;
        _viewportHeight = 0;
        _appliedRevision = 0;
        _engine = engine;

        if (_engine == IntPtr.Zero)
        {
            return;
        }

        NativeWebSceneApi.EngineSetVisible(_engine, IsLoaded ? (byte)1 : (byte)0);
        NativeWebSceneApi.EngineRequestSceneCheckpoint(_engine);
        SubmitResize(ActualWidth, ActualHeight);
    }

    public void OnNativeScenePublished(NativeScenePublished scene)
    {
        _publicationMailbox.Publish();
        lock (_publicationGate)
        {
            if (_publishedScenes.Count == 4096)
            {
                _publishedScenes.Dequeue();
            }
            _publishedScenes.Enqueue(new NativeScenePublicationSample(
                Stopwatch.GetTimestamp(),
                scene.Revision,
                scene.ConsumedInputSequence,
                scene.ViewportWidth,
                scene.ViewportHeight));
        }

        // Publication is worker-thread-only. The CompositionTarget frame driver
        // observes this mailbox and wakes the SKCanvasElement visual; no
        // publication posts work through the UI dispatcher.
    }

    public void SubmitAnimationFrame(double timestampMilliseconds)
        => NativeFrameInput.Submit(_engine, timestampMilliseconds);

    public void RequestRender()
    {
        // The composition clock is the sole render scheduler while attached.
    }

    public ulong SubmitResize(double width, double height)
    {
        var sequence = unchecked((ulong)Interlocked.Increment(ref _sequence));
        var input = new InputEvent
        {
            Kind = 6,
            Sequence = sequence,
            X = Math.Max(0, width),
            Y = Math.Max(0, height)
        };
        LastResizeSequence = NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0
            ? sequence
            : 0;
        return LastResizeSequence;
    }

    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        if (!IsSupportedOnCurrentPlatform())
        {
            throw new PlatformNotSupportedException(
                "WebScene's Uno scene backend requires Uno's Skia renderer.");
        }
    }

    protected override void RenderOverride(SKCanvas canvas, Windows.Foundation.Size area)
    {
        Interlocked.Increment(ref _renderCallbackCount);
        var width = (float)Math.Max(0, area.Width);
        var height = (float)Math.Max(0, area.Height);
        _lastRenderWidth = width;
        _lastRenderHeight = height;
        canvas.Clear(new SKColor(19, 23, 34, 255));
        if (_engine == IntPtr.Zero || width <= 0 || height <= 0)
        {
            return;
        }

        var scene = NativeWebSceneApi.EngineAcquireNextScene(_engine);
        if (scene != IntPtr.Zero)
        {
            _publicationMailbox.TryConsume();
            var view = (NativeSceneView*)scene;
            if (ValidateView(view) && view->Header.Revision > _appliedRevision)
            {
                var header = view->Header;
                if (_renderer.ApplyDiff(view))
                {
                    _viewportWidth = header.ViewportWidth;
                    _viewportHeight = header.ViewportHeight;
                    _appliedRevision = header.Revision;
                    NativeWebSceneApi.SceneAcknowledge(scene);
                    _renderObserver.RecordRendered(header);
                }
            }
            NativeWebSceneApi.SceneRelease(scene);
        }

        if (_viewportWidth <= 0 || _viewportHeight <= 0)
        {
            return;
        }

        var scale = NativeSceneResizeProjection.GetScale(
            width,
            height,
            _viewportWidth,
            _viewportHeight);
        var save = canvas.Save();
        try
        {
            canvas.Scale(scale.X, scale.Y);
            _renderer.RenderRetained(canvas, _viewportWidth, _viewportHeight, null);
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs args)
    {
        EnqueuePointer(1, args);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        _pointerDown = true;
        Focus(FocusState.Pointer);
        CapturePointer(args.Pointer);
        EnqueuePointer(2, args);
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs args)
    {
        _pointerDown = false;
        EnqueuePointer(3, args);
        ReleasePointerCapture(args.Pointer);
    }

    private void OnPointerCanceled(object sender, PointerRoutedEventArgs args)
    {
        SubmitPointerCaptureInterruption();
        ReleasePointerCapture(args.Pointer);
        args.Handled = true;
    }

    private void OnPointerCaptureLost(object sender, PointerRoutedEventArgs args)
    {
        SubmitPointerCaptureInterruption();
    }

    private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs args)
    {
        var point = args.GetCurrentPoint(this);
        var delta = -point.Properties.MouseWheelDelta / 120.0 * 100.0;
        var horizontal = point.Properties.IsHorizontalMouseWheel;
        var input = new InputEvent
        {
            Kind = 4,
            Flags = EncodeModifiers() << 16,
            Sequence = NextSequence(),
            X = point.Position.X,
            Y = point.Position.Y,
            DeltaX = horizontal ? delta : 0,
            DeltaY = horizontal ? 0 : delta
        };
        EnqueueInput(in input);
        args.Handled = true;
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (EnqueueKey(7, args)
            && TryGetTextCharacter(args.Key, out var character))
        {
            var input = new InputEvent
            {
                Kind = 9,
                Sequence = NextSequence(),
                X = character
            };
            EnqueueInput(in input);
        }
    }

    private void OnKeyUp(object sender, KeyRoutedEventArgs args)
    {
        EnqueueKey(8, args);
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        CompositionTarget.Rendering += OnCompositionFrame;
        if (_engine != IntPtr.Zero)
        {
            NativeWebSceneApi.EngineSetVisible(_engine, 1);
            SubmitResize(ActualWidth, ActualHeight);
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        CompositionTarget.Rendering -= OnCompositionFrame;
        if (_engine != IntPtr.Zero)
        {
            NativeWebSceneApi.EngineSetVisible(_engine, 0);
        }
    }

    private void OnSizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (_engine != IntPtr.Zero)
        {
            SubmitResize(args.NewSize.Width, args.NewSize.Height);
        }
    }

    private void OnCompositionFrame(object? sender, object args)
    {
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            return;
        }

        NativeFrameInput.Submit(
            engine,
            Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);
        if (_publicationMailbox.PendingCount > 0)
        {
            Invalidate();
        }
    }

    private ulong NextSequence()
        => unchecked((ulong)Interlocked.Increment(ref _sequence));

    private void EnqueuePointer(uint kind, PointerRoutedEventArgs args)
    {
        var point = args.GetCurrentPoint(this);
        var properties = point.Properties;
        var button = properties.PointerUpdateKind switch
        {
            PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 0,
            PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 1,
            PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 2,
            _ => -1
        };
        var buttons = (properties.IsLeftButtonPressed ? 1U : 0U)
            | (properties.IsRightButtonPressed ? 2U : 0U)
            | (properties.IsMiddleButtonPressed ? 4U : 0U);
        var input = new InputEvent
        {
            Kind = kind,
            Flags = buttons
                | (button >= 0 ? (uint)(button + 1) << 8 : 0U)
                | (EncodeModifiers() << 16),
            Sequence = NextSequence(),
            X = point.Position.X,
            Y = point.Position.Y
        };
        _lastPointerX = input.X;
        _lastPointerY = input.Y;
        EnqueueInput(in input);
        args.Handled = true;
    }

    private bool EnqueueKey(uint kind, KeyRoutedEventArgs args)
    {
        var input = new InputEvent
        {
            Kind = kind,
            Flags = EncodeModifiers(),
            Sequence = NextSequence(),
            X = (int)args.Key
        };
        args.Handled = EnqueueInput(in input);
        return args.Handled;
    }

    private ulong SubmitPointerCaptureInterruption()
    {
        if (!_pointerDown)
        {
            return 0;
        }

        _pointerDown = false;
        var input = new InputEvent
        {
            Kind = 3,
            Sequence = NextSequence(),
            X = _lastPointerX,
            Y = _lastPointerY
        };
        return EnqueueInput(in input) ? input.Sequence : 0;
    }

    private bool EnqueueInput(in InputEvent input)
    {
        Interlocked.Increment(ref _routedInputEvents);
        if (_engine == IntPtr.Zero
            || NativeWebSceneApi.EngineEnqueue(_engine, in input) == 0)
        {
            return false;
        }

        Interlocked.Increment(ref _acceptedInputEvents);
        return true;
    }

    private static uint EncodeModifiers()
        => (IsVirtualKeyDown(VirtualKey.Shift) ? 1U : 0U)
            | (IsVirtualKeyDown(VirtualKey.Control) ? 2U : 0U)
            | (IsVirtualKeyDown(VirtualKey.Menu) ? 4U : 0U)
            | (IsVirtualKeyDown(VirtualKey.LeftWindows)
                || IsVirtualKeyDown(VirtualKey.RightWindows)
                ? 8U
                : 0U);

    private static bool IsVirtualKeyDown(VirtualKey key)
        => (InputKeyboardSource.GetKeyStateForCurrentThread(key)
            & CoreVirtualKeyStates.Down) != 0;

    private static bool TryGetTextCharacter(
        VirtualKey key,
        out char character)
    {
        character = default;
        if (IsVirtualKeyDown(VirtualKey.Control)
            || IsVirtualKeyDown(VirtualKey.Menu)
            || IsVirtualKeyDown(VirtualKey.LeftWindows)
            || IsVirtualKeyDown(VirtualKey.RightWindows))
        {
            return false;
        }

        var value = (int)key;
        var shift = IsVirtualKeyDown(VirtualKey.Shift);
        if (value is >= 'A' and <= 'Z')
        {
            character = shift
                ? (char)value
                : char.ToLowerInvariant((char)value);
            return true;
        }
        if (value is >= '0' and <= '9')
        {
            character = shift
                ? ")!@#$%^&*("[value - '0']
                : (char)value;
            return true;
        }
        if (value is >= 96 and <= 105)
        {
            character = (char)('0' + value - 96);
            return true;
        }

        character = value switch
        {
            32 => ' ',
            106 => '*',
            107 => '+',
            109 => '-',
            110 => '.',
            111 => '/',
            186 => shift ? ':' : ';',
            187 => shift ? '+' : '=',
            188 => shift ? '<' : ',',
            189 => shift ? '_' : '-',
            190 => shift ? '>' : '.',
            191 => shift ? '?' : '/',
            192 => shift ? '~' : '`',
            219 => shift ? '{' : '[',
            220 => shift ? '|' : '\\',
            221 => shift ? '}' : ']',
            222 => shift ? '"' : '\'',
            _ => default
        };
        return character != default;
    }

    private static bool ValidateView(NativeSceneView* view)
        => view != null
            && view->StructSize == sizeof(NativeSceneView)
            && view->AbiVersion == 2
            && (view->Header.CommandCount == 0 || view->Commands != null)
            && (view->Header.CanvasLayerCount == 0
                || (view->CanvasLayers != null && view->CanvasCommands != null))
            && (view->StringCount == 0
                || (view->Strings != null && view->StringBytes != null));

    private static ulong GetPublishedSceneCount(IntPtr engine)
    {
        NativeWebSceneApi.EngineGetMetrics(engine, out var metrics);
        return metrics.PublishedScenes;
    }
}

internal static class NativeSceneDrawOperation
{
    public static int RectCommandCount;
    public static int LineCommandCount;
    public static int TextCommandCount;
    public static int SvgCommandCount;
}

public sealed class UnoNativeWebSceneView : ContentControl, IAsyncDisposable
{
    private readonly UnoNativeSceneSurface _surface = new();
    private IntPtr _engine;
    private NativeInteropInvoker? _interop;
    private JavaScriptCallbackSignal? _interopCallbackSignal;

    public UnoNativeWebSceneView()
    {
        HorizontalContentAlignment = HorizontalAlignment.Stretch;
        VerticalContentAlignment = VerticalAlignment.Stretch;
        Content = _surface;
    }

    public string? Source { get; private set; }

    public INativeWebSceneRenderDiagnostics RenderDiagnostics => _surface;

    public EngineMetrics EngineMetrics => _surface.EngineMetrics;

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

    public bool TryTakeHostRequest(out string request)
    {
        if (_engine == IntPtr.Zero)
        {
            request = string.Empty;
            return false;
        }

        return NativeWebSceneApi.TryTakeHostRequest(_engine, out request);
    }

    public async Task LoadAsync(
        string source,
        string nativeLibraryPath,
        string? compilationCacheDirectory = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(nativeLibraryPath);
        await NativeWebSceneRuntime.PrewarmAsync(nativeLibraryPath, cancellationToken);
        if (!string.IsNullOrWhiteSpace(compilationCacheDirectory))
        {
            Directory.CreateDirectory(compilationCacheDirectory);
        }

        var callbackSignal = new JavaScriptCallbackSignal();
        _engine = NativeWebSceneApi.EngineCreate(
            0,
            compilationCacheDirectory,
            new UnoResourceLoader(),
            _surface.OnNativeScenePublished,
            interopCallbackAvailable: callbackSignal.Notify);
        if (_engine == IntPtr.Zero)
        {
            throw new InvalidOperationException("The WebScene native engine could not be created.");
        }
        _interopCallbackSignal = callbackSignal;
        _interop = new NativeInteropInvoker(_engine);

        _surface.SetEngine(_engine);
        if (!NativeWebSceneApi.TryLoadUrl(_engine, source))
        {
            throw new InvalidOperationException(
                $"Native WebScene rejected {source}: {NativeWebSceneApi.GetLastError(_engine)}");
        }

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        var documentBarrierText = await EvaluateTextAsync(
                "({ hasDocumentElement: !!document.documentElement, hasBody: !!document.body })",
                "webscene-uno-document-barrier.js",
                timeout.Token).ConfigureAwait(false);
        var documentBarrier =
            documentBarrierText.Contains(
                "hasDocumentElement",
                StringComparison.Ordinal)
            && documentBarrierText.Contains(
                "hasBody",
                StringComparison.Ordinal);
        if (!documentBarrier)
        {
            throw new InvalidOperationException(
                $"Native WebScene did not construct a document for {source}: " +
                NativeWebSceneApi.GetLastError(_engine));
        }

        Source = source;
    }

    public ValueTask DisposeAsync()
    {
        _surface.SetEngine(IntPtr.Zero);
        Interlocked.Exchange(ref _interop, null)?.Dispose();
        Interlocked.Exchange(ref _interopCallbackSignal, null);
        if (_engine != IntPtr.Zero)
        {
            NativeWebSceneApi.EngineDestroy(_engine);
            _engine = IntPtr.Zero;
        }
        return ValueTask.CompletedTask;
    }
}

internal sealed class UnoResourceLoader : IWebSceneResourceLoader
{
    private static readonly HttpClient Client = CreateHttpClient();

    public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
    {
        var address = Resolve(request.Specifier, request.BaseAddress);
        var uri = new Uri(address);
        if (uri.IsFile)
        {
            return new WebSceneTextResource(
                address,
                File.ReadAllText(uri.LocalPath),
                address,
                null)
            {
                LastModified = File.GetLastWriteTimeUtc(uri.LocalPath),
                IsCacheable = true
            };
        }
        if (uri.Scheme.Equals("data", StringComparison.OrdinalIgnoreCase))
        {
            var separator = address.IndexOf(',');
            if (separator < 0)
            {
                throw new InvalidDataException("Malformed data URI.");
            }
            var metadata = address[..separator];
            var payload = address[(separator + 1)..];
            var dataContent = metadata.EndsWith(
                    ";base64",
                    StringComparison.OrdinalIgnoreCase)
                ? System.Text.Encoding.UTF8.GetString(
                    Convert.FromBase64String(payload))
                : Uri.UnescapeDataString(payload);
            return new WebSceneTextResource(address, dataContent, address, null);
        }
        if (uri.Scheme is not ("http" or "https"))
        {
            throw new NotSupportedException(
                $"Unsupported WebScene resource scheme '{uri.Scheme}'.");
        }

        using var message = new HttpRequestMessage(HttpMethod.Get, uri)
        {
            Version = HttpVersion.Version20,
            VersionPolicy = HttpVersionPolicy.RequestVersionOrLower
        };
        if (!string.IsNullOrWhiteSpace(request.IfNoneMatch)
            && EntityTagHeaderValue.TryParse(
                request.IfNoneMatch,
                out var entityTag))
        {
            message.Headers.IfNoneMatch.Add(entityTag);
        }
        if (request.IfModifiedSince is { } modifiedSince)
        {
            message.Headers.IfModifiedSince = modifiedSince;
        }

        using var response = Client.SendAsync(
                message,
                HttpCompletionOption.ResponseHeadersRead)
            .GetAwaiter()
            .GetResult();
        var responseEntityTag =
            response.Headers.ETag?.ToString() ?? request.IfNoneMatch;
        var responseLastModified =
            response.Content.Headers.LastModified ?? request.IfModifiedSince;
        var cachePolicy = ReadHttpCachePolicy(
            response,
            responseLastModified);
        if (response.StatusCode == HttpStatusCode.NotModified)
        {
            return new WebSceneTextResource(
                address,
                string.Empty,
                address,
                null)
            {
                EntityTag = responseEntityTag,
                LastModified = responseLastModified,
                FreshUntil = cachePolicy.FreshUntil,
                IsCacheable = cachePolicy.IsCacheable,
                NotModified = true
            };
        }

        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException(
                $"WebScene resource request for '{address}' returned "
                + $"{(int)response.StatusCode} ({response.ReasonPhrase}).",
                inner: null,
                response.StatusCode);
        }
        var content = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
        return new WebSceneTextResource(address, content, address, null)
        {
            EntityTag = responseEntityTag,
            LastModified = responseLastModified,
            FreshUntil = cachePolicy.FreshUntil,
            IsCacheable = cachePolicy.IsCacheable
        };
    }

    internal static (DateTimeOffset? FreshUntil, bool IsCacheable)
        ReadHttpCachePolicy(
            HttpResponseMessage response,
            DateTimeOffset? lastModified)
    {
        var cacheControl = response.Headers.CacheControl;
        if (cacheControl?.NoStore == true)
        {
            return (null, false);
        }
        if (cacheControl?.NoCache == true)
        {
            return (null, true);
        }

        var receivedAt = DateTimeOffset.UtcNow;
        var responseDate = response.Headers.Date ?? receivedAt;
        var responseAge = response.Headers.Age ?? TimeSpan.Zero;
        var apparentAge =
            receivedAt > responseDate ? receivedAt - responseDate : TimeSpan.Zero;
        var currentAge = responseAge > apparentAge ? responseAge : apparentAge;
        TimeSpan? freshnessLifetime = cacheControl?.MaxAge;
        if (freshnessLifetime is null
            && response.Content.Headers.Expires is { } expires)
        {
            freshnessLifetime = expires - responseDate;
        }
        if (freshnessLifetime is null
            && lastModified is { } modified
            && responseDate > modified)
        {
            var heuristic =
                TimeSpan.FromTicks((responseDate - modified).Ticks / 10);
            freshnessLifetime = TimeSpan.FromTicks(Math.Clamp(
                heuristic.Ticks,
                TimeSpan.FromMinutes(1).Ticks,
                TimeSpan.FromHours(1).Ticks));
        }
        if (freshnessLifetime is not { } lifetime || lifetime <= currentAge)
        {
            return (null, true);
        }
        return (receivedAt + lifetime - currentAge, true);
    }

    private static HttpClient CreateHttpClient()
    {
        var client = new HttpClient();
        client.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("WebScene-Uno", "0.1"));
        return client;
    }

    private static string Resolve(string specifier, string? baseAddress)
    {
        if (Uri.TryCreate(specifier, UriKind.Absolute, out var absolute))
        {
            return absolute.AbsoluteUri;
        }
        if (Uri.TryCreate(baseAddress, UriKind.Absolute, out var baseUri)
            && Uri.TryCreate(baseUri, specifier, out var relative))
        {
            return relative.AbsoluteUri;
        }
        throw new InvalidOperationException($"Unable to resolve WebScene resource '{specifier}'.");
    }
}

namespace WebScene.Core;

public enum WebSceneDispatchPriority
{
    Send,
    Input,
    Default,
    Render,
    Background
}

public interface IWebSceneScheduledWork : IDisposable
{
    bool IsCancellationRequested { get; }

    void Cancel();
}

public interface IWebSceneDispatcher
{
    bool CheckAccess();

    void VerifyAccess();

    void Post(Action callback, WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default);

    IWebSceneScheduledWork Schedule(
        TimeSpan delay,
        Action callback,
        WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default);
}

public interface IWebSceneClock
{
    TimeSpan Elapsed { get; }
}

public readonly record struct WebSceneFrameRequest(long Value)
{
    public bool IsEmpty => Value == 0;
}

public interface IWebSceneFrameScheduler
{
    WebSceneFrameRequest RequestFrame(Action<TimeSpan> callback);

    bool CancelFrame(WebSceneFrameRequest request);
}

public readonly record struct WebSceneViewportMetrics(
    WebSceneSize ClientSize,
    double DeviceScaleFactor,
    bool IsVisible)
{
    public static WebSceneViewportMetrics Empty { get; } = new(WebSceneSize.Empty, 1, false);
}

public sealed class WebSceneViewportChangedEventArgs : EventArgs
{
    public WebSceneViewportChangedEventArgs(WebSceneViewportMetrics previous, WebSceneViewportMetrics current)
    {
        Previous = previous;
        Current = current;
    }

    public WebSceneViewportMetrics Previous { get; }

    public WebSceneViewportMetrics Current { get; }
}

public interface IWebSceneViewport
{
    WebSceneViewportMetrics HostMetrics { get; }

    WebSceneViewportMetrics Metrics { get; }

    event EventHandler<WebSceneViewportChangedEventArgs>? Changed;
}

public enum WebSceneResourceKind
{
    Script,
    StyleSheet,
    Markup,
    Image,
    Font,
    Data
}

public enum WebSceneResourceInitiator
{
    Navigation,
    Subresource,
    Fetch
}

public enum WebSceneFetchMode
{
    None,
    SameOrigin,
    Cors,
    NoCors
}

public enum WebSceneRequestDestination
{
    None,
    Document,
    Script,
    Style,
    Image,
    Font
}

public readonly record struct WebSceneRequestContext(
    WebSceneResourceInitiator Initiator,
    string? Origin,
    string? Referrer,
    WebSceneFetchMode Mode,
    WebSceneRequestDestination Destination);

public readonly record struct WebSceneResourceRequest(
    string Specifier,
    string? BaseAddress,
    WebSceneResourceKind Kind)
{
    public WebSceneRequestContext Context { get; init; }

    public string? IfNoneMatch { get; init; }

    public DateTimeOffset? IfModifiedSince { get; init; }
}

public readonly record struct WebSceneTextResource(
    string CacheKey,
    string Content,
    string DisplayName,
    string? Directory)
{
    public string? EntityTag { get; init; }

    public DateTimeOffset? LastModified { get; init; }

    /// <summary>
    /// The HTTP freshness boundary for this representation. A persistent cache
    /// may reuse the content without contacting the origin before this instant;
    /// after it, validators such as <see cref="EntityTag"/> must be used.
    /// </summary>
    public DateTimeOffset? FreshUntil { get; init; }

    /// <summary>
    /// Whether the representation may be stored. HTTP <c>no-store</c>
    /// responses set this to <see langword="false"/>.
    /// </summary>
    public bool IsCacheable { get; init; } = true;

    public bool NotModified { get; init; }
}

public interface IWebSceneResourceLoader
{
    WebSceneTextResource LoadText(in WebSceneResourceRequest request);
}

public interface IWebSceneClipboard
{
    string? GetText();

    void SetText(string? text);

    byte[]? GetData(string format) => null;

    void SetData(string format, ReadOnlyMemory<byte> data)
    {
    }
}

public interface IWebSceneHostServices
{
    WebSceneBackendHandle RootHandle { get; }

    IWebSceneDispatcher Dispatcher { get; }

    IWebSceneClock Clock { get; }

    IWebSceneFrameScheduler Frames { get; }

    IWebSceneViewport Viewport { get; }

    IWebSceneResourceLoader Resources { get; }

    IWebSceneClipboard Clipboard { get; }

    IWebSceneInputSource Input { get; }
}

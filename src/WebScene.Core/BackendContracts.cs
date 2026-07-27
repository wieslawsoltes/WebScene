namespace WebScene.Core;

[Flags]
public enum WebSceneBackendCapabilities : ulong
{
    None = 0,
    DomProjection = 1UL << 0,
    CssLayout = 1UL << 1,
    Canvas2D = 1UL << 2,
    Svg = 1UL << 3,
    Images = 1UL << 4,
    PointerInput = 1UL << 5,
    KeyboardInput = 1UL << 6,
    TextInput = 1UL << 7,
    Focus = 1UL << 8,
    Clipboard = 1UL << 9,
    Accessibility = 1UL << 10,
    DragDrop = 1UL << 11,
    InputMethodEditor = 1UL << 12,
    OpenGl = 1UL << 13,
    WebGpu = 1UL << 14
}

public enum WebSceneBackendState
{
    Created,
    Mounted,
    Unmounted,
    Disposed
}

public enum WebSceneBackendNodeKind
{
    Container,
    Text,
    Image,
    Canvas,
    Svg,
    NativeControl
}

[Flags]
public enum WebSceneInvalidationKind
{
    None = 0,
    Style = 1 << 0,
    Measure = 1 << 1,
    Arrange = 1 << 2,
    Render = 1 << 3,
    HitTest = 1 << 4,
    Accessibility = 1 << 5
}

public readonly record struct WebSceneBackendNodeDescriptor(
    WebSceneNodeId Id,
    WebSceneBackendNodeKind Kind,
    string SemanticName);

public readonly record struct WebSceneBackendDiagnostic(
    string Category,
    string Message,
    WebSceneNodeId NodeId,
    DateTimeOffset Timestamp);

public sealed class WebSceneBackendCapabilityException : NotSupportedException
{
    public WebSceneBackendCapabilityException(
        WebSceneBackendCapabilities required,
        WebSceneBackendCapabilities available)
        : base($"Backend is missing required capabilities '{required & ~available}'. Available capabilities: '{available}'.")
    {
        Required = required;
        Available = available;
        Missing = required & ~available;
    }

    public WebSceneBackendCapabilities Required { get; }

    public WebSceneBackendCapabilities Available { get; }

    public WebSceneBackendCapabilities Missing { get; }
}

public interface IWebSceneBackendHost : IDisposable
{
    WebSceneBackendState State { get; }

    WebSceneBackendNode Root { get; }

    WebSceneBackendCapabilities Capabilities { get; }

    IWebSceneHostServices Services { get; }

    IWebSceneInputSource Input { get; }

    IReadOnlyList<WebSceneBackendDiagnostic> Diagnostics { get; }

    void EnsureCapabilities(WebSceneBackendCapabilities required);

    void Mount();

    void Unmount();

    WebSceneBackendNode CreateNode(in WebSceneBackendNodeDescriptor descriptor);

    void Attach(WebSceneBackendNode parent, WebSceneBackendNode child, int index);

    void Detach(WebSceneBackendNode node);

    void Arrange(WebSceneBackendNode node, WebSceneRect bounds);

    void SetVisible(WebSceneBackendNode node, bool visible);

    void SetZIndex(WebSceneBackendNode node, int zIndex);

    void Invalidate(WebSceneBackendNode node, WebSceneInvalidationKind kind);

    WebSceneBackendNode? HitTest(WebScenePoint point);
}

/// <summary>
/// Enforces the backend lifetime contract while leaving native object creation and
/// presentation to an adapter. Calls are serialized by the adapter's dispatcher.
/// </summary>
public abstract class WebSceneBackendHostBase : IWebSceneBackendHost
{
    private bool _disposed;

    protected WebSceneBackendHostBase(
        IWebSceneHostServices services,
        IWebSceneInputSource input,
        WebSceneBackendCapabilities capabilities)
    {
        Services = services ?? throw new ArgumentNullException(nameof(services));
        Input = input ?? throw new ArgumentNullException(nameof(input));
        Capabilities = capabilities;
        Root = new WebSceneBackendNode(new WebSceneNodeId(long.MinValue), services.RootHandle);
    }

    public WebSceneBackendState State { get; private set; } = WebSceneBackendState.Created;

    public WebSceneBackendNode Root { get; }

    public WebSceneBackendCapabilities Capabilities { get; }

    public IWebSceneHostServices Services { get; }

    public IWebSceneInputSource Input { get; }

    public abstract IReadOnlyList<WebSceneBackendDiagnostic> Diagnostics { get; }

    public void EnsureCapabilities(WebSceneBackendCapabilities required)
    {
        ThrowIfDisposed();
        var missing = required & ~Capabilities;
        if (missing == WebSceneBackendCapabilities.None)
        {
            return;
        }

        var diagnostic = new WebSceneBackendDiagnostic(
            "backend.capability",
            $"Missing required backend capabilities: {missing}.",
            default,
            DateTimeOffset.UtcNow);
        ReportDiagnostic(diagnostic);
        throw new WebSceneBackendCapabilityException(required, Capabilities);
    }

    public void Mount()
    {
        ThrowIfDisposed();
        if (State == WebSceneBackendState.Mounted)
        {
            return;
        }

        if (State is not (WebSceneBackendState.Created or WebSceneBackendState.Unmounted))
        {
            throw new InvalidOperationException($"Cannot mount a backend in state '{State}'.");
        }

        Services.Dispatcher.VerifyAccess();
        OnMount();
        State = WebSceneBackendState.Mounted;
    }

    public void Unmount()
    {
        ThrowIfDisposed();
        if (State == WebSceneBackendState.Unmounted)
        {
            return;
        }

        if (State != WebSceneBackendState.Mounted)
        {
            throw new InvalidOperationException($"Cannot unmount a backend in state '{State}'.");
        }

        Services.Dispatcher.VerifyAccess();
        OnUnmount();
        State = WebSceneBackendState.Unmounted;
    }

    public abstract WebSceneBackendNode CreateNode(in WebSceneBackendNodeDescriptor descriptor);

    public abstract void Attach(WebSceneBackendNode parent, WebSceneBackendNode child, int index);

    public abstract void Detach(WebSceneBackendNode node);

    public abstract void Arrange(WebSceneBackendNode node, WebSceneRect bounds);

    public abstract void SetVisible(WebSceneBackendNode node, bool visible);

    public abstract void SetZIndex(WebSceneBackendNode node, int zIndex);

    public abstract void Invalidate(WebSceneBackendNode node, WebSceneInvalidationKind kind);

    public abstract WebSceneBackendNode? HitTest(WebScenePoint point);

    protected void RequireMounted()
    {
        ThrowIfDisposed();
        if (State != WebSceneBackendState.Mounted)
        {
            throw new InvalidOperationException($"Backend operation requires Mounted state; current state is '{State}'.");
        }

        Services.Dispatcher.VerifyAccess();
    }

    protected virtual void OnMount()
    {
    }

    protected virtual void OnUnmount()
    {
    }

    protected virtual void DisposeCore()
    {
    }

    protected abstract void ReportDiagnostic(WebSceneBackendDiagnostic diagnostic);

    protected void ThrowIfDisposed()
        => ObjectDisposedException.ThrowIf(_disposed, this);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        Services.Dispatcher.VerifyAccess();
        if (State == WebSceneBackendState.Mounted)
        {
            OnUnmount();
        }

        DisposeCore();
        _disposed = true;
        State = WebSceneBackendState.Disposed;
        GC.SuppressFinalize(this);
    }
}

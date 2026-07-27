using WebScene.JavaScript;

namespace JavaScript.Avalonia;

/// <summary>
/// Factory for a JavaScript runtime that owns a virtual iframe.
/// </summary>
public interface IExternalVirtualBrowsingContextFactory
{
    IExternalVirtualBrowsingContext Create(
        IWebSceneJavaScriptHost host,
        IWebSceneJavaScriptDocument frameDocument,
        object frameElement);
}

/// <summary>
/// Narrow lifecycle used by WebScene's generic virtual-iframe infrastructure.
/// Engine adapters retain ownership of script values and callback identity.
/// </summary>
public interface IExternalVirtualBrowsingContext : IWebSceneJavaScriptRealm
{
    void ExecuteInlineClassicScript(
        string code,
        object currentScript,
        string? documentName = null);

    void ExecuteClassicScript(string specifier);

}

/// <summary>
/// Optional engine-owned view of a frame Document. Returning this exact realm
/// object preserves Window.document/contentDocument identity across a trusted
/// same-origin context group without exposing engine types to the DOM layer.
/// </summary>
public interface IExternalVirtualBrowsingContextDocumentView
{
    object Document { get; }
}

/// <summary>
/// Dispatches trusted DOM events to an externally owned top-level window.
/// </summary>
public interface IExternalWindowEventDispatcher
{
    void DispatchWindowEvent(string type, object eventObject);
}

/// <summary>
/// Completes the DOM-owned side of an external iframe context's disposal without
/// exposing engine-owned runtime fields through the JavaScript bridge.
/// </summary>
public static class ExternalVirtualBrowsingContextLifecycle
{
    public static void Detach(
        IWebSceneJavaScriptDocument document,
        object frameElement,
        IExternalVirtualBrowsingContext context)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(frameElement);
        ArgumentNullException.ThrowIfNull(context);
        document.DetachExternalBrowsingContext(frameElement, context);
    }
}

/// <summary>
/// Resolved classic-script or CommonJS module source supplied to an optional
/// external JavaScript runtime.
/// </summary>
public readonly record struct ExternalJavaScriptSource(
    string CacheKey,
    string Content,
    string FileName,
    string? Directory);

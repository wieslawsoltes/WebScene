using JavaScript.Avalonia;

namespace WebScene.JavaScript;

/// <summary>
/// DOM surface required by a JavaScript-engine adapter. Implementations retain their
/// concrete object identity when passed to an engine; the contract only controls
/// ownership and lifecycle slots.
/// </summary>
public interface IWebSceneJavaScriptDocument
{
    object JavaScriptObject { get; }

    object Location { get; }

    IExternalDomEventListenerAdapter? ExternalEventListenerAdapter { get; set; }

    object? ExternalWindowContext { get; set; }

    IExternalWindowEventDispatcher? ExternalWindowEventDispatcher { get; set; }

    object ParseMarkupDocument(string markup, string mimeType);

    void DetachExternalBrowsingContext(
        object frameElement,
        IExternalVirtualBrowsingContext context);
}

/// <summary>
/// Framework-neutral browser/runtime surface consumed by JavaScript engines.
/// Hot DOM and Canvas operations remain typed and batched so an engine boundary does
/// not require reflection, wrapper allocation, or one interface call per command.
/// </summary>
public interface IWebSceneJavaScriptHost
{
    IWebSceneJavaScriptDocument Document { get; }

    object BrowserWindow { get; }

    Type UrlBackendType { get; }

    string ScriptBaseDirectory { get; set; }

    IExternalJavaScriptCallbackAdapter? ExternalCallbackAdapter { get; set; }

    IExternalVirtualBrowsingContextFactory? ExternalVirtualBrowsingContextFactory { get; set; }

    IDisposable EnterExternalJavaScriptCall();

    double GetPerformanceTimestamp();

    void ExecuteExternalClassicScript(string specifier, Action<string, string> evaluator);

    void ExecuteExternalInlineClassicScript(object currentScript, Action evaluator);

    ExternalJavaScriptSource ResolveExternalScript(
        string specifier,
        string? referrerDirectory = null);

    object CreateCanvasPath(object? path);

}

/// <summary>
/// One JavaScript global environment. Owner windows and virtual iframe windows use
/// the same execution, task-checkpoint, event, and deterministic-disposal lifecycle.
/// Engine-owned values remain opaque and retain their native identity.
/// </summary>
public interface IWebSceneJavaScriptRealm : IDisposable
{
    object Window { get; }

    void Execute(string code, string? documentName = null);

    void ProcessPendingTasks();

    void DispatchWindowEvent(string type, object eventObject);
}

public interface IWebSceneJavaScriptRuntime : IWebSceneJavaScriptRealm
{
    object? Invoke(object callback, params object?[] arguments);
}

public interface IWebSceneJavaScriptRuntimeFactory
{
    IWebSceneJavaScriptRuntime Create(IWebSceneJavaScriptHost host);
}

public interface IWebSceneJavaScriptModuleCache
{
    ExternalJavaScriptSource Resolve(
        string resolutionKey,
        Func<ExternalJavaScriptSource> resolver);
}

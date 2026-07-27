using WebScene.JavaScript;
using JavaScript.Avalonia;
using Xunit;

namespace WebScene.JavaScript.Tests;

public sealed class RuntimeContractTests
{
    [Fact]
    public void VirtualBrowsingContextDetachPreservesDocumentElementAndRuntimeIdentity()
    {
        var document = new RecordingDocument();
        var element = new object();
        var context = new RecordingBrowsingContext();

        ExternalVirtualBrowsingContextLifecycle.Detach(document, element, context);

        Assert.Same(element, document.DetachedElement);
        Assert.Same(context, document.DetachedContext);
        Assert.False(context.Disposed);
    }

    [Fact]
    public void ExternalSourceIsAnImmutablePortableValue()
    {
        var source = new ExternalJavaScriptSource(
            "cache-key",
            "export default 42;",
            "module.js",
            "/assets");

        Assert.Equal("cache-key", source.CacheKey);
        Assert.Equal("export default 42;", source.Content);
        Assert.Equal("module.js", source.FileName);
        Assert.Equal("/assets", source.Directory);
        Assert.Equal(source, source with { });
    }

    [Fact]
    public void CallbackContractsRetainOpaqueValuesWithoutEngineDependencies()
    {
        var callback = new RecordingCallback();
        var thisValue = new object();
        var argument = new object();

        callback.Invoke(thisValue, argument, 42);

        Assert.Same(thisValue, callback.ThisValue);
        Assert.Same(argument, callback.Arguments![0]);
        Assert.Equal(42, callback.Arguments[1]);
    }

    [Fact]
    public void VirtualContextUsesThePortableRealmLifecycle()
    {
        var context = new RecordingBrowsingContext();
        IWebSceneJavaScriptRealm realm = context;
        var windowEvent = new object();

        realm.Execute("globalThis.answer = 42;", "realm.js");
        realm.DispatchWindowEvent("load", windowEvent);
        realm.ProcessPendingTasks();
        realm.Dispose();

        Assert.Same(context.Window, realm.Window);
        Assert.Equal("realm.js", context.ExecutedDocumentName);
        Assert.Equal("load", context.DispatchedEventType);
        Assert.Same(windowEvent, context.DispatchedEvent);
        Assert.Equal(1, context.PendingTaskCheckpoints);
        Assert.True(context.Disposed);
    }

    private sealed class RecordingDocument : IWebSceneJavaScriptDocument
    {
        public object JavaScriptObject => this;

        public object Location { get; } = new();

        public IExternalDomEventListenerAdapter? ExternalEventListenerAdapter { get; set; }

        public object? ExternalWindowContext { get; set; }

        public IExternalWindowEventDispatcher? ExternalWindowEventDispatcher { get; set; }

        public object? DetachedElement { get; private set; }

        public IExternalVirtualBrowsingContext? DetachedContext { get; private set; }

        public object ParseMarkupDocument(string markup, string mimeType)
            => new { markup, mimeType };

        public void DetachExternalBrowsingContext(
            object frameElement,
            IExternalVirtualBrowsingContext context)
        {
            DetachedElement = frameElement;
            DetachedContext = context;
        }
    }

    private sealed class RecordingBrowsingContext : IExternalVirtualBrowsingContext
    {
        public object Window { get; } = new();

        public bool Disposed { get; private set; }

        public string? ExecutedDocumentName { get; private set; }

        public string? DispatchedEventType { get; private set; }

        public object? DispatchedEvent { get; private set; }

        public int PendingTaskCheckpoints { get; private set; }

        public void Execute(string code, string? documentName = null)
        {
            ExecutedDocumentName = documentName;
        }

        public void ExecuteInlineClassicScript(
            string code,
            object currentScript,
            string? documentName = null)
        {
        }

        public void ExecuteClassicScript(string specifier)
        {
        }

        public void ProcessPendingTasks()
        {
            PendingTaskCheckpoints++;
        }

        public void DispatchWindowEvent(string type, object eventObject)
        {
            DispatchedEventType = type;
            DispatchedEvent = eventObject;
        }

        public void Dispose() => Disposed = true;
    }

    private sealed class RecordingCallback : IExternalJavaScriptCallback
    {
        public object? ThisValue { get; private set; }

        public object?[]? Arguments { get; private set; }

        public void Invoke(object? thisValue, params object?[] arguments)
        {
            ThisValue = thisValue;
            Arguments = arguments;
        }
    }
}

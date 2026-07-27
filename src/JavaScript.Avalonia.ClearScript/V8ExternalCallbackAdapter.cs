using JavaScript.Avalonia;
using Microsoft.ClearScript;

namespace JavaScript.Avalonia.ClearScript;

/// <summary>
/// Preserves ClearScript function identity when WebScene retains DOM listeners or
/// browser-task callbacks across calls into V8.
/// </summary>
public sealed class V8ExternalEventListener : IExternalDomEventListener, IExternalJavaScriptCallback
{
    private static int s_nextId;
    private static readonly bool s_traceCallbackErrors =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_TRACE_V8_CALLBACK_ERRORS"),
            "1",
            StringComparison.Ordinal);
    private readonly ScriptObject _eventApplyCallback;
    private readonly ScriptObject _applyCallback;
    private readonly ScriptObject _callback;
    private readonly string _scopeName;
    private readonly int _id = Interlocked.Increment(ref s_nextId);

    public V8ExternalEventListener(
        ScriptObject eventApplyCallback,
        ScriptObject applyCallback,
        ScriptObject callback,
        string scopeName)
    {
        _eventApplyCallback = eventApplyCallback;
        _applyCallback = applyCallback;
        _callback = callback;
        _scopeName = scopeName;
    }

    public int InvokeCount { get; private set; }

    internal ScriptObject Callback => _callback;

    public void Invoke(object currentTarget, object domEvent)
    {
        var sourceEvent = domEvent is IExternalSyntheticEventSource { SourceEvent: ScriptObject scriptEvent }
            ? scriptEvent
            : null;
        _eventApplyCallback.InvokeAsFunction(_callback, currentTarget, domEvent, sourceEvent);
        InvokeCount++;
    }

    public void Invoke(object? thisValue, params object?[] arguments)
    {
        try
        {
            _applyCallback.InvokeAsFunction(_callback, thisValue, arguments);
            InvokeCount++;
        }
        catch when (TraceCallbackError())
        {
            throw;
        }
    }

    public override string ToString()
    {
        try
        {
            var name = Convert.ToString(_callback.GetProperty("name"));
            return string.IsNullOrWhiteSpace(name)
                ? $"{_scopeName}:anonymous#{_id}"
                : $"{_scopeName}:{name}#{_id}";
        }
        catch
        {
            return $"{_scopeName}:callback#{_id}";
        }
    }

    private bool TraceCallbackError()
    {
        if (s_traceCallbackErrors)
        {
            Console.Error.WriteLine($"[V8 callback error] scope={_scopeName}, id={_id}, invokes={InvokeCount}");
        }
        return false;
    }
}

/// <summary>
/// Adapts opaque ClearScript function objects to WebScene's engine-neutral callback
/// and DOM-listener contracts.
/// </summary>
public sealed class V8ExternalEventListenerAdapter :
    IExternalDomEventListenerAdapter,
    IExternalSyntheticEventAdapter,
    IExternalDomEventListenerBatchInvoker,
    IExternalJavaScriptCallbackAdapter
{
    private readonly ScriptObject _eventApplyCallback;
    private readonly ScriptObject _eventBatchApplyCallback;
    private readonly ScriptObject _eventCompleteCallback;
    private readonly ScriptObject _applyCallback;
    private readonly string _scopeName;
    private readonly Dictionary<ScriptObject, V8ExternalEventListener> _listeners = new();

    public V8ExternalEventListenerAdapter(
        ScriptObject eventApplyCallback,
        ScriptObject eventBatchApplyCallback,
        ScriptObject eventCompleteCallback,
        ScriptObject applyCallback,
        string scopeName)
    {
        _eventApplyCallback = eventApplyCallback;
        _eventBatchApplyCallback = eventBatchApplyCallback;
        _eventCompleteCallback = eventCompleteCallback;
        _applyCallback = applyCallback;
        _scopeName = scopeName;
    }

    public IExternalDomEventListener? GetEventListener(object callback, bool create)
    {
        if (callback is not ScriptObject scriptCallback)
        {
            return null;
        }

        if (_listeners.TryGetValue(scriptCallback, out var listener) || !create)
        {
            return listener;
        }

        listener = new V8ExternalEventListener(
            _eventApplyCallback,
            _applyCallback,
            scriptCallback,
            _scopeName);
        _listeners.Add(scriptCallback, listener);
        return listener;
    }

    public IExternalJavaScriptCallback? GetCallback(object callback, bool create)
        => GetEventListener(callback, create) as IExternalJavaScriptCallback;

    public ExternalDomEventListenerOptions GetEventListenerOptions(object? options)
    {
        if (options is bool capture)
        {
            return new ExternalDomEventListenerOptions(capture, Once: false, Passive: false);
        }

        if (options is not ScriptObject scriptOptions)
        {
            return default;
        }

        return new ExternalDomEventListenerOptions(
            ReadBoolean(scriptOptions, "capture"),
            ReadBoolean(scriptOptions, "once"),
            ReadBoolean(scriptOptions, "passive"));
    }

    public bool TryReadSyntheticEvent(object eventValue, out ExternalSyntheticEventData data)
    {
        if (eventValue is not ScriptObject scriptEvent)
        {
            data = default;
            return false;
        }

        data = new ExternalSyntheticEventData(
            Convert.ToString(scriptEvent.GetProperty("type")) ?? string.Empty,
            ReadBoolean(scriptEvent, "bubbles"),
            ReadBoolean(scriptEvent, "cancelable"),
            scriptEvent.GetProperty("detail"),
            scriptEvent);
        return true;
    }

    public void SetDefaultPrevented(object eventValue, bool defaultPrevented)
    {
        if (eventValue is ScriptObject scriptEvent)
        {
            scriptEvent.SetProperty("defaultPrevented", defaultPrevented);
        }
    }

    public void CompleteDispatch(object eventValue)
    {
        if (eventValue is not ScriptObject scriptEvent)
        {
            return;
        }
        _eventCompleteCallback.InvokeAsFunction(scriptEvent);
    }

    public void InvokeBatch(
        object currentTarget,
        object domEvent,
        IReadOnlyList<IExternalDomEventListener> listeners,
        IExternalDomEventBatchControl control)
    {
        var callbacks = new ScriptObject[listeners.Count];
        for (var index = 0; index < listeners.Count; index++)
        {
            if (listeners[index] is not V8ExternalEventListener listener)
            {
                throw new ArgumentException("The listener batch contains a callback from another engine.", nameof(listeners));
            }
            callbacks[index] = listener.Callback;
        }

        var sourceEvent = domEvent is IExternalSyntheticEventSource { SourceEvent: ScriptObject scriptEvent }
            ? scriptEvent
            : null;
        _eventBatchApplyCallback.InvokeAsFunction(callbacks, currentTarget, domEvent, sourceEvent, control);
    }

    private static bool ReadBoolean(ScriptObject options, string name)
        => options.GetProperty(name) is bool value && value;

    internal void Clear() => _listeners.Clear();
}

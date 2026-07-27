namespace JavaScript.Avalonia;

/// <summary>
/// Engine-neutral callable used by browser task sources such as timers and
/// requestAnimationFrame. The engine adapter is responsible for preserving the
/// JavaScript function's identity and invoking it with the supplied this value.
/// </summary>
public interface IExternalJavaScriptCallback
{
    void Invoke(object? thisValue, params object?[] arguments);
}

/// <summary>
/// Converts opaque function objects from an alternate JavaScript engine into
/// callbacks that WebScene can retain across asynchronous browser tasks.
/// </summary>
public interface IExternalJavaScriptCallbackAdapter
{
    IExternalJavaScriptCallback? GetCallback(object callback, bool create);
}

public interface IExternalDomEventListener
{
    void Invoke(object currentTarget, object domEvent);
}

public readonly record struct ExternalDomEventListenerOptions(
    bool Capture,
    bool Once,
    bool Passive);

public readonly record struct ExternalSyntheticEventData(
    string Type,
    bool Bubbles,
    bool Cancelable,
    object? Detail,
    object? SourceEvent);

public interface IExternalSyntheticEventAdapter
{
    bool TryReadSyntheticEvent(object eventValue, out ExternalSyntheticEventData data);

    void SetDefaultPrevented(object eventValue, bool defaultPrevented);

    void CompleteDispatch(object eventValue);
}

/// <summary>
/// Converts opaque function and option values from another JavaScript engine into
/// WebScene's engine-neutral listener contract. Implementations own callback identity
/// caching so removal of the original engine function finds the same registration.
/// </summary>
public interface IExternalDomEventListenerAdapter
{
    IExternalDomEventListener? GetEventListener(object callback, bool create);

    ExternalDomEventListenerOptions GetEventListenerOptions(object? options);
}

/// <summary>
/// Invokes one DOM target/phase listener list in a single external-engine entry.
/// This preserves the browser microtask checkpoint after dispatch rather than
/// introducing a checkpoint between listeners.
/// </summary>
public interface IExternalDomEventListenerBatchInvoker
{
    void InvokeBatch(
        object currentTarget,
        object domEvent,
        IReadOnlyList<IExternalDomEventListener> listeners,
        IExternalDomEventBatchControl control);
}

public interface IExternalDomEventBatchControl
{
    bool ShouldStop { get; }

    void BeforeInvoke(int index);

    void ReportError(int index, string error);

    void AfterInvoke(int index);
}

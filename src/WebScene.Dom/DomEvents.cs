namespace JavaScript.Avalonia;

public enum DomEventPhase
{
    None = 0,
    CapturingPhase = 1,
    AtTarget = 2,
    BubblingPhase = 3
}

/// <summary>
/// Backend-neutral DOM event state machine. Platform adapters override
/// <see cref="OnHandledChanged"/> to synchronize a native routed-event flag without
/// adding wrapper objects or delegates to each event.
/// </summary>
public class DomEvent
{
    private bool _propagationStopped;
    private bool _immediatePropagationStopped;
    private bool _defaultPrevented;
    private bool _handledFlag;
    private bool _currentListenerPassive;
    private object[] _composedPath = Array.Empty<object>();

    public DomEvent(
        string type,
        bool bubbles,
        bool cancelable,
        bool initiallyHandled,
        double timeStamp,
        bool isTrusted)
    {
        this.type = type;
        this.bubbles = bubbles;
        this.cancelable = cancelable;
        this.timeStamp = timeStamp;
        this.isTrusted = isTrusted;
        _handledFlag = initiallyHandled;
    }

    public string type { get; }

    public object? target { get; internal set; }

    public object? currentTarget { get; internal set; }

    public DomEventPhase eventPhase { get; internal set; } = DomEventPhase.None;

    public bool bubbles { get; }

    public bool cancelable { get; }

    public double timeStamp { get; }

    public bool defaultPrevented => _defaultPrevented;

    public bool isTrusted { get; }

    public bool composed => true;

    internal void SetComposedPath(IEnumerable<object> path)
        => _composedPath = path.ToArray();

    public object[] composedPath() => _composedPath.ToArray();

    public bool handled
    {
        get => _handledFlag;
        set
        {
            _handledFlag = value;
            OnHandledChanged(value);
        }
    }

    internal bool PropagationStopped => _propagationStopped;

    internal bool ImmediatePropagationStopped => _immediatePropagationStopped;

    internal void SetCurrentTarget(object? currentTarget, DomEventPhase phase, bool passive)
    {
        this.currentTarget = currentTarget;
        eventPhase = phase;
        _currentListenerPassive = passive;
    }

    internal void ResetCurrentTarget()
    {
        currentTarget = null;
        eventPhase = DomEventPhase.None;
        _currentListenerPassive = false;
    }

    public void stopPropagation()
    {
        _propagationStopped = true;
    }

    public void stopImmediatePropagation()
    {
        _propagationStopped = true;
        _immediatePropagationStopped = true;
    }

    public void preventDefault()
    {
        if (!cancelable || _currentListenerPassive)
        {
            return;
        }

        _defaultPrevented = true;
        handled = true;
    }

    protected virtual void OnHandledChanged(bool value)
    {
    }
}

public sealed class DomSyntheticEvent : DomEvent, IExternalSyntheticEventSource
{
    internal DomSyntheticEvent(
        string type,
        bool bubbles,
        bool cancelable,
        double timeStamp,
        object? detail,
        DefaultPreventedAccessor? accessor,
        object? sourceEvent = null)
        : base(type, bubbles, cancelable, initiallyHandled: false, timeStamp, isTrusted: false)
    {
        this.detail = detail;
        _accessor = accessor;
        SourceEvent = sourceEvent;
    }

    public object? detail { get; }

    /// <summary>
    /// Opaque engine-owned Event object supplied to dispatchEvent. Adapters may
    /// deliver it back to listeners so ordinary expando/accessor properties and
    /// JavaScript object identity survive the backend-neutral dispatch path.
    /// </summary>
    public object? SourceEvent { get; }

    private readonly DefaultPreventedAccessor? _accessor;

    internal void SyncDefaultPrevented()
    {
        _accessor?.SetDefaultPrevented(defaultPrevented);
    }
}

public sealed class DomTransitionEvent : DomEvent
{
    internal DomTransitionEvent(
        string type,
        string propertyName,
        double elapsedTime,
        double timeStamp)
        : base(type, bubbles: true, cancelable: false, initiallyHandled: false,
            timeStamp, isTrusted: true)
    {
        this.propertyName = propertyName;
        this.elapsedTime = elapsedTime;
    }

    public string propertyName { get; }

    public double elapsedTime { get; }

    public string pseudoElement => string.Empty;
}

public sealed class DefaultPreventedAccessor
{
    private readonly Action<bool> _setDefaultPrevented;

    public DefaultPreventedAccessor(Action<bool> setDefaultPrevented)
    {
        _setDefaultPrevented = setDefaultPrevented;
    }

    public void SetDefaultPrevented(bool value) => _setDefaultPrevented(value);
}

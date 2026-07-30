using System.Threading.Tasks.Sources;

namespace WebScene.JavaScript.Interop;

/// <summary>
/// Coalesces native callback-queue edge notifications into an awaitable signal.
/// Continuations always run asynchronously so native engine callbacks never
/// re-enter the engine on its worker thread.
/// </summary>
public sealed class JavaScriptCallbackSignal : IValueTaskSource
{
    private readonly object _gate = new();
    private ManualResetValueTaskSourceCore<bool> _core =
        new()
        {
            RunContinuationsAsynchronously = true
        };
    private CancellationTokenRegistration _cancellationRegistration;
    private bool _waiting;
    private bool _signaled;

    public void Notify()
    {
        CancellationTokenRegistration cancellationRegistration;
        lock (_gate)
        {
            if (!_waiting)
            {
                _signaled = true;
                return;
            }
            _waiting = false;
            cancellationRegistration = _cancellationRegistration;
            _cancellationRegistration = default;
        }
        cancellationRegistration.Dispose();
        _core.SetResult(true);
    }

    public ValueTask WaitAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_signaled)
            {
                _signaled = false;
                return ValueTask.CompletedTask;
            }
            if (_waiting)
            {
                throw new InvalidOperationException(
                    "Only one callback signal waiter is supported.");
            }
            _core.Reset();
            _waiting = true;
            if (cancellationToken.CanBeCanceled)
            {
                _cancellationRegistration =
                    cancellationToken.UnsafeRegister(
                        static state =>
                            ((JavaScriptCallbackSignal)state!).Cancel(),
                        this);
            }
            return new ValueTask(this, _core.Version);
        }
    }

    void IValueTaskSource.GetResult(short token)
    {
        try
        {
            _core.GetResult(token);
        }
        finally
        {
            CancellationTokenRegistration cancellationRegistration;
            lock (_gate)
            {
                cancellationRegistration = _cancellationRegistration;
                _cancellationRegistration = default;
            }
            cancellationRegistration.Dispose();
        }
    }

    ValueTaskSourceStatus IValueTaskSource.GetStatus(short token)
        => _core.GetStatus(token);

    void IValueTaskSource.OnCompleted(
        Action<object?> continuation,
        object? state,
        short token,
        ValueTaskSourceOnCompletedFlags flags)
        => _core.OnCompleted(continuation, state, token, flags);

    private void Cancel()
    {
        lock (_gate)
        {
            if (!_waiting) return;
            _waiting = false;
        }
        _core.SetException(new OperationCanceledException());
    }
}

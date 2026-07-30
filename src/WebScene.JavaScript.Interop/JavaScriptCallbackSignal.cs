namespace WebScene.JavaScript.Interop;

/// <summary>
/// Coalesces native callback-queue edge notifications into an awaitable signal.
/// Continuations always run asynchronously so native engine callbacks never
/// re-enter the engine on its worker thread.
/// </summary>
public sealed class JavaScriptCallbackSignal
{
    private readonly object _gate = new();
    private TaskCompletionSource? _waiter;
    private bool _signaled;

    public void Notify()
    {
        TaskCompletionSource? waiter;
        lock (_gate)
        {
            waiter = _waiter;
            if (waiter is null)
            {
                _signaled = true;
                return;
            }
            _waiter = null;
        }
        waiter.TrySetResult();
    }

    public ValueTask WaitAsync(CancellationToken cancellationToken = default)
    {
        Task task;
        lock (_gate)
        {
            if (_signaled)
            {
                _signaled = false;
                return ValueTask.CompletedTask;
            }
            _waiter ??= new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
            task = _waiter.Task;
        }
        return cancellationToken.CanBeCanceled
            ? new ValueTask(task.WaitAsync(cancellationToken))
            : new ValueTask(task);
    }
}

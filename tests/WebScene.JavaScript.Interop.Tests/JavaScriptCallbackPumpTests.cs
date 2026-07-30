using WebScene.JavaScript.Interop;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class JavaScriptCallbackPumpTests
{
    [Fact]
    public async Task SignaledPumpDoesNotEvaluateWhileTheQueueIsIdle()
    {
        var signal = new JavaScriptCallbackSignal();
        var dispatched = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var queued = 0;
        var callbackEvaluations = 0;
        var invoker = new SignaledInvoker(
            signal,
            () =>
            {
                Interlocked.Increment(ref callbackEvaluations);
                if (Interlocked.Exchange(ref queued, 0) == 1)
                {
                    dispatched.TrySetResult();
                    return true;
                }
                return false;
            });
        await using var pump = JavaScriptCallbackPump.Start(invoker);

        await Task.Delay(30);
        Assert.Equal(0, Volatile.Read(ref callbackEvaluations));

        Interlocked.Exchange(ref queued, 1);
        signal.Notify();
        await dispatched.Task.WaitAsync(TimeSpan.FromSeconds(2));
        await WaitUntilAsync(
            () => Volatile.Read(ref callbackEvaluations) >= 2,
            TimeSpan.FromSeconds(2));
        var drainedEvaluationCount = Volatile.Read(ref callbackEvaluations);

        await Task.Delay(30);
        Assert.Equal(drainedEvaluationCount, Volatile.Read(ref callbackEvaluations));
    }

    [Fact]
    public async Task SignalCoalescesNotifications()
    {
        var signal = new JavaScriptCallbackSignal();
        signal.Notify();
        signal.Notify();

        await signal.WaitAsync();
        var pending = signal.WaitAsync().AsTask();
        Assert.False(pending.IsCompleted);

        signal.Notify();
        await pending.WaitAsync(TimeSpan.FromSeconds(2));
    }

    [Fact]
    public async Task SignalCanBeReusedAfterCancellation()
    {
        var signal = new JavaScriptCallbackSignal();
        using var cancellation = new CancellationTokenSource();
        var pending = signal.WaitAsync(cancellation.Token).AsTask();

        await cancellation.CancelAsync();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => pending);

        signal.Notify();
        await signal.WaitAsync();
    }

    private static async Task WaitUntilAsync(
        Func<bool> condition,
        TimeSpan timeout)
    {
        using var cancellation = new CancellationTokenSource(timeout);
        while (!condition())
        {
            await Task.Delay(1, cancellation.Token);
        }
    }

    private sealed class SignaledInvoker(
        JavaScriptCallbackSignal signal,
        Func<bool> pump)
        : IJavaScriptBidirectionalInvoker
    {
        public bool SupportsCallbackNotifications => true;

        public ValueTask WaitForCallbackAsync(
            CancellationToken cancellationToken = default)
            => signal.WaitAsync(cancellationToken);

        public ValueTask<bool> PumpCallbackAsync(
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult(pump());

        public ValueTask<JavaScriptObjectReference> ConstructAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public ValueTask<JavaScriptObjectReference> RegisterCallbackTargetAsync(
            IJavaScriptCallbackTarget target,
            IReadOnlyList<JavaScriptCallbackMethod> methods,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<JavaScriptFunctionReference> RegisterFunctionAsync(
            JavaScriptCallbackHandler callback,
            JavaScriptCallbackReturnKind returnKind =
                JavaScriptCallbackReturnKind.Void,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<T?> InvokeFunctionAsync<T>(
            JavaScriptObjectReference function,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask InvokeFunctionVoidAsync(
            JavaScriptObjectReference function,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();
    }
}

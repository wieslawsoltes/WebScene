using System.Text.Json;
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
        var invoker = new NativeJavaScriptInvoker(
            (_, document, _) =>
            {
                return Task.FromResult(document switch
                {
                    "webscene-native-dotnet-interop.js" => "true",
                    "webscene-interop-register-callback.js" => "51",
                    "webscene-interop-take-callback.js" => TakeCallback(),
                    "webscene-interop-complete-callback.js" => "true",
                    _ => throw new InvalidOperationException(document)
                });

                string TakeCallback()
                {
                    Interlocked.Increment(ref callbackEvaluations);
                    return Interlocked.Exchange(ref queued, 0) == 1
                        ? """{"call":7,"target":1,"method":"invoke","arguments":[]}"""
                        : "null";
                }
            },
            waitForCallbackAsync: signal.WaitAsync);
        await invoker.RegisterCallbackTargetAsync(
            new SignalTarget(dispatched),
            [new("invoke", JavaScriptCallbackReturnKind.Void)]);
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

    private sealed class SignalTarget(TaskCompletionSource dispatched)
        : IJavaScriptCallbackTarget
    {
        public ValueTask<object?> DispatchAsync(
            string method,
            JsonElement arguments,
            CancellationToken cancellationToken = default)
        {
            dispatched.TrySetResult();
            return ValueTask.FromResult<object?>(null);
        }
    }
}

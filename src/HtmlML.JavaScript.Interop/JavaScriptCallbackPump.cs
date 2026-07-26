namespace HtmlML.JavaScript.Interop;

/// <summary>
/// Continuously drains JavaScript-to-.NET calls queued by callback adapters.
/// The pump should live for at least as long as registered datafeed or broker objects.
/// </summary>
public sealed class JavaScriptCallbackPump : IAsyncDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _run;

    private JavaScriptCallbackPump(
        IJavaScriptBidirectionalInvoker invoker,
        TimeSpan idleDelay,
        CancellationToken cancellationToken)
    {
        var linked = CancellationTokenSource.CreateLinkedTokenSource(
            _stop.Token,
            cancellationToken);
        _run = RunAsync(invoker, idleDelay, linked);
    }

    public Task Completion => _run;

    public static JavaScriptCallbackPump Start(
        IJavaScriptBidirectionalInvoker invoker,
        TimeSpan? idleDelay = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(invoker);
        return new JavaScriptCallbackPump(
            invoker,
            idleDelay ?? TimeSpan.FromMilliseconds(4),
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        await _stop.CancelAsync().ConfigureAwait(false);
        try
        {
            await _run.ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (_stop.IsCancellationRequested)
        {
        }
        _stop.Dispose();
    }

    private static async Task RunAsync(
        IJavaScriptBidirectionalInvoker invoker,
        TimeSpan idleDelay,
        CancellationTokenSource linked)
    {
        using (linked)
        {
            while (!linked.IsCancellationRequested)
            {
                var handled = await invoker.PumpCallbackAsync(linked.Token)
                    .ConfigureAwait(false);
                if (!handled)
                {
                    await Task.Delay(idleDelay, linked.Token).ConfigureAwait(false);
                }
            }
        }
    }
}

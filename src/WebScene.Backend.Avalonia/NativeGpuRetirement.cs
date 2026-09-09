using System.Collections.Concurrent;
using System.Diagnostics;

namespace WebScene.Backends.Avalonia.Native;

// Owns detached presenters independently of their removed composition visual.
// Failed retirement stays retained for diagnosis; timeout/loss is not completion.
internal static class NativeGpuRetirement
{
    private sealed class Pending(NativeGpuScenePresenter presenter)
    {
        internal readonly NativeGpuScenePresenter Presenter = presenter;
        internal readonly TaskCompletionSource Completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
    private static readonly ConcurrentDictionary<long, Pending> Owners = new();
    private static long _nextId;
    internal static int RetainedCount => Owners.Count;

    internal static Task Start(NativeGpuScenePresenter presenter)
    {
        ArgumentNullException.ThrowIfNull(presenter);
        presenter.BeginShutdown();
        if (presenter.TryDiscardUnprepared()) return Task.CompletedTask;
        var pending = new Pending(presenter);
        var id = Interlocked.Increment(ref _nextId);
        if (!Owners.TryAdd(id, pending)) throw new InvalidOperationException("GPU retirement identity collision.");
        try
        {
            // Stop is delivered on the composition owner. Seal GPU reads here,
            // before handing polling to a background task. In particular Metal
            // session finalization is not covered by only the GRContext monitor.
            presenter.SealForDetachedRetirement();
            if (presenter.TryCompleteWithoutVisual())
            {
                Owners.TryRemove(id, out _);
                pending.Completion.TrySetResult();
                return pending.Completion.Task;
            }
            _ = Task.Run(async () =>
            {
                try
                {
                    var started = Stopwatch.GetTimestamp();
                    while (!presenter.TryCompleteWithoutVisual())
                    {
                        if (Stopwatch.GetElapsedTime(started) > TimeSpan.FromSeconds(15))
                            throw new TimeoutException("GPU retirement did not complete; resources remain retained.");
                        await Task.Delay(4).ConfigureAwait(false);
                    }
                    Owners.TryRemove(id, out _);
                    pending.Completion.TrySetResult();
                }
                catch (Exception error)
                {
                    Trace.TraceError($"GPU retirement {id} failed and retains its resources: {error}");
                    pending.Completion.TrySetException(error);
                }
            });
        }
        catch (Exception error) { pending.Completion.TrySetException(error); }
        return pending.Completion.Task;
    }
}

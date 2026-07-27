using System.Diagnostics;
using WebScene.Core;
using JavaScript.Avalonia;

namespace WebScene.JavaScript;

public readonly record struct WebSceneJavaScriptTaskPerformance(
    string Kind,
    long Count,
    long TotalTicks,
    long MaximumTicks,
    long TotalAllocatedBytes,
    long MaximumAllocatedBytes);

/// <summary>
/// Framework-neutral browser task scheduler. The backend supplies dispatcher and
/// frame primitives; callback values retain engine identity and are never wrapped.
/// </summary>
public abstract class WebSceneJavaScriptTaskSchedulerHost<THost>
    where THost : class, IWebSceneJavaScriptHost
{
    private const int MaximumPerformanceKinds = 256;
    private readonly Queue<(string Kind, Action Work)> _pendingUserTasks = new();
    private readonly Dictionary<int, IExternalJavaScriptCallback> _animationFrameCallbacks = new();
    private readonly Dictionary<int, IWebSceneScheduledWork> _timeouts = new();
    private readonly Dictionary<int, IWebSceneScheduledWork> _intervals = new();
    private readonly Dictionary<string, (
        long Count,
        long TotalTicks,
        long MaximumTicks,
        long TotalAllocatedBytes,
        long MaximumAllocatedBytes)> _taskPerformance = new(StringComparer.Ordinal);
    private readonly List<string> _exceptionDiagnostics = new();
    private int _timerSequence;
    private int _animationFrameSequence;
    private bool _isProcessingTasks;
    private bool _isDrainingUserTasks;
    private bool _animationFrameScheduled;
    private long _tasksEnqueued;
    private long _tasksExecuted;
    private int _maximumPendingTasks;
    private long _animationFrameBatchCount;
    private long _animationFrameCallbackCount;
    private long _taskTicks;
    private long _animationFrameTicks;
    private long _maximumTaskTicks;
    private long _maximumAnimationFrameTicks;
    private long _taskAllocatedBytes;
    private long _animationFrameAllocatedBytes;

    protected abstract bool IsJavaScriptEngineExecuting { get; }

    protected abstract bool IsJavaScriptTaskHostDisposed { get; }

    protected abstract bool TraceAnimationFrameCallbacks { get; }

    protected abstract IWebSceneDispatcher JavaScriptDispatcher { get; }

    protected abstract IWebSceneFrameScheduler JavaScriptFrames { get; }

    protected bool CollectJavaScriptTaskPerformanceMetrics { get; set; }

    public bool IsProcessingTasks => _isProcessingTasks;

    public bool IsDrainingUserTasks => _isDrainingUserTasks;

    public int PendingTimerCount => _timeouts.Count + _intervals.Count;

    public int PendingAnimationFrameCount => _animationFrameCallbacks.Count;

    public long TasksEnqueued => _tasksEnqueued;

    public long TasksExecuted => _tasksExecuted;

    public int MaximumPendingTasks => _maximumPendingTasks;

    public long AnimationFrameBatchCount => _animationFrameBatchCount;

    public long AnimationFrameCallbackCount => _animationFrameCallbackCount;

    public long TaskTicks => _taskTicks;

    public long AnimationFrameTicks => _animationFrameTicks;

    public long MaximumTaskTicks => _maximumTaskTicks;

    public long MaximumAnimationFrameTicks => _maximumAnimationFrameTicks;

    public long TaskAllocatedBytes => _taskAllocatedBytes;

    public long AnimationFrameAllocatedBytes => _animationFrameAllocatedBytes;

    public IReadOnlyList<string> ExceptionDiagnostics => _exceptionDiagnostics;

    public IReadOnlyList<WebSceneJavaScriptTaskPerformance> GetPerformanceMetrics()
        => _taskPerformance
            .OrderByDescending(static pair => pair.Value.MaximumTicks)
            .Select(static pair => new WebSceneJavaScriptTaskPerformance(
                pair.Key,
                pair.Value.Count,
                pair.Value.TotalTicks,
                pair.Value.MaximumTicks,
                pair.Value.TotalAllocatedBytes,
                pair.Value.MaximumAllocatedBytes))
            .ToArray();

    public void ResetPerformanceMetrics() => _taskPerformance.Clear();

    public void EnqueueUserTask(string kind, Action work)
    {
        ArgumentNullException.ThrowIfNull(kind);
        ArgumentNullException.ThrowIfNull(work);
        if (IsJavaScriptTaskHostDisposed)
        {
            return;
        }

        _pendingUserTasks.Enqueue((kind, work));
        if (CollectJavaScriptTaskPerformanceMetrics)
        {
            _tasksEnqueued++;
            _maximumPendingTasks = Math.Max(_maximumPendingTasks, _pendingUserTasks.Count);
        }
        if (_isDrainingUserTasks)
        {
            return;
        }
        _isDrainingUserTasks = true;
        Dispatcher.Post(DrainNextUserTask, WebSceneDispatchPriority.Background);
    }

    public int SetTimeout(IExternalJavaScriptCallback callback, int milliseconds)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (IsJavaScriptTaskHostDisposed)
        {
            return 0;
        }

        var id = ++_timerSequence;
        var work = Dispatcher.Schedule(
            TimeSpan.FromMilliseconds(Math.Max(0, milliseconds)),
            () =>
            {
                _timeouts.Remove(id);
                var kind = CollectJavaScriptTaskPerformanceMetrics ? $"timeout:{callback}" : "timeout";
                EnqueueUserTask(kind, () => Invoke(callback, "timeout"));
            },
            WebSceneDispatchPriority.Default);
        _timeouts[id] = work;
        return id;
    }

    public void ClearTimeout(int id)
    {
        if (!_timeouts.Remove(id, out var work))
        {
            return;
        }
        work.Cancel();
        work.Dispose();
    }

    public int SetInterval(IExternalJavaScriptCallback callback, int milliseconds)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (IsJavaScriptTaskHostDisposed)
        {
            return 0;
        }

        var id = ++_timerSequence;
        ScheduleInterval(id, callback, Math.Max(1, milliseconds));
        return id;
    }

    public void ClearInterval(int id)
    {
        if (!_intervals.Remove(id, out var work))
        {
            return;
        }
        work.Cancel();
        work.Dispose();
    }

    public int RequestAnimationFrame(IExternalJavaScriptCallback callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (IsJavaScriptTaskHostDisposed)
        {
            return 0;
        }

        var id = ++_animationFrameSequence;
        _animationFrameCallbacks[id] = callback;
        EnsureAnimationFrameScheduled();
        return id;
    }

    public void CancelAnimationFrame(int id) => _animationFrameCallbacks.Remove(id);

    public void QueueMicrotask(IExternalJavaScriptCallback callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (IsJavaScriptTaskHostDisposed)
        {
            return;
        }

        Dispatcher.Post(
            () =>
            {
                var kind = CollectJavaScriptTaskPerformanceMetrics ? $"microtask:{callback}" : "microtask";
                EnqueueUserTask(kind, () => Invoke(callback, "microtask"));
            },
            WebSceneDispatchPriority.Send);
    }

    protected void DisposeJavaScriptTaskScheduler()
    {
        if (IsJavaScriptTaskHostDisposed)
        {
            return;
        }

        CancelAllTimers();
        _animationFrameCallbacks.Clear();
        _pendingUserTasks.Clear();
        _isDrainingUserTasks = false;
    }

    public void CancelAllTimers()
    {
        foreach (var work in _timeouts.Values)
        {
            work.Dispose();
        }
        foreach (var work in _intervals.Values)
        {
            work.Dispose();
        }
        _timeouts.Clear();
        _intervals.Clear();
    }

    private void DrainNextUserTask()
    {
        if (_pendingUserTasks.Count == 0)
        {
            _isDrainingUserTasks = false;
            return;
        }

        var task = _pendingUserTasks.Dequeue();
        ExecuteUserTask(task.Kind, task.Work);

        if (_pendingUserTasks.Count > 0)
        {
            Dispatcher.Post(DrainNextUserTask, WebSceneDispatchPriority.Background);
        }
        else
        {
            _isDrainingUserTasks = false;
        }
    }

    private void ExecuteUserTask(string kind, Action work)
    {
        if (CollectJavaScriptTaskPerformanceMetrics)
        {
            _tasksExecuted++;
        }
        var started = CollectJavaScriptTaskPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
        var allocationStarted = CollectJavaScriptTaskPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        _isProcessingTasks = true;
        try
        {
            work();
        }
        catch (Exception exception)
        {
            if (_exceptionDiagnostics.Count < 200)
            {
                _exceptionDiagnostics.Add(exception.ToString());
            }
            Console.Error.WriteLine($"[UserTask {Host.GetPerformanceTimestamp():F1} ms] {kind}: {exception}");
        }
        finally
        {
            _isProcessingTasks = false;
            if (CollectJavaScriptTaskPerformanceMetrics)
            {
                RecordTaskPerformance(kind, started, allocationStarted);
            }
        }
    }

    private void RecordTaskPerformance(string kind, long started, long allocationStarted)
    {
        var elapsed = Stopwatch.GetTimestamp() - started;
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocationStarted;
        _taskTicks += elapsed;
        _maximumTaskTicks = Math.Max(_maximumTaskTicks, elapsed);
        _taskAllocatedBytes += allocatedBytes;
        var metricKind = kind;
        if (!_taskPerformance.ContainsKey(metricKind) && _taskPerformance.Count >= MaximumPerformanceKinds)
        {
            var separator = metricKind.IndexOf(':');
            metricKind = (separator >= 0 ? metricKind[..separator] : metricKind) + ":other";
        }
        if (_taskPerformance.TryGetValue(metricKind, out var metric))
        {
            _taskPerformance[metricKind] = (
                metric.Count + 1,
                metric.TotalTicks + elapsed,
                Math.Max(metric.MaximumTicks, elapsed),
                metric.TotalAllocatedBytes + allocatedBytes,
                Math.Max(metric.MaximumAllocatedBytes, allocatedBytes));
        }
        else
        {
            _taskPerformance[metricKind] = (1, elapsed, elapsed, allocatedBytes, allocatedBytes);
        }
    }

    private void ScheduleInterval(int id, IExternalJavaScriptCallback callback, int milliseconds)
    {
        IWebSceneScheduledWork? work = null;
        work = Dispatcher.Schedule(
            TimeSpan.FromMilliseconds(milliseconds),
            () =>
            {
                if (!_intervals.TryGetValue(id, out var current) || !ReferenceEquals(current, work))
                {
                    return;
                }

                current.Dispose();
                var kind = CollectJavaScriptTaskPerformanceMetrics ? $"interval:{callback}" : "interval";
                EnqueueUserTask(kind, () =>
                {
                    Invoke(callback, "interval");
                    if (!IsJavaScriptTaskHostDisposed
                        && _intervals.TryGetValue(id, out var pending)
                        && ReferenceEquals(pending, work))
                    {
                        ScheduleInterval(id, callback, milliseconds);
                    }
                });
            },
            WebSceneDispatchPriority.Default);
        _intervals[id] = work;
    }

    private void EnsureAnimationFrameScheduled()
    {
        if (_animationFrameScheduled || IsJavaScriptTaskHostDisposed)
        {
            return;
        }
        _animationFrameScheduled = true;
        var weakHost = new WeakReference<WebSceneJavaScriptTaskSchedulerHost<THost>>(this);
        Frames.RequestFrame(timestamp =>
        {
            if (weakHost.TryGetTarget(out var scheduler) && !scheduler.IsJavaScriptTaskHostDisposed)
            {
                scheduler.AnimationFrameTick(timestamp);
            }
        });
    }

    private void AnimationFrameTick(TimeSpan timestamp)
    {
        _animationFrameScheduled = false;
        if (IsJavaScriptEngineExecuting || _isProcessingTasks)
        {
            EnsureAnimationFrameScheduled();
            return;
        }
        if (_animationFrameCallbacks.Count == 0)
        {
            return;
        }

        var callbacks = _animationFrameCallbacks.Values.ToArray();
        _animationFrameCallbacks.Clear();
        if (CollectJavaScriptTaskPerformanceMetrics)
        {
            _animationFrameBatchCount++;
            _animationFrameCallbackCount += callbacks.Length;
        }

        Action work = () =>
        {
            var started = CollectJavaScriptTaskPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
            var allocationStarted = CollectJavaScriptTaskPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
            try
            {
                using var scope = Host.EnterExternalJavaScriptCall();
                foreach (var callback in callbacks)
                {
                    var callbackStarted = TraceAnimationFrameCallbacks ? Stopwatch.GetTimestamp() : 0;
                    try
                    {
                        callback.Invoke(Host.BrowserWindow, timestamp.TotalMilliseconds);
                    }
                    catch (Exception exception)
                    {
                        Console.Error.WriteLine($"[AnimationFrame] {exception}");
                    }
                    finally
                    {
                        if (TraceAnimationFrameCallbacks)
                        {
                            Console.WriteLine(
                                $"[RAF PROFILE] external:{callback.GetType().Name} " +
                                $"{TicksToTimeSpan(Stopwatch.GetTimestamp() - callbackStarted).TotalMilliseconds:F3} ms");
                        }
                    }
                }
            }
            finally
            {
                if (CollectJavaScriptTaskPerformanceMetrics)
                {
                    var elapsed = Stopwatch.GetTimestamp() - started;
                    _animationFrameTicks += elapsed;
                    _maximumAnimationFrameTicks = Math.Max(_maximumAnimationFrameTicks, elapsed);
                    _animationFrameAllocatedBytes +=
                        GC.GetAllocatedBytesForCurrentThread() - allocationStarted;
                }
            }
        };

        // The backend has already called us at its animation-frame boundary. Running
        // the batch here lets Canvas invalidations participate in this paint. Posting
        // it as Background work can defer an otherwise ready crosshair update until
        // the following frame.
        if (CollectJavaScriptTaskPerformanceMetrics)
        {
            _tasksEnqueued++;
        }
        ExecuteUserTask("animation-frame", work);
    }

    private void Invoke(IExternalJavaScriptCallback callback, string kind)
    {
        try
        {
            using var scope = Host.EnterExternalJavaScriptCall();
            callback.Invoke(Host.BrowserWindow);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[{kind}] {exception}");
        }
    }

    private static TimeSpan TicksToTimeSpan(long ticks)
        => TimeSpan.FromSeconds((double)ticks / Stopwatch.Frequency);

    private THost Host => (THost)(object)this;

    private IWebSceneDispatcher Dispatcher => JavaScriptDispatcher;

    private IWebSceneFrameScheduler Frames => JavaScriptFrames;
}

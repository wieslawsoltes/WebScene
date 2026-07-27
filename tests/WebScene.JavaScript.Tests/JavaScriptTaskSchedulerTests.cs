using WebScene.Core;
using WebScene.JavaScript;
using JavaScript.Avalonia;
using Xunit;

namespace WebScene.JavaScript.Tests;

public sealed class JavaScriptTaskSchedulerTests
{
    [Fact]
    public void TimeoutCrossesTheBackendClockOnceThenRunsAsAUserTask()
    {
        var fixture = new SchedulerFixture();
        var callback = new RecordingCallback();

        var id = fixture.Scheduler.SetTimeout(callback, 25);

        Assert.True(id > 0);
        Assert.Equal(TimeSpan.FromMilliseconds(25), fixture.Dispatcher.Scheduled[0].Delay);
        Assert.Empty(callback.Calls);

        fixture.Dispatcher.Scheduled[0].Fire();
        Assert.Empty(callback.Calls);
        fixture.Dispatcher.DrainPosted();

        Assert.Single(callback.Calls);
        Assert.Same(fixture.Host.BrowserWindow, callback.Calls[0].ThisValue);
        Assert.Equal(0, fixture.Scheduler.PendingTimerCount);
    }

    [Fact]
    public void TimeoutAndIntervalCancellationReleaseBackendWork()
    {
        var fixture = new SchedulerFixture();
        var callback = new RecordingCallback();
        var timeout = fixture.Scheduler.SetTimeout(callback, 1);
        var interval = fixture.Scheduler.SetInterval(callback, 1);

        fixture.Scheduler.ClearTimeout(timeout);
        fixture.Scheduler.ClearInterval(interval);
        foreach (var work in fixture.Dispatcher.Scheduled)
        {
            work.Fire();
        }
        fixture.Dispatcher.DrainPosted();

        Assert.Empty(callback.Calls);
        Assert.All(fixture.Dispatcher.Scheduled, static work => Assert.True(work.IsCancellationRequested));
        Assert.Equal(0, fixture.Scheduler.PendingTimerCount);
    }

    [Fact]
    public void IntervalRearmsOnlyAfterItsCallbackTaskCompletes()
    {
        var fixture = new SchedulerFixture();
        var callback = new RecordingCallback();
        fixture.Scheduler.SetInterval(callback, 2);

        fixture.Dispatcher.Scheduled[0].Fire();
        Assert.Single(fixture.Dispatcher.Scheduled);
        fixture.Dispatcher.DrainPosted();

        Assert.Single(callback.Calls);
        Assert.Equal(2, fixture.Dispatcher.Scheduled.Count);
        Assert.Equal(TimeSpan.FromMilliseconds(2), fixture.Dispatcher.Scheduled[1].Delay);
    }

    [Fact]
    public void AnimationFramesRunAtTheBackendFrameBoundaryAndPreserveCallbackIdentityAndTimestamp()
    {
        var fixture = new SchedulerFixture();
        var first = new RecordingCallback();
        var second = new RecordingCallback();

        fixture.Scheduler.RequestAnimationFrame(first);
        fixture.Scheduler.RequestAnimationFrame(second);

        Assert.Single(fixture.Frames.Pending);
        fixture.Frames.Fire(TimeSpan.FromMilliseconds(16.5));

        Assert.Single(first.Calls);
        Assert.Single(second.Calls);
        Assert.Same(fixture.Host.BrowserWindow, first.Calls[0].ThisValue);
        Assert.Equal(16.5, first.Calls[0].Arguments[0]);
        Assert.Equal(0, fixture.Scheduler.PendingAnimationFrameCount);
    }

    [Fact]
    public void AnimationFrameDefersWhileTheEngineIsExecuting()
    {
        var fixture = new SchedulerFixture { EngineExecuting = true };
        var callback = new RecordingCallback();
        fixture.Scheduler.RequestAnimationFrame(callback);

        fixture.Frames.Fire(TimeSpan.FromMilliseconds(10));

        Assert.Empty(callback.Calls);
        Assert.Single(fixture.Frames.Pending);
        fixture.EngineExecuting = false;
        fixture.Frames.Fire(TimeSpan.FromMilliseconds(20));
        Assert.Single(callback.Calls);
    }

    [Fact]
    public void MicrotaskCheckpointSerializesThroughThePortableTaskQueue()
    {
        var fixture = new SchedulerFixture();
        var callback = new RecordingCallback();

        fixture.Scheduler.QueueMicrotask(callback);

        Assert.Empty(callback.Calls);
        fixture.Dispatcher.DrainPosted();
        Assert.Single(callback.Calls);
        Assert.False(fixture.Scheduler.IsDrainingUserTasks);
    }

    [Fact]
    public void DisposeCancelsTimersAndDropsQueuedCallbacks()
    {
        var fixture = new SchedulerFixture();
        var callback = new RecordingCallback();
        fixture.Scheduler.SetTimeout(callback, 1);
        fixture.Scheduler.RequestAnimationFrame(callback);
        fixture.Scheduler.EnqueueUserTask("queued", () => callback.Invoke(null));

        fixture.Scheduler.Dispose();
        fixture.Dispatcher.Scheduled[0].Fire();
        fixture.Frames.Fire(TimeSpan.Zero);
        fixture.Dispatcher.DrainPosted();

        Assert.Empty(callback.Calls);
        Assert.True(fixture.Dispatcher.Scheduled[0].IsCancellationRequested);
        Assert.Equal(0, fixture.Scheduler.PendingTimerCount);
        Assert.Equal(0, fixture.Scheduler.PendingAnimationFrameCount);
    }

    private sealed class SchedulerFixture :
        WebSceneJavaScriptTaskSchedulerHost<SchedulerFixture>,
        IWebSceneJavaScriptHost,
        IDisposable
    {
        private bool _disposed;

        public SchedulerFixture()
        {
            Dispatcher = new RecordingDispatcher();
            Frames = new RecordingFrames();
        }

        public bool EngineExecuting { get; set; }

        protected override bool IsJavaScriptEngineExecuting => EngineExecuting;

        protected override bool IsJavaScriptTaskHostDisposed => _disposed;

        protected override bool TraceAnimationFrameCallbacks => false;

        protected override IWebSceneDispatcher JavaScriptDispatcher => Dispatcher;

        protected override IWebSceneFrameScheduler JavaScriptFrames => Frames;

        public SchedulerFixture Host => this;

        public SchedulerFixture Scheduler => this;

        public RecordingDispatcher Dispatcher { get; }

        public RecordingFrames Frames { get; }

        public IWebSceneJavaScriptDocument Document { get; } = new RecordingDocument();

        public object BrowserWindow { get; } = new();

        public Type UrlBackendType => typeof(object);

        public string ScriptBaseDirectory { get; set; } = string.Empty;

        public IExternalJavaScriptCallbackAdapter? ExternalCallbackAdapter { get; set; }

        public IExternalVirtualBrowsingContextFactory? ExternalVirtualBrowsingContextFactory { get; set; }

        public IDisposable EnterExternalJavaScriptCall() => EmptyDisposable.Instance;

        public double GetPerformanceTimestamp() => 0;

        public void ExecuteExternalClassicScript(string specifier, Action<string, string> evaluator)
            => throw new NotSupportedException();

        public void ExecuteExternalInlineClassicScript(object currentScript, Action evaluator)
            => evaluator();

        public ExternalJavaScriptSource ResolveExternalScript(string specifier, string? referrerDirectory = null)
            => throw new NotSupportedException();

        public object CreateCanvasPath(object? path) => new();

        public void Dispose()
        {
            DisposeJavaScriptTaskScheduler();
            _disposed = true;
        }
    }

    private sealed class RecordingDispatcher : IWebSceneDispatcher
    {
        private readonly Queue<Action> _posted = new();

        public List<RecordingWork> Scheduled { get; } = [];

        public bool CheckAccess() => true;

        public void VerifyAccess()
        {
        }

        public void Post(Action callback, WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default)
            => _posted.Enqueue(callback);

        public IWebSceneScheduledWork Schedule(
            TimeSpan delay,
            Action callback,
            WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default)
        {
            var work = new RecordingWork(delay, callback);
            Scheduled.Add(work);
            return work;
        }

        public void DrainPosted()
        {
            while (_posted.TryDequeue(out var callback))
            {
                callback();
            }
        }
    }

    private sealed class RecordingWork : IWebSceneScheduledWork
    {
        private readonly Action _callback;

        public RecordingWork(TimeSpan delay, Action callback)
        {
            Delay = delay;
            _callback = callback;
        }

        public TimeSpan Delay { get; }

        public bool IsCancellationRequested { get; private set; }

        public void Fire()
        {
            if (!IsCancellationRequested)
            {
                _callback();
            }
        }

        public void Cancel() => IsCancellationRequested = true;

        public void Dispose() => Cancel();
    }

    private sealed class RecordingFrames : IWebSceneFrameScheduler
    {
        private long _sequence;

        public Dictionary<long, Action<TimeSpan>> Pending { get; } = [];

        public WebSceneFrameRequest RequestFrame(Action<TimeSpan> callback)
        {
            var id = ++_sequence;
            Pending[id] = callback;
            return new WebSceneFrameRequest(id);
        }

        public bool CancelFrame(WebSceneFrameRequest request) => Pending.Remove(request.Value);

        public void Fire(TimeSpan timestamp)
        {
            var callback = Pending.First().Value;
            Pending.Remove(Pending.First().Key);
            callback(timestamp);
        }
    }

    private sealed class RecordingCallback : IExternalJavaScriptCallback
    {
        public List<(object? ThisValue, object?[] Arguments)> Calls { get; } = [];

        public void Invoke(object? thisValue, params object?[] arguments)
            => Calls.Add((thisValue, arguments));
    }

    private sealed class RecordingDocument : IWebSceneJavaScriptDocument
    {
        public object JavaScriptObject => this;
        public object Location { get; } = new();
        public IExternalDomEventListenerAdapter? ExternalEventListenerAdapter { get; set; }
        public object? ExternalWindowContext { get; set; }
        public IExternalWindowEventDispatcher? ExternalWindowEventDispatcher { get; set; }
        public object ParseMarkupDocument(string markup, string mimeType) => new();
        public void DetachExternalBrowsingContext(object frameElement, IExternalVirtualBrowsingContext context)
        {
        }
    }

    private sealed class EmptyDisposable : IDisposable
    {
        public static EmptyDisposable Instance { get; } = new();
        public void Dispose()
        {
        }
    }
}

using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeRuntimeDiagnosticsTests
{
    [Fact]
    public async Task FlushHasDeadlineAndIncludesReservedTerminalRecord()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        using var release = new ManualResetEventSlim();
        diagnostics.Begin();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        diagnostics.ConsoleMessage += _ => { entered.TrySetResult(); release.Wait(TimeSpan.FromSeconds(5)); };
        var terminalDelivered = false;
        diagnostics.RuntimeFailed += _ => terminalDelivered = true;
        diagnostics.Receive(diagnostics.Generation, Record("console"));
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        try
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromMilliseconds(50));
            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => diagnostics.FlushAsync(deadline.Token));
            for (var i = 0; i < 300; i++) diagnostics.Receive(diagnostics.Generation, Record("resource-failure", i + 2));
            diagnostics.Fail("terminal", null, "runtime", null);
            diagnostics.Detach();
        }
        finally { release.Set(); }
        using var flushDeadline = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await diagnostics.FlushAsync(flushDeadline.Token);
        Assert.True(terminalDelivered);
    }

    [NativeRuntimeFact]
    public async Task CaughtHttpFailureReachesHostWithStatusAndRedactedUrlWithoutConsole()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var engine = NativeWebSceneApi.EngineCreate(0, null, new FailingResourceLoader(), _ => { });
        using var diagnostics = new NativeRuntimeDiagnostics();
        try
        {
            diagnostics.Begin();
            var received = new TaskCompletionSource<WebSceneResourceFailure>(TaskCreationOptions.RunContinuationsAsynchronously);
            diagnostics.ResourceFailed += failure => received.TrySetResult(failure);
            diagnostics.Attach(engine);
            diagnostics.Ready();
            Assert.True(NativeWebSceneApi.TryExecuteScript(engine,
                "fetch('https://resource.test/missing?token=secret#private').catch(()=>{});", "resource-test.js"));
            var failure = await received.Task.WaitAsync(TimeSpan.FromSeconds(10));
            Assert.Equal("https://resource.test/missing", failure.Url);
            Assert.Equal(503, failure.HttpStatus);
            Assert.Equal("http", failure.ErrorCode);
            Assert.Equal("GET", failure.Method);
            Assert.Equal("data", failure.ResourceType);
            Assert.True(failure.Duration >= TimeSpan.Zero);
            Assert.DoesNotContain("secret", JsonSerializer.Serialize(failure));
            Assert.Equal(WebSceneRuntimeState.Ready, diagnostics.State);
            Assert.Null(diagnostics.LastFailure);
        }
        finally { diagnostics.Dispose(); NativeWebSceneApi.EngineDestroy(engine); }
    }

    private sealed class FailingResourceLoader : WebScene.Core.IWebSceneResourceLoader
    {
        public WebScene.Core.WebSceneTextResource LoadText(in WebScene.Core.WebSceneResourceRequest request)
            => throw new HttpRequestException("secret", null, System.Net.HttpStatusCode.ServiceUnavailable);
    }

    [Fact]
    public async Task AcceptedResourceEvidenceSurvivesTerminalCleanupButNotNewGeneration()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        using var release = new ManualResetEventSlim();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var delivered = new TaskCompletionSource<WebSceneResourceFailure>(TaskCreationOptions.RunContinuationsAsynchronously);
        diagnostics.Begin();
        diagnostics.ConsoleMessage += _ => { entered.TrySetResult(); release.Wait(TimeSpan.FromSeconds(5)); };
        diagnostics.ResourceFailed += value => delivered.TrySetResult(value);
        diagnostics.Receive(diagnostics.Generation, Record("console"));
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        try
        {
            diagnostics.Receive(diagnostics.Generation, Record("resource-failure", 2));
            diagnostics.Fail("navigation failed", null, "load", null);
            diagnostics.Detach();
        }
        finally { release.Set(); }
        Assert.Equal(2, (await delivered.Task.WaitAsync(TimeSpan.FromSeconds(5))).Context.Sequence);
        var old = diagnostics.Generation;
        diagnostics.Begin();
        var stale = false;
        diagnostics.ResourceFailed += _ => stale = true;
        diagnostics.Receive(old, Record("resource-failure", 3));
        Assert.False(stale);
    }

    private sealed class NativeRuntimeFactAttribute : FactAttribute
    {
        public NativeRuntimeFactAttribute()
        {
            if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")))
                Skip = "Set WEBSCENE_TEST_NATIVE_LIBRARY to run real native callback integration.";
        }
    }

    [NativeRuntimeFact]
    public async Task RealNativeTimerAndConsoleReachManagedSubscribersWithoutHostInvocation()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var engine = NativeWebSceneApi.EngineCreate(0, null,
            new WebScene.Backends.Avalonia.AvaloniaResourceLoader(), _ => { });
        Assert.NotEqual(IntPtr.Zero, engine);
        using var diagnostics = new NativeRuntimeDiagnostics();
        try {
            diagnostics.Begin();
            var exception = new TaskCompletionSource<WebSceneJavaScriptException>(TaskCreationOptions.RunContinuationsAsynchronously);
            var console = new TaskCompletionSource<WebSceneConsoleMessage>(TaskCreationOptions.RunContinuationsAsynchronously);
            diagnostics.JavaScriptException += error => exception.TrySetResult(error);
            diagnostics.ConsoleMessage += message => console.TrySetResult(message);
            diagnostics.Attach(engine);
            diagnostics.Ready();
            Assert.True(NativeWebSceneApi.TryExecuteScript(engine,
                "console.info('host-visible'); setTimeout(()=>{throw Error('page-initiated')},0)", "real-host.js"));
            Assert.Equal("host-visible", (await console.Task.WaitAsync(TimeSpan.FromSeconds(10))).Message);
            var error = await exception.Task.WaitAsync(TimeSpan.FromSeconds(10));
            Assert.Contains("page-initiated", error.Message);
            Assert.Contains("real-host.js", error.Context.Source);
            diagnostics.CheckForNativeFailure();
            Assert.Null(diagnostics.LastFailure);
            Assert.Equal(WebSceneRuntimeState.Ready, diagnostics.State);
        }
        finally {
            diagnostics.Dispose();
            NativeWebSceneApi.EngineDestroy(engine);
        }
    }

    private static string Record(string kind, long sequence = 1) => JsonSerializer.Serialize(new {
        kind, sequence, timestamp = 1234L, message = "page callback exploded", stack = "at callback (page.js:3:2)",
        source = "page.js", documentUrl = "https://page.test/", frameId = 2, line = 3, column = 2,
        promiseRejection = true, truncated = false, level = "info", stage = "runtime",
        arguments = new[] { new { type = "string", value = "hello" } }
    });

    [Fact]
    public async Task TerminalFailureCancelsStartupBarrierBeforeLifecycleCleanup()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        diagnostics.Begin();
        var waitingForWorker = Task.Delay(Timeout.InfiniteTimeSpan, diagnostics.FailureToken);
        diagnostics.Fail("worker stopped", null, "runtime", null);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => waitingForWorker);
        diagnostics.Detach();
        diagnostics.Begin();
        Assert.False(diagnostics.FailureToken.IsCancellationRequested);
        Assert.Null(diagnostics.LastFailure);
    }

    [Fact]
    public async Task PageExceptionsAreOrderedAndNonfatalAndSubscriberFailuresAreIsolated()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        diagnostics.Begin(); diagnostics.Ready();
        var received = new List<WebSceneJavaScriptException>();
        var complete = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        diagnostics.JavaScriptException += _ => throw new InvalidOperationException("bad logger");
        diagnostics.JavaScriptException += error => { received.Add(error); if (received.Count == 2) complete.SetResult(); };
        diagnostics.Receive(diagnostics.Generation, Record("exception", 1));
        diagnostics.Receive(diagnostics.Generation, Record("exception", 2));
        await complete.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(new long[] { 1, 2 }, received.Select(error => error.Context.Sequence));
        Assert.True(received[0].IsUnhandledPromiseRejection);
        Assert.Equal(2u, received[0].Context.FrameId);
        Assert.Equal(3, received[0].Context.Line);
        Assert.Equal(WebSceneRuntimeState.Ready, diagnostics.State);
        Assert.Null(diagnostics.LastFailure);
    }

    [Fact]
    public async Task SlowLoggerCannotDelayFatalStateAndFatalSurvivesFullDeliveryQueue()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        using var release = new ManualResetEventSlim();
        diagnostics.Begin(); diagnostics.Ready();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failures = 0;
        diagnostics.ConsoleMessage += _ => { entered.TrySetResult(); release.Wait(TimeSpan.FromSeconds(5)); };
        diagnostics.RuntimeFailed += _ => { Interlocked.Increment(ref failures); failed.TrySetResult(); };
        diagnostics.Receive(diagnostics.Generation, Record("console"));
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        try {
            for (var index = 0; index < 300; index++) diagnostics.Receive(diagnostics.Generation, Record("console", index + 2));
            diagnostics.Fail("terminal", "stack", "application", "page");
            diagnostics.Fail("duplicate", null, "runtime", null);
            Assert.Equal(WebSceneRuntimeState.Failed, diagnostics.State);
            Assert.Equal("terminal", diagnostics.LastFailure!.Message);
            Assert.True(diagnostics.DroppedCount > 0);
            diagnostics.Detach();
            Assert.NotNull(diagnostics.LastFailure);
        }
        finally { release.Set(); }
        await failed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1, failures);
    }

    [Fact]
    public async Task SubscriberMayDisposeWithoutDeadlockingAndOldGenerationIsIgnored()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        diagnostics.Begin();
        var old = diagnostics.Generation;
        diagnostics.Fail("old failure", null, "load", null);
        diagnostics.Begin();
        var called = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        diagnostics.JavaScriptException += _ => { diagnostics.Dispose(); called.TrySetResult(); };
        diagnostics.Receive(old, Record("exception"));
        Assert.False(called.Task.IsCompleted);
        Assert.Null(diagnostics.LastFailure);
        diagnostics.Receive(diagnostics.Generation, Record("exception"));
        await called.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(WebSceneRuntimeState.Disposed, diagnostics.State);
    }

    [AvaloniaFact]
    public async Task FatalFallbackIsOptInReplaceableAndRetainsDetailsAfterCleanup()
    {
        await using var view = new NativeWebSceneView(false) { ShowRuntimeFailure = true };
        var replacement = new TextBlock { Text = "Application failure UI" };
        view.RuntimeFailureContentFactory = _ => replacement;
        await view.ReportFatalFailureAsync("terminal", "at page.js:1");
        Assert.Same(replacement, view.Content);
        Assert.Equal("at page.js:1", view.LastFailure!.Stack);
        Assert.Equal(WebSceneRuntimeState.Failed, view.RuntimeState);
        await view.UnloadAsync();
        Assert.NotNull(view.LastFailure);
    }

    [AvaloniaFact]
    public async Task FailureDoesNotShowDetailsUnlessEnabled()
    {
        await using var view = new NativeWebSceneView(false);
        var original = view.Content;
        await view.ReportFatalFailureAsync("terminal");
        Assert.Same(original, view.Content);
        Assert.False(view.CaptureConsoleMessages);
        Assert.False(view.CaptureLegacyConsoleMessages);
    }
}

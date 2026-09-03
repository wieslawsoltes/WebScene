using WebScene.Backends.Native;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class RuntimeDiagnosticsTests
{
    [Fact]
    public async Task UnoUsesOrderedBackgroundFailureDeliveryAndRetainsFailureAfterDetach()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        diagnostics.Begin(); diagnostics.Ready();
        var failed = new TaskCompletionSource<WebSceneRuntimeFailure>(TaskCreationOptions.RunContinuationsAsynchronously);
        diagnostics.RuntimeFailed += _ => throw new InvalidOperationException("logger failure");
        diagnostics.RuntimeFailed += failure => failed.TrySetResult(failure);
        diagnostics.Fail("terminal", "stack", "application", "https://page.test/");
        diagnostics.Detach();
        var record = await failed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Same(record, diagnostics.LastFailure);
        Assert.Equal(WebSceneRuntimeState.Failed, diagnostics.State);
        diagnostics.Begin();
        Assert.Null(diagnostics.LastFailure);
        Assert.Equal(WebSceneRuntimeState.Loading, diagnostics.State);
    }

    [Fact]
    public void ConsoleIsOptInAndDisposalPreventsReuse()
    {
        using var diagnostics = new NativeRuntimeDiagnostics();
        Assert.False(diagnostics.CaptureConsole);
        Assert.False(diagnostics.LegacyConsole);
        diagnostics.Dispose();
        Assert.Equal(WebSceneRuntimeState.Disposed, diagnostics.State);
        Assert.Throws<ObjectDisposedException>(diagnostics.Begin);
    }
}

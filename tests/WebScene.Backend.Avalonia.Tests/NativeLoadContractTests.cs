using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeLoadContractTests
{
    [Fact]
    public void ViewRetainsLegacyAndOptionsLoadOverloads()
    {
        Assert.NotNull(typeof(NativeWebSceneView).GetMethod(
            nameof(NativeWebSceneView.LoadAsync),
            [
                typeof(string),
                typeof(string),
                typeof(string),
                typeof(CancellationToken)
            ]));
        Assert.NotNull(typeof(NativeWebSceneView).GetMethod(
            nameof(NativeWebSceneView.LoadAsync),
            [typeof(NativeWebSceneLoadOptions), typeof(CancellationToken)]));
    }

    [Fact]
    public void SharedLoadValidationRejectsInvalidScripts()
    {
        var options = new NativeWebSceneLoadOptions
        {
            Source = "https://example.test/index.html",
            NativeLibraryPath = "/tmp/webscene-native",
            DocumentStartScripts =
            [
                new WebSceneDocumentScript(
                    "globalThis.__ready = true;",
                    "document-start.js"),
                new WebSceneDocumentScript(" ", "invalid.js")
            ]
        };

        Assert.Throws<ArgumentException>(
            () => NativeWebSceneApi.ValidateLoadOptions(options));
    }

    [Fact]
    public void DocumentScriptDefaultsToAllFrames()
    {
        var script = new WebSceneDocumentScript("void 0;", "default.js");

        Assert.True(script.AllFrames);
    }

    [Fact]
    public async Task DisposeCancelsNavigationBeforeWaitingForLifecycleGate()
    {
        using var gate = new SemaphoreSlim(1, 1);
        using var lifetime = new CancellationTokenSource();
        await gate.WaitAsync();
        var unloaded = false;

        var dispose = NativeWebSceneViewLifecycle.DisposeAsync(
            lifetime,
            gate,
            () =>
            {
                unloaded = true;
                return Task.CompletedTask;
            }).AsTask();

        Assert.True(SpinWait.SpinUntil(
            () => lifetime.IsCancellationRequested,
            TimeSpan.FromSeconds(1)));
        Assert.False(dispose.IsCompleted);
        Assert.False(unloaded);
        gate.Release();
        await dispose.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.True(unloaded);
    }

    [Fact]
    public async Task DisposeCancelsReplacementNavigationCreatedWhileGateIsHeld()
    {
        using var gate = new SemaphoreSlim(1, 1);
        using var lifetime = new CancellationTokenSource();
        await gate.WaitAsync();

        var dispose = NativeWebSceneViewLifecycle.DisposeAsync(
            lifetime,
            gate,
            () => Task.CompletedTask).AsTask();

        Assert.True(SpinWait.SpinUntil(
            () => lifetime.IsCancellationRequested,
            TimeSpan.FromSeconds(1)));
        using var replacement =
            NativeWebSceneViewLifecycle.CreateNavigationCancellation(
                CancellationToken.None,
                lifetime.Token);
        Assert.True(replacement.IsCancellationRequested);
        Assert.False(dispose.IsCompleted);

        gate.Release();
        await dispose.WaitAsync(TimeSpan.FromSeconds(2));
    }
}

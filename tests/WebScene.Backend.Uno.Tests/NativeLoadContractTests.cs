using WebScene.Backends.Native;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class NativeLoadContractTests
{
    [Fact]
    public void OrdinaryUnoViewDoesNotOwnInspectorLifetimeState()
    {
        var fields = typeof(UnoNativeWebSceneView)
            .GetFields(System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic)
            .Select(field => field.Name)
            .ToArray();

        Assert.DoesNotContain("_lifetimeCancellation", fields);
        Assert.DoesNotContain("_navigationCancellation", fields);
        Assert.DoesNotContain("_disposeTask", fields);
    }

    [Fact]
    public void ViewRetainsLegacyAndOptionsLoadOverloads()
    {
        Assert.NotNull(typeof(UnoNativeWebSceneView).GetMethod(
            nameof(UnoNativeWebSceneView.LoadAsync),
            [
                typeof(string),
                typeof(string),
                typeof(string),
                typeof(CancellationToken)
            ]));
        Assert.NotNull(typeof(UnoNativeWebSceneView).GetMethod(
            nameof(UnoNativeWebSceneView.LoadAsync),
            [typeof(NativeWebSceneLoadOptions), typeof(CancellationToken)]));
    }

    [Fact]
    public async Task DisposeCancelsNavigationBeforeWaitingForLifecycleGate()
    {
        using var gate = new SemaphoreSlim(1, 1);
        using var lifetime = new CancellationTokenSource();
        await gate.WaitAsync();
        var unloaded = false;

        var dispose = UnoNativeWebSceneLifecycle.DisposeAsync(
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

        var dispose = UnoNativeWebSceneLifecycle.DisposeAsync(
            lifetime,
            gate,
            () => Task.CompletedTask).AsTask();

        Assert.True(SpinWait.SpinUntil(
            () => lifetime.IsCancellationRequested,
            TimeSpan.FromSeconds(1)));
        using var replacement =
            UnoNativeWebSceneLifecycle.CreateNavigationCancellation(
                CancellationToken.None,
                lifetime.Token);
        Assert.True(replacement.IsCancellationRequested);
        Assert.False(dispose.IsCompleted);

        gate.Release();
        await dispose.WaitAsync(TimeSpan.FromSeconds(2));
    }
}

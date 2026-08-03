using WebScene.Backends.Native;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class NativeLoadContractTests
{
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
        using var navigation = new CancellationTokenSource();
        await gate.WaitAsync();
        var unloaded = false;

        var dispose = UnoNativeWebSceneLifecycle.DisposeAsync(
            navigation,
            gate,
            () =>
            {
                unloaded = true;
                return Task.CompletedTask;
            }).AsTask();

        Assert.True(SpinWait.SpinUntil(
            () => navigation.IsCancellationRequested,
            TimeSpan.FromSeconds(1)));
        Assert.False(dispose.IsCompleted);
        Assert.False(unloaded);
        gate.Release();
        await dispose.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.True(unloaded);
    }
}

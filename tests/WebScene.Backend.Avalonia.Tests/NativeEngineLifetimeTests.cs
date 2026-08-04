using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeEngineLifetimeTests
{
    [Fact]
    public void OrdinaryViewAndResourceBridgeDoNotOwnInspectorLifetimeState()
    {
        var viewFields = typeof(NativeWebSceneView)
            .GetFields(System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic)
            .Select(field => field.Name)
            .ToArray();
        Assert.DoesNotContain("_lifetimeCancellation", viewFields);
        Assert.DoesNotContain("_unloadCancellation", viewFields);
        Assert.DoesNotContain("_disposeTask", viewFields);

        var resourceBridge = typeof(NativeWebSceneApi)
            .GetNestedType(
                "ResourceBridge",
                System.Reflection.BindingFlags.NonPublic);
        Assert.NotNull(resourceBridge);
        var bridgeFields = resourceBridge!
            .GetFields(System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic)
            .Select(field => field.Name)
            .ToArray();
        Assert.DoesNotContain("_inspectorSessions", bridgeFields);
        Assert.DoesNotContain(
            bridgeFields,
            field => field.Contains("InspectorLifetime", StringComparison.Ordinal));

        var callbackRegistration = typeof(NativeWebSceneApi).GetNestedType(
            "InspectorCallbackRegistration",
            System.Reflection.BindingFlags.NonPublic);
        Assert.NotNull(callbackRegistration);
        Assert.False(callbackRegistration!.Attributes.HasFlag(
            System.Reflection.TypeAttributes.BeforeFieldInit));
    }

    [Fact]
    public async Task EngineDestroyWaitsForConcurrentInspectorSendLease()
    {
        var lifetime = new NativeEngineLifetime();
        using var sendEntered = new ManualResetEventSlim();
        using var releaseSend = new ManualResetEventSlim();
        var send = Task.Run(() =>
        {
            Assert.True(lifetime.TryEnter());
            try
            {
                sendEntered.Set();
                Assert.True(releaseSend.Wait(TimeSpan.FromSeconds(5)));
            }
            finally
            {
                lifetime.Exit();
            }
        });
        Assert.True(sendEntered.Wait(TimeSpan.FromSeconds(5)));

        var destroy = Task.Run(lifetime.BeginClosingAndWait);
        Assert.True(SpinWait.SpinUntil(
            () => lifetime.IsClosing,
            TimeSpan.FromSeconds(5)));

        Assert.False(destroy.IsCompleted);
        Assert.False(lifetime.TryEnter());

        releaseSend.Set();
        await Task.WhenAll(send, destroy).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.False(lifetime.TryEnter());
    }

    [Fact]
    public async Task ConcurrentInspectorCallsCannotEnterAfterDestroyStarts()
    {
        var lifetime = new NativeEngineLifetime();
        Assert.True(lifetime.TryEnter());
        var destroy = Task.Run(lifetime.BeginClosingAndWait);
        Assert.True(SpinWait.SpinUntil(
            () => lifetime.IsClosing,
            TimeSpan.FromSeconds(5)));

        var enteredAfterDestroy = 0;
        var senders = Enumerable.Range(0, 8).Select(_ => Task.Run(() =>
        {
            for (var attempt = 0; attempt < 10_000; attempt++)
            {
                if (!lifetime.TryEnter()) continue;
                try
                {
                    Interlocked.Increment(ref enteredAfterDestroy);
                }
                finally
                {
                    lifetime.Exit();
                }
            }
        })).ToArray();

        lifetime.Exit();
        await Task.WhenAll(senders.Append(destroy)).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(0, enteredAfterDestroy);
        Assert.False(lifetime.TryEnter());
    }
}

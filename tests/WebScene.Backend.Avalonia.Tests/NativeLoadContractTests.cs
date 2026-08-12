using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Native;
using WebScene.Core;
using System.Runtime.InteropServices;
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
    public void SharedLoadValidationRejectsUnknownCapabilityBits()
    {
        var options = new NativeWebSceneLoadOptions
        {
            Source = "https://example.test/index.html",
            NativeLibraryPath = "/tmp/webscene-native",
            RequiredCapabilities = (WebSceneBackendCapabilities)(1UL << 63)
        };

        Assert.Throws<ArgumentOutOfRangeException>(
            () => NativeWebSceneApi.ValidateLoadOptions(options));
    }

    [Fact]
    public void GpuProviderResolutionUsesExplicitPathWithoutProbing()
    {
        var explicitPath = Path.Combine(
            Path.GetTempPath(),
            "webscene-test-provider",
            NativeWebSceneGpuRuntime.LibraryFileName);

        Assert.Equal(
            Path.GetFullPath(explicitPath),
            NativeWebSceneGpuRuntime.ResolveLibraryPath(
                "/tmp/libwebscene_native_engine.dylib",
                explicitPath));
    }

    [Fact]
    public void GpuProviderResolutionReturnsNullWhenAdjacentProviderIsAbsent()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            $"webscene-missing-gpu-{Guid.NewGuid():N}");

        Assert.Null(NativeWebSceneGpuRuntime.ResolveLibraryPath(
            Path.Combine(directory, "libwebscene_native_engine.dylib"),
            null));
    }

    [Fact]
    public void SceneAbi3LayoutsMatchTheNativeContract()
    {
        Assert.Equal(152, Marshal.SizeOf<NativeSceneView>());
        Assert.Equal(80, Marshal.SizeOf<NativeExternalTexture>());
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
                lifetime.Token,
                CancellationToken.None);
        Assert.True(replacement.IsCancellationRequested);
        Assert.False(dispose.IsCompleted);

        gate.Release();
        await dispose.WaitAsync(TimeSpan.FromSeconds(2));
    }

    [Fact]
    public void ExplicitUnloadCancelsReplacementNavigationCreatedAfterRequest()
    {
        using var lifetime = new CancellationTokenSource();
        using var unload = new CancellationTokenSource();

        unload.Cancel();
        using var replacement =
            NativeWebSceneViewLifecycle.CreateNavigationCancellation(
                CancellationToken.None,
                lifetime.Token,
                unload.Token);

        Assert.True(replacement.IsCancellationRequested);
    }
}

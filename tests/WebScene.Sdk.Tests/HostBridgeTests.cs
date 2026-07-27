using System.Text.Json;
using WebScene.Sdk;
using Xunit;

namespace WebScene.Sdk.Tests;

public sealed class HostBridgeTests
{
    [Fact]
    public async Task BridgeRequiresDeclarationAndGrantThenReturnsJson()
    {
        var diagnostics = new WebSceneDiagnosticCollector();
        var handler = new WebSceneDelegateCapabilityHandler(
            WebSceneComponentCapabilities.Commands,
            (method, arguments, _) =>
            {
                Assert.Equal("save", method);
                return ValueTask.FromResult<JsonElement?>(JsonSerializer.SerializeToElement(new
                {
                    saved = arguments.GetProperty("id").GetInt32()
                }));
            });
        var bridge = new WebSceneHostBridge(ComponentManifestTests.CreateManifest(), [handler], diagnostics);
        var response = await bridge.InvokeAsync(Request(WebSceneComponentCapabilities.Commands));

        Assert.True(response.Ok);
        Assert.Equal(42, response.Result!.Value.GetProperty("saved").GetInt32());
        Assert.Contains(diagnostics.Diagnostics, static value => value.Code == "bridge.completed");

        var notDeclared = await bridge.InvokeAsync(Request(WebSceneComponentCapabilities.FileSelection));
        Assert.False(notDeclared.Ok);
        Assert.Equal("bridge.capability.denied", notDeclared.Error!.Code);
    }

    [Fact]
    public async Task BridgeReportsUnavailableHandlerExceptionsVersionsAndCancellation()
    {
        var manifest = ComponentManifestTests.CreateManifest() with
        {
            Capabilities = [WebSceneComponentCapabilities.Dom, WebSceneComponentCapabilities.Settings]
        };
        var bridge = new WebSceneHostBridge(manifest, []);
        var unavailable = await bridge.InvokeAsync(Request(WebSceneComponentCapabilities.Settings));
        Assert.Equal("bridge.capability.unavailable", unavailable.Error!.Code);

        var wrongVersion = await bridge.InvokeAsync(Request(WebSceneComponentCapabilities.Settings) with { Version = "2.0" });
        Assert.Equal("bridge.version", wrongVersion.Error!.Code);

        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        var cancelling = new WebSceneHostBridge(manifest,
        [
            new WebSceneDelegateCapabilityHandler(
                WebSceneComponentCapabilities.Settings,
                (_, _, token) =>
                {
                    token.ThrowIfCancellationRequested();
                    return ValueTask.FromResult<JsonElement?>(null);
                })
        ]);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await cancelling.InvokeAsync(Request(WebSceneComponentCapabilities.Settings), cancellation.Token));
    }

    private static WebSceneHostBridgeRequest Request(string capability) => new(
        "request-1",
        WebSceneHostBridge.CurrentVersion,
        capability,
        "save",
        JsonSerializer.SerializeToElement(new { id = 42 }));
}

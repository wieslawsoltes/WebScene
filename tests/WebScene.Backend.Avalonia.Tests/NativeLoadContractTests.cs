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
}

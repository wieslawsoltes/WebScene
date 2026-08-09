using System.Text;
using WebScene.Core;
using WebScene.Sdk.NativeHost.Internal;
using Xunit;

namespace WebScene.Sdk.Avalonia.Tests;

public sealed class WebSceneComponentResourceLoaderTests : IDisposable
{
    private readonly string _directory = Path.Combine(
        Path.GetTempPath(),
        "webscene-sdk-avalonia-tests",
        Guid.NewGuid().ToString("N"));

    [Fact]
    public void ServesShellAndDeclaredAssetFromIsolatedOrigin()
    {
        var loader = CreateLoader();

        var document = loader.LoadText(new WebSceneResourceRequest(
            loader.DocumentUrl,
            null,
            WebSceneResourceKind.Markup));
        var script = loader.LoadText(new WebSceneResourceRequest(
            "dist/main.js",
            loader.DocumentUrl,
            WebSceneResourceKind.Script));

        Assert.Contains("<!doctype html>", document.Content, StringComparison.Ordinal);
        Assert.Equal("globalThis.mount = () => {};", script.Content);
        Assert.StartsWith(
            loader.DocumentUrl[..loader.DocumentUrl.LastIndexOf('/')],
            script.CacheKey,
            StringComparison.Ordinal);
        Assert.NotNull(script.EntityTag);
    }

    [Fact]
    public void ReturnsNotModifiedForMatchingAssetEntityTag()
    {
        var loader = CreateLoader();
        var first = loader.LoadText(new WebSceneResourceRequest(
            "dist/main.js",
            loader.DocumentUrl,
            WebSceneResourceKind.Script));
        var second = loader.LoadText(new WebSceneResourceRequest(
            "dist/main.js",
            loader.DocumentUrl,
            WebSceneResourceKind.Script)
        {
            IfNoneMatch = first.EntityTag
        });

        Assert.True(second.NotModified);
        Assert.Empty(second.Content);
        Assert.Equal(first.EntityTag, second.EntityTag);
    }

    [Fact]
    public void RejectsCrossOriginAndUndeclaredAssets()
    {
        var loader = CreateLoader();

        Assert.Throws<UnauthorizedAccessException>(() => loader.LoadText(
            new WebSceneResourceRequest(
                "https://example.com/main.js",
                loader.DocumentUrl,
                WebSceneResourceKind.Script)));
        Assert.Throws<FileNotFoundException>(() => loader.LoadText(
            new WebSceneResourceRequest(
                "not-declared.js",
                loader.DocumentUrl,
                WebSceneResourceKind.Script)));
    }

    [Fact]
    public void RejectsNonUtf8TextAssets()
    {
        Directory.CreateDirectory(Path.Combine(_directory, "dist"));
        File.WriteAllText(
            Path.Combine(_directory, "webscene-component.json"),
            ManifestJson(),
            Encoding.UTF8);
        File.WriteAllBytes(
            Path.Combine(_directory, "dist", "main.js"),
            [0xff, 0xfe]);
        var package = WebSceneComponentPackage.Open(
            _directory,
            new WebSceneSharedAssetCache());
        var loader = new WebSceneComponentResourceLoader(package, Guid.NewGuid());

        Assert.Throws<InvalidDataException>(() => loader.LoadText(
            new WebSceneResourceRequest(
                "dist/main.js",
                loader.DocumentUrl,
                WebSceneResourceKind.Script)));
    }

    public void Dispose()
    {
        if (Directory.Exists(_directory))
        {
            Directory.Delete(_directory, recursive: true);
        }
    }

    private WebSceneComponentResourceLoader CreateLoader()
    {
        Directory.CreateDirectory(Path.Combine(_directory, "dist"));
        File.WriteAllText(
            Path.Combine(_directory, "webscene-component.json"),
            ManifestJson(),
            Encoding.UTF8);
        File.WriteAllText(
            Path.Combine(_directory, "dist", "main.js"),
            "globalThis.mount = () => {};",
            new UTF8Encoding(false));
        var package = WebSceneComponentPackage.Open(
            _directory,
            new WebSceneSharedAssetCache());
        return new WebSceneComponentResourceLoader(package, Guid.NewGuid());
    }

    private static string ManifestJson() => """
        {
          "schemaVersion": "1.0",
          "id": "com.example.test",
          "displayName": "Test",
          "version": "1.0.0",
          "profileVersion": "1.0",
          "entryPoint": "dist/main.js",
          "assets": ["dist/main.js"],
          "capabilities": ["dom"],
          "lifecycle": { "mountExport": "mount", "unmountExport": "unmount" }
        }
        """;
}

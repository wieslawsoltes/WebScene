using WebScene.Sdk;
using Xunit;

namespace WebScene.Sdk.Tests;

public sealed class ComponentManifestTests
{
    [Fact]
    public void ManifestRoundTripsAndRejectsUnknownOrUnsafeValues()
    {
        var manifest = CreateManifest();
        var roundTrip = WebSceneComponentManifestSerializer.Parse(
            WebSceneComponentManifestSerializer.Serialize(manifest));

        Assert.Equal(manifest.Id, roundTrip.Id);
        Assert.Equal(manifest.Version, roundTrip.Version);
        Assert.Equal(manifest.Assets, roundTrip.Assets);
        Assert.Equal(manifest.Capabilities, roundTrip.Capabilities);
        Assert.Equal(manifest.Lifecycle, roundTrip.Lifecycle);
        var result = WebSceneComponentManifestSerializer.Validate(manifest with
        {
            Id = "Bad Id",
            Version = "latest",
            EntryPoint = "../escape.js",
            Assets = ["../escape.js", "../escape.js"],
            Capabilities = ["dom", "unknown"]
        });
        Assert.False(result.IsValid);
        Assert.Contains(result.Errors, static error => error.Contains("reverse-domain", StringComparison.Ordinal));
        Assert.Contains(result.Errors, static error => error.Contains("semantic version", StringComparison.Ordinal));
        Assert.Contains(result.Errors, static error => error.Contains("normalized relative", StringComparison.Ordinal));
        Assert.Contains(result.Errors, static error => error.Contains("duplicate", StringComparison.Ordinal));
        Assert.Contains(result.Errors, static error => error.Contains("Unknown capability", StringComparison.Ordinal));
    }

    [Fact]
    public void PackageSharesImmutableAssetsWhileInstancesKeepIsolatedState()
    {
        using var directory = new TemporaryDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "webscene-component.json"),
            WebSceneComponentManifestSerializer.Serialize(CreateManifest()));
        File.WriteAllText(Path.Combine(directory.Path, "main.js"), "export function mount() {}\n");
        var cache = new WebSceneSharedAssetCache();
        var package = WebSceneComponentPackage.Open(directory.Path, cache);

        var firstAsset = package.GetEntryPoint();
        var secondAsset = package.GetEntryPoint();
        using var first = package.CreateInstance();
        using var second = package.CreateInstance();
        first.Mount();
        second.Mount();
        first.SetState("count", 7);
        second.SetState("count", 2);

        Assert.Same(firstAsset, secondAsset);
        Assert.Equal(firstAsset.Sha256, secondAsset.Sha256);
        Assert.Equal(1, cache.Count);
        Assert.Equal(1, cache.Hits);
        Assert.True(first.TryGetState<int>("count", out var firstCount));
        Assert.True(second.TryGetState<int>("count", out var secondCount));
        Assert.Equal(7, firstCount);
        Assert.Equal(2, secondCount);
        Assert.NotEqual(first.InstanceId, second.InstanceId);
    }

    internal static WebSceneComponentManifest CreateManifest() => new()
    {
        SchemaVersion = WebSceneComponentManifest.CurrentSchemaVersion,
        Id = "dev.webscene.sample",
        DisplayName = "SDK sample",
        Version = "1.0.0",
        ProfileVersion = WebSceneComponentManifest.CurrentProfileVersion,
        EntryPoint = "main.js",
        Assets = ["main.js"],
        Capabilities = [WebSceneComponentCapabilities.Dom, WebSceneComponentCapabilities.Commands]
    };

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "webscene-sdk-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose() => Directory.Delete(Path, recursive: true);
    }
}

using System.Net;
using System.Net.Http.Headers;
using WebScene.Backends.Avalonia;
using WebScene.Core;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class AvaloniaResourceLoaderTests
{
    [Fact]
    public async Task HttpCaptureReplaysTextAndBinaryWithoutOriginFallback()
    {
        var fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixtures");
        var archiveDirectory = Path.Combine(
            Path.GetTempPath(),
            "webscene-resource-archive-tests",
            Guid.NewGuid().ToString("N"));
        try
        {
            var capture = new AvaloniaResourceLoader
            {
                ResourceCaptureDirectory = archiveDirectory
            };
            capture.MountDirectory("https://fixtures.webscene.test/", fixtureDirectory);
            var address = "https://fixtures.webscene.test/module-mutation-observer.js";
            var request = new WebSceneResourceRequest(
                address,
                null,
                WebSceneResourceKind.Script);

            var capturedText = capture.LoadText(request);
            var capturedBinary = await capture.LoadBytesAsync(
                address,
                null,
                CancellationToken.None);
            capture.FlushResourceCapture();

            var incrementalCapture = new AvaloniaResourceLoader
            {
                ResourceCaptureDirectory = archiveDirectory
            };
            incrementalCapture.MountDirectory(
                "https://incremental.fixtures.webscene.test/",
                fixtureDirectory);
            var incrementalAddress =
                "https://incremental.fixtures.webscene.test/module-mutation-observer.js";
            var incrementalResource = incrementalCapture.LoadText(
                new WebSceneResourceRequest(
                    incrementalAddress,
                    null,
                    WebSceneResourceKind.Script));
            incrementalCapture.FlushResourceCapture();

            var replay = new AvaloniaResourceLoader
            {
                ResourceReplayDirectory = archiveDirectory
            };
            replay.PrepareResourceReplay();
            var replayedText = replay.LoadText(request);
            var replayedIncremental = replay.LoadText(
                new WebSceneResourceRequest(
                    incrementalAddress,
                    null,
                    WebSceneResourceKind.Script));
            var replayedBinary = await replay.LoadBytesAsync(
                address,
                null,
                CancellationToken.None);

            Assert.Equal(capturedText.Content, replayedText.Content);
            Assert.Equal(incrementalResource.Content, replayedIncremental.Content);
            Assert.Equal(capturedText.CacheKey, replayedText.CacheKey);
            Assert.Equal(capturedBinary.Content, replayedBinary.Content);
            Assert.Equal(capturedBinary.CacheKey, replayedBinary.CacheKey);
            Assert.Throws<FileNotFoundException>(() => replay.LoadText(
                new WebSceneResourceRequest(
                    "https://fixtures.webscene.test/not-captured.js",
                    null,
                    WebSceneResourceKind.Script)));
            var replayFailure = Assert.Throws<InvalidOperationException>(
                replay.ThrowIfResourceReplayFailed);
            Assert.IsType<FileNotFoundException>(replayFailure.InnerException);
        }
        finally
        {
            if (Directory.Exists(archiveDirectory))
            {
                Directory.Delete(archiveDirectory, recursive: true);
            }
        }
    }

    [Fact]
    public void ResourceCaptureAndReplayAreMutuallyExclusive()
    {
        var loader = new AvaloniaResourceLoader
        {
            ResourceCaptureDirectory = "capture",
            ResourceReplayDirectory = "replay"
        };

        var error = Assert.Throws<InvalidOperationException>(() => loader.LoadText(
            new WebSceneResourceRequest(
                "https://fixtures.webscene.test/app.js",
                null,
                WebSceneResourceKind.Script)));

        Assert.Contains("mutually exclusive", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void MissingReplayManifestIsRememberedAcrossNativeCallbackBoundary()
    {
        var archiveDirectory = Path.Combine(
            Path.GetTempPath(),
            "webscene-resource-archive-tests",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(archiveDirectory);
        try
        {
            var replay = new AvaloniaResourceLoader
            {
                ResourceReplayDirectory = archiveDirectory
            };

            Assert.Throws<FileNotFoundException>(replay.PrepareResourceReplay);
            Assert.Throws<FileNotFoundException>(() => replay.LoadText(
                new WebSceneResourceRequest(
                    "https://fixtures.webscene.test/app.js",
                    null,
                    WebSceneResourceKind.Script)));
            var replayFailure = Assert.Throws<InvalidOperationException>(
                replay.ThrowIfResourceReplayFailed);
            Assert.IsType<FileNotFoundException>(replayFailure.InnerException);
        }
        finally
        {
            Directory.Delete(archiveDirectory, recursive: true);
        }
    }

    [Fact]
    public void HttpResourceFreshnessHonorsOriginPolicyAndBoundedValidatorHeuristics()
    {
        var now = DateTimeOffset.UtcNow;
        using var explicitResponse = new HttpResponseMessage(HttpStatusCode.OK);
        explicitResponse.Headers.Date = now - TimeSpan.FromSeconds(10);
        explicitResponse.Headers.CacheControl = new CacheControlHeaderValue
        {
            MaxAge = TimeSpan.FromMinutes(2)
        };
        var explicitPolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(
            explicitResponse,
            now - TimeSpan.FromDays(30));

        Assert.True(explicitPolicy.IsCacheable);
        Assert.InRange(
            explicitPolicy.FreshUntil!.Value,
            now + TimeSpan.FromSeconds(100),
            now + TimeSpan.FromSeconds(115));

        using var noStoreResponse = new HttpResponseMessage(HttpStatusCode.OK);
        noStoreResponse.Headers.CacheControl = new CacheControlHeaderValue { NoStore = true };
        var noStorePolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(noStoreResponse, now);
        Assert.False(noStorePolicy.IsCacheable);
        Assert.Null(noStorePolicy.FreshUntil);

        using var revalidateResponse = new HttpResponseMessage(HttpStatusCode.OK);
        revalidateResponse.Headers.CacheControl = new CacheControlHeaderValue { NoCache = true };
        var revalidatePolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(revalidateResponse, now);
        Assert.True(revalidatePolicy.IsCacheable);
        Assert.Null(revalidatePolicy.FreshUntil);

        using var validatorOnlyResponse = new HttpResponseMessage(HttpStatusCode.OK);
        validatorOnlyResponse.Headers.Date = now;
        var heuristicPolicy = AvaloniaResourceLoader.ReadHttpCachePolicy(
            validatorOnlyResponse,
            now - TimeSpan.FromDays(30));
        Assert.True(heuristicPolicy.IsCacheable);
        Assert.InRange(
            heuristicPolicy.FreshUntil!.Value,
            now + TimeSpan.FromMinutes(59),
            now + TimeSpan.FromMinutes(61));
    }

    [Fact]
    public void LoaderCoversDataFileRelativeBaseAndFailureContracts()
    {
        var fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixtures");
        var loader = new AvaloniaResourceLoader
        {
            ScriptBaseDirectory = AppContext.BaseDirectory
        };

        var base64 = loader.LoadText(new WebSceneResourceRequest(
            "data:text/plain;base64,cG9ydGFibGU=",
            null,
            WebSceneResourceKind.Data));
        var extensionFallback = loader.LoadText(new WebSceneResourceRequest(
            Path.Combine(fixtureDirectory, "module-mutation-observer"),
            null,
            WebSceneResourceKind.Script));
        var rootedBase = loader.LoadText(new WebSceneResourceRequest(
            "module-mutation-observer.js",
            Path.Combine(fixtureDirectory, "base.js"),
            WebSceneResourceKind.Script));
        var absoluteBase = loader.LoadText(new WebSceneResourceRequest(
            "module-mutation-observer.js",
            new Uri(Path.Combine(fixtureDirectory, "base.js")).AbsoluteUri,
            WebSceneResourceKind.Script));

        Assert.Equal("portable", base64.Content);
        Assert.Equal(extensionFallback.Content, rootedBase.Content);
        Assert.Equal(rootedBase.Content, absoluteBase.Content);
        Assert.Throws<ArgumentException>(() => loader.LoadText(
            new WebSceneResourceRequest(" ", null, WebSceneResourceKind.Data)));
        Assert.Throws<FormatException>(() => loader.LoadText(
            new WebSceneResourceRequest("data:text/plain", null, WebSceneResourceKind.Data)));
        Assert.Throws<FileNotFoundException>(() => loader.LoadText(
            new WebSceneResourceRequest("missing-resource.js", null, WebSceneResourceKind.Script)));
        Assert.Throws<NotSupportedException>(() => loader.LoadText(
            new WebSceneResourceRequest("ftp://webscene.invalid/file.js", null, WebSceneResourceKind.Script)));
    }
}

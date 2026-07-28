using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Text;
using WebScene.Backends.Uno.Native;
using WebScene.Core;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class UnoResourceLoaderTests
{
    [Fact]
    public void CachePolicyMatchesAvaloniaFreshnessAndRevalidationRules()
    {
        var now = DateTimeOffset.UtcNow;
        using var explicitResponse = new HttpResponseMessage(HttpStatusCode.OK);
        explicitResponse.Headers.Date = now - TimeSpan.FromSeconds(10);
        explicitResponse.Headers.CacheControl = new CacheControlHeaderValue
        {
            MaxAge = TimeSpan.FromMinutes(2)
        };
        var explicitPolicy = UnoResourceLoader.ReadHttpCachePolicy(
            explicitResponse,
            now - TimeSpan.FromDays(30));

        Assert.True(explicitPolicy.IsCacheable);
        Assert.InRange(
            explicitPolicy.FreshUntil!.Value,
            now + TimeSpan.FromSeconds(100),
            now + TimeSpan.FromSeconds(115));

        using var noStoreResponse = new HttpResponseMessage(HttpStatusCode.OK);
        noStoreResponse.Headers.CacheControl =
            new CacheControlHeaderValue { NoStore = true };
        var noStorePolicy = UnoResourceLoader.ReadHttpCachePolicy(
            noStoreResponse,
            now);
        Assert.False(noStorePolicy.IsCacheable);
        Assert.Null(noStorePolicy.FreshUntil);

        using var validatorOnlyResponse =
            new HttpResponseMessage(HttpStatusCode.OK);
        validatorOnlyResponse.Headers.Date = now;
        var heuristicPolicy = UnoResourceLoader.ReadHttpCachePolicy(
            validatorOnlyResponse,
            now - TimeSpan.FromDays(30));
        Assert.InRange(
            heuristicPolicy.FreshUntil!.Value,
            now + TimeSpan.FromMinutes(59),
            now + TimeSpan.FromMinutes(61));
    }

    [Fact]
    public async Task HttpLoaderSendsIdentityAndValidatorsAndPreserves304()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        var receivedHeaders = string.Empty;
        var server = Task.Run(async () =>
        {
            using var client = await listener.AcceptTcpClientAsync();
            await using var stream = client.GetStream();
            receivedHeaders = await ReadHeadersAsync(stream);
            var response = Encoding.ASCII.GetBytes(
                "HTTP/1.1 304 Not Modified\r\n"
                + "ETag: \"uno-cache\"\r\n"
                + "Last-Modified: Mon, 28 Jul 2025 12:00:00 GMT\r\n"
                + "Cache-Control: max-age=120\r\n"
                + "Content-Length: 0\r\n"
                + "Connection: close\r\n\r\n");
            await stream.WriteAsync(response);
        });

        var modified = new DateTimeOffset(
            2025,
            7,
            28,
            12,
            0,
            0,
            TimeSpan.Zero);
        var loader = new UnoResourceLoader();
        var resource = loader.LoadText(
            new WebSceneResourceRequest(
                $"http://127.0.0.1:{endpoint.Port}/bundle.js",
                null,
                WebSceneResourceKind.Script)
            {
                IfNoneMatch = "\"uno-cache\"",
                IfModifiedSince = modified
            });
        await server;

        Assert.True(resource.NotModified);
        Assert.Equal("\"uno-cache\"", resource.EntityTag);
        Assert.True(resource.FreshUntil > DateTimeOffset.UtcNow);
        Assert.Contains(
            "User-Agent: WebScene-Uno/0.1",
            receivedHeaders,
            StringComparison.OrdinalIgnoreCase);
        Assert.Contains(
            "If-None-Match: \"uno-cache\"",
            receivedHeaders,
            StringComparison.OrdinalIgnoreCase);
        Assert.Contains(
            "If-Modified-Since:",
            receivedHeaders,
            StringComparison.OrdinalIgnoreCase);
    }

    private static async Task<string> ReadHeadersAsync(NetworkStream stream)
    {
        var bytes = new List<byte>();
        var buffer = new byte[1];
        while (bytes.Count < 32 * 1024)
        {
            if (await stream.ReadAsync(buffer) == 0)
            {
                break;
            }
            bytes.Add(buffer[0]);
            if (bytes.Count >= 4
                && bytes[^4] == '\r'
                && bytes[^3] == '\n'
                && bytes[^2] == '\r'
                && bytes[^1] == '\n')
            {
                break;
            }
        }
        return Encoding.ASCII.GetString([.. bytes]);
    }
}

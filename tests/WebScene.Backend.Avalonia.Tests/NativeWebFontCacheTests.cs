using System.Net;
using System.Net.Sockets;
using System.Text;
using SkiaSharp;
using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

[CollectionDefinition("Native web-font cache", DisableParallelization = true)]
public sealed class NativeWebFontCacheCollection;

[Collection("Native web-font cache")]
public sealed class NativeWebFontCacheTests
{
    private sealed class NativeRuntimeFactAttribute : FactAttribute
    {
        public NativeRuntimeFactAttribute()
        {
            if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")))
                Skip = "Set WEBSCENE_TEST_NATIVE_LIBRARY to exercise native stylesheet caching.";
        }
    }

    [NativeRuntimeFact]
    public async Task CachedAndInlineStylesheetsRegisterFontsInEveryDocument()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var cache = Directory.CreateTempSubdirectory("webscene-webfont-test-").FullName;
        using var portReservation = new TcpListener(IPAddress.Loopback, 0);
        portReservation.Start();
        var port = ((IPEndPoint)portReservation.LocalEndpoint).Port;
        portReservation.Stop();
        using var listener = new HttpListener();
        var origin = $"http://127.0.0.1:{port}/";
        listener.Prefixes.Add(origin);
        listener.Start();
        using var stop = new CancellationTokenSource();
        var font = ReadPlatformFont();
        var stylesheetRequests = 0;
        var fontRequests = 0;
        var server = Task.Run(async () =>
        {
            while (!stop.IsCancellationRequested)
            {
                HttpListenerContext context;
                try { context = await listener.GetContextAsync().WaitAsync(stop.Token); }
                catch (OperationCanceledException) { break; }
                var path = context.Request.Url!.AbsolutePath;
                byte[] bytes;
                if (path == "/font.ttf")
                {
                    Interlocked.Increment(ref fontRequests);
                    context.Response.ContentType = "font/ttf";
                    bytes = font;
                }
                else if (path == "/style.css")
                {
                    Interlocked.Increment(ref stylesheetRequests);
                    context.Response.ContentType = "text/css";
                    bytes = Encoding.UTF8.GetBytes("@font-face{font-family:CachedFace;src:url('font.ttf')} body{font-family:CachedFace}");
                }
                else
                {
                    context.Response.ContentType = "text/html";
                    bytes = Encoding.UTF8.GetBytes("<!doctype html><link rel='stylesheet' href='style.css'><style>@font-face{font-family:InlineFace;src:url('font.ttf')}</style><p>Release notes</p>");
                }
                context.Response.Headers["Cache-Control"] = "public, max-age=3600";
                context.Response.ContentLength64 = bytes.Length;
                await context.Response.OutputStream.WriteAsync(bytes);
                context.Response.Close();
            }
        });
        try
        {
            for (var pass = 0; pass < 2; pass++)
            {
                var engine = NativeWebSceneApi.EngineCreate(0, cache, new AvaloniaResourceLoader(), _ => { });
                Assert.NotEqual(IntPtr.Zero, engine);
                try
                {
                    Assert.True(NativeWebSceneApi.TryLoadUrl(engine, origin));
                    var registry = NativeWebSceneApi.GetWebTypefaceRegistry(engine)!;
                    var deadline = DateTime.UtcNow.AddSeconds(10);
                    while ((!registry.Contains("CachedFace") || !registry.Contains("InlineFace")) && DateTime.UtcNow < deadline)
                        await Task.Delay(20);
                    Assert.True(registry.Contains("CachedFace"), $"External font missing on pass {pass}");
                    Assert.True(registry.Contains("InlineFace"), $"Inline font missing on pass {pass}");
                    Assert.True(registry.TryResolve("CachedFace", out var face));
                    Assert.False(string.IsNullOrEmpty(face.FamilyName));
                    if (pass == 1) Assert.True(NativeWebSceneApi.GetResourceCacheMetrics(engine).Hits > 0);
                }
                finally { NativeWebSceneApi.EngineDestroy(engine); }
            }
            Assert.Equal(1, stylesheetRequests); // Warm document really bypassed the managed CSS loader.
            Assert.True(fontRequests >= 2); // Each document rebuilt its family map.
        }
        finally
        {
            stop.Cancel();
            await server;
            listener.Stop();
            Directory.Delete(cache, recursive: true);
        }
    }

    private static byte[] ReadPlatformFont()
    {
        var root = OperatingSystem.IsWindows()
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Fonts")
            : OperatingSystem.IsMacOS() ? "/System/Library/Fonts" : "/usr/share/fonts";
        foreach (var path in Directory.EnumerateFiles(root, "*.*", SearchOption.AllDirectories)
                     .Where(path => path.EndsWith(".ttf", StringComparison.OrdinalIgnoreCase)
                         || path.EndsWith(".otf", StringComparison.OrdinalIgnoreCase)))
        {
            var bytes = File.ReadAllBytes(path);
            using var data = SKData.CreateCopy(bytes);
            using var face = SKTypeface.FromData(data);
            if (face != null) return bytes;
        }
        throw new InvalidOperationException("No platform OpenType font available.");
    }
}

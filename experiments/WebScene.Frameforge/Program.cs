using System.Net;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using WebScene.Backends.Avalonia.Native;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args) => AppBuilder.Configure<StudioApp>()
        .UsePlatformDetect()
        .With(new AvaloniaNativePlatformOptions { RenderingMode = [AvaloniaNativeRenderingMode.Metal] })
        .StartWithClassicDesktopLifetime(args);
}
internal sealed class StudioApp : Application
{
    private AssetServer? _server;
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var root = Environment.GetEnvironmentVariable("FRAMEFORGE_ASSETS")
                ?? Path.Combine(AppContext.BaseDirectory, "Assets");
            _server = new AssetServer(root);
            Console.WriteLine("Asset origin: " + _server.Origin);
            var view = new NativeWebSceneView(true, url => url.StartsWith(_server.Origin, StringComparison.Ordinal));
            long errors = 0;
            view.JavaScriptException += e => { Interlocked.Increment(ref errors); Console.Error.WriteLine($"JavaScript: {e.Message}\n{e.Stack}"); };
            view.RuntimeFailed += e => Console.Error.WriteLine($"Runtime: {e.Message}\n{e.Stack}");
            view.ResourceFailed += e => Console.Error.WriteLine($"Resource: {e.Url}: {e.Message}");
            view.ConsoleMessage += e => {
                if (e.Level == "error") Interlocked.Increment(ref errors);
                Console.WriteLine($"Console {e.Level}: {e.Message}");
            };
            var window = new Window { Title = "Frameforge in WebScene", Width = 1280, Height = 820, Content = view };
            desktop.MainWindow = window;
            desktop.Exit += (_, _) => _server.Dispose();
            window.Opened += async (_, _) =>
            {
                try
                {
                    var mediaVerify = Environment.GetCommandLineArgs().Contains("--media-verify");
                    var mediaDemo = Environment.GetCommandLineArgs().Contains("--media-demo");
                    if (mediaDemo) window.Title = "Video in WebScene";
                    await view.LoadAsync(_server.Origin + (mediaVerify ? "__webscene-media-verify.html" : mediaDemo ? "__webscene-media-demo.html" : "index.html"),
                        Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
                        ?? Path.Combine(AppContext.BaseDirectory, OperatingSystem.IsWindows() ? "webscene_native_engine.dll" : OperatingSystem.IsMacOS() ? "libwebscene_native_engine.dylib" : "libwebscene_native_engine.so"));
                    if (mediaDemo) return;
                    if (mediaVerify)
                    {
                        for (var i = 0; i < 120; i++)
                        {
                            if (await view.EvaluateTextAsync("globalThis.mediaVerification?.complete===true") == "true") break;
                            await Task.Delay(500);
                        }
                        Console.WriteLine("Media verification: " + await view.EvaluateTextAsync("JSON.stringify(globalThis.mediaVerification)"));
                        var passed = await view.EvaluateTextAsync("globalThis.mediaVerification?.passed===true");
                        Console.WriteLine($"Rendered scenes: {view.CapturePerformanceSnapshot().Surface.RenderedScenes}");
                        await view.DisposeAsync();
                        desktop.Shutdown(passed == "true" && Interlocked.Read(ref errors) == 0 ? 0 : 1);
                        return;
                    }
                    await Task.Delay(5000);
                    await view.EvaluateTextAsync("globalThis.frameforge?.ready?.catch(e=>{globalThis.__frameforgeStartupError=String(e.stack||e.message||e);})");
                    if (Environment.GetCommandLineArgs().Contains("--verify")) await Task.Delay(15000);
                    Console.WriteLine("Frameforge status: " + await view.EvaluateTextAsync("JSON.stringify({app:!!globalThis.frameforge,booted:globalThis.frameforge?.booted===true,error:globalThis.__frameforgeStartupError,renderer:globalThis.frameforge?.renderer?.kind,clips:globalThis.frameforge?.editor?.project?.clips?.length,media:typeof HTMLVideoElement,audio:typeof AudioContext,externalTexture:typeof globalThis.frameforge?.renderer?.device?.importExternalTexture})"));
                    if (Environment.GetCommandLineArgs().Contains("--verify"))
                    {
                        Console.WriteLine($"JavaScript/console errors: {Interlocked.Read(ref errors)}");
                        Console.WriteLine($"Rendered scenes: {view.CapturePerformanceSnapshot().Surface.RenderedScenes}");
                        var supported = await view.EvaluateTextAsync("globalThis.frameforge?.booted===true&&typeof HTMLVideoElement==='function'&&typeof AudioContext==='function'&&typeof globalThis.frameforge?.renderer?.device?.importExternalTexture==='function'");
                        await view.DisposeAsync();
                        desktop.Shutdown(Interlocked.Read(ref errors) == 0 && supported == "true" ? 0 : 1);
                    }
                }
                catch (Exception e)
                {
                    Console.Error.WriteLine(e);
                    if (Environment.GetCommandLineArgs().Contains("--verify")) desktop.Shutdown(1);
                }
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}
internal sealed class AssetServer : IDisposable
{
    private readonly HttpListener _listener = new();
    private readonly string _root;
    public string Origin { get; }
    public AssetServer(string root)
    {
        _root = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
        if (!File.Exists(Path.Combine(_root, "index.html"))) throw new FileNotFoundException("Frameforge assets are missing.", root);
        // Bind only loopback; random ports let independent app instances coexist.
        for (var attempt = 0; ; attempt++)
        {
            Origin = $"http://127.0.0.1:{Random.Shared.Next(20000, 60000)}/";
            _listener.Prefixes.Clear();
            _listener.Prefixes.Add(Origin);
            try { _listener.Start(); break; }
            catch (HttpListenerException) when (attempt < 20) { }
        }
        _ = Serve();
    }
    private async Task Serve()
    {
        while (_listener.IsListening)
        {
            HttpListenerContext context;
            try { context = await _listener.GetContextAsync(); }
            catch (Exception) when (!_listener.IsListening) { break; }
            _ = Respond(context);
        }
    }
    private async Task Respond(HttpListenerContext context)
    {
        try
        {
            var relative = Uri.UnescapeDataString(context.Request.Url!.AbsolutePath).TrimStart('/');
            if (relative is "__webscene-media-verify.html" or "__webscene-media-demo.html")
            {
                context.Response.ContentType = "text/html; charset=utf-8";
                var bytes = await File.ReadAllBytesAsync(Path.Combine(AppContext.BaseDirectory, relative == "__webscene-media-demo.html" ? "media-demo.html" : "media-verify.html"));
                context.Response.ContentLength64 = bytes.Length;
                await context.Response.OutputStream.WriteAsync(bytes);
                return;
            }
            var path = Path.GetFullPath(Path.Combine(_root, relative.Length == 0 ? "index.html" : relative));
            if (!path.StartsWith(_root, StringComparison.Ordinal) || !File.Exists(path))
            { context.Response.StatusCode = 404; return; }
            context.Response.ContentType = Path.GetExtension(path) switch
            {
                ".html" => "text/html; charset=utf-8",
                ".js" => "text/javascript; charset=utf-8",
                ".css" => "text/css; charset=utf-8",
                ".json" or ".aureon" => "application/json",
                ".wgsl" => "text/plain; charset=utf-8",
                ".svg" => "image/svg+xml",
                ".mp4" or ".m4v" => "video/mp4",
                ".mov" => "video/quicktime",
                ".wav" => "audio/wav",
                ".mp3" => "audio/mpeg",
                ".jpg" or ".jpeg" => "image/jpeg",
                ".png" => "image/png",
                _ => "application/octet-stream"
            };
            if (context.Request.HttpMethod is not ("GET" or "HEAD"))
            { context.Response.StatusCode = 405; return; }
            await using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
                65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
            long start = 0, end = input.Length - 1;
            context.Response.Headers["Accept-Ranges"] = "bytes";
            var range = context.Request.Headers["Range"];
            if (range is not null)
            {
                var valid = range.StartsWith("bytes=", StringComparison.Ordinal);
                var parts = valid ? range[6..].Split('-') : [];
                valid &= parts.Length == 2;
                if (valid && parts[0].Length == 0)
                {
                    valid = long.TryParse(parts[1], out var suffix) && suffix > 0;
                    if (valid) start = Math.Max(0, input.Length - suffix);
                }
                else if (valid)
                {
                    valid = long.TryParse(parts[0], out start) && start >= 0;
                    if (parts[1].Length != 0)
                    {
                        valid &= long.TryParse(parts[1], out var requestedEnd);
                        end = Math.Min(end, requestedEnd);
                    }
                }
                if (!valid || start > end || start >= input.Length)
                {
                    context.Response.StatusCode = 416;
                    context.Response.Headers["Content-Range"] = $"bytes */{input.Length}";
                    return;
                }
                context.Response.StatusCode = 206;
                context.Response.Headers["Content-Range"] = $"bytes {start}-{end}/{input.Length}";
            }
            var remaining = Math.Max(0, end - start + 1);
            context.Response.ContentLength64 = remaining;
            if (context.Request.HttpMethod == "HEAD") return;
            input.Position = start;
            var buffer = new byte[65536];
            while (remaining > 0)
            {
                var count = await input.ReadAsync(buffer.AsMemory(0, (int)Math.Min(buffer.Length, remaining)));
                if (count == 0) throw new EndOfStreamException("Media file changed during response.");
                await context.Response.OutputStream.WriteAsync(buffer.AsMemory(0, count));
                remaining -= count;
            }
        }
        catch (Exception e) { Console.Error.WriteLine($"Asset server: {e.Message}"); }
        finally { context.Response.Close(); }
    }
    public void Dispose() { _listener.Stop(); _listener.Close(); }
}

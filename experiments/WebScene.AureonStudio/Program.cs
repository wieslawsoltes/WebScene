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
            var root = Environment.GetEnvironmentVariable("AUREON_ASSETS")
                ?? Path.Combine(AppContext.BaseDirectory, "Assets");
            _server = new AssetServer(root);
            var view = new NativeWebSceneView(true, url => url.StartsWith(_server.Origin, StringComparison.Ordinal));
            long errors = 0, recoveryWarnings = 0;
            view.JavaScriptException += e => { Interlocked.Increment(ref errors); Console.Error.WriteLine($"JavaScript: {e.Message}\n{e.Stack}"); };
            view.RuntimeFailed += e => Console.Error.WriteLine($"Runtime: {e.Message}\n{e.Stack}");
            view.ResourceFailed += e => Console.Error.WriteLine($"Resource: {e.Url}: {e.Message}");
            view.ConsoleMessage += e => {
                if (e.Level == "error")
                {
                    if (e.Message == "Recovery storage is unavailable. Save a scene file to keep your work.")
                        Interlocked.Increment(ref recoveryWarnings);
                    else Interlocked.Increment(ref errors);
                }
                Console.WriteLine($"Console {e.Level}: {e.Message}");
            };
            var window = new Window { Title = "Aureon Studio in WebScene", Width = 1280, Height = 820, Content = view };
            desktop.MainWindow = window;
            desktop.Exit += (_, _) => _server.Dispose();
            window.Opened += async (_, _) =>
            {
                try
                {
                    await view.LoadAsync(_server.Origin + "index.html",
                        Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
                        ?? Path.Combine(AppContext.BaseDirectory, "libwebscene_native_engine.dylib"));
                    await Task.Delay(5000);
                    Console.WriteLine("Aureon status: " + await view.EvaluateTextAsync("""
                        JSON.stringify({
                          ready:globalThis.aureon?.ready===true,gpuReady:globalThis.aureon?.gpuReady===true,
                          error:globalThis.aureon?.renderer?.initializationError?.message,
                          worker:typeof Worker,secureContextType:typeof isSecureContext,
                          gpu:!!navigator.gpu,
                          backend:document.getElementById('render-backend')?.textContent
                        })
                        """));
                    await view.EvaluateTextAsync("""
                        (async()=>{const a=await navigator.gpu.requestAdapter();const d=await a.requestDevice();
                        const r=JSON.stringify({compute:typeof d.createComputePipeline,
                          computeAsync:typeof d.createComputePipelineAsync,
                          renderAsync:typeof d.createRenderPipelineAsync,
                          computePass:typeof d.createCommandEncoder().beginComputePass});
                        d.destroy();globalThis.__aureonGpuCapabilities=r;})()
                        """);
                    await Task.Delay(500);
                    Console.WriteLine("GPU API: " + await view.EvaluateTextAsync("globalThis.__aureonGpuCapabilities"));
                    if (Environment.GetCommandLineArgs().Contains("--verify"))
                    {
                        await AureonAcceptance.Run(view, window);
                        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
                        await view.FlushRuntimeDiagnosticsAsync(timeout.Token);
                        var ready = await view.EvaluateTextAsync("globalThis.aureon?.gpuReady===true&&globalThis.aureon?.builtRevision>=0");
                        if (Interlocked.Read(ref errors) != 0) ready = "false";
                        Console.WriteLine($"Aureon unsupported IndexedDB recovery warnings: {Interlocked.Read(ref recoveryWarnings)}");
                        Console.WriteLine($"Aureon JavaScript/console errors: {Interlocked.Read(ref errors)}");
                        Console.WriteLine($"Successfully rendered WebScene scenes: {view.CapturePerformanceSnapshot().Surface.RenderedScenes}");
                        await view.DisposeAsync();
                        desktop.Shutdown(ready == "true" ? 0 : 1);
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
        if (!File.Exists(Path.Combine(_root, "index.html"))) throw new FileNotFoundException("Aureon assets are missing.", root);
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
                _ => "application/octet-stream"
            };
            var bytes = await File.ReadAllBytesAsync(path);
            context.Response.ContentLength64 = bytes.Length;
            await context.Response.OutputStream.WriteAsync(bytes);
        }
        catch (Exception e) { Console.Error.WriteLine($"Asset server: {e.Message}"); }
        finally { context.Response.Close(); }
    }
    public void Dispose() { _listener.Stop(); _listener.Close(); }
}

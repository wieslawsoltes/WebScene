using System.Text.Json;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.OpenGL;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;

internal static class Program
{
    static Program()
    {
        if (Environment.GetEnvironmentVariable("WEBSCENE_TRACE_AOT_EXCEPTIONS") == "1")
            AppDomain.CurrentDomain.FirstChanceException += (_, e) => Console.Error.WriteLine(e.Exception);
    }

    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_probe")]
    internal static extern int RenderGraphite(uint texture, uint serial);
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_poll")]
    internal static extern int PollGraphite(int drain);
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_initializations")]
    internal static extern uint GraphiteInitializations();
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_context_initializations")]
    internal static extern uint GraphiteContextInitializations();
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_output_allocations")]
    internal static extern uint GraphiteOutputAllocations();
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_verify_marker")]
    internal static extern int VerifyGraphiteMarker(uint texture, uint serial);
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_canvas_allocations")]
    internal static extern uint GraphiteCanvasAllocations();
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_canvas_busy")]
    internal static extern uint GraphiteCanvasBusy();
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_shutdown")]
    internal static extern int ShutdownGraphite();
    [STAThread]
    public static int Main(string[] args) => args.Contains("--aot-serialization-probe")
        ? AotSerializationProbe.Run()
        : args.Contains("--canvas-backing-probe")
        ? AppBuilder.Configure<CanvasBackingProbeApp>().UsePlatformDetect().StartWithClassicDesktopLifetime(args)
        : args.Contains("--metal-host")
        ? AppBuilder.Configure<MetalHostProbeApp>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions { RenderingMode = new[] { AvaloniaNativeRenderingMode.Metal } })
            .StartWithClassicDesktopLifetime(args)
        : args.Contains("--ganesh-metal")
        ? AppBuilder.Configure<GaneshWindowProbeApp>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions { RenderingMode = new[] { AvaloniaNativeRenderingMode.Metal } })
            .StartWithClassicDesktopLifetime(args)
        : args.Contains("--webgpu-metal")
        ? AppBuilder.Configure<WebGpuDocumentProbeApp>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions { RenderingMode = new[] { AvaloniaNativeRenderingMode.Metal } })
            .StartWithClassicDesktopLifetime(args)
        : args.Contains("--webgpu-opengl")
        ? AppBuilder.Configure<WebGpuDocumentProbeApp>().UsePlatformDetect()
            .With(new AvaloniaNativePlatformOptions { RenderingMode = new[] { AvaloniaNativeRenderingMode.OpenGl } })
            .StartWithClassicDesktopLifetime(args)
        : args.Contains("--webgpu-vsync")
        ? WindowsVSyncProbe.Configure(AppBuilder.Configure<WebGpuDocumentProbeApp>().UsePlatformDetect())
            .StartWithClassicDesktopLifetime(args)
        : args.Contains("--webgpu-document")
        ? AppBuilder.Configure<WebGpuDocumentProbeApp>().UsePlatformDetect().StartWithClassicDesktopLifetime(args)
        : args.Contains("--ganesh-window")
        ? AppBuilder.Configure<GaneshWindowProbeApp>().UsePlatformDetect().StartWithClassicDesktopLifetime(args)
        : AppBuilder.Configure<ProbeApp>().UsePlatformDetect().StartWithClassicDesktopLifetime(args);
}
internal sealed class ProbeApp : Application
{
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var window = new Window { Width = 320, Height = 120, Title = "WebScene GPU host capability probe" };
            desktop.MainWindow = window;
            window.Opened += async (_, _) =>
            {
                var exit = 0;
                try
                {
                    var visual = ElementComposition.GetElementVisual(window)
                        ?? throw new InvalidOperationException("No composition visual");
                    var interop = await visual.Compositor.TryGetCompositionGpuInterop().AsTask().WaitAsync(TimeSpan.FromSeconds(30));
                    var sharing = await visual.Compositor.TryGetRenderInterfaceFeature(typeof(IOpenGlTextureSharingRenderInterfaceContextFeature))
                        as IOpenGlTextureSharingRenderInterfaceContextFeature;
                    bool graphiteSource = Environment.GetCommandLineArgs().Contains("--graphite");
                    bool sharedTextureUpdateCompleted = false;
                    bool visualCommitCompleted = false;
                    int graphiteSubmissionsCompleted = 0;
                    int hostUpdatesCompleted = 0;
                    int nativeShutdownsCompleted = 0;
                    uint canvasTextureAllocations=0, outputTextureAllocations=0, graphiteContextInitializations=0, dawnDeviceInitializations=0;
                    int diagnosticMarkersVerified = 0;
                    bool verifyMarkers = Environment.GetCommandLineArgs().Contains("--verify-markers");
                    if (interop is not null && sharing?.CanCreateSharedContext == true)
                    {
                        using var glContext = sharing.CreateSharedContext()
                            ?? throw new InvalidOperationException("Shared context creation failed");
                        using var texture = sharing.CreateSharedTextureForComposition(glContext, graphiteSource ? new PixelSize(17,4) : new PixelSize(32,32));
                        using (glContext.EnsureCurrent())
                        {
                            var gl = glContext.GlInterface;
                            var framebuffer = gl.GenFramebuffer();
                            try
                            {
                                gl.BindFramebuffer(GlConsts.GL_FRAMEBUFFER, framebuffer);
                                gl.FramebufferTexture2D(GlConsts.GL_FRAMEBUFFER, GlConsts.GL_COLOR_ATTACHMENT0,
                                    GlConsts.GL_TEXTURE_2D, texture.TextureId, 0);
                                if (gl.CheckFramebufferStatus(GlConsts.GL_FRAMEBUFFER) != GlConsts.GL_FRAMEBUFFER_COMPLETE)
                                    throw new InvalidOperationException("Shared framebuffer incomplete");
                                gl.Viewport(0,0,32,32);
                                gl.ClearColor(0.2f,0.4f,0.6f,1);
                                gl.Clear(GlConsts.GL_COLOR_BUFFER_BIT); gl.Flush();
                            }
                            finally { gl.BindFramebuffer(GlConsts.GL_FRAMEBUFFER,0); gl.DeleteFramebuffer(framebuffer); }
                        }
                        using var surface = visual.Compositor.CreateDrawingSurface();
                        await using var imported = interop.ImportImage(texture);
                        await imported.ImportCompleted.WaitAsync(TimeSpan.FromSeconds(30));
                        var surfaceVisual = visual.Compositor.CreateSurfaceVisual();
                        surfaceVisual.Size = new System.Numerics.Vector2(128,128);
                        surfaceVisual.Surface = surface;
                        ElementComposition.SetElementChildVisual(window, surfaceVisual);
                        try
                        {
                            if (graphiteSource) {
                              for (int submission=0; submission<64; submission++) {
                                using (glContext.EnsureCurrent()) {
                                    if (Program.RenderGraphite((uint)texture.TextureId,(uint)submission+1) != 0)
                                        throw new InvalidOperationException("Dawn/Graphite host texture verification failed");
                                    if (Program.RenderGraphite((uint)texture.TextureId,(uint)submission+1) == 0)
                                        throw new InvalidOperationException("Overlapping host submission was accepted");
                                    if (Program.ShutdownGraphite() == 0)
                                        throw new InvalidOperationException("Shutdown accepted outstanding native work");
                                }
                                var deadline = DateTime.UtcNow.AddSeconds(30);
                                while (true) {
                                    int completion;
                                    using (glContext.EnsureCurrent()) completion = Program.PollGraphite(0);
                                    if (completion == 1) {
                                        using (glContext.EnsureCurrent()) {
                                            if (Program.PollGraphite(0) != -1)
                                                throw new InvalidOperationException("Duplicate completion was accepted");
                                        }
                                        graphiteSubmissionsCompleted++;
                                        break;
                                    }
                                    if (completion < 0 || DateTime.UtcNow >= deadline) {
                                        using (glContext.EnsureCurrent()) Program.PollGraphite(1);
                                        throw new InvalidOperationException("GL completion failed or timed out");
                                    }
                                    await Task.Delay(1);
                                }
                                if (verifyMarkers) {
                                    using (glContext.EnsureCurrent()) {
                                        if (Program.VerifyGraphiteMarker((uint)texture.TextureId,(uint)submission+1) != 0)
                                            throw new InvalidOperationException("Stale or incorrect host texture marker");
                                        if (Program.VerifyGraphiteMarker((uint)texture.TextureId,(uint)submission+2) == 0)
                                            throw new InvalidOperationException("Incorrect expected marker was accepted");
                                    }
                                    diagnosticMarkersVerified++;
                                }
                                // Await host consumption before overwriting the borrowed GL texture.
                                await surface.UpdateAsync(imported).WaitAsync(TimeSpan.FromSeconds(30));
                                hostUpdatesCompleted++;
                                await visual.Compositor.RequestCommitAsync().WaitAsync(TimeSpan.FromSeconds(30));
                                if (submission == 31) {
                                    using (glContext.EnsureCurrent()) {
                                        if (Program.ShutdownGraphite() != 0 || Program.GraphiteInitializations() != 0 ||
                                            Program.GraphiteCanvasAllocations() != 0 || Program.GraphiteOutputAllocations() != 0)
                                            throw new InvalidOperationException("Quiescent native shutdown failed");
                                    }
                                    nativeShutdownsCompleted++;
                                }
                              }
                              if (Program.GraphiteInitializations() != 1)
                                  throw new InvalidOperationException("Dawn device was recreated between submissions");
                              if (Program.GraphiteContextInitializations() != 1)
                                  throw new InvalidOperationException("Graphite context was recreated between submissions");
                              if (Program.GraphiteOutputAllocations() != 1)
                                  throw new InvalidOperationException("Output texture was recreated between submissions");
                              if (Program.GraphiteCanvasAllocations() != 2 || Program.GraphiteCanvasBusy() != 0)
                                  throw new InvalidOperationException("Canvas pool did not reuse and retire its two source textures");
                            }
                            if (!graphiteSource) {
                                await surface.UpdateAsync(imported).WaitAsync(TimeSpan.FromSeconds(30));
                                hostUpdatesCompleted++;
                            }
                            sharedTextureUpdateCompleted = hostUpdatesCompleted == (graphiteSource ? 64 : 1);
                            await visual.Compositor.RequestCommitAsync().WaitAsync(TimeSpan.FromSeconds(30));
                            visualCommitCompleted = true;
                            if (Environment.GetCommandLineArgs().Contains("--inspect"))
                            {
                                Console.WriteLine("Shared surface attached; inspection window is open for 30 seconds.");
                                await Task.Delay(TimeSpan.FromSeconds(30));
                            }
                        }
                        finally
                        {
                            ElementComposition.SetElementChildVisual(window, null);
                            await visual.Compositor.RequestCommitAsync().WaitAsync(TimeSpan.FromSeconds(30));
                            surfaceVisual.Surface = null;
                            if (graphiteSource) {
                                canvasTextureAllocations=Program.GraphiteCanvasAllocations();
                                outputTextureAllocations=Program.GraphiteOutputAllocations();
                                graphiteContextInitializations=Program.GraphiteContextInitializations();
                                dawnDeviceInitializations=Program.GraphiteInitializations();
                                using (glContext.EnsureCurrent()) {
                                    if (Program.ShutdownGraphite() != 0)
                                        throw new InvalidOperationException("Final native shutdown refused outstanding work");
                                }
                                nativeShutdownsCompleted++;
                            }
                        }
                    }
                    Console.WriteLine(JsonSerializer.Serialize(new
                    {
                        schemaVersion = 1,
                        probe = "avalonia-gpu-host",
                        avalonia = typeof(Application).Assembly.GetName().Version?.ToString(),
                        status = interop is null ? "unavailable" : "available",
                        imageTypes = interop?.SupportedImageHandleTypes.Select(t => new
                        { type = t, synchronization = interop.GetSynchronizationCapabilities(t).ToString() }).ToArray(),
                        semaphoreTypes = interop?.SupportedSemaphoreTypes.ToArray(),
                        isLost = interop?.IsLost,
                        canCreateSharedOpenGlContext = sharing?.CanCreateSharedContext ?? false,
                        graphiteSource,
                        graphiteSubmissionsCompleted,
                        hostUpdatesCompleted,
                        diagnosticMarkersVerified,
                        nativeShutdownsCompleted,
                        nativeCounterScope = "last-runtime-cycle",
                        canvasTextureAllocations,
                        outputTextureAllocations,
                        graphiteContextInitializations,
                        dawnDeviceInitializations,
                        sharedTextureUpdateCompleted,
                        visualCommitCompleted,
                        presentationVerified = false
                    }));
                    if (interop is null) exit = 77;
                }
                catch (Exception error)
                {
                    Console.Error.WriteLine(error); exit = 1;
                }
                Avalonia.Threading.Dispatcher.UIThread.Post(() => desktop.Shutdown(exit));
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}

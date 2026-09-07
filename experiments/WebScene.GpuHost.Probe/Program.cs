using System.Text.Json;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.OpenGL;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;

internal static class Program
{
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_probe")]
    internal static extern int RenderGraphite(uint texture);
    [DllImport("webscene_graphite_host_probe", EntryPoint="webscene_graphite_host_poll")]
    internal static extern int PollGraphite(int drain);
    [STAThread]
    public static int Main(string[] args) => AppBuilder.Configure<ProbeApp>()
        .UsePlatformDetect().StartWithClassicDesktopLifetime(args);
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
                            if (graphiteSource && Program.RenderGraphite((uint)texture.TextureId) != 0)
                                throw new InvalidOperationException("Dawn/Graphite host texture verification failed");
                        }
                        if (graphiteSource) {
                            var deadline = DateTime.UtcNow.AddSeconds(30);
                            while (true) {
                                int completion;
                                using (glContext.EnsureCurrent()) completion = Program.PollGraphite(0);
                                if (completion == 1) break;
                                if (completion < 0 || DateTime.UtcNow >= deadline) {
                                    using (glContext.EnsureCurrent()) Program.PollGraphite(1);
                                    throw new InvalidOperationException("GL completion failed or timed out");
                                }
                                await Task.Delay(1);
                            }
                        }
                        using var surface = visual.Compositor.CreateDrawingSurface();
                        await using var imported = interop.ImportImage(texture);
                        await imported.ImportCompleted.WaitAsync(TimeSpan.FromSeconds(30));
                        await surface.UpdateAsync(imported).WaitAsync(TimeSpan.FromSeconds(30));
                        sharedTextureUpdateCompleted = true;
                        var surfaceVisual = visual.Compositor.CreateSurfaceVisual();
                        surfaceVisual.Size = new System.Numerics.Vector2(128,128);
                        surfaceVisual.Surface = surface;
                        ElementComposition.SetElementChildVisual(window, surfaceVisual);
                        try
                        {
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

using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.OpenGL;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Rendering.Composition;

internal static class Program
{
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

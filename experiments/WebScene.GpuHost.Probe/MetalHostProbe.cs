using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using System.Text.Json;

internal sealed class MetalHostProbeApp : Application
{
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var probe = new MetalHostProbeControl();
            desktop.MainWindow = new Window { Width=320, Height=180,
                Title="WebScene Metal host qualification", Content=probe };
            desktop.MainWindow.Opened += async (_, _) => {
                var exit=0;
                try { Console.WriteLine(await probe.Completed.Task.WaitAsync(TimeSpan.FromSeconds(20))); }
                catch(Exception error) { Console.Error.WriteLine(error); exit=1; }
                desktop.Shutdown(exit);
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}
internal sealed class MetalHostProbeControl : Control, ICustomDrawOperation
{
    public TaskCompletionSource<string> Completed { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public override void Render(DrawingContext context) => context.Custom(this);
    Rect ICustomDrawOperation.Bounds => new(0,0,Bounds.Width,Bounds.Height);
    public bool HitTest(Point point) => false;
    public bool Equals(ICustomDrawOperation? other) => ReferenceEquals(this,other);
    public void Dispose() { }
    public void Render(ImmediateDrawingContext context)
    {
        if(Completed.Task.IsCompleted) return;
        try {
            var feature=context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) as ISkiaSharpApiLeaseFeature
                ?? throw new NotSupportedException("No Skia drawing lease");
            using var lease=feature.Lease();
            string hostName;
            using (var platform=lease.TryLeasePlatformGraphicsApi()
                ?? throw new NotSupportedException("No platform graphics lease"))
            {
                var host=platform.Context;
                // Avalonia marks this interface PrivateApi and omits it from reference assemblies.
                // Probe the pinned runtime interface while its owning lease is held.
                var metal=host.GetType().GetInterface("Avalonia.Metal.IMetalDevice")
                    ?? throw new NotSupportedException("Host is not Metal: "+host.GetType().FullName);
                var device=(IntPtr)metal.GetProperty("Device")!.GetValue(host)!;
                var queue=(IntPtr)metal.GetProperty("CommandQueue")!.GetValue(host)!;
                if(device==IntPtr.Zero || queue==IntPtr.Zero || lease.GrContext is null)
                    throw new InvalidOperationException("Incomplete Metal/Skia host");
                hostName=host.GetType().FullName!;
            }
            lease.SkCanvas.Clear(SkiaSharp.SKColors.Teal);
            Completed.TrySetResult(JsonSerializer.Serialize(new {
                host=hostName, metalDeviceAvailable=true,
                metalQueueAvailable=true, skiaGpuContextAvailable=true,
                producerInteropVerified=false, physicalPresentationVerified=false }));
        } catch(Exception error) { Completed.TrySetException(error); }
    }
}

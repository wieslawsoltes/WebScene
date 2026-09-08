using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using System.Text.Json;
using System.Runtime.InteropServices;
using WebScene.Backends.Avalonia.Native;

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
    [DllImport("/usr/lib/libobjc.A.dylib", EntryPoint="objc_getClass")]
    private static extern IntPtr GetClass(string name);
    [DllImport("/usr/lib/libobjc.A.dylib", EntryPoint="sel_registerName")]
    private static extern IntPtr Selector(string name);
    [DllImport("/usr/lib/libobjc.A.dylib", EntryPoint="objc_msgSend")]
    private static extern IntPtr TextureDescriptor(IntPtr receiver,IntPtr selector,ulong format,ulong width,ulong height,[MarshalAs(UnmanagedType.I1)] bool mipmapped);
    [DllImport("/usr/lib/libobjc.A.dylib", EntryPoint="objc_msgSend")]
    private static extern IntPtr SendObject(IntPtr receiver,IntPtr selector,IntPtr argument);
    [DllImport("/usr/lib/libobjc.A.dylib", EntryPoint="objc_msgSend")]
    private static extern void SendVoid(IntPtr receiver,IntPtr selector);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte CreateFixture(out NativeGpuImageLeaseV3 image);
    private NativeGpuImageLeaseV3? _fixture;
    public MetalHostProbeControl()
    {
        if (!Environment.GetCommandLineArgs().Contains("--metal-fixture")) return;
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
            ?? throw new InvalidOperationException("Native library is required"));
        var library=NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")
            ?? throw new InvalidOperationException("Fixture library is required"));
        var create=Marshal.GetDelegateForFunctionPointer<CreateFixture>(NativeLibrary.GetExport(library,"webscene_test_create_dawn_iosurface"));
        if(create(out var fixture)==0) throw new InvalidOperationException("Dawn fixture failed");
        _fixture=fixture;
    }
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
            IntPtr metalDevice;
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
                var descriptor=TextureDescriptor(GetClass("MTLTextureDescriptor"),
                    Selector("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"),80,16,16,false);
                var texture=SendObject(device,Selector("newTextureWithDescriptor:"),descriptor);
                if(texture==IntPtr.Zero) throw new InvalidOperationException("Metal texture allocation failed");
                try {
                    using var wrapped=NativeMetalBackendTexture.Create(16,16,texture);
                    if(!wrapped.IsValid || wrapped.Width!=16 || wrapped.Height!=16)
                        throw new InvalidOperationException("Metal backend wrapper invalid");
                } finally { SendVoid(texture,Selector("release")); }
                metalDevice=device;
                hostName=host.GetType().FullName!;
            }
            if (_fixture is not null) VerifyFixturePixels(lease,metalDevice);
            lease.SkCanvas.Clear(SkiaSharp.SKColors.Teal);
            Completed.TrySetResult(JsonSerializer.Serialize(new {
                host=hostName, metalDeviceAvailable=true,
                metalQueueAvailable=true, skiaGpuContextAvailable=true, metalTextureWrapperVerified=true,
                dawnIOSurfaceImportVerified=Environment.GetCommandLineArgs().Contains("--metal-fixture"),
                metalSampledPixelsVerified=Environment.GetCommandLineArgs().Contains("--metal-fixture"),
                diagnosticReadbacks=Environment.GetCommandLineArgs().Contains("--metal-fixture")?1:0,
                producerInteropVerified=false, physicalPresentationVerified=false }));
        } catch(Exception error) { Completed.TrySetException(error); }
    }
    private void VerifyFixturePixels(ISkiaSharpApiLease lease,IntPtr device)
    {
        if(NativeGpuImageConsumerV3.Acquire(_fixture!,out var consumer)!=NativeSceneAcquireStatus.Success || consumer is null)
            throw new InvalidOperationException("Fixture consumer acquisition failed");
        NativeMetalIOSurfaceTexture? imported=null;
        try {
            using(var platform=lease.TryLeasePlatformGraphicsApi()
                ?? throw new NotSupportedException("No Metal platform lease"))
                imported=NativeMetalIOSurfaceTexture.Import(device,consumer);
            using var backend=NativeMetalBackendTexture.Create(imported.Width,imported.Height,imported.Handle);
            using var image=SkiaSharp.SKImage.FromTexture(lease.GrContext,backend,SkiaSharp.GRSurfaceOrigin.TopLeft,
                SkiaSharp.SKColorType.Bgra8888,SkiaSharp.SKAlphaType.Premul)
                ?? throw new InvalidOperationException("Metal image wrapping failed");
            var info=new SkiaSharp.SKImageInfo(imported.Width,imported.Height,SkiaSharp.SKColorType.Bgra8888,SkiaSharp.SKAlphaType.Premul);
            using var target=SkiaSharp.SKSurface.Create(lease.GrContext,false,info)
                ?? throw new InvalidOperationException("Metal diagnostic surface creation failed");
            target.Canvas.Clear(SkiaSharp.SKColors.Magenta);
            target.Canvas.DrawImage(image,0,0);
            using var pixels=new SkiaSharp.SKBitmap(info);
            if(!target.ReadPixels(info,pixels.GetPixels(),pixels.RowBytes,0,0))
                throw new InvalidOperationException("Metal diagnostic readback failed");
            for(var y=0;y<info.Height;y++) for(var x=0;x<info.Width;x++) {
                var color=pixels.GetPixel(x,y);
                if(Math.Abs(color.Red-51)>1 || Math.Abs(color.Green-102)>1 || Math.Abs(color.Blue-153)>1 || color.Alpha!=255)
                    throw new InvalidOperationException($"Metal sampled pixel mismatch at {x},{y}: {color}");
            }
        } finally {
            // Diagnostic-only synchronous completion, including failed reads.
            // Production retirement must use an asynchronous consumer fence.
            lease.GrContext!.Flush(true,true);
            imported?.Dispose(); consumer.Complete(); _fixture!.Dispose(); _fixture=null;
        }
    }

}

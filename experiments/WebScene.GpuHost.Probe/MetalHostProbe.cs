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
                if (_fixture is not null)
                {
                    if(NativeGpuImageConsumerV3.Acquire(_fixture,out var consumer)!=NativeSceneAcquireStatus.Success || consumer is null)
                        throw new InvalidOperationException("Fixture consumer acquisition failed");
                    try {
                        using var imported=NativeMetalIOSurfaceTexture.Import(device,consumer);
                        using var backend=NativeMetalBackendTexture.Create(imported.Width,imported.Height,imported.Handle);
                        if(!backend.IsValid) throw new InvalidOperationException("Imported Metal texture wrapper invalid");
                    } finally { consumer.Complete(); _fixture.Dispose(); _fixture=null; }
                }
                hostName=host.GetType().FullName!;
            }
            lease.SkCanvas.Clear(SkiaSharp.SKColors.Teal);
            Completed.TrySetResult(JsonSerializer.Serialize(new {
                host=hostName, metalDeviceAvailable=true,
                metalQueueAvailable=true, skiaGpuContextAvailable=true, metalTextureWrapperVerified=true,
                dawnIOSurfaceImportVerified=Environment.GetCommandLineArgs().Contains("--metal-fixture"),
                producerInteropVerified=false, physicalPresentationVerified=false }));
        } catch(Exception error) { Completed.TrySetException(error); }
    }
}

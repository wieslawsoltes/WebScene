using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.OpenGL;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
using SkiaSharp;
using WebScene.Backends.Avalonia.Native;

// Diagnostic window using the production-source import and retirement components.
// The fixture's producer wait runs before opening the window, never in Render.
internal sealed class GaneshWindowProbeApp : Application
{
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var control = new GaneshImageControl();
            desktop.MainWindow = new Window { Width = 400, Height = 220,
                Title = "WebScene direct Dawn / Ganesh", Content = control };
            desktop.MainWindow.Opened += async (_, _) =>
            {
                var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
                timer.Tick += (_, _) => control.InvalidateVisual();
                timer.Start();
                var exit = 0;
                try { await control.Completed.Task.WaitAsync(TimeSpan.FromSeconds(15)); }
                catch (Exception error) { Console.Error.WriteLine(error); exit = 1; }
                timer.Stop();
                Console.WriteLine(JsonSerializer.Serialize(new { route = "Dawn-IOSurface-CGL-Ganesh",
                    renderedFrames = control.Frames, imports = control.Imports,
                    gpuRetirementCompleted = control.Completed.Task.IsCompletedSuccessfully,
                    explicitTransportCopies = 0, diagnosticReadbacks = control.VerifiedPixels,
                    physicalPresentationVerified = false }));
                desktop.Shutdown(exit);
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}
internal sealed class GaneshImageControl : Control, ICustomDrawOperation
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte CreateImage(out NativeGpuImageLeaseV3 image);
    internal readonly TaskCompletionSource Completed = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private NativeGpuImageLeaseV3? _source;
    private NativeMacOSRetainedGpuImage? _retained;
    internal int Frames, Imports, VerifiedPixels;
    private readonly bool _verifyPixels = Environment.GetCommandLineArgs().Contains("--verify-window-pixels");
    public GaneshImageControl()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
            ?? throw new InvalidOperationException("WEBSCENE_TEST_NATIVE_LIBRARY is required"));
        var fixture = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")
            ?? throw new InvalidOperationException("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY is required"));
        var create = Marshal.GetDelegateForFunctionPointer<CreateImage>(
            NativeLibrary.GetExport(fixture, "webscene_test_create_dawn_iosurface"));
        if (create(out var image) == 0) throw new InvalidOperationException("Dawn fixture creation failed");
        _source = image;
    }
    public override void Render(DrawingContext context) => context.Custom(this);
    Rect ICustomDrawOperation.Bounds => new(0, 0, Bounds.Width, Bounds.Height);
    public bool HitTest(Point point) => false;
    public bool Equals(ICustomDrawOperation? other) => false;
    public void Dispose() { } // Retained owner spans draw-operation replacement; GPU fence retires it.
    public void Render(ImmediateDrawingContext context)
    {
        if (Completed.Task.IsCompleted) return;
        try
        {
            var feature = context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) as ISkiaSharpApiLeaseFeature
                ?? throw new NotSupportedException("Host has no Skia API lease");
            using var lease = feature.Lease();
            if (_retained is null)
            {
                _retained = NativeMacOSRetainedGpuImage.Import(_source!, lease,
                    GRSurfaceOrigin.TopLeft, SKAlphaType.Premul);
                if (_retained is null) return; // Admission backpressure: retry on the next callback.
                _source!.Dispose(); _source = null;
                ++Imports;
            }
            if (_retained.IsRetiring)
            {
                if (_retained.TryComplete(lease)) Completed.TrySetResult();
                return;
            }
            var canvas = lease.SkCanvas;
            var save = canvas.Save();
            canvas.Clear(SKColors.MidnightBlue);
            canvas.ClipRect(new SKRect(30, 25, 350, 165));
            using var paint = new SKPaint { Color = new SKColor(255, 255, 255, 192) };
            _retained.Draw(lease, new SKRect(10, 10, 370, 185), paint);
            canvas.RestoreToCount(save);
            if (_verifyPixels && Frames == 0)
            {
                var surface = lease.SkSurface ?? throw new NotSupportedException("Host has no diagnostic surface");
                // Read just two destination pixels, solely when explicitly requested.
                using var pixel = new SKBitmap(new SKImageInfo(1, 1, SKColorType.Rgba8888, SKAlphaType.Premul));
                foreach (var sample in new[] { (X: 60f, Y: 50f, R: 45, G: 83, B: 143),
                    (X: 15f, Y: 15f, R: 25, G: 25, B: 112) })
                {
                    var point = canvas.TotalMatrix.MapPoint(sample.X, sample.Y);
                    if (!surface.ReadPixels(pixel.Info, pixel.GetPixels(), pixel.RowBytes, (int)point.X, (int)point.Y))
                        throw new InvalidOperationException("Host diagnostic pixel read failed");
                    var actual = pixel.GetPixel(0, 0);
                    if (Math.Abs(actual.Red - sample.R) > 1 || Math.Abs(actual.Green - sample.G) > 1 ||
                        Math.Abs(actual.Blue - sample.B) > 1 || actual.Alpha != 255)
                        throw new InvalidOperationException($"Host pixel mismatch at {point}: {actual}");
                    ++VerifiedPixels;
                }
            }
            if (++Frames == 32) _retained.Retire(lease);
        }
        catch (Exception error) { Completed.TrySetException(error); }
    }
}

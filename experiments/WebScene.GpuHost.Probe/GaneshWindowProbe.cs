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
                Console.WriteLine(JsonSerializer.Serialize(new { route = Environment.GetCommandLineArgs().Contains("--ganesh-metal") ? "Dawn-IOSurface-Metal-Ganesh" : "Dawn-IOSurface-CGL-Ganesh",
                    renderedFrames = control.Frames, imports = control.Imports,
                    gpuRetirementCompleted = control.Completed.Task.IsCompletedSuccessfully,
                    explicitTransportCopies = 0, diagnosticReadbacks = control.VerifiedPixels,
                    physicalPresentationVerified = false, detachedBeforeRetirement = control.DetachedBeforeRetirement }));
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
    private readonly NativeCanvasSceneRenderer _renderer = new();
    private NativeGpuImageLeaseV3? _source;
    private NativeGpuScenePresenter? _retained;
    private NativeGpuSceneImages? _replacement;
    internal int Frames, Imports, VerifiedPixels;
    internal bool DetachedBeforeRetirement;
    private int _retirementStarted;
    private readonly bool _detachBeforeRetirement = Environment.GetCommandLineArgs().Contains("--detach-before-retirement");
    private readonly bool _verifyPixels = Environment.GetCommandLineArgs().Contains("--verify-window-pixels");
    public unsafe GaneshImageControl()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")
            ?? throw new InvalidOperationException("WEBSCENE_TEST_NATIVE_LIBRARY is required"));
        var fixture = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")
            ?? throw new InvalidOperationException("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY is required"));
        var create = Marshal.GetDelegateForFunctionPointer<CreateImage>(
            NativeLibrary.GetExport(fixture, "webscene_test_create_dawn_iosurface"));
        if (create(out var image) == 0) throw new InvalidOperationException("Dawn fixture creation failed");
        _source = image;
        var commands = stackalloc SceneCommand[] {
            new() { Kind = 1, Width = 400, Height = 220, Rgba = 0x191970ff },
            new() { Kind = 12, X = 30, Y = 25, Width = 320, Height = 140 },
            new() { Kind = 30, Rgba = 192 },
            new() { Kind = 256, X = 10, Y = 10, Width = 360, Height = 175, Rgba = 0 },
            new() { Kind = 31 }, new() { Kind = 13 },
            new() { Kind = 257, NodeId = 7 },
            new() { Kind = 9, X = 310, Y = 140, Width = 30, Height = 10, Rgba = 0xffff00ff }
        };
        var canvasCommand = new NativeCanvasCommand { Kind = 22, V2 = 20, V3 = 20 };
        var layer = new NativeCanvasLayer { NodeId = 7, Flags = 1, CommandCount = 1,
            X = 250, Y = 100, Width = 20, Height = 20, BitmapWidth = 20, BitmapHeight = 20, Generation = 1 };
        var scene = new NativeSceneView { Commands = commands, CanvasLayers = &layer,
            CanvasCommands = &canvasCommand, CanvasCommandCount = 1,
            Header = new SceneHeader { Revision = 1, Flags = 3, CommandCount = 8,
                CanvasLayerCount = 1, ViewportWidth = 400, ViewportHeight = 220 } };
        if (!_renderer.ApplyDiff(&scene, orderedGpuImages: true)) throw new InvalidOperationException("Ordered scene rejected");
    }
    public override void Render(DrawingContext context) => context.Custom(this);
    Rect ICustomDrawOperation.Bounds => new(0, 0, Bounds.Width, Bounds.Height);
    public bool HitTest(Point point) => false;
    public bool Equals(ICustomDrawOperation? other) => false;
    public void Dispose() { } // Retained owner spans draw-operation replacement; GPU fence retires it.
    public void Render(ImmediateDrawingContext context)
    {
        if (Completed.Task.IsCompleted || Volatile.Read(ref _retirementStarted) != 0) return;
        try
        {
            var feature = context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) as ISkiaSharpApiLeaseFeature
                ?? throw new NotSupportedException("Host has no Skia API lease");
            using var lease = feature.Lease();
            if (_retained is null)
            {
                var status = NativeGpuSceneImages.Retain(new[] { _source! }, out var first);
                if (status == NativeSceneAcquireStatus.Backpressure) return;
                if (status != NativeSceneAcquireStatus.Success || first is null) throw new InvalidOperationException($"Scene image capture failed: {status}");
                _retained = new NativeGpuScenePresenter();
                if (!_retained.TryReplace(first)) throw new InvalidOperationException("Initial scene rejected");
                status = NativeGpuSceneImages.Retain(new[] { _source! }, out _replacement);
                if (status != NativeSceneAcquireStatus.Success || _replacement is null) throw new InvalidOperationException("Replacement capture failed");
                _source!.Dispose(); _source = null;
            }
            if (_retained.IsStopping)
            {
                if (_retained.TryComplete(lease)) { _renderer.Reset(); Completed.TrySetResult(); }
                return;
            }
            if (Frames == 16 && _replacement is not null)
            {
                if (!_retained.TryReplace(_replacement)) return;
                _replacement = null;
            }
            if (!_retained.TryPrepare(lease)) return;
            Imports = _retained.ImportedCount;
            var canvas = lease.SkCanvas;
            _renderer.RenderRetained(canvas, 400, 220, null,
                (index, destination) =>
                {
                    if (index != 0) throw new InvalidOperationException("Unknown scene GPU image slot");
                    _retained.Draw(lease, index, destination);
                });
            if (_verifyPixels && (Frames == 0 || Frames == 16))
            {
                var surface = lease.SkSurface ?? throw new NotSupportedException("Host has no diagnostic surface");
                // Read four destination pixels, solely when explicitly requested.
                using var pixel = new SKBitmap(new SKImageInfo(1, 1, SKColorType.Rgba8888, SKAlphaType.Premul));
                foreach (var sample in new[] { (X: 60f, Y: 50f, R: 45, G: 83, B: 143),
                    (X: 15f, Y: 15f, R: 25, G: 25, B: 112),
                    (X: 320f, Y: 145f, R: 255, G: 255, B: 0),
                    (X: 255f, Y: 105f, R: 0, G: 0, B: 0) })
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
            if (++Frames == 32)
            {
                _retained.BeginShutdown();
                if (_detachBeforeRetirement)
                {
                    Interlocked.Exchange(ref _retirementStarted, 1);
                    var retiring = _retained;
                    var renderingThread = Environment.CurrentManagedThreadId;
                    Dispatcher.UIThread.Post(() =>
                    {
                        if (TopLevel.GetTopLevel(this) is not Window window)
                        { Completed.TrySetException(new InvalidOperationException("Probe window unavailable for detach")); return; }
                        window.Content = null;
                        DetachedBeforeRetirement = true;
                        _ = Task.Run(async () =>
                        {
                            try
                            {
                                if (Environment.CurrentManagedThreadId == renderingThread) throw new InvalidOperationException("Detached probe must retire on a different thread");
                                await NativeGpuRetirement.Start(retiring).WaitAsync(TimeSpan.FromSeconds(5));
                                if (NativeGpuRetirement.RetainedCount != 0) throw new InvalidOperationException("Completed retirement retained its owner");
                                _renderer.Reset();Completed.TrySetResult();
                            }
                            catch (Exception error) { Completed.TrySetException(error); }
                        });
                    });
                }
            }
        }
        catch (Exception error) { Completed.TrySetException(error); }
    }
}

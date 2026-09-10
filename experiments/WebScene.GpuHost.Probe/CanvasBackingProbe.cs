using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
using SkiaSharp;
using WebScene.Backends.Avalonia.Native;

internal sealed class CanvasBackingProbeApp : Application
{
    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var control = new CanvasBackingProbeControl();
            desktop.MainWindow = new Window { Width = 240, Height = 160,
                Title = "Canvas2D GPU backing verification", Content = control };
            desktop.MainWindow.Opened += async (_, _) =>
            {
                try { await control.Completed.Task.WaitAsync(TimeSpan.FromSeconds(20)); desktop.Shutdown(0); }
                catch (Exception error) { Console.Error.WriteLine(error); desktop.Shutdown(1); }
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}

internal sealed class CanvasBackingProbeControl : Control, ICustomDrawOperation
{
    private NativeCanvasCheckpointFence? _fence;
    private SKImage? _fenceImage;
    private NativeCanvasCheckpointTransfer? _transfer;
    private Task<SKImage>? _workerImage;
    private SKImage? _transferReference;
    private System.Diagnostics.Stopwatch? _fenceDeadline;
    internal readonly TaskCompletionSource Completed = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public override void Render(DrawingContext context) => context.Custom(this);
    Rect ICustomDrawOperation.Bounds => new(0, 0, Bounds.Width, Bounds.Height);
    public bool HitTest(Point point) => false;
    public bool Equals(ICustomDrawOperation? other) => false;
    public void Dispose() { }
    public void Render(ImmediateDrawingContext context)
    {
        if (Completed.Task.IsCompleted) return;
        try
        {
            var feature = context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) as ISkiaSharpApiLeaseFeature
                ?? throw new NotSupportedException("No Skia graphics lease");
            using var lease = feature.Lease();
            if (_workerImage is not null)
            {
                if (!_workerImage.IsCompleted)
                {
                    Dispatcher.UIThread.Post(InvalidateVisual);
                    return;
                }
                using var pixels = SKBitmap.FromImage(_workerImage.GetAwaiter().GetResult());
                using var expected = SKBitmap.FromImage(_transferReference!);
                if (pixels.Bytes.Zip(expected.Bytes).Any(pair => Math.Abs(pair.First - pair.Second) > 1))
                    throw new InvalidOperationException("Worker checkpoint transfer changed pixels or orientation.");
                _workerImage.Result.Dispose(); _transferReference!.Dispose();
                Console.WriteLine("Worker checkpoint transfer preserved RGBA pixels and row orientation.");
                Completed.TrySetResult();
                return;
            }
            if (_transfer is not null)
            {
                if (!_transfer.IsReady())
                {
                    if (_fenceDeadline!.Elapsed > TimeSpan.FromSeconds(5))
                        throw new TimeoutException("Checkpoint transfer did not complete");
                    Dispatcher.UIThread.Post(InvalidateVisual);
                    return;
                }
                var transfer = _transfer; _transfer = null;
                _workerImage = Task.Run(() => { using (transfer) return transfer.ReadOnWorker(); });
                Dispatcher.UIThread.Post(InvalidateVisual);
                return;
            }
            if (_fence is not null)
            {
                if (!_fence.IsReady())
                {
                    if (_fenceDeadline!.Elapsed > TimeSpan.FromSeconds(5))
                        throw new TimeoutException("Checkpoint GPU fence did not complete");
                    Dispatcher.UIThread.Post(InvalidateVisual);
                    return;
                }
                _fence.Dispose(); _fence = null;
                using (var raster = _fenceImage!.ToRasterImage(true))
                using (var pixels = SKBitmap.FromImage(raster))
                    if (pixels.GetPixel(0, 0) != SKColors.Red)
                        throw new InvalidOperationException("Deferred checkpoint snapshot changed after later drawing.");
                _fenceImage.Dispose(); _fenceImage = null;
                Console.WriteLine("Canvas checkpoint GPU fence completed; deferred snapshot preserved pixels.");
                using var surface = SKSurface.Create(lease.GrContext, false,
                    new SKImageInfo(17, 13, SKColorType.Rgba8888, SKAlphaType.Premul))!;
                surface.Canvas.Clear(SKColors.Transparent);
                using (var paint = new SKPaint { Color = new SKColor(230, 50, 10, 123) })
                    surface.Canvas.DrawRect(0, 0, 17, 5, paint);
                using (var paint = new SKPaint { Color = SKColors.Blue })
                    surface.Canvas.DrawRect(0, 9, 17, 4, paint);
                using var snapshot = surface.Snapshot();
                _transferReference = snapshot.ToRasterImage(true);
                _transfer = NativeCanvasCheckpointTransfer.Create(snapshot, lease)
                    ?? throw new NotSupportedException("Checkpoint pixel-buffer transfer unavailable");
                surface.Canvas.Clear(SKColors.Green);
                _fenceDeadline!.Restart();
                Dispatcher.UIThread.Post(InvalidateVisual);
                return;
            }
            Verify(lease.GrContext ?? throw new NotSupportedException("No GPU context"));
            Verify(lease.GrContext!, checkpoints: true);
            if (OperatingSystem.IsWindows())
            {
                using var surface = SKSurface.Create(lease.GrContext, false,
                    new SKImageInfo(16, 16, SKColorType.Rgba8888, SKAlphaType.Premul))!;
                surface.Canvas.Clear(SKColors.Red);
                _fenceImage = surface.Snapshot();
                _fence = NativeCanvasCheckpointFence.Create(lease)
                    ?? throw new NotSupportedException("No checkpoint GPU fence");
                surface.Canvas.Clear(SKColors.Blue);
                _fenceDeadline = System.Diagnostics.Stopwatch.StartNew();
                Dispatcher.UIThread.Post(InvalidateVisual);
                return;
            }
            Completed.TrySetResult();
        }
        catch (Exception error) { Completed.TrySetException(error); }
    }

    private static unsafe void Verify(GRContext context, bool checkpoints = false)
    {
        var incremental = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = true };
        var reference = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = checkpoints };
        var info = new SKImageInfo(35, 18, SKColorType.Rgba8888, SKAlphaType.Premul);
        using var actual = SKSurface.Create(context, false, info)!;
        using var expected = SKSurface.Create(context, false, info)!;
        using var actualPixels = new SKBitmap(info);
        using var expectedPixels = new SKBitmap(info);
        var commands = new List<NativeCanvasCommand>();
        var compared = 0;
        byte[] checkpointBytes = [];
        var checkpointAt = 0;
        ulong generation = 1;
        try
        {
            for (var frame = 0; frame < 48; ++frame)
            {
                // The backing width is 35; this clear stops at 34.5 pixels.
                // A mark in that edge must survive with partial coverage.
                commands.Add(new() { Kind = 4, V0 = 1.75, V3 = 1.75 });
                commands.Add(new() { Kind = 24, V2 = 34.5 / 1.75, V3 = 18 / 1.75 });
                commands.Add(new() { Kind = 1 });
                commands.Add(new() { Kind = 6, V0 = frame % 4 * 0.25, V1 = 0.5 });
                commands.Add(new() { Kind = 22, V0 = 2, V1 = 2, V2 = 7, V3 = 4 });
                commands.Add(new() { Kind = 2 });
                commands.Add(new() { Kind = 9 });
                commands.Add(new() { Kind = 11, V0 = 1, V1 = 1 });
                commands.Add(new() { Kind = 12, V0 = 18, V1 = 8 });
                commands.Add(new() { Kind = 20 });
                if (frame == 0) commands.Add(new() { Kind = 22, V0 = 19, V2 = 1, V3 = 10 });
                var storage = commands.ToArray();
                fixed (NativeCanvasCommand* data = storage)
                {
                    var layer = new NativeCanvasLayer { NodeId = 7, Flags = 1, Generation = 1,
                        CommandCount = (uint)storage.Length, Width = 20, Height = 18 / 1.75f,
                        BitmapWidth = 35, BitmapHeight = 18 };
                    var scene = new NativeSceneView { StructSize = (uint)sizeof(NativeSceneView), AbiVersion = 2,
                        CanvasLayers = &layer, CanvasCommands = data, CanvasCommandCount = (uint)storage.Length,
                        Header = new SceneHeader { Revision = (ulong)frame + 1, BaseRevision = (ulong)frame,
                            Flags = frame == 0 ? 1u : 0u, CanvasLayerCount = 1,
                            ViewportWidth = 20, ViewportHeight = 18 / 1.75f } };
                    if (!reference.ApplyDiff(&scene)) throw new Exception("Reference diff rejected");
                    if (checkpointBytes.Length == 0)
                    {
                        if (!incremental.ApplyDiff(&scene)) throw new Exception("Diff rejected");
                    }
                    else
                    {
                        NativeCanvasCommand[] compacted = [new() { Kind=58 },..storage[checkpointAt..]];
                        fixed (NativeCanvasCommand* compactData=compacted)
                        fixed (byte* resourceBytes=checkpointBytes)
                        {
                            var resource=new NativeSceneString { ByteLength=(uint)checkpointBytes.Length };
                            layer.CommandCount=(uint)compacted.Length;layer.Generation=generation;layer.StringCount=1;
                            scene.CanvasCommands=compactData;scene.CanvasCommandCount=layer.CommandCount;
                            scene.Strings=&resource;scene.StringCount=1;scene.StringBytes=resourceBytes;scene.StringByteCount=resource.ByteLength;
                            if(!incremental.ApplyDiff(&scene))throw new Exception("Checkpoint diff rejected");
                        }
                    }
                    foreach (var pair in new[] { (incremental, actual), (reference, expected) })
                    {
                        var canvas = pair.Item2.Canvas;
                        canvas.ResetMatrix(); canvas.Clear(SKColors.Transparent); canvas.Scale(1.75f);
                        pair.Item1.RenderRetained(canvas, 20, 18 / 1.75f, null, canvasGpuContext: context);
                    }
                }
                if(checkpoints && frame==47)
                {
                    // Drop all retained GPU pictures and rebuild from an independent
                    // raster checkpoint, as required after renderer/context replacement.
                    var recovery=incremental.EncodeRetainedCanvasCheckpoint(7);
                    incremental.Reset();
                    fixed(byte* bytes=recovery)
                    {
                        var resource=new NativeSceneString { ByteLength=(uint)recovery.Length };
                        var command=new NativeCanvasCommand { Kind=58 };
                        var layer=new NativeCanvasLayer { NodeId=7,Flags=1,Generation=99,CommandCount=1,StringCount=1,
                            Width=20,Height=18/1.75f,BitmapWidth=35,BitmapHeight=18 };
                        var scene=new NativeSceneView { StructSize=(uint)sizeof(NativeSceneView),AbiVersion=2,
                            CanvasLayers=&layer,CanvasCommands=&command,CanvasCommandCount=1,Strings=&resource,
                            StringCount=1,StringBytes=bytes,StringByteCount=resource.ByteLength,
                            Header=new SceneHeader { Revision=99,Flags=1,CanvasLayerCount=1,ViewportWidth=20,ViewportHeight=18/1.75f } };
                        if(!incremental.ApplyDiff(&scene))throw new Exception("Checkpoint recovery rejected");
                        actual.Canvas.ResetMatrix();actual.Canvas.Clear(SKColors.Transparent);actual.Canvas.Scale(1.75f);
                        incremental.RenderRetained(actual.Canvas,20,18/1.75f,null,canvasGpuContext:context);
                    }
                }
                // Compare actual GPU pixels; normal presentation remains GPU-only.
                if (!actual.ReadPixels(info, actualPixels.GetPixels(), actualPixels.RowBytes, 0, 0)
                    || !expected.ReadPixels(info, expectedPixels.GetPixels(), expectedPixels.RowBytes, 0, 0))
                    throw new Exception("Diagnostic readback failed");
                var a = actualPixels.Pixels; var b = expectedPixels.Pixels;
                for (var pixel = 0; pixel < a.Length; ++pixel)
                    if (Math.Abs(a[pixel].Alpha - b[pixel].Alpha) > 1
                        || Math.Abs(a[pixel].Red - b[pixel].Red) > 1
                        || Math.Abs(a[pixel].Green - b[pixel].Green) > 1
                        || Math.Abs(a[pixel].Blue - b[pixel].Blue) > 1)
                        throw new Exception($"GPU backing differs at frame {frame}, pixel {pixel}: {a[pixel]} / {b[pixel]}");
                compared++;
                if(checkpoints && frame%12==11 && frame<47)
                {
                    checkpointBytes=incremental.EncodeRetainedCanvasCheckpoint(7);
                    checkpointAt=commands.Count;++generation;
                }
            }
            if (incremental.ResumedCanvasCompilations != (checkpoints ? 44 : 47)) throw new Exception("Append-only compilation was not reused");
            Console.WriteLine($"Canvas GPU backing verified: {compared} frames, fractional clears and transforms; checkpoints={checkpoints}.");
        }
        finally { incremental.Reset(); reference.Reset(); }
    }
}

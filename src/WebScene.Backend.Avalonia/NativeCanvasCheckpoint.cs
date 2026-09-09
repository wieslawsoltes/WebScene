using System.Text.Json;
using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed unsafe partial class NativeCanvasSceneRenderer
{
    internal const uint CanvasCheckpointCommand = 58;
    internal const uint CanvasCheckpointInterval = 32768;
    private readonly Dictionary<uint, (ulong Generation, uint Count)> _checkpointAttempts = [];
    private long _checkpointSubmissions, _maximumCheckpointCommands;
    private double _maximumCheckpointMilliseconds;
    private long _checkpointDeferredReadbacks = 0, _checkpointFencePolls = 0;
    private long _checkpointWorkerReadbacks = 0;
    private bool _checkpointApiAvailable = true;
    private sealed record PendingCheckpoint(uint NodeId, ulong Generation, uint Count, Task<byte[]> Encoding);
    private PendingCheckpoint? _pendingCheckpoint;
#if !WEBSCENE_UNO
    private sealed record PendingReadback(uint NodeId, ulong Generation, uint Count,
        SKImage Image, RasterCheckpoint State, GRContext Context, NativeCanvasCheckpointFence? Fence) : IDisposable
    {
        internal NativeCanvasCheckpointTransfer? Transfer { get; set; }
        internal bool IsReady() => Transfer?.IsReady() ?? Fence!.IsReady();
        public void Dispose() { try { Transfer?.Dispose(); Fence?.Dispose(); } finally { Image.Dispose(); } }
    }
    private PendingReadback? _pendingReadback;
#endif
    private static readonly JsonSerializerOptions CheckpointJson = new() { IncludeFields = true };

    internal sealed class RasterCheckpoint
    {
        public int Version { get; set; } = 1;
        public string Png { get; set; } = "";
        public List<float[]> Path { get; set; } = [];
        public int FillType { get; set; }
        public float[] Matrix { get; set; } = [];
        public CanvasState State { get; set; }
    }

    internal static byte[] EncodeCheckpoint(SKImage image, CanvasReplaySnapshot snapshot)
        => EncodeCheckpoint(image, CaptureCheckpointState(snapshot));

    private static byte[] EncodeCheckpoint(SKImage image, RasterCheckpoint checkpoint)
    {
        using var png = image.Encode(SKEncodedImageFormat.Png, 100);
        if (png is null) throw new InvalidOperationException("Canvas checkpoint encoding failed.");
        checkpoint.Png = Convert.ToBase64String(png.ToArray());
        return JsonSerializer.SerializeToUtf8Bytes(checkpoint,CheckpointJson);
    }

    private static RasterCheckpoint CaptureCheckpointState(CanvasReplaySnapshot snapshot)
    {
        var m = snapshot.Matrix;
        return new RasterCheckpoint
        {
            Path = EncodePath(snapshot.Path),
            FillType = (int)snapshot.Path.FillType, State = snapshot.State,
            Matrix = [m.ScaleX,m.SkewX,m.TransX,m.SkewY,m.ScaleY,m.TransY,m.Persp0,m.Persp1,m.Persp2]
        };
    }

    private static List<float[]> EncodePath(SKPath path)
    {
        var result = new List<float[]>();
        using var iterator = path.CreateRawIterator();
        var points = new SKPoint[4];
        for (var verb=iterator.Next(points);verb!=SKPathVerb.Done;verb=iterator.Next(points))
            result.Add([(float)verb,points[0].X,points[0].Y,points[1].X,points[1].Y,
                points[2].X,points[2].Y,points[3].X,points[3].Y,verb==SKPathVerb.Conic ? iterator.ConicWeight() : 0]);
        return result;
    }

    internal byte[] EncodeRetainedCanvasCheckpoint(uint nodeId)
    {
        var layer = s_layers[nodeId];
        var state = layer.ReplaySnapshot ?? throw new InvalidOperationException("Canvas state cannot be checkpointed.");
        if (layer.CheckpointImage is { } image) return EncodeCheckpoint(image,state);
        using var surface = SKSurface.Create(new SKImageInfo((int)layer.BitmapWidth,(int)layer.BitmapHeight,
            SKColorType.Rgba8888,SKAlphaType.Premul));
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.DrawPicture(layer.Picture);
        using var raster = surface.Snapshot();
        return EncodeCheckpoint(raster,state);
    }

    // Called under the owning GPU lease. Checkpoint readback is periodic
    // recovery storage, never the presenter path or a per-frame pixel transfer.
    internal void CheckpointCanvasHistory(IntPtr engine
#if !WEBSCENE_UNO
        , global::Avalonia.Skia.ISkiaSharpApiLease? lease = null
#endif
        )
    {
        if (!UseIncrementalCanvasBacking || !_checkpointApiAvailable) return;
        foreach (var layer in s_layers.Values)
            _maximumCheckpointCommands = Math.Max(_maximumCheckpointCommands,layer.CommandCount);
#if !WEBSCENE_UNO
        if (_pendingReadback is not null) return;
#endif
        if (_pendingCheckpoint is { } pending)
        {
            if (!pending.Encoding.IsCompleted) return;
            _pendingCheckpoint = null;
            if (pending.Encoding.IsCompletedSuccessfully)
            {
                var payload = pending.Encoding.Result;
                try
                {
                    // Native access stays on the composition owner. The encoder
                    // never borrows an engine handle or a GPU object.
                    if (NativeWebSceneApi.SubmitCanvasCheckpoint(engine,pending.NodeId,pending.Generation,
                            pending.Count,payload,(nuint)payload.Length) != 0) _checkpointSubmissions++;
                }
                catch (EntryPointNotFoundException) { _checkpointApiAvailable=false; return; }
            }
            else { _ = pending.Encoding.Exception; }
        }
        foreach (var layer in s_layers.Values)
        {
            if (layer.CommandCount < CanvasCheckpointInterval || layer.ReplaySnapshot is not { } state
                || layer.CheckpointImage is not { } image || layer.GpuContext is not { IsAbandoned: false }) continue;
            if (_checkpointAttempts.TryGetValue(layer.NodeId, out var attempt)
                && attempt.Generation == layer.Generation
                && layer.CommandCount - Math.Min(layer.CommandCount,attempt.Count) < CanvasCheckpointInterval) continue;
            var started=System.Diagnostics.Stopwatch.GetTimestamp();
            try
            {
                var capturedState = CaptureCheckpointState(state);
#if !WEBSCENE_UNO
                if (lease is not null && Environment.GetEnvironmentVariable("WEBSCENE_ASYNC_CANVAS_READBACK") == "1"
                    && NativeCanvasCheckpointTransfer.Create(image, lease) is { } transfer)
                {
                    _pendingReadback = new(layer.NodeId, layer.Generation, layer.CommandCount,
                        image, capturedState, layer.GpuContext, null) { Transfer = transfer };
                    layer.CheckpointImage = null;
                    _checkpointAttempts[layer.NodeId] = (layer.Generation, layer.CommandCount);
                    break;
                }
                if (lease is not null && NativeCanvasCheckpointFence.Create(lease) is { } fence)
                {
                    // Transfer ownership: the retained picture keeps its own native
                    // image reference, while this immutable snapshot survives rebasing.
                    _pendingReadback = new(layer.NodeId, layer.Generation, layer.CommandCount,
                        image, capturedState, layer.GpuContext, fence);
                    layer.CheckpointImage = null;
                    _checkpointAttempts[layer.NodeId] = (layer.Generation,layer.CommandCount);
                    break;
                }
#endif
                var raster = image.ToRasterImage(true);
                if (raster is null) continue;
                var encoding = StartCheckpointEncoding(raster, capturedState);
                _pendingCheckpoint = new(layer.NodeId,layer.Generation,layer.CommandCount,encoding);
                _checkpointAttempts[layer.NodeId] = (layer.Generation,layer.CommandCount);
                break;
            }
            finally
            {
                _maximumCheckpointMilliseconds=Math.Max(_maximumCheckpointMilliseconds,
                    System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds);
                TraceCheckpointStage("queue", started);
            }
        }
        foreach (var id in _checkpointAttempts.Keys.Where(id => !s_layers.ContainsKey(id)).ToArray())
            _checkpointAttempts.Remove(id);
    }


#if !WEBSCENE_UNO
    internal void PollCanvasCheckpointReadback(global::Avalonia.Skia.ISkiaSharpApiLease lease)
    {
        if (_pendingReadback is { } readback)
        {
            if (readback.Context.IsAbandoned || !ReferenceEquals(lease?.GrContext, readback.Context)
                || !s_layers.TryGetValue(readback.NodeId, out var retained)
                || retained.Generation != readback.Generation)
            {
                _pendingReadback = null;
                readback.Dispose();
            }
            else
            {
                _checkpointFencePolls++;
                if (!readback.IsReady()) return;
                _pendingReadback = null;
                var started = System.Diagnostics.Stopwatch.GetTimestamp();
                using (readback)
                {
                    if (readback.Transfer is { } transfer)
                    {
                        readback.Transfer = null; // Worker now owns the buffer, including on failure/reset.
                        var task = Task.Run(() =>
                        {
                            using (transfer)
                            {
                                var copyStarted = System.Diagnostics.Stopwatch.GetTimestamp();
                                using var raster = transfer.ReadOnWorker();
                                TraceCheckpointStage("worker-copy", copyStarted);
                                return EncodeCheckpoint(raster, readback.State);
                            }
                        });
                        ObserveCheckpointTask(task);
                        _pendingCheckpoint = new(readback.NodeId, readback.Generation, readback.Count, task);
                        _checkpointWorkerReadbacks++;
                        _checkpointDeferredReadbacks++;
                    }
                    else
                    {
                        var raster = readback.Image.ToRasterImage(true);
                        if (raster is not null)
                        {
                            _checkpointDeferredReadbacks++;
                            _pendingCheckpoint = new(readback.NodeId, readback.Generation, readback.Count,
                                StartCheckpointEncoding(raster, readback.State));
                        }
                    }
                }
                _maximumCheckpointMilliseconds = Math.Max(_maximumCheckpointMilliseconds,
                    System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds);
                TraceCheckpointStage("complete", started);
            }
        }
    }
#endif

    private static void TraceCheckpointStage(string stage, long started)
    {
        if (Environment.GetEnvironmentVariable("WEBSCENE_TRACE_CANVAS_CHECKPOINTS") != "1") return;
        Console.WriteLine("Canvas checkpoint timing: " + JsonSerializer.Serialize(new
        {
            stage, started, ended = System.Diagnostics.Stopwatch.GetTimestamp(),
            frequency = System.Diagnostics.Stopwatch.Frequency
        }));
    }

    private static Task<byte[]> StartCheckpointEncoding(SKImage raster, RasterCheckpoint state)
    {
        var task = Task.Run(() => { using (raster) return EncodeCheckpoint(raster, state); });
        ObserveCheckpointTask(task);
        return task;
    }

    private static void ObserveCheckpointTask(Task<byte[]> task)
    {
        _ = task.ContinueWith(static failed => { _ = failed.Exception; }, CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static CanvasState ReplayCheckpoint(SKCanvas canvas, SKPath path, string resource,
        uint width, uint height)
    {
        var checkpoint = JsonSerializer.Deserialize<RasterCheckpoint>(resource, CheckpointJson)
            ?? throw new InvalidOperationException("Invalid canvas checkpoint.");
        if (checkpoint.Version != 1 || checkpoint.Matrix.Length != 9)
            throw new InvalidOperationException("Unsupported canvas checkpoint.");
        using var image = SKImage.FromEncodedData(Convert.FromBase64String(checkpoint.Png));
        if (image is null || image.Width != width || image.Height != height)
            throw new InvalidOperationException("Canvas checkpoint dimensions differ.");
        canvas.ResetMatrix();
        using (var paint = new SKPaint { BlendMode = SKBlendMode.Src }) canvas.DrawImage(image,0,0,paint);
        var m = checkpoint.Matrix;
        canvas.SetMatrix(new SKMatrix { ScaleX=m[0],SkewX=m[1],TransX=m[2],SkewY=m[3],ScaleY=m[4],
            TransY=m[5],Persp0=m[6],Persp1=m[7],Persp2=m[8] });
        path.Reset();
        foreach (var segment in checkpoint.Path)
        {
            if(segment.Length!=10)throw new InvalidOperationException("Invalid checkpoint path.");
            switch((SKPathVerb)segment[0])
            {
                case SKPathVerb.Move:path.MoveTo(segment[1],segment[2]);break;
                case SKPathVerb.Line:path.LineTo(segment[3],segment[4]);break;
                case SKPathVerb.Quad:path.QuadTo(segment[3],segment[4],segment[5],segment[6]);break;
                case SKPathVerb.Conic:path.ConicTo(segment[3],segment[4],segment[5],segment[6],segment[9]);break;
                case SKPathVerb.Cubic:path.CubicTo(segment[3],segment[4],segment[5],segment[6],segment[7],segment[8]);break;
                case SKPathVerb.Close:path.Close();break;
                default:throw new InvalidOperationException("Invalid checkpoint path verb.");
            }
        }
        path.FillType=(SKPathFillType)checkpoint.FillType;
        return checkpoint.State;
    }
}

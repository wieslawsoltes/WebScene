using SkiaSharp;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed unsafe partial class NativeCanvasSceneRenderer
{
    internal long ResumedCanvasCompilations { get; private set; }
    internal bool UseIncrementalCanvasBacking { get; set; }
        = Environment.GetEnvironmentVariable("WEBSCENE_INCREMENTAL_CANVAS_GPU") == "1";

    private RetainedLayer CompileIncrementalCanvasLayer(NativeSceneView* view, in NativeCanvasLayer layer)
    {
        var isolation = RequiresIsolation(view, layer);
        var prefix = FindReplayPrefix(view, layer);
        s_layers.TryGetValue(layer.NodeId, out var previous);

        var bounds = new SKRect(0, 0, Math.Max(1, layer.BitmapWidth), Math.Max(1, layer.BitmapHeight));
        using var recorder = new SKPictureRecorder();
        var canvas = recorder.BeginRecording(bounds);
        var continuation = Replay(canvas, view, layer, !isolation, prefix, captureContinuation: true);
        using var suffix = recorder.EndRecording();
        SKPicture? full = null, gpu = null;
        var history = prefix is null ? new List<CanvasPictureNode>()
            : previous!.CpuHistory!.Select(node => node.Retain()).ToList();
        try
        {
            canvas = recorder.BeginRecording(bounds);
            canvas.DrawPicture(suffix);
            history.Add(new CanvasPictureNode(recorder.EndRecording(), 1));
            // A persistent balanced forest avoids a deep DrawPicture chain and
            // periodic full recompilation. Export still sees the complete CPU
            // history; appending only creates O(log n) lightweight picture nodes.
            while (history.Count >= 2 && history[^1].Weight == history[^2].Weight)
            {
                var right = history[^1]; var left = history[^2];
                canvas = recorder.BeginRecording(bounds);
                canvas.DrawPicture(left.Picture); canvas.DrawPicture(right.Picture);
                var combined = new CanvasPictureNode(recorder.EndRecording(), left.Weight + right.Weight);
                history.RemoveRange(history.Count - 2, 2);
                left.Dispose(); right.Dispose(); history.Add(combined);
            }
            canvas = recorder.BeginRecording(bounds);
            foreach (var node in history) canvas.DrawPicture(node.Picture);
            full = recorder.EndRecording();
            if (prefix is not null)
            {
                ResumedCanvasCompilations++;
                if (previous!.GpuPicture is not null)
                {
                    canvas = recorder.BeginRecording(bounds);
                    canvas.DrawPicture(previous.GpuPicture);
                    canvas.DrawPicture(suffix);
                    gpu = recorder.EndRecording();
                }
            }
            var result = new RetainedLayer(layer.NodeId, layer.Generation,
                layer.Reserved & ~OffscreenCanvasLayer, (layer.Reserved & OffscreenCanvasLayer) != 0,
                layer.X, layer.Y, layer.Width, layer.Height, layer.BitmapWidth, layer.BitmapHeight,
                layer.CommandCount, isolation, full)
            {
                ReplaySnapshot = continuation, GpuPicture = gpu,
                GpuContext = gpu is null ? null : previous!.GpuContext,
                CpuHistory = history.ToArray()
            };
            history.Clear();
            full = null; gpu = null; continuation = null;
            return result;
        }
        finally { foreach (var node in history) node.Dispose(); full?.Dispose(); gpu?.Dispose(); continuation?.Dispose(); }
    }

    private CanvasReplaySnapshot? FindReplayPrefix(NativeSceneView* view, in NativeCanvasLayer layer)
        => UseIncrementalCanvasBacking
            && s_layers.TryGetValue(layer.NodeId, out var previous)
            && previous.Generation == layer.Generation
            && previous.BitmapWidth == layer.BitmapWidth && previous.BitmapHeight == layer.BitmapHeight
            && previous.ReplaySnapshot is { } retained
            && retained.Matches(view, layer, _presenterDeviceScaleFactor, s_revision)
                ? retained : null;

    internal sealed class CanvasPictureNode(SKPicture picture, long weight) : IDisposable
    {
        internal readonly SKPicture Picture = picture;
        internal readonly long Weight = weight;
        private int _references = 1;
        internal CanvasPictureNode Retain() { Interlocked.Increment(ref _references); return this; }
        public void Dispose() { if (Interlocked.Decrement(ref _references) == 0) Picture.Dispose(); }
    }

    // Called only while the owning graphics lease is current. All pixels stay
    // on the GPU: the previous immutable image plus appended commands becomes
    // the next backing image. The CPU picture remains intact for export and
    // context-loss fallback. No native WebGPU image lifetime is changed here.
    private static SKPicture? MaterializeCanvasBacking(RetainedLayer layer, GRContext context)
    {
        if (layer.ReplaySnapshot is null || !layer.RequiresIsolation || context.IsAbandoned
            || (ulong)layer.BitmapWidth * layer.BitmapHeight > 4 * 1024 * 1024) return null;
        if (layer.IsMaterialized && ReferenceEquals(layer.GpuContext, context)) return layer.GpuPicture;
        using var surface = SKSurface.Create(context, false, new SKImageInfo(
            checked((int)layer.BitmapWidth), checked((int)layer.BitmapHeight),
            SKColorType.Rgba8888, SKAlphaType.Premul));
        if (surface is null) return null;
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.DrawPicture(ReferenceEquals(layer.GpuContext, context) && layer.GpuPicture is not null
            ? layer.GpuPicture : layer.Picture);
        SKImage? image = surface.Snapshot();
        try
        {
            using var recorder = new SKPictureRecorder();
            var canvas = recorder.BeginRecording(new SKRect(0, 0, layer.BitmapWidth, layer.BitmapHeight));
            canvas.DrawImage(image, 0, 0);
            var picture = recorder.EndRecording();
            layer.GpuPicture?.Dispose();
            layer.GpuPicture = picture;
            layer.GpuContext = context;
            layer.IsMaterialized = true;
            layer.CheckpointImage?.Dispose();
            layer.CheckpointImage = image;
            image = null;
            return picture;
        }
        finally { image?.Dispose(); }
    }

    // An exact immutable prefix is required; generation alone does not prove
    // that commands or string resources remained unchanged. Active save/clip
    // stacks and mutable canvas/image dependencies deliberately use full replay.
    internal sealed class CanvasReplaySnapshot : IDisposable
    {
        internal readonly CanvasState State;
        internal readonly SKMatrix Matrix;
        internal readonly SKPath Path;
        internal readonly bool HasDrawn;
        internal readonly int CommandCount, Depth;
        private readonly byte[][] _commandChunks;
        private readonly bool _hasExactCommands;
        private readonly string[] _strings;
        private readonly float _scale;
        private readonly long _fontVersion = NativeTextShaping.FontRegistrationVersion;

        internal CanvasReplaySnapshot(NativeSceneView* view, in NativeCanvasLayer layer,
            CanvasState state, SKMatrix matrix, SKPath path, bool hasDrawn, float scale, CanvasReplaySnapshot? prefix)
        {
            State = state; Matrix = matrix; Path = new SKPath(path); HasDrawn = hasDrawn;
            CommandCount = checked((int)layer.CommandCount); Depth = (prefix?.Depth ?? -1) + 1; _scale = scale;
            _hasExactCommands = (layer.Flags & LayerUnchangedPrefix) == 0
                && (prefix is null || prefix._hasExactCommands)
                && CommandCount * (long)sizeof(NativeCanvasCommand) <= 16 * 1024 * 1024;
            if (_hasExactCommands)
            {
                var start = prefix?.CommandCount ?? 0;
                var appended = new ReadOnlySpan<byte>(view->CanvasCommands + layer.CommandOffset + start,
                    checked((CommandCount - start) * sizeof(NativeCanvasCommand))).ToArray();
                _commandChunks = prefix is null ? [appended] : [..prefix._commandChunks, appended];
            }
            else _commandChunks = [];
            _strings = new string[checked((int)layer.StringCount)];
            for (var index = 0; index < _strings.Length; ++index)
                _strings[index] = DomStringAt(view, layer.StringOffset + (uint)index);
        }

        internal bool Matches(NativeSceneView* view, in NativeCanvasLayer layer, float scale, ulong revision)
        {
            if (_scale != scale || _fontVersion != NativeTextShaping.FontRegistrationVersion
                || layer.CommandCount < CommandCount || layer.StringCount < _strings.Length) return false;
            // The engine verified these prefixes against this exact base
            // revision. Older runtimes omit the hint and use byte comparisons.
            if ((layer.Flags & LayerUnchangedPrefix) != 0 && view->Header.BaseRevision == revision) return true;
            if (!_hasExactCommands) return false;
            var current = (byte*)(view->CanvasCommands + layer.CommandOffset);
            foreach (var chunk in _commandChunks)
            {
                if (!chunk.AsSpan().SequenceEqual(new ReadOnlySpan<byte>(current, chunk.Length))) return false;
                current += chunk.Length;
            }
            for (var index = 0; index < _strings.Length; ++index)
                if (_strings[index] != DomStringAt(view, layer.StringOffset + (uint)index)) return false;
            return true;
        }

        public void Dispose() => Path.Dispose();
    }
}

using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using SkiaSharp;
using WebScene.Backends.Avalonia.Native;

namespace WebScene.NativeEngine.Benchmarks;

internal static unsafe class NativeRetainedRenderProbe
{
    private const uint SceneCheckpoint = 1;
    private const uint LayerReplace = 1;
    private const uint FillRect = 22;
    private const int ViewportWidth = 320;
    private const int ViewportHeight = 240;
    private const int LayerSize = 12;

    internal static int Run(string[] args)
    {
        var layerCount = ReadIntOption(args, "--layers", 2_048);
        var visibleLayerCount = ReadIntOption(args, "--visible", 32);
        var iterations = ReadIntOption(args, "--iterations", 40);
        var samples = ReadIntOption(args, "--samples", 11);
        if (layerCount is < 1 or > 100_000
            || visibleLayerCount is < 1 || visibleLayerCount > layerCount
            || iterations is < 1 or > 10_000
            || samples is < 3 or > 101)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Layers must be 1-100000, visible layers must be within that range, " +
                "iterations must be 1-10000, and samples must be 3-101.");
        }

        using var sparse = new RenderFixture(
            layerCount,
            visibleLayerCount,
            placeAllInViewport: false);
        using var visibleOnly = new RenderFixture(
            visibleLayerCount,
            visibleLayerCount,
            placeAllInViewport: false);
        using var dense = new RenderFixture(
            layerCount,
            layerCount,
            placeAllInViewport: true);
        using var sparseWithReplacement = new RenderFixture(
            layerCount,
            visibleLayerCount,
            placeAllInViewport: false);

        var sparseHash = sparse.RenderAndHash();
        var referenceHash = visibleOnly.RenderAndHash();
        if (!CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(sparseHash),
                Convert.FromHexString(referenceHash)))
        {
            throw new InvalidOperationException(
                "The retained renderer's sparse frame differs from the visible-only reference frame.");
        }

        sparse.WarmUp();
        dense.WarmUp();
        sparseWithReplacement.WarmUpApplyAndRender();
        var sparseTiming = Measure(sparse.Render, iterations, samples);
        var denseTiming = Measure(dense.Render, iterations, samples);
        var sparseReplacementTiming = Measure(
            sparseWithReplacement.ApplyNextAndRender,
            iterations,
            samples);
        var sparseReplacementHash = sparseWithReplacement.RenderAndHash();
        if (!CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(sparseReplacementHash),
                Convert.FromHexString(referenceHash)))
        {
            throw new InvalidOperationException(
                "A replaced sparse frame differs from the visible-only reference frame.");
        }
        Console.WriteLine(JsonSerializer.Serialize(
            new
            {
                schema = "webscene-native-retained-render-v1",
                capturedUtc = DateTimeOffset.UtcNow,
                options = new
                {
                    layerCount,
                    visibleLayerCount,
                    iterations,
                    samples,
                    viewportWidth = ViewportWidth,
                    viewportHeight = ViewportHeight
                },
                correctness = new
                {
                    surfaceClearedBeforeEveryRender = true,
                    sparseFrameSha256 = sparseHash,
                    sparseReplacementFrameSha256 = sparseReplacementHash,
                    visibleOnlyReferenceSha256 = referenceHash,
                    framesMatch = true
                },
                sparse = sparseTiming,
                dense = denseTiming,
                sparseWithReplacement = sparseReplacementTiming
            },
            new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }

    private static TimingSummary Measure(
        Action operation,
        int iterations,
        int samples)
    {
        var nanosecondsPerRender = new double[samples];
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var sample = 0; sample < samples; sample++)
        {
            var started = Stopwatch.GetTimestamp();
            for (var iteration = 0; iteration < iterations; iteration++)
            {
                operation();
            }
            nanosecondsPerRender[sample] =
                Stopwatch.GetElapsedTime(started).TotalNanoseconds / iterations;
        }
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        Array.Sort(nanosecondsPerRender);
        return new TimingSummary(
            nanosecondsPerRender[samples / 2],
            Percentile(nanosecondsPerRender, 0.95),
            nanosecondsPerRender.Average(),
            allocatedBytes / checked(samples * iterations));
    }

    private static double Percentile(IReadOnlyList<double> sorted, double percentile)
    {
        var index = (int)Math.Ceiling(percentile * sorted.Count) - 1;
        return sorted[Math.Clamp(index, 0, sorted.Count - 1)];
    }

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(args[index + 1], out var value))
            {
                return value;
            }
        }
        return fallback;
    }

    private sealed class RenderFixture : IDisposable
    {
        private readonly NativeCanvasSceneRenderer _renderer = new();
        private readonly SKSurface _surface;
        private readonly NativeCanvasLayer[] _layouts;
        private readonly NativeCanvasLayer[] _replacement = new NativeCanvasLayer[1];
        private readonly NativeCanvasCommand[] _replacementCommand = CreateCommands(1);
        private readonly int _visibleLayerCount;
        private ulong _revision = 1;
        private ulong _generation = 1;
        private int _nextReplacement;

        internal RenderFixture(
            int layerCount,
            int visibleLayerCount,
            bool placeAllInViewport)
        {
            _visibleLayerCount = visibleLayerCount;
            _surface = SKSurface.Create(new SKImageInfo(
                ViewportWidth,
                ViewportHeight,
                SKColorType.Rgba8888,
                SKAlphaType.Premul))
                ?? throw new InvalidOperationException("Could not create the benchmark surface.");
            _layouts = CreateLayers(
                layerCount,
                visibleLayerCount,
                placeAllInViewport);
            ApplyScene(
                _renderer,
                _layouts,
                CreateCommands(layerCount),
                revision: 1,
                baseRevision: 0,
                checkpoint: true);
        }

        internal void WarmUpApplyAndRender()
        {
            for (var iteration = 0; iteration < 20; iteration++)
            {
                ApplyNextAndRender();
            }
        }

        internal void ApplyNextAndRender()
        {
            var index = _nextReplacement++ % _visibleLayerCount;
            var replacement = _layouts[index];
            replacement.CommandOffset = 0;
            replacement.Generation = ++_generation;
            _replacement[0] = replacement;
            var baseRevision = _revision++;
            ApplyScene(
                _renderer,
                _replacement,
                _replacementCommand,
                _revision,
                baseRevision,
                checkpoint: false);
            Render();
        }

        internal void WarmUp()
        {
            for (var iteration = 0; iteration < 20; iteration++)
            {
                Render();
            }
        }

        internal void Render()
        {
            // Never rely on Avalonia or Skia preserving pixels outside an
            // invalidated area. A valid optimization must reconstruct the
            // complete viewport from transparent on every callback.
            _surface.Canvas.Clear(SKColors.Transparent);
            _renderer.RenderRetained(
                _surface.Canvas,
                ViewportWidth,
                ViewportHeight,
                intersects: null);
            _surface.Canvas.Flush();
        }

        internal string RenderAndHash()
        {
            Render();
            using var image = _surface.Snapshot();
            using var pixels = image.PeekPixels()
                ?? throw new InvalidOperationException("Could not read benchmark pixels.");
            var bytes = pixels.GetPixelSpan().ToArray();
            return Convert.ToHexString(SHA256.HashData(bytes));
        }

        public void Dispose()
        {
            _renderer.Reset();
            _surface.Dispose();
        }
    }

    private static NativeCanvasLayer[] CreateLayers(
        int layerCount,
        int visibleLayerCount,
        bool placeAllInViewport)
    {
        var layers = new NativeCanvasLayer[layerCount];
        var columns = Math.Max(1, ViewportWidth / LayerSize);
        for (var index = 0; index < layerCount; index++)
        {
            var inViewport = placeAllInViewport || index < visibleLayerCount;
            var visibleIndex = placeAllInViewport
                ? index % Math.Max(1, visibleLayerCount)
                : index;
            var x = inViewport
                ? (visibleIndex % columns) * LayerSize
                : ViewportWidth + LayerSize + (index % 32) * LayerSize;
            var y = inViewport
                ? (visibleIndex / columns * LayerSize) % ViewportHeight
                : ViewportHeight + LayerSize + (index % 32) * LayerSize;
            layers[index] = new NativeCanvasLayer
            {
                NodeId = checked((uint)index + 1),
                Flags = LayerReplace,
                CommandOffset = checked((uint)index),
                CommandCount = 1,
                Reserved = checked((uint)index),
                X = x,
                Y = y,
                Width = LayerSize,
                Height = LayerSize,
                BitmapWidth = LayerSize,
                BitmapHeight = LayerSize,
                Generation = 1
            };
        }
        return layers;
    }

    private static NativeCanvasCommand[] CreateCommands(int layerCount)
    {
        var commands = new NativeCanvasCommand[layerCount];
        for (var index = 0; index < layerCount; index++)
        {
            commands[index] = new NativeCanvasCommand
            {
                Kind = FillRect,
                V2 = LayerSize,
                V3 = LayerSize
            };
        }
        return commands;
    }

    private static void ApplyScene(
        NativeCanvasSceneRenderer renderer,
        NativeCanvasLayer[] layers,
        NativeCanvasCommand[] commands,
        ulong revision,
        ulong baseRevision,
        bool checkpoint)
    {
        fixed (NativeCanvasLayer* layerPointer = layers)
        fixed (NativeCanvasCommand* commandPointer = commands)
        {
            var view = new NativeSceneView
            {
                StructSize = checked((uint)sizeof(NativeSceneView)),
                AbiVersion = 2,
                Header = new SceneHeader
                {
                    Revision = revision,
                    BaseRevision = baseRevision,
                    ViewportWidth = ViewportWidth,
                    ViewportHeight = ViewportHeight,
                    CanvasLayerCount = checked((uint)layers.Length),
                    Flags = checkpoint ? SceneCheckpoint : 0
                },
                CanvasLayers = layerPointer,
                CanvasCommands = commandPointer,
                CanvasCommandCount = checked((uint)commands.Length)
            };
            if (!renderer.ApplyDiff(&view))
            {
                throw new InvalidOperationException(
                    "The retained renderer rejected the benchmark checkpoint.");
            }
        }
    }

    private sealed record TimingSummary(
        double MedianNanosecondsPerRender,
        double P95NanosecondsPerRender,
        double MeanNanosecondsPerRender,
        long AllocatedBytesPerRender);
}

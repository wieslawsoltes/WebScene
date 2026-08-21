using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;

namespace WebScene.NativeEngine.Benchmarks;

internal static unsafe class NativeRetainedApplyProbe
{
    private const uint SceneCheckpoint = 1;
    private const uint LayerReplace = 1;
    private const uint FillRect = 22;

    internal static int Run(string[] args)
    {
        var layerCount = ReadIntOption(args, "--layers", 4_096);
        var batchSize = ReadIntOption(args, "--batch", 256);
        var iterations = ReadIntOption(args, "--iterations", 100);
        var samples = ReadIntOption(args, "--samples", 11);
        if (layerCount is < 2 or > 100_000
            || batchSize is < 1 || batchSize > layerCount
            || iterations is < 1 or > 10_000
            || samples is < 3 or > 101)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Layers must be 2-100000, batch must be within that range, " +
                "iterations must be 1-10000, and samples must be 3-101.");
        }

        using var sparseFixture = new ApplyFixture(layerCount, changeCount: 1);
        using var batchFixture = new ApplyFixture(layerCount, batchSize);
        using var reorderFixture = new ApplyFixture(
            layerCount,
            changeCount: 1,
            reorder: true);
        sparseFixture.WarmUp();
        batchFixture.WarmUp();
        reorderFixture.WarmUp();

        var sparseTiming = Measure(
            sparseFixture.ApplyNext,
            iterations,
            samples);
        var batchTiming = Measure(
            batchFixture.ApplyNext,
            iterations,
            samples);
        var reorderTiming = Measure(
            reorderFixture.ApplyNext,
            iterations,
            samples);
        if (!sparseFixture.HasConsistentOrder
            || !batchFixture.HasConsistentOrder
            || !reorderFixture.HasConsistentOrder)
        {
            throw new InvalidOperationException(
                "The retained renderer's layer identity/order index is inconsistent.");
        }

        var result = new
        {
            schema = "webscene-native-retained-apply-v1",
            capturedUtc = DateTimeOffset.UtcNow,
            options = new { layerCount, batchSize, iterations, samples },
            correctness = new { retainedOrderConsistent = true },
            sparseReplacement = sparseTiming,
            batchReplacement = batchTiming,
            zOrderChange = reorderTiming
        };
        Console.WriteLine(JsonSerializer.Serialize(
            result,
            new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }

    private static TimingSummary Measure(
        Action operation,
        int iterations,
        int samples)
    {
        var nanosecondsPerOperation = new double[samples];
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (var sample = 0; sample < samples; sample++)
        {
            var started = Stopwatch.GetTimestamp();
            for (var iteration = 0; iteration < iterations; iteration++)
            {
                operation();
            }
            nanosecondsPerOperation[sample] =
                Stopwatch.GetElapsedTime(started).TotalNanoseconds / iterations;
        }
        var allocatedBytes = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
        Array.Sort(nanosecondsPerOperation);
        return new TimingSummary(
            nanosecondsPerOperation[samples / 2],
            Percentile(nanosecondsPerOperation, 0.95),
            nanosecondsPerOperation.Average(),
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

    private sealed class ApplyFixture : IDisposable
    {
        private readonly NativeCanvasSceneRenderer _renderer = new();
        private readonly int _layerCount;
        private readonly bool _reorder;
        private readonly NativeCanvasLayer[] _changes;
        private readonly NativeCanvasCommand[] _commands;
        private ulong _revision = 1;
        private ulong _generation = 1;
        private int _nextIndex;
        private bool _movedToEnd;

        internal ApplyFixture(int layerCount, int changeCount, bool reorder = false)
        {
            _layerCount = layerCount;
            _reorder = reorder;
            _changes = new NativeCanvasLayer[changeCount];
            _commands = CreateCommands(changeCount);
            var checkpointLayers = new NativeCanvasLayer[layerCount];
            for (var index = 0; index < layerCount; index++)
            {
                checkpointLayers[index] = CreateLayer(
                    index,
                    commandOffset: index,
                    generation: 1,
                    zOrder: checked((uint)index));
            }
            Apply(
                checkpointLayers,
                CreateCommands(layerCount),
                checkpoint: true);
        }

        internal void WarmUp()
        {
            for (var iteration = 0; iteration < 20; iteration++)
            {
                ApplyNext();
            }
        }

        internal void ApplyNext()
        {
            _generation++;
            for (var change = 0; change < _changes.Length; change++)
            {
                var index = _reorder
                    ? 0
                    : (_nextIndex + change) % _layerCount;
                var zOrder = _reorder && !_movedToEnd
                    ? checked((uint)_layerCount + 1)
                    : checked((uint)index);
                _changes[change] = CreateLayer(
                    index,
                    commandOffset: change,
                    _generation,
                    zOrder);
            }
            _nextIndex = (_nextIndex + _changes.Length) % _layerCount;
            if (_reorder)
            {
                _movedToEnd = !_movedToEnd;
            }
            Apply(_changes, _commands, checkpoint: false);
        }

        internal bool HasConsistentOrder => _renderer.HasConsistentLayerOrder();

        public void Dispose() => _renderer.Reset();

        private void Apply(
            NativeCanvasLayer[] layers,
            NativeCanvasCommand[] commands,
            bool checkpoint)
        {
            var baseRevision = checkpoint ? 0 : _revision;
            if (!checkpoint)
            {
                _revision++;
            }
            fixed (NativeCanvasLayer* layerPointer = layers)
            fixed (NativeCanvasCommand* commandPointer = commands)
            {
                var view = new NativeSceneView
                {
                    StructSize = checked((uint)sizeof(NativeSceneView)),
                    AbiVersion = 2,
                    Header = new SceneHeader
                    {
                        Revision = _revision,
                        BaseRevision = baseRevision,
                        ViewportWidth = 320,
                        ViewportHeight = 240,
                        CanvasLayerCount = checked((uint)layers.Length),
                        Flags = checkpoint ? SceneCheckpoint : 0
                    },
                    CanvasLayers = layerPointer,
                    CanvasCommands = commandPointer,
                    CanvasCommandCount = checked((uint)commands.Length)
                };
                if (!_renderer.ApplyDiff(&view))
                {
                    throw new InvalidOperationException(
                        "The retained renderer rejected an apply benchmark diff.");
                }
            }
        }
    }

    private static NativeCanvasLayer CreateLayer(
        int index,
        int commandOffset,
        ulong generation,
        uint zOrder)
        => new()
        {
            NodeId = checked((uint)index + 1),
            Flags = LayerReplace,
            CommandOffset = checked((uint)commandOffset),
            CommandCount = 1,
            Reserved = zOrder,
            X = index % 32 * 10,
            Y = index / 32 * 10,
            Width = 8,
            Height = 8,
            BitmapWidth = 8,
            BitmapHeight = 8,
            Generation = generation
        };

    private static NativeCanvasCommand[] CreateCommands(int commandCount)
    {
        var commands = new NativeCanvasCommand[commandCount];
        for (var index = 0; index < commandCount; index++)
        {
            commands[index] = new NativeCanvasCommand
            {
                Kind = FillRect,
                V2 = 8,
                V3 = 8
            };
        }
        return commands;
    }

    private sealed record TimingSummary(
        double MedianNanosecondsPerOperation,
        double P95NanosecondsPerOperation,
        double MeanNanosecondsPerOperation,
        long AllocatedBytesPerOperation);
}

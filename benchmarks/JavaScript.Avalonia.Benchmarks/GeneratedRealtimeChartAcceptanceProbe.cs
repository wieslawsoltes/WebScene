using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;
using WebScene.JavaScript.Interop;
using WebScene.NativeInterop.Benchmarks.Generated;

namespace JavaScript.Avalonia.Benchmarks;

internal static class GeneratedRealtimeChartAcceptanceProbe
{
    private const string InstallHost = """
        globalThis.realtimeChartHost = {
          count: 0,
          close: 0,
          onRealtimeUpdate(subscriberUid, bar) {
            this.count++;
            this.close = bar.close;
          },
          onHistoryResponse(requestId, bars) {
            this.count += bars.length;
          },
          getHistory() { return Promise.resolve([]); },
          updateCount() { return this.count; },
          lastClose() { return this.close; }
        };
        """;

    internal static async Task<int> RunAsync(string[] args)
    {
        var mode = ReadOption(args, "--mode", "binary");
        if (!string.Equals(mode, "binary", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException(
                "The JSON transport was removed; --mode must be 'binary'.",
                nameof(args));
        }
        var chartCount = ReadIntOption(args, "--charts", 4);
        var ticks = ReadIntOption(args, "--ticks", 600);
        var rate = ReadIntOption(args, "--rate", 60);
        if (chartCount <= 0 || ticks <= 0 || rate <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Charts, ticks, and rate must be positive.");
        }

        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);

        var engines = new List<EngineState>(chartCount);
        try
        {
            for (var index = 0; index < chartCount; index++)
            {
                engines.Add(await CreateEngineAsync());
            }

            var bar = new RealtimeChartBar
            {
                Time = 1_785_410_000_000,
                Open = 101.25,
                High = 102.50,
                Low = 100.75,
                Close = 102.125,
                Volume = 42_000
            };
            for (var tick = 0; tick < 64; tick++)
            {
                for (var index = 0; index < engines.Count; index++)
                {
                    await engines[index].Host.OnRealtimeUpdateAsync(
                        "subscriber-0",
                        bar);
                }
            }

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var allocationCounterProbe =
                GC.GetTotalAllocatedBytes(precise: true);
            var allocationCounterOverhead =
                GC.GetTotalAllocatedBytes(precise: true)
                - allocationCounterProbe;
            var process = Process.GetCurrentProcess();
            process.Refresh();
            var startCpu = process.TotalProcessorTime;
            var startAllocated = GC.GetTotalAllocatedBytes(precise: true);
            var startHeap = GC.GetGCMemoryInfo().HeapSizeBytes;
            var startWorkingSet = process.WorkingSet64;
            var startGen0 = GC.CollectionCount(0);
            var startGen1 = GC.CollectionCount(1);
            var startGen2 = GC.CollectionCount(2);
            var elapsed = Stopwatch.StartNew();
            var tickDuration = TimeSpan.FromSeconds(1d / rate);

            for (var tick = 0; tick < ticks; tick++)
            {
                var target = TimeSpan.FromTicks(
                    tickDuration.Ticks * (tick + 1L));
                for (var index = 0; index < engines.Count; index++)
                {
                    await engines[index].Host.OnRealtimeUpdateAsync(
                        "subscriber-0",
                        bar);
                }
                var remaining = target - elapsed.Elapsed;
                if (remaining > TimeSpan.Zero)
                {
                    Thread.Sleep(remaining);
                }
            }

            elapsed.Stop();
            var rawAllocated = GC.GetTotalAllocatedBytes(precise: true)
                - startAllocated;
            var allocated = Math.Max(
                0,
                rawAllocated - allocationCounterOverhead);
            process.Refresh();
            var cpu = process.TotalProcessorTime - startCpu;
            var heap = GC.GetGCMemoryInfo().HeapSizeBytes;
            var expectedCount = 64d + ticks;
            var observed = new double[engines.Count];
            for (var index = 0; index < engines.Count; index++)
            {
                observed[index] =
                    await engines[index].Host.UpdateCountAsync();
            }
            var correct = observed.All(
                value => Math.Abs(value - expectedCount) < 0.5);

            var pool = engines
                .Where(static state => state.Transport is not null)
                .Select(static state => state.Transport!.PoolMetrics)
                .ToArray();
            var result = new
            {
                mode = "binary",
                charts = chartCount,
                ticks,
                targetRatePerChart = rate,
                calls = checked(chartCount * ticks),
                elapsedMilliseconds = elapsed.Elapsed.TotalMilliseconds,
                processCpuMilliseconds = cpu.TotalMilliseconds,
                normalizedProcessCpuPercent =
                    cpu.TotalMilliseconds / elapsed.Elapsed.TotalMilliseconds
                    * 100d,
                managedAllocatedBytes = allocated,
                rawManagedAllocatedBytes = rawAllocated,
                allocationCounterOverheadBytes =
                    allocationCounterOverhead,
                managedBytesPerCall =
                    (double)allocated / checked(chartCount * ticks),
                gen0Collections = GC.CollectionCount(0) - startGen0,
                gen1Collections = GC.CollectionCount(1) - startGen1,
                gen2Collections = GC.CollectionCount(2) - startGen2,
                startManagedHeapBytes = startHeap,
                endManagedHeapBytes = heap,
                managedHeapDeltaBytes = heap - startHeap,
                startWorkingSetBytes = startWorkingSet,
                endWorkingSetBytes = process.WorkingSet64,
                workingSetDeltaBytes = process.WorkingSet64 - startWorkingSet,
                correct,
                observedCounts = observed,
                nativePool = new
                {
                    outstandingResults =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.OutstandingResults),
                    pooledBytes =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledBytes),
                    hits =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PoolHits),
                    misses =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PoolMisses),
                    oversizeAllocations =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.OversizeAllocations),
                    highWaterOutstandingResults =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.HighWaterOutstandingResults),
                    pooledRequestRecords =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledRequestRecords),
                    requestPoolHits =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.RequestPoolHits),
                    requestPoolMisses =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.RequestPoolMisses),
                    requestOversizeAllocations =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.RequestOversizeAllocations),
                    activeOperationSlots =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.ActiveOperationSlots),
                    availableOperationSlots =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.AvailableOperationSlots),
                    operationSlotHighWater =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.OperationSlotHighWater),
                    pooledResultBytes4K =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledResultBytes4K),
                    pooledResultBytes16K =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledResultBytes16K),
                    pooledResultBytes64K =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledResultBytes64K),
                    pooledResultBytes256K =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledResultBytes256K),
                    pooledResultBytes1M =
                        pool.Aggregate(0UL, static (sum, item) =>
                            sum + item.PooledResultBytes1M)
                }
            };
            Console.WriteLine(JsonSerializer.Serialize(
                result,
                new JsonSerializerOptions { WriteIndented = true }));
            return correct ? 0 : 1;
        }
        finally
        {
            foreach (var state in engines)
            {
                await state.Host.DisposeAsync();
                state.Transport.Dispose();
                NativeWebSceneApi.EngineDestroy(state.Engine);
            }
        }
    }

    private static async Task<EngineState> CreateEngineAsync()
    {
        var engine = NativeWebSceneApi.EngineCreate(
            simulatedChartCommandCount: 0,
            compilationCacheDirectory: null,
            EmptyResourceLoader.Instance,
            static _ => { });
        if (engine == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native chart acceptance engine could not be created.");
        }
        if (!NativeWebSceneApi.TryExecuteScript(
                engine,
                InstallHost,
                "generated-realtime-chart-acceptance.js"))
        {
            NativeWebSceneApi.EngineDestroy(engine);
            throw new InvalidOperationException(
                "The native chart acceptance host could not be installed.");
        }

        var transport = new NativeJavaScriptBinaryTransport(engine);
        var invoker = new NativeJavaScriptInvoker(transport);
        try
        {
            var host =
                await JavaScriptGlobals.GetRealtimeChartHostAsync(invoker);
            return new EngineState(
                engine,
                transport,
                host);
        }
        catch
        {
            transport.Dispose();
            NativeWebSceneApi.EngineDestroy(engine);
            throw;
        }
    }

    private static string ReadOption(
        IReadOnlyList<string> args,
        string name,
        string fallback)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(
                    args[index],
                    name,
                    StringComparison.OrdinalIgnoreCase))
            {
                return args[index + 1];
            }
        }
        return fallback;
    }

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback)
        => int.TryParse(
            ReadOption(args, name, fallback.ToString()),
            out var value)
            ? value
            : fallback;

    private sealed record EngineState(
        IntPtr Engine,
        NativeJavaScriptBinaryTransport Transport,
        RealtimeChartHost Host);

    private sealed class EmptyResourceLoader : IWebSceneResourceLoader
    {
        internal static EmptyResourceLoader Instance { get; } = new();

        public WebSceneTextResource LoadText(
            in WebSceneResourceRequest request)
            => throw new InvalidOperationException(
                $"Unexpected acceptance resource request '{request.Specifier}'.");
    }
}

using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

namespace WebScene.NativeEngine.Benchmarks;

internal static class NativeInspectorDisabledPerformanceProbe
{
    private const uint InspectorBuildFeature = 1U << 1;
    private const string ConsoleCompleteMarker = "__webscene_perf_console_complete__";
    private const string WorkloadCompleteMarker = "__webscene_perf_workload_complete__";

    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var contextCount = ReadIntOption(args, "--contexts", 4);
        var samples = ReadIntOption(args, "--samples", 10);
        var durationMilliseconds = ReadIntOption(args, "--duration-ms", 1_500);
        var timerTarget = ReadIntOption(args, "--timer-target", 200);
        var frameTarget = ReadIntOption(args, "--frame-target", 60);
        var consoleIterations = ReadIntOption(args, "--console-iterations", 1_000);
        var workloadNodes = ReadIntOption(args, "--workload-nodes", 1_000);
        if (contextCount is < 1 or > 16 || samples is < 1 or > 100 ||
            durationMilliseconds < 250 || timerTarget < 1 || frameTarget < 1 ||
            consoleIterations < 1 || workloadNodes < 1)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Contexts must be 1-16, samples 1-100, duration at least 250 ms, " +
                "and workload counts must be positive.");
        }

        var library = Environment.GetEnvironmentVariable("WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        library = Path.GetFullPath(library);
        var buildFeaturesExportAvailable = TryReadBuildFeatures(
            library,
            out var buildFeatures);
        var inspectorCompiledIn =
            (buildFeatures & InspectorBuildFeature) != 0;

        NativeWebSceneApi.ConfigureLibraryPath(library);
        var viewConstructionBytes = MeasureViewConstructionBytes();
        var process = Process.GetCurrentProcess();
        ForceCollection();
        process.Refresh();
        var processBaselineWorkingSet = process.WorkingSet64;
        var processBaselineAllocatedBytes = GC.GetTotalAllocatedBytes(precise: true);
        var prewarmStart = Stopwatch.GetTimestamp();
        if (NativeWebSceneApi.EnginePrewarm() == 0)
        {
            throw new InvalidOperationException("The native runtime failed to prewarm V8.");
        }
        var prewarmMilliseconds = Stopwatch.GetElapsedTime(prewarmStart).TotalMilliseconds;
        var prewarmAllocatedBytes =
            GC.GetTotalAllocatedBytes(precise: true) - processBaselineAllocatedBytes;

        var createMilliseconds = new List<double>(samples);
        var firstSceneMilliseconds = new List<double>(samples);
        var blankMemory = new List<MemorySample>(samples);
        var blankLifecycleAllocatedBytes = new List<long>(samples);
        for (var sample = 0; sample < samples; sample++)
        {
            var allocatedBefore = GC.GetTotalAllocatedBytes(precise: true);
            var state = CreateEngine();
            createMilliseconds.Add(state.CreateMilliseconds);
            firstSceneMilliseconds.Add(state.FirstSceneMilliseconds);
            blankMemory.Add(ReadMemory(state.Engine));
            state.Dispose();
            blankLifecycleAllocatedBytes.Add(
                GC.GetTotalAllocatedBytes(precise: true) - allocatedBefore);
        }

        ForceCollection();
        process.Refresh();
        var beforeViewsWorkingSet = process.WorkingSet64;
        var beforeViewsAllocatedBytes = GC.GetTotalAllocatedBytes(precise: true);
        var engines = new List<EngineState>(contextCount);
        try
        {
            for (var index = 0; index < contextCount; index++)
            {
                var state = CreateEngine();
                if (!NativeWebSceneApi.TryEnableRuntimeWorkMetrics(state.Engine))
                {
                    state.Dispose();
                    throw new InvalidOperationException(
                        "The native runtime-work metrics ABI is unavailable.");
                }
                engines.Add(state);
            }

            ForceCollection();
            process.Refresh();
            var populatedViewsWorkingSet = process.WorkingSet64;
            var populatedViewsAllocatedBytes = GC.GetTotalAllocatedBytes(precise: true);
            var blankViewMemory = engines.Select(state => ReadMemory(state.Engine)).ToArray();

            var idleBefore = engines.Select(state => ReadRuntimeWork(state.Engine)).ToArray();
            process.Refresh();
            var idleCpuBefore = process.TotalProcessorTime;
            var idleElapsed = Stopwatch.StartNew();
            Thread.Sleep(durationMilliseconds);
            idleElapsed.Stop();
            process.Refresh();
            var idleCpu = process.TotalProcessorTime - idleCpuBefore;
            var idleAfter = engines.Select(state => ReadRuntimeWork(state.Engine)).ToArray();

            var timerBefore = engines.Select(state => ReadRuntimeWork(state.Engine)).ToArray();
            process.Refresh();
            var timerCpuBefore = process.TotalProcessorTime;
            var timerElapsed = Stopwatch.StartNew();
            for (var index = 0; index < engines.Count; index++)
            {
                Execute(
                    engines[index].Engine,
                    TimerAndFrameFixture(timerTarget, frameTarget),
                    $"inspector-disabled-timer-raf-{index}.js");
            }
            WaitForRuntimeWork(
                engines,
                timerBefore,
                timerTarget,
                frameTarget,
                TimeSpan.FromSeconds(10));
            timerElapsed.Stop();
            process.Refresh();
            var timerCpu = process.TotalProcessorTime - timerCpuBefore;
            var timerAfter = engines.Select(state => ReadRuntimeWork(state.Engine)).ToArray();

            process.Refresh();
            var consoleCpuBefore = process.TotalProcessorTime;
            var consoleElapsed = Stopwatch.StartNew();
            for (var index = 0; index < engines.Count; index++)
            {
                Execute(
                    engines[index].Engine,
                    ConsoleFixture(consoleIterations, ConsoleCompleteMarker),
                    $"inspector-disabled-console-{index}.js");
            }
            var consoleCompletion = WaitForConsoleMarker(
                engines,
                ConsoleCompleteMarker,
                TimeSpan.FromSeconds(10));
            consoleElapsed.Stop();
            process.Refresh();
            var consoleCpu = process.TotalProcessorTime - consoleCpuBefore;

            process.Refresh();
            var workloadCpuBefore = process.TotalProcessorTime;
            var workloadElapsed = Stopwatch.StartNew();
            for (var index = 0; index < engines.Count; index++)
            {
                Execute(
                    engines[index].Engine,
                    RepresentativeWorkload(
                        workloadNodes,
                        index,
                        WorkloadCompleteMarker),
                    $"inspector-disabled-workload-{index}.js");
            }
            var workloadCompletion = WaitForConsoleMarker(
                engines,
                WorkloadCompleteMarker,
                TimeSpan.FromSeconds(10));
            workloadElapsed.Stop();
            process.Refresh();
            var workloadCpu = process.TotalProcessorTime - workloadCpuBefore;
            ForceCollection();
            process.Refresh();
            var workloadWorkingSet = process.WorkingSet64;
            var workloadMemory = engines.Select(state => ReadMemory(state.Engine)).ToArray();
            var totalManagedBytesSinceBaseline = Math.Max(
                0,
                GC.GetTotalAllocatedBytes(precise: true)
                    - processBaselineAllocatedBytes);
            var inspectorRegistryCreated =
                IsManagedInspectorRegistryCreated();

            var result = new
            {
                schema = "webscene-inspector-idle-performance-v2",
                capturedUtc = DateTimeOffset.UtcNow,
                library,
                libraryBytes = new FileInfo(library).Length,
                buildFeatures,
                buildFeaturesExportAvailable,
                inspectorCompiledIn,
                options = new
                {
                    contextCount,
                    samples,
                    durationMilliseconds,
                    timerTarget,
                    frameTarget,
                    consoleIterations,
                    workloadNodes
                },
                startup = new
                {
                    prewarmMilliseconds,
                    warmContextCreateMilliseconds = Summarize(createMilliseconds),
                    firstSceneMilliseconds = Summarize(firstSceneMilliseconds)
                },
                idle = new
                {
                    elapsedMilliseconds = idleElapsed.Elapsed.TotalMilliseconds,
                    processCpuMilliseconds = idleCpu.TotalMilliseconds,
                    normalizedProcessCpuPercent = NormalizeCpu(idleCpu, idleElapsed.Elapsed),
                    workerWaits = Difference(idleBefore, idleAfter, value => value.WorkerWaits),
                    signalledWakes = Difference(idleBefore, idleAfter, value => value.WorkerSignalledWakes),
                    timeoutWakes = Difference(idleBefore, idleAfter, value => value.WorkerTimeoutWakes)
                },
                timerAndAnimationFrame = new
                {
                    elapsedMilliseconds = timerElapsed.Elapsed.TotalMilliseconds,
                    processCpuMilliseconds = timerCpu.TotalMilliseconds,
                    normalizedProcessCpuPercent = NormalizeCpu(timerCpu, timerElapsed.Elapsed),
                    timersFired = Difference(timerBefore, timerAfter, value => value.TimersFired),
                    animationFramesInvoked = Difference(
                        timerBefore,
                        timerAfter,
                        value => value.AnimationFramesInvoked)
                },
                consoleHeavy = new
                {
                    calls = checked(contextCount * consoleIterations),
                    drainedConsoleMessages = consoleCompletion.Messages,
                    completionSignals = consoleCompletion.Signals,
                    elapsedMilliseconds = consoleElapsed.Elapsed.TotalMilliseconds,
                    processCpuMilliseconds = consoleCpu.TotalMilliseconds,
                    normalizedProcessCpuPercent = NormalizeCpu(consoleCpu, consoleElapsed.Elapsed)
                },
                representativeWorkload = new
                {
                    contexts = contextCount,
                    nodesPerContext = workloadNodes,
                    completionSignals = workloadCompletion.Signals,
                    elapsedMilliseconds = workloadElapsed.Elapsed.TotalMilliseconds,
                    processCpuMilliseconds = workloadCpu.TotalMilliseconds,
                    normalizedProcessCpuPercent = NormalizeCpu(workloadCpu, workloadElapsed.Elapsed)
                },
                memory = new
                {
                    processBaselineWorkingSetBytes = processBaselineWorkingSet,
                    beforeViewsWorkingSetBytes = beforeViewsWorkingSet,
                    populatedViewsWorkingSetBytes = populatedViewsWorkingSet,
                    multiViewIncrementalWorkingSetBytes = Math.Max(
                        0,
                        populatedViewsWorkingSet - beforeViewsWorkingSet),
                    workloadWorkingSetBytes = workloadWorkingSet,
                    blankLifecycleSamples = blankMemory,
                    blankViews = blankViewMemory,
                    workloadViews = workloadMemory
                },
                managedAllocations = new
                {
                    inspectorRegistryCreated,
                    ordinaryViewConstructionBytes = viewConstructionBytes,
                    prewarmBytes = prewarmAllocatedBytes,
                    blankLifecycleBytes = Summarize(
                        blankLifecycleAllocatedBytes.Select(static value => (double)value)),
                    multiViewCreateBytes = Math.Max(
                        0,
                        populatedViewsAllocatedBytes - beforeViewsAllocatedBytes),
                    totalBytesSinceBaseline = totalManagedBytesSinceBaseline
                }
            };
            Console.WriteLine(JsonSerializer.Serialize(
                result,
                new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        finally
        {
            foreach (var engine in engines)
            {
                engine.Dispose();
            }
        }
    }

    private static EngineState CreateEngine()
    {
        var firstScene = new ManualResetEventSlim();
        var start = Stopwatch.GetTimestamp();
        var firstSceneTimestamp = 0L;
        var engine = NativeWebSceneApi.EngineCreate(
            simulatedChartCommandCount: 0,
            compilationCacheDirectory: null,
            EmptyResourceLoader.Instance,
            _ =>
            {
                if (Interlocked.CompareExchange(
                        ref firstSceneTimestamp,
                        Stopwatch.GetTimestamp(),
                        0) == 0)
                {
                    firstScene.Set();
                }
            });
        var created = Stopwatch.GetTimestamp();
        if (engine == IntPtr.Zero)
        {
            firstScene.Dispose();
            throw new InvalidOperationException("The native engine could not be created.");
        }
        NativeWebSceneApi.EngineRequestSceneCheckpoint(engine);
        if (!firstScene.Wait(TimeSpan.FromSeconds(5)))
        {
            NativeWebSceneApi.EngineDestroy(engine);
            firstScene.Dispose();
            throw new TimeoutException("The native engine did not publish its first scene.");
        }
        var published = Volatile.Read(ref firstSceneTimestamp);
        firstScene.Dispose();
        var startupError = NativeWebSceneApi.GetLastError(engine);
        if (!string.IsNullOrWhiteSpace(startupError))
        {
            NativeWebSceneApi.EngineDestroy(engine);
            throw new InvalidOperationException(
                $"The native engine reported a startup error: {startupError}");
        }
        return new EngineState(
            engine,
            Stopwatch.GetElapsedTime(start, created).TotalMilliseconds,
            Stopwatch.GetElapsedTime(start, published).TotalMilliseconds);
    }

    private static void Execute(IntPtr engine, string source, string name)
    {
        if (!NativeWebSceneApi.TryExecuteScript(engine, source, name))
        {
            throw new InvalidOperationException(
                $"The native runtime rejected {name}: {NativeWebSceneApi.GetLastError(engine)}");
        }
    }

    private static void WaitForRuntimeWork(
        IReadOnlyList<EngineState> engines,
        IReadOnlyList<RuntimeWorkMetrics> before,
        int timerTarget,
        int frameTarget,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var complete = true;
            for (var index = 0; index < engines.Count; index++)
            {
                if (NativeWebSceneApi.EngineRequiresAnimationFrame(
                        engines[index].Engine) != 0)
                {
                    NativeFrameInput.Submit(
                        engines[index].Engine,
                        Stopwatch.GetElapsedTime(0).TotalMilliseconds);
                }
                var current = ReadRuntimeWork(engines[index].Engine);
                if (current.TimersFired - before[index].TimersFired < (ulong)timerTarget ||
                    current.AnimationFramesInvoked - before[index].AnimationFramesInvoked < (ulong)frameTarget)
                {
                    complete = false;
                }
            }
            if (complete) return;
            Thread.Sleep(10);
        }
        var progress = string.Join(
            "; ",
            engines.Select((engine, index) =>
            {
                var current = ReadRuntimeWork(engine.Engine);
                var baseline = before[index];
                var timersFired = current.TimersFired - baseline.TimersFired;
                var timersScheduled = current.TimersScheduled - baseline.TimersScheduled;
                var framesInvoked =
                    current.AnimationFramesInvoked - baseline.AnimationFramesInvoked;
                var framesRequested =
                    current.AnimationFramesRequested - baseline.AnimationFramesRequested;
                var waits = current.WorkerWaits - baseline.WorkerWaits;
                var signalled =
                    current.WorkerSignalledWakes - baseline.WorkerSignalledWakes;
                var timeouts =
                    current.WorkerTimeoutWakes - baseline.WorkerTimeoutWakes;
                return $"engine {index}: timers={timersFired}/{timerTarget}, " +
                    $"scheduled={timersScheduled}, " +
                    $"frames={framesInvoked}/{frameTarget}, " +
                    $"requested={framesRequested}, waits={waits}, " +
                    $"signalled={signalled}, timeouts={timeouts}, " +
                    $"error={NativeWebSceneApi.GetLastError(engine.Engine)}";
            }));
        throw new TimeoutException(
            "The timer/animation-frame performance fixture did not complete within the bounded interval. " +
            progress);
    }

    private static string TimerAndFrameFixture(int timerTarget, int frameTarget)
        => $$"""
            (() => {
              globalThis.__perfTimerCount = 0;
              globalThis.__perfFrameCount = 0;
              const timer = () => {
                if (++globalThis.__perfTimerCount < {{timerTarget}}) setTimeout(timer, 0);
              };
              const frame = () => {
                if (++globalThis.__perfFrameCount < {{frameTarget}}) requestAnimationFrame(frame);
              };
              setTimeout(timer, 0);
              requestAnimationFrame(frame);
            })();
            """;

    private static string ConsoleFixture(int iterations, string marker)
        => $$"""
            for (let index = 0; index < {{iterations}}; index++) {
              console.log('inspector-disabled-console', index, { parity: true });
            }
            console.log('{{marker}}');
            """;

    private static string RepresentativeWorkload(
        int nodes,
        int context,
        string marker)
        => $$"""
            (() => {
              const style = document.createElement('style');
              style.textContent = '.perf-row{display:flex;width:480px;height:18px}'
                + '.perf-value{flex:1;color:#2962ff}';
              document.body.appendChild(style);
              const root = document.createElement('main');
              root.id = 'perf-root-{{context}}';
              for (let index = 0; index < {{nodes}}; index++) {
                const row = document.createElement('div');
                row.className = 'perf-row';
                row.dataset.index = String(index);
                const value = document.createElement('span');
                value.className = 'perf-value';
                value.textContent = 'context-{{context}}-' + index;
                row.appendChild(value);
                root.appendChild(row);
              }
              document.body.appendChild(root);
              for (let index = 0; index < {{nodes}}; index += 2) {
                root.children[index].classList.toggle('selected');
                root.children[index].firstChild.textContent += '-updated';
              }
              document.querySelectorAll('.perf-row > .perf-value').length;
              console.log('{{marker}}');
            })();
            """;

    private static ConsoleCompletion WaitForConsoleMarker(
        IReadOnlyList<EngineState> engines,
        string marker,
        TimeSpan timeout)
    {
        var completed = new bool[engines.Count];
        var messages = 0;
        var signals = 0;
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            for (var index = 0; index < engines.Count; index++)
            {
                if (completed[index]) continue;
                while (NativeWebSceneApi.TryTakeConsoleMessage(
                           engines[index].Engine,
                           out _,
                           out var message))
                {
                    if (string.Equals(message, marker, StringComparison.Ordinal))
                    {
                        completed[index] = true;
                        signals++;
                        break;
                    }
                    messages++;
                }
            }
            if (signals == engines.Count)
            {
                return new ConsoleCompletion(messages, signals);
            }
            Thread.Sleep(1);
        }
        throw new TimeoutException(
            $"The native runtime did not publish console marker '{marker}' within the bounded interval.");
    }

    private static RuntimeWorkMetrics ReadRuntimeWork(IntPtr engine)
        => NativeWebSceneApi.TryGetRuntimeWorkMetrics(engine)
            ?? throw new InvalidOperationException(
                "The native runtime-work metrics ABI became unavailable.");

    private static MemorySample ReadMemory(IntPtr engine)
    {
        var value = NativeWebSceneApi.TryGetMemoryMetrics(engine)
            ?? throw new InvalidOperationException(
                "The native memory metrics ABI is unavailable.");
        return new MemorySample(
            value.V8UsedHeapBytes,
            value.V8PhysicalHeapBytes,
            value.V8CodeAndMetadataBytes,
            value.V8ExternalScriptSourceBytes,
            value.NativeDomNodeCount,
            value.NativeDomNodePoolReservedBytes,
            value.NativeDomAttributeStorageBytes,
            value.NativeWrapperStorageBytes,
            value.LatestSceneBytes);
    }

    private static object Summarize(IEnumerable<double> values)
    {
        var ordered = values.Order().ToArray();
        return new
        {
            count = ordered.Length,
            minimum = ordered[0],
            median = Percentile(ordered, 0.5),
            p95 = Percentile(ordered, 0.95),
            maximum = ordered[^1],
            mean = ordered.Average()
        };
    }

    private static double Percentile(IReadOnlyList<double> ordered, double percentile)
    {
        if (ordered.Count == 1) return ordered[0];
        var position = percentile * (ordered.Count - 1);
        var lower = (int)Math.Floor(position);
        var upper = (int)Math.Ceiling(position);
        if (lower == upper) return ordered[lower];
        var fraction = position - lower;
        return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction;
    }

    private static ulong Difference(
        IReadOnlyList<RuntimeWorkMetrics> before,
        IReadOnlyList<RuntimeWorkMetrics> after,
        Func<RuntimeWorkMetrics, ulong> selector)
    {
        ulong total = 0;
        for (var index = 0; index < before.Count; index++)
        {
            var start = selector(before[index]);
            var end = selector(after[index]);
            total += end >= start ? end - start : 0;
        }
        return total;
    }

    private static double NormalizeCpu(TimeSpan cpu, TimeSpan elapsed)
        => elapsed.TotalMilliseconds <= 0
            ? 0
            : cpu.TotalMilliseconds / elapsed.TotalMilliseconds * 100d;

    private static bool TryReadBuildFeatures(string library, out uint features)
    {
        var handle = NativeLibrary.Load(library);
        try
        {
            if (!NativeLibrary.TryGetExport(
                    handle,
                    "webscene_engine_get_build_features",
                    out var address))
            {
                features = 0;
                return false;
            }
            features = Marshal.GetDelegateForFunctionPointer<GetBuildFeatures>(address)();
            return true;
        }
        finally
        {
            NativeLibrary.Free(handle);
        }
    }

    private static void ForceCollection()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static double MeasureViewConstructionBytes()
    {
        const int viewsPerBatch = 32;
        const int batches = 7;
        CreateViewBatch(8);
        var measurements = new double[batches];
        for (var batch = 0; batch < measurements.Length; ++batch)
        {
            ForceCollection();
            var before = GC.GetTotalAllocatedBytes(precise: true);
            CreateViewBatch(viewsPerBatch);
            measurements[batch] =
                (GC.GetTotalAllocatedBytes(precise: true) - before)
                / (double)viewsPerBatch;
        }
        Array.Sort(measurements);
        return Percentile(measurements, 0.5);
    }

    private static void CreateViewBatch(int count)
    {
        var views = new NativeWebSceneView[count];
        for (var index = 0; index < views.Length; ++index)
        {
            views[index] = new NativeWebSceneView(useCompositionVisual: false);
        }
        GC.KeepAlive(views);
    }

    private static bool IsManagedInspectorRegistryCreated()
    {
        const string typeName =
            "WebScene.Backends.Avalonia.Native.NativeInspectorRegistry";
        var registryType = typeof(NativeWebSceneView).Assembly.GetType(typeName);
        var current = registryType?.GetField(
            "_current",
            BindingFlags.NonPublic | BindingFlags.Static);
        return current?.GetValue(null) is not null;
    }

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase) &&
                int.TryParse(args[index + 1], out var value))
            {
                return value;
            }
        }
        return fallback;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint GetBuildFeatures();

    private sealed class EngineState(
        IntPtr engine,
        double createMilliseconds,
        double firstSceneMilliseconds) : IDisposable
    {
        private IntPtr _engine = engine;

        internal IntPtr Engine => _engine;
        internal double CreateMilliseconds { get; } = createMilliseconds;
        internal double FirstSceneMilliseconds { get; } = firstSceneMilliseconds;

        public void Dispose()
        {
            var engine = Interlocked.Exchange(ref _engine, IntPtr.Zero);
            if (engine != IntPtr.Zero)
            {
                NativeWebSceneApi.EngineDestroy(engine);
            }
        }
    }

    private sealed class EmptyResourceLoader : IWebSceneResourceLoader
    {
        internal static EmptyResourceLoader Instance { get; } = new();

        public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
            => throw new InvalidOperationException(
                $"Unexpected performance resource request '{request.Specifier}'.");
    }

    private sealed record MemorySample(
        ulong V8UsedHeapBytes,
        ulong V8PhysicalHeapBytes,
        ulong V8CodeAndMetadataBytes,
        ulong V8ExternalScriptSourceBytes,
        ulong NativeDomNodeCount,
        ulong NativeDomNodePoolReservedBytes,
        ulong NativeDomAttributeStorageBytes,
        ulong NativeWrapperStorageBytes,
        ulong LatestSceneBytes);

    private sealed record ConsoleCompletion(int Messages, int Signals);
}

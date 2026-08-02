using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

namespace WebScene.NativeEngine.Benchmarks;

internal static class NativeRuntimeWorkProbe
{
    internal static int Run(string[] args)
    {
        var chartCount = ReadIntOption(args, "--contexts", 4);
        var durationSeconds = ReadIntOption(args, "--seconds", 5);
        if (chartCount <= 0 || durationSeconds <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Contexts and seconds must be positive.");
        }

        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);

        var engines = new List<IntPtr>(chartCount);
        try
        {
            for (var index = 0; index < chartCount; index++)
            {
                var engine = NativeWebSceneApi.EngineCreate(
                    simulatedChartCommandCount: 0,
                    compilationCacheDirectory: null,
                    EmptyResourceLoader.Instance,
                    static _ => { });
                if (engine == IntPtr.Zero)
                {
                    throw new InvalidOperationException(
                        "The native runtime-work probe could not create an engine.");
                }
                if (!NativeWebSceneApi.TryEnableRuntimeWorkMetrics(engine))
                {
                    NativeWebSceneApi.EngineDestroy(engine);
                    throw new InvalidOperationException(
                        "The native runtime-work metrics ABI is unavailable.");
                }
                engines.Add(engine);
            }

            var before = engines
                .Select(ReadMetrics)
                .ToArray();
            var process = Process.GetCurrentProcess();
            process.Refresh();
            var cpuBefore = process.TotalProcessorTime;
            var elapsed = Stopwatch.StartNew();
            Thread.Sleep(TimeSpan.FromSeconds(durationSeconds));
            elapsed.Stop();
            process.Refresh();
            var cpu = process.TotalProcessorTime - cpuBefore;
            var after = engines
                .Select(ReadMetrics)
                .ToArray();

            ulong Difference(
                Func<RuntimeWorkMetrics, ulong> select,
                int index)
            {
                var current = select(after[index]);
                var baseline = select(before[index]);
                return current >= baseline ? current - baseline : 0;
            }

            var contexts = Enumerable.Range(0, chartCount)
                .Select(index => new
                {
                    index,
                    waits = Difference(static value => value.WorkerWaits, index),
                    signalledWakes = Difference(
                        static value => value.WorkerSignalledWakes,
                        index),
                    timeoutWakes = Difference(
                        static value => value.WorkerTimeoutWakes,
                        index),
                    timersFired = Difference(
                        static value => value.TimersFired,
                        index),
                    animationFramesInvoked = Difference(
                        static value => value.AnimationFramesInvoked,
                        index),
                    sceneBuilds = Difference(
                        static value => value.SceneBuilds,
                        index)
                })
                .ToArray();
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    mode = "idle",
                    contextCount = chartCount,
                    elapsedMilliseconds = elapsed.Elapsed.TotalMilliseconds,
                    processCpuMilliseconds = cpu.TotalMilliseconds,
                    normalizedProcessCpuPercent =
                        cpu.TotalMilliseconds / elapsed.Elapsed.TotalMilliseconds
                        * 100d,
                    totalWaits = contexts.Aggregate(
                        0UL,
                        static (sum, value) => sum + value.waits),
                    totalSignalledWakes = contexts.Aggregate(
                        0UL,
                        static (sum, value) => sum + value.signalledWakes),
                    totalTimeoutWakes = contexts.Aggregate(
                        0UL,
                        static (sum, value) => sum + value.timeoutWakes),
                    contexts
                },
                new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        finally
        {
            foreach (var engine in engines)
            {
                NativeWebSceneApi.EngineDestroy(engine);
            }
        }
    }

    private static RuntimeWorkMetrics ReadMetrics(IntPtr engine)
        => NativeWebSceneApi.TryGetRuntimeWorkMetrics(engine)
            ?? throw new InvalidOperationException(
                "The native runtime-work metrics ABI became unavailable.");

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

    private sealed class EmptyResourceLoader : IWebSceneResourceLoader
    {
        internal static EmptyResourceLoader Instance { get; } = new();

        public WebSceneTextResource LoadText(
            in WebSceneResourceRequest request)
            => throw new InvalidOperationException(
                $"Unexpected runtime-work resource request '{request.Specifier}'.");
    }
}

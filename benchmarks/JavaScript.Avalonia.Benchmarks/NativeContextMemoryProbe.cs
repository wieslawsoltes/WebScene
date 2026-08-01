using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

namespace JavaScript.Avalonia.Benchmarks;

internal static class NativeContextMemoryProbe
{
    internal static int Run(string[] args)
    {
        var contextCount = ReadIntOption(args, "--contexts", 4);
        var nodesPerContext = ReadIntOption(args, "--nodes", 2_000);
        if (contextCount < 0 || contextCount > 16 || nodesPerContext <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Contexts must be between 0 and 16 and nodes must be positive.");
        }

        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);
        if (NativeWebSceneApi.EnginePrewarm() == 0)
        {
            throw new InvalidOperationException(
                "The native context-memory probe could not prewarm V8.");
        }

        ForceCollection();
        var process = Process.GetCurrentProcess();
        var baselineWorkingSet = ReadWorkingSet(process);
        var engines = new List<IntPtr>(contextCount);
        try
        {
            for (var context = 0; context < contextCount; context++)
            {
                var engine = NativeWebSceneApi.EngineCreate(
                    simulatedChartCommandCount: 0,
                    compilationCacheDirectory: null,
                    EmptyResourceLoader.Instance,
                    static _ => { });
                if (engine == IntPtr.Zero)
                {
                    throw new InvalidOperationException(
                        "The native context-memory probe could not create an engine.");
                }
                engines.Add(engine);
                if (!NativeWebSceneApi.TryExecuteScript(
                        engine,
                        Fixture(nodesPerContext, context),
                        $"native-context-memory-{context}.js"))
                {
                    throw new InvalidOperationException(
                        "The native context-memory fixture failed: "
                        + NativeWebSceneApi.GetLastError(engine));
                }
            }

            Thread.Sleep(1_200);
            foreach (var engine in engines)
            {
                NativeWebSceneApi.TryExecuteScript(
                    engine,
                    "true",
                    "native-context-memory-barrier.js");
            }
            ForceCollection();
            var populatedWorkingSet = ReadWorkingSet(process);
            var metrics = engines.Select(ReadMetrics).ToArray();
            var attributed = metrics.Select((value, index) => new
            {
                index,
                v8UsedHeapBytes = value.V8UsedHeapBytes,
                v8PhysicalHeapBytes = value.V8PhysicalHeapBytes,
                v8CodeAndMetadataBytes = value.V8CodeAndMetadataBytes,
                nativeDomNodeCount = value.NativeDomNodeCount,
                nativeDomNodeSizeBytes = value.NativeDomNodeSizeBytes,
                nativeDomInlineBytes = value.NativeDomInlineBytes,
                nativeDomAttributeStorageBytes = value.NativeDomAttributeStorageBytes,
                nativeDomNodePoolReservedBytes = value.NativeDomNodePoolReservedBytes,
                nativeWrapperStorageBytes = value.NativeWrapperStorageBytes,
                latestSceneBytes = value.LatestSceneBytes
            }).ToArray();

            foreach (var engine in engines)
            {
                NativeWebSceneApi.EngineDestroy(engine);
            }
            engines.Clear();
            ForceCollection();
            Thread.Sleep(500);
            var releasedWorkingSet = ReadWorkingSet(process);
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    contextCount,
                    nodesPerContext,
                    baselineWorkingSetBytes = baselineWorkingSet,
                    populatedWorkingSetBytes = populatedWorkingSet,
                    incrementalWorkingSetBytes = Math.Max(
                        0,
                        populatedWorkingSet - baselineWorkingSet),
                    incrementalWorkingSetBytesPerContext = contextCount == 0
                        ? 0
                        : Math.Max(0, populatedWorkingSet - baselineWorkingSet)
                            / contextCount,
                    releasedWorkingSetBytes = releasedWorkingSet,
                    retainedAfterDestroyBytes = Math.Max(
                        0,
                        releasedWorkingSet - baselineWorkingSet),
                    attributed
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

    private static string Fixture(int nodeCount, int context)
        => $$"""
            (() => {
              const style = document.createElement('style');
              style.textContent = '.memory-row { display:flex; width:320px; height:18px; }'
                + '.memory-value { flex:1; color:#2962ff; }';
              document.body.appendChild(style);
              const root = document.createElement('section');
              for (let index = 0; index < {{nodeCount}}; index++) {
                const row = document.createElement('div');
                row.className = 'memory-row';
                row.setAttribute('data-index', String(index));
                const value = document.createElement('span');
                value.className = 'memory-value';
                value.textContent = 'context-{{context}}-value-' + index;
                if ((index % 32) === 0) value.addEventListener('click', () => index);
                row.appendChild(value);
                root.appendChild(row);
              }
              document.body.appendChild(root);
              globalThis.__memorySeries = Array.from(
                { length: {{nodeCount}} },
                (_, index) => ({ time: index, open: index + .1, close: index + .2 }));
            })();
            """;

    private static EngineMemoryMetrics ReadMetrics(IntPtr engine)
        => NativeWebSceneApi.TryGetMemoryMetrics(engine)
            ?? throw new InvalidOperationException(
                "The native engine memory metrics ABI is unavailable.");

    private static long ReadWorkingSet(Process process)
    {
        process.Refresh();
        return process.WorkingSet64;
    }

    private static void ForceCollection()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(
                    args[index],
                    name,
                    StringComparison.OrdinalIgnoreCase)
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
                $"Unexpected context-memory resource '{request.Specifier}'.");
    }
}

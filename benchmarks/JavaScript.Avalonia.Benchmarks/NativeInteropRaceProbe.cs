using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;
using WebScene.JavaScript.Interop;

namespace JavaScript.Avalonia.Benchmarks;

internal static class NativeInteropRaceProbe
{
    private static readonly JavaScriptBinaryCallSite s_delayedCallSite = new(
        JavaScriptBinaryOperation.InvokeGlobal,
        globalName: "__webSceneInteropDelayed",
        memberName: null,
        JavaScriptBinaryResultMode.Value,
        JavaScriptBinaryCallFlags.AwaitPromise);

    internal static async Task<int> RunAsync(string[] args)
    {
        var batches = ReadIntOption(args, "--batches", 100);
        var width = ReadIntOption(args, "--width", 32);
        if (batches <= 0 || width <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Batches and width must be positive.");
        }

        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);

        var engine = NativeWebSceneApi.EngineCreate(
            simulatedChartCommandCount: 0,
            compilationCacheDirectory: null,
            EmptyResourceLoader.Instance,
            static _ => { });
        if (engine == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native interop race engine could not be created.");
        }

        var succeeded = 0;
        var cancelled = 0;
        var faulted = 0;
        var faultExamples = new List<string>();
        try
        {
            if (!NativeWebSceneApi.TryExecuteScript(
                    engine,
                    """
                    globalThis.__webSceneInteropDelayed = () =>
                      new Promise(resolve =>
                        setTimeout(() => resolve(42), 10));
                    """,
                    "native-interop-race.js"))
            {
                throw new InvalidOperationException(
                    "The native interop race fixture could not be installed.");
            }

            for (var batch = 0; batch < batches; batch++)
            {
                var transport = new NativeJavaScriptBinaryTransport(engine);
                var invoker = new NativeJavaScriptInvoker(transport);
                var pending = new Task<double>[width];
                for (var index = 0; index < pending.Length; index++)
                {
                    pending[index] = invoker.InvokeBinaryAsync<
                            JavaScriptBinaryVoid,
                            double,
                            DelayedCodec>(
                            s_delayedCallSite,
                            default,
                            new JavaScriptBinaryVoid())
                        .AsTask();
                }

                invoker.Dispose();
                foreach (var task in pending)
                {
                    try
                    {
                        _ = await task.ConfigureAwait(false);
                        succeeded++;
                    }
                    catch (OperationCanceledException)
                    {
                        cancelled++;
                    }
                    catch (Exception error)
                    {
                        faulted++;
                        if (faultExamples.Count < 5)
                        {
                            faultExamples.Add(error.Message);
                        }
                    }
                }

                var cleanupDeadline =
                    DateTime.UtcNow + TimeSpan.FromSeconds(5);
                while (NativeWebSceneApi.GetInteropPoolMetrics(engine)
                           .ActiveOperationSlots != 0
                       && DateTime.UtcNow < cleanupDeadline)
                {
                    await Task.Delay(1).ConfigureAwait(false);
                }
                await Task.Delay(20).ConfigureAwait(false);
                if (!NativeWebSceneApi.TryExecuteScript(
                        engine,
                        "true",
                        "native-interop-race-barrier.js"))
                {
                    throw new InvalidOperationException(
                        "The native interop race barrier failed: "
                        + NativeWebSceneApi.GetLastError(engine));
                }
            }

            var metrics = await WaitForPoolDrainAsync(engine)
                .ConfigureAwait(false);
            var correct =
                faulted == 0
                && succeeded + cancelled == checked(batches * width)
                && metrics.OutstandingResults == 0
                && metrics.ActiveOperationSlots == 0;
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    batches,
                    width,
                    operations = checked(batches * width),
                    succeeded,
                    cancelled,
                    faulted,
                    faultExamples,
                    metrics.OutstandingResults,
                    metrics.ActiveOperationSlots,
                    metrics.TakenResultLeases,
                    metrics.OperationResultLeases,
                    correct
                },
                new JsonSerializerOptions { WriteIndented = true }));
            return correct ? 0 : 1;
        }
        finally
        {
            NativeWebSceneApi.EngineDestroy(engine);
        }
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

    private static async Task<NativeInteropPoolMetrics> WaitForPoolDrainAsync(
        IntPtr engine)
    {
        // Loaded hosted runners can take several seconds to retire the final
        // cancelled promise results after every operation slot has drained.
        // Keep the probe strict about leaked leases, but allow the worker time
        // to finish returning those results to the pool.
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(30);
        NativeInteropPoolMetrics metrics;
        do
        {
            metrics = NativeWebSceneApi.GetInteropPoolMetrics(engine);
            if (metrics.ActiveOperationSlots == 0
                && metrics.OutstandingResults == 0)
            {
                return metrics;
            }
            await Task.Delay(10).ConfigureAwait(false);
        }
        while (DateTime.UtcNow < deadline);
        return metrics;
    }

    private readonly struct DelayedCodec
        : IJavaScriptBinaryCodec<JavaScriptBinaryVoid, double>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static double DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetNumber();
    }

    private sealed class EmptyResourceLoader : IWebSceneResourceLoader
    {
        internal static EmptyResourceLoader Instance { get; } = new();

        public WebSceneTextResource LoadText(
            in WebSceneResourceRequest request)
            => throw new InvalidOperationException(
                $"Unexpected race-probe resource '{request.Specifier}'.");
    }
}

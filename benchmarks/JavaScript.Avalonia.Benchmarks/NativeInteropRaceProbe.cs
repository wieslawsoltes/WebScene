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
        var contextCount = ReadIntOption(args, "--contexts", 4);
        if (batches <= 0 || width <= 0 || contextCount <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Batches, width, and contexts must be positive.");
        }

        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);

        var engines = new List<IntPtr>(contextCount);
        var succeeded = 0;
        var cancelled = 0;
        var faulted = 0;
        var faultExamples = new List<string>();
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
                        "The native interop race engine could not be created.");
                }
                engines.Add(engine);
                if (!NativeWebSceneApi.TryEnableRuntimeWorkMetrics(engine))
                {
                    throw new InvalidOperationException(
                        "The native interop work metrics ABI is unavailable.");
                }
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
            }

            for (var batch = 0; batch < batches; batch++)
            {
                var invokers = engines.Select(engine =>
                    new NativeJavaScriptInvoker(
                        new NativeJavaScriptBinaryTransport(engine)))
                    .ToArray();
                var pending = new List<Task<double>>(
                    checked(width * contextCount));
                foreach (var invoker in invokers)
                {
                    for (var index = 0; index < width; index++)
                    {
                        pending.Add(invoker.InvokeBinaryAsync<
                            JavaScriptBinaryVoid,
                            double,
                            DelayedCodec>(
                            s_delayedCallSite,
                            default,
                            new JavaScriptBinaryVoid())
                        .AsTask());
                    }
                }

                foreach (var invoker in invokers) invoker.Dispose();
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

                // Disposal cancels managed waiters, but JavaScript promises
                // and their timers still settle inside each realm. Allow that
                // standards-required work to drain before the next batch.
                await Task.Delay(20).ConfigureAwait(false);
                foreach (var engine in engines)
                {
                    var cleanupDeadline =
                        DateTime.UtcNow + TimeSpan.FromSeconds(5);
                    while (NativeWebSceneApi.GetInteropPoolMetrics(engine)
                               .ActiveOperationSlots != 0
                           && DateTime.UtcNow < cleanupDeadline)
                    {
                        await Task.Delay(1).ConfigureAwait(false);
                    }
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
            }

            var metrics = new NativeInteropPoolMetrics[engines.Count];
            for (var index = 0; index < engines.Count; index++)
            {
                metrics[index] = await WaitForPoolDrainAsync(engines[index])
                    .ConfigureAwait(false);
            }
            var work = engines.Select(engine =>
                    NativeWebSceneApi.TryGetRuntimeWorkMetrics(engine)
                    ?? throw new InvalidOperationException(
                        "The native interop work metrics ABI became unavailable."))
                .ToArray();
            var operations = checked(batches * width * contextCount);
            var generatedCalls = work.Aggregate(
                0UL,
                static (sum, value) => sum + value.GeneratedInvokeCalls);
            var generatedRequestBytes = work.Aggregate(
                0UL,
                static (sum, value) => sum + value.GeneratedRequestBytes);
            var evaluationCalls = work.Aggregate(
                0UL,
                static (sum, value) => sum + value.ArbitraryEvaluationCalls);
            var correct =
                faulted == 0
                && succeeded + cancelled == operations
                && generatedCalls == (ulong)operations
                && generatedRequestBytes > 0
                && metrics.All(static value =>
                    value.OutstandingResults == 0
                    && value.ActiveOperationSlots == 0);
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    batches,
                    width,
                    contextCount,
                    operations,
                    succeeded,
                    cancelled,
                    faulted,
                    faultExamples,
                    generatedCalls,
                    generatedRequestBytes,
                    arbitraryEvaluationCalls = evaluationCalls,
                    contexts = metrics.Select((value, index) => new
                    {
                        index,
                        value.OutstandingResults,
                        value.ActiveOperationSlots,
                        value.TakenResultLeases,
                        value.OperationResultLeases
                    }),
                    correct
                },
                new JsonSerializerOptions { WriteIndented = true }));
            return correct ? 0 : 1;
        }
        finally
        {
            foreach (var engine in engines)
            {
                NativeWebSceneApi.EngineDestroy(engine);
            }
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
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        NativeInteropPoolMetrics metrics;
        do
        {
            metrics = NativeWebSceneApi.GetInteropPoolMetrics(engine);
            if (metrics.ActiveOperationSlots == 0
                && metrics.OutstandingResults == 0)
            {
                return metrics;
            }
            await Task.Delay(1).ConfigureAwait(false);
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

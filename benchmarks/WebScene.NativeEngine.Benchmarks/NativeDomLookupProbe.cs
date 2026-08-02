using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;
using WebScene.JavaScript.Interop;

namespace WebScene.NativeEngine.Benchmarks;

internal static class NativeDomLookupProbe
{
    private static readonly JavaScriptBinaryCallSite s_measureCallSite = new(
        JavaScriptBinaryOperation.InvokeGlobal,
        globalName: "__webSceneMeasureDomLookup",
        memberName: null,
        JavaScriptBinaryResultMode.Value,
        JavaScriptBinaryCallFlags.None);

    internal static async Task<int> RunAsync(string[] args)
    {
        var nodeCount = ReadIntOption(args, "--nodes", 4_000);
        var lookupCount = ReadIntOption(args, "--lookups", 20_000);
        var sampleCount = ReadIntOption(args, "--samples", 5);
        var kind = ReadOption(args, "--kind", "id");
        if (!string.Equals(kind, "id", StringComparison.OrdinalIgnoreCase)
            && !string.Equals(kind, "named", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException(
                "DOM lookup kind must be 'id' or 'named'.",
                nameof(args));
        }
        if (nodeCount <= 0 || lookupCount <= 0 || sampleCount <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Nodes, lookups, and samples must be positive.");
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
                "The native DOM lookup probe could not create an engine.");
        }

        try
        {
            var lookupAttribute = string.Equals(
                kind,
                "id",
                StringComparison.OrdinalIgnoreCase)
                ? "id"
                : "name";
            var lookupExpression = string.Equals(
                kind,
                "id",
                StringComparison.OrdinalIgnoreCase)
                ? "document.getElementById(id)"
                : "globalThis[id]";
            var setup = $$"""
                (() => {
                  const root = document.createElement('div');
                  for (let index = 0; index < {{nodeCount}}; index++) {
                    const node = document.createElement('span');
                    node.setAttribute('{{lookupAttribute}}', `lookup-${index}`);
                    root.appendChild(node);
                  }
                  document.body.appendChild(root);
                  globalThis.__webSceneMeasureDomLookup = () => {
                    let checksum = 0;
                    for (let index = 0; index < {{lookupCount}}; index++) {
                      const id = (index & 1) === 0
                        ? 'lookup-{{nodeCount - 1}}'
                        : 'lookup-missing';
                      if ({{lookupExpression}} != null) checksum++;
                    }
                    return checksum;
                  };
                })();
                """;
            if (!NativeWebSceneApi.TryExecuteScript(
                    engine,
                    setup,
                    "native-dom-lookup-setup.js"))
            {
                throw new InvalidOperationException(
                    "The DOM lookup fixture could not be installed: "
                    + NativeWebSceneApi.GetLastError(engine));
            }

            using var transport = new NativeJavaScriptBinaryTransport(engine);
            using var invoker = new NativeJavaScriptInvoker(transport);
            _ = await MeasureAsync(invoker).ConfigureAwait(false);

            var samples = new double[sampleCount];
            var checksums = new double[sampleCount];
            var process = Process.GetCurrentProcess();
            process.Refresh();
            var cpuBefore = process.TotalProcessorTime;
            for (var index = 0; index < samples.Length; index++)
            {
                var elapsed = Stopwatch.StartNew();
                checksums[index] = await MeasureAsync(invoker)
                    .ConfigureAwait(false);
                elapsed.Stop();
                samples[index] = elapsed.Elapsed.TotalMilliseconds;
            }
            process.Refresh();
            var cpu = process.TotalProcessorTime - cpuBefore;
            Array.Sort(samples);
            var expectedChecksum = lookupCount / 2d
                + (lookupCount % 2);
            var correct = checksums.All(
                value => Math.Abs(value - expectedChecksum) < 0.5);
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    kind,
                    nodeCount,
                    lookupCount,
                    sampleCount,
                    samplesMilliseconds = samples,
                    medianMilliseconds = samples[samples.Length / 2],
                    processCpuMilliseconds = cpu.TotalMilliseconds,
                    expectedChecksum,
                    checksums,
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

    private static ValueTask<double> MeasureAsync(
        NativeJavaScriptInvoker invoker)
        => invoker.InvokeBinaryAsync<
            JavaScriptBinaryVoid,
            double,
            LookupCodec>(
            s_measureCallSite,
            default,
            new JavaScriptBinaryVoid());

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

    private readonly struct LookupCodec
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
                $"Unexpected DOM lookup resource '{request.Specifier}'.");
    }
}

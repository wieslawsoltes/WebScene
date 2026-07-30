using BenchmarkDotNet.Attributes;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;
using WebScene.JavaScript.Interop;
using WebScene.NativeInterop.Benchmarks.Generated;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Measures the generated managed API used by a realtime chart update, rather
/// than measuring only an isolated result decoder.
/// </summary>
[MemoryDiagnoser]
public class GeneratedRealtimeChartInteropBenchmarks
{
    private const string InstallHost = """
        globalThis.realtimeChartHost = {
          count: 0,
          close: 0,
          history: Array.from({ length: 256 }, (_, index) => ({
            time: 1785410000000 + index * 1000,
            open: 100 + index,
            high: 101 + index,
            low: 99 + index,
            close: 100.5 + index,
            volume: 42000 + index
          })),
          onRealtimeUpdate(subscriberUid, bar) {
            this.count++;
            this.close = bar.close;
          },
          onHistoryResponse(requestId, bars) {
            this.count += bars.length;
            if (bars.length) this.close = bars[bars.length - 1].close;
          },
          getHistory() { return Promise.resolve(this.history); },
          updateCount() { return this.count; },
          lastClose() { return this.close; }
        };
        """;

    private readonly List<EngineState> _binary = [];
    private RealtimeChartBar _bar = null!;

    [Params(1, 4, 8)]
    public int EngineCount { get; set; }

    [GlobalSetup]
    public async Task Setup()
    {
        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the built native engine library.");
        }
        NativeWebSceneApi.ConfigureLibraryPath(library);
        _bar = new RealtimeChartBar
        {
            Time = 1_785_410_000_000,
            Open = 101.25,
            High = 102.50,
            Low = 100.75,
            Close = 102.125,
            Volume = 42_000
        };
        for (var index = 0; index < EngineCount; index++)
        {
            _binary.Add(await CreateEngineAsync());
        }
        for (var iteration = 0; iteration < 64; iteration++)
        {
            await BinaryRealtimeUpdate();
        }
    }

    [GlobalCleanup]
    public async Task Cleanup()
    {
        foreach (var state in _binary)
        {
            await state.Host.DisposeAsync();
            state.Transport.Dispose();
            NativeWebSceneApi.EngineDestroy(state.Engine);
        }
        _binary.Clear();
    }

    [Benchmark(Baseline = true)]
    public async ValueTask BinaryRealtimeUpdate()
    {
        for (var index = 0; index < _binary.Count; index++)
        {
            await _binary[index].Host.OnRealtimeUpdateAsync(
                "subscriber-0",
                _bar);
        }
    }

    [Benchmark]
    public async ValueTask<double> BinaryMaterializedHistory()
    {
        double result = 0;
        for (var index = 0; index < _binary.Count; index++)
        {
            var history = await _binary[index].Host.GetHistoryAsync();
            result += history[^1].Close;
        }
        return result;
    }

    [Benchmark]
    public async ValueTask<double> BinaryBorrowedHistory()
    {
        double result = 0;
        for (var index = 0; index < _binary.Count; index++)
        {
            using var lease =
                await _binary[index].Host.BorrowHistoryAsync();
            using var view = lease.Borrow();
            result += view[^1]
                .GetRequiredProperty("close"u8)
                .GetNumber();
        }
        return result;
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
                "The native chart benchmark engine could not be created.");
        }
        if (!NativeWebSceneApi.TryExecuteScript(
                engine,
                InstallHost,
                "generated-realtime-chart-host.js"))
        {
            NativeWebSceneApi.EngineDestroy(engine);
            throw new InvalidOperationException(
                "The native chart benchmark host could not be installed.");
        }

        var transport = new NativeJavaScriptBinaryTransport(engine);
        var invoker = new NativeJavaScriptInvoker(transport);
        try
        {
            var host = await JavaScriptGlobals.GetRealtimeChartHostAsync(invoker);
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
                $"Unexpected benchmark resource request '{request.Specifier}'.");
    }
}

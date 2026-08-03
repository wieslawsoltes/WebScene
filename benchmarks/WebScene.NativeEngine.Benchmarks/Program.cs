using BenchmarkDotNet.Running;
using WebScene.NativeEngine.Benchmarks;

if (args.Length > 0 && string.Equals(args[0], "probe", StringComparison.OrdinalIgnoreCase))
{
    var probeArgs = args.Skip(2).ToArray();
    if (args.Length > 1)
    {
        switch (args[1].ToLowerInvariant())
        {
            case "generated-realtime-chart":
                return await GeneratedRealtimeChartAcceptanceProbe.RunAsync(probeArgs);
            case "native-interop-race":
                return await NativeInteropRaceProbe.RunAsync(probeArgs);
            case "native-runtime-work":
                return NativeRuntimeWorkProbe.Run(probeArgs);
            case "native-dom-lookup":
                return await NativeDomLookupProbe.RunAsync(probeArgs);
            case "native-context-memory":
                return NativeContextMemoryProbe.Run(probeArgs);
            case "native-lifecycle":
                return NativeViewLifecycleProbe.Run(probeArgs);
            case "native-inspector-disabled-performance":
                return NativeInspectorDisabledPerformanceProbe.Run(probeArgs);
        }
    }

    Console.Error.WriteLine(
        "Unknown probe. Use one of: generated-realtime-chart, native-interop-race, " +
        "native-runtime-work, native-dom-lookup, native-context-memory, native-lifecycle, " +
        "native-inspector-disabled-performance.");
    return 2;
}

BenchmarkSwitcher
    .FromAssembly(typeof(Program).Assembly)
    .Run(args);

return 0;

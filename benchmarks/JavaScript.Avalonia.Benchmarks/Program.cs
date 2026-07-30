using BenchmarkDotNet.Running;
using JavaScript.Avalonia.Benchmarks;

if (args.Length > 0 && string.Equals(args[0], "probe", StringComparison.OrdinalIgnoreCase))
{
    if (args.Length > 1 && string.Equals(args[1], "css-custom-properties", StringComparison.OrdinalIgnoreCase))
    {
        return CssCustomPropertyPerformanceProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "css-style-storage", StringComparison.OrdinalIgnoreCase))
    {
        return CssStyleStoragePerformanceProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "v8dom", StringComparison.OrdinalIgnoreCase))
    {
        return V8DomInteropProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8canvasboundary", StringComparison.OrdinalIgnoreCase))
    {
        return V8CanvasStringBoundaryProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8dataset", StringComparison.OrdinalIgnoreCase))
    {
        return V8DatasetInteropProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8tokens", StringComparison.OrdinalIgnoreCase))
    {
        return V8TokenListInteropProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8observer", StringComparison.OrdinalIgnoreCase))
    {
        return V8MutationObserverProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8textnode", StringComparison.OrdinalIgnoreCase))
    {
        return V8TextNodeInteropProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8attributes", StringComparison.OrdinalIgnoreCase))
    {
        return V8AttributeInteropProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8react", StringComparison.OrdinalIgnoreCase))
    {
        return V8ReactSchedulerProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "v8reactfocus", StringComparison.OrdinalIgnoreCase))
    {
        return V8ReactFocusProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "v8iframepointer", StringComparison.OrdinalIgnoreCase))
    {
        return V8IframePointerProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8domidentity", StringComparison.OrdinalIgnoreCase))
    {
        return V8DomIdentityProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8interactioncontracts", StringComparison.OrdinalIgnoreCase))
    {
        return V8InteractionContractsProbe.Run();
    }
    if (args.Length > 1 && string.Equals(args[1], "v8lifecycle", StringComparison.OrdinalIgnoreCase))
    {
        return V8LifecycleProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "v8sharedcache", StringComparison.OrdinalIgnoreCase))
    {
        return V8SharedCompilationCacheProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "v8nativepackage", StringComparison.OrdinalIgnoreCase))
    {
        return V8NativePackageProbe.Run(args.Skip(2).ToArray());
    }
    if (args.Length > 1 && string.Equals(args[1], "generated-realtime-chart", StringComparison.OrdinalIgnoreCase))
    {
        return await GeneratedRealtimeChartAcceptanceProbe.RunAsync(
            args.Skip(2).ToArray());
    }
    Console.Error.WriteLine("Unknown probe. Use one of: css-custom-properties, css-style-storage, v8dom, v8canvasboundary, v8dataset, v8tokens, v8observer, v8textnode, v8attributes, v8react, v8reactfocus, v8iframepointer, v8domidentity, v8interactioncontracts, v8lifecycle, v8sharedcache, v8nativepackage, generated-realtime-chart.");
    return 2;
}

BenchmarkSwitcher
    .FromAssembly(typeof(Program).Assembly)
    .Run(args);

return 0;

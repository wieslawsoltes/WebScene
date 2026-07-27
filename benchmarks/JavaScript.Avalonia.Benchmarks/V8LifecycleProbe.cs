using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free lifecycle spike for V8 runtimes, iframe contexts, DOM proxy
/// retention, and simultaneous host isolation.
/// </summary>
internal static class V8LifecycleProbe
{
    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var iterations = ReadIntOption(args, "--iterations", 8, minimum: 2, maximum: 100);
        var nodes = ReadIntOption(args, "--nodes", 128, minimum: 1, maximum: 10_000);
        var process = Process.GetCurrentProcess();
        var privateBytes = new List<long>(iterations);
        var usedHeap = new List<long>(iterations);
        var passed = ProbeMultipleInstances();

        for (var iteration = 0; iteration < iterations; iteration++)
        {
            var result = RunIteration(iteration + 1, nodes);
            ForceManagedCollection();
            process.Refresh();
            var processBytes = GetProcessMemoryBytes(process);
            privateBytes.Add(processBytes);
            usedHeap.Add(result.UsedHeapAfterCollection);
            passed &= result.Passed;
            Console.WriteLine(
                $"V8 lifecycle {iteration + 1}/{iterations}: {(result.Passed ? "pass" : "fail")}; " +
                $"proxy strong={result.Baseline.Strong}->{result.Attached.Strong}->{result.Detached.Strong}, " +
                $"entries={result.Baseline.Entries}->{result.Attached.Entries}->{result.Detached.Entries}, " +
                $"heap={result.UsedHeapAfterCollection / (1024d * 1024d):F2} MB used/" +
                $"{result.PhysicalHeapAfterCollection / (1024d * 1024d):F2} MB physical, " +
                $"process={processBytes / (1024d * 1024d):F1} MB, " +
                $"detached={result.OwnerDetached}, frame-remount={result.FrameRemounted}, " +
                $"frame-detached={result.FrameDetached}");
        }

        var privateGrowth = privateBytes[^1] - privateBytes[1];
        var heapSpread = usedHeap.Max() - usedHeap.Min();
        var memoryPassed = privateGrowth <= 64L * 1024 * 1024
                           && heapSpread <= 8L * 1024 * 1024;
        passed &= memoryPassed;
        Console.WriteLine(
            $"V8 lifecycle memory plateau: {(memoryPassed ? "pass" : "fail")}; " +
            $"process-growth(after warmup)={privateGrowth / (1024d * 1024d):F1} MB, " +
            $"collected-heap-spread={heapSpread / (1024d * 1024d):F2} MB");
        return passed ? 0 : 1;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static IterationResult RunIteration(int iteration, int nodes)
    {
        var window = CreateWindow($"V8 lifecycle {iteration}");
        window.Show();
        Dispatcher.UIThread.RunJobs();
        using var host = new AvaloniaBrowserHost(window);
        var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions
            {
                EnableCanvasBatching = true,
                EnableTrustedSameOriginContextSharing = true
            });
        try
        {
            var baseline = ReadOwnerRetention(runtime.DescribeDomProxyRetention());
            runtime.Execute($$"""
                globalThis.__webSceneLifecycleTree = document.createElement('section');
                for (let index = 0; index < {{nodes}}; index++) {
                  const child = document.createElement('button');
                  child.textContent = 'node-' + index;
                  child.addEventListener('click', function() {});
                  __webSceneLifecycleTree.appendChild(child);
                }
                document.body.appendChild(__webSceneLifecycleTree);
                """, "v8-lifecycle-attached-tree.js");
            Dispatcher.UIThread.RunJobs();
            var attached = ReadOwnerRetention(runtime.DescribeDomProxyRetention());

            runtime.Execute("""
                __webSceneLifecycleTree.remove();
                globalThis.__webSceneLifecycleTree = null;
                """, "v8-lifecycle-detached-tree.js");
            runtime.CollectGarbage(exhaustive: true);
            runtime.CollectGarbage(exhaustive: true);
            var detached = ReadOwnerRetention(runtime.DescribeDomProxyRetention());

            runtime.Execute("""
                globalThis.__webSceneLifecycleFrame = document.createElement('iframe');
                __webSceneLifecycleFrame.id = 'v8-lifecycle-frame';
                document.body.appendChild(__webSceneLifecycleFrame);
                __webSceneLifecycleFrame.src = URL.createObjectURL(new Blob([
                  '<!doctype html><html><body><div id="frame-ready">ready</div>' +
                  '<script>globalThis.frameCounter = 41; ' +
                  'document.body.addEventListener("click", function(){}); ' +
                  'globalThis.lifecycleTimeout = setTimeout(function(){}, 60000); ' +
                  'globalThis.lifecycleInterval = setInterval(function(){}, 60000); ' +
                  'globalThis.lifecycleObserver = new MutationObserver(function(){}); ' +
                  'globalThis.lifecycleObserver.observe(document.body, { childList: true });<\/script></body></html>'
                ], { type: 'text/html' }));
                """, "v8-lifecycle-frame.js");

            var baselineTimers = host.BrowserWindow.PendingTimerCount;
            var baselineAnimationFrames = host.PendingExternalAnimationFrameCount;
            var (iframe, frameDocument) = WaitForFrame(host);
            var firstWindow = frameDocument.ExternalWindowContext;
            var frameAttached = frameDocument.ExternalEventListenerAdapter is not null
                                && frameDocument.ExternalWindowContext is not null
                                && host.Document.ExternalWindowEventDispatcher is not null;
            var frameWorks = Convert.ToInt32(runtime.Engine.Evaluate(
                "document.querySelector('#v8-lifecycle-frame').contentWindow.frameCounter")) == 41;
            var frameTasksAttached = host.BrowserWindow.PendingTimerCount >= baselineTimers + 2
                                     && runtime.ActiveFrameCount == 1;

            runtime.Execute("""
                __webSceneLifecycleFrame.contentWindow.requestAnimationFrame(function() {});
                """, "v8-lifecycle-frame-pending-raf.js");
            frameTasksAttached &= host.PendingExternalAnimationFrameCount
                                  >= baselineAnimationFrames + 1;

            runtime.Execute("""
                __webSceneLifecycleFrame.remove();
                globalThis.__webSceneLifecycleOwnerStillAlive = 41;
                """, "v8-lifecycle-frame-remove.js");
            Dispatcher.UIThread.RunJobs();
            var removedFrameReclaimed = runtime.ActiveFrameCount == 0
                                        && host.BrowserWindow.PendingTimerCount == baselineTimers
                                        && host.PendingExternalAnimationFrameCount == baselineAnimationFrames
                                        && frameDocument.ExternalEventListenerAdapter is null
                                        && frameDocument.ExternalWindowContext is null
                                        && frameDocument.ExternalRuntime is null
                                        && iframe.GetContentDocument() is null
                                        && iframe.GetExternalContentWindowRuntime() is null
                                        && Convert.ToInt32(runtime.Engine.Evaluate(
                                            "__webSceneLifecycleOwnerStillAlive + 1")) == 42;

            runtime.Execute("""
                document.body.appendChild(__webSceneLifecycleFrame);
                """, "v8-lifecycle-frame-reinsert.js");
            var (reinsertedIframe, reinsertedDocument) = WaitForFrame(host);
            var frameRemounted = removedFrameReclaimed
                                 && ReferenceEquals(iframe, reinsertedIframe)
                                 && !ReferenceEquals(frameDocument, reinsertedDocument)
                                 && !ReferenceEquals(firstWindow, reinsertedDocument.ExternalWindowContext)
                                 && runtime.ActiveFrameCount == 1
                                 && Convert.ToInt32(runtime.Engine.Evaluate(
                                     "__webSceneLifecycleFrame.contentWindow.frameCounter")) == 41;
            runtime.CollectGarbage(exhaustive: true);
            var heap = runtime.GetHeapInfo();

            runtime.Dispose();
            var ownerDetached = host.ExternalCallbackAdapter is null
                                && host.ExternalVirtualBrowsingContextFactory is null
                                && host.Document.ExternalEventListenerAdapter is null
                                && host.Document.ExternalWindowContext is null
                                && host.Document.ExternalWindowEventDispatcher is null;
            var frameDetached = reinsertedDocument.ExternalEventListenerAdapter is null
                                && reinsertedDocument.ExternalWindowContext is null
                                && reinsertedDocument.ExternalRuntime is null
                                && iframe.GetExternalContentWindowRuntime() is null;
            var proxyPassed = attached.Entries >= baseline.Entries + nodes + 1
                              && detached.Entries <= baseline.Entries + 2
                              && detached.Strong < attached.Strong;
            return new IterationResult(
                proxyPassed && frameAttached && frameWorks && frameTasksAttached
                    && frameRemounted && ownerDetached && frameDetached,
                baseline,
                attached,
                detached,
                checked((long)heap.UsedHeapSize),
                checked((long)heap.TotalPhysicalSize),
                ownerDetached,
                frameRemounted,
                frameDetached);
        }
        finally
        {
            runtime.Dispose();
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool ProbeMultipleInstances()
    {
        var firstWindow = CreateWindow("V8 lifecycle A");
        var secondWindow = CreateWindow("V8 lifecycle B");
        firstWindow.Show();
        secondWindow.Show();
        Dispatcher.UIThread.RunJobs();
        using var firstHost = new AvaloniaBrowserHost(firstWindow);
        using var secondHost = new AvaloniaBrowserHost(secondWindow);
        var options = new ClearScriptV8RuntimeOptions
        {
            EnableTrustedSameOriginContextSharing = true
        };
        using var first = new ClearScriptV8Runtime(firstHost, options);
        using var second = new ClearScriptV8Runtime(secondHost, options);
        try
        {
            first.Execute("globalThis.instanceToken = 'A'; globalThis.instanceCount = 1;");
            second.Execute("globalThis.instanceToken = 'B'; globalThis.instanceCount = 40;");
            first.Execute("""
                globalThis.__webSceneIsolationFrame = document.createElement('iframe');
                __webSceneIsolationFrame.id = 'v8-isolation-frame';
                document.body.appendChild(__webSceneIsolationFrame);
                __webSceneIsolationFrame.src = URL.createObjectURL(new Blob([
                  '<!doctype html><html><body><div id="isolation-ready">A</div>' +
                  '<script>globalThis.frameInstanceToken = "A";<\/script></body></html>'
                ], { type: 'text/html' }));
                """, "v8-isolation-frame-a.js");
            second.Execute("""
                globalThis.__webSceneIsolationFrame = document.createElement('iframe');
                __webSceneIsolationFrame.id = 'v8-isolation-frame';
                document.body.appendChild(__webSceneIsolationFrame);
                __webSceneIsolationFrame.src = URL.createObjectURL(new Blob([
                  '<!doctype html><html><body><div id="isolation-ready">B</div>' +
                  '<script>globalThis.frameInstanceToken = "B";<\/script></body></html>'
                ], { type: 'text/html' }));
                """, "v8-isolation-frame-b.js");

            var (_, firstFrameDocument) = WaitForFrame(
                firstHost,
                "#v8-isolation-frame",
                "#isolation-ready");
            var (_, secondFrameDocument) = WaitForFrame(
                secondHost,
                "#v8-isolation-frame",
                "#isolation-ready");
            var framesIndependent = first.ActiveFrameCount == 1
                                    && second.ActiveFrameCount == 1
                                    && string.Equals(
                                        Convert.ToString(first.Engine.Evaluate(
                                            "__webSceneIsolationFrame.contentWindow.frameInstanceToken")),
                                        "A",
                                        StringComparison.Ordinal)
                                    && string.Equals(
                                        Convert.ToString(second.Engine.Evaluate(
                                            "__webSceneIsolationFrame.contentWindow.frameInstanceToken")),
                                        "B",
                                        StringComparison.Ordinal)
                                    && !ReferenceEquals(firstFrameDocument, secondFrameDocument)
                                    && !ReferenceEquals(
                                        firstFrameDocument.ExternalWindowContext,
                                        secondFrameDocument.ExternalWindowContext);
            first.Dispose();
            second.Execute("instanceCount += 2;");
            var firstDetached = firstHost.ExternalCallbackAdapter is null
                                && firstHost.Document.ExternalWindowContext is null
                                && firstHost.Document.ExternalWindowEventDispatcher is null
                                && first.ActiveFrameCount == 0
                                && firstFrameDocument.ExternalEventListenerAdapter is null
                                && firstFrameDocument.ExternalWindowContext is null
                                && firstFrameDocument.ExternalRuntime is null;
            var secondAttached = secondHost.ExternalCallbackAdapter is not null
                                 && secondHost.Document.ExternalWindowContext is not null
                                 && secondHost.Document.ExternalWindowEventDispatcher is not null
                                 && second.ActiveFrameCount == 1
                                 && secondFrameDocument.ExternalEventListenerAdapter is not null
                                 && secondFrameDocument.ExternalWindowContext is not null;
            var secondIndependent = string.Equals(
                                        Convert.ToString(second.Engine.Evaluate("instanceToken")),
                                        "B",
                                        StringComparison.Ordinal)
                                    && Convert.ToInt32(second.Engine.Evaluate("instanceCount")) == 42;
            var secondFrameIndependent = string.Equals(
                Convert.ToString(second.Engine.Evaluate(
                    "__webSceneIsolationFrame.contentWindow.frameInstanceToken")),
                "B",
                StringComparison.Ordinal);
            var passed = framesIndependent && firstDetached && secondAttached
                         && secondIndependent && secondFrameIndependent;
            Console.WriteLine(
                $"V8 simultaneous runtime isolation: {(passed ? "pass" : "fail")}; " +
                $"frames-independent={framesIndependent}, first-detached={firstDetached}, " +
                $"second-attached={secondAttached}, second-independent={secondIndependent}, " +
                $"second-frame-independent={secondFrameIndependent}");
            return passed;
        }
        finally
        {
            firstWindow.Close();
            secondWindow.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static (AvaloniaDomElement Iframe, VirtualIframeDomDocument Document) WaitForFrame(
        AvaloniaBrowserHost host)
        => WaitForFrame(host, "#v8-lifecycle-frame", "#frame-ready");

    private static (AvaloniaDomElement Iframe, VirtualIframeDomDocument Document) WaitForFrame(
        AvaloniaBrowserHost host,
        string iframeSelector,
        string readySelector)
    {
        var timeout = Stopwatch.StartNew();
        while (timeout.Elapsed < TimeSpan.FromSeconds(5))
        {
            Thread.Sleep(4);
            Dispatcher.UIThread.RunJobs();
            var iframe = host.Document.querySelector(iframeSelector) as AvaloniaDomElement;
            if (iframe?.GetContentDocument() is VirtualIframeDomDocument frameDocument
                && frameDocument.querySelector(readySelector) is not null)
            {
                return (iframe, frameDocument);
            }
        }
        throw new TimeoutException($"The V8 lifecycle iframe '{iframeSelector}' did not initialize.");
    }

    private static ProxyRetention ReadOwnerRetention(string json)
    {
        using var document = JsonDocument.Parse(json);
        var owner = document.RootElement.EnumerateArray()
            .First(item => string.Equals(item.GetProperty("scope").GetString(), "owner", StringComparison.Ordinal));
        var retention = owner.GetProperty("retention");
        return new ProxyRetention(
            retention.GetProperty("entries").GetInt32(),
            retention.GetProperty("strong").GetInt32(),
            retention.GetProperty("weakLive").GetInt32(),
            retention.GetProperty("weakDead").GetInt32());
    }

    private static Window CreateWindow(string title)
        => new()
        {
            Title = title,
            Width = 480,
            Height = 320,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback,
        int minimum,
        int maximum)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(args[index + 1], out var value))
            {
                return Math.Clamp(value, minimum, maximum);
            }
        }
        return fallback;
    }

    private static void ForceManagedCollection()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static long GetProcessMemoryBytes(Process process)
        => process.PrivateMemorySize64 > 0
            ? process.PrivateMemorySize64
            : process.WorkingSet64;

    private readonly record struct ProxyRetention(
        int Entries,
        int Strong,
        int WeakLive,
        int WeakDead);

    private readonly record struct IterationResult(
        bool Passed,
        ProxyRetention Baseline,
        ProxyRetention Attached,
        ProxyRetention Detached,
        long UsedHeapAfterCollection,
        long PhysicalHeapAfterCollection,
        bool OwnerDetached,
        bool FrameRemounted,
        bool FrameDetached);
}

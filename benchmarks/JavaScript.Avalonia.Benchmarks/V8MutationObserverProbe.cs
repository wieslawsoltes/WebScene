using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

namespace JavaScript.Avalonia.Benchmarks;

public static class V8MutationObserverProbe
{
    internal static int Run()
    {
        BenchmarkApp.EnsureInitialized();
        var window = new Window { Width = 320, Height = 240, Content = new CssLayoutPanel() };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            using var host = new AvaloniaBrowserHost(window);
            using var runtime = new ClearScriptV8Runtime(host);
            var element = (AvaloniaDomElement)host.Document.createElement("div")!;

            using (var warmup = new ManagedObserverScope(host.Document, element))
            {
                for (var index = 0; index < 100; index++)
                {
                    element.setAttribute("data-observer-probe", index.ToString());
                }
                _ = warmup.Observer.takeRecords();
            }

            const int managedIterations = 5_000;
            using var measured = new ManagedObserverScope(host.Document, element);
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var allocationStart = GC.GetAllocatedBytesForCurrentThread();
            var managedStarted = Stopwatch.StartNew();
            for (var index = 0; index < managedIterations; index++)
            {
                element.setAttribute("data-observer-probe", index.ToString());
            }
            var managedRecords = measured.Observer.takeRecords();
            managedStarted.Stop();
            var managedAllocated = GC.GetAllocatedBytesForCurrentThread() - allocationStart;

            runtime.Execute("""
                const observerProbeElement = document.createElement('div');
                const observerProbeWarmup = new MutationObserver(function () {});
                observerProbeWarmup.observe(observerProbeElement, {
                  attributes: true,
                  attributeOldValue: true
                });
                for (let index = 0; index < 100; index++) {
                  observerProbeElement.setAttribute('data-observer-probe', String(index));
                }
                observerProbeWarmup.takeRecords();
                observerProbeWarmup.disconnect();
                """, "v8-observer-warmup.js");

            allocationStart = GC.GetAllocatedBytesForCurrentThread();
            var v8Started = Stopwatch.StartNew();
            runtime.Execute("""
                const observerProbe = new MutationObserver(function () {});
                observerProbe.observe(observerProbeElement, {
                  attributes: true,
                  attributeOldValue: true
                });
                for (let index = 0; index < 2000; index++) {
                  observerProbeElement.setAttribute('data-observer-probe', String(index));
                }
                globalThis.observerProbeRecordCount = observerProbe.takeRecords().length;
                observerProbe.disconnect();
                """, "v8-observer-measure.js");
            v8Started.Stop();
            var v8Allocated = GC.GetAllocatedBytesForCurrentThread() - allocationStart;
            var v8RecordCount = Convert.ToInt32(runtime.Engine.Script.observerProbeRecordCount);
            var passed = managedRecords.Length == managedIterations && v8RecordCount == 2_000;

            Console.WriteLine(
                $"V8 mutation-observer interop: {(passed ? "pass" : "fail")}; " +
                $"managed={managedStarted.Elapsed.TotalMilliseconds:F1} ms/{managedAllocated / 1024.0:F1} KB " +
                $"({managedIterations} records), " +
                $"v8={v8Started.Elapsed.TotalMilliseconds:F1} ms/{v8Allocated / 1024.0:F1} KB " +
                "(2000 records)");
            return passed ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(
                $"V8 mutation-observer interop: fail; {exception.GetType().Name}: {exception.Message}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private sealed class ManagedObserverScope : IDisposable
    {
        public ManagedObserverScope(AvaloniaDomDocument document, AvaloniaDomElement element)
        {
            Observer = new DomMutationObserver(document, NoOpCallback.Instance);
            Observer.__webSceneObserve(
                element,
                childList: false,
                attributes: true,
                subtree: false,
                attributeOldValue: true);
        }

        public DomMutationObserver Observer { get; }

        public void Dispose() => Observer.disconnect();
    }

    private sealed class NoOpCallback : IExternalJavaScriptCallback
    {
        public static NoOpCallback Instance { get; } = new();

        public void Invoke(object? receiver, params object?[] arguments)
        {
        }
    }
}

using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

namespace JavaScript.Avalonia.Benchmarks;

internal static class NativeViewLifecycleProbe
{
    internal static int Run(string[] args)
    {
        _ = args;
        BenchmarkApp.EnsureInitialized();
        var library = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }
        var sharedIsolateEnabled = Environment.GetEnvironmentVariable(
            "WEBSCENE_V8_SHARED_ISOLATE") is not null;
        NativeWebSceneApi.ConfigureLibraryPath(library);
        if (NativeWebSceneApi.EnginePrewarm() == 0)
        {
            throw new InvalidOperationException(
                "The native lifecycle probe could not prewarm V8.");
        }

        var sentinel = NativeWebSceneApi.EngineCreate(
            simulatedChartCommandCount: 0,
            compilationCacheDirectory: null,
            EmptyResourceLoader.Instance,
            static _ => { });
        if (sentinel == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "The native lifecycle sentinel could not be created.");
        }

        var directory = Path.Combine(
            Path.GetTempPath(),
            "webscene-native-lifecycle-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var documentPath = Path.Combine(directory, "index.html");
        File.WriteAllText(
            documentPath,
            "<!doctype html><html><body><div id='ready'>ready</div></body></html>");
        var firstPanel = new Grid();
        var secondPanel = new Grid();
        var floatingPanel = new Grid();
        var firstWindow = new Window
        {
            Width = 640,
            Height = 400,
            Content = new Grid
            {
                Children = { firstPanel, secondPanel }
            }
        };
        var floatingWindow = new Window
        {
            Width = 480,
            Height = 320,
            Content = floatingPanel
        };
        var view = new NativeWebSceneView(useCompositionVisual: false);
        firstPanel.Children.Add(view);
        firstWindow.Show();
        floatingWindow.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            var baselineContexts = WaitForActiveContexts(
                sentinel,
                minimum: 1);
            Pump(view.LoadAsync(new Uri(documentPath).AbsoluteUri, library));
            Dispatcher.UIThread.RunJobs();
            var initial = view.CapturePerformanceSnapshot();
            Pump(view.EvaluateTextAsync(
                "globalThis.__lifecycleToken = 41; __lifecycleToken",
                "native-lifecycle-token.js"));
            var loadedContexts = initial.ProcessCache
                ?.SharedIsolateActiveContexts ?? 0;
            var loadedSlot = initial.ProcessCache
                ?.SharedIsolateSlot ?? ulong.MaxValue;

            firstPanel.Children.Remove(view);
            secondPanel.Children.Add(view);
            Dispatcher.UIThread.RunJobs();
            var sameWindow = view.CapturePerformanceSnapshot();
            var sameWindowValue = Pump(view.EvaluateTextAsync(
                "++__lifecycleToken",
                "native-lifecycle-same-window.js"));

            secondPanel.Children.Remove(view);
            floatingPanel.Children.Add(view);
            Dispatcher.UIThread.RunJobs();
            var floating = view.CapturePerformanceSnapshot();
            var floatingValue = Pump(view.EvaluateTextAsync(
                "++__lifecycleToken",
                "native-lifecycle-floating-window.js"));

            var surface = (NativeSceneSurface)view.Content!;
            surface.SetPresentationActive(false);
            Pump(view.EvaluateTextAsync(
                "globalThis.__hiddenLifecycleTimer = 0; setTimeout(() => __hiddenLifecycleTimer = 44, 0); true",
                "native-lifecycle-hidden.js"));
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            string hiddenValue;
            do
            {
                Thread.Sleep(10);
                Dispatcher.UIThread.RunJobs();
                hiddenValue = Pump(view.EvaluateTextAsync(
                    "__hiddenLifecycleTimer",
                    "native-lifecycle-hidden-check.js"));
            }
            while (hiddenValue != "44" && DateTime.UtcNow < deadline);
            surface.SetPresentationActive(true);
            Dispatcher.UIThread.RunJobs();
            var visible = view.CapturePerformanceSnapshot();

            var identityStable = initial.ContextId == sameWindow.ContextId
                && initial.ContextId == floating.ContextId
                && initial.ContextId == visible.ContextId;
            var stateStable = sameWindowValue == "42"
                && floatingValue == "43"
                && hiddenValue == "44";

            Pump(view.DisposeAsync().AsTask());
            var replacement = NativeWebSceneApi.EngineCreate(
                simulatedChartCommandCount: 0,
                compilationCacheDirectory: null,
                EmptyResourceLoader.Instance,
                static _ => { });
            if (replacement == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "The native lifecycle replacement context could not be created.");
            }
            ulong replacementSlot;
            try
            {
                if (!NativeWebSceneApi.TryExecuteScript(
                        replacement,
                        "true",
                        "native-lifecycle-replacement.js"))
                {
                    throw new InvalidOperationException(
                        "The native lifecycle replacement context failed: "
                        + NativeWebSceneApi.GetLastError(replacement));
                }
                replacementSlot = sharedIsolateEnabled
                    ? WaitForSharedIsolateSlot(replacement)
                    : ulong.MaxValue;
            }
            finally
            {
                NativeWebSceneApi.EngineDestroy(replacement);
            }
            var releasedContexts = WaitForActiveContexts(
                sentinel,
                maximum: baselineContexts);
            var releasedSlotReused = loadedSlot != ulong.MaxValue
                && replacementSlot == loadedSlot;
            var correct = identityStable
                && stateStable
                && loadedContexts >= 1
                && (!sharedIsolateEnabled || releasedSlotReused)
                && releasedContexts == baselineContexts;
            Console.WriteLine(JsonSerializer.Serialize(
                new
                {
                    initialContextId = initial.ContextId,
                    sameWindowContextId = sameWindow.ContextId,
                    floatingWindowContextId = floating.ContextId,
                    visibleContextId = visible.ContextId,
                    sameWindowValue,
                    floatingValue,
                    hiddenValue,
                    baselineContexts,
                    loadedContexts,
                    loadedSlot,
                    replacementSlot,
                    releasedContexts,
                    releasedSlotReused,
                    sharedIsolateEnabled,
                    identityStable,
                    stateStable,
                    correct
                },
                new JsonSerializerOptions { WriteIndented = true }));
            return correct ? 0 : 1;
        }
        finally
        {
            Pump(view.DisposeAsync().AsTask());
            firstWindow.Close();
            floatingWindow.Close();
            Dispatcher.UIThread.RunJobs();
            NativeWebSceneApi.EngineDestroy(sentinel);
            try
            {
                Directory.Delete(directory, recursive: true);
            }
            catch
            {
            }
        }
    }

    private static ulong WaitForActiveContexts(
        IntPtr sentinel,
        ulong? minimum = null,
        ulong? maximum = null)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        ulong active;
        do
        {
            if (!NativeWebSceneApi.TryExecuteScript(
                    sentinel,
                    "true",
                    "native-lifecycle-context-count-barrier.js"))
            {
                throw new InvalidOperationException(
                    "The native lifecycle context-count barrier failed: "
                    + NativeWebSceneApi.GetLastError(sentinel));
            }
            active = NativeWebSceneApi.TryGetProcessCacheMetrics(sentinel)
                ?.SharedIsolateActiveContexts
                ?? throw new InvalidOperationException(
                    "The native process cache metrics ABI is unavailable.");
            if ((!minimum.HasValue || active >= minimum.Value)
                && (!maximum.HasValue || active <= maximum.Value))
            {
                return active;
            }
            Thread.Sleep(10);
            Dispatcher.UIThread.RunJobs();
        }
        while (DateTime.UtcNow < deadline);
        return active;
    }

    private static ulong WaitForSharedIsolateSlot(IntPtr engine)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        ulong slot;
        do
        {
            slot = NativeWebSceneApi.TryGetProcessCacheMetrics(engine)
                ?.SharedIsolateSlot ?? ulong.MaxValue;
            if (slot != ulong.MaxValue) return slot;
            Thread.Sleep(2);
            Dispatcher.UIThread.RunJobs();
        }
        while (DateTime.UtcNow < deadline);
        return slot;
    }

    private static void Pump(Task task)
    {
        while (!task.IsCompleted)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(1);
        }
        task.GetAwaiter().GetResult();
    }

    private static T Pump<T>(Task<T> task)
    {
        Pump((Task)task);
        return task.GetAwaiter().GetResult();
    }

    private sealed class EmptyResourceLoader : IWebSceneResourceLoader
    {
        internal static EmptyResourceLoader Instance { get; } = new();

        public WebSceneTextResource LoadText(
            in WebSceneResourceRequest request)
            => throw new InvalidOperationException(
                $"Unexpected lifecycle resource '{request.Specifier}'.");
    }
}

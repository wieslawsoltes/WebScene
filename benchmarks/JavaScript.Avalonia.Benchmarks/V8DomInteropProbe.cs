using System.Buffers;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;
using System.Diagnostics;
using Microsoft.ClearScript;
using Microsoft.ClearScript.JavaScript;
using Microsoft.ClearScript.V8;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Boundary spike only: checks whether ClearScript/V8 can drive the existing
/// Avalonia DOM/canvas objects without a second DOM implementation.
/// </summary>
public static class V8DomInteropProbe
{
    private const string HotCanvasScript = """
        context.clearRect(0, 0, 120, 80);
        for (let i = 0; i < 500; i++) {
            context.fillStyle = (i & 1) === 0 ? '#ef5350' : '#42a5f5';
            context.fillRect(i % 120, (i * 3) % 80, 1, 1);
        }
        """;

    private const string PureJavaScriptSetup = """
        function runPureJavaScript(count) {
            const bars = new Array(count);
            let rolling = 0;
            for (let i = 0; i < count; i++) {
                const close = 100 + Math.sin(i * 0.017) * 8 + (i % 29) * 0.03125;
                const bar = {
                    time: 1700000000 + i * 60,
                    open: close - 0.25,
                    high: close + 0.75,
                    low: close - 0.5,
                    close,
                    volume: 1000 + (i % 97) * 11
                };
                bars[i] = bar;
                rolling += bar.close * bar.volume;
                if (i >= 32) rolling -= bars[i - 32].close * bars[i - 32].volume;
                bar.weighted = rolling / Math.min(i + 1, 32);
            }
            return bars[count - 1].weighted;
        }
        globalThis.pureJavaScriptSink = 0;
        """;

    private const string PureJavaScriptWork = """
        for (let iteration = 0; iteration < 8; iteration++) {
            pureJavaScriptSink = runPureJavaScript(8479);
        }
        """;

    private const string BatchedCanvasSetup = """
        class BatchedCanvasRenderingContext2D {
            constructor(sink, capacity) {
                this.sink = sink;
                this.buffer = new Float64Array(capacity * 6);
                this.commandCount = 0;
                this.fillStyleCode = 0;
            }

            set fillStyle(value) {
                this.fillStyleCode = value === '#42a5f5' ? 1 : 0;
            }

            clearRect(x, y, width, height) {
                this.push(0, x, y, width, height, 0);
            }

            fillRect(x, y, width, height) {
                this.push(1, x, y, width, height, this.fillStyleCode);
            }

            push(opcode, x, y, width, height, styleCode) {
                if (this.commandCount * 6 === this.buffer.length) this.flush();
                const offset = this.commandCount++ * 6;
                this.buffer[offset] = opcode;
                this.buffer[offset + 1] = x;
                this.buffer[offset + 2] = y;
                this.buffer[offset + 3] = width;
                this.buffer[offset + 4] = height;
                this.buffer[offset + 5] = styleCode;
            }

            flush() {
                if (this.commandCount === 0) return;
                this.sink.Flush(this.buffer, this.commandCount);
                this.commandCount = 0;
            }
        }

        globalThis.batchedContext = new BatchedCanvasRenderingContext2D(batchSink, 501);
        """;

    private const string HotBatchedCanvasScript = """
        batchedContext.clearRect(0, 0, 120, 80);
        for (let i = 0; i < 500; i++) {
            batchedContext.fillStyle = (i & 1) === 0 ? '#ef5350' : '#42a5f5';
            batchedContext.fillRect(i % 120, (i * 3) % 80, 1, 1);
        }
        batchedContext.flush();
        """;

    internal static int Run()
    {
        BenchmarkApp.EnsureInitialized();
        if (!ProbeOptionalRuntimeTrustAndDisposal())
        {
            return 1;
        }
        ProbeSharedRuntimeContexts();
        ProbeSharedRuntimeMicrotasks();
        if (!ProbeBatchedRuntimeCanvas())
        {
            return 1;
        }
        var window = CreateWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var v8 = new V8ScriptEngine();
        v8.AddHostObject("document", host.Document);

        try
        {
            v8.Execute("""
                globalThis.canvas = document.createElement('canvas');
                canvas.width = 120;
                canvas.height = 80;
                document.body.appendChild(canvas);
                globalThis.context = canvas.getContext('2d');
                context.fillStyle = '#ef5350';
                context.fillRect(0, 0, 120, 80);
                globalThis.v8BoundaryResult = {
                    canvasType: typeof canvas,
                    contextType: typeof context,
                    fillRectType: typeof context.fillRect
                };
                """);

            dynamic result = v8.Script.v8BoundaryResult;
            Console.WriteLine($"V8 DOM boundary succeeded: canvas={result.canvasType}, " +
                              $"context={result.contextType}, fillRect={result.fillRectType}");

            for (var iteration = 0; iteration < 3; iteration++)
            {
                v8.Execute(HotCanvasScript);
            }
            var v8AllocationStarted = GC.GetAllocatedBytesForCurrentThread();
            var v8Started = Stopwatch.StartNew();
            for (var iteration = 0; iteration < 8; iteration++)
            {
                v8.Execute(HotCanvasScript);
            }
            v8Started.Stop();
            var v8AllocatedBytes = GC.GetAllocatedBytesForCurrentThread() - v8AllocationStarted;

            var batchCanvas = (AvaloniaDomElement?)host.Document.createElement("canvas")
                              ?? throw new InvalidOperationException("Unable to create the batch-probe canvas.");
            batchCanvas.width = 120;
            batchCanvas.height = 80;
            var batchContext = (CanvasRenderingContext2D?)batchCanvas.getContext("2d")
                               ?? throw new InvalidOperationException("Unable to create the batch-probe 2D context.");
            var batchSink = new V8CanvasBatchSink(batchContext);
            v8.AddHostObject("batchSink", batchSink);
            v8.Execute(BatchedCanvasSetup);
            for (var iteration = 0; iteration < 3; iteration++)
            {
                v8.Execute(HotBatchedCanvasScript);
            }
            var flushCountStarted = batchSink.FlushCount;
            var replayedCommandCountStarted = batchSink.ReplayedCommandCount;
            var batchedAllocationStarted = GC.GetAllocatedBytesForCurrentThread();
            var batchedStarted = Stopwatch.StartNew();
            for (var iteration = 0; iteration < 8; iteration++)
            {
                v8.Execute(HotBatchedCanvasScript);
            }
            batchedStarted.Stop();
            var batchedAllocatedBytes = GC.GetAllocatedBytesForCurrentThread() - batchedAllocationStarted;

            ReplayManagedHotCanvas(batchContext);
            var managedAllocationStarted = GC.GetAllocatedBytesForCurrentThread();
            var managedStarted = Stopwatch.StartNew();
            for (var iteration = 0; iteration < 8; iteration++)
            {
                ReplayManagedHotCanvas(batchContext);
            }
            managedStarted.Stop();
            var managedAllocatedBytes = GC.GetAllocatedBytesForCurrentThread() - managedAllocationStarted;

            Console.WriteLine($"Hot canvas loop (8 x 501 commands): " +
                              $"V8={v8Started.Elapsed.TotalMilliseconds:F1} ms/{v8AllocatedBytes / 1024.0:F1} KB");
            Console.WriteLine($"Batched V8 canvas (8 x 501 commands): {batchedStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                              $"{batchedAllocatedBytes / 1024.0:F1} KB managed, " +
                              $"flushes={batchSink.FlushCount - flushCountStarted}, " +
                              $"replayed={batchSink.ReplayedCommandCount - replayedCommandCountStarted}");
            Console.WriteLine($"Managed canvas replay (8 x 501 commands): {managedStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                              $"{managedAllocatedBytes / 1024.0:F1} KB managed");

            v8.Execute(PureJavaScriptSetup);
            v8.Execute("pureJavaScriptSink = runPureJavaScript(8479);");
            var v8PureStarted = Stopwatch.StartNew();
            v8.Execute(PureJavaScriptWork);
            v8PureStarted.Stop();

            Console.WriteLine($"Pure JavaScript bar loop (8 x 8,479 bars): V8={v8PureStarted.Elapsed.TotalMilliseconds:F1} ms");

            var v8EventElement = CreateEventElement(host);
            v8.Execute("globalThis.externalEventChecksum = 0;");
            var v8EventApplyCallback = (ScriptObject)v8.Evaluate("(function(callback, currentTarget, event) { return callback.call(currentTarget, event); })");
            var v8ApplyCallback = (ScriptObject)v8.Evaluate("(function(callback, currentTarget, args) { return callback.apply(currentTarget, Array.from(args)); })");
            var v8EventAdapter = new V8ExternalEventListenerAdapter(v8EventApplyCallback, v8ApplyCallback);
            host.Document.ExternalEventListenerAdapter = v8EventAdapter;
            host.ExternalCallbackAdapter = v8EventAdapter;
            v8.AddHostObject("v8EventElement", v8EventElement);
            v8.Execute("""
                globalThis.externalEventCallback = function(event) {
                    externalEventChecksum += event.clientX + event.clientY;
                };
                v8EventElement.addEventListener('pointermove', externalEventCallback, { passive: true });
                v8EventElement.addEventListener('pointermove', externalEventCallback, { passive: true });
                """);
            var v8EventCallback = (ScriptObject)v8.Script.externalEventCallback;
            var v8EventListener = (V8ExternalEventListener?)v8EventAdapter.GetEventListener(v8EventCallback, create: false)
                                  ?? throw new InvalidOperationException("V8 callback adapter did not preserve listener identity.");

            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            MeasurePointerMoves(v8EventElement, pointer, 10);
            var v8Events = MeasurePointerMoves(v8EventElement, pointer, 1000);
            v8.Execute("v8EventElement.removeEventListener('pointermove', externalEventCallback);");
            var callbacksBeforeRemovalProbe = v8EventListener.InvokeCount;
            MeasurePointerMoves(v8EventElement, pointer, 1);
            Console.WriteLine($"External pointer listener (1,000 events): " +
                              $"V8={v8Events.Elapsed.TotalMilliseconds:F1} ms/{v8Events.AllocatedBytes / 1024.0:F1} KB, " +
                              $"V8 callbacks={v8EventListener.InvokeCount}, " +
                              $"identity/removal={(v8EventListener.InvokeCount == callbacksBeforeRemovalProbe ? "pass" : "fail")}");

            v8.AddHostObject("externalWindow", host.BrowserWindow);
            v8.Execute("""
                globalThis.externalTaskState = { timer: 0, interval: 0, microtask: 0, frame: 0, cancelled: 0 };
                externalWindow.setTimeout(function() { externalTaskState.timer++; }, 0);
                const intervalId = externalWindow.setInterval(function() {
                    externalTaskState.interval++;
                    externalWindow.clearInterval(intervalId);
                }, 1);
                externalWindow.queueMicrotask(function() { externalTaskState.microtask++; });
                externalWindow.requestAnimationFrame(function(timestamp) {
                    if (timestamp >= 0) externalTaskState.frame++;
                });
                const cancelledTimer = externalWindow.setTimeout(function() { externalTaskState.cancelled++; }, 0);
                externalWindow.clearTimeout(cancelledTimer);
                const cancelledFrame = externalWindow.requestAnimationFrame(function() { externalTaskState.cancelled++; });
                externalWindow.cancelAnimationFrame(cancelledFrame);
                """);
            for (var iteration = 0; iteration < 4; iteration++)
            {
                Thread.Sleep(17);
                Dispatcher.UIThread.RunJobs();
            }
            var taskState = (ScriptObject)v8.Script.externalTaskState;
            var taskStatePassed = Convert.ToInt32(taskState.GetProperty("timer")) == 1
                                  && Convert.ToInt32(taskState.GetProperty("interval")) == 1
                                  && Convert.ToInt32(taskState.GetProperty("microtask")) == 1
                                  && Convert.ToInt32(taskState.GetProperty("frame")) == 1
                                  && Convert.ToInt32(taskState.GetProperty("cancelled")) == 0;
            Console.WriteLine($"External V8 task sources: {(taskStatePassed ? "pass" : "fail")}; " +
                              $"timer={taskState.GetProperty("timer")}, interval={taskState.GetProperty("interval")}, " +
                              $"microtask={taskState.GetProperty("microtask")}, frame={taskState.GetProperty("frame")}, " +
                              $"cancelled={taskState.GetProperty("cancelled")}");
            if (!taskStatePassed)
            {
                return 1;
            }

            v8.Execute("""
                function ExternalMutationObserver(callback) {
                    const observer = this;
                    this.backend = document.__webSceneCreateExternalMutationObserver(function(records) {
                        callback(Array.from(records), observer);
                    });
                    this.backend.__webSceneSetExternalObserver(this);
                }
                ExternalMutationObserver.prototype.observe = function(target, options) {
                    options = options || {};
                    this.backend.__webSceneObserve(
                        target,
                        Boolean(options.childList),
                        Boolean(options.attributes),
                        Boolean(options.subtree),
                        Boolean(options.attributeOldValue));
                };
                ExternalMutationObserver.prototype.disconnect = function() { this.backend.disconnect(); };
                ExternalMutationObserver.prototype.takeRecords = function() {
                    return Array.from(this.backend.takeRecords());
                };

                globalThis.externalMutationState = { calls: 0, records: 0, type: '', oldValue: '' };
                globalThis.externalMutationObserver = new ExternalMutationObserver(function(records) {
                    externalMutationState.calls++;
                    externalMutationState.records += records.length;
                    if (records.length) {
                        externalMutationState.type = records[0].type;
                        externalMutationState.oldValue = String(records[records.length - 1].oldValue || '');
                    }
                });
                externalMutationObserver.observe(v8EventElement, {
                    attributes: true,
                    attributeOldValue: true
                });
                v8EventElement.setAttribute('data-v8-probe', 'first');
                v8EventElement.setAttribute('data-v8-probe', 'second');
                """);
            Dispatcher.UIThread.RunJobs();
            var mutationState = (ScriptObject)v8.Script.externalMutationState;
            var mutationPassed = Convert.ToInt32(mutationState.GetProperty("calls")) == 1
                                 && Convert.ToInt32(mutationState.GetProperty("records")) == 2
                                 && string.Equals(Convert.ToString(mutationState.GetProperty("type")), "attributes", StringComparison.Ordinal)
                                 && string.Equals(Convert.ToString(mutationState.GetProperty("oldValue")), "first", StringComparison.Ordinal);
            Console.WriteLine($"External V8 MutationObserver: {(mutationPassed ? "pass" : "fail")}; " +
                              $"calls={mutationState.GetProperty("calls")}, records={mutationState.GetProperty("records")}, " +
                              $"type={mutationState.GetProperty("type")}, old={mutationState.GetProperty("oldValue")}");
            if (!mutationPassed)
            {
                return 1;
            }
            v8.Execute("externalMutationObserver.disconnect();");

            var heap = v8.GetRuntimeHeapInfo();
            Console.WriteLine($"V8 heap after probes: used={heap.UsedHeapSize / (1024.0 * 1024.0):F1} MB, " +
                              $"physical={heap.TotalPhysicalSize / (1024.0 * 1024.0):F1} MB, " +
                              $"external={heap.TotalExternalSize / (1024.0 * 1024.0):F1} MB");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"V8 DOM boundary failed: {ex.GetType().Name}: {ex.Message}");
            return 1;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool ProbeOptionalRuntimeTrustAndDisposal()
    {
        var window = CreateWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();
        using var host = new AvaloniaBrowserHost(window);
        var runtime = new ClearScriptV8Runtime(host);
        var defaultIsolation = host.ExternalVirtualBrowsingContextFactory is null;
        var attached = host.ExternalCallbackAdapter is not null
                       && host.Document.ExternalEventListenerAdapter is not null
                       && host.Document.ExternalWindowContext is not null;

        ClearScriptV8Runtime? competingRuntime = null;
        var competingRuntimeRejected = false;
        try
        {
            competingRuntime = new ClearScriptV8Runtime(host);
        }
        catch (InvalidOperationException)
        {
            competingRuntimeRejected = true;
        }
        finally
        {
            competingRuntime?.Dispose();
        }

        var replacementWindowContext = new object();
        host.Document.ExternalWindowContext = replacementWindowContext;

        runtime.Dispose();

        var detached = host.ExternalVirtualBrowsingContextFactory is null
                       && host.ExternalCallbackAdapter is null
                       && host.Document.ExternalEventListenerAdapter is null;
        var replacementPreserved = ReferenceEquals(
            host.Document.ExternalWindowContext,
            replacementWindowContext);
        host.Document.ExternalWindowContext = null;
        var passed = defaultIsolation
                     && attached
                     && competingRuntimeRejected
                     && detached
                     && replacementPreserved;
        Console.WriteLine(
            $"Optional V8 runtime trust/disposal: {(passed ? "pass" : "fail")}; " +
            $"default-isolation={defaultIsolation}, attached={attached}, " +
            $"collision-rejected={competingRuntimeRejected}, detached={detached}, " +
            $"replacement-preserved={replacementPreserved}");
        window.Close();
        return passed;
    }

    private static Window CreateWindow()
        => new()
        {
            Width = 400,
            Height = 300,
            Content = new CssLayoutPanel { ClipToBounds = true }
        };

    private static void ProbeSharedRuntimeContexts()
    {
        using var runtime = new V8Runtime();
        using var owner = runtime.CreateScriptEngine("owner-context");
        using var frame = runtime.CreateScriptEngine("frame-context");
        owner.Execute("globalThis.ownerValue = { offset: 7 }; globalThis.ownerAdd = function(value) { return value + ownerValue.offset; };");

        try
        {
            frame.Script.ownerWindow = owner.Script;
            frame.Execute("globalThis.sharedContextResult = ownerWindow.ownerAdd(35);");
            Console.WriteLine(
                $"Shared V8 runtime owner/frame object bridge: " +
                $"{(Convert.ToInt32(frame.Script.sharedContextResult) == 42 ? "pass" : "fail")}");
        }
        catch (Exception exception)
        {
            Console.WriteLine(
                $"Shared V8 runtime owner/frame object bridge: fail; " +
                $"{exception.GetType().Name}: {exception.Message}");
        }
    }

    private static void ProbeSharedRuntimeMicrotasks()
    {
        using var runtime = new V8Runtime();
        using var owner = runtime.CreateScriptEngine("microtask-owner-context");
        using var frame = runtime.CreateScriptEngine("microtask-frame-context");
        owner.Execute("""
            globalThis.sequence = [];
            globalThis.record = function(value) { sequence.push(String(value)); };
            """);
        frame.Script.ownerWindow = owner.Script;
        frame.Execute("""
            globalThis.frameEntry = function() {
                ownerWindow.record('frame:start');
                Promise.resolve().then(function() { ownerWindow.record('frame:microtask'); });
                ownerWindow.record('frame:end');
            };
            """);
        owner.Script.frameWindow = frame.Script;
        owner.Execute("""
            record('direct:start');
            Promise.resolve().then(function() { record('direct:owner-microtask'); });
            frameWindow.frameEntry();
            record('direct:end');
            """);
        var directSequence = Convert.ToString(owner.Evaluate("sequence.join(',')")) ?? string.Empty;

        owner.Execute("sequence.length = 0;");
        owner.AddHostObject("nestedBridge", new NestedV8InvocationBridge(frame));
        owner.Execute("""
            record('nested:start');
            Promise.resolve().then(function() { record('nested:owner-microtask'); });
            nestedBridge.InvokeFrame();
            record('nested:end');
            """);
        var nestedSequence = Convert.ToString(owner.Evaluate("sequence.join(',')")) ?? string.Empty;
        var directExpected = "direct:start,frame:start,frame:end,direct:end,direct:owner-microtask,frame:microtask";
        var nestedExpected = "nested:start,frame:start,frame:end,nested:end,nested:owner-microtask,frame:microtask";
        Console.WriteLine(
            $"Shared V8 runtime direct microtask checkpoint: " +
            $"{(string.Equals(directSequence, directExpected, StringComparison.Ordinal) ? "pass" : "fail")}; " +
            directSequence);
        Console.WriteLine(
            $"Shared V8 runtime nested-host microtask checkpoint: " +
            $"{(string.Equals(nestedSequence, nestedExpected, StringComparison.Ordinal) ? "pass" : "fail")}; " +
            nestedSequence);
    }

    private static bool ProbeBatchedRuntimeCanvas()
    {
        var window = CreateWindow();
        window.Show();
        Dispatcher.UIThread.RunJobs();
        try
        {
            using var host = new AvaloniaBrowserHost(window);
            using var runtime = new ClearScriptV8Runtime(
                host,
                new ClearScriptV8RuntimeOptions
                {
                    EnableCanvasBatching = true,
                    EnableTrustedSameOriginContextSharing = true
                });
            var identityReadProbe = new DomIdentityReadProbe();
            runtime.Engine.AddHostObject("identityReadProbe", identityReadProbe);
            runtime.Engine.Execute("""
                globalThis.identityReadWrapped = __webSceneWrapHostObject(identityReadProbe);
                globalThis.batchProbeCanvas = document.createElement('canvas');
                document.documentElement.classList.add('theme-dark');
                document.documentElement.dataset.theme = 'dark';
                globalThis.batchProbeDomTokenSemantics =
                    document.documentElement.classList.contains('theme-dark') &&
                    Array.from(document.documentElement.classList).includes('theme-dark') &&
                    document.documentElement.classList.length === 1 &&
                    document.documentElement.getAttribute('data-theme') === 'dark';
                document.documentElement.classList.remove('theme-dark');
                delete document.documentElement.dataset.theme;
                globalThis.batchProbeAppendContainer = document.createElement('div');
                globalThis.batchProbeAppendChild = document.createElement('span');
                batchProbeAppendContainer.append(batchProbeAppendChild);
                globalThis.batchProbeAppendSemantics =
                    batchProbeAppendContainer.childElementCount === 1 &&
                    batchProbeAppendContainer.firstElementChild === batchProbeAppendChild;
                globalThis.batchProbeMethodIdentity =
                    document.createElement === document.createElement &&
                    batchProbeCanvas.getContext === batchProbeCanvas.getContext;
                batchProbeCanvas.width = 120;
                batchProbeCanvas.height = 80;
                document.body.appendChild(batchProbeCanvas);
                globalThis.batchProbeContext = batchProbeCanvas.getContext('2d');
                globalThis.batchProbeIdentity = batchProbeContext === batchProbeCanvas.getContext('2d') &&
                    batchProbeContext.canvas === batchProbeCanvas;
                globalThis.batchProbeRect = batchProbeCanvas.getBoundingClientRect();
                globalThis.batchProbeRectSemantics =
                    typeof batchProbeRect.x === 'number' &&
                    typeof batchProbeRect.y === 'number' &&
                    typeof batchProbeRect.width === 'number' &&
                    typeof batchProbeRect.height === 'number' &&
                    batchProbeRect.right === batchProbeRect.left + batchProbeRect.width &&
                    batchProbeRect.bottom === batchProbeRect.top + batchProbeRect.height &&
                    typeof batchProbeRect.toJSON === 'function';
                batchProbeContext.fillStyle = '#ef5350';
                batchProbeContext.save();
                batchProbeContext.translate(2, 3);
                batchProbeContext.fillRect(1, 2, 20, 10);
                batchProbeContext.restore();
                batchProbeContext.beginPath();
                batchProbeContext.moveTo(0, 0);
                batchProbeContext.lineTo(12, 8);
                batchProbeContext.stroke();
                globalThis.batchProbePath = new Path2D();
                batchProbePath.moveTo(1, 1);
                batchProbePath.lineTo(7, 1);
                batchProbePath.quadraticCurveTo(9, 4, 7, 7);
                batchProbePath.bezierCurveTo(5, 9, 3, 9, 1, 7);
                batchProbePath.arcTo(0, 4, 1, 1, 1);
                batchProbePath.arc(4, 4, 2, 0, Math.PI, false);
                batchProbePath.rect(2, 2, 3, 3);
                batchProbePath.closePath();
                batchProbeContext.fill(batchProbePath);
                globalThis.batchProbeSourcePath = new Path2D('M 2 1 L 8 1 L 5 7 Z');
                globalThis.batchProbeScaledPath = new Path2D();
                batchProbeScaledPath.addPath(batchProbeSourcePath, new DOMMatrix().scaleSelf(2, 3));
                batchProbeContext.fill(batchProbeScaledPath, 'evenodd');
                globalThis.batchProbeParsedSvg = new DOMParser().parseFromString(
                    '<svg viewBox="0 0 12 9"><g><path d="M 1 2 L 11 2 L 6 9 Z" /></g></svg>',
                    'application/xml');
                globalThis.batchProbeParsedPaths = batchProbeParsedSvg.getElementsByTagName('path');
                globalThis.batchProbeDomParserSemantics =
                    batchProbeParsedSvg.documentElement.tagName.toLowerCase() === 'svg' &&
                    batchProbeParsedSvg.documentElement.children.length === 1 &&
                    batchProbeParsedSvg.documentElement.children[0].children.length === 1 &&
                    batchProbeParsedPaths.length === 1;
                globalThis.batchProbeDomParserDetails = JSON.stringify({
                    root: batchProbeParsedSvg.documentElement.tagName,
                    rootChildren: batchProbeParsedSvg.documentElement.children.length,
                    groupChildren: batchProbeParsedSvg.documentElement.children[0] &&
                        batchProbeParsedSvg.documentElement.children[0].children.length,
                    paths: batchProbeParsedPaths.length
                });
                if (batchProbeDomParserSemantics) {
                    batchProbeContext.fill(new Path2D(batchProbeParsedPaths[0].getAttribute('d')));
                }
                globalThis.batchProbeTextWidth = batchProbeContext.measureText('WebScene').width;
                globalThis.batchProbeTransformA = batchProbeContext.getTransform().a;
                __webSceneFlushCanvases();
                """);
            var canvas = (AvaloniaDomElement)host.Document.querySelector("canvas")!;
            var surface = (CanvasDrawingSurface)canvas.Control;
            var initialKinds = surface.Commands.Select(command => command.GetType().Name).ToArray();
            var initialPassed = Convert.ToBoolean(runtime.Engine.Script.batchProbeIdentity)
                                && Convert.ToBoolean(runtime.Engine.Script.batchProbeDomTokenSemantics)
                                && Convert.ToBoolean(runtime.Engine.Script.batchProbeAppendSemantics)
                                && Convert.ToBoolean(runtime.Engine.Script.batchProbeMethodIdentity)
                                && Convert.ToBoolean(runtime.Engine.Script.batchProbeRectSemantics)
                                && Convert.ToBoolean(runtime.Engine.Script.batchProbeDomParserSemantics)
                                && Convert.ToDouble(runtime.Engine.Script.batchProbeTextWidth) > 0
                                && Convert.ToDouble(runtime.Engine.Script.batchProbeTransformA) == 1
                                && initialKinds.SequenceEqual([
                                    "FillRectCommand",
                                    "StrokePathCommand",
                                    "FillPathCommand",
                                    "FillPathCommand",
                                    "FillPathCommand"
                                ])
                                && surface.Commands[3].GetBounds() == new Rect(4, 3, 12, 18)
                                && surface.Commands[4].GetBounds() == new Rect(1, 2, 10, 7);

            runtime.Engine.Execute("""
                batchProbeCanvas.width = 240;
                batchProbeContext.fillRect(0, 0, 5, 5);
                __webSceneFlushCanvases();
                """);
            var resizePassed = surface.Commands.Count == 1
                               && surface.Commands[0].GetType().Name == "FillRectCommand";
            runtime.Execute("""
                // Each beginPath command occupies two values in the 16,384-value batch.
                // Put a string-bearing command just beyond the packet boundary. The string
                // must be interned after the capacity flush so its index belongs to the new packet.
                for (let index = 0; index < 8191; index++) batchProbeContext.beginPath();
                batchProbeContext.fillStyle = '#123456';
                __webSceneFlushCanvases();

                for (let index = 0; index < 8190; index++) batchProbeContext.beginPath();
                batchProbeContext.fillText('packet-boundary', 1, 1);
                __webSceneFlushCanvases();
                globalThis.batchProbeStringBoundaryPassed = true;
                """, "v8-canvas-string-packet-boundary.js");
            var stringBoundaryPassed = Convert.ToBoolean(
                runtime.Engine.Script.batchProbeStringBoundaryPassed);
            var resizeObserverPassed = ProbeResizeObserverSemantics(runtime, host);
            runtime.Engine.Execute("""
                globalThis.runCachedMethodLookupProbe = function(iterations) {
                    let matches = 0;
                    for (let index = 0; index < iterations; index++) {
                        if (batchProbeContext.fillRect === batchProbeContext.fillRect) matches++;
                        if (batchProbeCanvas.getContext === batchProbeCanvas.getContext) matches++;
                    }
                    return matches;
                };
                globalThis.runDomRectProbe = function(iterations) {
                    let total = 0;
                    for (let index = 0; index < iterations; index++) {
                        const rect = batchProbeCanvas.getBoundingClientRect();
                        total += rect.x + rect.y + rect.width + rect.height +
                            rect.left + rect.top + rect.right + rect.bottom;
                    }
                    return total;
                };
                globalThis.runTextMetricsProbe = function(iterations) {
                    let total = 0;
                    for (let index = 0; index < iterations; index++) {
                        total += batchProbeContext.measureText('WebScene ' + (index % 10)).width;
                    }
                    return total;
                };
                globalThis.runWrappedResultProbe = function(iterations) {
                    let matches = 0;
                    const expectedParent = batchProbeCanvas.parentElement;
                    const expectedStyle = batchProbeCanvas.style;
                    for (let index = 0; index < iterations; index++) {
                        if (batchProbeCanvas.parentElement === expectedParent) matches++;
                        if (batchProbeCanvas.style === expectedStyle) matches++;
                    }
                    return matches;
                };
                globalThis.runMissingPropertyProbe = function(iterations) {
                    let matches = 0;
                    for (let index = 0; index < iterations; index++) {
                        if (batchProbeCanvas.__webSceneAbsentOptionalProperty === undefined) matches++;
                    }
                    return matches;
                };
                runCachedMethodLookupProbe(1000);
                runDomRectProbe(100);
                runTextMetricsProbe(20);
                runWrappedResultProbe(100);
                runMissingPropertyProbe(100);
                """);
            const int methodLookupIterations = 20_000;
            var methodLookupAllocationStart = GC.GetAllocatedBytesForCurrentThread();
            var methodLookupStarted = Stopwatch.StartNew();
            var methodLookupMatches = Convert.ToInt32(
                runtime.Engine.Script.runCachedMethodLookupProbe(methodLookupIterations));
            methodLookupStarted.Stop();
            var methodLookupAllocatedBytes =
                GC.GetAllocatedBytesForCurrentThread() - methodLookupAllocationStart;
            const int domRectIterations = 5_000;
            var domRectAllocationStart = GC.GetAllocatedBytesForCurrentThread();
            var domRectStarted = Stopwatch.StartNew();
            var domRectTotal = Convert.ToDouble(runtime.Engine.Script.runDomRectProbe(domRectIterations));
            domRectStarted.Stop();
            var domRectAllocatedBytes = GC.GetAllocatedBytesForCurrentThread() - domRectAllocationStart;
            const int textMetricsIterations = 2_000;
            var textMetricsAllocationStart = GC.GetAllocatedBytesForCurrentThread();
            var textMetricsStarted = Stopwatch.StartNew();
            var textMetricsTotal = Convert.ToDouble(
                runtime.Engine.Script.runTextMetricsProbe(textMetricsIterations));
            textMetricsStarted.Stop();
            var textMetricsAllocatedBytes =
                GC.GetAllocatedBytesForCurrentThread() - textMetricsAllocationStart;
            const int wrappedResultIterations = 20_000;
            var wrappedResultAllocationStart = GC.GetAllocatedBytesForCurrentThread();
            var wrappedResultStarted = Stopwatch.StartNew();
            var wrappedResultMatches = Convert.ToInt32(
                runtime.Engine.Script.runWrappedResultProbe(wrappedResultIterations));
            wrappedResultStarted.Stop();
            var wrappedResultAllocatedBytes =
                GC.GetAllocatedBytesForCurrentThread() - wrappedResultAllocationStart;
            const int missingPropertyIterations = 20_000;
            var missingPropertyAllocationStart = GC.GetAllocatedBytesForCurrentThread();
            var missingPropertyStarted = Stopwatch.StartNew();
            var missingPropertyMatches = Convert.ToInt32(
                runtime.Engine.Script.runMissingPropertyProbe(missingPropertyIterations));
            missingPropertyStarted.Stop();
            var missingPropertyAllocatedBytes =
                GC.GetAllocatedBytesForCurrentThread() - missingPropertyAllocationStart;
            var passed = initialPassed
                         && resizePassed
                         && stringBoundaryPassed
                         && resizeObserverPassed
                         && identityReadProbe.ReadCount == 1;
            Console.WriteLine(
                $"Batched V8 runtime Canvas2D semantics: {(passed ? "pass" : "fail")}; " +
                $"initial=[{string.Join(',', initialKinds)}], after-resize={surface.Commands.Count}, " +
                $"string-packet-boundary={(stringBoundaryPassed ? "pass" : "fail")}, " +
                $"dom-parser={runtime.Engine.Script.batchProbeDomParserDetails}, " +
                runtime.DescribeCanvasBatching());
            Console.WriteLine(
                $"V8 new non-DOM host identity reads: " +
                $"{(identityReadProbe.ReadCount == 1 ? "pass" : "fail")}; " +
                $"reads={identityReadProbe.ReadCount}");
            Console.WriteLine(
                $"V8 cached DOM/Canvas method lookup ({methodLookupIterations * 4:N0} reads): " +
                $"{methodLookupStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                $"{methodLookupAllocatedBytes / 1024d:F1} KB managed, " +
                $"identity={(methodLookupMatches == methodLookupIterations * 2 ? "pass" : "fail")}");
            Console.WriteLine(
                $"V8 DOMRect bridge ({domRectIterations:N0} rectangles / {domRectIterations * 8:N0} field reads): " +
                $"{domRectStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                $"{domRectAllocatedBytes / 1024d:F1} KB managed, " +
                $"finite={(double.IsFinite(domRectTotal) ? "pass" : "fail")}");
            Console.WriteLine(
                $"V8 TextMetrics bridge ({textMetricsIterations:N0} measureText/width reads): " +
                $"{textMetricsStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                $"{textMetricsAllocatedBytes / 1024d:F1} KB managed, " +
                $"finite={(double.IsFinite(textMetricsTotal) && textMetricsTotal > 0 ? "pass" : "fail")}");
            Console.WriteLine(
                $"V8 wrapped-result classification ({wrappedResultIterations * 2:N0} reads): " +
                $"{wrappedResultStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                $"{wrappedResultAllocatedBytes / 1024d:F1} KB managed, " +
                $"identity={(wrappedResultMatches == wrappedResultIterations * 2 ? "pass" : "fail")}");
            Console.WriteLine(
                $"V8 missing DOM property lookup ({missingPropertyIterations:N0} reads): " +
                $"{missingPropertyStarted.Elapsed.TotalMilliseconds:F1} ms, " +
                $"{missingPropertyAllocatedBytes / 1024d:F1} KB managed, " +
                $"semantics={(missingPropertyMatches == missingPropertyIterations ? "pass" : "fail")}");
            return passed;
        }
        catch (Exception exception)
        {
            Console.WriteLine(
                $"Batched V8 runtime Canvas2D semantics: fail; " +
                $"{exception.GetType().Name}: {exception.Message}");
            return false;
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool ProbeResizeObserverSemantics(
        ClearScriptV8Runtime runtime,
        AvaloniaBrowserHost host)
    {
        runtime.Execute("""
            globalThis.resizeObserverProbeTarget = document.createElement('div');
            document.body.appendChild(resizeObserverProbeTarget);
            globalThis.resizeObserverProbeState = {
                phase: 'before',
                synchronous: false,
                calls: 0,
                entries: 0,
                sizes: [],
                entryShape: false
            };
            globalThis.resizeObserverProbe = new ResizeObserver(function(entries, observer) {
                if (resizeObserverProbeState.phase === 'observing') {
                    resizeObserverProbeState.synchronous = true;
                }
                resizeObserverProbeState.calls++;
                resizeObserverProbeState.entries += entries.length;
                for (const entry of entries) {
                    resizeObserverProbeState.sizes.push(
                        entry.contentRect.width + 'x' + entry.contentRect.height);
                    resizeObserverProbeState.entryShape =
                        entry instanceof ResizeObserverEntry &&
                        entry.target === resizeObserverProbeTarget &&
                        Array.isArray(entry.contentBoxSize) &&
                        typeof entry.contentBoxSize[0].inlineSize === 'number' &&
                        typeof entry.contentBoxSize[0].blockSize === 'number' &&
                        'devicePixelContentBoxSize' in ResizeObserverEntry.prototype;
                }
            });
            resizeObserverProbeState.phase = 'observing';
            resizeObserverProbe.observe(resizeObserverProbeTarget);
            resizeObserverProbeState.phase = 'after';
            """, "v8-resize-observer-probe.js");

        var target = (AvaloniaDomElement?)host.Document.querySelector("div")
                     ?? throw new InvalidOperationException("ResizeObserver probe target was not created.");
        Dispatcher.UIThread.RunJobs();
        var initialCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));

        target.Control.Width = 160;
        target.Control.Height = 90;
        target.Control.InvalidateMeasure();
        Dispatcher.UIThread.RunJobs();
        var resizedCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));
        var resizedSize = Convert.ToString(runtime.Engine.Evaluate(
            "resizeObserverProbeState.sizes[resizeObserverProbeState.sizes.length - 1]"));

        target.Control.Width = 160;
        target.Control.Height = 90;
        target.Control.InvalidateMeasure();
        Dispatcher.UIThread.RunJobs();
        var equalSizeCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));

        runtime.Execute("resizeObserverProbe.unobserve(resizeObserverProbeTarget);", "v8-resize-unobserve.js");
        target.Control.Width = 180;
        target.Control.Height = 100;
        target.Control.InvalidateMeasure();
        Dispatcher.UIThread.RunJobs();
        var unobservedCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));

        runtime.Execute("resizeObserverProbe.observe(resizeObserverProbeTarget);", "v8-resize-reobserve.js");
        Dispatcher.UIThread.RunJobs();
        var reobservedCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));
        var reobservedSize = Convert.ToString(runtime.Engine.Evaluate(
            "resizeObserverProbeState.sizes[resizeObserverProbeState.sizes.length - 1]"));

        runtime.Execute("resizeObserverProbe.disconnect();", "v8-resize-disconnect.js");
        target.Control.Width = 200;
        target.Control.Height = 110;
        target.Control.InvalidateMeasure();
        Dispatcher.UIThread.RunJobs();
        var disconnectedCalls = Convert.ToInt32(runtime.Engine.Evaluate("resizeObserverProbeState.calls"));

        var synchronous = Convert.ToBoolean(runtime.Engine.Evaluate("resizeObserverProbeState.synchronous"));
        var entryShape = Convert.ToBoolean(runtime.Engine.Evaluate("resizeObserverProbeState.entryShape"));
        var passed = !synchronous
                     && entryShape
                     && initialCalls == 1
                     && resizedCalls == initialCalls + 1
                     && string.Equals(resizedSize, "160x90", StringComparison.Ordinal)
                     && equalSizeCalls == resizedCalls
                     && unobservedCalls == equalSizeCalls
                     && reobservedCalls == unobservedCalls + 1
                     && string.Equals(reobservedSize, "180x100", StringComparison.Ordinal)
                     && disconnectedCalls == reobservedCalls;
        Console.WriteLine(
            $"V8 ResizeObserver repeated delivery/lifecycle: {(passed ? "pass" : "fail")}; " +
            $"sync={synchronous}, entry-shape={entryShape}, calls=" +
            $"{initialCalls}/{resizedCalls}/{equalSizeCalls}/{unobservedCalls}/" +
            $"{reobservedCalls}/{disconnectedCalls}, sizes={resizedSize}/{reobservedSize}");
        return passed;
    }

    public sealed class NestedV8InvocationBridge
    {
        private readonly ScriptObject _frameEntry;

        internal NestedV8InvocationBridge(V8ScriptEngine frame)
        {
            _frameEntry = (ScriptObject)frame.Script.frameEntry;
        }

        public void InvokeFrame() => _frameEntry.InvokeAsFunction();
    }

    private static AvaloniaDomElement CreateEventElement(AvaloniaBrowserHost host)
    {
        var element = (AvaloniaDomElement?)host.Document.createElement("div")
                      ?? throw new InvalidOperationException("Unable to create the event-probe element.");
        if (host.Document.body is AvaloniaDomElement body)
        {
            body.appendChild(element);
        }
        return element;
    }

    private static (TimeSpan Elapsed, long AllocatedBytes) MeasurePointerMoves(
        AvaloniaDomElement element,
        Pointer pointer,
        int eventCount)
    {
        var allocationStarted = GC.GetAllocatedBytesForCurrentThread();
        var started = Stopwatch.StartNew();
        for (var index = 0; index < eventCount; index++)
        {
            element.Control.RaiseEvent(new PointerEventArgs(
                InputElement.PointerMovedEvent,
                element.Control,
                pointer,
                element.Control,
                new Point(index % 120, index % 80),
                (ulong)index,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None));
        }
        started.Stop();
        return (started.Elapsed, GC.GetAllocatedBytesForCurrentThread() - allocationStarted);
    }

    private static void ReplayManagedHotCanvas(CanvasRenderingContext2D context)
    {
        context.clearRect(0, 0, 120, 80);
        for (var index = 0; index < 500; index++)
        {
            context.fillStyle = (index & 1) == 0 ? "#ef5350" : "#42a5f5";
            context.fillRect(index % 120, index * 3 % 80, 1, 1);
        }
    }

    public sealed class V8CanvasBatchSink
    {
        private const int CommandWidth = 6;
        private readonly CanvasRenderingContext2D _context;

        public V8CanvasBatchSink(CanvasRenderingContext2D context)
        {
            _context = context;
        }

        public int FlushCount { get; private set; }

        public int ReplayedCommandCount { get; private set; }

        public void Flush(ITypedArray<double> source, int commandCount)
        {
            if (commandCount <= 0)
            {
                return;
            }

            var valueCount = checked(commandCount * CommandWidth);
            var values = ArrayPool<double>.Shared.Rent(valueCount);
            try
            {
                var read = source.Read(0, (ulong)valueCount, values, 0);
                if (read != (ulong)valueCount)
                {
                    throw new InvalidOperationException($"Expected {valueCount} batch values but received {read}.");
                }

                for (var index = 0; index < valueCount; index += CommandWidth)
                {
                    switch ((int)values[index])
                    {
                        case 0:
                            _context.clearRect(values[index + 1], values[index + 2], values[index + 3], values[index + 4]);
                            break;
                        case 1:
                            _context.fillStyle = values[index + 5] == 1 ? "#42a5f5" : "#ef5350";
                            _context.fillRect(values[index + 1], values[index + 2], values[index + 3], values[index + 4]);
                            break;
                        default:
                            throw new InvalidOperationException($"Unknown canvas batch opcode {values[index]}.");
                    }
                }

                FlushCount++;
                ReplayedCommandCount += commandCount;
            }
            finally
            {
                ArrayPool<double>.Shared.Return(values);
            }
        }
    }

    public sealed class V8ExternalEventListener : IExternalDomEventListener, IExternalJavaScriptCallback
    {
        private readonly ScriptObject _eventApplyCallback;
        private readonly ScriptObject _applyCallback;
        private readonly ScriptObject _callback;

        public V8ExternalEventListener(
            ScriptObject eventApplyCallback,
            ScriptObject applyCallback,
            ScriptObject callback)
        {
            _eventApplyCallback = eventApplyCallback;
            _applyCallback = applyCallback;
            _callback = callback;
        }

        public int InvokeCount { get; private set; }

        public void Invoke(object currentTarget, object domEvent)
        {
            _eventApplyCallback.InvokeAsFunction(_callback, currentTarget, domEvent);
            InvokeCount++;
        }

        public void Invoke(object? thisValue, params object?[] arguments)
            => InvokeCore(thisValue, arguments);

        private void InvokeCore(object? thisValue, object?[] arguments)
        {
            _applyCallback.InvokeAsFunction(_callback, thisValue, arguments);
            InvokeCount++;
        }
    }

    public sealed class DomIdentityReadProbe
    {
        public int ReadCount { get; private set; }

        public object? __webSceneDomIdentity
        {
            get
            {
                ReadCount++;
                return null;
            }
        }
    }

    public sealed class V8ExternalEventListenerAdapter :
        IExternalDomEventListenerAdapter,
        IExternalJavaScriptCallbackAdapter
    {
        private readonly ScriptObject _eventApplyCallback;
        private readonly ScriptObject _applyCallback;
        private readonly Dictionary<ScriptObject, V8ExternalEventListener> _listeners = new();

        public V8ExternalEventListenerAdapter(
            ScriptObject eventApplyCallback,
            ScriptObject applyCallback)
        {
            _eventApplyCallback = eventApplyCallback;
            _applyCallback = applyCallback;
        }

        public IExternalDomEventListener? GetEventListener(object callback, bool create)
        {
            if (callback is not ScriptObject scriptCallback)
            {
                return null;
            }

            if (_listeners.TryGetValue(scriptCallback, out var listener) || !create)
            {
                return listener;
            }

            listener = new V8ExternalEventListener(
                _eventApplyCallback,
                _applyCallback,
                scriptCallback);
            _listeners.Add(scriptCallback, listener);
            return listener;
        }

        public IExternalJavaScriptCallback? GetCallback(object callback, bool create)
            => GetEventListener(callback, create) as IExternalJavaScriptCallback;

        public ExternalDomEventListenerOptions GetEventListenerOptions(object? options)
        {
            if (options is bool capture)
            {
                return new ExternalDomEventListenerOptions(capture, Once: false, Passive: false);
            }

            if (options is not ScriptObject scriptOptions)
            {
                return default;
            }

            return new ExternalDomEventListenerOptions(
                ReadBoolean(scriptOptions, "capture"),
                ReadBoolean(scriptOptions, "once"),
                ReadBoolean(scriptOptions, "passive"));
        }

        private static bool ReadBoolean(ScriptObject options, string name)
            => options.GetProperty(name) is bool value && value;

    }
}

using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using WebScene.Backends.Avalonia.Native;

namespace WebScene.NativeEngine.Benchmarks;

internal static class NativeResizeCadenceProbe
{
    private const string Fixture = """
        <!doctype html>
        <meta charset="utf-8">
        <style>
          * { box-sizing: border-box }
          html, body { width: 100%; height: 100%; margin: 0; overflow: hidden; font: 12px sans-serif }
          #shell { display: grid; grid-template-rows: 38px 1fr 24px; width: 100vw; height: 100vh }
          #toolbar { display: flex; gap: 6px; padding: 5px; border-bottom: 1px solid #333 }
          #workspace { display: grid; grid-template-columns: minmax(180px, 1fr) 3fr; min-height: 0 }
          #watchlist { overflow: hidden; border-right: 1px solid #333 }
          #chart { position: relative; overflow: hidden; min-width: 0 }
          .row { display: grid; grid-template-columns: 1fr 70px 70px; height: 22px; padding: 2px 5px }
          .row:nth-child(odd) { background: #17191d }
          .pane { position: absolute; inset: 8px; display: flex; flex-direction: column }
          .plot { flex: 1; min-height: 0; border: 1px solid #444 }
          #status { padding: 4px 8px; border-top: 1px solid #333 }
        </style>
        <div id="shell">
          <div id="toolbar"></div>
          <div id="workspace"><div id="watchlist"></div><div id="chart"><div class="pane"><div class="plot"></div></div></div></div>
          <div id="status"></div>
        </div>
        <script>
          const toolbar = document.querySelector('#toolbar');
          const watchlist = document.querySelector('#watchlist');
          for (let i = 0; i < 24; i++) {
            const button = document.createElement('button');
            button.textContent = `Tool ${i}`;
            toolbar.append(button);
          }
          for (let i = 0; i < 180; i++) {
            const row = document.createElement('div');
            row.className = 'row';
            row.innerHTML = `<span>SYMBOL-${i}</span><span>${(100 + i / 7).toFixed(2)}</span><span>${i % 2 ? '+' : '-'}0.12%</span>`;
            watchlist.append(row);
          }
          const pane = document.querySelector('.pane');
          const plot = document.querySelector('.plot');
          const status = document.querySelector('#status');
          addEventListener('resize', () => {
            pane.style.height = `${Math.max(1, innerHeight - 86)}px`;
            const width = plot.clientWidth;
            const height = plot.getBoundingClientRect().height;
            status.textContent = `${width} x ${height}`;
            requestAnimationFrame(() => {
              plot.style.transform = `translate(${width % 2}px, ${height % 2}px)`;
              void plot.offsetHeight;
            });
          });
        </script>
        """;

    internal static int Run(string[] args)
    {
        var library = Environment.GetEnvironmentVariable("WEBSCENE_NATIVE_ENGINE_PATH");
        if (string.IsNullOrWhiteSpace(library))
        {
            throw new InvalidOperationException(
                "Set WEBSCENE_NATIVE_ENGINE_PATH to the native V8 library.");
        }

        var seconds = ReadDoubleOption(args, "--seconds", 10);
        var warmupSeconds = ReadDoubleOption(args, "--warmup-seconds", 2);
        var frequency = ReadDoubleOption(args, "--hz", 60);
        var baseWidth = ReadDoubleOption(args, "--width", 1180);
        var baseHeight = ReadDoubleOption(args, "--height", 720);
        var widthSpan = ReadIntOption(args, "--width-span", 24);
        var heightSpan = ReadIntOption(args, "--height-span", 30);
        var composition = HasOption(args, "--composition");
        var enforce = HasOption(args, "--enforce");
        var enforceChromeReference = HasOption(
            args,
            "--enforce-chrome-reference");
        if (seconds <= 0 || warmupSeconds < 0 || frequency <= 0
            || baseWidth <= 1 || baseHeight <= 1 || widthSpan < 1 || heightSpan < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(args), "Resize probe options are invalid.");
        }

        BenchmarkApp.EnsureInitialized();
        NativeWebSceneApi.ConfigureLibraryPath(library);
        var temporaryDirectory = Path.Combine(
            Path.GetTempPath(), "webscene-native-resize-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryDirectory);
        var fixturePath = Path.Combine(temporaryDirectory, "index.html");
        File.WriteAllText(fixturePath, Fixture);
        var source = ReadOption(args, "--url") ?? new Uri(fixturePath).AbsoluteUri;
        var chromeReference = ReadChromeReference(
            ReadOption(args, "--chrome-reference"),
            source,
            frequency,
            seconds);
        if (enforceChromeReference && chromeReference is null)
        {
            throw new InvalidOperationException(
                "--enforce-chrome-reference requires --chrome-reference <JSON>.");
        }
        var view = new NativeWebSceneView(useCompositionVisual: composition);
        var window = new Window
        {
            Width = baseWidth,
            Height = baseHeight,
            Content = view
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            Pump(view.LoadAsync(source, library));
            Dispatcher.UIThread.RunJobs();
            var surface = (NativeSceneSurface)view.Content!;
            RunCadence(window, warmupSeconds, frequency, baseWidth, baseHeight, widthSpan, heightSpan);
            WaitForResizeDrain(view, TimeSpan.FromSeconds(2));

            var baseline = view.CapturePerformanceSnapshot();
            var submittedBaseline = surface.SubmittedResizes.Length;
            var publishedBaseline = surface.PublishedScenes.Length;
            var renderedBaseline = surface.RenderedScenes.Length;
            var presentationBaseline = surface.PresentationTimestamps.Length;
            var process = Process.GetCurrentProcess();
            process.Refresh();
            var cpuBefore = process.TotalProcessorTime;
            var measurementStarted = Stopwatch.GetTimestamp();
            var submitted = RunCadence(
                window, seconds, frequency, baseWidth, baseHeight, widthSpan, heightSpan);
            var measurementElapsed = Stopwatch.GetElapsedTime(measurementStarted);
            WaitForResizeDrain(view, TimeSpan.FromSeconds(3));
            process.Refresh();
            var cpu = process.TotalProcessorTime - cpuBefore;
            var snapshot = view.CapturePerformanceSnapshot();
            var delta = snapshot.Since(baseline);

            var submissions = surface.SubmittedResizes.Skip(submittedBaseline).ToArray();
            var publications = surface.PublishedScenes.Skip(publishedBaseline).ToArray();
            var renders = surface.RenderedScenes.Skip(renderedBaseline).ToArray();
            var presentations = surface.PresentationTimestamps
                .Skip(presentationBaseline)
                .ToArray();
            var publicationLatencies = MatchLatencies(
                submissions,
                publications.Select(static value =>
                    (value.Timestamp, value.ConsumedInputSequence)).ToArray());
            var renderLatencies = MatchLatencies(
                submissions,
                renders.Select(static value =>
                    (value.Timestamp, value.ConsumedInputSequence)).ToArray());
            var publicationToRenderLatencies = MatchSceneLatencies(publications, renders);
            var renderIntervals = renders
                .Zip(renders.Skip(1), static (left, right) =>
                    (right.Timestamp - left.Timestamp) * 1000d / Stopwatch.Frequency)
                .Where(static value => value >= 0)
                .ToArray();
            var presentationIntervals = presentations
                .Zip(presentations.Skip(1), static (left, right) =>
                    (right - left) * 1000d / Stopwatch.Frequency)
                .Where(static value => value >= 0)
                .ToArray();
            var renderedFps = delta.RenderedScenes / measurementElapsed.TotalSeconds;
            var presentationFps = presentations.Length > 1
                ? (presentations.Length - 1) * 1000d
                    / ((presentations[^1] - presentations[0]) * 1000d
                        / Stopwatch.Frequency)
                : 0;
            var renderP95 = Percentile(renderLatencies, 0.95);
            var presentationP95 = Percentile(presentationIntervals, 0.95);
            var maximumInterval = renderIntervals.Length == 0 ? double.PositiveInfinity : renderIntervals.Max();
            var compositionTiming = NativeSceneSurface.LastCompositionTiming;
            var certificationDiagnostics = view.SceneDiagnostics;
            var passed = renderP95 <= 16.7
                && renderedFps >= 58
                && maximumInterval <= 33.4
                && delta.DroppedInputs == 0;
            var chromeReferencePassed = chromeReference is null
                ? (bool?)null
                : presentationFps >= chromeReference.Value.FramesPerSecond
                    && presentationP95
                        <= chromeReference.Value.P95IntervalMilliseconds
                    && delta.DroppedInputs == 0;

            var json = JsonSerializer.Serialize(
                new
                {
                    schema = "webscene-native-resize-cadence-v1",
                    sourceKind = ReadOption(args, "--url") is null ? "deterministic-fixture" : "url",
                    composition,
                    certificationTelemetryEnabled = !certificationDiagnostics.StartsWith(
                        "certification telemetry disabled",
                        StringComparison.Ordinal),
                    requestedHz = frequency,
                    warmupSeconds,
                    requestedSeconds = seconds,
                    elapsedMilliseconds = measurementElapsed.TotalMilliseconds,
                    submitted,
                    acceptedSubmissions = submissions.Length,
                    appliedPairs = Difference(
                        snapshot.ResizeFrames.AppliedPairs,
                        baseline.ResizeFrames.AppliedPairs),
                    publishedPairs = Difference(
                        snapshot.ResizeFrames.PublishedPairs,
                        baseline.ResizeFrames.PublishedPairs),
                    coalescedPairs = Difference(
                        snapshot.ResizeFrames.SubmittedPairs - snapshot.ResizeFrames.AppliedPairs,
                        baseline.ResizeFrames.SubmittedPairs - baseline.ResizeFrames.AppliedPairs),
                    renderedFrames = delta.RenderedScenes,
                    presentations = presentations.Length,
                    renderedFramesPerSecond = renderedFps,
                    presentationFramesPerSecond = presentationFps,
                    layoutPasses = delta.LayoutPasses,
                    layoutPassesPerAppliedResize = delta.LayoutPasses / (double)Math.Max(
                        1UL,
                        Difference(snapshot.ResizeFrames.AppliedPairs, baseline.ResizeFrames.AppliedPairs)),
                    publishedScenes = delta.PublishedScenes,
                    publicationAttempts = delta.PublicationAttempts,
                    blockedPublications = delta.BlockedPublications,
                    fullInvalidations = delta.CompositionFullInvalidations,
                    unchangedRenderCallbacks = delta.CompositionUnchangedRenderCallbacks,
                    droppedInputs = delta.DroppedInputs,
                    processCpuMilliseconds = cpu.TotalMilliseconds,
                    normalizedProcessCpuPercent =
                        cpu.TotalMilliseconds / measurementElapsed.TotalMilliseconds * 100d,
                    lastCompositionMilliseconds = new
                    {
                        diffApply = compositionTiming.DiffApplyMilliseconds,
                        retainedDraw = compositionTiming.RetainedDrawMilliseconds,
                        skiaSubmit = compositionTiming.SkiaSubmitMilliseconds,
                        callback = compositionTiming.RenderCallbackMilliseconds
                    },
                    lastResizeStageMilliseconds = new
                    {
                        outerListeners = snapshot.Engine
                            .LastResizeOuterListenersNanoseconds / 1_000_000d,
                        frameListeners = snapshot.Engine
                            .LastResizeFrameListenersNanoseconds / 1_000_000d,
                        finalLayout = snapshot.Engine
                            .LastResizeLayoutNanoseconds / 1_000_000d,
                        observers = snapshot.Engine
                            .LastResizeObserversNanoseconds / 1_000_000d,
                        totalDispatch = snapshot.Engine
                            .LastResizeDispatchNanoseconds / 1_000_000d,
                        scenePublication = snapshot.Engine
                            .LastScenePublicationNanoseconds / 1_000_000d
                    },
                    queueMilliseconds = Distribution(
                        Difference(snapshot.ResizeFrames.TotalQueueNanoseconds, baseline.ResizeFrames.TotalQueueNanoseconds),
                        Difference(snapshot.ResizeFrames.AppliedPairs, baseline.ResizeFrames.AppliedPairs),
                        snapshot.ResizeFrames.MaximumQueueNanoseconds),
                    dispatchMilliseconds = Distribution(
                        Difference(snapshot.ResizeFrames.TotalDispatchNanoseconds, baseline.ResizeFrames.TotalDispatchNanoseconds),
                        Difference(snapshot.ResizeFrames.AppliedPairs, baseline.ResizeFrames.AppliedPairs),
                        snapshot.ResizeFrames.MaximumDispatchNanoseconds),
                    publicationLatencyMilliseconds = Summary(publicationLatencies),
                    publicationToRenderLatencyMilliseconds = Summary(
                        publicationToRenderLatencies),
                    renderLatencyMilliseconds = Summary(renderLatencies),
                    renderIntervalMilliseconds = Summary(renderIntervals),
                    presentationIntervalMilliseconds = Summary(presentationIntervals),
                    practicalVsyncGate = new
                    {
                        maximumP95LatencyMilliseconds = 16.7,
                        minimumFramesPerSecond = 58,
                        maximumConsecutiveMissIntervalMilliseconds = 33.4,
                        passed
                    },
                    chromeReferenceComparison = chromeReference is { } reference
                        ? new
                        {
                            identity = reference.Identity,
                            referenceFramesPerSecond = reference.FramesPerSecond,
                            nativeFramesPerSecond = presentationFps,
                            framesPerSecondDelta =
                                presentationFps - reference.FramesPerSecond,
                            referenceP95IntervalMilliseconds =
                                reference.P95IntervalMilliseconds,
                            nativeP95IntervalMilliseconds = presentationP95,
                            p95IntervalDeltaMilliseconds =
                                presentationP95
                                    - reference.P95IntervalMilliseconds,
                            passed = chromeReferencePassed
                        }
                        : null,
                    certificationDiagnostics
                },
                new JsonSerializerOptions { WriteIndented = true });
            Console.WriteLine(json);
            if (ReadOption(args, "--output") is { } output)
            {
                var outputPath = Path.GetFullPath(output);
                Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
                File.WriteAllText(outputPath, json);
            }
            return (enforce && !passed)
                || (enforceChromeReference && chromeReferencePassed != true)
                ? 1
                : 0;
        }
        finally
        {
            Pump(view.DisposeAsync().AsTask());
            window.Close();
            Dispatcher.UIThread.RunJobs();
            try
            {
                Directory.Delete(temporaryDirectory, recursive: true);
            }
            catch
            {
            }
        }
    }

    private readonly record struct ChromeReference(
        string Identity,
        double FramesPerSecond,
        double P95IntervalMilliseconds);

    private static ChromeReference? ReadChromeReference(
        string? path,
        string source,
        double requestedHz,
        double requestedSeconds)
    {
        if (path is null)
        {
            return null;
        }

        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        if (root.GetProperty("schema").GetString()
            != "webscene-chrome-resize-cadence-v1")
        {
            throw new InvalidOperationException(
                $"Chrome reference '{path}' has an unsupported schema.");
        }
        var referenceUrl = root.GetProperty("url").GetString();
        var referenceHz = root.GetProperty("requestedHz").GetDouble();
        var referenceSeconds = root.GetProperty("requestedSeconds").GetDouble();
        if (!string.Equals(referenceUrl, source, StringComparison.Ordinal)
            || Math.Abs(referenceHz - requestedHz) > 0.001
            || Math.Abs(referenceSeconds - requestedSeconds) > 0.001)
        {
            throw new InvalidOperationException(
                "Chrome reference URL, cadence, and duration must match the native probe.");
        }
        return new ChromeReference(
            root.GetProperty("identity").GetString() ?? "Chrome",
            root.GetProperty("renderedFramesPerSecond").GetDouble(),
            root.GetProperty("animationFrameIntervalMilliseconds")
                .GetProperty("p95")
                .GetDouble());
    }

    private static int RunCadence(
        Window window,
        double seconds,
        double frequency,
        double baseWidth,
        double baseHeight,
        int widthSpan,
        int heightSpan)
    {
        var frameCount = (int)Math.Round(seconds * frequency);
        var started = Stopwatch.GetTimestamp();
        for (var index = 0; index < frameCount; index++)
        {
            var deadline = started + (long)((index + 1) * Stopwatch.Frequency / frequency);
            while (Stopwatch.GetTimestamp() < deadline)
            {
                Dispatcher.UIThread.RunJobs();
                var remaining = deadline - Stopwatch.GetTimestamp();
                if (remaining > Stopwatch.Frequency / 500)
                {
                    Thread.Sleep(1);
                }
                else
                {
                    Thread.SpinWait(32);
                }
            }
            window.Width = baseWidth + index % widthSpan;
            window.Height = baseHeight + index % heightSpan;
            Dispatcher.UIThread.RunJobs();
        }
        return frameCount;
    }

    private static void WaitForResizeDrain(
        NativeWebSceneView view,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        var stable = 0;
        var previous = view.CapturePerformanceSnapshot().ResizeFrames.PublishedPairs;
        while (DateTime.UtcNow < deadline && stable < 5)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(5);
            var current = view.CapturePerformanceSnapshot().ResizeFrames.PublishedPairs;
            stable = current == previous ? stable + 1 : 0;
            previous = current;
        }
    }

    private static double[] MatchLatencies(
        NativeResizeSubmissionSample[] submissions,
        (long Timestamp, ulong ConsumedSequence)[] completions)
    {
        var result = new List<double>(submissions.Length);
        var completionIndex = 0;
        foreach (var submission in submissions)
        {
            while (completionIndex < completions.Length
                && completions[completionIndex].ConsumedSequence < submission.Sequence)
            {
                completionIndex++;
            }
            if (completionIndex >= completions.Length)
            {
                break;
            }
            var elapsed = completions[completionIndex].Timestamp - submission.Timestamp;
            if (elapsed >= 0)
            {
                result.Add(elapsed * 1000d / Stopwatch.Frequency);
            }
        }
        return result.ToArray();
    }

    private static double[] MatchSceneLatencies(
        NativeScenePublicationSample[] publications,
        NativeSceneRenderSample[] renders)
    {
        var renderTimestamps = renders
            .GroupBy(static sample => sample.Revision)
            .ToDictionary(
                static group => group.Key,
                static group => group.Min(sample => sample.Timestamp));
        return publications
            .Where(publication => renderTimestamps.ContainsKey(publication.Revision))
            .Select(publication =>
                (renderTimestamps[publication.Revision] - publication.Timestamp)
                    * 1000d / Stopwatch.Frequency)
            .Where(static elapsed => elapsed >= 0)
            .ToArray();
    }

    private static object Summary(double[] values)
        => values.Length == 0
            ? new { count = 0, average = 0d, p50 = 0d, p95 = 0d, maximum = 0d }
            : new
            {
                count = values.Length,
                average = values.Average(),
                p50 = Percentile(values, 0.50),
                p95 = Percentile(values, 0.95),
                maximum = values.Max()
            };

    private static object Distribution(ulong totalNanoseconds, ulong count, ulong maximumNanoseconds)
        => new
        {
            average = count == 0 ? 0 : totalNanoseconds / (double)count / 1_000_000d,
            maximum = maximumNanoseconds / 1_000_000d
        };

    private static double Percentile(double[] values, double percentile)
    {
        if (values.Length == 0)
        {
            return double.PositiveInfinity;
        }
        var ordered = values.Order().ToArray();
        var index = Math.Clamp(
            (int)Math.Ceiling(percentile * ordered.Length) - 1,
            0,
            ordered.Length - 1);
        return ordered[index];
    }

    private static ulong Difference(ulong current, ulong baseline)
        => current >= baseline ? current - baseline : 0;

    private static bool HasOption(IReadOnlyList<string> args, string name)
        => args.Any(value => string.Equals(value, name, StringComparison.OrdinalIgnoreCase));

    private static string? ReadOption(IReadOnlyList<string> args, string name)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase))
            {
                return args[index + 1];
            }
        }
        return null;
    }

    private static int ReadIntOption(IReadOnlyList<string> args, string name, int fallback)
        => int.TryParse(ReadOption(args, name), out var value) ? value : fallback;

    private static double ReadDoubleOption(IReadOnlyList<string> args, string name, double fallback)
        => double.TryParse(
            ReadOption(args, name),
            System.Globalization.NumberStyles.Float,
            System.Globalization.CultureInfo.InvariantCulture,
            out var value)
            ? value
            : fallback;

    private static void Pump(Task task)
    {
        while (!task.IsCompleted)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(1);
        }
        task.GetAwaiter().GetResult();
    }
}

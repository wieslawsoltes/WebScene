using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;
using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Native;

namespace NativeTradingViewTerminal;

public sealed partial class MainWindow : Window
{
    private readonly IReadOnlyList<string> _arguments;
    private readonly DispatcherTimer _diagnosticsTimer;
    private AvaloniaResourceLoader? _resourceLoader;
    private string _lastMonitoredError = string.Empty;
    private ulong _lastMonitoredScriptErrors;
    private ulong _lastMonitoredFrameScriptErrors;
    private NativeWebScenePerformanceSnapshot? _lastPerformanceSnapshot;

    public MainWindow()
        : this(Environment.GetCommandLineArgs())
    {
    }

    internal MainWindow(IReadOnlyList<string> arguments)
    {
        _arguments = arguments;
        InitializeComponent();
        var textMode = Environment.GetEnvironmentVariable(
            "WEBSCENE_TEXT_POSITIONING")?.Trim().ToLowerInvariant();
        Title += textMode is "harfbuzz" or "legacy" or "off" or "0"
            ? " · HarfBuzz baseline"
            : OperatingSystem.IsMacOS()
                ? " · CoreText positioning"
                : " · Platform text fallback";
        var rasterizationMode = Environment.GetEnvironmentVariable(
            "WEBSCENE_TEXT_RASTERIZATION")?.Trim();
        if (!string.IsNullOrWhiteSpace(rasterizationMode))
        {
            Title += $" · {rasterizationMode} raster";
        }
        if (IsRuntimeMonitoringEnabled)
        {
            Title += " · cadence monitor";
        }
        _diagnosticsTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _diagnosticsTimer.Tick += OnDiagnosticsTick;
        Opened += OnOpened;
        Closed += OnClosed;
    }

    private async void OnOpened(object? sender, EventArgs args)
    {
        Opened -= OnOpened;
        try
        {
            var paths = SamplePaths.Resolve(_arguments);
            _resourceLoader = new AvaloniaResourceLoader
            {
                ResourceCaptureDirectory = paths.ResourceCaptureDirectory,
                ResourceReplayDirectory = paths.ResourceReplayDirectory
            };
            var startupTimer = Stopwatch.StartNew();
            var replayPreparationTimer = Stopwatch.StartNew();
            _resourceLoader.PrepareResourceReplay();
            replayPreparationTimer.Stop();
            Stopwatch? navigationTimer = null;
            StatusText.Text = "Loading hosted TradingView terminal…";
            DiagnosticsText.Text = paths.DocumentUrl;
            await TerminalHost.LoadAsync(
                new NativeWebSceneLoadOptions
                {
                    Source = paths.DocumentUrl,
                    NativeLibraryPath = paths.NativeLibraryPath,
                    CompilationCacheDirectory = paths.CompilationCacheDirectory,
                    ResourceLoader = _resourceLoader
                },
                (_, _) =>
                {
                    navigationTimer = Stopwatch.StartNew();
                    return ValueTask.CompletedTask;
                });
            StatusText.Text = "TradingView terminal loaded";
            if (_arguments.Contains("--startup-profile", StringComparer.Ordinal))
            {
                var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(30);
                var chartReady = false;
                while (DateTime.UtcNow < deadline)
                {
                    var ready = await TerminalHost.EvaluateTextAsync("""
                        (() => {
                          for (const frame of document.querySelectorAll('iframe')) {
                            try {
                              const chart = frame.contentDocument;
                              const loading = chart?.querySelector('.loading-indicator');
                              if ((chart?.querySelectorAll('canvas').length ?? 0) >= 8
                                  && loading && getComputedStyle(loading).display === 'none') {
                                return true;
                              }
                            } catch {}
                          }
                          return false;
                        })()
                        """);
                    if (ready == "true")
                    {
                        chartReady = true;
                        break;
                    }
                    _resourceLoader.ThrowIfResourceReplayFailed();
                    await Task.Delay(25);
                }
                _resourceLoader.ThrowIfResourceReplayFailed();
                if (!chartReady)
                {
                    throw new TimeoutException(
                        "TradingView chart did not reach the startup-profile readiness gate.");
                }
                _resourceLoader.FlushResourceCapture();
                Console.WriteLine(FormattableString.Invariant(
                    $"TradingView desktop chart ready wall: {startupTimer.Elapsed.TotalMilliseconds:F3} ms"));
                Console.WriteLine(FormattableString.Invariant(
                    $"TradingView desktop replay preparation: {replayPreparationTimer.Elapsed.TotalMilliseconds:F3} ms"));
                Console.WriteLine(FormattableString.Invariant(
                    $"TradingView desktop chart ready navigation: {navigationTimer?.Elapsed.TotalMilliseconds ?? double.NaN:F3} ms"));
                await TerminalHost.EvaluateTextAsync("""
                    globalThis.__webSceneComponentReady = true;
                    document.body.setAttribute('data-webscene-profile-ready', 'true');
                    true
                    """);
                // Conditional chunks can be requested shortly after the visual
                // readiness gate. Give capture runs a longer observation window
                // so later strict replays do not depend on lucky task ordering.
                await Task.Delay(paths.ResourceCaptureDirectory is null ? 500 : 2_000);
                var startupMetrics = TerminalHost.CapturePerformanceSnapshot();
                Console.WriteLine(
                    "TradingView desktop startup metrics: "
                    + JsonSerializer.Serialize(new
                    {
                        startupMetrics.Engine.CompilationRequests,
                        startupMetrics.Engine.CompilationMemoryHits,
                        startupMetrics.Engine.CompilationPersistentHits,
                        startupMetrics.Engine.CompilationPersistentMisses,
                        CompilationTimeMilliseconds =
                            startupMetrics.Engine.CompilationTimeNanoseconds / 1_000_000d,
                        startupMetrics.ResourceCache.Requests,
                        startupMetrics.ResourceCache.Hits,
                        startupMetrics.ResourceCache.Misses,
                        startupMetrics.ResourceCache.BytesRead,
                        startupMetrics.Engine.DomNodes,
                        startupMetrics.Engine.LayoutPasses,
                        startupMetrics.Engine.PublishedScenes,
                        startupMetrics.Engine.AcquiredScenes,
                        LastLayoutMilliseconds =
                            startupMetrics.Engine.LastLayoutNanoseconds / 1_000_000d,
                        LastSceneBuildMilliseconds =
                            startupMetrics.Engine.LastSceneBuildNanoseconds / 1_000_000d,
                        LastScenePublicationMilliseconds =
                            startupMetrics.Engine.LastScenePublicationNanoseconds / 1_000_000d,
                        MaximumScenePublicationMilliseconds =
                            startupMetrics.Engine.MaximumScenePublicationNanoseconds / 1_000_000d,
                        startupMetrics.SceneFlow.PublicationAttempts,
                        startupMetrics.SceneFlow.BlockedPublications,
                        startupMetrics.SceneFlow.AcknowledgedScenes
                    }));
                var diagnostics = TerminalHost.SceneDiagnostics;
                var profileStart = diagnostics.IndexOf(
                    "startup-profile=", StringComparison.Ordinal);
                var profileEnd = profileStart < 0
                    ? -1
                    : diagnostics.IndexOf(" | ", profileStart, StringComparison.Ordinal);
                Console.WriteLine(profileStart < 0
                    ? "TradingView desktop startup profile unavailable"
                    : "TradingView desktop " + diagnostics[profileStart..(
                        profileEnd < 0 ? diagnostics.Length : profileEnd)]);
                Close();
                return;
            }
            _diagnosticsTimer.Start();
            await RefreshDiagnosticsAsync();
        }
        catch (Exception error)
        {
            StatusText.Text = "TradingView terminal failed to load";
            LoadFailureText.Text = error.ToString();
            LoadFailure.IsVisible = true;
        }
    }

    private async void OnDiagnosticsTick(object? sender, EventArgs args)
    {
        await RefreshDiagnosticsAsync();
        MonitorRuntime();
    }

    private async Task RefreshDiagnosticsAsync()
    {
        try
        {
            var json = await TerminalHost.EvaluateTextAsync("""
                (() => {
                  const ws = [
                    globalThis,
                    ...Array.from(document.querySelectorAll('iframe'))
                      .map(frame => frame.contentWindow)
                  ]
                    .map(realm =>
                      realm?.__webSceneWebSocketDiagnostics?.() ?? null)
                    .filter(Boolean)
                    .reduce((total, current) => ({
                      opened: total.opened + current.opened,
                      messages: total.messages + current.messages,
                      bytesReceived:
                        total.bytesReceived + current.bytesReceived,
                      errors: total.errors + current.errors
                    }), {
                      opened: 0, messages: 0,
                      bytesReceived: 0, errors: 0
                    });
                  return `WebSocket: ${ws.opened ?? 0} opened · `
                    + `${ws.messages ?? 0} messages · `
                    + `${ws.bytesReceived ?? 0} bytes · `
                    + `${ws.errors ?? 0} errors`;
                })()
                """);
            DiagnosticsText.Text = JsonSerializer.Deserialize<string>(json) ?? json;
        }
        catch (Exception error)
        {
            if (IsRuntimeMonitoringEnabled)
            {
                Console.Error.WriteLine(
                    $"[WebScene monitor] diagnostics evaluation failed: {error}");
                MonitorRuntime();
            }
            else
            {
                _diagnosticsTimer.Stop();
            }
        }
    }

    private bool IsRuntimeMonitoringEnabled
        => _arguments.Contains("--monitor-runtime", StringComparer.Ordinal);

    private void MonitorRuntime()
    {
        if (!IsRuntimeMonitoringEnabled)
        {
            return;
        }

        try
        {
            foreach (var message in TerminalHost.DrainConsoleMessages())
            {
                Console.WriteLine("[WebScene console] " + message);
            }

            var lastError = TerminalHost.LastError;
            if (!string.IsNullOrWhiteSpace(lastError)
                && !string.Equals(
                    lastError,
                    _lastMonitoredError,
                    StringComparison.Ordinal))
            {
                Console.Error.WriteLine("[WebScene last error] " + lastError);
                _lastMonitoredError = lastError;
            }

            var snapshot = TerminalHost.CapturePerformanceSnapshot();
            var engine = snapshot.Engine;
            var errorsChanged = engine.ScriptErrors != _lastMonitoredScriptErrors
                || engine.FrameScriptErrors != _lastMonitoredFrameScriptErrors;
            if (_lastPerformanceSnapshot is { } baseline)
            {
                var delta = snapshot.Since(baseline);
                var elapsedSeconds = Math.Max(
                    delta.Elapsed.TotalSeconds,
                    double.Epsilon);
                static double Rate(double count, double elapsed)
                    => count / elapsed;
                Console.WriteLine(
                    string.Create(
                        CultureInfo.InvariantCulture,
                        $"[WebScene cadence] compositor={Rate(delta.CompositionAnimationFrames, elapsedSeconds):F1} Hz, "
                        + $"hostFrames={Rate(delta.CompositionSubmittedAnimationFrames, elapsedSeconds):F1} Hz, "
                        + $"published={Rate(delta.PublishedScenes, elapsedSeconds):F1} Hz, "
                        + $"invalidated={Rate(delta.CompositionInvalidations, elapsedSeconds):F1} Hz, "
                        + $"rendered={Rate(delta.RenderedScenes, elapsedSeconds):F1} Hz, "
                        + $"renderCallbacks={Rate(delta.CompositionRenderCallbacks, elapsedSeconds):F1} Hz, "
                        + $"unchangedRenderCallbacks={Rate(delta.CompositionUnchangedRenderCallbacks, elapsedSeconds):F1} Hz, "
                        + $"uiWakes={Rate(delta.CompositionUiWakes, elapsedSeconds):F1} Hz, "
                        + $"pendingMailbox={snapshot.Surface.PendingCompositionPublications}, "
                        + $"acceptedInputs={Rate(delta.AcceptedInputEvents, elapsedSeconds):F1} Hz"));
            }
            if (errorsChanged)
            {
                Console.WriteLine(
                    $"[WebScene monitor] scripts={engine.ExecutedScripts}, "
                    + $"scriptErrors={engine.ScriptErrors}, "
                    + $"frameScriptErrors={engine.FrameScriptErrors}");
            }
            _lastMonitoredScriptErrors = engine.ScriptErrors;
            _lastMonitoredFrameScriptErrors = engine.FrameScriptErrors;
            _lastPerformanceSnapshot = snapshot;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene monitor] runtime sampling failed: {error}");
        }
    }

    private async void OnClosed(object? sender, EventArgs args)
    {
        _diagnosticsTimer.Stop();
        _resourceLoader?.FlushResourceCapture();
        await TerminalHost.DisposeAsync();
    }
}

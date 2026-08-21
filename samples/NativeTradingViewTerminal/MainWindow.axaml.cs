using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Threading;

namespace NativeTradingViewTerminal;

public sealed partial class MainWindow : Window
{
    private readonly IReadOnlyList<string> _arguments;
    private readonly DispatcherTimer _diagnosticsTimer;

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
            var startupTimer = Stopwatch.StartNew();
            StatusText.Text = "Loading hosted TradingView terminal…";
            DiagnosticsText.Text = paths.DocumentUrl;
            await TerminalHost.LoadAsync(
                paths.DocumentUrl,
                paths.NativeLibraryPath,
                paths.CompilationCacheDirectory);
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
                    await Task.Delay(25);
                }
                if (!chartReady)
                {
                    throw new TimeoutException(
                        "TradingView chart did not reach the startup-profile readiness gate.");
                }
                Console.WriteLine(FormattableString.Invariant(
                    $"TradingView desktop chart ready wall: {startupTimer.Elapsed.TotalMilliseconds:F3} ms"));
                await TerminalHost.EvaluateTextAsync("""
                    globalThis.__webSceneComponentReady = true;
                    document.body.setAttribute('data-webscene-profile-ready', 'true');
                    true
                    """);
                await Task.Delay(500);
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
                        startupMetrics.ResourceCache.BytesRead
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
        => await RefreshDiagnosticsAsync();

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
        catch
        {
            _diagnosticsTimer.Stop();
        }
    }

    private async void OnClosed(object? sender, EventArgs args)
    {
        _diagnosticsTimer.Stop();
        await TerminalHost.DisposeAsync();
    }
}

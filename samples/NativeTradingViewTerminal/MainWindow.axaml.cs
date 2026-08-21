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
            StatusText.Text = "Loading hosted TradingView terminal…";
            DiagnosticsText.Text = paths.DocumentUrl;
            await TerminalHost.LoadAsync(
                paths.DocumentUrl,
                paths.NativeLibraryPath,
                paths.CompilationCacheDirectory);
            StatusText.Text = "TradingView terminal loaded";
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

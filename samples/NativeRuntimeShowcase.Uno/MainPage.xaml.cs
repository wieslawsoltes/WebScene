using System.Diagnostics;
using WebScene.Backends.Uno.Native;
using WebScene.Diagnostics.Cdp;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using NativeRuntimeShowcase.Interop;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace NativeRuntimeShowcase.Uno;

public sealed partial class MainPage : Page
{
    private readonly UnoNativeWebSceneView _terminal = new();
    private readonly UnoNativeWebSceneView _editor = new();
    private readonly DispatcherTimer _diagnosticsTimer = new();
    private readonly IReadOnlyList<string> _arguments =
        Environment.GetCommandLineArgs();
    private readonly WebSceneV8InspectorLaunchConfiguration? _inspectorLaunch;
    private WebSceneV8InspectorHost? _v8InspectorHost;
    private UnoNativeWebSceneView? _inspectedView;
    private string? _nativeLibraryPath;
    private ShowcaseEditorSession? _editorSession;
    private StorageFile? _currentFile;
    private bool _terminalLoaded;
    private bool _terminalLoading;
    private bool _editorLoading;
    private bool _terminalDiagnosticsReported;
    private TimeSpan _terminalLoadElapsed;

    public MainPage()
    {
        _inspectorLaunch = WebSceneV8InspectorCommandLine.Resolve(_arguments);
        InitializeComponent();
        TerminalContent.Content = _terminal;
        EditorContent.Content = _editor;
        _diagnosticsTimer.Interval = TimeSpan.FromSeconds(1);
        _diagnosticsTimer.Tick += OnDiagnosticsTick;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        Loaded -= OnLoaded;
        try
        {
            _nativeLibraryPath =
                ShowcasePaths.ResolveNativeLibraryPath(_arguments);
            if (_arguments.Contains("--editor", StringComparer.Ordinal))
            {
                await ShowEditorAsync();
                return;
            }
            await ShowTradingViewAsync();
        }
        catch (Exception error)
        {
            ShowFailure("Native TradingView startup failed", error);
        }
    }

    private async void OnShowTradingView(object sender, RoutedEventArgs args)
    {
        try
        {
            await ShowTradingViewAsync();
        }
        catch (Exception error)
        {
            ShowFailure("Native TradingView startup failed", error);
        }
    }

    private async Task ShowTradingViewAsync()
    {
        TerminalContent.Visibility = Visibility.Visible;
        EditorContent.Visibility = Visibility.Collapsed;
        await WaitForLayoutAsync(TerminalContent);
        await EnsureTerminalAsync();
        DocumentText.Text = ShowcasePaths.TradingViewUrl;
        RefreshDiagnostics("TradingView terminal");
    }

    private async Task EnsureTerminalAsync()
    {
        if (_terminalLoaded)
        {
            return;
        }
        if (_terminalLoading)
        {
            while (_terminalLoading)
            {
                await Task.Delay(16);
            }
            return;
        }

        _terminalLoading = true;
        try
        {
            _nativeLibraryPath ??=
                ShowcasePaths.ResolveNativeLibraryPath(_arguments);
            StatusText.Text = "Loading hosted TradingView terminal…";
            var started = Stopwatch.StartNew();
            await _terminal.LoadAsync(
                ShowcasePaths.TradingViewUrl,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Uno", "tradingview"),
                PrepareInspectorAsync,
                DocumentBarrierTimeout);
            started.Stop();
            _terminalLoadElapsed = started.Elapsed;
            _terminalLoaded = true;
            _diagnosticsTimer.Start();
            Console.WriteLine(
                $"[WebScene Uno] TradingView document loaded in "
                + $"{_terminalLoadElapsed.TotalMilliseconds:F0} ms.");
        }
        finally
        {
            _terminalLoading = false;
        }
    }

    private async void OnShowEditor(object sender, RoutedEventArgs args)
    {
        try
        {
            await ShowEditorAsync();
        }
        catch (Exception error)
        {
            ShowFailure("Native Monaco startup failed", error);
        }
    }

    private async Task ShowEditorAsync()
    {
        TerminalContent.Visibility = Visibility.Collapsed;
        EditorContent.Visibility = Visibility.Visible;
        await WaitForLayoutAsync(EditorContent);
        await EnsureEditorAsync();
        DocumentText.Text = _currentFile?.Path ?? "GeneratedMonacoApi.cs";
        StatusText.Text =
            "Monaco ready · generated C# facade: MonacoEditor + MonacoApi";
    }

    private static async Task WaitForLayoutAsync(FrameworkElement element)
    {
        for (var attempt = 0;
            attempt < 10
            && (element.ActualWidth <= 0 || element.ActualHeight <= 0);
            attempt++)
        {
            await Task.Delay(16);
        }
    }

    private async Task EnsureEditorAsync()
    {
        if (_editorSession is not null)
        {
            return;
        }
        if (_editorLoading)
        {
            while (_editorLoading)
            {
                await Task.Delay(16);
            }
            return;
        }

        _editorLoading = true;
        try
        {
            _nativeLibraryPath ??=
                ShowcasePaths.ResolveNativeLibraryPath(_arguments);
            var documentPath = Path.Combine(
                AppContext.BaseDirectory,
                "index.html");
            var started = Stopwatch.StartNew();
            await _editor.LoadAsync(
                new Uri(documentPath).AbsoluteUri,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Uno", "monaco"),
                PrepareInspectorAsync,
                DocumentBarrierTimeout);
            var session = new ShowcaseEditorSession(
                _editor.CreateJavaScriptInvoker());
            try
            {
                await session.InitializeAsync();
                started.Stop();
                _editorSession = session;
            }
            catch
            {
                await session.DisposeAsync();
                throw;
            }
            var metrics = _editor.EngineMetrics;
            Console.WriteLine(
                $"[WebScene Uno] Monaco ready in "
                + $"{started.Elapsed.TotalMilliseconds:F0} ms; V8 cache="
                + $"{metrics.CompilationPersistentHits}/"
                + $"{metrics.CompilationRequests}.");
        }
        finally
        {
            _editorLoading = false;
        }
    }

    private async void OnOpenFile(object sender, RoutedEventArgs args)
    {
        try
        {
            await ShowEditorAsync();
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add("*");
            var file = await picker.PickSingleFileAsync();
            if (file is null)
            {
                return;
            }

            var content = await FileIO.ReadTextAsync(file);
            await _editorSession!.OpenAsync(file.Name, content);
            _currentFile = file;
            SaveButton.IsEnabled = true;
            DocumentText.Text = string.IsNullOrWhiteSpace(file.Path)
                ? file.Name
                : file.Path;
            StatusText.Text =
                $"Opened {file.Name} through generated MonacoEditor.SetValueAsync";
        }
        catch (Exception error)
        {
            ShowFailure("The file could not be opened", error);
        }
    }

    private async void OnSaveFile(object sender, RoutedEventArgs args)
    {
        if (_currentFile is null || _editorSession is null)
        {
            return;
        }

        try
        {
            var content = await _editorSession.ReadAsync();
            await FileIO.WriteTextAsync(_currentFile, content);
            StatusText.Text =
                $"Saved {_currentFile.Name} through generated MonacoEditor.GetValueAsync";
        }
        catch (Exception error)
        {
            ShowFailure("The file could not be saved", error);
        }
    }

    private void OnDiagnosticsTick(object? sender, object args)
    {
        if (TerminalContent.Visibility == Visibility.Visible)
        {
            RefreshDiagnostics("TradingView terminal");
        }
    }

    private void RefreshDiagnostics(string prefix)
    {
        var diagnostics = _terminal.RenderDiagnostics;
        var metrics = _terminal.EngineMetrics;
        StatusText.Text =
            $"{prefix} · native scenes rendered={diagnostics.RenderedSceneCount} "
            + $"published={diagnostics.PublishedSceneCount} "
            + $"load={_terminalLoadElapsed.TotalMilliseconds:F0}ms "
            + $"V8 cache={metrics.CompilationPersistentHits}/"
            + $"{metrics.CompilationRequests}";
        if (!_terminalDiagnosticsReported
            && diagnostics.RenderedSceneCount > 0)
        {
            _terminalDiagnosticsReported = true;
            Console.WriteLine($"[WebScene Uno] {StatusText.Text}");
        }
    }

    private void ShowFailure(string title, Exception error)
    {
        LoadFailureText.Text = $"{title}\n\n{error}";
        LoadFailure.Visibility = Visibility.Visible;
        StatusText.Text = title;
        Console.Error.WriteLine($"{title}: {error}");
    }

    private ValueTask PrepareInspectorAsync(
        UnoNativeWebSceneView view,
        CancellationToken cancellationToken)
        => new(SelectInspectorTargetAsync(
            view,
            ReferenceEquals(view, _editor)
                ? "WebScene V8 · Monaco · Uno"
                : "WebScene V8 · TradingView · Uno",
            cancellationToken));

    private TimeSpan? DocumentBarrierTimeout
        => _inspectorLaunch?.WaitForDebugger == true
            ? Timeout.InfiniteTimeSpan
            : null;

    private async Task SelectInspectorTargetAsync(
        UnoNativeWebSceneView view,
        string title,
        CancellationToken cancellationToken)
    {
        if (_inspectorLaunch is null) return;
        if (ReferenceEquals(_inspectedView, view)
            && _v8InspectorHost?.IsRunning == true)
        {
            return;
        }
        if (_v8InspectorHost is not null)
        {
            await _v8InspectorHost.DisposeAsync();
        }
        var host = new WebSceneV8InspectorHost(
            view.OpenV8InspectorSession,
            () => view.Source,
            _inspectorLaunch.CreateHostOptions(),
            title: title);
        try
        {
            await host.StartAsync(cancellationToken);
        }
        catch
        {
            await host.DisposeAsync();
            throw;
        }
        _v8InspectorHost = host;
        _inspectedView = view;
        Console.WriteLine(
            $"WebScene V8 Inspector{(_inspectorLaunch.WaitForDebugger ? " (waiting for debugger)" : string.Empty)} "
            + $"discovery: {host.DiscoveryUri}json/list");
    }

    private async void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _diagnosticsTimer.Stop();
        if (_v8InspectorHost is not null)
        {
            await _v8InspectorHost.DisposeAsync();
        }
        if (_editorSession is not null)
        {
            await _editorSession.DisposeAsync();
        }
        await _editor.DisposeAsync();
        await _terminal.DisposeAsync();
    }
}

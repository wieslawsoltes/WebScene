using WebScene.Backends.Uno.Native;
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
    private string? _nativeLibraryPath;
    private ShowcaseEditorSession? _editorSession;
    private StorageFile? _currentFile;
    private bool _editorLoading;

    public MainPage()
    {
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
            StatusText.Text = "Loading hosted TradingView terminal…";
            await _terminal.LoadAsync(
                ShowcasePaths.TradingViewUrl,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Uno", "tradingview"));
            _diagnosticsTimer.Start();
            RefreshDiagnostics("TradingView terminal loaded");
        }
        catch (Exception error)
        {
            ShowFailure("Native TradingView startup failed", error);
        }
    }

    private void OnShowTradingView(object sender, RoutedEventArgs args)
    {
        TerminalContent.Visibility = Visibility.Visible;
        EditorContent.Visibility = Visibility.Collapsed;
        DocumentText.Text = ShowcasePaths.TradingViewUrl;
        RefreshDiagnostics("TradingView terminal");
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
        await EnsureEditorAsync();
        TerminalContent.Visibility = Visibility.Collapsed;
        EditorContent.Visibility = Visibility.Visible;
        DocumentText.Text = _currentFile?.Path ?? "GeneratedMonacoApi.cs";
        StatusText.Text =
            "Monaco ready · generated C# facade: MonacoEditor + MonacoApi";
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
            await _editor.LoadAsync(
                new Uri(documentPath).AbsoluteUri,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Uno", "monaco"));
            var session = new ShowcaseEditorSession(
                _editor.EvaluateJsonAsync);
            await session.InitializeAsync();
            _editorSession = session;
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
        StatusText.Text =
            $"{prefix} · native scenes rendered={diagnostics.RenderedSceneCount} "
            + $"published={diagnostics.PublishedSceneCount}";
    }

    private void ShowFailure(string title, Exception error)
    {
        LoadFailureText.Text = $"{title}\n\n{error}";
        LoadFailure.Visibility = Visibility.Visible;
        StatusText.Text = title;
        Console.Error.WriteLine($"{title}: {error}");
    }

    private async void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _diagnosticsTimer.Stop();
        if (_editorSession is not null)
        {
            await _editorSession.DisposeAsync();
        }
        await _editor.DisposeAsync();
        await _terminal.DisposeAsync();
    }
}

using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using NativeRuntimeShowcase.Interop;

namespace NativeRuntimeShowcase.Avalonia;

public sealed partial class MainWindow : Window
{
    private readonly IReadOnlyList<string> _arguments;
    private readonly DispatcherTimer _diagnosticsTimer;
    private string? _nativeLibraryPath;
    private ShowcaseEditorSession? _editorSession;
    private IStorageFile? _currentFile;
    private bool _editorLoading;

    public MainWindow()
        : this(Environment.GetCommandLineArgs())
    {
    }

    internal MainWindow(IReadOnlyList<string> arguments)
    {
        _arguments = arguments;
        InitializeComponent();
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
            _nativeLibraryPath =
                ShowcasePaths.ResolveNativeLibraryPath(_arguments);
            if (_arguments.Contains("--editor", StringComparer.Ordinal))
            {
                await ShowEditorAsync();
                return;
            }
            StatusText.Text = "Loading hosted TradingView terminal…";
            await TerminalHost.LoadAsync(
                ShowcasePaths.TradingViewUrl,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Avalonia", "tradingview"));
            _diagnosticsTimer.Start();
            RefreshDiagnostics("TradingView terminal loaded");
        }
        catch (Exception error)
        {
            ShowFailure("Native TradingView startup failed", error);
        }
    }

    private void OnShowTradingView(object? sender, RoutedEventArgs args)
    {
        TerminalHost.IsVisible = true;
        EditorHost.IsVisible = false;
        DocumentText.Text = ShowcasePaths.TradingViewUrl;
        RefreshDiagnostics("TradingView terminal");
    }

    private async void OnShowEditor(object? sender, RoutedEventArgs args)
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
        TerminalHost.IsVisible = false;
        EditorHost.IsVisible = true;
        DocumentText.Text = _currentFile?.Name ?? "GeneratedMonacoApi.cs";
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
            await EditorHost.LoadAsync(
                new Uri(documentPath).AbsoluteUri,
                _nativeLibraryPath,
                ShowcasePaths.CacheDirectory("Avalonia", "monaco"));
            var session = new ShowcaseEditorSession(
                EditorHost.EvaluateJsonAsync);
            await session.InitializeAsync();
            _editorSession = session;
        }
        finally
        {
            _editorLoading = false;
        }
    }

    private async void OnOpenFile(object? sender, RoutedEventArgs args)
    {
        try
        {
            await ShowEditorAsync();
            var files = await StorageProvider.OpenFilePickerAsync(
                new FilePickerOpenOptions
                {
                    Title = "Open a file in native Monaco",
                    AllowMultiple = false,
                    FileTypeFilter = [FilePickerFileTypes.All]
                });
            var file = files.FirstOrDefault();
            if (file is null)
            {
                return;
            }

            await using var stream = await file.OpenReadAsync();
            using var reader = new StreamReader(
                stream,
                detectEncodingFromByteOrderMarks: true);
            var content = await reader.ReadToEndAsync();
            await _editorSession!.OpenAsync(file.Name, content);
            _currentFile = file;
            SaveButton.IsEnabled = true;
            DocumentText.Text = file.TryGetLocalPath() ?? file.Name;
            StatusText.Text =
                $"Opened {file.Name} through generated MonacoEditor.SetValueAsync";
        }
        catch (Exception error)
        {
            ShowFailure("The file could not be opened", error);
        }
    }

    private async void OnSaveFile(object? sender, RoutedEventArgs args)
    {
        if (_currentFile is null || _editorSession is null)
        {
            return;
        }

        try
        {
            var content = await _editorSession.ReadAsync();
            await using var stream = await _currentFile.OpenWriteAsync();
            stream.SetLength(0);
            await using var writer = new StreamWriter(stream);
            await writer.WriteAsync(content);
            await writer.FlushAsync();
            StatusText.Text =
                $"Saved {_currentFile.Name} through generated MonacoEditor.GetValueAsync";
        }
        catch (Exception error)
        {
            ShowFailure("The file could not be saved", error);
        }
    }

    private void OnDiagnosticsTick(object? sender, EventArgs args)
    {
        if (TerminalHost.IsVisible)
        {
            RefreshDiagnostics("TradingView terminal");
        }
    }

    private void RefreshDiagnostics(string prefix)
    {
        var diagnostics = TerminalHost.RenderDiagnostics;
        StatusText.Text =
            $"{prefix} · native scenes rendered={diagnostics.RenderedSceneCount} "
            + $"published={diagnostics.PublishedSceneCount}";
    }

    private void ShowFailure(string title, Exception error)
    {
        LoadFailureText.Text = $"{title}\n\n{error}";
        LoadFailure.IsVisible = true;
        StatusText.Text = title;
        Console.Error.WriteLine($"{title}: {error}");
    }

    private async void OnClosed(object? sender, EventArgs args)
    {
        _diagnosticsTimer.Stop();
        if (_editorSession is not null)
        {
            await _editorSession.DisposeAsync();
        }
        await EditorHost.DisposeAsync();
        await TerminalHost.DisposeAsync();
    }
}

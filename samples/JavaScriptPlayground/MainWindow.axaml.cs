using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using JavaScript.Avalonia;
using Avalonia.Interactivity;
using AvaloniaEdit.Document;
using AvaloniaEdit.TextMate;
using TextMateSharp.Grammars;
using System.Linq;
using Avalonia.VisualTree;
#if WEBSCENE_CLEARSCRIPT_V8
using JavaScript.Avalonia.ClearScript;
#endif

namespace JavaScriptPlayground;

public partial class MainWindow : Window
{
    private const string TradingViewTerminalUrl =
        "https://trading-terminal.tradingview-widget.com/";
    private readonly List<Preset> _presets;
    private readonly PlaygroundShellBridge _shellBridge = new();
    private readonly List<AvaloniaBrowserHost> _hosts = new();
    private readonly List<Control> _documentRoots = new();
    private Preset? _activePreset;
#if WEBSCENE_CLEARSCRIPT_V8
    private readonly List<ClearScriptV8Runtime> _v8Runtimes = new();
    private readonly ClearScriptV8SharedCache _v8SharedCache = CreatePlaygroundV8Cache();
    private CancellationTokenSource? _v8PreparationCancellation;
    private Task<V8PreparationSummary>? _v8PreparationTask;
    private DispatcherTimer? _v8PreparationIndicator;
    private int _v8PreparationIndicatorFrame;
    private int _v8PreparationUiHeartbeats;
    private long _v8PreparationLastHeartbeat;
    private long _v8PreparationMaximumHeartbeatGap;
#endif
    private readonly TextDocument _xamlDocument = new();
    private readonly TextDocument _scriptDocument = new();
    private readonly RegistryOptions _registryOptions = new(ThemeName.LightPlus);
    private CancellationTokenSource? _nativeMonacoCancellation;
    private Task? _nativeMonacoLoadTask;
    private int _nativeMonacoLoadVersion;
    private readonly DispatcherTimer _nativeTradingViewDiagnosticsTimer;
    private CancellationTokenSource? _nativeTradingViewCancellation;
    private Task? _nativeTradingViewLoadTask;
    private int _nativeTradingViewLoadVersion;
    private int _xamlLoadVersion;

    public MainWindow()
    {
        InitializeComponent();
#if DEBUG
        this.AttachDevTools();
#endif

        ConfigureEditors();

        _presets = CreatePresets();
        _nativeTradingViewDiagnosticsTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        _nativeTradingViewDiagnosticsTimer.Tick +=
            OnNativeTradingViewDiagnosticsTick;
        PresetCombo.ItemsSource = _presets;
        WorkspaceTabs.SelectionChanged += WorkspaceTabsOnSelectionChanged;
        var presetIndex = ResolveStartupPresetIndex(_presets);
        PresetCombo.SelectedIndex = presetIndex;
        PresetCombo.SelectionChanged += PresetComboOnSelectionChanged;
        AutoRunCheckBox.IsChecked = true;
        var startWithNativeMonaco = Environment.GetCommandLineArgs()
            .Any(static argument => string.Equals(
                argument,
                "--monaco",
                StringComparison.Ordinal));
        var startWithNativeTradingView = Environment.GetCommandLineArgs()
            .Any(static argument => string.Equals(
                argument,
                "--tradingview",
                StringComparison.Ordinal));

        if (startWithNativeTradingView)
        {
            if (presetIndex >= 0)
            {
                _activePreset = _presets[presetIndex];
                _xamlDocument.Text = _activePreset.Xaml;
                _scriptDocument.Text = _activePreset.Script;
            }
            WorkspaceTabs.SelectedItem = NativeTradingViewTab;
        }
        else if (startWithNativeMonaco)
        {
            if (presetIndex >= 0)
            {
                _activePreset = _presets[presetIndex];
                _xamlDocument.Text = _activePreset.Xaml;
                _scriptDocument.Text = _activePreset.Script;
            }
            WorkspaceTabs.SelectedItem = NativeMonacoTab;
        }
        else if (presetIndex >= 0)
        {
            ApplyPreset(_presets[presetIndex]);
        }
        else
        {
            ResetHost();
        }
    }

    private void WorkspaceTabsOnSelectionChanged(
        object? sender,
        SelectionChangedEventArgs e)
    {
        if (ReferenceEquals(sender, WorkspaceTabs)
            && ReferenceEquals(WorkspaceTabs.SelectedItem, NativeMonacoTab)
            && IsLoaded)
        {
            _ = EnsureNativeMonacoLoadedAsync(forceReload: false);
        }
        else if (ReferenceEquals(sender, WorkspaceTabs)
            && ReferenceEquals(WorkspaceTabs.SelectedItem, NativeTradingViewTab)
            && IsLoaded)
        {
            _ = EnsureNativeTradingViewLoadedAsync(forceReload: false);
        }
    }

    private async void OnReloadNativeMonacoClick(
        object? sender,
        RoutedEventArgs e)
    {
        await EnsureNativeMonacoLoadedAsync(forceReload: true);
    }

    private Task EnsureNativeMonacoLoadedAsync(bool forceReload)
    {
        if (!forceReload && _nativeMonacoLoadTask is not null)
        {
            return _nativeMonacoLoadTask;
        }

        _nativeMonacoCancellation?.Cancel();
        _nativeMonacoCancellation?.Dispose();
        _nativeMonacoCancellation = new CancellationTokenSource();
        var loadVersion = ++_nativeMonacoLoadVersion;
        _nativeMonacoLoadTask = LoadNativeMonacoAsync(
            loadVersion,
            _nativeMonacoCancellation.Token);
        return _nativeMonacoLoadTask;
    }

    private async Task LoadNativeMonacoAsync(
        int loadVersion,
        CancellationToken cancellationToken)
    {
        NativeMonacoFailure.IsVisible = false;
        NativeMonacoStatus.Text = "Loading native Monaco…";
        NativeMonacoStatus.Foreground = Brushes.Goldenrod;

        try
        {
            var libraryPath = ResolveNativeLibraryPath(
                Environment.GetCommandLineArgs());
            var documentPath = Path.Combine(
                AppContext.BaseDirectory,
                "monaco",
                "index.html");
            var cachePath = Path.Combine(
                Environment.GetFolderPath(
                    Environment.SpecialFolder.LocalApplicationData),
                "WebScene",
                "JavaScriptPlayground",
                "native-monaco-v8-cache");
            await NativeMonacoHost.LoadAsync(
                new Uri(documentPath).AbsoluteUri,
                libraryPath,
                cachePath,
                cancellationToken);

            if (loadVersion == _nativeMonacoLoadVersion)
            {
                NativeMonacoStatus.Text =
                    "Ready · editable JavaScript · syntax highlighting";
                NativeMonacoStatus.Foreground = Brushes.LightGreen;
                Console.WriteLine(
                    "[JavaScript Playground] Native Monaco loaded from "
                    + $"'{libraryPath}'.");
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception error)
        {
            if (loadVersion != _nativeMonacoLoadVersion)
            {
                return;
            }

            Console.Error.WriteLine(
                $"[JavaScript Playground] Native Monaco failure: {error}");
            NativeMonacoStatus.Text = "Native Monaco failed to load";
            NativeMonacoStatus.Foreground = Brushes.IndianRed;
            NativeMonacoFailureText.Text =
                "The native Monaco editor could not start.\n\n"
                + error.Message
                + "\n\nPass --native-library /absolute/path/to/"
                + NativeLibraryFileName()
                + " or set WEBSCENE_NATIVE_ENGINE_LIBRARY.";
            NativeMonacoFailure.IsVisible = true;
        }
    }

    private async void OnReloadNativeTradingViewClick(
        object? sender,
        RoutedEventArgs e)
    {
        await EnsureNativeTradingViewLoadedAsync(forceReload: true);
    }

    private Task EnsureNativeTradingViewLoadedAsync(bool forceReload)
    {
        if (!forceReload && _nativeTradingViewLoadTask is not null)
        {
            return _nativeTradingViewLoadTask;
        }

        _nativeTradingViewDiagnosticsTimer.Stop();
        _nativeTradingViewCancellation?.Cancel();
        _nativeTradingViewCancellation?.Dispose();
        _nativeTradingViewCancellation = new CancellationTokenSource();
        var loadVersion = ++_nativeTradingViewLoadVersion;
        _nativeTradingViewLoadTask = LoadNativeTradingViewAsync(
            loadVersion,
            _nativeTradingViewCancellation.Token);
        return _nativeTradingViewLoadTask;
    }

    private async Task LoadNativeTradingViewAsync(
        int loadVersion,
        CancellationToken cancellationToken)
    {
        NativeTradingViewFailure.IsVisible = false;
        NativeTradingViewStatus.Text = "Loading hosted TradingView terminal…";
        NativeTradingViewStatus.Foreground = Brushes.Goldenrod;

        try
        {
            var libraryPath = ResolveNativeLibraryPath(
                Environment.GetCommandLineArgs());
            var cachePath = Path.Combine(
                Environment.GetFolderPath(
                    Environment.SpecialFolder.LocalApplicationData),
                "WebScene",
                "JavaScriptPlayground",
                "native-tradingview-v8-cache");
            await NativeTradingViewHost.LoadAsync(
                TradingViewTerminalUrl,
                libraryPath,
                cachePath,
                cancellationToken);

            if (loadVersion == _nativeTradingViewLoadVersion)
            {
                NativeTradingViewStatus.Text =
                    "Loaded · connecting to live market data…";
                NativeTradingViewStatus.Foreground = Brushes.LightGreen;
                _nativeTradingViewDiagnosticsTimer.Start();
                await RefreshNativeTradingViewDiagnosticsAsync();
                Console.WriteLine(
                    "[JavaScript Playground] Native TradingView loaded from "
                    + $"'{libraryPath}'.");
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception error)
        {
            if (loadVersion != _nativeTradingViewLoadVersion)
            {
                return;
            }

            Console.Error.WriteLine(
                $"[JavaScript Playground] Native TradingView failure: {error}");
            NativeTradingViewStatus.Text =
                "Native TradingView failed to load";
            NativeTradingViewStatus.Foreground = Brushes.IndianRed;
            NativeTradingViewFailureText.Text =
                "The native TradingView terminal could not start.\n\n"
                + error.Message
                + "\n\nPass --native-library /absolute/path/to/"
                + NativeLibraryFileName()
                + " or set WEBSCENE_NATIVE_ENGINE_LIBRARY.";
            NativeTradingViewFailure.IsVisible = true;
        }
    }

    private async void OnNativeTradingViewDiagnosticsTick(
        object? sender,
        EventArgs e)
        => await RefreshNativeTradingViewDiagnosticsAsync();

    private async Task RefreshNativeTradingViewDiagnosticsAsync()
    {
        try
        {
            var json = await NativeTradingViewHost.EvaluateJsonAsync("""
                (() => {
                  const websocket = [
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
                  return websocket.opened > 0
                    ? `Live WebSocket · ${websocket.messages} messages · `
                      + `${websocket.bytesReceived} bytes`
                    : `Loaded · waiting for native WebSocket data…`;
                })()
                """);
            NativeTradingViewStatus.Text =
                System.Text.Json.JsonSerializer.Deserialize<string>(json)
                ?? json;
        }
        catch when (_nativeTradingViewCancellation?.IsCancellationRequested == true)
        {
        }
        catch (Exception error)
        {
            _nativeTradingViewDiagnosticsTimer.Stop();
            NativeTradingViewStatus.Text =
                $"TradingView diagnostics stopped: {error.Message}";
            NativeTradingViewStatus.Foreground = Brushes.IndianRed;
        }
    }

    private static string ResolveNativeLibraryPath(
        IReadOnlyList<string> arguments)
    {
        for (var index = 0; index + 1 < arguments.Count; index++)
        {
            if (string.Equals(
                    arguments[index],
                    "--native-library",
                    StringComparison.Ordinal))
            {
                return Path.GetFullPath(arguments[index + 1]);
            }
        }

        var configured = Environment.GetEnvironmentVariable(
            "WEBSCENE_NATIVE_ENGINE_LIBRARY");
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return Path.GetFullPath(configured);
        }

        var packaged = Path.Combine(
            AppContext.BaseDirectory,
            NativeLibraryFileName());
        if (File.Exists(packaged))
        {
            return packaged;
        }

        throw new FileNotFoundException(
            "No WebScene native engine was configured.",
            packaged);
    }

    private static string NativeLibraryFileName()
        => OperatingSystem.IsWindows()
            ? "webscene_native_engine.dll"
            : OperatingSystem.IsMacOS()
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";

    private void PresetComboOnSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (PresetCombo.SelectedItem is Preset preset)
        {
            ApplyPreset(preset);
        }
    }

    private void ApplyPreset(Preset preset)
    {
        _activePreset = preset;
        _xamlDocument.Text = preset.Xaml;
        _scriptDocument.Text = preset.Script;
        if (preset.InstanceCount > 1)
        {
            Width = Math.Max(Width, 1800);
            Height = Math.Max(Height, 1100);
        }
        LoadXaml(AutoRunCheckBox.IsChecked == true);
    }

    private void OnApplyPresetClick(object? sender, RoutedEventArgs e)
    {
        if (PresetCombo.SelectedItem is Preset preset)
        {
            ApplyPreset(preset);
        }
    }

    private void OnReloadXamlClick(object? sender, RoutedEventArgs e)
    {
        LoadXaml(AutoRunCheckBox.IsChecked == true);
    }

    private async void OnRunScriptClick(object? sender, RoutedEventArgs e)
    {
#if WEBSCENE_CLEARSCRIPT_V8
        if (_v8PreparationTask is { IsCompleted: false } preparation)
        {
            SetStatus("Waiting for background V8 compilation...", false);
            try
            {
                await preparation;
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
#endif
        RunScript(resetHost: true);
    }

    private void LoadXaml(bool runScript)
    {
        var loadVersion = ++_xamlLoadVersion;
        try
        {
            var xaml = _xamlDocument.Text ?? string.Empty;
            var control = CreatePreviewContent(xaml, _activePreset?.InstanceCount ?? 1);

            PreviewHost.Child = control;
            SetStatus("XAML loaded", false);

            // Establish the containing block before the DOM script creates its descendants.
            PreviewHost.InvalidateMeasure();
            PreviewHost.InvalidateArrange();
            Dispatcher.UIThread.RunJobs();

#if WEBSCENE_CLEARSCRIPT_V8
            DisposeSessions();
            SetDomLoadingOverlay(true, "Compiling JavaScript");
            SetStatus("⠋ Compiling JavaScript off the UI thread...", false);
            CompilationProgress.IsVisible = true;
            Dispatcher.UIThread.Post(
                () =>
                {
                    if (loadVersion == _xamlLoadVersion)
                    {
                        BeginV8Preparation(loadVersion, runScript);
                    }
                },
                DispatcherPriority.Loaded);
#else
            throw new InvalidOperationException("The Playground requires the ClearScript V8 runtime.");
#endif
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[JavaScript Playground] XAML/load failure: {ex}");
            PreviewHost.Child = null;
            SetStatus($"XAML error: {ex.Message}", true);
        }
    }

#if WEBSCENE_CLEARSCRIPT_V8
    private void BeginV8Preparation(int loadVersion, bool runScript)
    {
        _v8PreparationCancellation?.Cancel();
        _v8PreparationCancellation?.Dispose();
        _v8PreparationCancellation = new CancellationTokenSource();
        var cancellationToken = _v8PreparationCancellation.Token;
        var script = _scriptDocument.Text ?? string.Empty;
        SetStatus("⠋ Compiling JavaScript off the UI thread...", false);
        StartV8PreparationIndicator();
        _v8PreparationTask = PrepareV8ScriptsAsync(
            script,
            cancellationToken);
        _ = CompleteV8PreparationAsync(
            _v8PreparationTask,
            loadVersion,
            runScript,
            cancellationToken);
    }

    private async Task CompleteV8PreparationAsync(
        Task<V8PreparationSummary> preparationTask,
        int loadVersion,
        bool runScript,
        CancellationToken cancellationToken)
    {
        try
        {
            var summary = await preparationTask.ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(
                () =>
                {
                    if (cancellationToken.IsCancellationRequested
                        || loadVersion != _xamlLoadVersion)
                    {
                        return;
                    }

                    StopV8PreparationIndicator();
                    SetDomLoadingOverlay(false);
                    ResetHost();
                    var maximumGapMilliseconds = _v8PreparationMaximumHeartbeatGap * 1000d
                                                 / Stopwatch.Frequency;
                    SetStatus(
                        $"V8 prepared {summary.Requested} units in {summary.Elapsed.TotalMilliseconds:F0} ms " +
                        $"({summary.Compiled} compiled, {summary.Reused} reused, " +
                        $"{summary.Workers} workers; UI heartbeat {_v8PreparationUiHeartbeats}, " +
                        $"max gap {maximumGapMilliseconds:F0} ms)",
                        false);
                    Console.WriteLine(
                        $"[JavaScript Playground] V8 preparation: requested={summary.Requested}, " +
                        $"compiled={summary.Compiled}, reused={summary.Reused}, " +
                        $"workers={summary.Workers}, elapsed={summary.Elapsed.TotalMilliseconds:F1} ms, " +
                        $"ui-heartbeats={_v8PreparationUiHeartbeats}, " +
                        $"max-ui-gap={maximumGapMilliseconds:F1} ms, " +
                        $"persistent='{_v8SharedCache.PersistentDirectory}'.");
                    if (runScript)
                    {
                        ScheduleAutoRun(loadVersion);
                    }
                },
                DispatcherPriority.Background);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[JavaScript Playground] V8 preparation failure: {ex}");
            await Dispatcher.UIThread.InvokeAsync(
                () =>
                {
                    if (loadVersion == _xamlLoadVersion)
                    {
                        StopV8PreparationIndicator();
                        SetDomLoadingOverlay(false);
                        SetStatus($"V8 preparation error: {ex.Message}", true);
                    }
                });
        }
    }

    private void StartV8PreparationIndicator()
    {
        StopV8PreparationIndicator();
        _v8PreparationIndicatorFrame = 0;
        _v8PreparationUiHeartbeats = 0;
        _v8PreparationMaximumHeartbeatGap = 0;
        _v8PreparationLastHeartbeat = Stopwatch.GetTimestamp();
        var frames = new[] { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" };
        var timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(80)
        };
        timer.Tick += (_, _) =>
        {
            var now = Stopwatch.GetTimestamp();
            var gap = now - _v8PreparationLastHeartbeat;
            _v8PreparationLastHeartbeat = now;
            _v8PreparationMaximumHeartbeatGap = Math.Max(
                _v8PreparationMaximumHeartbeatGap,
                gap);
            _v8PreparationUiHeartbeats++;
            SetStatus(
                $"{frames[_v8PreparationIndicatorFrame++ % frames.Length]} Compiling JavaScript " +
                $"off the UI thread... UI heartbeat {_v8PreparationUiHeartbeats}",
                false);
        };
        _v8PreparationIndicator = timer;
        CompilationProgress.IsVisible = true;
        timer.Start();
    }

    private void StopV8PreparationIndicator()
    {
        _v8PreparationIndicator?.Stop();
        _v8PreparationIndicator = null;
        CompilationProgress.IsVisible = false;
    }

    private void SetDomLoadingOverlay(bool visible, string? text = null)
    {
        foreach (var root in _documentRoots)
        {
            var surfaces = root.GetVisualDescendants()
                .OfType<CssLayoutPanel>()
                .Where(static panel => string.Equals(
                    panel.Name,
                    "chart_container",
                    StringComparison.Ordinal))
                .ToArray();
            if (root is CssLayoutPanel rootPanel
                && string.Equals(rootPanel.Name, "chart_container", StringComparison.Ordinal))
            {
                surfaces = [rootPanel, .. surfaces];
            }

            foreach (var surface in surfaces.Distinct())
            {
                if (!string.IsNullOrWhiteSpace(text))
                {
                    surface.LoadingOverlayText = text;
                }
                surface.IsLoadingOverlayVisible = visible;
            }
        }
    }

    private async Task<V8PreparationSummary> PrepareV8ScriptsAsync(
        string editorScript,
        CancellationToken cancellationToken)
    {
        var stopwatch = Stopwatch.StartNew();
        var sources = await Task.Run(
            () => BuildV8CompilationSources(
                editorScript,
                cancellationToken),
            cancellationToken).ConfigureAwait(false);
        var workerCount = Math.Min(4, Math.Max(1, sources.Count));
        var partitions = Enumerable.Range(0, workerCount)
            .Select(index => sources
                .Where((_, sourceIndex) => sourceIndex % workerCount == index)
                .ToArray())
            .Where(static partition => partition.Length > 0)
            .ToArray();
        var results = await Task.WhenAll(
            partitions.Select((partition, index) =>
                ClearScriptV8Runtime.PrecompileAsync(
                    _v8SharedCache,
                    partition,
                    includeRuntimeBootstrap: index == 0,
                    cancellationToken: cancellationToken))).ConfigureAwait(false);
        stopwatch.Stop();
        return new V8PreparationSummary(
            results.Sum(static result => result.Requested),
            results.Sum(static result => result.Compiled),
            results.Sum(static result => result.Reused),
            results.Select(static result => result.WorkerThreadId).Distinct().Count(),
            stopwatch.Elapsed);
    }

    private static List<V8CompilationSource> BuildV8CompilationSources(
        string editorScript,
        CancellationToken cancellationToken)
    {
        var sources = new List<V8CompilationSource>();
        if (!string.IsNullOrWhiteSpace(editorScript))
        {
            sources.Add(new V8CompilationSource("playground-editor.js", editorScript));
        }
        return sources;
    }

    private static ClearScriptV8SharedCache CreatePlaygroundV8Cache()
    {
        var configuredDirectory = Environment.GetEnvironmentVariable(
            "WEBSCENE_PLAYGROUND_V8_CACHE_DIRECTORY");
        var directory = string.IsNullOrWhiteSpace(configuredDirectory)
            ? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "WebScene",
                "JavaScriptPlayground",
                "v8-cache")
            : configuredDirectory;
        return new ClearScriptV8SharedCache(new ClearScriptV8SharedCacheOptions
        {
            PersistentDirectory = directory,
            CompatibilityTag = CreateNativeCompatibilityTag(),
            MaxPersistentEntries = 4096,
            MaxPersistentBytes = 768L * 1024 * 1024
        });
    }

    private static string CreateNativeCompatibilityTag()
    {
        var configuredPath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        var rid = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_RID")
                  ?? RuntimeInformation.RuntimeIdentifier;
        var extension = rid.StartsWith("win-", StringComparison.Ordinal)
            ? "dll"
            : rid.StartsWith("linux-", StringComparison.Ordinal)
                ? "so"
                : "dylib";
        var path = string.IsNullOrWhiteSpace(configuredPath)
            ? Path.Combine(
                AppContext.BaseDirectory,
                "runtimes",
                rid,
                "native",
                $"ClearScriptV8.{rid}.{extension}")
            : configuredPath;
        if (!File.Exists(path))
        {
            return $"playground-v1|{rid}|native-missing";
        }
        var info = new FileInfo(path);
        return $"playground-v1|{rid}|{info.Length}|{info.LastWriteTimeUtc.Ticks}";
    }

    private readonly record struct V8PreparationSummary(
        int Requested,
        int Compiled,
        int Reused,
        int Workers,
        TimeSpan Elapsed);
#endif

    private Control CreatePreviewContent(string xaml, int instanceCount)
    {
        _documentRoots.Clear();
        if (instanceCount <= 1)
        {
            var root = LoadPreviewRoot(xaml);
            _documentRoots.Add(root);
            return root;
        }

        if (instanceCount != 4)
        {
            throw new InvalidOperationException("The Playground multi-instance surface currently supports exactly four instances.");
        }

        var grid = new Grid
        {
            RowDefinitions = new RowDefinitions("*,*"),
            ColumnDefinitions = new ColumnDefinitions("*,*")
        };
        for (var index = 0; index < instanceCount; index++)
        {
            var root = LoadPreviewRoot(xaml);
            root.Margin = new Thickness(3);
            Grid.SetRow(root, index / 2);
            Grid.SetColumn(root, index % 2);
            grid.Children.Add(root);
            _documentRoots.Add(root);
        }
        return grid;
    }

    private static Control LoadPreviewRoot(string xaml)
        => AvaloniaRuntimeXamlLoader.Load(xaml) as Control
           ?? throw new InvalidOperationException("Root element must derive from Control.");


    private void ScheduleAutoRun(int loadVersion)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (loadVersion != _xamlLoadVersion)
            {
                return;
            }

            RequestAnimationFrame(_ =>
            {
                Dispatcher.UIThread.Post(() =>
                {
                    if (loadVersion == _xamlLoadVersion)
                    {
                        RunScript(resetHost: false);
                    }
                }, DispatcherPriority.Background);
            });
        }, DispatcherPriority.Loaded);
    }

    private void ResetHost()
    {
        DisposeSessions();
        var roots = _documentRoots.Count > 0
            ? _documentRoots.ToArray()
            : PreviewHost.Child is Control root
                ? [root]
                : Array.Empty<Control>();
        if (roots.Length == 0)
        {
            throw new InvalidOperationException("The Playground preview has no document roots.");
        }

        try
        {
            foreach (var documentRoot in roots)
            {
                CreateSession(documentRoot);
            }
        }
        catch
        {
            DisposeSessions();
            throw;
        }
    }

    private void CreateSession(Control documentRoot)
    {
        var host = new AvaloniaBrowserHost(
            this,
            currentHost => new PlaygroundDomDocument(currentHost, documentRoot));
        _hosts.Add(host);

#if WEBSCENE_CLEARSCRIPT_V8
        var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions
            {
                EnableTrustedSameOriginContextSharing = true,
                SharedCache = _v8SharedCache
            });
        _v8Runtimes.Add(runtime);
        runtime.Engine.AddHostObject("hostShell", _shellBridge);
        runtime.Engine.AddHostObject(
            "playgroundFiles",
            new PlaygroundFileBridge(
                this,
                (callback, result) => runtime.Invoke(callback, result)));
        runtime.Execute("""
if (typeof window !== 'undefined') {
  window.hostShell = hostShell;
  window.playgroundFiles = playgroundFiles;
}
if (typeof globalThis !== 'undefined') {
  globalThis.hostShell = hostShell;
  globalThis.playgroundFiles = playgroundFiles;
}
""", "playground-host-bridges.js");
#else
        throw new InvalidOperationException("The Playground requires the ClearScript V8 runtime.");
#endif
    }

    private void DisposeSessions()
    {
#if WEBSCENE_CLEARSCRIPT_V8
        for (var index = _v8Runtimes.Count - 1; index >= 0; index--)
        {
            _v8Runtimes[index].Dispose();
        }
        _v8Runtimes.Clear();
#endif
        for (var index = _hosts.Count - 1; index >= 0; index--)
        {
            var host = _hosts[index];
            var budget = host.GetUiThreadWorkBudgetMetrics();
            if (budget.JavaScriptSamples + budget.CssSamples + budget.LayoutSamples > 0)
            {
                Console.WriteLine(
                    $"[JavaScript Playground] UI work budget {budget.Budget.TotalMilliseconds:F1} ms, " +
                    $"chart={index + 1}: JS {budget.JavaScriptOverruns}/{budget.JavaScriptSamples} overruns " +
                    $"(max {budget.MaximumJavaScriptDuration.TotalMilliseconds:F1} ms), " +
                    $"CSS {budget.CssOverruns}/{budget.CssSamples} " +
                    $"(max {budget.MaximumCssDuration.TotalMilliseconds:F1} ms), " +
                    $"layout {budget.LayoutOverruns}/{budget.LayoutSamples} " +
                    $"(max {budget.MaximumLayoutDuration.TotalMilliseconds:F1} ms).");
            }
            host.Dispose();
        }
        _hosts.Clear();
    }

    private void ConfigureEditors()
    {
        XamlEditor.Document = _xamlDocument;
        ScriptEditor.Document = _scriptDocument;

        var xamlTextMate = XamlEditor.InstallTextMate(_registryOptions);
        var scriptTextMate = ScriptEditor.InstallTextMate(_registryOptions);
        ApplyGrammar(xamlTextMate, ".xaml");
        ApplyGrammar(scriptTextMate, ".js");
    }

    private void ApplyGrammar(dynamic installation, string extension)
    {
        var language = _registryOptions.GetLanguageByExtension(extension);
        if (language is null)
        {
            return;
        }

        var scope = _registryOptions.GetScopeByLanguageId(language.Id);
        if (scope is not null)
        {
            installation.SetGrammar(scope);
        }
    }

    private void RunScript(bool resetHost)
    {
        try
        {
            if (resetHost || _hosts.Count == 0)
            {
                ResetHost();
            }

            if (_hosts.Count == 0)
            {
                SetStatus("Host not ready", true);
                return;
            }

            var script = _scriptDocument.Text;
            if (!string.IsNullOrWhiteSpace(script))
            {
                if (_v8Runtimes.Count > 0)
                {
                    foreach (var runtime in _v8Runtimes)
                    {
                        runtime.Execute(script, "playground-editor.js");
                    }
                }
            }
            foreach (var host in _hosts)
            {
                if (host.EnableTargetOnlyInlineStyles)
                {
                    // The initial DOM/style transaction must use the full CSS
                    // cascade. Arm the generic incremental policy only after the
                    // user script has completed and the chart can enter resize/
                    // interaction steady state.
                    host.ArmTargetOnlyInlineStyles();
                }
            }
            var instanceText = _hosts.Count == 1 ? string.Empty : $", {_hosts.Count} isolated charts";
            var v8CacheMetrics = _v8SharedCache.GetMetrics();
            SetStatus(
                $"Script executed (V8{instanceText}; source-text reuses " +
                $"{v8CacheMetrics.SourceHits}, compiled-code reuses " +
                $"{v8CacheMetrics.CodeHits}, compilation leaders " +
                $"{v8CacheMetrics.CompilationLeaders}, disk reuses " +
                $"{v8CacheMetrics.DiskHits})",
                false);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[JavaScript Playground] script failure: {ex}");
            SetStatus($"Script error: {ex.Message}", true);
        }
    }

    private void SetStatus(string message, bool isError)
    {
        StatusText.Text = message;
        StatusText.Foreground = isError ? Brushes.DarkRed : Brushes.DarkGreen;
    }

    protected override void OnLoaded(RoutedEventArgs e)
    {
        base.OnLoaded(e);
        if (ReferenceEquals(WorkspaceTabs.SelectedItem, NativeMonacoTab))
        {
            _ = EnsureNativeMonacoLoadedAsync(forceReload: false);
        }
        else if (ReferenceEquals(
            WorkspaceTabs.SelectedItem,
            NativeTradingViewTab))
        {
            _ = EnsureNativeTradingViewLoadedAsync(forceReload: false);
        }
    }

    protected override async void OnClosed(EventArgs e)
    {
        _nativeMonacoCancellation?.Cancel();
        _nativeMonacoCancellation?.Dispose();
        _nativeMonacoCancellation = null;
        _nativeTradingViewDiagnosticsTimer.Stop();
        _nativeTradingViewCancellation?.Cancel();
        _nativeTradingViewCancellation?.Dispose();
        _nativeTradingViewCancellation = null;
#if WEBSCENE_CLEARSCRIPT_V8
        _v8PreparationCancellation?.Cancel();
        _v8PreparationCancellation?.Dispose();
        _v8PreparationCancellation = null;
        StopV8PreparationIndicator();
#endif
        DisposeSessions();
#if WEBSCENE_CLEARSCRIPT_V8
        _v8SharedCache.Clear();
#endif
        await NativeMonacoHost.DisposeAsync();
        await NativeTradingViewHost.DisposeAsync();
        base.OnClosed(e);
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e)
    {
        base.OnSizeChanged(e);
    }

    private static List<Preset> CreatePresets()
    {
        return new List<Preset>
        {
            new Preset(
                "Welcome Panel",
                """
<Border xmlns="https://github.com/avaloniaui"
        Padding="20"
        Background="#f7f9fc"
        BorderBrush="#d9e2f1"
        BorderThickness="1"
        CornerRadius="8">
  <StackPanel Spacing="10">
    <TextBlock Name="title"
               Text="WebScene.Backend.Avalonia"
               Foreground="#1f2937"
               FontSize="22"
               FontWeight="SemiBold" />
    <TextBlock Text="Play with DOM-style APIs directly inside Avalonia."
               Foreground="#475569" />
    <Button Name="mainButton"
            Content="Click me"
            HorizontalAlignment="Left"
            Padding="16,8"
            Background="#2563eb"
            Foreground="White"
            CornerRadius="4" />
  </StackPanel>
</Border>
""",
                """
const header = document.getElementById('title');
const button = document.getElementById('mainButton');

button.addEventListener('click', () => {
  const stamp = new Date().toLocaleTimeString();
  header.textContent = `Button clicked at ${stamp}`;
});

header.textContent = 'JavaScript.Avalonia – Playground';
"""),
            new Preset(
                "Text nodes",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="8">
    <TextBlock Text="document.createTextNode demo" FontWeight="SemiBold" />
    <StackPanel Name="textContainer" Spacing="4" />
    <Button Name="updateText" Content="Update text node" HorizontalAlignment="Left" />
  </StackPanel>
</Border>
""",
                """
const container = document.getElementById('textContainer');
const update = document.getElementById('updateText');

const greeting = document.createTextNode('Hello from createTextNode');
const literals = document.createTextNode('<StackPanel /> stays literal & safe');

container.appendChild(greeting);
container.appendChild(literals);

update.addEventListener('click', () => {
  greeting.data = `Updated at ${new Date().toLocaleTimeString()}`;
  const history = document.createTextNode(`History entry ${container.childElementCount + 1}`);
  container.appendChild(history);
});
"""),
            new Preset(
                "Child manipulation",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="8">
    <TextBlock Text="appendChild / insertBefore / removeChild / replaceChild" FontWeight="SemiBold" />
    <StackPanel Name="list" Spacing="4">
      <TextBlock Text="Alpha" />
      <TextBlock Text="Charlie" />
    </StackPanel>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="insert" Content="Insert Bravo" />
      <Button Name="replace" Content="Replace Charlie" />
      <Button Name="remove" Content="Remove Alpha" />
      <Button Name="append" Content="Append Delta" />
    </StackPanel>
  </StackPanel>
</Border>
""",
                """
const list = document.getElementById('list');
const buttons = {
  insert: document.getElementById('insert'),
  replace: document.getElementById('replace'),
  remove: document.getElementById('remove'),
  append: document.getElementById('append')
};

const nodes = {
  alpha: list.children[0],
  charlie: list.children[1],
  bravo: document.createElement('TextBlock'),
  delta: document.createElement('TextBlock')
};

nodes.bravo.textContent = 'Bravo';
nodes.delta.textContent = 'Delta';

buttons.insert.addEventListener('click', () => {
  if (!nodes.bravo.parentElement) {
    list.insertBefore(nodes.bravo, nodes.charlie);
  }
});

buttons.replace.addEventListener('click', () => {
  const wrapper = document.createElement('Border');
  wrapper.setAttribute('Padding', '4');
  wrapper.setAttribute('Background', '#f5f5f5');
  const label = document.createElement('TextBlock');
  label.textContent = 'Charlie (wrapped)';
  wrapper.appendChild(label);
  list.replaceChild(wrapper, nodes.charlie);
  nodes.charlie = wrapper;
});

buttons.remove.addEventListener('click', () => {
  if (nodes.alpha && nodes.alpha.parentElement) {
    list.removeChild(nodes.alpha);
  }
});

buttons.append.addEventListener('click', () => {
  if (!nodes.delta.parentElement) {
    list.appendChild(nodes.delta);
  }
});
"""),
            new Preset(
                "Avalonia properties",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="12">
    <Border Name="sampleBorder"
            Width="200"
            Height="120"
            HorizontalAlignment="Left"
            CornerRadius="8"
            Background="#2b2d42">
      <TextBlock Name="sampleText"
                 Text="Click the buttons to update properties"
                 TextWrapping="Wrap"
                 Foreground="White"
                 HorizontalAlignment="Center"
                 VerticalAlignment="Center"
                 Margin="12" />
    </Border>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="brushBtn" Content="Set Accent Colors" />
      <Button Name="thicknessBtn" Content="Set Padding" />
      <Button Name="radiusBtn" Content="Set CornerRadius" />
      <Button Name="resetBtn" Content="Reset" />
    </StackPanel>
  </StackPanel>
</Border>
""",
                """
const border = document.getElementById('sampleBorder');
const text = document.getElementById('sampleText');
const brushBtn = document.getElementById('brushBtn');
const thicknessBtn = document.getElementById('thicknessBtn');
const radiusBtn = document.getElementById('radiusBtn');
const resetBtn = document.getElementById('resetBtn');

brushBtn.addEventListener('click', () => {
  border.setAttribute('background', '#ff7b7b');
  border.setAttribute('border-brush', '#feb47b');
  text.style.setProperty('foreground', '#1b1b1d');
});

thicknessBtn.addEventListener('click', () => {
  border.setAttribute('padding', '24,12');
  border.setAttribute('border-thickness', '2');
  border.setAttribute('border-brush', '#1b1b1d');
});

radiusBtn.addEventListener('click', () => {
  border.setAttribute('corner-radius', '0,24,0,24');
});

resetBtn.addEventListener('click', () => {
  border.setAttribute('background', '#2b2d42');
  border.setAttribute('padding', '0');
  border.setAttribute('border-thickness', '0');
  border.setAttribute('border-brush', 'Transparent');
  border.setAttribute('corner-radius', '8');
  text.style.setProperty('foreground', 'White');
});
"""),
            new Preset(
                "Layout inspector",
                """
<StackPanel xmlns="https://github.com/avaloniaui"
            Margin="20"
            Spacing="12">
  <Border Name="card"
          Width="220"
          Padding="12"
          Background="#FFEEF2FF"
          BorderBrush="#FF537FE7"
          BorderThickness="2"
          CornerRadius="6">
    <StackPanel Spacing="6">
      <TextBlock Text="Layout &amp; Style Metrics" FontWeight="SemiBold" />
      <ScrollViewer Name="sampleScroll"
                    Height="90"
                    HorizontalScrollBarVisibility="Disabled">
        <StackPanel Name="scrollItems" Spacing="4">
          <TextBlock Text="Item 1" />
          <TextBlock Text="Item 2" />
          <TextBlock Text="Item 3" />
          <TextBlock Text="Item 4" />
        </StackPanel>
      </ScrollViewer>
    </StackPanel>
  </Border>
  <StackPanel Orientation="Horizontal" Spacing="8">
    <Button Name="addItem" Content="Add Item" />
    <Button Name="toggleAccent" Content="Toggle Accent" />
  </StackPanel>
  <TextBlock Name="output"
             Text="Metrics pending..."
             TextWrapping="Wrap"
             FontFamily="Consolas, Courier New, monospace" />
</StackPanel>
""",
                """
const card = document.getElementById('card');
const scroll = document.getElementById('sampleScroll');
const output = document.getElementById('output');
const list = document.getElementById('scrollItems');
const addItemButton = document.getElementById('addItem');
const toggleAccentButton = document.getElementById('toggleAccent');

for (let i = 5; i <= 12; i++) {
  const item = document.createElement('TextBlock');
  item.textContent = `Item ${i}`;
  list.appendChild(item);
}

scroll.scrollTop = 30;

const log = [];

function computeMetrics() {
  const style = window.getComputedStyle(card);
  return [
    `offset: (${Math.round(card.offsetLeft)}, ${Math.round(card.offsetTop)})`,
    `client: ${Math.round(card.clientWidth)} x ${Math.round(card.clientHeight)}`,
    `padding: ${style.getPropertyValue('padding')}`,
    `background: ${style.getPropertyValue('background-color')}`,
    `scroll viewport: ${Math.round(scroll.clientWidth)} x ${Math.round(scroll.clientHeight)}`,
    `scroll extent: ${Math.round(scroll.scrollWidth)} x ${Math.round(scroll.scrollHeight)}`,
    `scroll position: ${Math.round(scroll.scrollLeft)}, ${Math.round(scroll.scrollTop)}`
  ];
}

function render() {
  const metrics = computeMetrics();
  const history = log.slice(-5);
  output.textContent = metrics.concat(history).join('\n');
}

const observer = new MutationObserver(records => {
  records.forEach(r => {
    if (r.type === 'childList') {
      log.push(`childList ➜ +${r.addedNodes.length} / -${r.removedNodes.length}`);
    } else if (r.type === 'attributes') {
      log.push(`attribute ➜ ${r.attributeName} (old: ${r.oldValue ?? 'null'})`);
    }
  });
  render();
});

observer.observe(card, { attributes: true, attributeOldValue: true });
observer.observe(list, { childList: true });

addItemButton.addEventListener('click', () => {
  const item = document.createElement('TextBlock');
  item.textContent = `Item ${list.childNodes.length + 1}`;
  list.appendChild(item);
  render();
});

let accent = false;
toggleAccentButton.addEventListener('click', () => {
  accent = !accent;
  card.setAttribute('background', accent ? '#FFF4F0FF' : '#FFEEF2FF');
  card.setAttribute('border-brush', accent ? '#FF414BB2' : '#FF537FE7');
  render();
});

render();
"""),
            new Preset(
                "ClassList & Dataset",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <Border Name="panel" Padding="16" Background="#FFEFD5">
    <Border.Styles>
      <Style Selector="Border.highlight">
        <Setter Property="Background" Value="#FFF2B6" />
        <Setter Property="BorderBrush" Value="#D88400" />
        <Setter Property="BorderThickness" Value="2" />
      </Style>
    </Border.Styles>
    <StackPanel Spacing="8">
      <TextBlock Name="status" Text="Highlight is off" />
      <Button Name="toggle" Content="Toggle highlight" HorizontalAlignment="Left" />
    </StackPanel>
  </Border>
</Border>
""",
                """
const panel = document.getElementById('panel');
panel.dataset.info = 'sample-panel';

const toggle = document.getElementById('toggle');
const status = document.getElementById('status');

toggle.addEventListener('click', () => {
  const active = panel.classList.toggle('highlight');
  status.textContent = active ? 'Highlight is on' : 'Highlight is off';
  panel.setAttribute('title', `dataset: ${panel.dataset.info}`);
});
"""),
            new Preset(
                "Pointer tracking",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="12">
  <StackPanel Spacing="12">
    <Border Name="surface"
            Height="200"
            Background="#1f6feb"
            CornerRadius="6"
            ToolTip.Tip="Move the pointer here">
      <TextBlock Name="coords"
                 HorizontalAlignment="Center"
                 VerticalAlignment="Center"
                 FontSize="18"
                 Foreground="White"
                 Text="Move pointer" />
    </Border>
    <TextBlock Name="log" Text="" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('surface');
const coords = document.getElementById('coords');
const log = document.getElementById('log');

surface.addEventListener('pointermove', info => {
  coords.textContent = `x: ${info.x.toFixed(0)}, y: ${info.y.toFixed(0)}`;
});

surface.addEventListener('pointerdown', info => {
  log.textContent = `Pointer button: ${info.button}`;
  info.stopPropagation();
});
"""),
            new Preset(
                "Events & head",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="12">
    <TextBlock Text="Capture, passive, and custom events" FontWeight="SemiBold" />
    <Border Name="outer" Background="#f2f4ff" Padding="12">
      <Border Name="inner" Background="#d7e2ff" Padding="12">
        <StackPanel Spacing="8">
          <Button Name="trigger" Content="Dispatch custom event" HorizontalAlignment="Left" />
          <TextBlock Name="info" FontWeight="Bold" TextWrapping="Wrap" />
          <TextBlock Name="log" TextWrapping="Wrap" />
        </StackPanel>
      </Border>
    </Border>
  </StackPanel>
</Border>
""",
                """
const outer = document.getElementById('outer');
const inner = document.getElementById('inner');
const trigger = document.getElementById('trigger');
const info = document.getElementById('info');
const log = document.getElementById('log');

function write(message) {
  log.textContent += message + '\n';
}

document.title = 'JavaScript.Avalonia events';
const headEntry = document.createElement('TextBlock');
headEntry.textContent = 'Head updated at ' + new Date().toLocaleTimeString();
document.head.appendChild(headEntry);

const body = document.body;
const firstChildName = body.firstChild ? body.firstChild.nodeName : 'none';
info.textContent = `documentElement: ${document.documentElement.nodeName}, body first child: ${firstChildName}`;

document.addEventListener('pointerdown', evt => {
  write(`document capture → phase=${evt.eventPhase}`);
}, { capture: true });

document.addEventListener('pointerdown', evt => {
  write(`document bubble → phase=${evt.eventPhase}`);
});

outer.addEventListener('pointerdown', evt => {
  write(`outer capture → currentTarget=${evt.currentTarget.nodeName}`);
}, { capture: true });

outer.addEventListener('pointerdown', evt => {
  write(`outer bubble → defaultPrevented=${evt.defaultPrevented}`);
});

inner.addEventListener('pointerdown', evt => {
  write('inner passive listener (preventDefault ignored)');
  evt.preventDefault();
}, { passive: true });

inner.addEventListener('pointerdown', evt => {
  write(`inner bubble → pointerType=${evt.pointerType}, button=${evt.button}`);
});

inner.addEventListener('custom', evt => {
  write(`custom event detail=${evt.detail}`);
  evt.preventDefault();
});

trigger.addEventListener('click', () => {
  log.textContent = '';
  const synthetic = { type: 'custom', bubbles: true, detail: Date.now() };
  const result = inner.dispatchEvent(synthetic);
          write(`dispatchEvent returned ${result}, defaultPrevented=${synthetic.defaultPrevented}`);
});
"""),
            new Preset(
                "Custom events",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="20" Background="#f8fafc" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="CustomEvent constructors" FontWeight="SemiBold" FontSize="18" Foreground="#1f2937" />
    <Border Background="#eef2ff" Padding="12" CornerRadius="6">
      <StackPanel Name="eventArea" Spacing="8">
        <TextBlock Text="Event log" FontWeight="SemiBold" />
        <Border Background="White" Padding="8" CornerRadius="4" BorderBrush="#c7d2fe" BorderThickness="1">
          <TextBlock Name="eventLog" TextWrapping="Wrap" Foreground="#1e293b" />
        </Border>
        <StackPanel Orientation="Horizontal" Spacing="8">
          <Button Name="fireCustom" Content="CustomEvent" Padding="14,8" />
          <Button Name="fireNative" Content="Event" Padding="14,8" />
          <Button Name="clearLog" Content="Clear" Padding="14,8" />
        </StackPanel>
      </StackPanel>
    </Border>
    <Border Name="syntheticNode" Background="#fde68a" Padding="10" CornerRadius="6" BorderBrush="#f59e0b" BorderThickness="1">
      <TextBlock Text="Synthetic path node" Foreground="#92400e" />
    </Border>
  </StackPanel>
</Border>
""",
                """
const area = document.getElementById('eventArea');
const logBlock = document.getElementById('eventLog');
const helper = document.getElementById('syntheticNode');
const fireCustom = document.getElementById('fireCustom');
const fireNative = document.getElementById('fireNative');
const clearLog = document.getElementById('clearLog');

const appendLog = message => {
  const stamp = new Date().toLocaleTimeString();
  const existing = logBlock.textContent ? `${logBlock.textContent}\n` : '';
  logBlock.textContent = `${existing}[${stamp}] ${message}`;
};

area.addEventListener('notify', evt => {
  appendLog(`capture → target: ${evt.target?.nodeName}, current: ${evt.currentTarget?.nodeName}`);
}, { capture: true });

area.addEventListener('notify', evt => {
  appendLog(`bubble → detail: ${JSON.stringify(evt.detail)}, from synthetic: ${evt.eventPhase === 3}`);
});

helper.addEventListener('notify', evt => {
  appendLog(`synthetic node observed phase=${evt.eventPhase}`);
}, { capture: true });

fireCustom.addEventListener('click', () => {
  const evt = new CustomEvent('notify', { detail: { count: logBlock.textContent.split('\n').filter(Boolean).length + 1 }, bubbles: true, path: [helper] });
  area.dispatchEvent(evt);
});

fireNative.addEventListener('click', () => {
  const evt = new Event('notify', { bubbles: true });
  area.dispatchEvent(evt);
});

clearLog.addEventListener('click', () => {
  logBlock.textContent = '';
});
"""),
            new Preset(
                "Timers & animation",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#101820">
  <StackPanel Spacing="12">
    <ProgressBar Name="progress" Minimum="0" Maximum="100" Height="8" />
    <TextBlock Name="timerLabel" Foreground="#f0f0f0" />
  </StackPanel>
</Border>
""",
                """
const bar = document.getElementById('progress');
const label = document.getElementById('timerLabel');
let value = 0;

function tick() {
  value = (value + 1) % 101;
  bar.setAttribute('value', value);
  label.textContent = `requestAnimationFrame progress: ${value}%`;
  window.requestAnimationFrame(tick);
}

tick();

let counter = 0;
const handle = window.setInterval(() => {
  counter++;
  console.info(`Interval fired ${counter} times`);
  if (counter >= 5) {
    window.clearInterval(handle);
    label.textContent += ' | interval stopped';
  }
}, 500);
"""),
            new Preset(
                "Canvas 2D",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d4dbe5" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="CanvasRenderingContext2D demo" FontWeight="SemiBold" Foreground="#0f172a" />
    <Border Name="paintSurface" Width="520" Height="280" Background="#ffffff" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="drawCanvas" Content="Draw scene" />
      <Button Name="animateCanvas" Content="Animate" />
      <Button Name="clearCanvas" Content="Clear" />
    </StackPanel>
    <TextBlock Name="canvasStatus" Foreground="#334155" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('paintSurface');
const ctx = surface.getContext('2d');
const drawBtn = document.getElementById('drawCanvas');
const animateBtn = document.getElementById('animateCanvas');
const clearBtn = document.getElementById('clearCanvas');
const status = document.getElementById('canvasStatus');

let animationHandle = 0;
let angle = 0;

function drawScene() {
  const width = surface.offsetWidth;
  const height = surface.offsetHeight;
  ctx.clearRect(0, 0, width, height);

  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, width, height);

  ctx.fillStyle = '#2563eb';
  ctx.fillRect(28, 28, 160, 100);

  ctx.strokeStyle = '#f97316';
  ctx.lineWidth = 6;
  ctx.beginPath();
  ctx.moveTo(220, 44);
  ctx.lineTo(340, 140);
  ctx.lineTo(220, 184);
  ctx.closePath();
  ctx.stroke();

  ctx.fillStyle = '#10b981';
  ctx.beginPath();
  ctx.arc(400, 120, 48, 0, Math.PI * 2, false);
  ctx.fill();

  ctx.fillStyle = '#0f172a';
  ctx.font = '20px Segoe UI';
  ctx.fillText('CanvasRenderingContext2D from Avalonia', 28, height - 36);

  status.textContent = 'Scene rendered using the Avalonia drawing context.';
}

function animate() {
  const width = surface.offsetWidth;
  const height = surface.offsetHeight;
  ctx.clearRect(0, 0, width, height);

  ctx.fillStyle = '#f1f5f9';
  ctx.fillRect(0, 0, width, height);

  const cx = width / 2;
  const cy = height / 2;
  const radius = Math.min(width, height) / 4;

  ctx.strokeStyle = '#475569';
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2, false);
  ctx.stroke();

  const orbitX = cx + Math.cos(angle) * radius;
  const orbitY = cy + Math.sin(angle) * radius;

  ctx.fillStyle = '#f97316';
  ctx.beginPath();
  ctx.arc(orbitX, orbitY, 16, 0, Math.PI * 2, false);
  ctx.fill();

  angle += 0.1;
  animationHandle = window.requestAnimationFrame(animate);
}

drawBtn.addEventListener('click', () => {
  if (animationHandle) {
    window.cancelAnimationFrame(animationHandle);
    animationHandle = 0;
  }
  drawScene();
});

animateBtn.addEventListener('click', () => {
  if (animationHandle) {
    return;
  }
  status.textContent = 'Animating with requestAnimationFrame...';
  angle = 0;
  animationHandle = window.requestAnimationFrame(animate);
});

clearBtn.addEventListener('click', () => {
  if (animationHandle) {
    window.cancelAnimationFrame(animationHandle);
    animationHandle = 0;
  }
  ctx.clearRect(0, 0, surface.offsetWidth, surface.offsetHeight);
  status.textContent = 'Canvas cleared.';
});

drawScene();
"""
            ),
            new Preset(
                "Canvas 2D + bezier.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#ffffff" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with bezier-js" FontWeight="SemiBold" Foreground="#1f2937" />
    <Border Name="librarySurface" Width="540" Height="300" Background="#f8fafc" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="libraryDraw" Content="Render curve" />
      <Button Name="libraryRandom" Content="Randomise control points" />
    </StackPanel>
    <TextBlock Name="libraryStatus" Foreground="#475569" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('librarySurface');
const ctx = surface.getContext('2d');
const drawBtn = document.getElementById('libraryDraw');
const randomBtn = document.getElementById('libraryRandom');
const status = document.getElementById('libraryStatus');

let Bezier;
try {
  const bezierModule = require('https://cdn.jsdelivr.net/npm/bezier-js@6.1.3/dist/bezier.cjs');
  Bezier = bezierModule?.Bezier ?? bezierModule?.default ?? bezierModule;
  if (typeof Bezier !== 'function') {
    throw new Error('bezier-js module did not expose a constructor');
  }
} catch (error) {
  const message = `Failed to load bezier-js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

function createRandomCurve() {
  const w = surface.offsetWidth;
  const h = surface.offsetHeight;
  const margin = 32;
  return new Bezier(
    margin,
    h - margin,
    w * 0.25 + Math.random() * w * 0.2,
    margin + Math.random() * (h - 2 * margin),
    w * 0.6 + Math.random() * w * 0.2,
    margin + Math.random() * (h - 2 * margin),
    w - margin,
    margin
  );
}

let curve = createRandomCurve();

function renderCurve() {
  const w = surface.offsetWidth;
  const h = surface.offsetHeight;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = '#f8fafc';
  ctx.fillRect(0, 0, w, h);

  ctx.lineWidth = 3;
  ctx.strokeStyle = '#1f2937';
  ctx.beginPath();
  const primary = curve.getLUT(120);
  ctx.moveTo(primary[0].x, primary[0].y);
  for (let i = 1; i < primary.length; i++) {
    ctx.lineTo(primary[i].x, primary[i].y);
  }
  ctx.stroke();

  ctx.lineWidth = 2;
  ctx.strokeStyle = '#3b82f6';
  const offsets = curve.offset(18);
  offsets.forEach(offsetCurve => {
    const points = offsetCurve.getLUT(80);
    ctx.beginPath();
    ctx.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; i++) {
      ctx.lineTo(points[i].x, points[i].y);
    }
    ctx.stroke();
  });

  ctx.lineWidth = 1;
  ctx.strokeStyle = '#94a3b8';
  ctx.beginPath();
  ctx.moveTo(curve.points[0].x, curve.points[0].y);
  for (let i = 1; i < curve.points.length; i++) {
    ctx.lineTo(curve.points[i].x, curve.points[i].y);
  }
  ctx.stroke();

  ctx.fillStyle = '#ef4444';
  curve.points.forEach(pt => {
    ctx.beginPath();
    ctx.arc(pt.x, pt.y, 4, 0, Math.PI * 2, false);
    ctx.fill();
  });
}

function renderAndReport(message) {
  renderCurve();
  if (status) {
    status.textContent = message ?? 'Curves rendered with bezier-js';
  }
}

const interactionRadius = 16;
let activePointIndex = -1;

const getPointerPosition = evt => ({
  x: evt?.x ?? 0,
  y: evt?.y ?? 0
});

const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

const distanceSquared = (a, b) => {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  return dx * dx + dy * dy;
};

function findClosestPoint(position, radius = interactionRadius) {
  let index = -1;
  let minDist = radius * radius;
  curve.points.forEach((pt, i) => {
    const dist = distanceSquared(position, pt);
    if (dist <= minDist) {
      minDist = dist;
      index = i;
    }
  });
  return index;
}

function updateActivePoint(position, message) {
  if (activePointIndex === -1) {
    return;
  }

  const pt = curve.points[activePointIndex];
  pt.x = clamp(position.x, 0, surface.offsetWidth);
  pt.y = clamp(position.y, 0, surface.offsetHeight);
  curve.update();
  renderAndReport(message);
}

function handlePointerDown(evt) {
  const position = getPointerPosition(evt);
  const index = findClosestPoint(position);
  if (index === -1) {
    return;
  }

  evt.preventDefault?.();
  activePointIndex = index;
  updateActivePoint(position, 'Dragging control point');
  surface.setPointerCapture?.(evt.pointerId);
}

function handlePointerMove(evt) {
  if (activePointIndex === -1) {
    return;
  }

  evt.preventDefault?.();
  updateActivePoint(getPointerPosition(evt), 'Dragging control point');
}

function handlePointerUp(evt) {
  if (activePointIndex === -1) {
    return;
  }

  activePointIndex = -1;
  surface.releasePointerCapture?.(evt?.pointerId);
  renderAndReport('Control point updated');
}

surface.addEventListener('pointerdown', handlePointerDown);
surface.addEventListener('pointermove', handlePointerMove);
surface.addEventListener('pointerup', handlePointerUp);
surface.addEventListener('pointerleave', handlePointerUp);

drawBtn.addEventListener('click', () => renderAndReport('Bezier curve redrawn'));

randomBtn.addEventListener('click', () => {
  activePointIndex = -1;
  curve = createRandomCurve();
  renderAndReport('Generated new curve — drag the red points to edit');
});

renderAndReport('Initial curve generated with bezier-js. Drag the red points to edit.');

"""
            ),
            new Preset(
                "Canvas 2D + rough.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#ffffff" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with rough.js" FontWeight="SemiBold" Foreground="#111827" />
    <Border Name="roughSurface" Width="540" Height="300" Background="#f8fafc" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
      <TextBlock Text="Roughness:" VerticalAlignment="Center" />
      <Slider Name="roughnessSlider" Minimum="0.5" Maximum="3.0" Value="1.0" Width="160" />
      <Button Name="roughRender" Content="Render scene" />
      <Button Name="roughPalette" Content="New palette" />
    </StackPanel>
    <TextBlock Name="roughStatus" Foreground="#475569" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('roughSurface');
const ctx = surface.getContext('2d');
const slider = document.getElementById('roughnessSlider');
const renderBtn = document.getElementById('roughRender');
const paletteBtn = document.getElementById('roughPalette');
const status = document.getElementById('roughStatus');

let roughModule;
try {
  const mod = require('https://cdn.jsdelivr.net/npm/roughjs@4.6.6/bundled/rough.cjs.js');
  roughModule = mod?.canvas ? mod : (mod?.default?.canvas ? mod.default : mod);
  if (typeof roughModule?.canvas !== 'function') {
    throw new Error('rough.js canvas factory not found');
  }
} catch (error) {
  const message = `Failed to load rough.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const palettes = [
  ['#fde68a', '#f97316', '#1f2937'],
  ['#fbcfe8', '#ec4899', '#312e81'],
  ['#bfdbfe', '#1d4ed8', '#0f172a'],
  ['#dcfce7', '#16a34a', '#064e3b'],
  ['#fee2e2', '#ef4444', '#7f1d1d']
];
let paletteIndex = 0;

const getRoughness = () => {
  if (!slider) {
    return 1;
  }

  const raw = slider.Value ?? slider.value ?? slider.getAttribute?.('value') ?? slider.getAttribute?.('Value');
  const numeric = typeof raw === 'number' ? raw : parseFloat(raw ?? '1');
  return Number.isFinite(numeric) ? numeric : 1;
};

const pickPalette = () => {
  paletteIndex = Math.floor(Math.random() * palettes.length);
  return palettes[paletteIndex];
};

let currentPalette = pickPalette();

function renderScene(message) {
  const w = surface.offsetWidth;
  const h = surface.offsetHeight;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = '#f8fafc';
  ctx.fillRect(0, 0, w, h);

  const rough = roughModule.canvas(surface);
  const roughness = getRoughness();
  const [fill1, stroke1, accent] = currentPalette;

  rough.rectangle(36, 28, 180, 120, {
    roughness,
    fill: fill1,
    stroke: stroke1,
    fillStyle: 'hachure',
    strokeWidth: 2
  });

  rough.circle(320, 110, 120, {
    roughness,
    fill: accent,
    fillStyle: 'zigzag',
    stroke: stroke1,
    strokeWidth: 2
  });

  rough.linearPath([
    [60, 220],
    [160, 260],
    [260, 200],
    [360, 260],
    [460, 200]
  ], {
    roughness: roughness * 0.8,
    stroke: stroke1,
    bowing: 1.2,
    strokeWidth: 3
  });

  rough.rectangle(280, 180, 180, 90, {
    roughness: roughness * 1.1,
    fill: fill1,
    fillStyle: 'cross-hatch',
    stroke: accent,
    strokeWidth: 2
  });

  if (status) {
    const text = message ?? `Rendered with roughness ${roughness.toFixed(2)}`;
    status.textContent = text;
  }
}

const refreshFromSlider = () => {
  renderScene(`Rendered with roughness ${getRoughness().toFixed(2)}`);
};

renderBtn.addEventListener('click', () => renderScene('Manual render using rough.js'));

paletteBtn.addEventListener('click', () => {
  currentPalette = pickPalette();
  renderScene('Random palette applied');
});

slider.addEventListener('pointerup', refreshFromSlider);
slider.addEventListener('pointermove', evt => {
  if (evt.buttons) {
    refreshFromSlider();
  }
});

slider.addEventListener('keydown', refreshFromSlider);
slider.addEventListener('wheel', evt => {
  evt.preventDefault?.();
  refreshFromSlider();
}, { passive: false });

slider.addEventListener('valuechanged', refreshFromSlider);
slider.addEventListener('input', refreshFromSlider);

renderScene('Sketch scene rendered with rough.js — adjust the slider or change palette');
"""
            ),
            new Preset(
                "Canvas 2D + TypeScript rough.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with rough.js (TypeScript module)" FontWeight="SemiBold" Foreground="#1f2937" />
    <TextBlock Text="The script imports a local TypeScript helper that pulls rough.js from esm.sh and drives the canvas." TextWrapping="Wrap" Foreground="#475569" />
    <Border Name="tsRoughSurface" Width="560" Height="320" Background="#ffffff" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="6" />
    <StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
      <TextBlock Text="Roughness:" VerticalAlignment="Center" />
      <Slider Name="tsRoughness" Minimum="0.5" Maximum="2.5" Value="1.0" Width="160" />
      <Button Name="tsRoughRender" Content="Render once" />
      <Button Name="tsRoughAnimate" Content="Toggle animation" />
      <Button Name="tsRoughPalette" Content="Next palette" />
    </StackPanel>
    <TextBlock Name="tsRoughStatus" Foreground="#334155" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('tsRoughSurface');
const slider = document.getElementById('tsRoughness');
const renderBtn = document.getElementById('tsRoughRender');
const animateBtn = document.getElementById('tsRoughAnimate');
const paletteBtn = document.getElementById('tsRoughPalette');
const status = document.getElementById('tsRoughStatus');

const { createRoughSketch } = require('./Scripts/rough-sketch.ts');

if (!surface) {
  throw new Error('tsRoughSurface element not found');
}

const getSliderValue = () => {
  if (!slider) {
    return 1;
  }

  const raw = slider.Value ?? slider.value ?? slider.getAttribute?.('value') ?? slider.getAttribute?.('Value');
  const numeric = typeof raw === 'number' ? raw : parseFloat(raw ?? '1');
  return Number.isFinite(numeric) ? numeric : 1;
};

const controller = createRoughSketch({
  surface,
  initialRoughness: getSliderValue(),
  notify: message => {
    if (status) {
      status.textContent = message;
    }
  }
});

let isAnimating = false;

const renderStatic = () => {
  controller.stop();
  controller.setRoughness(getSliderValue());
  controller.render();
};

renderBtn?.addEventListener('click', () => {
  isAnimating = false;
  renderStatic();
});

animateBtn?.addEventListener('click', () => {
  isAnimating = !isAnimating;
  controller.stop();
  controller.setRoughness(getSliderValue());
  if (isAnimating) {
    controller.start();
    if (status) {
      status.textContent = 'Animating canvas via TypeScript rough.js helper';
    }
  } else {
    if (status) {
      status.textContent = 'Animation paused';
    }
  }
});

paletteBtn?.addEventListener('click', () => {
  const palette = controller.cyclePalette();
  if (status) {
    status.textContent = `Palette switched to ${palette.join(', ')}`;
  }
  if (isAnimating) {
    controller.start();
  }
});

const handleSliderChange = () => {
  controller.setRoughness(getSliderValue());
  if (!isAnimating) {
    controller.render();
  }
};

slider?.addEventListener('pointerup', handleSliderChange);
slider?.addEventListener('input', handleSliderChange);
slider?.addEventListener('valuechanged', handleSliderChange);
slider?.addEventListener('keydown', evt => {
  if (evt.key === 'ArrowLeft' || evt.key === 'ArrowRight') {
    handleSliderChange();
  }
});

controller.render();
if (status) {
  status.textContent = 'TypeScript helper loaded rough.js and rendered the initial scene';
}
"""
            ),
            new Preset(
                "TypeScript + xterm.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="xterm.js headless (TypeScript)" FontWeight="SemiBold" FontSize="18" Foreground="#1f2937" />
    <TextBlock Text="Renders the @xterm/headless buffer onto a CanvasRenderingContext2D and routes non-built-in commands through the host OS shell." TextWrapping="Wrap" Foreground="#475569" />
    <Border Name="xtermSurface" Width="1120" Height="640" Background="#ffffff" BorderBrush="#cbd5e1" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
      <TextBox Name="xtermInput" Width="360" Watermark="Enter command (help, ls, mc, btop, clear)" />
      <Button Name="xtermSend" Content="Send" />
      <Button Name="xtermDemo" Content="Run demo" />
      <Button Name="xtermClear" Content="Clear" />
    </StackPanel>
    <TextBlock Name="xtermStatus" Foreground="#334155" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
try {
  require('./Scripts/xterm-demo.ts');
} catch (error) {
  const status = document.getElementById('xtermStatus');
  if (status) {
    status.textContent = `Failed to load xterm demo: ${error}`;
  }
  throw error;
}
"""
            ),
            new Preset(
                "JavaScript + xterm.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="xterm.js headless (JavaScript)" FontWeight="SemiBold" FontSize="18" Foreground="#1f2937" />
    <TextBlock Text="Loads the plain JavaScript helper that integrates the headless xterm.js bundle into the canvas renderer and host OS shell bridge." TextWrapping="Wrap" Foreground="#475569" />
    <Border Name="xtermJsSurface" Width="1120" Height="640" Background="#ffffff" BorderBrush="#cbd5e1" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center">
      <TextBox Name="xtermJsInput" Width="360" Watermark="Enter command (help, ls, mc, btop, clear)" />
      <Button Name="xtermJsSend" Content="Send" />
      <Button Name="xtermJsDemo" Content="Run demo" />
      <Button Name="xtermJsClear" Content="Clear" />
    </StackPanel>
    <TextBlock Name="xtermJsStatus" Foreground="#334155" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
try {
  require('./Scripts/xterm-browser.js');
} catch (error) {
  const status = document.getElementById('xtermJsStatus');
  if (status) {
    status.textContent = `Failed to load JavaScript xterm demo: ${error}`;
  }
  throw error;
}
"""
            ),
            new Preset(
                "Canvas 2D + Chart.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with Chart.js" FontWeight="SemiBold" Foreground="#1f2937" />
    <Border Name="chartSurface" Width="540" Height="300" Background="#ffffff" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="chartRandomize" Content="Randomise data" />
      <Button Name="chartToggle" Content="Toggle line/bar" />
    </StackPanel>
    <TextBlock Name="chartStatus" Foreground="#475569" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('chartSurface');
const randomBtn = document.getElementById('chartRandomize');
const toggleBtn = document.getElementById('chartToggle');
const status = document.getElementById('chartStatus');

if (!surface) {
  throw new Error('chartSurface element not found');
}

const context = surface.getContext('2d');
if (!context) {
  throw new Error('CanvasRenderingContext2D unavailable for Chart.js demo');
}

let logicalWidth = surface.offsetWidth;
let logicalHeight = surface.offsetHeight;
const updateLogicalSize = (width, height) => {
  if (typeof width === 'number' && !Number.isNaN(width)) {
    logicalWidth = width;
  }
  if (typeof height === 'number' && !Number.isNaN(height)) {
    logicalHeight = height;
  }
};

const attributes = new Map([
  ['width', String(logicalWidth)],
  ['height', String(logicalHeight)]
]);

const styleState = {
  display: 'block',
  width: `${surface.offsetWidth}px`,
  height: `${surface.offsetHeight}px`,
  boxSizing: 'border-box',
  getPropertyValue(property) {
    const key = String(property ?? '')
      .replace(/-([a-z])/g, (_, value) => value.toUpperCase());
    const raw = this[key];
    return raw == null ? '' : String(raw);
  }
};

const syncCanvasState = () => {
  attributes.set('width', String(logicalWidth));
  attributes.set('height', String(logicalHeight));
  styleState.width = `${logicalWidth}px`;
  styleState.height = `${logicalHeight}px`;
};

const canvasElement = {
  nodeName: 'CANVAS',
  style: styleState,
  ownerDocument: surface.ownerDocument ?? document,
  defaultView: typeof window !== 'undefined' ? window : undefined,
  parentNode: surface.parentNode ?? surface.parentElement ?? surface.ownerDocument?.body ?? null,
  parentElement: surface.parentElement ?? surface.parentNode ?? surface.ownerDocument?.body ?? null,
  getContext: () => context,
  addEventListener: () => {},
  removeEventListener: () => {},
  dispatchEvent: () => false,
  getAttribute: name => attributes.get(String(name ?? '').toLowerCase()) ?? null,
  setAttribute: (name, value) => {
    const key = String(name ?? '').toLowerCase();
    const text = value == null ? '' : String(value);
    attributes.set(key, text);
    if (key === 'width') {
      updateLogicalSize(Number(text), logicalHeight);
    } else if (key === 'height') {
      updateLogicalSize(logicalWidth, Number(text));
    }
    syncCanvasState();
  },
  removeAttribute: name => {
    const key = String(name ?? '').toLowerCase();
    attributes.delete(key);
    if (key === 'width') {
      updateLogicalSize(surface.offsetWidth, logicalHeight);
    } else if (key === 'height') {
      updateLogicalSize(logicalWidth, surface.offsetHeight);
    }
    syncCanvasState();
  },
  getBoundingClientRect: () => surface.getBoundingClientRect?.() ?? {
    width: surface.offsetWidth,
    height: surface.offsetHeight,
    top: 0,
    left: 0,
    right: surface.offsetWidth,
    bottom: surface.offsetHeight
  }
};

Object.defineProperties(canvasElement, {
  width: {
    configurable: true,
    enumerable: true,
    get: () => logicalWidth,
    set: value => updateLogicalSize(value, logicalHeight)
  },
  height: {
    configurable: true,
    enumerable: true,
    get: () => logicalHeight,
    set: value => updateLogicalSize(logicalWidth, value)
  },
  clientWidth: {
    configurable: true,
    enumerable: true,
    get: () => surface.offsetWidth
  },
  clientHeight: {
    configurable: true,
    enumerable: true,
    get: () => surface.offsetHeight
  },
  offsetWidth: {
    configurable: true,
    enumerable: true,
    get: () => surface.offsetWidth
  },
  offsetHeight: {
    configurable: true,
    enumerable: true,
    get: () => surface.offsetHeight
  }
});

syncCanvasState();

if (canvasElement.ownerDocument && typeof canvasElement.ownerDocument.defaultView === 'undefined' && typeof window !== 'undefined') {
  canvasElement.ownerDocument.defaultView = window;
}

context.canvas = canvasElement;

let chartModule;
try {
  chartModule = require('https://cdn.jsdelivr.net/npm/chart.js@4.4.3/dist/chart.umd.js');
} catch (error) {
  const message = `Failed to load Chart.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const Chart = chartModule?.Chart ?? chartModule?.default ?? chartModule;
if (typeof Chart !== 'function' && typeof Chart?.register !== 'function') {
  const message = 'Chart.js module did not expose a Chart constructor';
  if (status) {
    status.textContent = message;
  }
  throw new Error(message);
}

if (typeof Chart.register === 'function' && chartModule?.registerables) {
  Chart.register(...chartModule.registerables);
}

const BasePlatform = Chart?.BasicPlatform ?? chartModule?.BasicPlatform;
const AvaloniaChartPlatform = typeof BasePlatform === 'function'
  ? class extends BasePlatform {}
  : undefined;

const helpers = Chart?.helpers ?? chartModule?.helpers;
if (helpers?.canvas?.acquireContext && !helpers.canvas.__avaloniaPatched) {
  const originalAcquire = helpers.canvas.acquireContext.bind(helpers.canvas);
  helpers.canvas.acquireContext = (item, ...args) => {
    if (item === surface || item === canvasElement || item === context) {
      return context;
    }
    return originalAcquire(item, ...args);
  };
  helpers.canvas.__avaloniaPatched = true;
}

const labels = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
let dataPoints = labels.map(() => 40 + Math.round(Math.random() * 60));
let currentType = 'line';
let chart;

const buildGradient = () => {
  const gradientHeight = canvasElement.offsetHeight ?? canvasElement.height ?? logicalHeight;
  const gradient = context.createLinearGradient(0, 0, 0, gradientHeight);
  gradient.addColorStop(0, 'rgba(59, 130, 246, 0.35)');
  gradient.addColorStop(1, 'rgba(59, 130, 246, 0.08)');
  return gradient;
};

const createConfig = type => ({
  platform: AvaloniaChartPlatform,
  type,
  data: {
    labels,
    datasets: [
      {
        label: 'Active sessions',
        data: dataPoints,
        borderColor: '#2563eb',
        backgroundColor: type === 'bar' ? '#93c5fd' : buildGradient(),
        borderWidth: 2,
        fill: type !== 'bar',
        tension: 0.45,
        pointRadius: type === 'bar' ? 0 : 4,
        pointBackgroundColor: '#1d4ed8',
        hoverRadius: 6
      }
    ]
  },
  options: {
    responsive: false,
    animation: false,
    scales: {
      x: {
        ticks: { color: '#64748b' },
        grid: { color: 'rgba(148, 163, 184, 0.18)' }
      },
      y: {
        beginAtZero: true,
        suggestedMax: 120,
        ticks: { color: '#64748b' },
        grid: { color: 'rgba(148, 163, 184, 0.12)' }
      }
    },
    plugins: {
      legend: {
        labels: { color: '#0f172a' }
      },
      tooltip: {
        backgroundColor: '#0f172a',
        borderColor: '#1d4ed8',
        borderWidth: 1,
        padding: 12
      }
    }
  }
});

const report = message => {
  if (status) {
    status.textContent = message;
  }
};

const renderChart = type => {
  if (chart) {
    chart.destroy();
  }
  try {
    chart = new Chart(canvasElement, createConfig(type));
    report(`Chart.js ${(chartModule?.version ?? Chart?.version) ?? ''} rendering a ${type} chart`);
  } catch (error) {
    report(`Failed to create chart: ${error}`);
    console.error('Chart.js initialisation error', error);
    chart = null;
  }
};

const randomiseData = () => {
  dataPoints = labels.map(() => 25 + Math.round(Math.random() * 90));
  if (!chart) {
    return;
  }
  chart.data.datasets[0].data = dataPoints;
  if (currentType !== 'bar') {
    chart.data.datasets[0].backgroundColor = buildGradient();
  }
  chart.update();
  report('Dataset randomised');
};

randomBtn.addEventListener('click', () => {
  if (chart) {
    randomiseData();
  }
});

toggleBtn.addEventListener('click', () => {
  currentType = currentType === 'line' ? 'bar' : 'line';
  renderChart(currentType);
});

renderChart(currentType);
"""
            ),
            new Preset(
                "Canvas WebGL + Three.js",
                """
<Border xmlns="https://github.com/avaloniaui" xmlns:js="clr-namespace:JavaScript.Avalonia;assembly=WebScene.Backend.Avalonia" Padding="16" Background="#0f172a" BorderBrush="#1e293b" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas WebGL with Three.js" FontWeight="SemiBold" Foreground="#e5e7eb" />
    <TextBlock Text="Runs Three.js against JavaScript.Avalonia's browser-style WebGL context and renders the scene into the canvas surface." TextWrapping="Wrap" Foreground="#94a3b8" />
    <Border Background="#0b1220" BorderBrush="#334155" BorderThickness="1" CornerRadius="6" ClipToBounds="True">
      <js:CanvasOpenGlDrawingSurface Name="threeSurface" Width="720" Height="420" />
    </Border>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="threeToggle" Content="Pause rotation" />
      <Button Name="threeShuffle" Content="Shuffle colors" />
      <Button Name="threeReset" Content="Reset camera" />
    </StackPanel>
    <TextBlock Name="threeStatus" Foreground="#cbd5e1" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('threeSurface');
const toggleBtn = document.getElementById('threeToggle');
const shuffleBtn = document.getElementById('threeShuffle');
const resetBtn = document.getElementById('threeReset');
const status = document.getElementById('threeStatus');

if (!surface) {
  throw new Error('threeSurface element not found');
}

const gl = surface.getContext('webgl') || surface.getContext('experimental-webgl');
if (!gl) {
  throw new Error('Canvas WebGL context unavailable');
}

let threeModule;
try {
  threeModule = require('https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.min.js');
} catch (error) {
  const message = `Failed to load Three.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const THREE = threeModule?.REVISION ? threeModule : (typeof window !== 'undefined' ? window.THREE : undefined);
if (!THREE) {
  throw new Error('Three.js did not expose a renderer API');
}

const renderer = new THREE.WebGLRenderer({
  canvas: surface,
  context: gl,
  antialias: false,
  alpha: false
});
renderer.setPixelRatio(1);
renderer.setSize(surface.offsetWidth, surface.offsetHeight, false);
renderer.setClearColor(0x0b1220, 1);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(45, surface.offsetWidth / surface.offsetHeight, 0.1, 50);
camera.position.set(0, 1.2, 5.8);
camera.lookAt(0, 0, 0);

const group = new THREE.Group();
scene.add(group);

const cube = new THREE.Mesh(
  new THREE.BoxGeometry(1.45, 1.45, 1.45),
  new THREE.MeshBasicMaterial({ color: 0x38bdf8 })
);
cube.position.x = -1.25;
group.add(cube);

const prism = new THREE.Mesh(
  new THREE.ConeGeometry(0.9, 1.8, 5),
  new THREE.MeshBasicMaterial({ color: 0xf97316 })
);
prism.position.x = 1.25;
prism.rotation.z = 0.35;
group.add(prism);

const base = new THREE.Mesh(
  new THREE.PlaneGeometry(5.6, 2.2),
  new THREE.MeshBasicMaterial({ color: 0x1e293b })
);
base.position.y = -1.35;
base.rotation.x = -Math.PI / 2;
scene.add(base);

const palette = [0x38bdf8, 0xf97316, 0xa855f7, 0x22c55e, 0xf43f5e, 0xeab308];
let spinning = true;
let frameHandle = 0;

const report = message => {
  if (status) {
    status.textContent = `${message} Backend: ${gl.RenderBackend}; draw calls: ${gl.DrawCallCount}; triangles: ${gl.TriangleCount}; ${gl.LastDrawStatus}.`;
  }
};

const drawScene = () => {
  renderer.render(scene, camera);
  report(`Three.js ${THREE.REVISION} rendered via Canvas WebGL.`);
};

const tick = () => {
  frameHandle = 0;
  if (spinning) {
    group.rotation.x += 0.012;
    group.rotation.y += 0.018;
    drawScene();
    frameHandle = window.requestAnimationFrame(tick);
  }
};

toggleBtn.addEventListener('click', () => {
  spinning = !spinning;
  toggleBtn.textContent = spinning ? 'Pause rotation' : 'Resume rotation';
  if (spinning) {
    if (!frameHandle) {
      frameHandle = window.requestAnimationFrame(tick);
    }
  } else if (frameHandle) {
    window.cancelAnimationFrame(frameHandle);
    frameHandle = 0;
    drawScene();
  }
});

shuffleBtn.addEventListener('click', () => {
  cube.material.color.setHex(palette[Math.floor(Math.random() * palette.length)]);
  prism.material.color.setHex(palette[Math.floor(Math.random() * palette.length)]);
  drawScene();
});

resetBtn.addEventListener('click', () => {
  group.rotation.set(0, 0, 0);
  camera.position.set(0, 1.2, 5.8);
  camera.lookAt(0, 0, 0);
  drawScene();
});

drawScene();
frameHandle = window.requestAnimationFrame(tick);
"""
            ),
            new Preset(
                "Canvas WebGL + Three.js Lava Shader",
                """
<Border xmlns="https://github.com/avaloniaui" xmlns:js="clr-namespace:JavaScript.Avalonia;assembly=WebScene.Backend.Avalonia" Padding="16" Background="#050505" BorderBrush="#1f2937" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Three.js lava shader" FontWeight="SemiBold" Foreground="#f8fafc" />
    <TextBlock Text="Port of the current three.js webgl_shader_lava: custom ShaderMaterial, official lava textures, BloomPass blur, and OutputPass sRGB transfer." TextWrapping="Wrap" Foreground="#fca5a5" />
    <Border Background="#000000" BorderBrush="#7f1d1d" BorderThickness="1" CornerRadius="6" ClipToBounds="True">
      <js:CanvasOpenGlDrawingSurface Name="lavaSurface" Width="720" Height="420" />
    </Border>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="lavaToggle" Content="Pause animation" />
      <Button Name="lavaReset" Content="Reset rotation" />
    </StackPanel>
    <TextBlock Name="lavaStatus" Foreground="#fecaca" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('lavaSurface');
const toggleBtn = document.getElementById('lavaToggle');
const resetBtn = document.getElementById('lavaReset');
const status = document.getElementById('lavaStatus');

if (!surface) {
  throw new Error('lavaSurface element not found');
}

const gl = surface.getContext('webgl') || surface.getContext('experimental-webgl');
if (!gl) {
  throw new Error('Canvas WebGL context unavailable');
}

let threeModule;
try {
  threeModule = require('https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.min.js');
} catch (error) {
  const message = `Failed to load Three.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const THREE = threeModule?.REVISION ? threeModule : (typeof window !== 'undefined' ? window.THREE : undefined);
if (!THREE) {
  throw new Error('Three.js did not expose a renderer API');
}

const fragmentShader = `
uniform float time;

uniform float fogDensity;
uniform vec3 fogColor;

uniform sampler2D texture1;
uniform sampler2D texture2;

varying vec2 vUv;

void main( void ) {

  vec2 position = - 1.0 + 2.0 * vUv;

  vec4 noise = texture2D( texture1, vUv );
  vec2 T1 = vUv + vec2( 1.5, - 1.5 ) * time * 0.02;
  vec2 T2 = vUv + vec2( - 0.5, 2.0 ) * time * 0.01;

  T1.x += noise.x * 2.0;
  T1.y += noise.y * 2.0;
  T2.x -= noise.y * 0.2;
  T2.y += noise.z * 0.2;

  float p = texture2D( texture1, T1 * 2.0 ).a;

  vec4 color = texture2D( texture2, T2 * 2.0 );
  vec4 temp = color * ( vec4( p, p, p, p ) * 2.0 ) + ( color * color - 0.1 );

  if( temp.r > 1.0 ) { temp.bg += clamp( temp.r - 2.0, 0.0, 100.0 ); }
  if( temp.g > 1.0 ) { temp.rb += temp.g - 1.0; }
  if( temp.b > 1.0 ) { temp.rg += temp.b - 1.0; }

  gl_FragColor = temp;

  float depth = gl_FragCoord.z / gl_FragCoord.w;
  const float LOG2 = 1.442695;
  float fogFactor = exp2( - fogDensity * fogDensity * depth * depth * LOG2 );
  fogFactor = 1.0 - clamp( fogFactor, 0.0, 1.0 );

  gl_FragColor = mix( gl_FragColor, vec4( fogColor, gl_FragColor.w ), fogFactor );

}`;

const vertexShader = `
uniform vec2 uvScale;
varying vec2 vUv;

void main()
{

  vUv = uvScale * uv;
  vec4 mvPosition = modelViewMatrix * vec4( position, 1.0 );
  gl_Position = projectionMatrix * mvPosition;

}`;

const buildKernel = sigma => {
  const maxKernelSize = 25;
  let kernelSize = 2 * Math.ceil(sigma * 3.0) + 1;
  if (kernelSize > maxKernelSize) {
    kernelSize = maxKernelSize;
  }

  const halfWidth = (kernelSize - 1) * 0.5;
  const values = new Array(kernelSize);
  let sum = 0.0;
  for (let i = 0; i < kernelSize; i++) {
    const x = i - halfWidth;
    values[i] = Math.exp(-(x * x) / (2.0 * sigma * sigma));
    sum += values[i];
  }

  for (let i = 0; i < kernelSize; i++) {
    values[i] /= sum;
  }

  return values;
};

let renderScene = () => {};
const textureLoader = new THREE.TextureLoader();
const refreshWhenTextureLoads = () => {
  renderScene();
  report();
};
const cloudTexture = textureLoader.load('https://threejs.org/examples/textures/lava/cloud.png', refreshWhenTextureLoads);
const lavaTexture = textureLoader.load('https://threejs.org/examples/textures/lava/lavatile.jpg', refreshWhenTextureLoads);

if (THREE.SRGBColorSpace) {
  lavaTexture.colorSpace = THREE.SRGBColorSpace;
} else if (THREE.sRGBEncoding) {
  lavaTexture.encoding = THREE.sRGBEncoding;
}

cloudTexture.wrapS = cloudTexture.wrapT = THREE.RepeatWrapping;
lavaTexture.wrapS = lavaTexture.wrapT = THREE.RepeatWrapping;

const uniforms = {
  fogDensity: { value: 0.45 },
  fogColor: { value: new THREE.Vector3(0, 0, 0) },
  time: { value: 1.0 },
  uvScale: { value: new THREE.Vector2(3.0, 1.0) },
  texture1: { value: cloudTexture },
  texture2: { value: lavaTexture }
};

const renderer = new THREE.WebGLRenderer({
  canvas: surface,
  context: gl,
  antialias: true,
  alpha: false
});
const pixelRatio = Math.max(1, window.devicePixelRatio || 1);
const renderWidth = Math.max(1, Math.round(surface.offsetWidth * pixelRatio));
const renderHeight = Math.max(1, Math.round(surface.offsetHeight * pixelRatio));
renderer.setPixelRatio(pixelRatio);
renderer.setSize(surface.offsetWidth, surface.offsetHeight, false);
renderer.setClearColor(0x000000, 1);
renderer.autoClear = false;

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(35, surface.offsetWidth / surface.offsetHeight, 1, 3000);
camera.position.z = 4;

const material = new THREE.ShaderMaterial({
  uniforms,
  vertexShader,
  fragmentShader
});

const size = 0.65;
const mesh = new THREE.Mesh(new THREE.TorusGeometry(size, 0.3, 30, 30), material);
mesh.rotation.x = 0.3;
scene.add(mesh);

let running = true;
let frameHandle = 0;
let lastTimestamp = 0;

const targetOptions = {
  minFilter: THREE.LinearFilter,
  magFilter: THREE.LinearFilter,
  format: THREE.RGBAFormat,
  type: THREE.HalfFloatType || THREE.UnsignedByteType,
  stencilBuffer: false
};
const sceneTarget = new THREE.WebGLRenderTarget(renderWidth, renderHeight, targetOptions);
const bloomTargetX = new THREE.WebGLRenderTarget(renderWidth, renderHeight, targetOptions);
const bloomTargetY = new THREE.WebGLRenderTarget(renderWidth, renderHeight, targetOptions);

const quadScene = new THREE.Scene();
const quadCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
const quadGeometry = new THREE.BufferGeometry();
quadGeometry.setAttribute('position', new THREE.Float32BufferAttribute([-1, 3, 0, -1, -1, 0, 3, -1, 0], 3));
quadGeometry.setAttribute('uv', new THREE.Float32BufferAttribute([0, 2, 0, 0, 2, 0], 2));
const quad = new THREE.Mesh(quadGeometry, null);
quadScene.add(quad);

const outputMaterial = new THREE.ShaderMaterial({
  uniforms: {
    tDiffuse: { value: null },
    toneMappingExposure: { value: 1 }
  },
  vertexShader: `
varying vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`,
  fragmentShader: `
uniform sampler2D tDiffuse;
uniform float toneMappingExposure;
varying vec2 vUv;

vec3 linearToneMapping(vec3 color) {
  return toneMappingExposure * color;
}

vec3 sRGBTransferOETF(vec3 value) {
  vec3 low = value * 12.92;
  vec3 high = pow(max(value, vec3(0.0)), vec3(0.41666)) * 1.055 - 0.055;
  return mix(low, high, step(vec3(0.0031308), value));
}

void main() {
  vec4 texel = texture2D(tDiffuse, vUv);
  texel.rgb = linearToneMapping(texel.rgb);
  texel.rgb = sRGBTransferOETF(texel.rgb);
  gl_FragColor = texel;
}`,
  blending: THREE.NoBlending,
  depthTest: false,
  depthWrite: false
});

const combineMaterial = new THREE.ShaderMaterial({
  uniforms: {
    tDiffuse: { value: null },
    strength: { value: 1.25 }
  },
  vertexShader: `
varying vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`,
  fragmentShader: `
uniform float strength;
uniform sampler2D tDiffuse;
varying vec2 vUv;
void main() {
  vec4 texel = texture2D(tDiffuse, vUv);
  gl_FragColor = strength * texel;
}`,
  blending: THREE.AdditiveBlending,
  transparent: true,
  depthTest: false,
  depthWrite: false
});

const convolutionMaterial = new THREE.ShaderMaterial({
  uniforms: {
    tDiffuse: { value: null },
    uImageIncrement: { value: new THREE.Vector2(0.001953125, 0.0) },
    cKernel: { value: buildKernel(4) }
  },
  defines: {
    KERNEL_SIZE_FLOAT: '25.0',
    KERNEL_SIZE_INT: '25'
  },
  vertexShader: `
uniform vec2 uImageIncrement;
varying vec2 vUv;
void main() {
  vUv = uv - ((KERNEL_SIZE_FLOAT - 1.0) / 2.0) * uImageIncrement;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`,
  fragmentShader: `
uniform float cKernel[KERNEL_SIZE_INT];
uniform sampler2D tDiffuse;
uniform vec2 uImageIncrement;
varying vec2 vUv;
void main() {
  vec2 imageCoord = vUv;
  vec4 sum = vec4(0.0);
  for (int i = 0; i < KERNEL_SIZE_INT; i++) {
    sum += texture2D(tDiffuse, imageCoord) * cKernel[i];
    imageCoord += uImageIncrement;
  }
  gl_FragColor = sum;
}`,
  blending: THREE.NoBlending,
  depthTest: false,
  depthWrite: false
});

const renderQuad = (material, target, clearTarget) => {
  quad.material = material;
  renderer.setRenderTarget(target);
  if (clearTarget) {
    renderer.clear();
  }
  renderer.render(quadScene, quadCamera);
};

renderScene = (delta = 0.01) => {
  renderer.clear();

  renderer.setRenderTarget(sceneTarget);
  renderer.clear();
  renderer.render(scene, camera);

  convolutionMaterial.uniforms.tDiffuse.value = sceneTarget.texture;
  convolutionMaterial.uniforms.uImageIncrement.value.set(0.001953125, 0.0);
  renderQuad(convolutionMaterial, bloomTargetX, true);

  convolutionMaterial.uniforms.tDiffuse.value = bloomTargetX.texture;
  convolutionMaterial.uniforms.uImageIncrement.value.set(0.0, 0.001953125);
  renderQuad(convolutionMaterial, bloomTargetY, true);

  combineMaterial.uniforms.tDiffuse.value = bloomTargetY.texture;
  renderQuad(combineMaterial, sceneTarget, false);

  outputMaterial.uniforms.tDiffuse.value = sceneTarget.texture;
  renderQuad(outputMaterial, null, false);
};

const report = () => {
  if (status) {
    const native = gl.LastNativeDrawStatus ? ` native: ${gl.LastNativeDrawStatus}.` : '';
    const caps = gl.NativeGlCapabilities ? ` ${gl.NativeGlCapabilities}.` : '';
    status.textContent = `three.js ${THREE.REVISION} lava shader with BloomPass + OutputPass. Backend: ${gl.RenderBackend}; draw calls: ${gl.DrawCallCount}; triangles: ${gl.TriangleCount}; ${gl.LastDrawStatus}.${native}${caps}`;
  }
};

const render = timestamp => {
  frameHandle = 0;
  const seconds = timestamp ? timestamp / 1000 : 0;
  const delta = lastTimestamp ? Math.max(0.001, seconds - lastTimestamp) : 0.016;
  lastTimestamp = seconds;

  if (running) {
    const scaledDelta = 5 * delta;
    uniforms.time.value += 0.2 * scaledDelta;
    mesh.rotation.y += 0.0125 * scaledDelta;
    mesh.rotation.x += 0.05 * scaledDelta;
  }

  renderScene(0.01);
  if (typeof gl.flush === 'function') {
    gl.flush();
  }

  report();

  if (running) {
    frameHandle = window.requestAnimationFrame(render);
  }
};

toggleBtn.addEventListener('click', () => {
  running = !running;
  toggleBtn.textContent = running ? 'Pause animation' : 'Resume animation';
  if (running && !frameHandle) {
    frameHandle = window.requestAnimationFrame(render);
  } else if (!running && frameHandle) {
    window.cancelAnimationFrame(frameHandle);
    frameHandle = 0;
  }
});

resetBtn.addEventListener('click', () => {
  uniforms.time.value = 1.0;
  mesh.rotation.set(0.3, 0, 0);
  renderScene();
  report();
});

renderScene();
report();
frameHandle = window.requestAnimationFrame(render);
"""
            ),
            new Preset(
                "Canvas 2D + Fabric.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#ffffff" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with Fabric.js" FontWeight="SemiBold" Foreground="#1f2937" />
    <Border Name="fabricSurface" Width="560" Height="320" Background="#f8fafc" BorderBrush="#e2e8f0" BorderThickness="1" CornerRadius="4" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="fabricAddRect" Content="Add rectangle" />
      <Button Name="fabricAddCircle" Content="Add circle" />
      <Button Name="fabricToggleGrid" Content="Toggle grid" />
      <Button Name="fabricReset" Content="Reset scene" />
    </StackPanel>
    <TextBlock Name="fabricStatus" Foreground="#475569" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('fabricSurface');
const addRect = document.getElementById('fabricAddRect');
const addCircle = document.getElementById('fabricAddCircle');
const gridBtn = document.getElementById('fabricToggleGrid');
const resetBtn = document.getElementById('fabricReset');
const status = document.getElementById('fabricStatus');

if (!surface) {
  throw new Error('fabricSurface element not found');
}

surface.width = surface.offsetWidth;
surface.height = surface.offsetHeight;

let fabricModule;
try {
  fabricModule = require('https://cdn.jsdelivr.net/npm/fabric@5.3.0/dist/fabric.min.js');
} catch (error) {
  const message = `Failed to load Fabric.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const fabricGlobal = typeof window !== 'undefined' ? window.fabric : undefined;
const fabric = fabricModule?.fabric ?? fabricModule?.default ?? fabricGlobal ?? fabricModule;
if (!fabric?.Canvas) {
  const message = 'Fabric.js module did not expose a Canvas constructor';
  if (status) {
    status.textContent = message;
  }
  throw new Error(message);
}

if (fabric.Object?.prototype) {
  fabric.Object.prototype.objectCaching = false;
}

const canvas = new fabric.Canvas(surface, {
  selection: true,
  backgroundColor: '#f8fafc',
  preserveObjectStacking: true
});
canvas.setDimensions({ width: surface.offsetWidth, height: surface.offsetHeight });
const interactiveTarget = canvas.upperCanvasEl ?? surface;
interactiveTarget.tabIndex = 0;

const report = message => {
  if (status) {
    status.textContent = message;
  }
};

const randomBetween = (min, max) => Math.round(min + Math.random() * (max - min));
const randomColor = () => {
  const palette = ['#3b82f6', '#ec4899', '#f97316', '#22c55e', '#a855f7'];
  return palette[randomBetween(0, palette.length - 1)];
};

const gridLines = [];
let gridVisible = false;

const clearGrid = () => {
  while (gridLines.length) {
    const line = gridLines.pop();
    canvas.remove(line);
  }
};

const clearCanvasObjects = () => {
  canvas.discardActiveObject?.();
  canvas.getObjects().slice().forEach(object => canvas.remove(object));
  canvas.backgroundImage = null;
  canvas.overlayImage = null;
  canvas.overlayColor = '';
  canvas.clearContext?.(canvas.contextContainer);
  canvas.clearContext?.(canvas.contextTop);
};

const createGrid = () => {
  clearGrid();
  const step = 60;
  const width = canvas.getWidth();
  const height = canvas.getHeight();
  for (let x = step; x < width; x += step) {
    const line = new fabric.Line([x, 0, x, height], {
      stroke: '#cbd5f5',
      strokeWidth: 1,
      strokeDashArray: [6, 6],
      selectable: false,
      evented: false
    });
    line.excludeFromExport = true;
    gridLines.push(line);
    canvas.add(line);
    line.sendToBack();
  }
  for (let y = step; y < height; y += step) {
    const line = new fabric.Line([0, y, width, y], {
      stroke: '#cbd5f5',
      strokeWidth: 1,
      strokeDashArray: [6, 6],
      selectable: false,
      evented: false
    });
    line.excludeFromExport = true;
    gridLines.push(line);
    canvas.add(line);
    line.sendToBack();
  }
  gridVisible = true;
  report('Grid overlay enabled – drag shapes to explore snapping visuals.');
  canvas.renderAll();
};

const toggleGrid = () => {
  if (gridVisible) {
    clearGrid();
    gridVisible = false;
    canvas.renderAll();
    report('Grid overlay removed.');
    return;
  }
  createGrid();
};

const baseShapes = () => {
  clearGrid();
  gridVisible = false;
  clearCanvasObjects();
  canvas.backgroundColor = '#f1f5f9';

  const gradientRect = new fabric.Rect({
    left: 80,
    top: 70,
    width: 200,
    height: 140,
    rx: 26,
    ry: 26,
    fill: '#3b82f6',
    stroke: '#1e3a8a',
    strokeWidth: 2
  });

  const circle = new fabric.Circle({
    left: 320,
    top: 90,
    radius: 70,
    fill: '#ec4899',
    stroke: '#be123c',
    strokeWidth: 2
  });

  const triangle = new fabric.Triangle({
    left: 235,
    top: 230,
    width: 96,
    height: 72,
    fill: '#f59e0b',
    stroke: '#92400e',
    strokeWidth: 2,
    angle: -8
  });

  canvas.add(gradientRect, circle, triangle);
  canvas.renderAll();
  report('Scene initialised. Use mouse to select/drag; arrow keys nudge; Delete removes.');
};

const addRandomRect = () => {
  const rect = new fabric.Rect({
    left: randomBetween(40, canvas.getWidth() - 160),
    top: randomBetween(40, canvas.getHeight() - 120),
    width: randomBetween(80, 160),
    height: randomBetween(60, 120),
    rx: 18,
    ry: 18,
    fill: randomColor(),
    opacity: 0.85
  });
  canvas.add(rect);
  canvas.setActiveObject(rect);
  interactiveTarget.focus?.();
  report('Added a draggable rounded rectangle.');
};

const addRandomCircle = () => {
  const circle = new fabric.Circle({
    left: randomBetween(60, canvas.getWidth() - 140),
    top: randomBetween(60, canvas.getHeight() - 140),
    radius: randomBetween(36, 72),
    fill: randomColor(),
    stroke: '#1f2937',
    strokeWidth: 1.5,
    opacity: 0.9
  });
  canvas.add(circle);
  canvas.setActiveObject(circle);
  interactiveTarget.focus?.();
  report('Added an interactive circle.');
};

const nudgeActiveObject = (dx, dy) => {
  const active = canvas.getActiveObject();
  if (!active) {
    report('Select a shape before using keyboard shortcuts.');
    return false;
  }

  active.set({
    left: (active.left ?? 0) + dx,
    top: (active.top ?? 0) + dy
  });
  active.setCoords();
  canvas.requestRenderAll();
  report(`Nudged ${active.type} to left=${Math.round(active.left ?? 0)}, top=${Math.round(active.top ?? 0)}.`);
  return true;
};

interactiveTarget.addEventListener('keydown', evt => {
  const step = evt.shiftKey ? 10 : 2;
  let handled = false;
  switch (evt.key) {
    case 'ArrowLeft':
      handled = nudgeActiveObject(-step, 0);
      break;
    case 'ArrowRight':
      handled = nudgeActiveObject(step, 0);
      break;
    case 'ArrowUp':
      handled = nudgeActiveObject(0, -step);
      break;
    case 'ArrowDown':
      handled = nudgeActiveObject(0, step);
      break;
    case 'Delete':
    case 'Backspace': {
      const active = canvas.getActiveObject();
      if (active) {
        canvas.remove(active);
        canvas.discardActiveObject();
        canvas.requestRenderAll();
        report(`Removed ${active.type}.`);
        handled = true;
      }
      break;
    }
    case 'Escape':
      canvas.discardActiveObject();
      canvas.requestRenderAll();
      report('Selection cleared.');
      handled = true;
      break;
  }

  if (handled) {
    evt.preventDefault();
  }
});

canvas.on('mouse:down', () => {
  interactiveTarget.focus?.();
});

canvas.on('object:modified', evt => {
  const target = evt?.target;
  if (!target) {
    return;
  }
  const { left, top, angle } = target;
  report(`Updated ${target.type} → left=${Math.round(left ?? 0)}, top=${Math.round(top ?? 0)}, angle=${Math.round(angle ?? 0)}`);
});

canvas.on('selection:created', evt => {
  const target = evt?.selected?.[0];
  if (target) {
    interactiveTarget.focus?.();
    report(`Selected ${target.type} – try scaling or rotating.`);
  }
});

canvas.on('selection:updated', evt => {
  const target = evt?.selected?.[0];
  if (target) {
    interactiveTarget.focus?.();
    report(`Selected ${target.type} – arrow keys nudge, Delete removes.`);
  }
});

canvas.on('selection:cleared', () => {
  report('Selection cleared.');
});

addRect.addEventListener('click', addRandomRect);
addCircle.addEventListener('click', addRandomCircle);
gridBtn.addEventListener('click', toggleGrid);
resetBtn.addEventListener('click', () => {
  baseShapes();
  report('Scene reset to default composition.');
});

baseShapes();
"""
            ),
            new Preset(
                "Canvas 2D + Paper.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#0f172a" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with Paper.js" FontWeight="SemiBold" Foreground="#f8fafc" />
    <Border Name="paperSurface" Width="560" Height="320" Background="#111827" BorderBrush="#1e293b" BorderThickness="1" CornerRadius="6" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="paperMutate" Content="Mutate scene" />
      <Button Name="paperToggle" Content="Pause / resume" />
    </StackPanel>
    <TextBlock Name="paperStatus" Foreground="#e2e8f0" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const surface = document.getElementById('paperSurface');
const mutateBtn = document.getElementById('paperMutate');
const toggleBtn = document.getElementById('paperToggle');
const status = document.getElementById('paperStatus');

if (!surface) {
  throw new Error('paperSurface element not found');
}

const paperContext = surface.getContext('2d');
if (!paperContext) {
  throw new Error('CanvasRenderingContext2D unavailable for Paper.js demo');
}

const paperCanvas = paperContext.canvas ?? surface;
paperCanvas.width = surface.offsetWidth;
paperCanvas.height = surface.offsetHeight;

let paperModule;
try {
  paperModule = require('https://cdn.jsdelivr.net/npm/paper@0.12.17/dist/paper-full.min.js');
} catch (error) {
  const message = `Failed to load Paper.js: ${error}`;
  if (status) {
    status.textContent = message;
  }
  console.error(message);
  throw error;
}

const paperGlobal = typeof window !== 'undefined' ? window.paper : undefined;
const paper = paperModule?.paper ?? paperModule?.default ?? paperGlobal ?? paperModule;
if (typeof paper?.setup !== 'function') {
  const message = 'Paper.js module did not expose a setup function';
  if (status) {
    status.textContent = message;
  }
  throw new Error(message);
}

paper.setup(paperCanvas);

const report = message => {
  if (status) {
    status.textContent = message;
  }
};

const width = surface.offsetWidth;
const height = surface.offsetHeight;
const center = new paper.Point(width / 2, height / 2);

const background = new paper.Path.Rectangle({
  point: [0, 0],
  size: [width, height],
  radius: 18,
  fillColor: '#111827'
});
background.sendToBack();

const halo = new paper.Path.Circle({
  center,
  radius: Math.min(width, height) * 0.45,
  strokeColor: '#60a5fa',
  dashArray: [12, 8],
  strokeWidth: 2,
  opacity: 0.85
});

const star = new paper.Path.Star({
  center,
  points: 7,
  radius1: Math.min(width, height) * 0.16,
  radius2: Math.min(width, height) * 0.34,
  fillColor: '#ec4899',
  strokeColor: '#be185d',
  strokeWidth: 3
});
star.shadowColor = new paper.Color(0, 0, 0, 0.28);
star.shadowBlur = 18;
star.shadowOffset = new paper.Point(0, 10);

const ribbon = new paper.Path.Rectangle({
  point: [60, height / 2 - 40],
  size: [width - 120, 80],
  radius: 28,
  fillColor: new paper.Color(0.22, 0.35, 0.55, 0.45)
});
ribbon.rotate(-8, center);
ribbon.blendMode = 'soft-light';

const wave = new paper.Path({
  strokeColor: '#38bdf8',
  strokeWidth: 2,
  opacity: 0.75
});
wave.sendToBack();
let waveBase = [];

const rebuildWave = () => {
  wave.removeSegments();
  const segments = 14;
  const baseline = height - 70;
  const amplitude = 22;
  for (let i = 0; i <= segments; i++) {
    const x = (width / segments) * i;
    const phase = (i / segments) * Math.PI * 2;
    const y = baseline + Math.sin(phase) * amplitude;
    wave.add(new paper.Point(x, y));
  }
  wave.smooth({ type: 'catmull-rom', factor: 0.5 });
  waveBase = wave.segments.map(segment => segment.point.y);
};

rebuildWave();

const connectors = new paper.Group();
const rebuildConnectors = () => {
  connectors.removeChildren();
  const spokes = 6;
  for (let i = 0; i < spokes; i++) {
    const angle = (360 / spokes) * i;
    const start = center.add(new paper.Point({ length: star.bounds.width * 0.2, angle }));
    const end = center.add(new paper.Point({ length: star.bounds.width * 0.8, angle }));
    const path = new paper.Path.Line(start, end);
    path.strokeColor = new paper.Color(0.55, 0.63, 0.75, 0.65);
    path.strokeWidth = 1.5;
    path.dashArray = [8, 6];
    connectors.addChild(path);
  }
  connectors.sendToBack();
};

rebuildConnectors();

const particles = new paper.Group();
const createParticles = count => {
  particles.removeChildren();
  for (let i = 0; i < count; i++) {
    const base = new paper.Point(
      40 + Math.random() * (width - 80),
      80 + Math.random() * (height - 140)
    );
    const radius = 5 + Math.random() * 9;
    const blob = new paper.Path.Circle({
      center: base,
      radius,
      fillColor: new paper.Color({
        hue: 210 + Math.random() * 40,
        saturation: 0.35,
        brightness: 0.95,
        alpha: 0.45
      })
    });
    blob.blendMode = 'screen';
    blob.data = {
      base,
      amplitude: 4 + Math.random() * 6,
      speed: 0.5 + Math.random() * 1.2
    };
    particles.addChild(blob);
  }
};

createParticles(26);

let animationEnabled = true;

paper.view.onFrame = event => {
  if (!animationEnabled) {
    return;
  }

  star.rotate(0.6);
  connectors.rotate(0.25, center);

  particles.children.forEach((particle, index) => {
    const base = particle.data.base;
    const amp = particle.data.amplitude;
    const speed = particle.data.speed;
    const phase = event.time * speed + index;
    particle.position.x = base.x + Math.sin(phase) * amp * 4;
    particle.position.y = base.y + Math.cos(phase * 1.4) * amp * 2;
  });

  wave.segments.forEach((segment, index) => {
    segment.point.y = waveBase[index] + Math.sin(event.time * 1.6 + index * 0.35) * 4;
  });
  wave.smooth({ type: 'catmull-rom', factor: 0.5 });
};

const mutateScene = () => {
  const hue = Math.round(Math.random() * 360);
  star.fillColor = new paper.Color({ hue, saturation: 0.65, brightness: 0.95 });
  star.strokeColor = new paper.Color({ hue: (hue + 320) % 360, saturation: 0.6, brightness: 0.6 });
  halo.strokeColor = new paper.Color({ hue: (hue + 180) % 360, saturation: 0.4, brightness: 0.85 });
  ribbon.fillColor = new paper.Color({ hue: (hue + 40) % 360, saturation: 0.3, brightness: 0.55, alpha: 0.5 });

  particles.children.forEach((particle, index) => {
    const base = new paper.Point(
      40 + Math.random() * (width - 80),
      80 + Math.random() * (height - 140)
    );
    particle.data.base = base;
    particle.position = base.clone();
    particle.data.amplitude = 4 + Math.random() * 7;
    particle.data.speed = 0.5 + Math.random() * 1.4;
    particle.fillColor = new paper.Color({
      hue: (hue + index * 9) % 360,
      saturation: 0.4,
      brightness: 0.95,
      alpha: 0.48
    });
  });

  rebuildWave();
  rebuildConnectors();
  paper.view.update();
  paper.view.requestUpdate();
  report('Paper.js scene mutated with new palette and geometry.');
};

mutateBtn.addEventListener('click', mutateScene);

toggleBtn.addEventListener('click', () => {
  animationEnabled = !animationEnabled;
  if (animationEnabled) {
    paper.view.play();
    report('Animation resumed.');
  } else {
    paper.view.pause();
    report('Animation paused — mutate to explore new layouts.');
  }
});

report('Paper.js generative scene ready — mutate the palette or pause the motion.');
paper.view.update();
paper.view.play();
"""
            ),
            new Preset(
                "Canvas 2D + PDF.js",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f8fafc" BorderBrush="#d1d5db" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Canvas 2D with PDF.js" FontWeight="SemiBold" Foreground="#1f2937" />
    <TextBlock Text="Loads PDF.js, previews the built-in PDF, and can open external PDF documents through the host file picker." TextWrapping="Wrap" Foreground="#475569" />
    <Border Name="pdfSurface" Width="640" Height="460" Background="#e2e8f0" BorderBrush="#cbd5e1" BorderThickness="1" CornerRadius="6" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="pdfOpen" Content="Open PDF..." />
      <Button Name="pdfBuiltIn" Content="Load built-in" />
      <Button Name="pdfPrev" Content="Previous page" />
      <Button Name="pdfNext" Content="Next page" />
      <Button Name="pdfZoomIn" Content="Zoom in" />
      <Button Name="pdfZoomOut" Content="Zoom out" />
    </StackPanel>
    <TextBlock Name="pdfInfo" Foreground="#334155" TextWrapping="Wrap" />
    <TextBlock Name="pdfStatus" Foreground="#475569" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
try {
  require('./Scripts/pdf-demo.js');
} catch (error) {
  const status = document.getElementById('pdfStatus');
  if (status) {
    status.textContent = `Failed to load PDF.js demo: ${error}`;
  }
  throw error;
}
"""
            ),
            new Preset(
                "Modules: CommonJS",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#f9fafc" BorderBrush="#d8e2ef" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="10">
    <TextBlock Text="CommonJS module (ms)" FontWeight="SemiBold" FontSize="18" Foreground="#1f2937" />
    <TextBlock Text="Convert between human readable durations and milliseconds using the ms package." TextWrapping="Wrap" Foreground="#475569" />
    <StackPanel Orientation="Horizontal" Spacing="8">
      <TextBox Name="msInput" Width="160" Text="1.5h" />
      <Button Name="msParse" Content="Parse ⟶ ms" />
      <Button Name="msFormat" Content="Format ⟶ long" />
    </StackPanel>
    <TextBlock Name="msOutput" Foreground="#0f172a" FontFamily="Consolas" />
  </StackPanel>
</Border>
""",
                """
const input = document.getElementById('msInput');
const parseBtn = document.getElementById('msParse');
const formatBtn = document.getElementById('msFormat');
const output = document.getElementById('msOutput');

let ms;
try {
  ms = require('https://cdn.jsdelivr.net/npm/ms@2.1.3/index.js');
  if (typeof ms !== 'function') {
    throw new Error('ms module did not export a function');
  }
} catch (error) {
  const message = `Failed to load ms module: ${error}`;
  output.textContent = message;
  console.error(message);
  throw error;
}

const report = message => {
  output.textContent = message ?? '';
};

parseBtn.addEventListener('click', () => {
  const value = input.value?.trim();
  if (!value) {
    report('Enter a duration like 2500, 2m, 1.5h');
    return;
  }

  try {
    const result = ms(value);
    if (typeof result !== 'number' || Number.isNaN(result)) {
      report(`Could not parse "${value}"`);
      return;
    }
    report(`${value} = ${result.toLocaleString()} ms`);
  } catch (error) {
    report(`Parse error: ${error}`);
  }
});

formatBtn.addEventListener('click', () => {
  const value = Number.parseFloat(input.value ?? '');
  if (!Number.isFinite(value)) {
    report('Provide a numeric millisecond value to format.');
    return;
  }

  try {
    report(`${value.toLocaleString()} ms ≈ ${ms(value, { long: true })}`);
  } catch (error) {
    report(`Format error: ${error}`);
  }
});

report('CommonJS require() ready – try parsing "45m" or formatting 900000.');
"""
            ),
            new Preset(
                "Modules: AMD",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#ffffff" BorderBrush="#d9e3f1" BorderThickness="1" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="AMD module (underscore)" FontWeight="SemiBold" FontSize="18" Foreground="#111827" />
    <TextBlock Text="Load underscore via its AMD factory and explore functional helpers." TextWrapping="Wrap" Foreground="#475569" />
    <ItemsControl Name="amdTodo">
      <ItemsControl.Items>
        <TextBlock Text="Refactor module loader" />
        <TextBlock Text="Review pull requests" />
        <TextBlock Text="Write release notes" />
        <TextBlock Text="Triage issues" />
        <TextBlock Text="Ship new sample" />
      </ItemsControl.Items>
    </ItemsControl>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="amdShuffle" Content="Shuffle" />
      <Button Name="amdSample" Content="Pick task" />
    </StackPanel>
    <TextBlock Name="amdStatus" Foreground="#1e293b" FontFamily="Consolas" TextWrapping="Wrap" />
  </StackPanel>
</Border>
""",
                """
const list = document.getElementById('amdTodo');
const shuffleBtn = document.getElementById('amdShuffle');
const sampleBtn = document.getElementById('amdSample');
const status = document.getElementById('amdStatus');

let underscore;
try {
  underscore = require('https://cdn.jsdelivr.net/npm/underscore@1.13.6/underscore-min.js');
  if (!underscore || typeof underscore.sample !== 'function') {
    throw new Error('underscore export missing expected helpers');
  }
} catch (error) {
  const message = `Failed to load underscore via AMD: ${error}`;
  status.textContent = message;
  console.error(message);
  throw error;
}

const readItems = () => Array.from(list.children ?? []).map(child => child.textContent).filter(Boolean);

shuffleBtn.addEventListener('click', () => {
  const items = readItems();
  const shuffled = underscore.shuffle(items);
  list.children?.forEach(child => child.remove());
  shuffled.forEach(text => {
    const node = document.createElement('TextBlock');
    node.textContent = text;
    list.appendChild(node);
  });
  status.textContent = 'Shuffled using underscore.shuffle()';
});

sampleBtn.addEventListener('click', () => {
  const items = readItems();
  if (items.length === 0) {
    status.textContent = 'List is empty';
    return;
  }
  const pick = underscore.sample(items);
  status.textContent = `Focus next: ${pick}`;
});

status.textContent = 'underscore AMD module ready – shuffle the backlog or pick a task.';
"""
            ),
            new Preset(
                "Modules: Global fallback",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16" Background="#0f172a" CornerRadius="8">
  <StackPanel Spacing="12">
    <TextBlock Text="Global factory (anime.js)" FontWeight="SemiBold" Foreground="#f8fafc" FontSize="18" />
    <TextBlock Text="anime.js exposes a browser global – the loader captures it so we can orchestrate tweening." TextWrapping="Wrap" Foreground="#cbd5f5" />
    <StackPanel Orientation="Horizontal" Spacing="10" HorizontalAlignment="Center">
      <Border Name="animeBar1" Width="60" Height="12" CornerRadius="6" Background="#38bdf8" />
      <Border Name="animeBar2" Width="60" Height="12" CornerRadius="6" Background="#818cf8" />
      <Border Name="animeBar3" Width="60" Height="12" CornerRadius="6" Background="#f472b6" />
    </StackPanel>
    <StackPanel Orientation="Horizontal" Spacing="8">
      <Button Name="animePlay" Content="Play animation" />
      <Button Name="animeReset" Content="Reset" />
    </StackPanel>
    <TextBlock Name="animeStatus" Foreground="#e2e8f0" />
  </StackPanel>
</Border>
""",
                """
const bar1 = document.getElementById('animeBar1');
const bar2 = document.getElementById('animeBar2');
const bar3 = document.getElementById('animeBar3');
const playBtn = document.getElementById('animePlay');
const resetBtn = document.getElementById('animeReset');
const status = document.getElementById('animeStatus');

let animeFactory;
try {
  const module = require('https://cdn.jsdelivr.net/npm/animejs@3.2.1/lib/anime.min.js');
  animeFactory = module?.anime ?? module ?? window.anime;
  if (typeof animeFactory !== 'function') {
    throw new Error('anime.js global was not detected');
  }
} catch (error) {
  const message = `Failed to load anime.js: ${error}`;
  status.textContent = message;
  console.error(message);
  throw error;
}

const bars = [bar1, bar2, bar3].filter(Boolean);

const resetBars = () => {
  bars.forEach((bar, index) => {
    bar.style.setProperty('transform', 'scaleX(1)');
    bar.style.setProperty('opacity', '1');
    bar.style.setProperty('box-shadow', 'none');
    bar.setAttribute('width', '60');
    bar.setAttribute('Background', index === 0 ? '#38bdf8' : index === 1 ? '#818cf8' : '#f472b6');
  });
  status.textContent = 'Ready – animate the bars!';
};

resetBars();

let running = null;

const playAnimation = () => {
  if (running) {
    running.pause?.();
  }

  running = animeFactory({
    targets: bars,
    scaleX: [1, 2.4],
    opacity: [1, 0.45],
    borderRadius: ['6px', '12px'],
    direction: 'alternate',
    easing: 'easeInOutSine',
    duration: 1200,
    delay: animeFactory.stagger(120),
    loop: 2,
    complete: () => {
      status.textContent = 'Animation finished – global fallback succeeded.';
    }
  });

  status.textContent = 'Animating with anime.js global factory...';
};

playBtn.addEventListener('click', playAnimation);

resetBtn.addEventListener('click', () => {
  if (running) {
    running.pause?.();
    running = null;
  }
  resetBars();
  status.textContent = 'Animation reset.';
});
"""
            ),
            new Preset(
                "Ready state & query",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="6">
    <TextBlock Name="state" FontWeight="Bold" />
    <ItemsControl Name="list">
      <ItemsControl.Items>
        <TextBlock Text="One" />
        <TextBlock Text="Two" />
        <TextBlock Text="Three" />
      </ItemsControl.Items>
    </ItemsControl>
  </StackPanel>
</Border>
""",
                """
const stateLabel = document.getElementById('state');
stateLabel.textContent = `readyState: ${document.readyState}`;

document.addEventListener('DOMContentLoaded', () => {
  stateLabel.textContent = 'DOMContentLoaded fired';
});

const items = document.querySelectorAll('TextBlock');
console.table(items.map(i => i.textContent));
"""),
            new Preset(
                "External library",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="12">
    <TextBlock Name="status" FontSize="18" FontWeight="SemiBold" Text="Loading external library..." />
    <TextBlock Text="This preset fetches day.js from a CDN using require() and shows the formatted time." TextWrapping="Wrap" />
    <Button Name="refresh" Content="Refresh timestamp" HorizontalAlignment="Left" />
  </StackPanel>
</Border>
""",
                """
const status = document.getElementById('status');
const refresh = document.getElementById('refresh');

function updateTime() {
  try {
    const dayjs = require('https://cdn.jsdelivr.net/npm/dayjs@1/dayjs.min.js');
    status.textContent = `Loaded dayjs@${dayjs.version} – ${dayjs().format('YYYY-MM-DD HH:mm:ss')}`;
  } catch (err) {
    status.textContent = `Failed to load dayjs: ${err}`;
  }
}

refresh.addEventListener('click', () => updateTime());
updateTime();
"""),

            new Preset(
                "External library (relative time)",
                """
<Border xmlns="https://github.com/avaloniaui" Padding="16">
  <StackPanel Spacing="12">
    <TextBlock Name="statusAdvanced" FontSize="18" FontWeight="SemiBold" Text="Loading timeline..." />
    <TextBlock Text="Requires dayjs and its relativeTime plugin, loaded from jsDelivr." TextWrapping="Wrap" />
    <Button Name="refreshAdvanced" Content="Refresh timeline" HorizontalAlignment="Left" />
    <StackPanel Name="eventList" Spacing="4" />
  </StackPanel>
</Border>
""",
                """
const status = document.getElementById('statusAdvanced');
const refresh = document.getElementById('refreshAdvanced');
const eventList = document.getElementById('eventList');

function updateTimeline() {
  try {
    const dayjs = require('https://cdn.jsdelivr.net/npm/dayjs@1/dayjs.min.js');
    const relativeTime = require('https://cdn.jsdelivr.net/npm/dayjs@1/plugin/relativeTime.js');
    dayjs.extend(relativeTime);

    const events = [
      { label: 'Kick-off', offset: -3 },
      { label: 'Design freeze', offset: -1 },
      { label: 'Beta release', offset: 4 },
      { label: 'Launch', offset: 12 }
    ];

    for (const child of eventList.children) {
      child.remove();
    }

    const now = dayjs();
    for (const evt of events) {
      const block = document.createElement('TextBlock');
      block.textContent = `${evt.label}: ${now.add(evt.offset, 'day').fromNow()}`;
      eventList.appendChild(block);
    }

    status.textContent = `Timeline refreshed at ${now.format('YYYY-MM-DD HH:mm:ss')}`;
  } catch (error) {
    status.textContent = `Failed to load timeline data: ${error}`;
  }
}

refresh.addEventListener('click', () => updateTimeline());
updateTimeline();
"""),
        };
    }

    private static int ResolveStartupPresetIndex(IReadOnlyList<Preset> presets)
    {
        if (presets.Count == 0)
        {
            return -1;
        }

        var requestedPreset = Environment.GetEnvironmentVariable("JAVASCRIPT_PLAYGROUND_PRESET");
        if (string.IsNullOrWhiteSpace(requestedPreset))
        {
            return 0;
        }

        for (var i = 0; i < presets.Count; i++)
        {
            if (string.Equals(presets[i].Name, requestedPreset, StringComparison.OrdinalIgnoreCase) ||
                presets[i].Name.Contains(requestedPreset, StringComparison.OrdinalIgnoreCase))
            {
                return i;
            }
        }

        return 0;
    }

    private sealed class Preset
    {
        public Preset(string name, string xaml, string script, int instanceCount = 1)
        {
            Name = name;
            Xaml = xaml;
            Script = script;
            InstanceCount = instanceCount;
        }

        public string Name { get; }
        public string Xaml { get; }
        public string Script { get; }
        public int InstanceCount { get; }
    }

    private sealed class PlaygroundDomDocument : AvaloniaDomDocument
    {
        private readonly Control _root;

        public PlaygroundDomDocument(AvaloniaBrowserHost host, Control root)
            : base(host)
        {
            _root = root;
        }

        protected override Control? GetDocumentRoot()
            => _root;
    }
}

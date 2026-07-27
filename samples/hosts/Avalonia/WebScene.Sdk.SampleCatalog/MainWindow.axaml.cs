using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using WebScene.Sdk;
using WebScene.Sdk.Avalonia;

namespace WebScene.Sdk.SampleCatalog;

public sealed partial class MainWindow : Window
{
    private CatalogSample[] _samples = [];
    private readonly List<WebSceneComponentHost> _currentHosts = [];
    private Exception? _lastMountFailure;
    private Action? _scenarioSmokeInteraction;

    public MainWindow()
    {
        InitializeComponent();
        Opened += async (_, _) =>
        {
            LoadCatalog();
            if (Program.SmokeMode)
            {
                await RunCatalogSmokeAsync();
            }
        };
        Closed += (_, _) => DisposeCurrentHosts();
        SampleList.SelectionChanged += (_, _) => ShowSelectedSample();
    }

    private void LoadCatalog()
    {
        try
        {
            var path = Path.Combine(AppContext.BaseDirectory, "Catalog", "catalog.json");
            using var document = JsonDocument.Parse(File.ReadAllText(path));
            _samples = document.RootElement.GetProperty("samples")
                .EnumerateArray()
                .Select(static item => new CatalogSample(
                    item.GetProperty("id").GetString()!,
                    item.GetProperty("purpose").GetString()!,
                    item.GetProperty("expectedInteractions").GetString()!))
                .ToArray();
            SampleList.ItemsSource = _samples.Select(static sample => sample.Id).ToArray();
            var requestedIndex = Array.FindIndex(
                _samples,
                sample => string.Equals(sample.Id, Program.InitialSampleId, StringComparison.Ordinal));
            SampleList.SelectedIndex = requestedIndex >= 0 ? requestedIndex : 0;
            StatusText.Text = $"Loaded {_samples.Length} offline component packages.";
        }
        catch (Exception exception)
        {
            StatusText.Text = $"Catalog failed: {exception.Message}";
        }
    }

    private void ShowSelectedSample()
    {
        if (SampleList.SelectedIndex is < 0 || SampleList.SelectedIndex >= _samples.Length)
        {
            return;
        }

        var sample = _samples[SampleList.SelectedIndex];
        DisposeCurrentHosts();
        _scenarioSmokeInteraction = null;
        PreviewPresenter.Content = CreatePreview(sample);
        SampleTitle.Text = sample.Id;
        PackageText.Text = $"Catalog/{sample.Id}/webscene-component.json";
        PurposeText.Text = sample.Purpose;
        InteractionText.Text = $"Expected: {sample.ExpectedInteractions}";
        ReloadButton.IsEnabled = true;
        MountErrorPanel.IsVisible = false;
        _lastMountFailure = null;

        try
        {
            foreach (var host in _currentHosts.ToArray())
            {
                host.MountComponent();
            }
            StatusText.Text = $"Mounted {sample.Id} ({_currentHosts.Count} component instance{(_currentHosts.Count == 1 ? string.Empty : "s")}).";
            Console.WriteLine($"[WebScene sample catalog] Mounted {sample.Id} ({_currentHosts.Count} instances).");
        }
        catch (Exception exception)
        {
            _lastMountFailure = exception;
            var message = FormatMountFailure(exception);
            MountErrorText.Text = message;
            MountErrorPanel.IsVisible = true;
            StatusText.Text = message;
            Console.Error.WriteLine($"[WebScene sample catalog] {message}");
            Console.Error.WriteLine(exception);
        }
    }

    private void OnReload(object? sender, RoutedEventArgs e) => ShowSelectedSample();

    private async Task RunCatalogSmokeAsync()
    {
        await Task.Yield();
        var indexes = Program.InitialSampleId is null
            ? Enumerable.Range(0, _samples.Length)
            : new[] { Array.FindIndex(_samples, sample => sample.Id == Program.InitialSampleId) };
        var failures = new List<string>();
        foreach (var index in indexes.Where(static value => value >= 0))
        {
            SampleList.SelectedIndex = -1;
            SampleList.SelectedIndex = index;
            if (_lastMountFailure is null && _scenarioSmokeInteraction is not null)
            {
                try
                {
                    _scenarioSmokeInteraction();
                    if (_currentHosts.Any(host => host.ComponentState != WebSceneComponentState.Mounted))
                    {
                        throw new InvalidOperationException("A peer component was not mounted after the scenario interaction.");
                    }
                }
                catch (Exception exception)
                {
                    _lastMountFailure = exception;
                }
            }
            if (_lastMountFailure is null)
            {
                Console.WriteLine($"[WebScene sample catalog smoke] PASS {_samples[index].Id} ({_currentHosts.Count} instances)");
            }
            else
            {
                failures.Add($"{_samples[index].Id}: {_lastMountFailure.GetBaseException().Message}");
            }
        }
        Environment.ExitCode = failures.Count == 0 ? 0 : 1;
        if (failures.Count == 0)
        {
            Console.WriteLine($"[WebScene sample catalog smoke] PASS {indexes.Count()} scenario(s)");
        }
        else
        {
            Console.Error.WriteLine(string.Join(Environment.NewLine, failures));
        }
        Close();
    }

    private Control CreatePreview(CatalogSample sample)
        => sample.Id switch
        {
            "Hybrid.ReactIslands" => CreateHybridPreview(sample),
            "MultiInstanceWorkstation" => CreateMultiInstancePreview(sample),
            "WebToNativeMigration" => CreateMigrationPreview(sample),
            _ => CreateHost(sample.Id)
        };

    private Control CreateHybridPreview(CatalogSample sample)
    {
        var roundTrip = new TextBlock
        {
            Text = "Native command surface · waiting for an island event",
            Foreground = Brushes.White,
            VerticalAlignment = VerticalAlignment.Center
        };
        var grid = new Grid { RowDefinitions = new RowDefinitions("Auto,*") };
        grid.Children.Add(new Border
        {
            Background = new SolidColorBrush(Color.Parse("#164E63")),
            Padding = new Thickness(14, 9),
            Child = roundTrip
        });
        var islands = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,*"),
            ColumnSpacing = 10,
            Margin = new Thickness(10)
        };
        Grid.SetRow(islands, 1);
        for (var index = 0; index < 2; index++)
        {
            var label = $"React island {(char)('A' + index)}";
            var host = CreateHost(sample.Id, (capability, method, arguments) =>
                Dispatcher.UIThread.Post(() => roundTrip.Text = $"Native received {capability}.{method} from {label}: {CompactJson(arguments)}"));
            var panel = CreateLabeledHost(label, host);
            Grid.SetColumn(panel, index);
            islands.Children.Add(panel);
        }
        grid.Children.Add(islands);
        return grid;
    }

    private Control CreateMultiInstancePreview(CatalogSample sample)
    {
        var stateText = new TextBlock
        {
            Text = "Four isolated runtimes · one immutable package cache",
            Foreground = Brushes.White,
            VerticalAlignment = VerticalAlignment.Center
        };
        var recycle = new Button { Content = "Recycle fourth instance" };
        var commandBar = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,Auto"),
            Background = new SolidColorBrush(Color.Parse("#312E81")),
            Margin = new Thickness(0),
        };
        commandBar.Children.Add(stateText);
        Grid.SetColumn(recycle, 1);
        commandBar.Children.Add(recycle);
        var outer = new Grid { RowDefinitions = new RowDefinitions("Auto,*") };
        outer.Children.Add(new Border { Padding = new Thickness(12, 8), Background = commandBar.Background, Child = commandBar });
        var workstation = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,*"),
            RowDefinitions = new RowDefinitions("*,*"),
            ColumnSpacing = 8,
            RowSpacing = 8,
            Margin = new Thickness(8)
        };
        Grid.SetRow(workstation, 1);
        WebSceneComponentHost? fourthHost = null;
        for (var index = 0; index < 4; index++)
        {
            var host = CreateHost(sample.Id);
            Grid.SetColumn(host, index % 2);
            Grid.SetRow(host, index / 2);
            workstation.Children.Add(host);
            if (index == 3)
            {
                fourthHost = host;
            }
        }
        void RecycleFourthInstance()
        {
            var oldHost = fourthHost;
            if (oldHost is not null)
            {
                workstation.Children.Remove(oldHost);
                oldHost.Dispose();
                _currentHosts.Remove(oldHost);
            }
            var replacement = CreateHost(sample.Id);
            Grid.SetColumn(replacement, 1);
            Grid.SetRow(replacement, 1);
            workstation.Children.Add(replacement);
            fourthHost = replacement;
            replacement.MountComponent();
            stateText.Text = "Fourth instance disposed and remounted; peers stayed alive";
        }
        recycle.Click += (_, _) =>
        {
            try
            {
                RecycleFourthInstance();
            }
            catch (Exception exception) { stateText.Text = FormatMountFailure(exception); }
        };
        _scenarioSmokeInteraction = RecycleFourthInstance;
        outer.Children.Add(workstation);
        return outer;
    }

    private Control CreateMigrationPreview(CatalogSample sample)
    {
        var orderId = new TextBox { Text = "1042", Watermark = "Order" };
        var customer = new TextBox { Text = "Northwind", Watermark = "Customer" };
        var status = new ComboBox { ItemsSource = new[] { "Ready", "Review", "Queued" }, SelectedIndex = 0 };
        var nativeEditor = new StackPanel
        {
            Spacing = 10,
            Children =
            {
                new TextBlock { Text = "Native Avalonia editor", FontSize = 20, FontWeight = FontWeight.SemiBold },
                new TextBlock { Text = "Selection from the React list updates these native controls.", TextWrapping = TextWrapping.Wrap },
                new TextBlock { Text = "Order number" }, orderId,
                new TextBlock { Text = "Customer" }, customer,
                new TextBlock { Text = "Status" }, status,
                new Button { Content = "Save in native shell" }
            }
        };
        var host = CreateHost(sample.Id, (capability, method, arguments) =>
        {
            if (method != "selectionChanged")
            {
                return;
            }
            Dispatcher.UIThread.Post(() =>
            {
                if (arguments.TryGetProperty("id", out var id)) orderId.Text = id.ToString();
                if (arguments.TryGetProperty("customer", out var name)) customer.Text = name.GetString();
                if (arguments.TryGetProperty("status", out var state)) status.SelectedItem = state.GetString();
            });
        });
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("3*,2*"), ColumnSpacing = 12, Margin = new Thickness(12) };
        grid.Children.Add(host);
        var editorBorder = new Border
        {
            Padding = new Thickness(20),
            CornerRadius = new CornerRadius(8),
            Background = new SolidColorBrush(Color.Parse("#E2E8F0")),
            Child = nativeEditor
        };
        Grid.SetColumn(editorBorder, 1);
        grid.Children.Add(editorBorder);
        return grid;
    }

    private WebSceneComponentHost CreateHost(
        string sampleId,
        Action<string, string, JsonElement>? hostInvocation = null)
    {
        var host = new WebSceneComponentHost
        {
            PackagePath = Path.Combine("Catalog", sampleId),
            AutoMount = false,
            Background = Brushes.White,
            Foreground = new SolidColorBrush(Color.Parse("#0F172A"))
        };
        ConfigureCapabilities(host, sampleId, hostInvocation);
        host.DiagnosticReported += (_, diagnostic) =>
            Dispatcher.UIThread.Post(() => StatusText.Text = $"{diagnostic.Code}: {diagnostic.Message}");
        _currentHosts.Add(host);
        return host;
    }

    private static Control CreateLabeledHost(string label, WebSceneComponentHost host)
    {
        var panel = new Grid { RowDefinitions = new RowDefinitions("Auto,*") };
        panel.Children.Add(new TextBlock { Text = label, FontWeight = FontWeight.SemiBold, Margin = new Thickness(4, 0, 0, 6) });
        Grid.SetRow(host, 1);
        panel.Children.Add(host);
        return panel;
    }

    private void DisposeCurrentHosts()
    {
        foreach (var host in _currentHosts)
        {
            host.Dispose();
        }
        _currentHosts.Clear();
    }

    private static void ConfigureCapabilities(
        WebSceneComponentHost host,
        string sampleId,
        Action<string, string, JsonElement>? hostInvocation = null)
    {
        var manifestPath = Path.Combine(AppContext.BaseDirectory, "Catalog", sampleId, "webscene-component.json");
        var manifest = WebSceneComponentManifestSerializer.Parse(File.ReadAllText(manifestPath));
        foreach (var capability in manifest.Capabilities.Where(static value => value.StartsWith("host.", StringComparison.Ordinal)))
        {
            host.RegisterHostCapability(new WebSceneDelegateCapabilityHandler(
                capability,
                (method, arguments, _) =>
                {
                    hostInvocation?.Invoke(capability, method, arguments);
                    return ValueTask.FromResult<JsonElement?>(
                        JsonSerializer.SerializeToElement(new { accepted = true, capability, method, arguments }));
                }));
        }
    }

    private static string CompactJson(JsonElement value)
    {
        var text = value.GetRawText();
        return text.Length <= 72 ? text : $"{text[..69]}…";
    }

    private static string FormatMountFailure(Exception exception)
    {
        var failure = exception.ToString();
        if (exception.GetBaseException() is DllNotFoundException
            || failure.Contains("Unable to load shared library", StringComparison.OrdinalIgnoreCase)
            || failure.Contains("dlopen(", StringComparison.OrdinalIgnoreCase))
        {
            var rid = RuntimeInformation.RuntimeIdentifier;
            var preparation = OperatingSystem.IsWindows()
                ? "Follow third-party/clearscript-patches/README.md to build the matching Windows native library."
                : $"Run scripts/build-clearscript-v8-native.sh --rid {rid} --download-v8.";
            return "The reviewed ClearScript V8 native library is missing for this platform. "
                   + preparation
                   + " Alternatively, set WEBSCENE_CLEARSCRIPT_NATIVE and WEBSCENE_CLEARSCRIPT_RID to an existing reviewed build.";
        }

        return $"Mount failed: {exception.GetBaseException().Message.Split('\n')[0]}";
    }

    private sealed record CatalogSample(string Id, string Purpose, string ExpectedInteractions);
}

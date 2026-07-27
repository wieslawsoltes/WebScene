using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Interactivity;
using WebScene.Sdk;
using WebScene.Sdk.Avalonia;

namespace WebScene.Samples.Hybrid;

public sealed partial class MainWindow : Window
{
    private int _nativeCommandCount;

    public MainWindow()
    {
        InitializeComponent();
        Configure(PrimaryHost);
        Configure(SecondaryHost);
        Opened += (_, _) => MountComponents();
        Closed += (_, _) =>
        {
            PrimaryHost.Dispose();
            SecondaryHost.Dispose();
        };
    }

    private void MountComponents()
    {
        try
        {
            PrimaryHost.MountComponent();
            SecondaryHost.MountComponent();
            StatusText.Text = "Mounted two isolated instances of dev.webscene.hybrid-reactislands";
        }
        catch (Exception exception)
        {
            StatusText.Text = $"Mount failed: {exception.Message}";
        }
    }

    private void OnNativeCommand(object? sender, RoutedEventArgs e)
        => CommandText.Text = $"Native command #{++_nativeCommandCount}";

    private static void Configure(WebSceneComponentHost host)
    {
        host.RegisterHostCapability(CreateHandler(WebSceneComponentCapabilities.Commands));
        host.RegisterHostCapability(CreateHandler(WebSceneComponentCapabilities.Settings));
    }

    private static WebSceneDelegateCapabilityHandler CreateHandler(string capability)
        => new(capability, (method, arguments, _) =>
            ValueTask.FromResult<JsonElement?>(JsonSerializer.SerializeToElement(new { accepted = true, capability, method, arguments })));
}

using System.Text.Json;
using Avalonia.Controls;
using WebScene.Sdk;
using WebScene.Sdk.Avalonia;

namespace WebScene.Samples.TypeScriptDesktop;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        PrimaryHost.RegisterHostCapability(CreateHandler(WebSceneComponentCapabilities.Settings));
        PrimaryHost.RegisterHostCapability(CreateHandler(WebSceneComponentCapabilities.Notifications));
        Opened += (_, _) => MountComponent();
        Closed += (_, _) => PrimaryHost.Dispose();
    }

    private void MountComponent()
    {
        try
        {
            PrimaryHost.MountComponent();
            StatusText.Text = "Mounted dev.webscene.typescriptdesktop";
        }
        catch (Exception exception)
        {
            StatusText.Text = $"Mount failed: {exception.Message}";
        }
    }

    private static WebSceneDelegateCapabilityHandler CreateHandler(string capability)
        => new(capability, (method, arguments, _) =>
            ValueTask.FromResult<JsonElement?>(JsonSerializer.SerializeToElement(new { accepted = true, capability, method, arguments })));
}

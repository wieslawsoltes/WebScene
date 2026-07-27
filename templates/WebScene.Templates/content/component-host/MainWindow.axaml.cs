using System.Text.Json;
using Avalonia.Controls;
using WebScene.Sdk;
using WebScene.Sdk.Avalonia;

namespace WebSceneComponentHost;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Opened += (_, _) => MountComponents();
    }

    private void MountComponents()
    {
        Configure(PrimaryHost);
        PrimaryHost.MountComponent();
    }

    private static void Configure(WebSceneComponentHost host)
    {
        foreach (var capability in new[] { WebSceneComponentCapabilities.Commands, WebSceneComponentCapabilities.Settings, WebSceneComponentCapabilities.Notifications })
        {
            if (!File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Component", "webscene-component.json")).Contains($"\\\"{capability}\\\"", StringComparison.Ordinal)) continue;
            host.RegisterHostCapability(new WebSceneDelegateCapabilityHandler(capability, (_, arguments, _) =>
                ValueTask.FromResult<JsonElement?>(JsonSerializer.SerializeToElement(new { accepted = true, arguments }))));
        }
    }
}

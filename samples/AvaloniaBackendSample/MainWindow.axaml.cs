using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using WebScene.Backends.Avalonia;
using WebScene.Core;

namespace AvaloniaBackendSample;

public sealed partial class MainWindow : Window
{
    private AvaloniaBackendHost? _backend;

    public MainWindow()
    {
        InitializeComponent();
        Opened += OnOpened;
        Closed += OnClosed;
    }

    private void OnOpened(object? sender, EventArgs args)
    {
        _backend = new AvaloniaBackendHost(this);
        _backend.Mount();
        _backend.EnsureCapabilities(
            WebSceneBackendCapabilities.DomProjection
            | WebSceneBackendCapabilities.CssLayout
            | WebSceneBackendCapabilities.Canvas2D
            | WebSceneBackendCapabilities.Svg
            | WebSceneBackendCapabilities.PointerInput
            | WebSceneBackendCapabilities.KeyboardInput
            | WebSceneBackendCapabilities.Focus
            | WebSceneBackendCapabilities.Accessibility);

        var root = _backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(1),
            WebSceneBackendNodeKind.Container,
            "WebScene backend sample"));
        var heading = _backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(2),
            WebSceneBackendNodeKind.Text,
            "WebScene.Backend.Avalonia"));
        var detail = _backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(3),
            WebSceneBackendNodeKind.Text,
            "Persistent Avalonia visuals projected through IWebSceneBackendHost"));

        _backend.Attach(_backend.Root, root, 0);
        _backend.Attach(root, heading, 0);
        _backend.Attach(root, detail, 1);
        _backend.Arrange(heading, new WebSceneRect(48, 56, 600, 48));
        _backend.Arrange(detail, new WebSceneRect(48, 116, 620, 36));

        var headingControl = heading.Handle.GetRequired<TextBlock>();
        headingControl.FontSize = 30;
        headingControl.FontWeight = FontWeight.SemiBold;
        headingControl.Foreground = Brushes.White;
        var detailControl = detail.Handle.GetRequired<TextBlock>();
        detailControl.FontSize = 16;
        detailControl.Foreground = new SolidColorBrush(Color.Parse("#AEBCC8"));
    }

    private void OnClosed(object? sender, EventArgs args)
    {
        _backend?.Dispose();
        _backend = null;
    }
}

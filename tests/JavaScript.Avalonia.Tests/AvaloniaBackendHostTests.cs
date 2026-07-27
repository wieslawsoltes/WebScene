using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends;
using WebScene.Core;
using JavaScript.Avalonia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class AvaloniaBackendHostTests
{
    [AvaloniaFact]
    public void NativeSceneSurfaceIsOwnedByTheBackendAndAcceptsFocus()
    {
        var surface = new NativeSceneSurface(IntPtr.Zero);

        Assert.Equal("WebScene.Backend.Avalonia", surface.GetType().Assembly.GetName().Name);
        Assert.True(surface.Focusable);
        Assert.True(surface.ClipToBounds);
        Assert.IsAssignableFrom<INativeWebSceneRenderDiagnostics>(surface);
    }

    [AvaloniaFact]
    public void PublicBackendProjectsNodesThroughThePortableContract()
    {
        var window = new Window { Width = 320, Height = 200 };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var backend = new AvaloniaBackendHost(window);
        Assert.Equal(WebSceneBackendState.Created, backend.State);
        Assert.Same(window, backend.Root.Handle.GetRequired<TopLevel>());
        Assert.Equal(AvaloniaBackendHost.DefaultCapabilities, backend.Capabilities);

        backend.Mount();
        var root = backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(1),
            WebSceneBackendNodeKind.Container,
            "sample-root"));
        var text = backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(2),
            WebSceneBackendNodeKind.Text,
            "Hello from the Avalonia backend"));

        backend.Attach(backend.Root, root, 0);
        backend.Attach(root, text, 0);
        backend.Arrange(text, new WebSceneRect(20, 30, 180, 32));
        backend.SetZIndex(text, 7);
        backend.SetVisible(text, true);
        backend.Invalidate(
            text,
            WebSceneInvalidationKind.Measure
            | WebSceneInvalidationKind.Arrange
            | WebSceneInvalidationKind.Render
            | WebSceneInvalidationKind.Accessibility);
        Dispatcher.UIThread.RunJobs();

        var rootControl = root.Handle.GetRequired<Canvas>();
        var textControl = text.Handle.GetRequired<TextBlock>();
        Assert.Same(rootControl, window.Content);
        Assert.Same(rootControl, textControl.GetVisualParent());
        Assert.Equal(7, textControl.GetValue(Canvas.ZIndexProperty));
        Assert.True(textControl.IsVisible);
        Assert.Equal(text, backend.HitTest(new WebScenePoint(25, 35)));

        backend.Detach(text);
        Assert.Null(textControl.GetVisualParent());
        backend.Unmount();
        Assert.Null(window.Content);
        Assert.Equal(WebSceneBackendState.Unmounted, backend.State);
        window.Close();
    }

    [AvaloniaFact]
    public void BrowserHostPublishesAndOwnsTheRealBackendContract()
    {
        var window = new Window
        {
            Width = 320,
            Height = 200,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        var host = new AvaloniaBrowserHost(window);
        var backend = host.Backend;
        Assert.Equal(WebSceneBackendState.Mounted, backend.State);
        backend.EnsureCapabilities(
            WebSceneBackendCapabilities.DomProjection
            | WebSceneBackendCapabilities.CssLayout
            | WebSceneBackendCapabilities.Canvas2D
            | WebSceneBackendCapabilities.Svg
            | WebSceneBackendCapabilities.PointerInput
            | WebSceneBackendCapabilities.KeyboardInput
            | WebSceneBackendCapabilities.TextInput
            | WebSceneBackendCapabilities.Focus
            | WebSceneBackendCapabilities.Clipboard
            | WebSceneBackendCapabilities.Accessibility
            | WebSceneBackendCapabilities.InputMethodEditor
            | WebSceneBackendCapabilities.OpenGl);

        var exception = Assert.Throws<WebSceneBackendCapabilityException>(
            () => backend.EnsureCapabilities(WebSceneBackendCapabilities.WebGpu));
        Assert.Equal(WebSceneBackendCapabilities.WebGpu, exception.Missing);
        Assert.Single(backend.Diagnostics);

        host.Dispose();
        Assert.Equal(WebSceneBackendState.Disposed, backend.State);
        window.Close();
    }

    [AvaloniaFact]
    public void PublishedManifestMatchesTheRuntimeAndAdvancedProfileClaim()
    {
        var window = new Window { Width = 160, Height = 100 };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        using var backend = new AvaloniaBackendHost(window);
        backend.Mount();

        var manifestPath = Path.Combine(
            FindRepositoryRoot(),
            "src",
            "WebScene.Backend.Avalonia",
            "webscene-backend.json");
        using var stream = File.OpenRead(manifestPath);
        var manifest = WebSceneBackendManifestSerializer.Read(stream);
        WebSceneBackendContractVerifier.Verify(
            backend,
            manifest,
            WebSceneBackendSupportLevel.Advanced);
        Assert.Equal(typeof(AvaloniaBackendHost).FullName, manifest.BackendType);
        window.Close();
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln")))
            {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate WebScene.sln.");
    }
}

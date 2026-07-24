using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Avalonia.VisualTree;
using HtmlML.Backends.Avalonia;
using HtmlML.Backends.Avalonia.Native;
using HtmlML.Backends;
using HtmlML.Core;
using JavaScript.Avalonia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class AvaloniaBackendHostTests
{
    [AvaloniaFact]
    public void NativeSceneSurfaceIsOwnedByTheBackendAndAcceptsFocus()
    {
        var surface = new NativeSceneSurface(IntPtr.Zero);

        Assert.Equal("HtmlML.Backend.Avalonia", surface.GetType().Assembly.GetName().Name);
        Assert.True(surface.Focusable);
        Assert.True(surface.ClipToBounds);
        Assert.IsAssignableFrom<INativeHtmlMlRenderDiagnostics>(surface);
    }

    [AvaloniaFact]
    public void PublicBackendProjectsNodesThroughThePortableContract()
    {
        var window = new Window { Width = 320, Height = 200 };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var backend = new AvaloniaBackendHost(window);
        Assert.Equal(HtmlMlBackendState.Created, backend.State);
        Assert.Same(window, backend.Root.Handle.GetRequired<TopLevel>());
        Assert.Equal(AvaloniaBackendHost.DefaultCapabilities, backend.Capabilities);

        backend.Mount();
        var root = backend.CreateNode(new HtmlMlBackendNodeDescriptor(
            new HtmlMlNodeId(1),
            HtmlMlBackendNodeKind.Container,
            "sample-root"));
        var text = backend.CreateNode(new HtmlMlBackendNodeDescriptor(
            new HtmlMlNodeId(2),
            HtmlMlBackendNodeKind.Text,
            "Hello from the Avalonia backend"));

        backend.Attach(backend.Root, root, 0);
        backend.Attach(root, text, 0);
        backend.Arrange(text, new HtmlMlRect(20, 30, 180, 32));
        backend.SetZIndex(text, 7);
        backend.SetVisible(text, true);
        backend.Invalidate(
            text,
            HtmlMlInvalidationKind.Measure
            | HtmlMlInvalidationKind.Arrange
            | HtmlMlInvalidationKind.Render
            | HtmlMlInvalidationKind.Accessibility);
        Dispatcher.UIThread.RunJobs();

        var rootControl = root.Handle.GetRequired<Canvas>();
        var textControl = text.Handle.GetRequired<TextBlock>();
        Assert.Same(rootControl, window.Content);
        Assert.Same(rootControl, textControl.GetVisualParent());
        Assert.Equal(7, textControl.GetValue(Canvas.ZIndexProperty));
        Assert.True(textControl.IsVisible);
        Assert.Equal(text, backend.HitTest(new HtmlMlPoint(25, 35)));

        backend.Detach(text);
        Assert.Null(textControl.GetVisualParent());
        backend.Unmount();
        Assert.Null(window.Content);
        Assert.Equal(HtmlMlBackendState.Unmounted, backend.State);
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
        Assert.Equal(HtmlMlBackendState.Mounted, backend.State);
        backend.EnsureCapabilities(
            HtmlMlBackendCapabilities.DomProjection
            | HtmlMlBackendCapabilities.CssLayout
            | HtmlMlBackendCapabilities.Canvas2D
            | HtmlMlBackendCapabilities.Svg
            | HtmlMlBackendCapabilities.PointerInput
            | HtmlMlBackendCapabilities.KeyboardInput
            | HtmlMlBackendCapabilities.TextInput
            | HtmlMlBackendCapabilities.Focus
            | HtmlMlBackendCapabilities.Clipboard
            | HtmlMlBackendCapabilities.Accessibility
            | HtmlMlBackendCapabilities.InputMethodEditor
            | HtmlMlBackendCapabilities.OpenGl);

        var exception = Assert.Throws<HtmlMlBackendCapabilityException>(
            () => backend.EnsureCapabilities(HtmlMlBackendCapabilities.WebGpu));
        Assert.Equal(HtmlMlBackendCapabilities.WebGpu, exception.Missing);
        Assert.Single(backend.Diagnostics);

        host.Dispose();
        Assert.Equal(HtmlMlBackendState.Disposed, backend.State);
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
            "HtmlML.Backend.Avalonia",
            "htmlml-backend.json");
        using var stream = File.OpenRead(manifestPath);
        var manifest = HtmlMlBackendManifestSerializer.Read(stream);
        HtmlMlBackendContractVerifier.Verify(
            backend,
            manifest,
            HtmlMlBackendSupportLevel.Advanced);
        Assert.Equal(typeof(AvaloniaBackendHost).FullName, manifest.BackendType);
        window.Close();
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "HtmlML.sln")))
            {
                return directory.FullName;
            }
        }
        throw new DirectoryNotFoundException("Could not locate HtmlML.sln.");
    }
}

using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssAbsoluteGeneratedPseudoInitialContainingBlockTests
{
    [AvaloniaFact]
    public void EmptyAbsolutePseudosOnStaticZeroHeightHostPaintAgainstInitialContainingBlockInBothLanes()
    {
        var root = new CssLayoutPanel { Width = 200, Height = 120, Background = Brushes.White };
        var window = new Window
        {
            Width = 200,
            Height = 120,
            Content = root,
            Background = Brushes.White,
            SystemDecorations = SystemDecorations.None
        };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 120px; margin: 0; width: 200px; }
            #test::before, #test::after {
                background: blue;
                bottom: 0;
                content: "";
                height: 40px;
                position: absolute;
                right: 0;
                width: 20px;
            }
            #test::before { right: 20px; }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = """
            <p>Test passes if there is a square at the bottom right of the page.</p>
            <div id="test"></div>
            """;
        var element = HostTestUtilities.GetElement(body.querySelector("#test"));

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        document.FlushPendingLayout();

        var panel = Assert.IsType<CssLayoutPanel>(element.Control);
        var hostRect = element.getBoundingClientRect();
        Assert.Equal(0, hostRect.height);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(body.Control),
            new Size(200, 120));
        Assert.True(portable.TryGetPseudoBox(panel, before: true, out var portableBefore));
        Assert.True(portable.TryGetPseudoBox(panel, before: false, out var portableAfter));
        Assert.Equal((160d, 80d, 20d, 40d), ToTuple(portableBefore.BorderBox));
        Assert.Equal((180d, 80d, 20d, 40d), ToTuple(portableAfter.BorderBox));

        AssertLivePaint(panel, window, $"client={window.ClientSize} root={root.Bounds} body={body.Control.Bounds} host={panel.Bounds} doc={document.documentElement.clientWidth}x{document.documentElement.clientHeight}");

        CssLayout.SetNativeLayoutHotPath(root, false);
        root.InvalidateMeasure();
        root.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
        document.FlushPendingLayout();
        AssertLivePaint(panel, window, $"client={window.ClientSize} root={root.Bounds} body={body.Control.Bounds} host={panel.Bounds} doc={document.documentElement.clientWidth}x{document.documentElement.clientHeight}");

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    private static void AssertLivePaint(CssLayoutPanel panel, Window window, string diagnostic)
    {
        var controls = panel.Children.OfType<DomGeneratedBackgroundControl>().ToArray();
        Assert.Equal(2, controls.Length);
        var before = controls.Single(control => control.Before);
        var after = controls.Single(control => !control.Before);
        Assert.Equal(new Size(20, 40), before.Bounds.Size);
        Assert.Equal(new Size(20, 40), after.Bounds.Size);
        Assert.True(before.TranslatePoint(default, window) == new Point(160, 80),
            $"before={before.TranslatePoint(default, window)} {diagnostic}");
        Assert.True(after.TranslatePoint(default, window) == new Point(180, 80),
            $"after={after.TranslatePoint(default, window)} {diagnostic}");

        using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
        Assert.Equal(Colors.Blue, ReadPixel(frame, 160, 80));
        Assert.Equal(Colors.Blue, ReadPixel(frame, 199, 119));
        Assert.Equal(Colors.White, ReadPixel(frame, 159, 80));
        Assert.Equal(Colors.White, ReadPixel(frame, 160, 79));
    }

    private static (double X, double Y, double Width, double Height) ToTuple(WebScene.Core.WebSceneRect rect)
        => (rect.X, rect.Y, rect.Width, rect.Height);

    private static Color ReadPixel(Bitmap bitmap, int x, int y)
    {
        var bytes = new byte[4];
        var handle = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(new PixelRect(x, y, 1, 1), handle.AddrOfPinnedObject(), bytes.Length, 4);
        }
        finally
        {
            handle.Free();
        }

        return bitmap.Format == PixelFormat.Rgba8888
            ? Color.FromArgb(bytes[3], bytes[0], bytes[1], bytes[2])
            : Color.FromArgb(bytes[3], bytes[2], bytes[1], bytes[0]);
    }
}

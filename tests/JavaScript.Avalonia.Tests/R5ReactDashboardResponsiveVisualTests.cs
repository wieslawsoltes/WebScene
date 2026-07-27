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

/// <summary>
/// Native visual authority for the responsive Metric grid authored by the R5
/// ReactDashboard sample. The declarations mirror shared/ui.tsx and
/// ReactDashboard/src/main.tsx; only a fixed card height removes font metrics
/// from this layout/raster contract.
/// </summary>
public sealed class R5ReactDashboardResponsiveVisualTests
{
    [AvaloniaFact]
    public void ShellPercentageMinimumHeightUsesAutoBodyContainingBlockLikeChrome()
    {
        using var fixture = CreateFixture();

        SetViewport(fixture, width: 600, height: 300);
        AssertRect(fixture.Shell.getBoundingClientRect(), 0, 0, 600, 128);

        SetViewport(fixture, width: 360, height: 420);
        AssertRect(fixture.Shell.getBoundingClientRect(), 0, 0, 360, 316);
    }

    [AvaloniaFact]
    public void FractionalWideFlexDistributionPreservesEveryAuthoredFourteenPixelGap()
    {
        using var fixture = CreateFixture();
        SetViewport(fixture, width: 600, height: 300);
        var cards = fixture.Cards.Select(static card => card.getBoundingClientRect()).ToArray();
        AssertPortableClose(14, cards[1].left - cards[0].right);
        AssertPortableClose(14, cards[2].left - cards[1].right);
    }

    [AvaloniaFact]
    public void MetricCardPaintsItsComputedOnePixelBorder()
    {
        using var fixture = CreateFixture();
        SetViewport(fixture, width: 601, height: 300);
        using var frame = CaptureRoot(fixture.Root);
        AssertAnyPixel(frame, Color.Parse("#cbd5e1"),
            (21, 60), (22, 60), (23, 60), (24, 60), (25, 60), (26, 60), (27, 60), (28, 60));
    }

    [AvaloniaFact]
    public void MetricGridMatchesChromeAcrossLiveWideToNarrowResizeInNativePortablePaintAndHits()
    {
        using var fixture = CreateFixture();

        SetViewport(fixture, width: 601, height: 300);
        AssertWideNativeGeometry(fixture);
        AssertWidePortableGeometry(fixture);
        AssertWideRaster(fixture);
        AssertCenterHits(fixture);

        SetViewport(fixture, width: 360, height: 420);
        AssertNarrowNativeGeometry(fixture);
        AssertNarrowPortableGeometry(fixture);
        AssertNarrowRaster(fixture);
        AssertCenterHits(fixture);
    }

    private static Fixture CreateFixture()
    {
        var root = new CssLayoutPanel
        {
            Width = 600,
            Height = 300,
            Background = Brushes.White
        };
        var window = new Window
        {
            Width = 600,
            Height = 300,
            Background = Brushes.White,
            Content = root
        };
        var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;

        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { margin: 0; }
            .shell {
                box-sizing: border-box;
                min-height: 100%;
                padding: 24px;
                color: #0f172a;
                background: #eef4fb;
                font-family: Inter, system-ui, sans-serif;
            }
            .grid {
                display: flex;
                flex-wrap: wrap;
                gap: 14px;
                align-items: stretch;
            }
            .card {
                box-sizing: border-box;
                padding: 16px;
                color: #0f172a;
                background: #ffffff;
                border: 1px solid #cbd5e1;
                border-radius: 12px;
                min-width: 155px;
                flex: 1 1 155px;
                height: 80px;
            }
            """;
        document.head.appendChild(style);

        var shell = HostTestUtilities.GetElement(document.createElement("main"));
        shell.className = "shell";
        var grid = HostTestUtilities.GetElement(document.createElement("div"));
        grid.className = "grid";
        var cards = Enumerable.Range(0, 3)
            .Select(index =>
            {
                var card = HostTestUtilities.GetElement(document.createElement("section"));
                card.id = $"metric-{(char)('a' + index)}";
                card.className = "card";
                grid.appendChild(card);
                return card;
            })
            .ToArray();
        shell.appendChild(grid);
        HostTestUtilities.GetElement(document.body).appendChild(shell);

        window.Show();
        document.EnsureStylesCurrent();
        foreach (var panel in new[] { shell.Control, grid.Control }
                     .Concat(cards.Select(static card => card.Control))
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
        }
        Dispatcher.UIThread.RunJobs();
        return new Fixture(window, host, document, root, shell, grid, cards);
    }

    private static void SetViewport(Fixture fixture, double width, double height)
    {
        fixture.Window.Width = width;
        fixture.Window.Height = height;
        fixture.Root.Width = width;
        fixture.Root.Height = height;
        foreach (var control in new[] { fixture.Root, fixture.Shell.Control, fixture.Grid.Control }
                     .Concat(fixture.Cards.Select(static card => card.Control)))
        {
            control.InvalidateMeasure();
            control.InvalidateArrange();
        }
        Dispatcher.UIThread.RunJobs();
        fixture.Document.FlushPendingLayout();
    }

    private static void AssertWideNativeGeometry(Fixture fixture)
    {
        AssertRect(fixture.Shell.getBoundingClientRect(), 0, 0, 601, 128);
        var grid = fixture.Grid.getBoundingClientRect();
        var cards = fixture.Cards.Select(static card => card.getBoundingClientRect()).ToArray();

        AssertRect(grid, 24, 24, 553, 80);
        AssertRect(cards[0], 24, 24, 175, 80);
        AssertRect(cards[1], 213, 24, 175, 80);
        AssertRect(cards[2], 402, 24, 175, 80);
        AssertClose(14, cards[1].left - cards[0].right);
        AssertClose(14, cards[2].left - cards[1].right);
        AssertClose(525, cards.Sum(static card => card.width));
    }

    private static void AssertNarrowNativeGeometry(Fixture fixture)
    {
        AssertRect(fixture.Shell.getBoundingClientRect(), 0, 0, 360, 316);
        var grid = fixture.Grid.getBoundingClientRect();
        var cards = fixture.Cards.Select(static card => card.getBoundingClientRect()).ToArray();

        AssertRect(grid, 24, 24, 312, 268);
        AssertRect(cards[0], 24, 24, 312, 80);
        AssertRect(cards[1], 24, 118, 312, 80);
        AssertRect(cards[2], 24, 212, 312, 80);
        AssertClose(14, cards[1].top - cards[0].bottom);
        AssertClose(14, cards[2].top - cards[1].bottom);
    }

    private static void AssertWidePortableGeometry(Fixture fixture)
    {
        var snapshot = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Shell.Control),
            new Size(601, 128));
        var grid = snapshot.GetBox(fixture.Grid.Control).BorderBox;
        var cards = fixture.Cards.Select(card => snapshot.GetBox(card.Control).BorderBox).ToArray();

        AssertPortableRect(grid, 24, 24, 553, 80);
        AssertPortableRect(cards[0], 24, 24, 175, 80);
        AssertPortableRect(cards[1], 213, 24, 175, 80);
        AssertPortableRect(cards[2], 402, 24, 175, 80);
        AssertPortableClose(14, cards[1].Left - cards[0].Right);
        AssertPortableClose(14, cards[2].Left - cards[1].Right);
    }

    private static void AssertNarrowPortableGeometry(Fixture fixture)
    {
        var snapshot = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Shell.Control),
            new Size(360, 316));
        var grid = snapshot.GetBox(fixture.Grid.Control).BorderBox;
        var cards = fixture.Cards.Select(card => snapshot.GetBox(card.Control).BorderBox).ToArray();

        AssertPortableRect(grid, 24, 24, 312, 268);
        AssertPortableRect(cards[0], 24, 24, 312, 80);
        AssertPortableRect(cards[1], 24, 118, 312, 80);
        AssertPortableRect(cards[2], 24, 212, 312, 80);
        AssertPortableClose(14, cards[1].Top - cards[0].Bottom);
        AssertPortableClose(14, cards[2].Top - cards[1].Bottom);
    }

    private static void AssertWideRaster(Fixture fixture)
    {
        using var frame = CaptureRoot(fixture.Root);
        AssertPixel(frame, 5, 5, Color.Parse("#eef4fb"));
        AssertPixel(frame, 80, 60, Colors.White);
        AssertPixel(frame, 205, 60, Color.Parse("#eef4fb"));
    }

    private static void AssertNarrowRaster(Fixture fixture)
    {
        using var frame = CaptureRoot(fixture.Root);
        AssertPixel(frame, 5, 5, Color.Parse("#eef4fb"));
        AssertPixel(frame, 80, 60, Colors.White);
        AssertPixel(frame, 80, 111, Color.Parse("#eef4fb"));
    }

    private static void AssertCenterHits(Fixture fixture)
    {
        foreach (var card in fixture.Cards)
        {
            var rect = card.getBoundingClientRect();
            Assert.Same(card, fixture.Document.elementFromPoint(
                rect.left + rect.width / 2,
                rect.top + rect.height / 2));
        }
    }

    private static void AssertRect(DomRect actual, double x, double y, double width, double height)
    {
        AssertClose(x, actual.x);
        AssertClose(y, actual.y);
        AssertClose(width, actual.width);
        AssertClose(height, actual.height);
    }

    private static void AssertPortableRect(WebScene.Core.WebSceneRect actual, double x, double y, double width, double height)
    {
        AssertPortableClose(x, actual.X);
        AssertPortableClose(y, actual.Y);
        AssertPortableClose(width, actual.Width);
        AssertPortableClose(height, actual.Height);
    }

    private static void AssertClose(double expected, double actual)
        => Assert.InRange(actual, expected - 0.5, expected + 0.5);

    private static void AssertPortableClose(double expected, double actual)
        => Assert.InRange(actual, expected - 0.05, expected + 0.05);

    private static void AssertPixel(Bitmap bitmap, int x, int y, Color expected)
        => Assert.Equal(expected, ReadPixel(bitmap, x, y));

    private static RenderTargetBitmap CaptureRoot(CssLayoutPanel root)
    {
        var size = root.Bounds.Size;
        var frame = new RenderTargetBitmap(
            new PixelSize((int)Math.Ceiling(size.Width), (int)Math.Ceiling(size.Height)),
            new Vector(96, 96));
        frame.Render(root);
        return frame;
    }

    private static void AssertAnyPixel(Bitmap bitmap, Color expected, params (int X, int Y)[] points)
    {
        var actual = points.Select(point => (point, Color: ReadPixel(bitmap, point.X, point.Y))).ToArray();
        Assert.True(
            actual.Any(item => item.Color == expected),
            $"Expected {expected}; sampled {string.Join(", ", actual.Select(item => $"{item.point}={item.Color}"))}.");
    }

    private static Color ReadPixel(Bitmap bitmap, int x, int y)
    {
        var bytes = new byte[4];
        var handle = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(new PixelRect(x, y, 1, 1), handle.AddrOfPinnedObject(), 4, 4);
        }
        finally
        {
            handle.Free();
        }
        return bitmap.Format == PixelFormat.Rgba8888
            ? Color.FromArgb(bytes[3], bytes[0], bytes[1], bytes[2])
            : Color.FromArgb(bytes[3], bytes[2], bytes[1], bytes[0]);
    }

    private sealed class Fixture(
        Window window,
        AvaloniaBrowserHost host,
        AvaloniaDomDocument document,
        CssLayoutPanel root,
        AvaloniaDomElement shell,
        AvaloniaDomElement grid,
        AvaloniaDomElement[] cards) : IDisposable
    {
        public Window Window { get; } = window;
        public AvaloniaDomDocument Document { get; } = document;
        public CssLayoutPanel Root { get; } = root;
        public AvaloniaDomElement Shell { get; } = shell;
        public AvaloniaDomElement Grid { get; } = grid;
        public AvaloniaDomElement[] Cards { get; } = cards;

        public void Dispose()
        {
            host.Dispose();
            Window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

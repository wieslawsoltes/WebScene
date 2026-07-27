using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using WebScene.Css;
using Xunit;

namespace JavaScript.Avalonia.Tests;

/// <summary>
/// Fractional flex allocation authority for CSS Flexbox 1 section 9.7,
/// "Resolving Flexible Lengths". The existing upstream
/// css/css-flexbox/flexbox-flex-wrap-horiz-001.html profile verifies line
/// collection and flexible sizing; this spike additionally pins Chromium's
/// 1/64 CSS-pixel used-value precision and verifies that authored gaps are not
/// consumed by native device-pixel layout rounding.
/// </summary>
public sealed class CssFractionalFlexDistributionTests
{
    [Fact]
    public void LayoutUnitResidualsPreferLargestFractionThenLaterSourceItem()
    {
        double[] unequal = [1.10, 1.20, 1.30];
        CssSubpixelFlexAllocator.QuantizeFinalMainSizes(unequal);
        Assert.Equal(70d / 64, unequal[0]);
        Assert.Equal(77d / 64, unequal[1]);
        Assert.Equal(83d / 64, unequal[2]);
        Assert.Equal(230d / 64, unequal.Sum(), 9);

        double[] equal = [174d + 2d / 3, 174d + 2d / 3, 174d + 2d / 3];
        CssSubpixelFlexAllocator.QuantizeFinalMainSizes(equal);
        Assert.Equal(174.65625, equal[0]);
        Assert.Equal(174.671875, equal[1]);
        Assert.Equal(174.671875, equal[2]);
        Assert.Equal(524, equal.Sum(), 9);
    }

    [AvaloniaFact]
    public void EqualGrowItemsUseDeterministicSubpixelResidualsAndKeepEveryFourteenPixelGap()
    {
        using var fixture = CreateFlexFixture();

        var grid = fixture.Grid.getBoundingClientRect();
        var native = fixture.Cards.Select(static card => card.getBoundingClientRect()).ToArray();
        AssertRect(grid, 24, 0, 552, 20);
        AssertRect(native[0], 24, 0, 174.65625, 20);
        AssertRect(native[1], 212.65625, 0, 174.671875, 20);
        AssertRect(native[2], 401.328125, 0, 174.671875, 20);
        Assert.Equal(14, native[1].left - native[0].right, 9);
        Assert.Equal(14, native[2].left - native[1].right, 9);
        Assert.Equal(524, native.Sum(static card => card.width), 9);

        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Shell.Control),
            new Size(600, 20));
        var portableCards = fixture.Cards
            .Select(card => portable.GetBox(card.Control).BorderBox)
            .ToArray();
        AssertPortableRect(portableCards[0], 24, 0, 174.65625, 20);
        AssertPortableRect(portableCards[1], 212.65625, 0, 174.671875, 20);
        AssertPortableRect(portableCards[2], 401.328125, 0, 174.671875, 20);
        Assert.Equal(14, portableCards[1].Left - portableCards[0].Right, 9);
        Assert.Equal(14, portableCards[2].Left - portableCards[1].Right, 9);

        using var frame = Assert.IsAssignableFrom<Bitmap>(fixture.Window.CaptureRenderedFrame());
        var gap = Color.Parse("#0b57d0");
        var card = Color.Parse("#ff2d55");
        AssertSolidRun(frame, 199, 211, 10, gap);
        AssertSolidRun(frame, 388, 400, 10, gap);
        AssertBlendedEdge(frame, 198, 10, gap, card);
        AssertBlendedEdge(frame, 212, 10, gap, card);
        AssertBlendedEdge(frame, 387, 10, gap, card);
        AssertBlendedEdge(frame, 401, 10, gap, card);
        Assert.Equal(card, ReadPixel(frame, 197, 10));
        Assert.Equal(Color.Parse("#ff2d55"), ReadPixel(frame, 213, 10));
        Assert.Equal(Color.Parse("#ff2d55"), ReadPixel(frame, 386, 10));
        Assert.Equal(card, ReadPixel(frame, 402, 10));
    }

    [AvaloniaFact]
    public void ResidualOwnershipUsesOrderModifiedSourceOrderBeforeRowReversePlacement()
    {
        using var fixture = CreateFlexFixture(reverseOrdered: true);
        var native = fixture.Cards.Select(static card => card.getBoundingClientRect()).ToArray();

        // Order-modified sequence is card 1, card 2, card 0. Residual units are
        // assigned in that sequence before row-reverse changes visual placement.
        Assert.Equal(174.671875, native[0].width, 9);
        Assert.Equal(174.65625, native[1].width, 9);
        Assert.Equal(174.671875, native[2].width, 9);
        Assert.Equal(24, native[0].left, 9);
        Assert.Equal(212.671875, native[2].left, 9);
        Assert.Equal(401.34375, native[1].left, 9);

        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Shell.Control),
            new Size(600, 20));
        var portableCards = fixture.Cards
            .Select(card => portable.GetBox(card.Control).BorderBox)
            .ToArray();
        for (var index = 0; index < native.Length; index++)
        {
            Assert.Equal(native[index].left, portableCards[index].Left, 9);
            Assert.Equal(native[index].width, portableCards[index].Width, 9);
        }
    }

    [AvaloniaFact]
    public void SubpixelRoundingContextFollowsDisplayMutationAndReparenting()
    {
        using var fixture = CreateFlexFixture();
        Assert.All(fixture.Cards, static card =>
        {
            Assert.False(card.Control.UseLayoutRounding);
        });

        fixture.Grid.style.setProperty("display", "block");
        fixture.FlushLayout();
        Assert.All(fixture.Cards, static card =>
        {
            Assert.True(card.Control.UseLayoutRounding);
        });

        fixture.Grid.style.setProperty("display", "flex");
        fixture.FlushLayout();
        Assert.All(fixture.Cards, static card =>
            Assert.False(card.Control.UseLayoutRounding));

        var blockHost = HostTestUtilities.GetElement(fixture.Document.createElement("div"));
        blockHost.style.cssText = "display: block; width: 552px; height: 20px";
        HostTestUtilities.GetElement(fixture.Document.body).appendChild(blockHost);
        blockHost.appendChild(fixture.Cards[0]);
        fixture.FlushLayout();

        Assert.True(fixture.Cards[0].Control.UseLayoutRounding);
        Assert.False(fixture.Cards[1].Control.UseLayoutRounding);
        Assert.False(fixture.Cards[2].Control.UseLayoutRounding);
    }

    [AvaloniaFact]
    public void FractionalLayoutDoesNotChangeIntegralBlockBorderOrHitGeometry()
    {
        using var fixture = CreateIntegralFixture();
        var box = fixture.Box.getBoundingClientRect();
        AssertRect(box, 10, 8, 80, 30);
        Assert.Same(fixture.Box, fixture.Document.elementFromPoint(10, 8));
        Assert.Same(fixture.Box, fixture.Document.elementFromPoint(89, 37));
        Assert.NotSame(fixture.Box, fixture.Document.elementFromPoint(90, 38));

        using var frame = Assert.IsAssignableFrom<Bitmap>(fixture.Window.CaptureRenderedFrame());
        Assert.Equal(Color.Parse("#f9fafb"), ReadPixel(frame, 11, 9));
        Assert.Equal(new Thickness(1), Assert.IsType<CssLayoutPanel>(fixture.Box.Control).BorderThickness);
    }

    private static Fixture CreateFlexFixture(bool reverseOrdered = false)
    {
        var fixture = CreateFixture(600, 20);
        var style = HostTestUtilities.GetElement(fixture.Document.createElement("style"));
        style.textContent = """
            html, body { margin: 0; }
            .shell { box-sizing: border-box; width: 600px; height: 20px; padding: 0 24px; }
            .grid { display: flex; flex-wrap: wrap; gap: 14px; width: 100%; height: 20px; background: #0b57d0; }
            .card { box-sizing: border-box; flex: 1 1 155px; min-width: 155px; height: 20px; background: #ff2d55; }
            """;
        if (reverseOrdered)
        {
            style.textContent += """
                .grid { flex-direction: row-reverse; }
                #card-0 { order: 2; }
                #card-1 { order: 0; }
                #card-2 { order: 1; }
                """;
        }
        fixture.Document.head.appendChild(style);
        var shell = HostTestUtilities.GetElement(fixture.Document.createElement("main"));
        shell.className = "shell";
        var grid = HostTestUtilities.GetElement(fixture.Document.createElement("div"));
        grid.className = "grid";
        var cards = Enumerable.Range(0, 3).Select(index =>
        {
            var card = HostTestUtilities.GetElement(fixture.Document.createElement("section"));
            card.id = $"card-{index}";
            card.className = "card";
            grid.appendChild(card);
            return card;
        }).ToArray();
        shell.appendChild(grid);
        HostTestUtilities.GetElement(fixture.Document.body).appendChild(shell);
        fixture.SetElements(shell, grid, cards, cards[0]);
        fixture.ShowAndLayout();
        return fixture;
    }

    private static Fixture CreateIntegralFixture()
    {
        var fixture = CreateFixture(120, 60);
        var style = HostTestUtilities.GetElement(fixture.Document.createElement("style"));
        style.textContent = """
            html, body { margin: 0; }
            .box { box-sizing: border-box; width: 80px; height: 30px; margin: 8px 0 0 10px; border: 1px solid #111827; background: #f9fafb; }
            """;
        fixture.Document.head.appendChild(style);
        var box = HostTestUtilities.GetElement(fixture.Document.createElement("div"));
        box.className = "box";
        HostTestUtilities.GetElement(fixture.Document.body).appendChild(box);
        fixture.SetElements(box, box, [box], box);
        fixture.ShowAndLayout();
        return fixture;
    }

    private static Fixture CreateFixture(double width, double height)
    {
        var root = new CssLayoutPanel { Width = width, Height = height, Background = Brushes.White };
        var window = new Window { Width = width, Height = height, Content = root, Background = Brushes.White };
        return new Fixture(window, new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true));
    }

    private static void AssertRect(DomRect actual, double x, double y, double width, double height)
    {
        Assert.Equal(x, actual.x, 9);
        Assert.Equal(y, actual.y, 9);
        Assert.Equal(width, actual.width, 9);
        Assert.Equal(height, actual.height, 9);
    }

    private static void AssertPortableRect(WebScene.Core.WebSceneRect actual, double x, double y, double width, double height)
    {
        Assert.Equal(x, actual.X, 9);
        Assert.Equal(y, actual.Y, 9);
        Assert.Equal(width, actual.Width, 9);
        Assert.Equal(height, actual.Height, 9);
    }

    private static void AssertSolidRun(Bitmap bitmap, int startX, int endX, int y, Color expected)
    {
        for (var x = startX; x <= endX; x++)
        {
            var actual = ReadPixel(bitmap, x, y);
            Assert.True(actual == expected, $"Expected {expected} at ({x},{y}), actual {actual}.");
        }
    }

    private static void AssertBlendedEdge(Bitmap bitmap, int x, int y, Color first, Color second)
    {
        var actual = ReadPixel(bitmap, x, y);
        Assert.Equal(byte.MaxValue, actual.A);
        Assert.NotEqual(first, actual);
        Assert.NotEqual(second, actual);
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

    private sealed class Fixture(Window window, AvaloniaBrowserHost host) : IDisposable
    {
        public Window Window { get; } = window;
        public AvaloniaDomDocument Document { get; } = host.Document;
        public AvaloniaDomElement Shell { get; private set; } = null!;
        public AvaloniaDomElement Grid { get; private set; } = null!;
        public AvaloniaDomElement[] Cards { get; private set; } = [];
        public AvaloniaDomElement Box { get; private set; } = null!;

        public void SetElements(AvaloniaDomElement shell, AvaloniaDomElement grid, AvaloniaDomElement[] cards, AvaloniaDomElement box)
        {
            Shell = shell;
            Grid = grid;
            Cards = cards;
            Box = box;
        }

        public void ShowAndLayout()
        {
            Window.Show();
            Document.EnsureStylesCurrent();
            foreach (var panel in new[] { Shell.Control, Grid.Control }.Concat(Cards.Select(static card => card.Control)).OfType<CssLayoutPanel>())
            {
                CssLayout.SetNativeLayoutHotPath(panel, true);
            }
            Dispatcher.UIThread.RunJobs();
            Document.FlushPendingLayout();
        }

        public void FlushLayout()
        {
            Document.EnsureStylesCurrent();
            if (Window.Content is Control root)
            {
                root.InvalidateMeasure();
                root.InvalidateArrange();
            }
            Dispatcher.UIThread.RunJobs();
            Document.FlushPendingLayout();
        }

        public void Dispose()
        {
            host.Dispose();
            Window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

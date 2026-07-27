using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using WebScene.Core;
using Xunit;

namespace JavaScript.Avalonia.Tests;

/// <summary>
/// Browser-authoritative reduction for CSS Lists 3 inside-marker placement.
/// Chromium 150 was queried at DPR 1 with this exact fixed-size fixture. The
/// marker's glyph width is intentionally not asserted: the contract is that its
/// 20px first-line formatting box participates in line breaking before either
/// inline or block content.
/// </summary>
public sealed class CssInsideListMarkerFormattingContextAuthorityTests
{
    [AvaloniaFact]
    public void InsideMarkerOwnsFirstLineWrapsInlineBoxesAndPrecedesBlockChild()
    {
        using var fixture = CreateFixture();

        var portableLive = CaptureLive(fixture);
        var projection = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Container.Control),
            new Size(40, 160));
        var portableProjection = CapturePortable(fixture, projection);

        EnableNativeLayout(fixture);
        var nativeLive = CaptureLive(fixture);
        using var frame = Assert.IsAssignableFrom<Bitmap>(fixture.Window.CaptureRenderedFrame());
        var raster = CaptureRaster(frame);

        var expected = new Geometry(
            Two: new Box(0, 0, 40, 40),
            First: new Box(0, 20, 20, 10),
            Second: new Box(20, 20, 20, 10),
            Full: new Box(0, 50, 40, 40),
            FullChild: new Box(0, 70, 40, 10),
            Block: new Box(0, 100, 40, 30),
            BlockChild: new Box(0, 120, 40, 10));

        Assert.True(
            expected == portableLive
            && expected == portableProjection
            && expected == nativeLive,
            $"Chromium geometry: {expected}\n"
            + $"portable live: {portableLive}\n"
            + $"portable projection: {portableProjection}\n"
            + $"native live: {nativeLive}\n"
            + $"native raster: {raster}");

        AssertMarkerPrecedesFirstChild(
            fixture.Two,
            fixture.First,
            projection,
            expected.Two,
            expected.First);
        AssertMarkerPrecedesFirstChild(
            fixture.Block,
            fixture.BlockChild,
            projection,
            expected.Block,
            expected.BlockChild);

        Assert.Equal(Color.Parse("#00ccff"), raster.FirstInline);
        Assert.Equal(Color.Parse("#00ccff"), raster.SecondInline);
        Assert.Equal(Color.Parse("#00ccff"), raster.FullWidthInline);
        Assert.Equal(Color.Parse("#ff00cc"), raster.BlockChild);
        Assert.True(raster.FirstLineHasMarker, "The marker must paint in the reserved first line.");
        Assert.True(raster.BlockFirstLineHasMarker, "A marker before a block child must paint in its own line.");
    }

    private static Fixture CreateFixture()
    {
        var root = new CssLayoutPanel
        {
            Width = 120,
            Height = 160,
            Background = Brushes.White
        };
        var window = new Window
        {
            Width = 120,
            Height = 160,
            Background = Brushes.White,
            Content = root
        };
        var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body, #fixture { margin: 0; padding: 0; }
            #fixture { width: 40px; }
            .case {
                box-sizing: border-box;
                display: list-item;
                width: 40px;
                padding: 0;
                margin: 0 0 10px;
                list-style: square inside;
                font: 20px/20px monospace;
                color: #000000;
                background: #eeeeee;
            }
            .inline {
                display: inline-block;
                vertical-align: top;
                width: 20px;
                height: 10px;
                background: #00ccff;
            }
            .block {
                display: block;
                width: 40px;
                height: 10px;
                background: #ff00cc;
            }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = """
            <div id="fixture"><div class="case" id="two"><span class="inline" id="first"></span><span class="inline" id="second"></span></div><div class="case" id="full"><span class="inline" id="full-child" style="width:40px"></span></div><div class="case" id="block"><div class="block" id="block-child"></div></div></div>
            """;

        var fixture = HostTestUtilities.GetElement(body.querySelector("#fixture"));
        var two = HostTestUtilities.GetElement(body.querySelector("#two"));
        var first = HostTestUtilities.GetElement(body.querySelector("#first"));
        var second = HostTestUtilities.GetElement(body.querySelector("#second"));
        var full = HostTestUtilities.GetElement(body.querySelector("#full"));
        var fullChild = HostTestUtilities.GetElement(body.querySelector("#full-child"));
        var block = HostTestUtilities.GetElement(body.querySelector("#block"));
        var blockChild = HostTestUtilities.GetElement(body.querySelector("#block-child"));

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        document.FlushPendingLayout();
        return new Fixture(
            window,
            host,
            fixture,
            two,
            first,
            second,
            full,
            fullChild,
            block,
            blockChild);
    }

    private static Geometry CaptureLive(Fixture fixture)
        => new(
            From(fixture.Two.getBoundingClientRect()),
            From(fixture.First.getBoundingClientRect()),
            From(fixture.Second.getBoundingClientRect()),
            From(fixture.Full.getBoundingClientRect()),
            From(fixture.FullChild.getBoundingClientRect()),
            From(fixture.Block.getBoundingClientRect()),
            From(fixture.BlockChild.getBoundingClientRect()));

    private static Geometry CapturePortable(Fixture fixture, AvaloniaCssLayoutSnapshot projection)
        => new(
            From(projection.GetBox(fixture.Two.Control).BorderBox),
            From(projection.GetBox(fixture.First.Control).BorderBox),
            From(projection.GetBox(fixture.Second.Control).BorderBox),
            From(projection.GetBox(fixture.Full.Control).BorderBox),
            From(projection.GetBox(fixture.FullChild.Control).BorderBox),
            From(projection.GetBox(fixture.Block.Control).BorderBox),
            From(projection.GetBox(fixture.BlockChild.Control).BorderBox));

    private static void EnableNativeLayout(Fixture fixture)
    {
        foreach (var panel in fixture.Elements
                     .Select(static element => element.Control)
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        fixture.Container.Control.InvalidateMeasure();
        fixture.Container.Control.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
        fixture.Document.FlushPendingLayout();
    }

    private static void AssertMarkerPrecedesFirstChild(
        AvaloniaDomElement item,
        AvaloniaDomElement firstChild,
        AvaloniaCssLayoutSnapshot projection,
        Box expectedItem,
        Box expectedChild)
    {
        var panel = Assert.IsType<CssLayoutPanel>(item.Control);
        Assert.True(projection.TryGetListMarkerBox(panel, out var portableMarker));
        Assert.Equal(expectedItem.Y, portableMarker.BorderBox.Top, 9);
        Assert.Equal(20, portableMarker.BorderBox.Height, 9);
        Assert.True(
            portableMarker.BorderBox.Bottom <= expectedChild.Y,
            $"Portable marker {portableMarker.BorderBox} overlays first child {expectedChild}.");

        Assert.True(panel.TryResolveListMarkerRect(panel.Bounds.Size, out var nativeMarker));
        Assert.Equal(0, nativeMarker.Top, 9);
        Assert.Equal(20, nativeMarker.Height, 9);
        Assert.True(
            nativeMarker.Bottom <= firstChild.Control.Bounds.Top,
            $"Native marker {nativeMarker} overlays first child {firstChild.Control.Bounds}.");
    }

    private static RasterAuthority CaptureRaster(Bitmap bitmap)
        => new(
            ReadPixel(bitmap, 5, 25),
            ReadPixel(bitmap, 25, 25),
            ReadPixel(bitmap, 5, 75),
            ReadPixel(bitmap, 5, 125),
            ContainsDarkPixel(bitmap, 0, 0, 20, 20),
            ContainsDarkPixel(bitmap, 0, 100, 20, 120));

    private static bool ContainsDarkPixel(Bitmap bitmap, int left, int top, int right, int bottom)
    {
        for (var y = top; y < bottom; y++)
        for (var x = left; x < right; x++)
        {
            var color = ReadPixel(bitmap, x, y);
            if (color.A > 128 && color.R < 64 && color.G < 64 && color.B < 64)
            {
                return true;
            }
        }
        return false;
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

    private static Box From(DomRect rect) => new(rect.x, rect.y, rect.width, rect.height);

    private static Box From(WebSceneRect rect) => new(rect.X, rect.Y, rect.Width, rect.Height);

    private readonly record struct Box(double X, double Y, double Width, double Height);

    private readonly record struct Geometry(
        Box Two,
        Box First,
        Box Second,
        Box Full,
        Box FullChild,
        Box Block,
        Box BlockChild);

    private readonly record struct RasterAuthority(
        Color FirstInline,
        Color SecondInline,
        Color FullWidthInline,
        Color BlockChild,
        bool FirstLineHasMarker,
        bool BlockFirstLineHasMarker);

    private sealed class Fixture(
        Window window,
        AvaloniaBrowserHost host,
        AvaloniaDomElement fixture,
        AvaloniaDomElement two,
        AvaloniaDomElement first,
        AvaloniaDomElement second,
        AvaloniaDomElement full,
        AvaloniaDomElement fullChild,
        AvaloniaDomElement block,
        AvaloniaDomElement blockChild) : IDisposable
    {
        public Window Window { get; } = window;
        public AvaloniaDomDocument Document { get; } = host.Document;
        public AvaloniaDomElement Container { get; } = fixture;
        public AvaloniaDomElement Two { get; } = two;
        public AvaloniaDomElement First { get; } = first;
        public AvaloniaDomElement Second { get; } = second;
        public AvaloniaDomElement Full { get; } = full;
        public AvaloniaDomElement FullChild { get; } = fullChild;
        public AvaloniaDomElement Block { get; } = block;
        public AvaloniaDomElement BlockChild { get; } = blockChild;

        public AvaloniaDomElement[] Elements =>
        [
            Container,
            Two,
            First,
            Second,
            Full,
            FullChild,
            Block,
            BlockChild
        ];

        public void Dispose()
        {
            Window.Close();
            Dispatcher.UIThread.RunJobs();
            host.Dispose();
        }
    }
}

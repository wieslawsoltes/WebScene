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
using WebScene.Graphics;
using SkiaSharp;
using Svg.Skia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class SvgSkiaSpikeTests
{
    [AvaloniaFact]
    public void PortableSvgFixtureReplaysThroughReferenceAndAvaloniaBackends()
    {
        var root = new SvgSceneNode(1, SvgSceneNodeKind.Group);
        root.Add(new SvgSceneNode(2, SvgSceneNodeKind.Rectangle)
        {
            Bounds = new WebSceneRect(0, 0, 10, 10),
            Fill = new SvgPaint(WebSceneColor.FromRgb(12, 34, 56))
        });
        var scene = new SvgScene(new WebSceneRect(0, 0, 10, 10), root, 1);
        var reference = new SvgReferenceRenderer();
        reference.Render(scene, new WebSceneSize(10, 10));
        var avalonia = new AvaloniaSvgSceneSurface();
        avalonia.Render(scene, new WebSceneSize(10, 10));
        var window = new Window { Width = 10, Height = 10, Content = avalonia };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var offset = (5 * frame.PixelSize.Width + 5) * 4;
            var actual = new WebSceneColor(pixels[offset + 3], pixels[offset], pixels[offset + 1], pixels[offset + 2]);

            Assert.Equal(reference.Surface.GetPixel(5, 5), actual);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void LiveSvgDomCompilesToPortableSceneAndBypassesSkiaMarkup()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 18 18");
            svg.setAttribute("width", "18");
            svg.setAttribute("height", "18");
            svg.setAttribute("color", "#dbdbdb");
            var path = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            path.setAttribute("fill", "currentColor");
            path.setAttribute("d", "M3.5 8a4.5 4.5 0 1 1 9 0 4.5 4.5 0 0 1-9 0Z");
            svg.appendChild(path);
            body.appendChild(svg);
            window.Width = 18;
            window.Height = 18;
            window.Background = Brushes.Black;

            try
            {
                window.Show();
                Dispatcher.UIThread.RunJobs();
                var panel = Assert.IsType<SvgLayoutPanel>(svg.Control);
                Assert.NotNull(panel.SceneProvider);
                Assert.Null(panel.SkiaMarkupProvider);
                Assert.True(panel.SceneBuildCount > 0);
                var stableBuildCount = panel.SceneBuildCount;

                using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
                var pixels = CopyPixels(frame);
                Assert.Equal(stableBuildCount, panel.SceneBuildCount);
                var painted = 0;
                for (var offset = 0; offset < pixels.Length; offset += 4)
                {
                    if (pixels[offset] > 64 || pixels[offset + 1] > 64 || pixels[offset + 2] > 64)
                    {
                        painted++;
                    }
                }

                Assert.InRange(painted, 20, 250);

                path.setAttribute("transform", "translate(1 0)");
                Dispatcher.UIThread.RunJobs();
                using var updated = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
                Assert.Equal(stableBuildCount + 1, panel.SceneBuildCount);
                Assert.False(pixels.SequenceEqual(CopyPixels(updated)));
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    [AvaloniaFact]
    public void RootSvgFillCurrentColorIsInheritedByChildPaths()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 18 18");
            svg.setAttribute("color", "#dbdbdb");
            svg.setAttribute("fill", "currentColor");
            var path = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            path.setAttribute("d", "M2 2h14v14H2z");
            svg.appendChild(path);

            var scene = Assert.IsType<SvgScene>(Assert.IsType<SvgLayoutPanel>(svg.Control).SceneProvider!());
            var paint = Assert.IsType<SvgPaint>(Assert.Single(scene.Root.Children).Fill);
            Assert.Equal(WebSceneColor.FromRgb(219, 219, 219), paint.Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StylesheetFillCurrentColorOnSvgReachesAttrlessPathSceneAndRaster()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                html, body { margin: 0; width: 18px; height: 18px; }
                .menu-item { color: #dbdbdb; }
                .submenu-arrow svg { fill: currentColor; width: 18px; height: 18px; }
                .submenu-arrow.explicit svg { fill: #c05020; }
                """;
            document.head.appendChild(style);

            var item = HostTestUtilities.GetElement(document.createElement("div"));
            item.className = "menu-item submenu-arrow";
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 18 18");
            svg.setAttribute("width", "18");
            svg.setAttribute("height", "18");
            svg.setAttribute("fill", "#ff0000");
            var path = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            path.setAttribute("d", "M2 2h14v14H2z");
            svg.appendChild(path);
            item.appendChild(svg);
            HostTestUtilities.GetElement(document.body).appendChild(item);

            window.Width = 18;
            window.Height = 18;
            window.Background = Brushes.Black;
            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal("currentColor", document.getComputedStyle(svg).getPropertyValue("fill"));
            Assert.Equal("rgb(219, 219, 219)", document.getComputedStyle(svg).getPropertyValue("color"));
            var panel = Assert.IsType<SvgLayoutPanel>(svg.Control);
            var scene = Assert.IsType<SvgScene>(panel.SceneProvider!());
            var paint = Assert.IsType<SvgPaint>(Assert.Single(scene.Root.Children).Fill);
            Assert.Equal(WebSceneColor.FromRgb(219, 219, 219), paint.Color);

            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var offset = (9 * frame.PixelSize.Width + 9) * 4;
            var actual = new WebSceneColor(pixels[offset + 3], pixels[offset], pixels[offset + 1], pixels[offset + 2]);
            Assert.Equal(WebSceneColor.FromRgb(219, 219, 219), actual);

            var initialBuildCount = panel.SceneBuildCount;
            item.style.setProperty("color", "#00c080");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            using (var recolored = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame()))
            {
                var recoloredPixels = CopyPixels(recolored);
                var recoloredOffset = (9 * recolored.PixelSize.Width + 9) * 4;
                var recoloredActual = new WebSceneColor(
                    recoloredPixels[recoloredOffset + 3],
                    recoloredPixels[recoloredOffset],
                    recoloredPixels[recoloredOffset + 1],
                    recoloredPixels[recoloredOffset + 2]);
                Assert.Equal(WebSceneColor.FromRgb(0, 192, 128), recoloredActual);
            }
            Assert.True(panel.SceneBuildCount > initialBuildCount);

            var recoloredBuildCount = panel.SceneBuildCount;
            item.className = "menu-item submenu-arrow explicit";
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            using (var explicitFill = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame()))
            {
                var explicitPixels = CopyPixels(explicitFill);
                var explicitOffset = (9 * explicitFill.PixelSize.Width + 9) * 4;
                var explicitActual = new WebSceneColor(
                    explicitPixels[explicitOffset + 3],
                    explicitPixels[explicitOffset],
                    explicitPixels[explicitOffset + 1],
                    explicitPixels[explicitOffset + 2]);
                Assert.Equal(WebSceneColor.FromRgb(192, 80, 32), explicitActual);
            }
            Assert.True(panel.SceneBuildCount > recoloredBuildCount);

            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void SvgPresentationAttributeBeatsInheritedFillButLosesToMatchingAuthorRule()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                svg { color: #dbdbdb; fill: currentColor; }
                path.override { fill: #2468ac; }
                """;
            document.head.appendChild(style);
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 20 10");
            var attributed = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            attributed.setAttribute("fill", "#804020");
            attributed.setAttribute("d", "M0 0h10v10H0z");
            var overridden = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            overridden.className = "override";
            overridden.setAttribute("fill", "#804020");
            overridden.setAttribute("d", "M10 0h10v10H10z");
            svg.appendChild(attributed);
            svg.appendChild(overridden);
            HostTestUtilities.GetElement(document.body).appendChild(svg);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            var scene = Assert.IsType<SvgScene>(Assert.IsType<SvgLayoutPanel>(svg.Control).SceneProvider!());
            Assert.Equal(WebSceneColor.FromRgb(128, 64, 32), Assert.IsType<SvgPaint>(scene.Root.Children[0].Fill).Color);
            Assert.Equal(WebSceneColor.FromRgb(36, 104, 172), Assert.IsType<SvgPaint>(scene.Root.Children[1].Fill).Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void SvgRotateWithPivotKeepsTheDeclaredPivotFixed()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 28 28");
            var path = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "path"));
            path.setAttribute("d", "M4 13h20v2H4z");
            path.setAttribute("transform", "rotate(-45 14 14)");
            svg.appendChild(path);

            var scene = Assert.IsType<SvgScene>(Assert.IsType<SvgLayoutPanel>(svg.Control).SceneProvider!());
            var transform = Assert.Single(scene.Root.Children).Transform;
            var pivotX = 14 * transform.M11 + 14 * transform.M21 + transform.M31;
            var pivotY = 14 * transform.M12 + 14 * transform.M22 + transform.M32;
            Assert.Equal(14, pivotX, 6);
            Assert.Equal(14, pivotY, 6);
            Assert.NotEqual(GraphicsTransform.Identity, transform);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void LiveSvgImageHrefDecodesIntoPortableSceneAndAvaloniaProjection()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        using var sourceBitmap = new SKBitmap(2, 2);
        sourceBitmap.Erase(SKColors.Red);
        using var sourceImage = SKImage.FromBitmap(sourceBitmap);
        using var encoded = sourceImage.Encode(SKEncodedImageFormat.Png, 100);
        var dataUri = "data:image/png;base64," + Convert.ToBase64String(encoded.ToArray());

        var (host, hostWindow) = HostTestUtilities.CreateHost();
        using (host)
        {
            var document = host.Document;
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 4 4");
            var image = Assert.IsType<AvaloniaDomImageElement>(
                document.createElementNS(svgNamespace, "image"));
            image.setAttribute("x", "0");
            image.setAttribute("y", "0");
            image.setAttribute("width", "4");
            image.setAttribute("height", "4");
            image.setAttribute("href", dataUri);
            svg.appendChild(image);

            var panel = Assert.IsType<SvgLayoutPanel>(svg.Control);
            var scene = Assert.IsType<SvgScene>(panel.SceneProvider!());
            var imageNode = Assert.Single(scene.Root.Children);
            Assert.Equal(SvgSceneNodeKind.Image, imageNode.Kind);
            Assert.Equal(dataUri, imageNode.ResourceUri);
            Assert.True(imageNode.Resource.TryGet<IImage>(out _));

            var projection = new AvaloniaSvgSceneSurface();
            projection.Render(scene, new WebSceneSize(4, 4));
            var window = new Window { Width = 4, Height = 4, Content = projection };
            try
            {
                window.Show();
                Dispatcher.UIThread.RunJobs();
                using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
                var pixels = CopyPixels(frame);
                var offset = (2 * frame.PixelSize.Width + 2) * 4;
                var rgba = frame.Format == PixelFormat.Rgba8888;
                var red = pixels[offset + (rgba ? 0 : 2)];
                var green = pixels[offset + 1];
                var blue = pixels[offset + (rgba ? 2 : 0)];
                Assert.True(red > 240 && green < 8 && blue < 8,
                    $"Expected the portable SVG image resource to project as red pixels, got " +
                    $"R={red}, G={green}, B={blue}, format={frame.Format}, bounds={imageNode.Bounds}.");
            }
            finally
            {
                window.Close();
                hostWindow.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    // A representative toolbar icon, kept isolated from a full component so
    // renderer compatibility and output can be verified independently.
    private const string SearchIcon = """
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 18 18" width="18" height="18" color="#dbdbdb">
          <path fill="currentColor" d="M3.5 8a4.5 4.5 0 1 1 9 0 4.5 4.5 0 0 1-9 0ZM8 2a6 6 0 1 0 3.65 10.76l3.58 3.58 1.06-1.06-3.57-3.57A6 6 0 0 0 8 2Z"/>
        </svg>
        """;

    private const string UndoIcon = """
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 28 28" width="28" height="28">
          <path fill="#ffffff" d="M8.707 13l2.647 2.646-.707.708L6.792 12.5l3.853-3.854.708.708L8.707 12H14.5a5.5 5.5 0 0 1 5.5 5.5V19h-1v-1.5a4.5 4.5 0 0 0-4.5-4.5H8.707z"/>
        </svg>
        """;

    [AvaloniaFact]
    public void SparseToolbarIconUsesDeclaredViewBoxInsteadOfTightPaintBounds()
    {
        var icon = new SvgLayoutPanel
        {
            Width = 28,
            Height = 28,
            ViewBox = new Rect(0, 0, 28, 28),
            SkiaMarkupProvider = static () => UndoIcon
        };
        var surface = new Canvas
        {
            Width = 38,
            Height = 38,
            Background = Brushes.Black
        };
        Canvas.SetLeft(icon, 5);
        Canvas.SetTop(icon, 5);
        surface.Children.Add(icon);
        var window = new Window
        {
            Width = 38,
            Height = 38,
            Content = surface
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var stride = frame.PixelSize.Width * 4;
            var paintedRows = new List<int>();
            for (var y = 0; y < frame.PixelSize.Height; y++)
            for (var x = 0; x < frame.PixelSize.Width; x++)
            {
                var offset = y * stride + x * 4;
                if (pixels[offset] > 96 || pixels[offset + 1] > 96 || pixels[offset + 2] > 96)
                {
                    paintedRows.Add(y);
                }
            }

            Assert.NotEmpty(paintedRows);
            Assert.InRange(paintedRows.Min(), 13, 15);
            Assert.InRange(paintedRows.Max(), 23, 25);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [Fact]
    public void ToolbarIcon_RendersThroughSvgSkiaWithAntialiasedCoverage()
    {
        using var svg = new SKSvg();
        using var picture = svg.FromSvg(SearchIcon);
        Assert.NotNull(picture);

        using var surface = SKSurface.Create(new SKImageInfo(36, 36, SKColorType.Bgra8888, SKAlphaType.Premul));
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.Scale(2);
        surface.Canvas.DrawPicture(picture);
        surface.Canvas.Flush();

        using var image = surface.Snapshot();
        using var bitmap = SKBitmap.FromImage(image);
        var opaque = 0;
        var antialiased = 0;
        for (var y = 0; y < bitmap.Height; y++)
        for (var x = 0; x < bitmap.Width; x++)
        {
            var alpha = bitmap.GetPixel(x, y).Alpha;
            if (alpha > 0) opaque++;
            if (alpha is > 0 and < 255) antialiased++;
        }

        Assert.InRange(opaque, 250, 700);
        Assert.InRange(antialiased, 40, 300);
    }

    [Fact]
    public void QueuedDrawOperation_RetainsCompiledSvgUntilOperationIsDisposed()
    {
        var svg = new SKSvg();
        var picture = svg.FromSvg(SearchIcon);
        Assert.NotNull(picture);
        var resource = new SvgSkiaResource(svg);
        var operation = new SvgSkiaDrawOperation(new Rect(0, 0, 18, 18), resource, picture.CullRect);

        resource.Release();
        Assert.False(resource.IsDisposed);

        operation.Dispose();
        Assert.True(resource.IsDisposed);
    }

    private static byte[] CopyPixels(Bitmap bitmap)
    {
        var stride = bitmap.PixelSize.Width * 4;
        var pixels = new byte[stride * bitmap.PixelSize.Height];
        var handle = GCHandle.Alloc(pixels, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(
                new PixelRect(bitmap.PixelSize),
                handle.AddrOfPinnedObject(),
                pixels.Length,
                stride);
        }
        finally
        {
            handle.Free();
        }

        return pixels;
    }
}

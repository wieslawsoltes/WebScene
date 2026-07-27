using System.Runtime.InteropServices;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssFixedGridRowTracksTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void FixedPixelRowsDetermineNaturalGridHeightInsideVirtualIframe()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var root = new CssLayoutPanel
        {
            Width = 800,
            Height = 600,
            ClipToBounds = true
        };
        var window = new Window { Width = 800, Height = 600, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions { EnableTrustedSameOriginContextSharing = true });
        runtime.Execute("""
            document.body.style.margin = '0';
            const frame = document.createElement('iframe');
            frame.id = '__grid_frame';
            frame.style.display = 'block';
            frame.style.border = '0';
            frame.style.width = '800px';
            frame.style.height = '600px';
            document.body.appendChild(frame);
            frame.src = URL.createObjectURL(new Blob([`<!DOCTYPE html>
              <style>
                #grid { display:grid; width:200px; gap:20px; grid-template-columns:90px 90px; grid-template-rows:90px 90px; background:green; }
                #grid > div { background:silver; }
              </style>
              <p>The test passes if it has the same visual effect as reference.</p>
              <div id="grid">
                <div></div>
                <div></div>
                <div></div>
                <div></div>
              </div>`], { type: 'text/html' }));
            globalThis.__gridFrame = frame;
            """, "fixed-grid-row-tracks-iframe.js");

        for (var index = 0; index < 50; index++)
        {
            runtime.ProcessPendingTasks();
            Dispatcher.UIThread.RunJobs();
            if (Convert.ToBoolean(runtime.Engine.Evaluate(
                    "__gridFrame.contentDocument && __gridFrame.contentDocument.readyState === 'complete'")))
            {
                break;
            }
        }
        using var state = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate("""
            JSON.stringify((() => {
              const grid = __gridFrame.contentDocument.getElementById('grid');
              const rect = grid.getBoundingClientRect();
              return { width: rect.width, height: rect.height };
            })())
            """)) ?? "{}");

        Assert.Equal(200, state.RootElement.GetProperty("width").GetDouble());
        Assert.Equal(200, state.RootElement.GetProperty("height").GetDouble());
        var iframe = HostTestUtilities.GetElement(host.Document.querySelector("#__grid_frame"));
        for (var index = 0; index < 24; index++)
        {
            iframe.GetContentDocument()!.EnsureStylesCurrent();
            iframe.Control.InvalidateMeasure();
            window.InvalidateMeasure();
            Dispatcher.UIThread.RunJobs();
        }
        using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
        Assert.Equal(Colors.White, ReadPixel(frame, 100, 230));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void FixedPixelRowsDetermineNaturalGridHeightInNativeBlockFlow()
    {
        var root = new CssLayoutPanel { Width = 800, Height = 600, Background = Brushes.White };
        var window = new Window { Width = 800, Height = 600, Content = root, Background = Brushes.White };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            #grid {
                background: green;
                display: grid;
                gap: 20px;
                grid-template-columns: 90px 90px;
                grid-template-rows: 90px 90px;
                width: 200px;
            }
            #grid > div { background: silver; }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = """
            <p>The test passes if it has the same visual effect as reference.</p>
            <div id="grid"><div></div><div></div><div></div><div></div></div>
            """;
        var grid = HostTestUtilities.GetElement(body.querySelector("#grid"));
        var cells = grid.children.Cast<AvaloniaDomElement>().ToArray();

        window.Show();
        document.EnsureStylesCurrent();
        foreach (var panel in EnumeratePanels(root))
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        Dispatcher.UIThread.RunJobs();
        document.FlushPendingLayout();

        AssertNativeGeometry(grid, cells);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void FixedPixelRowsAndColumnsPlaceAndPaintTwoByTwoGridInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 200, Height = 200, Background = Brushes.White };
        var window = new Window { Width = 200, Height = 200, Content = root, Background = Brushes.White };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 200px; margin: 0; width: 200px; }
            #grid {
                background: green;
                display: grid;
                gap: 20px;
                grid-template-columns: 90px 90px;
                grid-template-rows: 90px 90px;
                width: 200px;
            }
            #grid > div { background: silver; }
            """;
        document.head.appendChild(style);
        var grid = HostTestUtilities.GetElement(document.createElement("div"));
        grid.id = "grid";
        var cells = Enumerable.Range(0, 4).Select(_ =>
        {
            var cell = HostTestUtilities.GetElement(document.createElement("div"));
            grid.appendChild(cell);
            return cell;
        }).ToArray();
        HostTestUtilities.GetElement(document.body).appendChild(grid);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("90px 90px", document.getComputedStyle(grid).getPropertyValue("grid-template-rows"));
        AssertNativeGeometry(grid, cells);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(grid.Control),
            new Size(200, 200));
        AssertPortableGeometry(portable, grid, cells);

        grid.Control.Measure(new Size(200, 200));
        grid.Control.Arrange(new Rect(0, 0, 200, 200));
        using var frame = new RenderTargetBitmap(new PixelSize(200, 200), new Vector(96, 96));
        frame.Render(grid.Control);
        var silver = Color.Parse("#c0c0c0");
        var green = Color.Parse("#008000");
        Assert.Equal(silver, ReadPixel(frame, 45, 45));
        Assert.Equal(silver, ReadPixel(frame, 155, 45));
        Assert.Equal(silver, ReadPixel(frame, 45, 155));
        Assert.Equal(silver, ReadPixel(frame, 155, 155));
        Assert.Equal(green, ReadPixel(frame, 100, 45));
        Assert.Equal(green, ReadPixel(frame, 45, 100));
        Assert.Equal(green, ReadPixel(frame, 100, 100));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    private static void AssertNativeGeometry(AvaloniaDomElement grid, IReadOnlyList<AvaloniaDomElement> cells)
    {
        var gridRect = grid.getBoundingClientRect();
        Assert.Equal((200d, 200d), (gridRect.width, gridRect.height));
        var expected = new[]
        {
            (0d, 0d, 90d, 90d),
            (110d, 0d, 90d, 90d),
            (0d, 110d, 90d, 90d),
            (110d, 110d, 90d, 90d)
        };
        for (var index = 0; index < cells.Count; index++)
        {
            var rect = cells[index].getBoundingClientRect();
            Assert.Equal(expected[index],
                (rect.left - gridRect.left, rect.top - gridRect.top, rect.width, rect.height));
        }
    }

    private static void AssertPortableGeometry(
        AvaloniaCssLayoutSnapshot portable,
        AvaloniaDomElement grid,
        IReadOnlyList<AvaloniaDomElement> cells)
    {
        Assert.Equal((200d, 200d),
            (portable.GetBox(grid.Control).BorderBox.Width, portable.GetBox(grid.Control).BorderBox.Height));
        var expected = new[]
        {
            new Rect(0, 0, 90, 90),
            new Rect(110, 0, 90, 90),
            new Rect(0, 110, 90, 90),
            new Rect(110, 110, 90, 90)
        };
        for (var index = 0; index < cells.Count; index++)
        {
            var rect = portable.GetBox(cells[index].Control).BorderBox;
            Assert.Equal(expected[index], new Rect(rect.X, rect.Y, rect.Width, rect.Height));
        }
    }

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

    private static IEnumerable<CssLayoutPanel> EnumeratePanels(Control root)
    {
        if (root is CssLayoutPanel panel)
        {
            yield return panel;
        }
        if (root is not Panel parent)
        {
            yield break;
        }
        foreach (var child in parent.Children)
        {
            foreach (var descendant in EnumeratePanels(child))
            {
                yield return descendant;
            }
        }
    }
}

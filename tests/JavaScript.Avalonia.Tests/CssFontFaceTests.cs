using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Threading;
using JavaScript.Avalonia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssFontFaceTests
{
    [AvaloniaFact]
    public void DomTextNodeKeepsInheritedFontPropertiesInsteadOfLocalDefaults()
    {
        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = ".parent { font-size: 23px; font-weight: 700; font-style: italic; }";
        document.head.appendChild(style);

        var parent = HostTestUtilities.GetElement(document.createElement("div"));
        parent.className = "parent";
        parent.textContent = "Inherited";
        HostTestUtilities.GetElement(document.body).appendChild(parent);
        document.EnsureStylesCurrent();

        var textNode = Assert.IsType<AvaloniaDomTextNode>(parent.firstChild);
        var text = Assert.IsType<DomTextBlockControl>(textNode.Control);
        Assert.Equal(23, text.FontSize);
        Assert.Equal(FontWeight.Bold, text.FontWeight);
        Assert.Equal(FontStyle.Italic, text.FontStyle);

        parent.setAttribute("style", "font-size:17px;font-weight:400;font-style:normal");
        document.EnsureStylesCurrent();
        Assert.Equal(17, text.FontSize);
        Assert.Equal(FontWeight.Normal, text.FontWeight);
        Assert.Equal(FontStyle.Normal, text.FontStyle);
        window.Close();
    }

    [AvaloniaFact]
    public void RelativeDownloadedFaceReappliesDomAndIsSharedWithCanvas()
    {
        var sourceFont = FindPlatformTrueTypeFont();
        var directory = Path.Combine(Path.GetTempPath(), "webscene-font-face-test-" + Guid.NewGuid().ToString("N"));
        var fontsDirectory = Path.Combine(directory, "assets", "fonts");
        Directory.CreateDirectory(fontsDirectory);
        File.WriteAllBytes(
            Path.Combine(fontsDirectory, "unsupported.woff2"),
            [(byte)'w', (byte)'O', (byte)'F', (byte)'2', 0, 0, 0, 0]);
        File.Copy(sourceFont, Path.Combine(fontsDirectory, "chart.ttf"));

        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window) { ScriptBaseDirectory = directory };
        try
        {
            var document = host.Document;
            var baseElement = HostTestUtilities.GetElement(document.createElement("base"));
            baseElement.setAttribute("href", new Uri(Path.Combine(directory, "assets", "index.html")).AbsoluteUri);
            document.head.appendChild(baseElement);

            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                @font-face {
                  font-family: "Fixture Chart";
                  src: url("fonts/unsupported.woff2") format("woff2"),
                       url("fonts/chart.ttf") format("truetype");
                  font-style: normal;
                  font-weight: 400;
                }
                .metric { font-family: "Fixture Chart", sans-serif; font-size: 19px; }
                """;
            document.head.appendChild(style);

            var label = HostTestUtilities.GetElement(document.createElement("span"));
            label.className = "metric";
            label.textContent = "iiiiWWWW 123";
            HostTestUtilities.GetElement(document.body).appendChild(label);

            document.EnsureStylesCurrent();
            Assert.Equal(1, document.FontFaces.FaceCount);
            var textNode = Assert.IsType<AvaloniaDomTextNode>(label.firstChild);
            var text = Assert.IsType<DomTextBlockControl>(textNode.Control);
            var fallback = text.FontFamily;
            text.Measure(Size.Infinity);
            var fallbackWidth = text.DesiredSize.Width;

            WaitUntil(() => document.FontFaces.LoadedFaceCount == 1, document.FontFaces);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.NotEqual(fallback, text.FontFamily);
            Assert.StartsWith("fonts:webscene-", text.FontFamily.ToString(), StringComparison.Ordinal);
            text.Measure(Size.Infinity);
            Assert.NotEqual(fallbackWidth, text.DesiredSize.Width);

            var canvas = HostTestUtilities.GetElement(document.createElement("canvas"));
            HostTestUtilities.GetElement(document.body).appendChild(canvas);
            var context = Assert.IsType<CanvasRenderingContext2D>(canvas.getContext("2d"));
            context.font = "400 19px \"Fixture Chart\", sans-serif";
            context.fillText("iiiiWWWW 123", 0, 20);
            var surface = Assert.IsType<CanvasDrawingSurface>(canvas.Control);
            var command = Assert.IsType<FillTextCommand>(surface.Commands.Single());
            Assert.Equal(text.FontFamily, command.Snapshot.Typeface.FontFamily);
        }
        finally
        {
            window.Close();
            try { Directory.Delete(directory, recursive: true); }
            catch { }
        }
    }

    [AvaloniaFact]
    public void FaceMatchingUsesAuthoredStyleAndWeightInsteadOfOnlyFamilyName()
    {
        var sourceFont = FindPlatformTrueTypeFont();
        var directory = Path.Combine(Path.GetTempPath(), "webscene-font-match-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        File.Copy(sourceFont, Path.Combine(directory, "regular.ttf"));
        File.Copy(sourceFont, Path.Combine(directory, "bold-italic.ttf"));

        var window = new Window { Content = new Canvas() };
        using var host = new AvaloniaBrowserHost(window) { ScriptBaseDirectory = directory };
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = $$"""
                @font-face { font-family: 'Weighted Fixture'; src: url('{{new Uri(Path.Combine(directory, "regular.ttf")).AbsoluteUri}}'); font-style: normal; font-weight: 400; }
                @font-face { font-family: 'Weighted Fixture'; src: url('{{new Uri(Path.Combine(directory, "bold-italic.ttf")).AbsoluteUri}}'); font-style: italic; font-weight: 700; }
                """;
            document.head.appendChild(style);
            document.EnsureStylesCurrent();
            Assert.Equal(2, document.FontFaces.FaceCount);
            WaitUntil(() => document.FontFaces.LoadedFaceCount == 2, document.FontFaces);

            var regular = Assert.NotNull(document.FontFaces.Resolve(
                "'Weighted Fixture'", FontStyle.Normal, FontWeight.Normal, FontStretch.Normal));
            var boldItalic = Assert.NotNull(document.FontFaces.Resolve(
                "'Weighted Fixture'", FontStyle.Italic, FontWeight.Bold, FontStretch.Normal));
            Assert.NotEqual(regular.Family.ToString(), boldItalic.Family.ToString());
        }
        finally
        {
            window.Close();
            try { Directory.Delete(directory, recursive: true); }
            catch { }
        }
    }

    [Fact]
    public void FontSourceParserPreservesOrderedQuotedAndUnquotedUrls()
    {
        var urls = CssFontSourceParser.ExtractUrls(
            "local('Fixture'), url(../font.woff2) format('woff2'), url(\"../fallback.ttf\") format('truetype')");

        Assert.Equal(new[] { "../font.woff2", "../fallback.ttf" }, urls);
    }

    private static void WaitUntil(Func<bool> predicate, CssFontFaceRegistry registry)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!predicate() && DateTime.UtcNow < timeout)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }
        Dispatcher.UIThread.RunJobs();
        Assert.True(predicate(),
            "Timed out waiting for the downloadable font face to install. " + string.Join(" | ", registry.LoadErrors));
    }

    private static string FindPlatformTrueTypeFont()
    {
        var preferred = OperatingSystem.IsWindows()
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Fonts", "consola.ttf")
            : OperatingSystem.IsMacOS()
                ? "/System/Library/Fonts/SFNSMono.ttf"
                : "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
        if (File.Exists(preferred)) return preferred;

        var roots = OperatingSystem.IsWindows()
            ? new[] { Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Fonts") }
            : OperatingSystem.IsMacOS()
                ? new[] { "/System/Library/Fonts", "/Library/Fonts" }
                : new[] { "/usr/share/fonts", "/usr/local/share/fonts" };
        var font = roots.Where(Directory.Exists)
            .SelectMany(root => Directory.EnumerateFiles(root, "*.ttf", SearchOption.AllDirectories))
            .FirstOrDefault();
        return font ?? throw new InvalidOperationException("The test platform has no TrueType font fixture.");
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

/// <summary>
/// Reduced authority for CSS2 positioning/abspos-negative-margin-001.html.
/// This deliberately avoids text metrics and source-indentation whitespace so
/// the missing primitive is only propagation of an auto-inset absolute box's
/// hypothetical inline static position through nested inline ancestors.
/// </summary>
public sealed class CssAbsoluteInlineStaticPositionSpikeTests
{
    [AvaloniaFact]
    public void AutoInsetAbsoluteDescendantKeepsNestedInlineStaticPositionAcrossNegativeMarginAncestor()
    {
        var root = new CssLayoutPanel { Width = 120, Height = 40 };
        var window = new Window { Width = 120, Height = 40, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 40px; margin: 0; padding: 0; width: 120px; }
            .row { height: 10px; line-height: 10px; width: 120px; }
            .prefix { display: inline-block; height: 10px; width: 7px; }
            .negative { margin-right: -10px; }
            .token { display: inline-block; height: 10px; width: 5px; }
            .absolute { height: 10px; position: absolute; width: 10px; }
            """;
        document.head.appendChild(style);

        var row = Append(document, "div", "row");
        var outer = Append(document, "span", null, row);
        _ = Append(document, "span", "prefix", outer);
        var negative = Append(document, "span", "negative", outer);
        _ = Append(document, "span", "token", negative);
        var absolute = Append(document, "span", "absolute", negative);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        // The static-position rectangle is where the box would have appeared
        // in the original inline formatting context: 7px prefix + 5px token.
        // The ancestor's -10px inline-end margin affects subsequent flow, not
        // this already-established hypothetical start position.
        var native = absolute.getBoundingClientRect();
        Assert.Equal((12d, 0d, 10d, 10d), (native.left, native.top, native.width, native.height));

        var body = HostTestUtilities.GetElement(document.body);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(body.Control),
            new Size(120, 40)).GetBox(absolute.Control).BorderBox;
        Assert.Equal((12d, 0d, 10d, 10d),
            (portable.X, portable.Y, portable.Width, portable.Height));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void InlineAndBlockAbsoluteBoxesUseTheirHypotheticalLinePositions()
    {
        var root = new CssLayoutPanel { Width = 120, Height = 50 };
        var window = new Window { Width = 120, Height = 50, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 50px; margin: 0; padding: 0; width: 120px; }
            html { font-size: 10px; line-height: 1; }
            .abspos {
                background: blue;
                height: 10px;
                position: absolute;
                width: 10px;
            }
            .token { display: inline-block; width: 5px; }
            """;
        document.head.appendChild(style);

        var inlineAbsolute = AppendWptRow(document, "span");
        var blockAbsolute = AppendWptRow(document, "div");

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertRect(inlineAbsolute.getBoundingClientRect(), 5, 0, 10, 10);
        AssertRect(blockAbsolute.getBoundingClientRect(), 0, 20, 10, 10);

        var body = HostTestUtilities.GetElement(document.body);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(body.Control),
            new Size(120, 50));
        Assert.Equal((5d, 0d, 10d, 10d), RectTuple(portable.GetBox(inlineAbsolute.Control).BorderBox));
        Assert.Equal((0d, 20d, 10d, 10d), RectTuple(portable.GetBox(blockAbsolute.Control).BorderBox));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void SourceIndentationCollapsesAtLineBoundariesAroundStaticPosition()
    {
        var root = new CssLayoutPanel { Width = 120, Height = 50 };
        var window = new Window { Width = 120, Height = 50, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 50px; margin: 0; padding: 0; width: 120px; }
            html { font-size: 10px; line-height: 1; }
            .abspos { height: 10px; position: absolute; width: 10px; }
            .token { display: inline-block; width: 5px; }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = """
              <div>
                <span>
                  <span style="margin-right: -10px;">
                    <span class="token">x</span><span class="abspos"></span>
                  </span>
                </span>
              </div>
              <div>
                <span>
                  <span style="margin-right: -10px;">
                    <span class="token">x</span><div class="abspos"></div>
                  </span>
                </span>
              </div>
            """;
        var absolute = document.querySelectorAll(".abspos")
            .Cast<AvaloniaDomElement>()
            .ToArray();

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertRect(absolute[0].getBoundingClientRect(), 5, 0, 10, 10);
        AssertRect(absolute[1].getBoundingClientRect(), 0, 20, 10, 10);

        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(body.Control),
            new Size(120, 50));
        Assert.Equal((5d, 0d, 10d, 10d), RectTuple(portable.GetBox(absolute[0].Control).BorderBox));
        Assert.Equal((0d, 20d, 10d, 10d), RectTuple(portable.GetBox(absolute[1].Control).BorderBox));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void WhiteSpacePresentationPreservesAuthoredDomTextData()
    {
        var root = new CssLayoutPanel { Width = 160, Height = 80 };
        var window = new Window { Width = 160, Height = 80, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var parent = Append(document, "div", null);
        const string authored = "\n  A \t B\r\n C  ";
        var textNode = Assert.IsType<AvaloniaDomTextNode>(document.createTextNode(authored));
        parent.appendChild(textNode);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal(authored, textNode.data);
        Assert.Equal(authored, parent.textContent);
        Assert.Equal("A B C", Assert.IsType<DomTextBlockControl>(textNode.Control).Text);

        parent.style.setProperty("white-space", "pre");
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(authored, textNode.data);
        Assert.Equal(authored, Assert.IsType<DomTextBlockControl>(textNode.Control).Text);

        parent.style.setProperty("white-space", "normal");
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal("A B C", Assert.IsType<DomTextBlockControl>(textNode.Control).Text);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void BrTerminatesTheCurrentInlineLineInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 120, Height = 40 };
        var window = new Window { Width = 120, Height = 40, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 40px; margin: 0; padding: 0; width: 120px; }
            html { font-size: 10px; line-height: 1; }
            .blue { display: inline-block; height: 10px; width: 10px; }
            """;
        document.head.appendChild(style);
        var row = Append(document, "div", null);
        row.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode("x")));
        _ = Append(document, "br", null, row);
        var blue = Append(document, "span", "blue", row);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertRect(blue.getBoundingClientRect(), 0, 10, 10, 10);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(row.Control),
            new Size(120, 40));
        Assert.Equal((0d, 10d, 10d, 10d), RectTuple(portable.GetBox(blue.Control).BorderBox));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void WhitespaceOnlyTextAtBlockBoundariesDoesNotGenerateLineBoxes()
    {
        var root = new CssLayoutPanel { Width = 120, Height = 40 };
        var window = new Window { Width = 120, Height = 40, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 40px; margin: 0; padding: 0; width: 120px; }
            .block { height: 10px; width: 20px; }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);
        body.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode("\n    ")));
        var first = Append(document, "div", "block");
        body.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode("\n  \t  ")));
        var second = Append(document, "div", "block");
        body.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode("\n    ")));

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertRect(first.getBoundingClientRect(), 0, 0, 20, 10);
        AssertRect(second.getBoundingClientRect(), 0, 10, 20, 10);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(body.Control),
            new Size(120, 40));
        Assert.Equal((0d, 0d, 20d, 10d), RectTuple(portable.GetBox(first.Control).BorderBox));
        Assert.Equal((0d, 10d, 20d, 10d), RectTuple(portable.GetBox(second.Control).BorderBox));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void ZeroFontSizeAccessibilityTextStaysSuppressedDuringWhitespaceRefresh()
    {
        var root = new CssLayoutPanel { Width = 180, Height = 50 };
        var window = new Window { Width = 180, Height = 50, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 50px; margin: 0; padding: 0; width: 180px; }
            .screen-reader { font-size: 0; }
            .app { height: 20px; width: 180px; }
            """;
        document.head.appendChild(style);
        var screenReader = Append(document, "div", "screen-reader");
        foreach (var instruction in new[] { "Component is interactive", "Quick search", "Change interval" })
        {
            var line = Append(document, "div", null, screenReader);
            line.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode(instruction)));
            _ = Append(document, "br", null, line);
        }
        var app = Append(document, "div", "app");

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertRect(screenReader.getBoundingClientRect(), 0, 0, 180, 0);
        AssertRect(app.getBoundingClientRect(), 0, 0, 180, 20);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(HostTestUtilities.GetElement(document.body).Control),
            new Size(180, 50));
        Assert.Equal(0, portable.GetBox(screenReader.Control).BorderBox.Height);
        Assert.Equal(0, portable.GetBox(app.Control).BorderBox.Y);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    private static AvaloniaDomElement AppendWptRow(AvaloniaDomDocument document, string absoluteTag)
    {
        var row = Append(document, "div", null);
        var outer = Append(document, "span", null, row);
        var negative = Append(document, "span", null, outer);
        negative.style.setProperty("margin-right", "-10px");
        var token = Append(document, "span", "token", negative);
        token.textContent = "x";
        return Append(document, absoluteTag, "abspos", negative);
    }

    private static void AssertRect(DomRect rect, double x, double y, double width, double height)
        => Assert.Equal((x, y, width, height), (rect.left, rect.top, rect.width, rect.height));

    private static (double X, double Y, double Width, double Height) RectTuple(WebScene.Core.WebSceneRect rect)
        => (rect.X, rect.Y, rect.Width, rect.Height);

    private static AvaloniaDomElement Append(
        AvaloniaDomDocument document,
        string tag,
        string? className,
        AvaloniaDomElement? parent = null)
    {
        var element = HostTestUtilities.GetElement(document.createElement(tag));
        if (!string.IsNullOrEmpty(className))
        {
            element.className = className;
        }
        (parent ?? HostTestUtilities.GetElement(document.body)).appendChild(element);
        return element;
    }
}

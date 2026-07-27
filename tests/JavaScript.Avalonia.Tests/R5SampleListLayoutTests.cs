using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using WebScene.Css;
using System.Runtime.InteropServices;
using Xunit;

namespace JavaScript.Avalonia.Tests;

/// <summary>
/// Isolated layout contract for the plain ul/li markup shared by the R5
/// ReactDashboard activity card and TypeScriptDesktop async-records view.
/// </summary>
public sealed class R5SampleListLayoutTests
{
    [AvaloniaFact]
    public void ParsedWptMetadataAndOmittedListItemEndTagsKeepUaListStyles()
    {
        var root = new CssLayoutPanel { Width = 800, Height = 600 };
        var window = new Window { Width = 800, Height = 600, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var metadata = HostTestUtilities.GetElement(document.createElement("link"));
        metadata.setAttribute("rel", "match");
        metadata.setAttribute("href", "reference.html");
        document.head.appendChild(metadata);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = "<ol reversed><li>seven<li value=\"6\">six<li>five</ol>";

        document.EnsureStylesCurrent();
        var list = Assert.IsType<AvaloniaDomElement>(body.querySelector("ol"));
        var items = list.GetChildElements().ToArray();
        var listStyle = document.getComputedStyle(list);
        Assert.Equal("block", listStyle.getPropertyValue("display"));
        Assert.Equal("40px", listStyle.getPropertyValue("padding-left"));
        Assert.Equal("decimal", listStyle.getPropertyValue("list-style-type"));
        Assert.Equal(3, items.Length);
        Assert.All(items, item => Assert.Equal(
            "list-item",
            document.getComputedStyle(item).getPropertyValue("display")));
        Assert.Equal(new[] { "7.", "6.", "5." }, items.Select(item =>
            Assert.IsType<CssLayoutPanel>(item.Control).ListMarker?.Text).ToArray());

        var inserted = HostTestUtilities.GetElement(document.createElement("li"));
        inserted.textContent = "eight";
        list.insertBefore(inserted, items[0]);
        document.EnsureStylesCurrent();
        items = list.GetChildElements().ToArray();
        Assert.Equal(new[] { "8.", "7.", "6.", "5." }, items.Select(item =>
            Assert.IsType<CssLayoutPanel>(item.Control).ListMarker?.Text).ToArray());

        items[2].setAttribute("value", "4");
        document.EnsureStylesCurrent();
        Assert.Equal(new[] { "6.", "5.", "4.", "3." }, items.Select(item =>
            Assert.IsType<CssLayoutPanel>(item.Control).ListMarker?.Text).ToArray());
    }

    [AvaloniaFact]
    public void ParsedWptFlexFirstChildKeepsUaMarkerAndIndentation()
    {
        var root = new CssLayoutPanel { Width = 800, Height = 600 };
        var window = new Window { Width = 800, Height = 600, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var metadata = HostTestUtilities.GetElement(document.createElement("link"));
        metadata.setAttribute("rel", "match");
        metadata.setAttribute("href", "reference.html");
        document.head.appendChild(metadata);
        var body = HostTestUtilities.GetElement(document.body);
        body.innerHTML = """
            <p>There should be no extra line generated between the marker and the flex.</p>
            <ul><li><div style="border: 1px black solid;"><div style="display: flex; align-items: flex-end; height: 200px;"><span style="line-height: 50px">text</span></div></div></li></ul>
            """;

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        var list = Assert.IsType<AvaloniaDomElement>(body.querySelector("ul"));
        var item = Assert.IsType<AvaloniaDomElement>(body.querySelector("li"));
        var bordered = item.firstElementChild!;
        var listStyle = document.getComputedStyle(list);
        Assert.Equal("block", listStyle.getPropertyValue("display"));
        Assert.Equal("40px", listStyle.getPropertyValue("padding-left"));
        Assert.Equal("list-item", document.getComputedStyle(item).getPropertyValue("display"));
        Assert.NotNull(Assert.IsType<CssLayoutPanel>(item.Control).ListMarker);
        Assert.Equal(list.getBoundingClientRect().left + 40, item.getBoundingClientRect().left, 3);
        Assert.Equal(item.getBoundingClientRect().left, bordered.getBoundingClientRect().left, 3);
    }

    [AvaloniaFact]
    public void VirtualIframeInitialHydrationStylesEntireParsedListSubtree()
    {
        var root = new CssLayoutPanel { Width = 800, Height = 600 };
        var frameBody = new CssLayoutPanel();
        root.Children.Add(frameBody);
        var window = new Window { Width = 800, Height = 600, Content = root };
        using var host = new AvaloniaBrowserHost(window);
        var document = new VirtualIframeDomDocument(host, frameBody);
        var body = HostTestUtilities.GetElement(document.body);
        body.SetStyleProperty("width", "100%");
        body.SetStyleProperty("height", "100%");
        body.SetStyleProperty("overflow", "hidden");
        // External iframe runtime creation observes the empty document before
        // the queued navigation hydrates its head and body.
        document.EnsureStylesCurrent();
        var metadata = HostTestUtilities.GetElement(document.createElement("link"));
        metadata.setAttribute("rel", "match");
        metadata.setAttribute("href", "reference.html");
        document.head.appendChild(metadata);
        body.innerHTML = "<ol reversed><li>seven<li value=\"6\">six<li>five</ol>";

        document.EnsureStylesCurrent();
        var list = Assert.IsType<AvaloniaDomElement>(body.querySelector("ol"));
        var item = Assert.IsType<AvaloniaDomElement>(body.querySelector("li"));
        Assert.Equal("block", document.getComputedStyle(list).getPropertyValue("display"));
        Assert.Equal("40px", document.getComputedStyle(list).getPropertyValue("padding-left"));
        Assert.Equal("list-item", document.getComputedStyle(item).getPropertyValue("display"));
        Assert.NotNull(Assert.IsType<CssLayoutPanel>(item.Control).ListMarker);
    }

    [Fact]
    public void StylesheetCompilerPreservesListStyleAsOneAuthoredCascadeDeclaration()
    {
        var compilation = CssStylesheetCompiler.Compile("""
            #ordinary { list-style: square inside !important; list-style-type: circle; }
            #wide { list-style: initial; }
            #variable { list-style: var(--marker); }
            """);
        Assert.Collection(
            compilation.Rules,
            rule =>
            {
                Assert.Equal("list-style", rule.Declarations[0].Name);
                Assert.Equal("square inside", rule.Declarations[0].Value);
                Assert.True(rule.Declarations[0].Important);
                Assert.Equal("list-style-type", rule.Declarations[1].Name);
            },
            rule =>
            {
                var declaration = Assert.Single(rule.Declarations);
                Assert.Equal("list-style", declaration.Name);
                Assert.Equal("initial", declaration.Value);
            },
            rule =>
            {
                var declaration = Assert.Single(rule.Declarations);
                Assert.Equal("list-style", declaration.Name);
                Assert.Equal("var(--marker)", declaration.Value);
            });
    }

    [AvaloniaFact]
    public void PlainR5ListUsesBrowserUaGeometryAndMarkersInNativeAndPortableLayout()
    {
        using var fixture = CreateFixture();

        var listStyle = fixture.Document.getComputedStyle(fixture.List);
        var firstStyle = fixture.Document.getComputedStyle(fixture.FirstItem);
        Assert.Equal("block", listStyle.getPropertyValue("display"));
        Assert.Equal("16px", listStyle.getPropertyValue("margin-top"));
        Assert.Equal("16px", listStyle.getPropertyValue("margin-bottom"));
        Assert.Equal("40px", listStyle.getPropertyValue("padding-left"));
        Assert.Equal("disc", listStyle.getPropertyValue("list-style-type"));
        Assert.Equal("outside", listStyle.getPropertyValue("list-style-position"));
        Assert.Equal("list-item", firstStyle.getPropertyValue("display"));

        AssertBrowserGeometry(fixture);
        AssertPortableGeometry(fixture);

        EnableNativeLayout(fixture);
        AssertBrowserGeometry(fixture);
        var firstPanel = Assert.IsType<CssLayoutPanel>(fixture.FirstItem.Control);
        Assert.True(firstPanel.TryResolveListMarkerRect(firstPanel.Bounds.Size, out var markerRect));
        Assert.True(firstPanel.HitTest(markerRect.Center));
        AssertOutsideMarkersPaint(fixture);
    }

    private static Fixture CreateFixture()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 180, Background = Brushes.White };
        var window = new Window { Width = 300, Height = 180, Background = Brushes.White, Content = root };
        var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;

        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            body { margin: 0; }
            .card {
                box-sizing: border-box;
                width: 300px;
                padding: 16px;
                color: black;
                background: white;
                font: 16px/18px sans-serif;
            }
            li { margin-bottom: 8px; }
            """;
        document.head.appendChild(style);

        var card = HostTestUtilities.GetElement(document.createElement("section"));
        card.className = "card";
        var list = HostTestUtilities.GetElement(document.createElement("ul"));
        var first = HostTestUtilities.GetElement(document.createElement("li"));
        first.textContent = "Package validated";
        var second = HostTestUtilities.GetElement(document.createElement("li"));
        second.textContent = "Dashboard data refreshed";
        list.appendChild(first);
        list.appendChild(second);
        card.appendChild(list);
        HostTestUtilities.GetElement(document.body).appendChild(card);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        return new Fixture(window, host, document, card, list, first, second);
    }

    private static void AssertBrowserGeometry(Fixture fixture)
    {
        var card = fixture.Card.getBoundingClientRect();
        var list = fixture.List.getBoundingClientRect();
        var first = fixture.FirstItem.getBoundingClientRect();
        var second = fixture.SecondItem.getBoundingClientRect();

        Assert.Equal(card.left + 16, list.left, 3);
        Assert.Equal(card.left + 56, first.left, 3);
        Assert.Equal(first.left, second.left, 3);
        Assert.Equal(18, first.height, 3);
        Assert.Equal(first.bottom + 8, second.top, 3);
        Assert.Equal(44, list.height, 3);
    }

    private static void AssertPortableGeometry(Fixture fixture)
    {
        var snapshot = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Card.Control),
            new Size(300, 108));
        var list = snapshot.GetBox(fixture.List.Control).BorderBox;
        var first = snapshot.GetBox(fixture.FirstItem.Control).BorderBox;
        var second = snapshot.GetBox(fixture.SecondItem.Control).BorderBox;

        Assert.Equal(list.Left + 40, first.Left, 3);
        Assert.Equal(first.Left, second.Left, 3);
        Assert.Equal(18, first.Height, 3);
        Assert.Equal(first.Bottom + 8, second.Top, 3);
        Assert.Equal(44, list.Height, 3);

        AssertPortableMarker(fixture.FirstItem, first);
        AssertPortableMarker(fixture.SecondItem, second);

        void AssertPortableMarker(AvaloniaDomElement element, WebScene.Core.WebSceneRect item)
        {
            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            Assert.True(snapshot.TryGetListMarkerBox(panel, out var marker));
            Assert.Equal(item.Left - 16, marker.BorderBox.Left, 3);
            Assert.Equal(7, marker.BorderBox.Width, 3);
            Assert.Equal(18, marker.BorderBox.Height, 3);
            Assert.True(marker.BorderBox.Left >= list.Left);
        }
    }

    [AvaloniaFact]
    public void HtmlUaDisplayDefaultsRemainBelowAuthorOriginAndInitialUsesCssInitialValue()
    {
        var root = new CssLayoutPanel { Width = 400, Height = 300 };
        var window = new Window { Width = 400, Height = 300, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = "#initial { display: initial; } #scaled { font-size: 20px; } #relative { font-size: 125%; } #authored { margin-top: 3px; }";
        document.head.appendChild(style);

        var expected = new Dictionary<string, string>
        {
            ["div"] = "block",
            ["section"] = "block",
            ["details"] = "block",
            ["dialog"] = "block",
            ["summary"] = "list-item",
            ["table"] = "table",
            ["tbody"] = "table-row-group",
            ["tr"] = "table-row",
            ["td"] = "table-cell",
            ["span"] = "inline",
            ["webscene-widget"] = "inline"
        };
        foreach (var pair in expected)
        {
            var element = HostTestUtilities.GetElement(document.createElement(pair.Key));
            HostTestUtilities.GetElement(document.body).appendChild(element);
            Assert.Equal(pair.Value, document.getComputedStyle(element).getPropertyValue("display"));
        }

        var initial = HostTestUtilities.GetElement(document.createElement("div"));
        initial.id = "initial";
        HostTestUtilities.GetElement(document.body).appendChild(initial);
        Assert.Equal("inline", document.getComputedStyle(initial).getPropertyValue("display"));

        var scaled = HostTestUtilities.GetElement(document.createElement("ul"));
        scaled.id = "scaled";
        HostTestUtilities.GetElement(document.body).appendChild(scaled);
        Assert.Equal("20px", document.getComputedStyle(scaled).getPropertyValue("margin-top"));

        var relative = HostTestUtilities.GetElement(document.createElement("ul"));
        relative.id = "relative";
        HostTestUtilities.GetElement(document.body).appendChild(relative);
        var relativeStyle = document.getComputedStyle(relative);
        Assert.Equal("20px", relativeStyle.getPropertyValue("font-size"));
        Assert.Equal("20px", relativeStyle.getPropertyValue("margin-top"));

        var authored = HostTestUtilities.GetElement(document.createElement("ul"));
        authored.id = "authored";
        HostTestUtilities.GetElement(document.body).appendChild(authored);
        Assert.Equal("3px", document.getComputedStyle(authored).getPropertyValue("margin-top"));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void ListStyleShorthandHonorsCascadeCssWideVariablesAndInvalidSyntax()
    {
        var root = new CssLayoutPanel { Width = 400, Height = 300 };
        var window = new Window { Width = 400, Height = 300, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            #parent { list-style: circle inside; }
            #ordered { list-style-type: square; list-style: circle outside; list-style-position: inside; }
            #important { list-style: square inside !important; list-style-type: circle; list-style-position: outside; }
            #inherit { list-style: inherit; }
            #initial { list-style: initial; }
            #unset { list-style: unset; }
            #var-inherit { list-style: var(--missing, inherit); }
            #var-initial { list-style: var(--missing, initial); }
            #var-unset { list-style: var(--missing, unset); }
            #invalid { list-style-type: square; list-style-position: inside; list-style: bogus; list-style: disc circle; }
            #invalid-var { --value: bogus; list-style: square outside; list-style: var(--value); }
            """;
        document.head.appendChild(style);
        var parent = HostTestUtilities.GetElement(document.createElement("ul"));
        parent.id = "parent";
        HostTestUtilities.GetElement(document.body).appendChild(parent);

        AssertStyle("ordered", "circle", "inside");
        AssertStyle("important", "square", "inside");
        AssertStyle("inherit", "circle", "inside");
        AssertStyle("initial", "disc", "outside");
        AssertStyle("unset", "circle", "inside");
        AssertStyle("var-inherit", "circle", "inside");
        AssertStyle("var-initial", "disc", "outside");
        AssertStyle("var-unset", "circle", "inside");
        AssertStyle("invalid", "square", "inside");
        // Invalid after var() substitution is invalid at computed-value time;
        // inherited list longhands win rather than an earlier declaration.
        AssertStyle("invalid-var", "circle", "inside");

        window.Close();
        Dispatcher.UIThread.RunJobs();
        return;

        void AssertStyle(string id, string type, string position)
        {
            var item = HostTestUtilities.GetElement(document.createElement("li"));
            item.id = id;
            parent.appendChild(item);
            var computed = document.getComputedStyle(item);
            Assert.Equal(type, computed.getPropertyValue("list-style-type"));
            Assert.Equal(position, computed.getPropertyValue("list-style-position"));
        }
    }

    [AvaloniaFact]
    public void ListBlockMarginCollapseIsFencedAndDoesNotSwallowLargerChildEdgeStruts()
    {
        var root = new CssLayoutPanel { Width = 160, Height = 400 };
        var window = new Window { Width = 160, Height = 400, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            body { margin: 0; }
            #wrapper { width: 100px; }
            .case { margin: 16px 0; }
            .child { height: 10px; }
            #dominant > .child { margin: 0 0 8px; }
            #larger { margin: 0; }
            #larger > .child { margin: 20px 0 30px; }
            #padded { padding-bottom: 1px; }
            #padded > .child, #overflow > .child { margin-bottom: 8px; }
            #overflow { overflow: hidden; }
            #siblings > .first { margin-bottom: 12px; }
            #siblings > .second { margin-top: 7px; }
            #negative > .first { margin-bottom: -5px; }
            #negative > .second { margin-top: -8px; }
            """;
        document.head.appendChild(style);
        var wrapper = HostTestUtilities.GetElement(document.createElement("div"));
        wrapper.id = "wrapper";
        HostTestUtilities.GetElement(document.body).appendChild(wrapper);

        var cases = new Dictionary<AvaloniaDomElement, double>
        {
            [CreateCase("dominant", 1)] = 10,
            [CreateCase("larger", 1)] = 60,
            [CreateCase("padded", 1)] = 19,
            [CreateCase("overflow", 1)] = 18,
            [CreateCase("siblings", 2)] = 32,
            [CreateCase("negative", 2)] = 12
        };

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        var snapshot = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(wrapper.Control),
            new Size(100, 400));
        foreach (var pair in cases)
        {
            Assert.Equal(pair.Value, snapshot.GetBox(pair.Key.Control).BorderBox.Height, 3);
        }

        foreach (var panel in cases.Keys.Append(wrapper)
                     .Select(static element => element.Control)
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        Dispatcher.UIThread.RunJobs();
        foreach (var pair in cases)
        {
            Assert.Equal(pair.Value, pair.Key.getBoundingClientRect().height, 3);
        }

        window.Close();
        Dispatcher.UIThread.RunJobs();
        return;

        AvaloniaDomElement CreateCase(string id, int childCount)
        {
            var parent = HostTestUtilities.GetElement(document.createElement("div"));
            parent.id = id;
            parent.className = "case";
            for (var index = 0; index < childCount; index++)
            {
                var child = HostTestUtilities.GetElement(document.createElement("div"));
                child.className = index == 0 ? "child first" : "child second";
                parent.appendChild(child);
            }
            wrapper.appendChild(parent);
            return parent;
        }
    }

    [AvaloniaFact]
    public void MarkerKindsOrderedValuesNestingAndCounterMutationsStayCoherent()
    {
        var root = new CssLayoutPanel { Width = 400, Height = 400 };
        var window = new Window { Width = 400, Height = 400, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            ol { list-style-type: decimal; }
            #circle { list-style: circle outside; }
            #square { list-style: square; }
            #none { list-style: none; }
            """;
        document.head.appendChild(style);
        var body = HostTestUtilities.GetElement(document.body);

        var kinds = HostTestUtilities.GetElement(document.createElement("ul"));
        body.appendChild(kinds);
        var circle = AppendItem(kinds, "circle");
        var square = AppendItem(kinds, "square");
        var none = AppendItem(kinds, "none");

        var ordered = HostTestUtilities.GetElement(document.createElement("ol"));
        ordered.setAttribute("start", "3");
        body.appendChild(ordered);
        var orderedFirst = AppendItem(ordered);
        var orderedValue = AppendItem(ordered);
        orderedValue.setAttribute("value", "8");
        var orderedThird = AppendItem(ordered);

        var reversed = HostTestUtilities.GetElement(document.createElement("ol"));
        reversed.setAttribute("reversed", string.Empty);
        body.appendChild(reversed);
        var reversedFirst = AppendItem(reversed);
        var reversedValue = AppendItem(reversed);
        reversedValue.setAttribute("value", "1");
        var reversedThird = AppendItem(reversed);

        var nestedHost = AppendItem(ordered);
        var nested = HostTestUtilities.GetElement(document.createElement("ol"));
        nested.setAttribute("start", "4");
        nestedHost.appendChild(nested);
        var nestedFirst = AppendItem(nested);
        var nestedSecond = AppendItem(nested);

        document.EnsureStylesCurrent();
        Assert.Equal(CssListStyleType.Circle, Marker(circle).Type);
        Assert.Equal(CssListStyleType.Square, Marker(square).Type);
        Assert.Null(Assert.IsType<CssLayoutPanel>(none.Control).ListMarker);
        AssertTexts((orderedFirst, "3."), (orderedValue, "8."), (orderedThird, "9."));
        AssertTexts((reversedFirst, "2."), (reversedValue, "1."), (reversedThird, "0."));
        AssertTexts((nestedFirst, "4."), (nestedSecond, "5."));

        ordered.setAttribute("start", "5");
        orderedThird.setAttribute("value", "12");
        var reversedFourth = AppendItem(reversed);
        document.EnsureStylesCurrent();
        AssertTexts((orderedFirst, "5."), (orderedValue, "8."), (orderedThird, "12."));
        AssertTexts((reversedFirst, "2."), (reversedValue, "1."), (reversedThird, "0."), (reversedFourth, "-1."));

        window.Close();
        Dispatcher.UIThread.RunJobs();
        return;

        AvaloniaDomElement AppendItem(AvaloniaDomElement list, string? id = null)
        {
            var item = HostTestUtilities.GetElement(document.createElement("li"));
            if (id is not null) item.id = id;
            item.textContent = "item";
            list.appendChild(item);
            return item;
        }

        static CssListMarker Marker(AvaloniaDomElement item)
            => Assert.IsType<CssListMarker>(Assert.IsType<CssLayoutPanel>(item.Control).ListMarker);

        static void AssertTexts(params (AvaloniaDomElement Item, string Text)[] expected)
        {
            foreach (var pair in expected) Assert.Equal(pair.Text, Marker(pair.Item).Text);
        }
    }

    [AvaloniaFact]
    public void ReactAndTypeScriptR5ListShapesMatchChromeAuthority()
    {
        var root = new CssLayoutPanel { Width = 360, Height = 300 };
        var window = new Window { Width = 360, Height = 300, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            body { margin: 0; }
            #fixture { width: 300px; font: 16px/18px sans-serif; }
            #react li { margin-bottom: 8px; }
            """;
        document.head.appendChild(style);
        var fixture = HostTestUtilities.GetElement(document.createElement("div"));
        fixture.id = "fixture";
        HostTestUtilities.GetElement(document.body).appendChild(fixture);
        var react = AppendList("react", 4);
        var typescript = AppendList("typescript", 3);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        AssertShape(react, expectedHeight: 96, expectedStep: 26);
        AssertShape(typescript, expectedHeight: 54, expectedStep: 18);

        var snapshot = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(fixture.Control),
            new Size(300, 300));
        AssertPortableShape(react, expectedHeight: 96, expectedStep: 26);
        AssertPortableShape(typescript, expectedHeight: 54, expectedStep: 18);

        foreach (var panel in new[] { fixture, react, typescript }
                     .Concat(react.GetChildElements())
                     .Concat(typescript.GetChildElements())
                     .Select(static element => element.Control)
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        Dispatcher.UIThread.RunJobs();
        AssertShape(react, expectedHeight: 96, expectedStep: 26);
        AssertShape(typescript, expectedHeight: 54, expectedStep: 18);

        window.Close();
        Dispatcher.UIThread.RunJobs();
        return;

        AvaloniaDomElement AppendList(string id, int count)
        {
            var list = HostTestUtilities.GetElement(document.createElement("ul"));
            list.id = id;
            for (var index = 0; index < count; index++)
            {
                var item = HostTestUtilities.GetElement(document.createElement("li"));
                item.textContent = $"item {index + 1}";
                list.appendChild(item);
            }
            fixture.appendChild(list);
            return list;
        }

        static void AssertShape(AvaloniaDomElement list, double expectedHeight, double expectedStep)
        {
            var items = list.GetChildElements().ToArray();
            Assert.Equal(expectedHeight, list.getBoundingClientRect().height, 3);
            for (var index = 1; index < items.Length; index++)
            {
                Assert.Equal(
                    expectedStep,
                    items[index].getBoundingClientRect().top - items[index - 1].getBoundingClientRect().top,
                    3);
            }
        }

        void AssertPortableShape(AvaloniaDomElement list, double expectedHeight, double expectedStep)
        {
            var items = list.GetChildElements().ToArray();
            Assert.Equal(expectedHeight, snapshot.GetBox(list.Control).BorderBox.Height, 3);
            for (var index = 1; index < items.Length; index++)
            {
                Assert.Equal(
                    expectedStep,
                    snapshot.GetBox(items[index].Control).BorderBox.Top
                    - snapshot.GetBox(items[index - 1].Control).BorderBox.Top,
                    3);
            }
        }
    }

    private static void EnableNativeLayout(Fixture fixture)
    {
        foreach (var panel in new[]
                 {
                     fixture.Card.Control as CssLayoutPanel,
                     fixture.List.Control as CssLayoutPanel,
                     fixture.FirstItem.Control as CssLayoutPanel,
                     fixture.SecondItem.Control as CssLayoutPanel
                 }.OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        fixture.Card.Control.InvalidateMeasure();
        fixture.Card.Control.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
    }

    private static void AssertOutsideMarkersPaint(Fixture fixture)
    {
        using var frame = Assert.IsAssignableFrom<Bitmap>(fixture.Window.CaptureRenderedFrame());
        var pixels = CopyPixels(frame);
        AssertMarker(fixture.FirstItem.getBoundingClientRect());
        AssertMarker(fixture.SecondItem.getBoundingClientRect());

        void AssertMarker(DomRect item)
        {
            var left = Math.Max(0, (int)Math.Floor(item.left - 24));
            var right = Math.Min(frame.PixelSize.Width, (int)Math.Ceiling(item.left - 4));
            var top = Math.Max(0, (int)Math.Floor(item.top));
            var bottom = Math.Min(frame.PixelSize.Height, (int)Math.Ceiling(item.bottom));
            Assert.True(
                ContainsDarkPixel(frame, pixels, left, top, right, bottom),
                $"Expected an outside list marker in ({left},{top})-({right},{bottom}).");
        }
    }

    private static byte[] CopyPixels(Bitmap bitmap)
    {
        var stride = bitmap.PixelSize.Width * 4;
        var bytes = new byte[stride * bitmap.PixelSize.Height];
        var handle = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(
                new PixelRect(bitmap.PixelSize),
                handle.AddrOfPinnedObject(),
                bytes.Length,
                stride);
        }
        finally
        {
            handle.Free();
        }
        return bytes;
    }

    private static bool ContainsDarkPixel(
        Bitmap bitmap,
        byte[] pixels,
        int left,
        int top,
        int right,
        int bottom)
    {
        var rgba = bitmap.Format == PixelFormat.Rgba8888;
        var stride = bitmap.PixelSize.Width * 4;
        for (var y = top; y < bottom; y++)
        for (var x = left; x < right; x++)
        {
            var offset = y * stride + x * 4;
            var red = pixels[offset + (rgba ? 0 : 2)];
            var green = pixels[offset + 1];
            var blue = pixels[offset + (rgba ? 2 : 0)];
            var alpha = pixels[offset + 3];
            if (alpha > 128 && red < 128 && green < 128 && blue < 128)
            {
                return true;
            }
        }
        return false;
    }

    private sealed class Fixture(
        Window window,
        AvaloniaBrowserHost host,
        AvaloniaDomDocument document,
        AvaloniaDomElement card,
        AvaloniaDomElement list,
        AvaloniaDomElement firstItem,
        AvaloniaDomElement secondItem) : IDisposable
    {
        public Window Window { get; } = window;
        public AvaloniaDomDocument Document { get; } = document;
        public AvaloniaDomElement Card { get; } = card;
        public AvaloniaDomElement List { get; } = list;
        public AvaloniaDomElement FirstItem { get; } = firstItem;
        public AvaloniaDomElement SecondItem { get; } = secondItem;

        public void Dispose()
        {
            host.Dispose();
            Window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

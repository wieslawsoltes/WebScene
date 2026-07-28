using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

[Collection(AvaloniaRasterAuthorityCollection.Name)]
public sealed class CssInvalidationOptimizationTests
{
    [AvaloniaFact]
    public void StylePropertyMutationPreservesObservedOldAttributeValue()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.style.cssText = "color: red; width: 10px";
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();

            var callback = new MutationRecordCallback();
            var observer = document.__webSceneCreateExternalMutationObserver(callback);
            observer.__webSceneObserve(
                element,
                childList: false,
                attributes: true,
                subtree: false,
                attributeOldValue: true);

            element.SetStyleProperty("width", "25px");
            Dispatcher.UIThread.RunJobs();

            var record = Assert.Single(callback.Records);
            Assert.Equal("attributes", record.type);
            Assert.Equal("style", record.attributeName);
            Assert.Contains("width: 10px", record.oldValue, StringComparison.Ordinal);
            Assert.Equal("25px", document.getComputedStyle(element).getPropertyValue("width"));
            observer.disconnect();
            window.Close();
        }
    }

    [AvaloniaFact]
    public void InheritedCustomPropertyColorsSideBorderShorthands()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                [data-theme=dark] { --divider: #4a4a4a; }
                .search {
                    border-top: 1px solid;
                    border-bottom: 1px solid;
                    border-color: var(--divider);
                }
                """;
            document.head.appendChild(style);
            var themed = HostTestUtilities.GetElement(document.createElement("section"));
            themed.setAttribute("data-theme", "dark");
            HostTestUtilities.GetElement(document.body).appendChild(themed);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = "search";
            themed.appendChild(element);

            document.EnsureStylesCurrent();

            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            Assert.Equal("#4a4a4a", element.ComputedStyleValues["border-top-color"]);
            Assert.Equal("rgb(74, 74, 74)", document.getComputedStyle(element).getPropertyValue("border-top-color"));
            Assert.Equal(Color.Parse("#4a4a4a"), Assert.IsAssignableFrom<ISolidColorBrush>(panel.BorderBrush).Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void BackgroundCurrentColorTracksComputedColorInComputedNativeAndRasterOutput()
    {
        var (host, window) = CreateTargetOnlyHost(40);
        using (host)
        {
            var document = host.Document;
            window.Background = Brushes.Black;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = "html, body { margin: 0; width: 40px; height: 40px; } .swatch { display: block; width: 20px; height: 20px; background-color: currentColor; color: #285078; }";
            document.head.appendChild(style);
            var swatch = HostTestUtilities.GetElement(document.createElement("div"));
            swatch.className = "swatch";
            HostTestUtilities.GetElement(document.body).appendChild(swatch);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var panel = Assert.IsType<CssLayoutPanel>(swatch.Control);
            Assert.Equal("rgb(40, 80, 120)", document.getComputedStyle(swatch).getPropertyValue("background-color"));
            Assert.Equal(Color.Parse("#285078"), Assert.IsAssignableFrom<ISolidColorBrush>(panel.Background).Color);

            // Color is applied after background in the native presentation pass.
            // The background must nevertheless follow the newly computed color.
            swatch.SetStyleProperty("color", "#3d3d3d");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("rgb(61, 61, 61)", document.getComputedStyle(swatch).getPropertyValue("background-color"));
            Assert.Equal(Color.Parse("#3d3d3d"), Assert.IsAssignableFrom<ISolidColorBrush>(panel.Background).Color);
            window.Close();
            Dispatcher.UIThread.RunJobs();

            var raster = new CssLayoutPanel
            {
                Width = 20,
                Height = 20,
                Background = panel.Background
            };
            raster.Measure(new Size(20, 20));
            raster.Arrange(new Rect(0, 0, 20, 20));
            using var frame = new RenderTargetBitmap(new PixelSize(20, 20), new Vector(96, 96));
            frame.Render(raster);
            Assert.Equal(Color.Parse("#3d3d3d"), ReadPixel(frame, 10, 10));
        }
    }

    [AvaloniaFact]
    public void BackgroundCurrentColorShorthandParticipatesInLonghandCascadeAndPaintsComputedColor()
    {
        var (host, window) = CreateTargetOnlyHost(80);
        using (host)
        {
            var document = host.Document;
            window.Background = Brushes.Black;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                html, body { margin: 0; width: 80px; height: 40px; }
                .swatch { display: block; width: 20px; height: 20px; color: #008000; background: red; background: currentColor; }
                .later-longhand { color: green; background: currentColor; background-color: #123456; }
                .later-shorthand { color: #285078; background-color: red; background: currentColor; }
                .important-shorthand { color: green; background: currentColor !important; background-color: red; }
                """;
            document.head.appendChild(style);

            var swatch = HostTestUtilities.GetElement(document.createElement("div"));
            swatch.className = "swatch";
            var laterLonghand = HostTestUtilities.GetElement(document.createElement("div"));
            laterLonghand.className = "later-longhand";
            var laterShorthand = HostTestUtilities.GetElement(document.createElement("div"));
            laterShorthand.className = "later-shorthand";
            var importantShorthand = HostTestUtilities.GetElement(document.createElement("div"));
            importantShorthand.className = "important-shorthand";
            var body = HostTestUtilities.GetElement(document.body);
            body.appendChild(swatch);
            body.appendChild(laterLonghand);
            body.appendChild(laterShorthand);
            body.appendChild(importantShorthand);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var computed = document.getComputedStyle(swatch);
            var panel = Assert.IsType<CssLayoutPanel>(swatch.Control);
            Assert.Equal("rgb(0, 128, 0)", computed.getPropertyValue("background-color"));
            Assert.Equal(Color.Parse("#008000"), Assert.IsAssignableFrom<ISolidColorBrush>(panel.Background).Color);
            Assert.Equal("rgb(18, 52, 86)", document.getComputedStyle(laterLonghand).getPropertyValue("background-color"));
            Assert.Equal("rgb(40, 80, 120)", document.getComputedStyle(laterShorthand).getPropertyValue("background-color"));
            Assert.Equal("rgb(0, 128, 0)", document.getComputedStyle(importantShorthand).getPropertyValue("background-color"));

            var raster = new CssLayoutPanel
            {
                Width = 20,
                Height = 20,
                Background = panel.Background
            };
            raster.Measure(new Size(20, 20));
            raster.Arrange(new Rect(0, 0, 20, 20));
            using var frame = new RenderTargetBitmap(new PixelSize(20, 20), new Vector(96, 96));
            frame.Render(raster);
            Assert.Equal(Color.Parse("#008000"), ReadPixel(frame, 10, 10));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void BorderShorthandsAndLonghandsCompeteAtLonghandCascadePrecedence()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .strong-longhand { border: 2px solid white; }
                .strong-longhand.theme { border-color: #3d3d3d; }
                .strong-shorthand { border-color: white; }
                .strong-shorthand.theme { border: 3px solid #3d3d3d; }
                .later-longhand { border: 1px solid red; border-color: blue; }
                .later-shorthand { border-color: red; border: 4px solid blue; }
                .variable-shorthand { --edge: 5px solid #123456; border: var(--edge); }
                """;
            document.head.appendChild(style);

            var strongLonghand = Append("strong-longhand theme");
            var strongShorthand = Append("strong-shorthand theme");
            var laterLonghand = Append("later-longhand");
            var laterShorthand = Append("later-shorthand");
            var variableShorthand = Append("variable-shorthand");
            document.EnsureStylesCurrent();

            AssertBorder(strongLonghand, 2, "rgb(61, 61, 61)");
            AssertBorder(strongShorthand, 3, "rgb(61, 61, 61)");
            AssertBorder(laterLonghand, 1, "rgb(0, 0, 255)");
            AssertBorder(laterShorthand, 4, "rgb(0, 0, 255)");
            AssertBorder(variableShorthand, 5, "rgb(18, 52, 86)");
            window.Close();

            AvaloniaDomElement Append(string className)
            {
                var element = HostTestUtilities.GetElement(document.createElement("div"));
                element.className = className;
                HostTestUtilities.GetElement(document.body).appendChild(element);
                return element;
            }

            void AssertBorder(AvaloniaDomElement element, double width, string color)
            {
                var computed = document.getComputedStyle(element);
                Assert.Equal($"{width}px", computed.getPropertyValue("border-top-width"));
                Assert.Equal(color, computed.getPropertyValue("border-top-color"));
                var panel = Assert.IsType<CssLayoutPanel>(element.Control);
                Assert.Equal(new Thickness(width), panel.BorderThickness);
                Assert.Equal(Color.Parse(color), Assert.IsAssignableFrom<ISolidColorBrush>(panel.BorderBrush).Color);
            }
        }
    }

    [AvaloniaFact]
    public void DarkThemeLonghandBorderColorOverridesLegendActionShorthand()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                :root {
                    --color-cold-gray-200: #dbdbdb;
                    --color-cold-gray-750: #3d3d3d;
                }
                .withAction .buttons {
                    border: 1px solid var(--color-cold-gray-200);
                    border-left: 0;
                }
                .chart-widget__top--themed-dark .withAction .buttons {
                    border-color: var(--color-cold-gray-750);
                }
                """;
            document.head.appendChild(style);
            var themed = HostTestUtilities.GetElement(document.createElement("section"));
            themed.className = "chart-widget__top--themed-dark";
            var row = HostTestUtilities.GetElement(document.createElement("div"));
            row.className = "withAction";
            var buttons = HostTestUtilities.GetElement(document.createElement("div"));
            buttons.className = "buttons";
            row.appendChild(buttons);
            themed.appendChild(row);
            HostTestUtilities.GetElement(document.body).appendChild(themed);

            document.EnsureStylesCurrent();

            var computed = document.getComputedStyle(buttons);
            var panel = Assert.IsType<CssLayoutPanel>(buttons.Control);
            Assert.Equal("0", computed.getPropertyValue("border-left-width"));
            Assert.Equal("rgb(61, 61, 61)", computed.getPropertyValue("border-right-color"));
            Assert.Equal(new Thickness(0, 1, 1, 1), panel.BorderThickness);
            Assert.Equal(
                Color.Parse("#3d3d3d"),
                Assert.IsAssignableFrom<ISolidColorBrush>(panel.BorderBrush).Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void TransparentLeftBorderRetainsInlineLegendSpacingWithoutPaintingAWhiteStem()
    {
        var (host, window) = CreateTargetOnlyHost(40);
        using (host)
        {
            var document = host.Document;
            window.Background = Brushes.Black;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                html, body { margin: 0; width: 40px; height: 40px; background: #0f0f0f; }
                .legend { color: #dbdbdb; }
                .valuesAdditionalWrapper {
                    background: #0f0f0f;
                    border-left: 4px solid;
                    height: 20px;
                    width: 20px;
                }
                .noWrap .valuesAdditionalWrapper { border-left-color: transparent; }
                """;
            document.head.appendChild(style);
            var legend = HostTestUtilities.GetElement(document.createElement("div"));
            legend.className = "legend noWrap";
            var values = HostTestUtilities.GetElement(document.createElement("div"));
            values.className = "valuesAdditionalWrapper";
            legend.appendChild(values);
            HostTestUtilities.GetElement(document.body).appendChild(legend);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var computed = document.getComputedStyle(values);
            var panel = Assert.IsType<CssLayoutPanel>(values.Control);
            Assert.Equal("4px", computed.getPropertyValue("border-left-width"));
            Assert.Equal("rgba(0, 0, 0, 0)", computed.getPropertyValue("border-left-color"));
            Assert.Equal(4, panel.BorderThickness.Left);
            Assert.Equal(0, Assert.IsAssignableFrom<ISolidColorBrush>(panel.BorderLeftBrush).Color.A);

            panel.Measure(new Size(24, 20));
            panel.Arrange(new Rect(0, 0, 24, 20));
            using var frame = new RenderTargetBitmap(new PixelSize(24, 20), new Vector(96, 96));
            frame.Render(panel);
            Assert.Equal(Color.Parse("#0f0f0f"), ReadPixel(frame, 1, 10));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void SemanticHeadingContributesTransformedTrackedLineBoxToFlexHeader()
    {
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .header {
                    box-sizing: border-box;
                    display: flex;
                    padding: 16px 84px 8px 32px;
                    width: 380px;
                }
                .heading {
                    font-size: 11px;
                    font-weight: 400;
                    letter-spacing: .4px;
                    line-height: 16px;
                    margin: 0;
                    text-transform: uppercase;
                }
                """;
            document.head.appendChild(style);
            var header = HostTestUtilities.GetElement(document.createElement("div"));
            header.className = "header";
            var heading = HostTestUtilities.GetElement(document.createElement("h3"));
            heading.className = "heading";
            heading.textContent = "Script name";
            header.appendChild(heading);
            HostTestUtilities.GetElement(document.body).appendChild(header);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var headerRect = header.getBoundingClientRect();
            var headingRect = heading.getBoundingClientRect();
            var textNode = Assert.IsType<AvaloniaDomTextNode>(heading.firstChild);
            var textBlock = Assert.IsAssignableFrom<TextBlock>(textNode.Control);
            Assert.IsType<CssLayoutPanel>(heading.Control);
            Assert.Equal(40, headerRect.height);
            Assert.Equal((32d, 16d, 16d),
                (headingRect.left - headerRect.left, headingRect.top - headerRect.top, headingRect.height));
            Assert.Equal("Script name", textNode.data);
            Assert.Equal("SCRIPT NAME", textBlock.Text);
            Assert.Equal(0.4, textBlock.LetterSpacing);
            Assert.Equal(16, textBlock.LineHeight);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void DynamicallyAssignedHeadingTextUsesCssLineHeightWithoutNativeClipping()
    {
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .header {
                    box-sizing: border-box;
                    display: flex;
                    align-items: center;
                    height: 64px;
                    padding: 0 32px;
                    width: 380px;
                }
                .heading {
                    font-size: 24px;
                    font-weight: 600;
                    line-height: 28px;
                    margin: 0;
                }
                """;
            document.head.appendChild(style);
            var header = HostTestUtilities.GetElement(document.createElement("div"));
            header.className = "header";
            var heading = HostTestUtilities.GetElement(document.createElement("h2"));
            heading.className = "heading";
            header.appendChild(heading);
            HostTestUtilities.GetElement(document.body).appendChild(header);

            heading.textContent = "Indicators";
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var headingRect = heading.getBoundingClientRect();
            var textNode = Assert.IsType<AvaloniaDomTextNode>(heading.firstChild);
            var textBlock = Assert.IsAssignableFrom<TextBlock>(textNode.Control);
            Assert.Equal(28, headingRect.height);
            Assert.Equal(28, textBlock.Bounds.Height);
            Assert.True(double.IsNaN(textBlock.Height));
            Assert.Equal(24, textBlock.FontSize);
            Assert.Equal(28, textBlock.LineHeight);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ZeroCssLineHeightKeepsAutomaticGlyphMetricsWithoutNativeClipping()
    {
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".label { font-size: 24px; line-height: 0; }";
            document.head.appendChild(style);
            var label = HostTestUtilities.GetElement(document.createElement("div"));
            label.className = "label";
            label.textContent = "Indicators";
            HostTestUtilities.GetElement(document.body).appendChild(label);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var textNode = Assert.IsType<AvaloniaDomTextNode>(label.firstChild);
            var textBlock = Assert.IsAssignableFrom<TextBlock>(textNode.Control);
            Assert.True(double.IsNaN(textBlock.Height));
            Assert.True(double.IsNaN(textBlock.LineHeight));
            Assert.True(textBlock.IsVisible);
            Assert.Equal(24, textBlock.FontSize);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void FontShorthandResolvesAbsoluteUnitsAndUnitlessChildLineHeight()
    {
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                #outer { font: 1in/1em serif; margin-top: .5in; }
                #outer .inner { line-height: 0; }
                """;
            document.head.appendChild(style);

            var outer = HostTestUtilities.GetElement(document.createElement("div"));
            outer.id = "outer";
            var inner = HostTestUtilities.GetElement(document.createElement("div"));
            inner.className = "inner";
            inner.textContent = "X";
            outer.appendChild(inner);
            HostTestUtilities.GetElement(document.body).appendChild(outer);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var outerStyle = document.getComputedStyle(outer);
            var innerStyle = document.getComputedStyle(inner);
            var textNode = Assert.IsType<AvaloniaDomTextNode>(inner.firstChild);
            var textBlock = Assert.IsAssignableFrom<TextBlock>(textNode.Control);
            Assert.Equal("1in", outerStyle.getPropertyValue("font-size"));
            Assert.Equal("96px", outerStyle.getPropertyValue("line-height"));
            Assert.Equal("1in", innerStyle.getPropertyValue("font-size"));
            Assert.Equal("0px", innerStyle.getPropertyValue("line-height"));
            Assert.Equal(new CssLength(48, CssLengthUnit.Pixel), CssLayout.GetMarginTop(outer.Control));
            Assert.Equal(96, textBlock.FontSize);
            Assert.True(double.IsNaN(textBlock.LineHeight));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CssInitialFontsAndNumericWeightsMatchBrowserComputedValues()
    {
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var unstyled = HostTestUtilities.GetElement(document.createElement("div"));
            unstyled.textContent = "Initial";
            var weighted = HostTestUtilities.GetElement(document.createElement("span"));
            weighted.style.cssText = "font-weight: 600";
            weighted.textContent = "Weighted";
            var bold = HostTestUtilities.GetElement(document.createElement("span"));
            bold.style.cssText = "font-weight: bold";
            bold.textContent = "Bold";
            HostTestUtilities.GetElement(document.body).appendChild(unstyled);
            HostTestUtilities.GetElement(document.body).appendChild(weighted);
            HostTestUtilities.GetElement(document.body).appendChild(bold);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var initialStyle = document.getComputedStyle(unstyled);
            Assert.Equal("sans-serif", initialStyle.getPropertyValue("font-family"));
            Assert.Equal("16px", initialStyle.getPropertyValue("font-size"));
            Assert.Equal("400", initialStyle.getPropertyValue("font-weight"));
            Assert.Equal("normal", initialStyle.getPropertyValue("font-style"));
            Assert.Equal("normal", initialStyle.getPropertyValue("line-height"));

            var weightedStyle = document.getComputedStyle(weighted);
            Assert.Equal("600", weightedStyle.getPropertyValue("font-weight"));
            Assert.Equal("700", document.getComputedStyle(bold).getPropertyValue("font-weight"));
            var textNode = Assert.IsType<AvaloniaDomTextNode>(weighted.firstChild);
            var textBlock = Assert.IsType<DomTextBlockControl>(textNode.Control);
            Assert.Equal(600, (int)textBlock.FontWeight);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void DomSystemFontMetricsMatchCanvasAndRetainFiniteWidthWrapping()
    {
        const string familyStack = "-apple-system, BlinkMacSystemFont, 'Trebuchet MS', Roboto, Ubuntu, sans-serif";
        var (host, window) = CreateTargetOnlyHost(500);
        using (host)
        {
            var document = host.Document;
            var label = HostTestUtilities.GetElement(document.createElement("span"));
            label.style.cssText = $"font: 12px {familyStack}; white-space: nowrap";
            label.textContent = "100.00";
            HostTestUtilities.GetElement(document.body).appendChild(label);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var textNode = Assert.IsType<AvaloniaDomTextNode>(label.firstChild);
            var textBlock = Assert.IsType<DomTextBlockControl>(textNode.Control);
            textBlock.Measure(Size.Infinity);

            var surface = new CanvasDrawingSurface();
            surface.Context.font = $"12px {familyStack}";
            var canvasWidth = Assert.IsType<CanvasTextMetrics>(surface.Context.measureText("100.00")).width;
            Assert.True(
                Math.Abs(textBlock.DesiredSize.Width - canvasWidth) <= 0.25,
                $"Expected DOM {textBlock.DesiredSize.Width:F4}px to match Canvas {canvasWidth:F4}px.");
            if (OperatingSystem.IsMacOS())
            {
                Assert.InRange(textBlock.DesiredSize.Width, 38.0, 38.75);
            }

            var resolution = CssFontResolver.Resolve(familyStack);
            var wrapped = new DomTextBlockControl
            {
                Text = "alpha beta gamma delta",
                FontFamily = resolution.Family,
                FontSize = 12,
                FontWidthScale = resolution.ResolveWidthScale(12, FontWeight.Normal),
                TextWrapping = TextWrapping.Wrap
            };
            wrapped.Measure(new Size(60, double.PositiveInfinity));
            wrapped.Arrange(new Rect(0, 0, 60, wrapped.DesiredSize.Height));
            Assert.InRange(wrapped.DesiredSize.Width, 0, 60);
            Assert.True(wrapped.TextLayout.TextLines.Count > 1);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ExitingHoveredAncestorClearsDescendantHoverTarget()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".row:hover { background-color: #363636; }";
            document.head.appendChild(style);
            var row = HostTestUtilities.GetElement(document.createElement("div"));
            row.className = "row";
            var child = HostTestUtilities.GetElement(document.createElement("span"));
            child.textContent = "Aroon";
            row.appendChild(child);
            HostTestUtilities.GetElement(document.body).appendChild(row);
            document.EnsureStylesCurrent();

            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            var args = new PointerEventArgs(
                InputElement.PointerEnteredEvent,
                child.Control,
                pointer,
                window,
                new Point(10, 10),
                0,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);
            document.UpdatePointerHover(child, args);
            document.EnsureStylesCurrent();
            Assert.Equal("rgb(54, 54, 54)", document.getComputedStyle(row).getPropertyValue("background-color"));

            document.ClearPointerHover(row, args);
            Assert.False(document.IsPointerHovered(row));
            document.EnsureStylesCurrent();

            Assert.Equal("rgba(255, 255, 255, 0)", document.getComputedStyle(row).getPropertyValue("background-color"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void NestedSvgHoverRefreshesRoundedCustomPropertyPseudoBackground()
    {
        const string svgNamespace = "http://www.w3.org/2000/svg";
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .tool {
                    --hover-radius: 6px;
                    position: relative;
                    width: 52px;
                    height: 38px;
                }
                .tool::before {
                    content: none;
                    position: absolute;
                    left: 9px;
                    right: 9px;
                    top: 2px;
                    bottom: 2px;
                    border-radius: var(--hover-radius);
                    background-color: transparent;
                }
                .tool:hover::before {
                    content: "";
                    background-color: #2e2e2e;
                }
                """;
            document.head.appendChild(style);
            var button = HostTestUtilities.GetElement(document.createElement("button"));
            button.className = "tool";
            var svg = HostTestUtilities.GetElement(document.createElementNS(svgNamespace, "svg"));
            svg.setAttribute("viewBox", "0 0 18 18");
            button.appendChild(svg);
            HostTestUtilities.GetElement(document.body).appendChild(button);
            document.EnsureStylesCurrent();

            var panel = Assert.IsType<DomButtonControl>(button.Control);
            Assert.Equal("6px", document.getComputedStyle(button).getPropertyValue("--hover-radius"));
            Assert.Null(panel.BeforePseudoElement);
            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            var args = new PointerEventArgs(
                InputElement.PointerEnteredEvent,
                svg.Control,
                pointer,
                window,
                new Point(26, 19),
                0,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);
            document.UpdatePointerHover(svg, args);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var hovered = Assert.IsType<CssGeneratedPseudoElement>(panel.BeforePseudoElement);
            Assert.Equal(6, hovered.ResolveCornerRadius(34));
            Assert.Equal(Color.Parse("#2e2e2e"), Assert.IsAssignableFrom<ISolidColorBrush>(hovered.Background).Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void AdjacentToolbarGroupsCreateFlowBlockSeparatorFromAuthoredPseudoRule()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .group + .group::before {
                    background-color: #363a45;
                    content: "";
                    display: block;
                    height: 1px;
                    margin: 0 8px 6px;
                }
                """;
            document.head.appendChild(style);
            var first = HostTestUtilities.GetElement(document.createElement("div"));
            var second = HostTestUtilities.GetElement(document.createElement("div"));
            first.className = "group";
            second.className = "group";
            HostTestUtilities.GetElement(document.body).appendChild(first);
            HostTestUtilities.GetElement(document.body).appendChild(second);

            document.EnsureStylesCurrent();

            var panel = Assert.IsType<CssLayoutPanel>(second.Control);
            var separator = Assert.IsType<CssGeneratedPseudoElement>(panel.BeforePseudoElement);
            Assert.True(separator.IsFlowBlock);
            Assert.Equal(7, separator.ResolveFlowOuterHeight(100));
            Assert.Equal(new Rect(8, 0, 84, 1),
                separator.ResolveFlowRect(new Rect(0, 0, 100, 20), before: true));
            Assert.Equal(
                Color.Parse("#363a45"),
                Assert.IsAssignableFrom<ISolidColorBrush>(separator.Background).Color);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void SideBorderShorthandsRenderOnCssLayoutPanels()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".search { border-top: 1px solid; border-bottom: 1px solid; border-color: #55606a; }";
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = "search";
            HostTestUtilities.GetElement(document.body).appendChild(element);

            document.EnsureStylesCurrent();

            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            Assert.Equal(new Thickness(0, 1, 0, 1), panel.BorderThickness);
            Assert.Equal(Color.Parse("#55606a"), Assert.IsAssignableFrom<ISolidColorBrush>(panel.BorderBrush).Color);
            Assert.Contains(panel.Children, child => child is DomBorderOverlayControl);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StylesheetBorderRadiusFlowsToRoundedOverflowClipOnCssLayoutPanel()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .marker {
                    width: 18px;
                    height: 18px;
                    overflow: hidden;
                    border-radius: 9px;
                }
                """;
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("button"));
            element.className = "marker";
            HostTestUtilities.GetElement(document.body).appendChild(element);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var panel = Assert.IsType<DomButtonControl>(element.Control);
            Assert.Equal(new CornerRadius(9), panel.CornerRadius);
            Assert.True(panel.ClipToBounds);
            Assert.IsType<StreamGeometry>(panel.Clip);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void PercentageBorderRadiusResolvesAgainstTheElementsOwnWidth()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".radio { width: 18px; height: 18px; background: white; border: 1px solid white; border-radius: 50%; }";
            document.head.appendChild(style);
            var radio = HostTestUtilities.GetElement(document.createElement("span"));
            radio.className = "radio";
            HostTestUtilities.GetElement(document.body).appendChild(radio);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var panel = Assert.IsType<CssLayoutPanel>(radio.Control);
            Assert.Equal(new CornerRadius(9), panel.CornerRadius);
            panel.Measure(new Size(18, 18));
            panel.Arrange(new Rect(0, 0, 18, 18));
            var background = Assert.IsType<DrawingBrush>(panel.Background);
            var drawing = Assert.IsType<DrawingGroup>(background.Drawing);
            Assert.IsType<StreamGeometry>(Assert.IsType<GeometryDrawing>(drawing.Children[0]).Geometry);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CalculatedBorderRadiusRoundsNativeButtonPaintAndPreservesPortableGeometry()
    {
        var (host, window) = CreateTargetOnlyHost(40);
        using (host)
        {
            var document = host.Document;
            window.Background = Brushes.Black;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                html, body { margin: 0; width: 40px; height: 40px; background: #000; }
                .surface { width: 40px; height: 40px; background: #000; }
                .action {
                    --action-radius: calc(
                        max(0, 1 - (3 - 3) * (3 - 3)) * 6px +
                        max(0, 1 - (3 - 4) * (3 - 4)) * 8px);
                    width: 34px;
                    height: 34px;
                    box-sizing: border-box;
                    background: white;
                    border: 1px solid white;
                    border-radius: var(--action-radius);
                }
                """;
            document.head.appendChild(style);
            var surface = HostTestUtilities.GetElement(document.createElement("div"));
            surface.className = "surface";
            var button = HostTestUtilities.GetElement(document.createElement("button"));
            button.className = "action";
            surface.appendChild(button);
            HostTestUtilities.GetElement(document.body).appendChild(surface);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var panel = Assert.IsType<DomButtonControl>(button.Control);
            Assert.True(
                panel.CornerRadius == new CornerRadius(6),
                $"Expected a 6px used radius; computed='" +
                document.getComputedStyle(button).getPropertyValue("border-top-left-radius") +
                $"', native={panel.CornerRadius}.");
            var native = button.getBoundingClientRect();
            Assert.Equal((0d, 0d, 34d, 34d), (native.x, native.y, native.width, native.height));
            var portable = AvaloniaCssLayoutProjection.Capture(panel, new Size(34, 34));
            var portableBorder = portable.GetBox(panel).BorderBox;
            Assert.Equal(
                (0d, 0d, 34d, 34d),
                (portableBorder.X, portableBorder.Y, portableBorder.Width, portableBorder.Height));

            // Render this fixture's containing surface directly. Headless window
            // captures share a compositor across tests and can return a prior
            // top-level frame when the class runs as a suite; RenderTargetBitmap
            // still exercises the native Avalonia paint tree without that global
            // top-level cache. A square button would make (0,0) white, so this is
            // intentionally a paint assertion rather than a geometry surrogate.
            var surfacePanel = Assert.IsType<CssLayoutPanel>(surface.Control);
            surfacePanel.Measure(new Size(40, 40));
            surfacePanel.Arrange(new Rect(0, 0, 40, 40));
            using var frame = new RenderTargetBitmap(new PixelSize(40, 40), new Vector(96, 96));
            frame.Render(surfacePanel);
            Assert.Equal(Colors.Black, ReadPixel(frame, 0, 0));
            Assert.Equal(Colors.White, ReadPixel(frame, 17, 17));
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void VariableBorderRadiusShorthandAndCornerLonghandHonorCascadeOrderAndImportance()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .shorthand-wins {
                    --radius: 6px;
                    border-top-left-radius: 11px;
                    border-radius: var(--radius);
                }
                .longhand-wins {
                    --radius: 6px;
                    border-radius: var(--radius);
                    border-top-left-radius: 11px;
                }
                .important-longhand-wins {
                    --radius: 6px;
                    border-top-left-radius: 11px !important;
                    border-radius: var(--radius);
                }
                """;
            document.head.appendChild(style);
            var shorthandWins = HostTestUtilities.GetElement(document.createElement("button"));
            shorthandWins.className = "shorthand-wins";
            var longhandWins = HostTestUtilities.GetElement(document.createElement("button"));
            longhandWins.className = "longhand-wins";
            var importantWins = HostTestUtilities.GetElement(document.createElement("button"));
            importantWins.className = "important-longhand-wins";
            var body = HostTestUtilities.GetElement(document.body);
            body.appendChild(shorthandWins);
            body.appendChild(longhandWins);
            body.appendChild(importantWins);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(new CornerRadius(6), Assert.IsType<DomButtonControl>(shorthandWins.Control).CornerRadius);
            Assert.Equal(
                new CornerRadius(11, 6, 6, 6),
                Assert.IsType<DomButtonControl>(longhandWins.Control).CornerRadius);
            Assert.Equal(
                new CornerRadius(11, 6, 6, 6),
                Assert.IsType<DomButtonControl>(importantWins.Control).CornerRadius);
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void ScaleXAndTransformOriginProjectToTheNativeRenderTransform()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var element = HostTestUtilities.GetElement(host.Document.createElement("div"));
            element.style.cssText = "width: 100px; height: 4px; transform: translateX(12px) scaleX(.44); transform-origin: left center";
            HostTestUtilities.GetElement(host.Document.body).appendChild(element);

            host.Document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var group = Assert.IsType<TransformGroup>(element.Control.RenderTransform);
            Assert.Equal(.44, Assert.IsType<ScaleTransform>(group.Children[0]).ScaleX, 6);
            Assert.Equal(12, Assert.IsType<TranslateTransform>(group.Children[1]).X);
            Assert.Equal(
                "matrix(0.44, 0, 0, 1, 12, 0)",
                host.Document.getComputedStyle(element).getPropertyValue("transform"));
            Assert.Equal(new RelativePoint(0, .5, RelativeUnit.Relative), element.Control.RenderTransformOrigin);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ElementFromPointHonorsPositionedZIndexAcrossNonStackingAncestors()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .study { display: flex; width: 300px; height: 24px; pointer-events: none; }
                .no-wrap { display: flex; width: 53px; height: 24px; pointer-events: none; }
                .label { width: 53px; height: 24px; flex-shrink: 0; }
                .buttons-wrapper { display: flex; position: relative; width: 0; height: 24px; z-index: 2; pointer-events: none; }
                .buttons { display: flex; width: 84px; height: 24px; flex-shrink: 0; pointer-events: auto; }
                .button { width: 28px; height: 22px; flex-shrink: 0; pointer-events: auto; }
                .values { position: relative; width: 100px; height: 24px; pointer-events: auto; }
                """;
            document.head.appendChild(style);
            var study = HostTestUtilities.GetElement(document.createElement("div"));
            study.className = "study";
            var noWrap = HostTestUtilities.GetElement(document.createElement("div"));
            noWrap.className = "no-wrap";
            var label = HostTestUtilities.GetElement(document.createElement("div"));
            label.className = "label";
            var buttonsWrapper = HostTestUtilities.GetElement(document.createElement("div"));
            buttonsWrapper.className = "buttons-wrapper";
            var buttons = HostTestUtilities.GetElement(document.createElement("div"));
            buttons.className = "buttons";
            AvaloniaDomElement? target = null;
            for (var index = 0; index < 3; index++)
            {
                var button = HostTestUtilities.GetElement(document.createElement("button"));
                button.className = "button";
                buttons.appendChild(button);
                if (index == 1) target = button;
            }
            buttonsWrapper.appendChild(buttons);
            noWrap.appendChild(label);
            noWrap.appendChild(buttonsWrapper);
            var values = HostTestUtilities.GetElement(document.createElement("div"));
            values.className = "values";
            study.appendChild(noWrap);
            study.appendChild(values);
            HostTestUtilities.GetElement(document.body).appendChild(study);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var rect = target!.getBoundingClientRect();
            Assert.Equal(2, noWrap.Control.GetValue(Canvas.ZIndexProperty));
            Assert.Equal(0, values.Control.GetValue(Canvas.ZIndexProperty));
            Assert.Same(target, document.elementFromPoint(rect.left + rect.width / 2, rect.top + rect.height / 2));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void PercentageTranslateResolvesAgainstTheElementsOwnBorderBox()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .icon {
                    width: 18px;
                    height: 18px;
                    transform: translate(-50%, -50%);
                }
                """;
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = "icon";
            HostTestUtilities.GetElement(document.body).appendChild(element);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var transform = Assert.IsType<TranslateTransform>(element.Control.RenderTransform);
            Assert.Equal(-9, transform.X);
            Assert.Equal(-9, transform.Y);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CssRotateUsesCenterOriginAndIframeTitleDoesNotCreateNativeTooltip()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var icon = HostTestUtilities.GetElement(document.createElement("svg"));
            icon.style.cssText = "transform: rotate(180deg)";
            body.appendChild(icon);
            var iframe = HostTestUtilities.GetElement(document.createElement("iframe"));
            iframe.setAttribute("title", "Financial Chart");
            body.appendChild(iframe);
            var ordinary = HostTestUtilities.GetElement(document.createElement("div"));
            ordinary.setAttribute("title", "Ordinary tooltip");
            body.appendChild(ordinary);

            document.EnsureStylesCurrent();

            Assert.Equal(180, Assert.IsType<RotateTransform>(icon.Control.RenderTransform).Angle);
            Assert.Equal(RelativePoint.Center, icon.Control.RenderTransformOrigin);
            Assert.Equal("Financial Chart", iframe.getAttribute("title"));
            Assert.Null(ToolTip.GetTip(iframe.Control));
            Assert.Equal("Ordinary tooltip", ToolTip.GetTip(ordinary.Control));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void AncestorClassChangeRecascadesDescendantTransform()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .toggler .icon { transform: rotate(-180deg); }
                .closed .toggler .icon { transform: rotate(0); }
                """;
            document.head.appendChild(style);

            var wrapper = HostTestUtilities.GetElement(document.createElement("div"));
            var toggler = HostTestUtilities.GetElement(document.createElement("button"));
            toggler.className = "toggler";
            var icon = HostTestUtilities.GetElement(document.createElement("span"));
            icon.className = "icon";
            toggler.appendChild(icon);
            wrapper.appendChild(toggler);
            HostTestUtilities.GetElement(document.body).appendChild(wrapper);

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("matrix(-1, 0, 0, -1, 0, 0)", document.getComputedStyle(icon).getPropertyValue("transform"));
            Assert.Equal(-180, Assert.IsType<RotateTransform>(icon.Control.RenderTransform).Angle);

            wrapper.classList.add("closed");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal("matrix(1, 0, 0, 1, 0, 0)", document.getComputedStyle(icon).getPropertyValue("transform"));
            Assert.Equal(0, Assert.IsType<RotateTransform>(icon.Control.RenderTransform).Angle);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void UnthemedFocusedInputShowsBlinkingFallbackCaretWhileEmpty()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var inputElement = HostTestUtilities.GetElement(host.Document.createElement("input"));
            inputElement.placeholder = "Search";
            HostTestUtilities.GetElement(host.Document.body).appendChild(inputElement);
            host.Document.EnsureStylesCurrent();
            inputElement.focus();
            Dispatcher.UIThread.RunJobs();

            var input = Assert.IsType<DomTextInputControl>(inputElement.Control);
            Assert.True(input.IsFocused);
            Assert.True(input.FallbackCaretVisible);
            input.AdvanceFallbackCaretForTest();
            Assert.False(input.FallbackCaretVisible);
            input.AdvanceFallbackCaretForTest();
            Assert.True(input.FallbackCaretVisible);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void UnrelatedChildPositionRuleDoesNotRecascadeRemainingRowsOnRemoval()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".unrelated:first-child { color: red; }";
            document.head.appendChild(style);
            var list = HostTestUtilities.GetElement(document.createElement("div"));
            HostTestUtilities.GetElement(document.body).appendChild(list);
            var rows = Enumerable.Range(0, 80).Select(_ =>
            {
                var row = HostTestUtilities.GetElement(document.createElement("div"));
                row.className = "result-row";
                list.appendChild(row);
                return row;
            }).ToArray();
            document.EnsureStylesCurrent();
            var computedBefore = document.ElementStyleComputeCount;

            list.removeChild(rows[20]);
            document.EnsureStylesCurrent();

            Assert.Equal(computedBefore, document.ElementStyleComputeCount);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void BatchedListFilteringRecascadesRemainingRowsOnce()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .result-row:first-child { color: red; }
                .result-row:last-child { background-color: blue; }
                """;
            document.head.appendChild(style);
            var list = HostTestUtilities.GetElement(document.createElement("div"));
            HostTestUtilities.GetElement(document.body).appendChild(list);
            var rows = Enumerable.Range(0, 100).Select(_ =>
            {
                var row = HostTestUtilities.GetElement(document.createElement("div"));
                row.className = "result-row";
                list.appendChild(row);
                return row;
            }).ToArray();
            document.EnsureStylesCurrent();
            var computedBefore = document.ElementStyleComputeCount;
            var selectorMatchesBefore = document.SelectorMatchEvaluationCount;

            foreach (var row in rows.Skip(5).Take(90))
            {
                list.removeChild(row);
            }
            document.EnsureStylesCurrent();

            Assert.Equal("rgb(255, 0, 0)", document.getComputedStyle(rows[0]).getPropertyValue("color"));
            Assert.Equal("rgb(0, 0, 255)", document.getComputedStyle(rows[99]).getPropertyValue("background-color"));
            Assert.InRange(document.ElementStyleComputeCount - computedBefore, 1, 12);
            Assert.InRange(document.SelectorMatchEvaluationCount - selectorMatchesBefore, 1, 400);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ConnectedBranchAssemblyUsesPendingAncestorCascadeAndPreservesIdIndex()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".highlight { color: red; }";
            document.head.appendChild(style);
            var list = HostTestUtilities.GetElement(document.createElement("div"));
            HostTestUtilities.GetElement(document.body).appendChild(list);
            document.EnsureStylesCurrent();
            Assert.Null(document.getElementById("late-highlight"));

            var row = HostTestUtilities.GetElement(document.createElement("div"));
            list.appendChild(row);
            var title = HostTestUtilities.GetElement(document.createElement("span"));
            title.className = "highlight";
            title.id = "late-highlight";
            row.appendChild(title);
            title.appendChild(Assert.IsAssignableFrom<AvaloniaDomElement>(document.createTextNode("Volume")));
            document.EnsureStylesCurrent();

            Assert.Same(title, document.getElementById("late-highlight"));
            Assert.Equal("rgb(255, 0, 0)", document.getComputedStyle(title).getPropertyValue("color"));
            Assert.Equal("Volume", title.textContent);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CssTextBatchesInlinePresentationAndPreservesDeclarationParsing()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();

            var presentationBefore = document.InlinePresentationApplyCount;
            element.style.cssText =
                " position : absolute ; left: 25px; top:30px; opacity: .5; --payload: alpha:beta; invalid ";

            Assert.Equal(1, document.InlinePresentationApplyCount - presentationBefore);
            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            Assert.Equal(CssPosition.Absolute, CssLayout.GetPosition(panel));
            Assert.Equal(new CssLength(25, CssLengthUnit.Pixel), CssLayout.GetLeft(panel));
            Assert.Equal(new CssLength(30, CssLengthUnit.Pixel), CssLayout.GetTop(panel));

            document.EnsureStylesCurrent();
            var computed = document.getComputedStyle(element);
            Assert.Equal("alpha:beta", computed.getPropertyValue("--payload"));
            Assert.Equal(".5", computed.getPropertyValue("opacity"));
            Assert.Equal(0.5, panel.Opacity);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CustomPropertyOnlyMutationRecascadesConsumersAndRebasesPassiveDescendants()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".consumer { left: var(--alias); position: absolute; }";
            document.head.appendChild(style);

            var root = HostTestUtilities.GetElement(document.createElement("section"));
            root.className = "root";
            root.style.cssText = "--x: 10px; --alias: var(--x)";
            body.appendChild(root);

            var consumer = HostTestUtilities.GetElement(document.createElement("div"));
            consumer.className = "consumer";
            root.appendChild(consumer);

            AvaloniaDomElement? passive = null;
            for (var index = 0; index < 10; index++)
            {
                var child = HostTestUtilities.GetElement(document.createElement("div"));
                root.appendChild(child);
                passive = child;
            }

            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();

            // The first target-only pass populates the matched-rule cache used
            // to distinguish consumers from passive descendants.
            root.SetStyleProperty("--x", "20px");
            document.EnsureStylesCurrent();

            var computedBefore = document.ElementStyleComputeCount;
            root.SetStyleProperty("--x", "30px");
            document.EnsureStylesCurrent();

            Assert.Equal(2, document.ElementStyleComputeCount - computedBefore);
            Assert.Equal("30px", document.getComputedStyle(consumer).getPropertyValue("left"));
            Assert.Equal("30px", document.getComputedStyle(passive!).getPropertyValue("--x"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CustomPropertyMutationStillPropagatesChangedInheritedOrdinaryValues()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".root { color: var(--theme-color); }";
            document.head.appendChild(style);

            var root = HostTestUtilities.GetElement(document.createElement("section"));
            root.className = "root";
            root.style.cssText = "--theme-color: red";
            body.appendChild(root);
            var child = HostTestUtilities.GetElement(document.createElement("div"));
            root.appendChild(child);
            var grandchild = HostTestUtilities.GetElement(document.createElement("span"));
            child.appendChild(grandchild);

            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();
            root.SetStyleProperty("--theme-color", "green");
            document.EnsureStylesCurrent();
            root.SetStyleProperty("--theme-color", "blue");
            document.EnsureStylesCurrent();

            Assert.Equal("rgb(0, 0, 255)", document.getComputedStyle(child).getPropertyValue("color"));
            Assert.Equal("rgb(0, 0, 255)", document.getComputedStyle(grandchild).getPropertyValue("color"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void LayoutOnlyChangeDoesNotReapplyUnchangedComputedPresentation()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".colored { background-color: #123456; position: absolute; }";
            document.head.appendChild(style);

            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = "colored";
            body.appendChild(element);
            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();

            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            var background = Assert.IsType<SolidColorBrush>(panel.Background);
            Assert.Equal(Color.Parse("#123456"), background.Color);

            element.SetStyleProperty("left", "25px");
            document.EnsureStylesCurrent();

            Assert.Same(background, panel.Background);
            Assert.Equal(new CssLength(25, CssLengthUnit.Pixel), CssLayout.GetLeft(panel));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void NativePresentationPropertyResolutionRemainsTypeSpecificAcrossCachedHitsAndMisses()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .cached-native {
                    background-color: #112233;
                    color: #445566;
                    font-size: 13px;
                    padding: 2px 3px;
                }
                """;
            document.head.appendChild(style);

            // CssLayoutPanel owns Background but not the text-control
            // properties. These negative lookups must not be reused for a
            // different Avalonia control type.
            var panelElement = HostTestUtilities.GetElement(document.createElement("div"));
            panelElement.className = "cached-native";
            body.appendChild(panelElement);

            var firstInput = HostTestUtilities.GetElement(document.createElement("input"));
            firstInput.className = "cached-native";
            body.appendChild(firstInput);
            var secondInput = HostTestUtilities.GetElement(document.createElement("input"));
            secondInput.className = "cached-native";
            body.appendChild(secondInput);

            document.EnsureStylesCurrent();

            var panel = Assert.IsType<CssLayoutPanel>(panelElement.Control);
            Assert.Equal(
                Color.Parse("#112233"),
                Assert.IsAssignableFrom<ISolidColorBrush>(panel.Background).Color);
            AssertNativeInputPresentation(firstInput.Control);
            AssertNativeInputPresentation(secondInput.Control);

            host.ArmTargetOnlyInlineStyles();
            secondInput.SetStyleProperty("color", "green");
            document.EnsureStylesCurrent();

            var changedInput = Assert.IsType<DomTextInputControl>(secondInput.Control);
            Assert.Equal(
                Colors.Green,
                Assert.IsAssignableFrom<ISolidColorBrush>(changedInput.Foreground).Color);
            window.Close();
        }

        static void AssertNativeInputPresentation(Control control)
        {
            var input = Assert.IsType<DomTextInputControl>(control);
            Assert.Equal(
                Color.Parse("#112233"),
                Assert.IsAssignableFrom<ISolidColorBrush>(input.Background).Color);
            Assert.Equal(
                Color.Parse("#445566"),
                Assert.IsAssignableFrom<ISolidColorBrush>(input.Foreground).Color);
            Assert.Equal(13, input.FontSize);
            Assert.Equal(new Thickness(3, 2), input.Padding);
        }
    }

    [AvaloniaFact]
    public void IdenticalOrdinaryStylesShareFrozenStorageAcrossElements()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".shared { color: red; padding: 2px 4px; }";
            document.head.appendChild(style);

            var first = HostTestUtilities.GetElement(document.createElement("div"));
            first.className = "shared";
            first.style.cssText = "--instance-token: one";
            body.appendChild(first);

            var second = HostTestUtilities.GetElement(document.createElement("div"));
            second.className = "shared";
            second.style.cssText = "--instance-token: two";
            body.appendChild(second);

            var third = HostTestUtilities.GetElement(document.createElement("div"));
            third.className = "shared";
            third.style.cssText = "--instance-token: three";
            body.appendChild(third);

            document.EnsureStylesCurrent();

            Assert.Same(second.ComputedOrdinaryStyleStorage, third.ComputedOrdinaryStyleStorage);
            Assert.Same(second.DeclaredOrdinaryStyleStorage, third.DeclaredOrdinaryStyleStorage);
            Assert.Equal("one", document.getComputedStyle(first).getPropertyValue("--instance-token"));
            Assert.Equal("two", document.getComputedStyle(second).getPropertyValue("--instance-token"));
            Assert.Equal("three", document.getComputedStyle(third).getPropertyValue("--instance-token"));
            Assert.True(document.SharedOrdinaryStyleHitCount > 0 || document.CascadeTemplateHitCount > 0);
            Assert.True(document.SharedOrdinaryStyleEntryCount > 0 || document.CascadeTemplateEntryCount > 0);
            Assert.Throws<InvalidOperationException>(
                () => first.ComputedOrdinaryStyleStorage["color"] = "blue");

            second.SetStyleProperty("color", "blue");
            document.EnsureStylesCurrent();

            Assert.NotSame(second.ComputedOrdinaryStyleStorage, third.ComputedOrdinaryStyleStorage);
            Assert.Equal("rgb(0, 0, 255)", document.getComputedStyle(second).getPropertyValue("color"));
            Assert.Equal("rgb(255, 0, 0)", document.getComputedStyle(third).getPropertyValue("color"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void FrameSpecificStylesDoNotPolluteTheSharedStylePool()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var animated = HostTestUtilities.GetElement(document.createElement("div"));
            animated.style.cssText = "position: absolute; left: 0px";
            body.appendChild(animated);
            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();
            var entriesBefore = document.SharedOrdinaryStyleEntryCount;
            var templatesBefore = document.CascadeTemplateEntryCount;

            for (var frame = 1; frame <= 64; frame++)
            {
                animated.SetStyleProperty("left", $"{frame}px");
                document.EnsureStylesCurrent();
            }

            Assert.Equal(entriesBefore, document.SharedOrdinaryStyleEntryCount);
            Assert.Equal(templatesBefore, document.CascadeTemplateEntryCount);
            Assert.Equal("64px", document.getComputedStyle(animated).getPropertyValue("left"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void RepeatedSiblingStylesReuseCascadeTemplatesWithDistinctUnusedCustomProperties()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".template-item { display: flex; width: 40px; padding: 2px; color: blue; }";
            document.head.appendChild(style);

            var elements = new List<AvaloniaDomElement>();
            for (var index = 0; index < 32; index++)
            {
                var element = HostTestUtilities.GetElement(document.createElement("div"));
                element.className = "template-item";
                element.style.cssText = $"--unused-instance-token: {index}";
                body.appendChild(element);
                elements.Add(element);
            }

            document.EnsureStylesCurrent();

            Assert.True(document.CascadeTemplateEntryCount > 0);
            Assert.True(document.CascadeTemplateHitCount >= elements.Count - 2);
            Assert.All(elements, element =>
                Assert.Equal("40px", document.getComputedStyle(element).getPropertyValue("width")));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CascadeTemplatesRespectInheritedAndReferencedCustomPropertyInputs()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = ".template-consumer { color: var(--theme-color); width: inherit; }";
            document.head.appendChild(style);

            var firstParent = HostTestUtilities.GetElement(document.createElement("section"));
            firstParent.style.cssText = "--theme-color: red; width: 70px";
            body.appendChild(firstParent);
            var secondParent = HostTestUtilities.GetElement(document.createElement("section"));
            secondParent.style.cssText = "--theme-color: blue; width: 90px";
            body.appendChild(secondParent);

            var firstChildren = AppendConsumers(document, firstParent);
            var secondChildren = AppendConsumers(document, secondParent);
            document.EnsureStylesCurrent();

            Assert.All(firstChildren, child =>
            {
                Assert.Equal("rgb(255, 0, 0)", document.getComputedStyle(child).getPropertyValue("color"));
                Assert.Equal("70px", document.getComputedStyle(child).getPropertyValue("width"));
            });
            Assert.All(secondChildren, child =>
            {
                Assert.Equal("rgb(0, 0, 255)", document.getComputedStyle(child).getPropertyValue("color"));
                Assert.Equal("90px", document.getComputedStyle(child).getPropertyValue("width"));
            });
            Assert.NotSame(
                firstChildren[1].ComputedOrdinaryStyleStorage,
                secondChildren[1].ComputedOrdinaryStyleStorage);
            window.Close();
        }

        static List<AvaloniaDomElement> AppendConsumers(
            AvaloniaDomDocument document,
            AvaloniaDomElement parent)
        {
            var children = new List<AvaloniaDomElement>();
            for (var index = 0; index < 3; index++)
            {
                var child = HostTestUtilities.GetElement(document.createElement("div"));
                child.className = "template-consumer";
                parent.appendChild(child);
                children.Add(child);
            }
            return children;
        }
    }

    [AvaloniaFact]
    public void IdenticalStylesheetsReuseCompiledRulesAcrossDocuments()
    {
        var className = $"compiled-cache-{Guid.NewGuid():N}";
        var css = $".{className} {{ color: #123456; }}";

        var (firstHost, firstWindow) = CreateTargetOnlyHost();
        using (firstHost)
        {
            var document = firstHost.Document;
            var parsesBefore = document.StylesheetParseCount;
            AppendStyledElement(document, css, className);
            document.EnsureStylesCurrent();

            Assert.Equal(1, document.StylesheetParseCount - parsesBefore);
            Assert.Equal(0, document.CompiledStylesheetCacheHitCount);
            firstWindow.Close();
        }

        var (secondHost, secondWindow) = CreateTargetOnlyHost();
        using (secondHost)
        {
            var document = secondHost.Document;
            var parsesBefore = document.StylesheetParseCount;
            var element = AppendStyledElement(document, css, className);
            document.EnsureStylesCurrent();

            Assert.Equal(0, document.StylesheetParseCount - parsesBefore);
            Assert.Equal(1, document.CompiledStylesheetCacheHitCount);
            Assert.Equal(
                "rgb(18, 52, 86)",
                document.getComputedStyle(element).getPropertyValue("color"));
            secondWindow.Close();
        }

        static AvaloniaDomElement AppendStyledElement(
            AvaloniaDomDocument document,
            string stylesheet,
            string className)
        {
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = stylesheet;
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = className;
            HostTestUtilities.GetElement(document.body).appendChild(element);
            return element;
        }
    }

    [AvaloniaFact]
    public void CachedStylesheetMediaQueriesRemainDocumentSpecific()
    {
        var className = $"compiled-media-{Guid.NewGuid():N}";
        var css = $$"""
            .{{className}} { width: 10px; }
            @media (min-width: 600px) { .{{className}} { width: 80px; } }
            """;

        var narrowWidth = RenderWidth(css, className, 320, out var narrowParses, out var narrowHits);
        var wideWidth = RenderWidth(css, className, 800, out var wideParses, out var wideHits);

        Assert.Equal("10px", narrowWidth);
        Assert.Equal("80px", wideWidth);
        Assert.Equal(1, narrowParses);
        Assert.Equal(0, narrowHits);
        Assert.Equal(0, wideParses);
        Assert.Equal(1, wideHits);

        static string RenderWidth(
            string stylesheet,
            string className,
            double viewportWidth,
            out int parseCount,
            out long cacheHitCount)
        {
            var (host, window) = CreateTargetOnlyHost(viewportWidth);
            using (host)
            {
                var document = host.Document;
                var parsesBefore = document.StylesheetParseCount;
                var style = HostTestUtilities.GetElement(document.createElement("style"));
                style.textContent = stylesheet;
                document.head.appendChild(style);
                var element = HostTestUtilities.GetElement(document.createElement("div"));
                element.className = className;
                HostTestUtilities.GetElement(document.body).appendChild(element);
                document.EnsureStylesCurrent();

                parseCount = document.StylesheetParseCount - parsesBefore;
                cacheHitCount = document.CompiledStylesheetCacheHitCount;
                var width = document.getComputedStyle(element).getPropertyValue("width") ?? string.Empty;
                window.Close();
                return width;
            }
        }
    }

    [AvaloniaFact]
    public void ViewportResizeWithinSameMediaOutcomesSkipsRecascade()
    {
        var (host, window) = CreateTargetOnlyHost(320);
        using (host)
        {
            var document = host.Document;
            var className = $"media-stable-{Guid.NewGuid():N}";
            var element = AppendMediaElement(
                document,
                className,
                $$"""
                    .{{className}} { width: 10px; }
                    @media (min-width: 600px) { .{{className}} { width: 80px; } }
                    """);
            document.EnsureStylesCurrent();
            Assert.Equal("10px", document.getComputedStyle(element).getPropertyValue("width"));

            var computesBefore = document.ElementStyleComputeCount;
            var cacheHitsBefore = document.MediaQueryOutcomeCacheHitCount;
            var reappliesBefore = document.ViewportPresentationReapplyElementCount;
            ResizeAndReconcile(host, window, 400);

            Assert.Equal(computesBefore, document.ElementStyleComputeCount);
            Assert.Equal(cacheHitsBefore + 1, document.MediaQueryOutcomeCacheHitCount);
            Assert.True(document.ViewportPresentationReapplyElementCount > reappliesBefore);
            Assert.Equal("10px", document.getComputedStyle(element).getPropertyValue("width"));
            Assert.False(document.ForceElementPresentationApply);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StableMediaResizeReappliesOnlyViewportSensitivePresentation()
    {
        var (host, window) = CreateTargetOnlyHost(320);
        using (host)
        {
            var document = host.Document;
            var className = $"media-presentation-{Guid.NewGuid():N}";
            var element = AppendMediaElement(
                document,
                className,
                $$"""
                    .{{className}} {
                        position: absolute;
                        left: 25px;
                        transform: translate(7px, 9px);
                        background-color: #123456;
                        font-size: 0;
                        overflow: hidden;
                        pointer-events: auto;
                        visibility: visible;
                        opacity: 0.6;
                        z-index: 12;
                    }
                    @media (min-width: 600px) { .{{className}} { left: 80px; } }
                    """);
            var textNode = Assert.IsType<AvaloniaDomTextNode>(document.createTextNode("collapsed"));
            element.appendChild(textNode);
            document.EnsureStylesCurrent();

            var panel = Assert.IsType<CssLayoutPanel>(element.Control);
            var textBlock = Assert.IsAssignableFrom<TextBlock>(textNode.Control);
            var background = Assert.IsType<SolidColorBrush>(panel.Background);
            Assert.False(textBlock.IsVisible);
            Assert.Equal(new CssLength(25, CssLengthUnit.Pixel), CssLayout.GetLeft(panel));
            var transform = Assert.IsType<TranslateTransform>(panel.RenderTransform);
            Assert.Equal(7, transform.X);
            Assert.Equal(9, transform.Y);
            Assert.True(panel.ClipToBounds);
            Assert.True(panel.IsHitTestVisible);
            Assert.Equal(0.6, panel.Opacity);
            Assert.Equal(12, panel.GetValue(Canvas.ZIndexProperty));

            // Model layout state dirtied by a viewport/layout pass. The stable
            // media path must restore viewport-sensitive state without
            // reconverting viewport-invariant native presentation.
            CssLayout.SetLeft(panel, new CssLength(999, CssLengthUnit.Pixel));
            CssLayout.SetPointerEventsNone(panel, true);
            panel.IsHitTestVisible = false;
            panel.ClipToBounds = false;
            panel.Opacity = 0.2;
            panel.SetValue(Canvas.ZIndexProperty, 999);
            panel.SetValue(Canvas.LeftProperty, 222d);
            panel.RenderTransform = null;
            textBlock.IsVisible = true;
            ResizeAndReconcile(host, window, 400);

            Assert.Equal(new CssLength(25, CssLengthUnit.Pixel), CssLayout.GetLeft(panel));
            transform = Assert.IsType<TranslateTransform>(panel.RenderTransform);
            Assert.Equal(7, transform.X);
            Assert.Equal(9, transform.Y);
            Assert.Same(background, panel.Background);
            Assert.False(textBlock.IsVisible);
            Assert.False(CssLayout.GetPointerEventsNone(panel));
            Assert.True(panel.IsHitTestVisible);
            Assert.True(panel.ClipToBounds);
            Assert.Equal(0.6, panel.Opacity);
            Assert.Equal(12, panel.GetValue(Canvas.ZIndexProperty));
            Assert.False(panel.IsSet(Canvas.LeftProperty));
            Assert.Equal("25px", document.getComputedStyle(element).getPropertyValue("left"));
            Assert.False(document.ForceElementPresentationApply);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ViewportResizeAcrossMediaBreakpointRecascadesAndUpdatesStyle()
    {
        var (host, window) = CreateTargetOnlyHost(320);
        using (host)
        {
            var document = host.Document;
            var className = $"media-crossing-{Guid.NewGuid():N}";
            var element = AppendMediaElement(
                document,
                className,
                $$"""
                    .{{className}} { width: 10px; }
                    @media (min-width: 600px) { .{{className}} { width: 80px; } }
                    """);
            document.EnsureStylesCurrent();

            var computesBefore = document.ElementStyleComputeCount;
            var cacheHitsBefore = document.MediaQueryOutcomeCacheHitCount;
            ResizeAndReconcile(host, window, 800);

            Assert.True(document.ElementStyleComputeCount > computesBefore);
            Assert.Equal(cacheHitsBefore, document.MediaQueryOutcomeCacheHitCount);
            Assert.Equal("80px", document.getComputedStyle(element).getPropertyValue("width"));
            Assert.False(document.ForceElementPresentationApply);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void InactiveNestedMediaQueriesBecomeTrackedWhenParentActivates()
    {
        var (host, window) = CreateTargetOnlyHost(320);
        using (host)
        {
            var document = host.Document;
            var className = $"media-nested-{Guid.NewGuid():N}";
            var element = AppendMediaElement(
                document,
                className,
                $$"""
                    .{{className}} { width: 10px; }
                    @media (min-width: 600px) {
                        @media (max-width: 900px) { .{{className}} { width: 80px; } }
                    }
                    """);
            document.EnsureStylesCurrent();

            var computesBeforeStableResize = document.ElementStyleComputeCount;
            ResizeAndReconcile(host, window, 500);
            Assert.Equal(computesBeforeStableResize, document.ElementStyleComputeCount);
            Assert.Equal("10px", document.getComputedStyle(element).getPropertyValue("width"));

            ResizeAndReconcile(host, window, 700);
            Assert.Equal("80px", document.getComputedStyle(element).getPropertyValue("width"));

            var computesBeforeActiveStableResize = document.ElementStyleComputeCount;
            ResizeAndReconcile(host, window, 800);
            Assert.Equal(computesBeforeActiveStableResize, document.ElementStyleComputeCount);
            Assert.Equal("80px", document.getComputedStyle(element).getPropertyValue("width"));

            var computesBeforeNestedCrossing = document.ElementStyleComputeCount;
            ResizeAndReconcile(host, window, 1000);
            Assert.True(document.ElementStyleComputeCount > computesBeforeNestedCrossing);
            Assert.Equal("10px", document.getComputedStyle(element).getPropertyValue("width"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StyleElementMediaAttributeUsesSameViewportOutcomeCache()
    {
        var (host, window) = CreateTargetOnlyHost(320);
        using (host)
        {
            var document = host.Document;
            var className = $"owner-media-{Guid.NewGuid():N}";
            var baseStyle = HostTestUtilities.GetElement(document.createElement("style"));
            baseStyle.textContent = $".{className} {{ width: 10px; }}";
            document.head.appendChild(baseStyle);
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.setAttribute("media", "(min-width: 600px)");
            style.textContent = $".{className} {{ width: 80px; }}";
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = className;
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();

            var computesBefore = document.ElementStyleComputeCount;
            ResizeAndReconcile(host, window, 500);
            Assert.Equal(computesBefore, document.ElementStyleComputeCount);

            ResizeAndReconcile(host, window, 700);
            Assert.True(document.ElementStyleComputeCount > computesBefore);
            Assert.Equal("80px", document.getComputedStyle(element).getPropertyValue("width"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ChangedStylesheetTextMissesTheCompiledCacheAndRecompiles()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var className = $"compiled-mutation-{Guid.NewGuid():N}";
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = $".{className} {{ color: red; }}";
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = className;
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();

            var parsesBeforeMutation = document.StylesheetParseCount;
            style.textContent = $".{className} {{ color: blue; }}";
            document.EnsureStylesCurrent();

            Assert.Equal(1, document.StylesheetParseCount - parsesBeforeMutation);
            Assert.Equal(
                "rgb(0, 0, 255)",
                document.getComputedStyle(element).getPropertyValue("color"));
            window.Close();
        }
    }

    [AvaloniaFact]
    public void CachedStylesheetsKeepDocumentLocalSourceOrder()
    {
        var className = $"compiled-order-{Guid.NewGuid():N}";
        var red = $".{className} {{ color: red; }}";
        var blue = $".{className} {{ color: blue; }}";

        Assert.Equal("rgb(0, 0, 255)", Render(red, blue, out _));
        Assert.Equal("rgb(255, 0, 0)", Render(blue, red, out var cacheHits));
        Assert.Equal(2, cacheHits);

        string Render(string firstCss, string secondCss, out long cacheHits)
        {
            var (host, window) = CreateTargetOnlyHost();
            using (host)
            {
                var document = host.Document;
                foreach (var css in new[] { firstCss, secondCss })
                {
                    var style = HostTestUtilities.GetElement(document.createElement("style"));
                    style.textContent = css;
                    document.head.appendChild(style);
                }
                var element = HostTestUtilities.GetElement(document.createElement("div"));
                element.className = className;
                HostTestUtilities.GetElement(document.body).appendChild(element);
                document.EnsureStylesCurrent();

                cacheHits = document.CompiledStylesheetCacheHitCount;
                var color = document.getComputedStyle(element).getPropertyValue("color") ?? string.Empty;
                window.Close();
                return color;
            }
        }
    }

    [AvaloniaFact]
    public void OverflowAxisCouplingCreatesScrollableVerticalViewport()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var viewport = HostTestUtilities.GetElement(document.createElement("div"));
            viewport.style.cssText = "width: 200px; height: 80px; overflow-x: hidden";
            body.appendChild(viewport);
            for (var index = 0; index < 10; index++)
            {
                var row = HostTestUtilities.GetElement(document.createElement("div"));
                row.style.cssText = "height: 20px";
                row.textContent = $"Row {index}";
                viewport.appendChild(row);
            }

            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var computed = document.getComputedStyle(viewport);
            Assert.Equal("hidden", computed.getPropertyValue("overflow-x"));
            Assert.Equal("auto", computed.getPropertyValue("overflow-y"));
            Assert.True(viewport.scrollHeight > viewport.clientHeight);
            var first = Assert.IsType<AvaloniaDomElement>(viewport.firstElementChild);
            var firstTop = first.getBoundingClientRect().top;
            var firstOffsetTop = first.offsetTop;
            var scrollEvents = new CountingEventListener();
            viewport.__webSceneAddExternalEventListener(
                "scroll",
                scrollEvents,
                capture: false,
                once: false,
                passive: false);

            viewport.scrollTop = 60;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(60, viewport.scrollTop);
            Assert.Equal(firstTop - 60, first.getBoundingClientRect().top);
            Assert.Equal(firstOffsetTop, first.offsetTop);
            Assert.Equal(1, scrollEvents.InvocationCount);

            viewport.scrollTop = 60;
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, scrollEvents.InvocationCount);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void OverflowViewportClipsHitTestingAndReclampsAfterExtentMutation()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            body.style.cssText = "margin: 0; background: white";
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .no-scrollbar::-webkit-scrollbar {
                    display: none;
                    width: 0;
                    height: 0;
                }
                """;
            document.head.appendChild(style);
            var viewport = HostTestUtilities.GetElement(document.createElement("div"));
            viewport.style.cssText = "width: 120px; height: 60px; overflow: auto; background: white";
            var content = HostTestUtilities.GetElement(document.createElement("div"));
            content.style.cssText = "position: relative; width: 120px; height: 200px";
            var tail = HostTestUtilities.GetElement(document.createElement("div"));
            tail.style.cssText = "position: absolute; left: 0; top: 160px; width: 120px; height: 40px";
            content.appendChild(tail);
            viewport.appendChild(content);
            body.appendChild(viewport);
            var scrollEvents = new CountingEventListener();
            viewport.__webSceneAddExternalEventListener(
                "scroll",
                scrollEvents,
                capture: false,
                once: false,
                passive: false);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(60, viewport.clientHeight);
            Assert.Equal(200, viewport.scrollHeight);
            Assert.NotSame(tail, document.elementFromPoint(10, 170));
            Dispatcher.UIThread.RunJobs();
            var scrollPanel = Assert.IsType<CssLayoutPanel>(viewport.Control);
            var indicator = Assert.Single(scrollPanel.Children.OfType<DomScrollIndicatorControl>());
            using var initialFrame = new RenderTargetBitmap(
                new PixelSize(120, 60),
                new Vector(96, 96));
            initialFrame.Render(indicator);
            var initialThumb = ReadPixel(initialFrame, 117, 6);
            var initialTrackEnd = ReadPixel(initialFrame, 117, 50);
            Assert.True(initialThumb.A > initialTrackEnd.A + 100);

            viewport.scrollTop = 1e6;
            Assert.Equal(140, viewport.scrollTop);
            Assert.Equal(1, scrollEvents.InvocationCount);
            Assert.Same(tail, document.elementFromPoint(10, 30));
            Dispatcher.UIThread.RunJobs();
            using var scrolledFrame = new RenderTargetBitmap(
                new PixelSize(120, 60),
                new Vector(96, 96));
            scrolledFrame.Render(indicator);
            var scrolledTrackStart = ReadPixel(scrolledFrame, 117, 6);
            var scrolledThumb = ReadPixel(scrolledFrame, 117, 50);
            Assert.True(scrolledThumb.A > scrolledTrackStart.A + 100);

            viewport.className = "no-scrollbar";
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Empty(scrollPanel.Children.OfType<DomScrollIndicatorControl>());
            Assert.Equal(140, viewport.scrollTop);

            viewport.className = "";
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Single(scrollPanel.Children.OfType<DomScrollIndicatorControl>());
            Assert.Equal(140, viewport.scrollTop);

            viewport.scrollTop = 1e6;
            Assert.Equal(1, scrollEvents.InvocationCount);

            content.style.setProperty("height", "80px");
            _ = body.offsetWidth;
            Assert.Equal(80, viewport.scrollHeight);
            Assert.Equal(20, viewport.scrollTop);

            content.style.setProperty("height", "200px");
            viewport.scrollTop = 70;
            Assert.Equal(70, viewport.scrollTop);
            Assert.Equal(200, viewport.scrollHeight);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void NativeTextInputDispatchesOneInputEventAfterValueMutation()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var input = HostTestUtilities.GetElement(document.createElement("input"));
            HostTestUtilities.GetElement(document.body).appendChild(input);
            var listener = new InputValueListener();
            document.__webSceneAddExternalEventListener(
                "input",
                listener,
                capture: false,
                once: false,
                passive: false);

            input.Control.RaiseEvent(new TextInputEventArgs
            {
                RoutedEvent = InputElement.TextInputEvent,
                Source = input.Control,
                Text = "Volume"
            });
            Dispatcher.UIThread.RunJobs();

            Assert.NotNull(document.GetType().GetProperty("oninput"));
            Assert.Equal("Volume", input.value);
            Assert.Equal(["Volume"], listener.Values);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void ReleasingOnAButtonDescendantDispatchesOneBubblingClick()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var button = HostTestUtilities.GetElement(document.createElement("button"));
            button.style.cssText = "width: 30px; height: 24px";
            var icon = HostTestUtilities.GetElement(document.createElement("span"));
            icon.style.cssText = "display: block; width: 18px; height: 18px";
            button.appendChild(icon);
            HostTestUtilities.GetElement(document.body).appendChild(button);
            var listener = new CountingEventListener();
            button.__webSceneAddExternalEventListener("click", listener, capture: false, once: false, passive: false);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            icon.Control.RaiseEvent(new PointerPressedEventArgs(
                icon.Control,
                pointer,
                window,
                new Point(9, 9),
                0,
                new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed),
                KeyModifiers.None));
            icon.Control.RaiseEvent(new PointerReleasedEventArgs(
                icon.Control,
                pointer,
                window,
                new Point(9, 9),
                1,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased),
                KeyModifiers.None,
                MouseButton.Left));
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(1, listener.InvocationCount);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StoppingDomKeyPropagationPreservesNativeTextBoxEditingDefaults()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var input = HostTestUtilities.GetElement(host.Document.createElement("input"));
            HostTestUtilities.GetElement(host.Document.body).appendChild(input);
            input.value = "Volume";
            input.focus();
            Dispatcher.UIThread.RunJobs();
            var textBox = Assert.IsType<DomTextInputControl>(input.Control);
            textBox.CaretIndex = textBox.Text?.Length ?? 0;
            var listener = new StopPropagationListener();
            var inputListener = new InputValueListener();
            input.__webSceneAddExternalEventListener(
                "keydown",
                listener,
                capture: false,
                once: false,
                passive: false);
            input.__webSceneAddExternalEventListener(
                "input",
                inputListener,
                capture: false,
                once: false,
                passive: false);

            window.KeyPress(Key.Left, RawInputModifiers.None, PhysicalKey.ArrowLeft, "ArrowLeft");
            window.KeyRelease(Key.Left, RawInputModifiers.None, PhysicalKey.ArrowLeft, "ArrowLeft");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(5, textBox.CaretIndex);

            window.KeyPress(Key.Back, RawInputModifiers.None, PhysicalKey.Backspace, "Backspace");
            window.KeyRelease(Key.Back, RawInputModifiers.None, PhysicalKey.Backspace, "Backspace");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Volue", input.value);
            Assert.Equal(4, textBox.CaretIndex);

            window.KeyPress(Key.Delete, RawInputModifiers.None, PhysicalKey.Delete, "Delete");
            window.KeyRelease(Key.Delete, RawInputModifiers.None, PhysicalKey.Delete, "Delete");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("Volu", input.value);
            Assert.Equal(4, textBox.CaretIndex);

            window.KeyPress(Key.Left, RawInputModifiers.Shift, PhysicalKey.ArrowLeft, "ArrowLeft");
            window.KeyRelease(Key.Left, RawInputModifiers.Shift, PhysicalKey.ArrowLeft, "ArrowLeft");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(3, textBox.CaretIndex);
            Assert.Equal(3, textBox.SelectionStart);
            Assert.Equal(4, textBox.SelectionEnd);

            window.KeyPress(Key.Right, RawInputModifiers.None, PhysicalKey.ArrowRight, "ArrowRight");
            window.KeyRelease(Key.Right, RawInputModifiers.None, PhysicalKey.ArrowRight, "ArrowRight");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(4, textBox.CaretIndex);
            Assert.Equal(textBox.SelectionStart, textBox.SelectionEnd);

            listener.PreventDefault = true;
            window.KeyPress(Key.Left, RawInputModifiers.None, PhysicalKey.ArrowLeft, "ArrowLeft");
            window.KeyRelease(Key.Left, RawInputModifiers.None, PhysicalKey.ArrowLeft, "ArrowLeft");
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(4, textBox.CaretIndex);
            Assert.Equal(6, listener.InvocationCount);
            Assert.Equal(["Volue", "Volu"], inputListener.Values);
            window.Close();
        }
    }

    [AvaloniaFact]
    public void StylesheetNormalizationGuardsPreserveLogicalAndVariableDeclarations()
    {
        var (host, window) = CreateTargetOnlyHost();
        using (host)
        {
            var document = host.Document;
            var className = $"normalized-{Guid.NewGuid():N}";
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = $$"""
                .{{className}} {
                    --surface: #123456;
                    background: var(--surface);
                    padding-block: 3px 7px;
                    inset-inline-start: 11px;
                }
                """;
            document.head.appendChild(style);
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = className;
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();

            var computed = document.getComputedStyle(element);
            Assert.Equal("rgb(18, 52, 86)", computed.getPropertyValue("background-color"));
            Assert.Equal("3px", computed.getPropertyValue("padding-top"));
            Assert.Equal("7px", computed.getPropertyValue("padding-bottom"));
            Assert.Equal("11px", computed.getPropertyValue("left"));
            window.Close();
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

    private static (AvaloniaBrowserHost Host, Window Window) CreateTargetOnlyHost(double width = 320)
    {
        var panel = new CssLayoutPanel { Width = width, Height = 180 };
        var window = new Window
        {
            Width = width,
            Height = 180,
            Content = panel
        };
        var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return (host, window);
    }

    private sealed class InputValueListener : IExternalDomEventListener
    {
        public List<string?> Values { get; } = [];

        public void Invoke(object currentTarget, object domEvent)
            => Values.Add((((DomEvent)domEvent).target as AvaloniaDomElement)?.value);
    }

    private sealed class StopPropagationListener : IExternalDomEventListener
    {
        public int InvocationCount { get; private set; }

        public bool PreventDefault { get; set; }

        public void Invoke(object currentTarget, object domEvent)
        {
            InvocationCount++;
            var currentEvent = (DomEvent)domEvent;
            currentEvent.stopPropagation();
            if (PreventDefault)
            {
                currentEvent.preventDefault();
            }
        }
    }

    private sealed class CountingEventListener : IExternalDomEventListener
    {
        public int InvocationCount { get; private set; }

        public void Invoke(object currentTarget, object domEvent) => InvocationCount++;
    }

    private sealed class MutationRecordCallback : IExternalJavaScriptCallback
    {
        public List<DomMutationRecord> Records { get; } = [];

        public void Invoke(object? thisValue, params object?[] arguments)
        {
            if (arguments.Length > 0 && arguments[0] is DomMutationRecord[] records)
            {
                Records.AddRange(records);
            }
        }
    }

    private static AvaloniaDomElement AppendMediaElement(
        AvaloniaDomDocument document,
        string className,
        string css)
    {
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = css;
        document.head.appendChild(style);
        var element = HostTestUtilities.GetElement(document.createElement("div"));
        element.className = className;
        HostTestUtilities.GetElement(document.body).appendChild(element);
        return element;
    }

    private static void ResizeAndReconcile(
        AvaloniaBrowserHost host,
        Window window,
        double width)
    {
        window.Width = width;
        Assert.IsType<CssLayoutPanel>(window.Content).Width = width;
        Dispatcher.UIThread.RunJobs();
        host.Document.ReconcileStylesAfterViewportResize();
        host.Document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
    }
}

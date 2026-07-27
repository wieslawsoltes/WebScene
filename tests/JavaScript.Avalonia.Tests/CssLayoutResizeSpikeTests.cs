using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using JavaScript.Avalonia;
using Xunit;

namespace JavaScript.Avalonia.Tests;

/// <summary>
/// Small render spikes for shrink/grow bugs. These deliberately avoid a
/// JavaScript engine and any product component so retained clipping can be diagnosed at
/// the Avalonia/CSS-layout boundary.
/// </summary>
public sealed class CssLayoutResizeSpikeTests
{
    [AvaloniaFact]
    public void PositionedResizeReapplyStaysInsideTheHostsDocumentRoot()
    {
        var firstRoot = CreateLegacyCanvasDocument(out var firstPositioned);
        var secondRoot = CreateLegacyCanvasDocument(out var secondPositioned);
        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("*,*")
        };
        Grid.SetColumn(secondRoot, 1);
        grid.Children.Add(firstRoot);
        grid.Children.Add(secondRoot);
        var window = new Window { Width = 400, Height = 200, Content = grid };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Canvas.SetLeft(firstPositioned, 7);
            Canvas.SetLeft(secondPositioned, 13);
            using var host = new AvaloniaBrowserHost(
                window,
                currentHost => new ScopedTestDocument(currentHost, firstRoot));
            host.Document.WrapControl(firstPositioned).style.setProperty("left", "50%");
            host.Document.WrapControl(secondPositioned).style.setProperty("left", "50%");
            Canvas.SetLeft(firstPositioned, 7);
            Canvas.SetLeft(secondPositioned, 13);

            host.Document.ReapplyAllPositionedLayout();

            Assert.NotEqual(7, Canvas.GetLeft(firstPositioned));
            Assert.Equal(13, Canvas.GetLeft(secondPositioned));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void ProductionProjectionCapturesOnlyThePanelAndItsDirectChildren()
    {
        var root = new CssLayoutPanel();
        var child = new CssLayoutPanel();
        var grandchild = new Border();
        root.Children.Add(child);
        child.Children.Add(grandchild);
        CssLayout.SetWidth(child, new CssLength(100, CssLengthUnit.Pixel));
        CssLayout.SetHeight(child, new CssLength(50, CssLengthUnit.Pixel));
        CssLayout.SetWidth(grandchild, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetHeight(grandchild, new CssLength(10, CssLengthUnit.Pixel));
        root.Measure(new Size(200, 100));
        root.Arrange(new Rect(0, 0, 200, 100));

        var direct = AvaloniaCssLayoutProjection.CaptureDirect(
            root,
            new Size(200, 100),
            new Rect(0, 0, 200, 100),
            new Rect(0, 0, 200, 100));
        var complete = AvaloniaCssLayoutProjection.Capture(root, new Size(200, 100));

        Assert.NotEqual(WebScene.Core.WebSceneRect.Empty, direct.GetBox(child).BorderBox);
        Assert.Throws<KeyNotFoundException>(() => direct.GetBox(grandchild));
        Assert.NotEqual(WebScene.Core.WebSceneRect.Empty, complete.GetBox(grandchild).BorderBox);
    }

    private static Canvas CreateLegacyCanvasDocument(out Border positioned)
    {
        var root = new Canvas { Width = 200, Height = 200 };
        positioned = new Border { Width = 20, Height = 20 };
        CssLayout.SetPosition(positioned, CssPosition.Absolute);
        CssLayout.SetLeft(positioned, new CssLength(50, CssLengthUnit.Percent));
        root.Children.Add(positioned);
        return root;
    }

    private sealed class ScopedTestDocument : AvaloniaDomDocument
    {
        private readonly Control _root;

        internal ScopedTestDocument(AvaloniaBrowserHost host, Control root)
            : base(host)
        {
            _root = root;
        }

        protected override Control? GetDocumentRoot() => _root;
    }

    [AvaloniaFact]
    public void PortableLayoutDualRunMatchesAvaloniaAbsoluteGeometry()
    {
        var root = new CssLayoutPanel { Width = 400, Height = 200 };
        var child = new Border();
        root.Children.Add(child);
        CssLayout.SetPosition(child, CssPosition.Absolute);
        CssLayout.SetLeft(child, new CssLength(50, CssLengthUnit.Percent) { PixelOffset = -14 });
        CssLayout.SetTop(child, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetWidth(child, new CssLength(28, CssLengthUnit.Pixel));
        CssLayout.SetHeight(child, new CssLength(30, CssLengthUnit.Pixel));
        var window = new Window { Width = 400, Height = 200, Content = root };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var dualRun = AvaloniaCssLayoutProjection.Capture(root, new Size(400, 200));
            var portable = dualRun.GetBox(child).BorderBox;

            Assert.Equal((child.Bounds.X, child.Bounds.Y, child.Bounds.Width, child.Bounds.Height),
                (portable.X, portable.Y, portable.Width, portable.Height));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void NativeAbsoluteLayoutHonorsPositionedMarginsAndDualInsetStretch()
    {
        var root = new CssLayoutPanel { Width = 200, Height = 100 };
        CssLayout.SetNativeLayoutHotPath(root, true);
        var automatic = new Border();
        var fromEnd = new Border();
        var stretched = new Border();
        root.Children.Add(automatic);
        root.Children.Add(fromEnd);
        root.Children.Add(stretched);

        CssLayout.SetPosition(automatic, CssPosition.Absolute);
        CssLayout.SetWidth(automatic, new CssLength(100, CssLengthUnit.Pixel));
        CssLayout.SetHeight(automatic, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetMarginLeft(automatic, new CssLength(50, CssLengthUnit.Pixel));
        CssLayout.SetMarginTop(automatic, new CssLength(12, CssLengthUnit.Pixel));

        CssLayout.SetPosition(fromEnd, CssPosition.Absolute);
        CssLayout.SetRight(fromEnd, new CssLength(10, CssLengthUnit.Pixel));
        CssLayout.SetWidth(fromEnd, new CssLength(50, CssLengthUnit.Pixel));
        CssLayout.SetHeight(fromEnd, new CssLength(10, CssLengthUnit.Pixel));
        CssLayout.SetMarginRight(fromEnd, new CssLength(7, CssLengthUnit.Pixel));

        CssLayout.SetPosition(stretched, CssPosition.Absolute);
        CssLayout.SetLeft(stretched, new CssLength(10, CssLengthUnit.Pixel));
        CssLayout.SetRight(stretched, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetHeight(stretched, new CssLength(10, CssLengthUnit.Pixel));
        CssLayout.SetMarginLeft(stretched, new CssLength(5, CssLengthUnit.Pixel));
        CssLayout.SetMarginRight(stretched, new CssLength(7, CssLengthUnit.Pixel));

        root.Measure(new Size(200, 100));
        root.Arrange(new Rect(0, 0, 200, 100));

        Assert.Equal(new Rect(50, 12, 100, 20), automatic.Bounds);
        Assert.Equal(new Rect(133, 0, 50, 10), fromEnd.Bounds);
        Assert.Equal(new Rect(15, 0, 158, 10), stretched.Bounds);
    }

    [AvaloniaFact]
    public void NativeBlockAbsoluteAutoInsetsUseStaticPositionWithoutConsumingFlow()
    {
        var root = new CssLayoutPanel { Width = 100, Height = 100 };
        CssLayout.SetNativeLayoutHotPath(root, true);
        var first = new Border();
        var absoluteA = new Border();
        var absoluteB = new Border();
        var last = new Border();
        root.Children.Add(first);
        root.Children.Add(absoluteA);
        root.Children.Add(absoluteB);
        root.Children.Add(last);
        CssLayout.SetHeight(first, new CssLength(12, CssLengthUnit.Pixel));
        foreach (var absolute in new[] { absoluteA, absoluteB })
        {
            CssLayout.SetPosition(absolute, CssPosition.Absolute);
            CssLayout.SetWidth(absolute, new CssLength(10, CssLengthUnit.Pixel));
            CssLayout.SetHeight(absolute, new CssLength(10, CssLengthUnit.Pixel));
        }
        CssLayout.SetHeight(last, new CssLength(8, CssLengthUnit.Pixel));

        root.Measure(new Size(100, 100));
        root.Arrange(new Rect(0, 0, 100, 100));

        Assert.Equal(12, absoluteA.Bounds.Y);
        Assert.Equal(12, absoluteB.Bounds.Y);
        Assert.Equal(12, last.Bounds.Y);
    }

    [AvaloniaFact]
    public void AbsoluteStaticPositionTracksEarlierSiblingMutationThroughStaticParent()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 300 };
        var window = new Window { Width = 300, Height = 300, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var stylesheet = HostTestUtilities.GetElement(document.createElement("style"));
        stylesheet.textContent = """
            #container { position: relative; }
            #intermediate { overflow: hidden; width: 200px; height: 200px; }
            #block { height: 200px; }
            #target { position: absolute; width: 200px; height: 100px; }
            """;
        document.head.appendChild(stylesheet);
        var container = HostTestUtilities.GetElement(document.createElement("div"));
        var intermediate = HostTestUtilities.GetElement(document.createElement("div"));
        var block = HostTestUtilities.GetElement(document.createElement("div"));
        var target = HostTestUtilities.GetElement(document.createElement("div"));

        container.id = "container";
        intermediate.id = "intermediate";
        block.id = "block";
        target.id = "target";
        intermediate.appendChild(block);
        intermediate.appendChild(target);
        container.appendChild(intermediate);
        HostTestUtilities.GetElement(document.body).appendChild(container);

        try
        {
            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            _ = HostTestUtilities.GetElement(document.body).offsetTop;

            block.style.setProperty("height", "100px");

            Assert.Same(intermediate.Control, target.Control.Parent);
            Assert.Equal(100, target.offsetTop);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void PortableArrangementMatchesAvaloniaBlockFlexAndGridGeometry()
    {
        AssertPortableGeometryMatches(CreateBlockFixture(), new Size(300, 120));
        AssertPortableGeometryMatches(CreateFlexFixture(), new Size(300, 100));
        AssertPortableGeometryMatches(CreateGridFixture(), new Size(300, 100));
    }

    [AvaloniaFact]
    public void FlexWrapProjectsIntoNativeAndPortablePerLineGeometry()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 120 };
        var window = new Window { Width = 300, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .wrap {
                    column-gap: 4px;
                    display: flex;
                    flex-wrap: wrap;
                    height: 40px;
                    row-gap: 4px;
                    width: 100px;
                }
                .item { flex-grow: 1; width: 40px; }
                .wide { width: 70px; }
                """;
            document.head.appendChild(style);

            var container = HostTestUtilities.GetElement(document.createElement("div"));
            container.className = "wrap";
            var first = HostTestUtilities.GetElement(document.createElement("div"));
            first.className = "item wide";
            var second = HostTestUtilities.GetElement(document.createElement("div"));
            second.className = "item";
            var third = HostTestUtilities.GetElement(document.createElement("div"));
            third.className = "item";
            container.appendChild(first);
            container.appendChild(second);
            container.appendChild(third);
            HostTestUtilities.GetElement(document.body).appendChild(container);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal("wrap", document.getComputedStyle(container).getPropertyValue("flex-wrap"));
            var containerRect = container.getBoundingClientRect();
            var firstRect = first.getBoundingClientRect();
            var secondRect = second.getBoundingClientRect();
            var thirdRect = third.getBoundingClientRect();
            Assert.Equal((0d, 0d, 100d, 18d),
                (firstRect.left - containerRect.left, firstRect.top - containerRect.top, firstRect.width, firstRect.height));
            Assert.Equal((0d, 22d, 48d, 18d),
                (secondRect.left - containerRect.left, secondRect.top - containerRect.top, secondRect.width, secondRect.height));
            Assert.Equal((52d, 22d, 48d, 18d),
                (thirdRect.left - containerRect.left, thirdRect.top - containerRect.top, thirdRect.width, thirdRect.height));

            var portable = AvaloniaCssLayoutProjection.Capture(
                Assert.IsType<CssLayoutPanel>(container.Control),
                new Size(containerRect.width, containerRect.height));
            foreach (var element in new[] { first, second, third })
            {
                var native = element.getBoundingClientRect();
                var box = portable.GetBox(element.Control).BorderBox;
                Assert.Equal(
                    (native.left - containerRect.left, native.top - containerRect.top, native.width, native.height),
                    (box.X, box.Y, box.Width, box.Height));
            }

            container.style.setProperty("flex-wrap", "wrap-reverse");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("wrap-reverse", document.getComputedStyle(container).getPropertyValue("flex-wrap"));
            Assert.Equal(22, first.getBoundingClientRect().top - container.getBoundingClientRect().top);
            Assert.Equal(0, second.getBoundingClientRect().top - container.getBoundingClientRect().top);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void FixedPixelGridTracksAndLegacyGridGapMatchNativeAndPortableGeometry()
    {
        var root = new CssLayoutPanel { Width = 262, Height = 28 };
        var window = new Window { Width = 262, Height = 28, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .calendar-row {
                    display: grid;
                    grid-template-columns: 150px 100px;
                    grid-gap: 4px;
                    gap: 12px;
                    height: 28px;
                    width: 262px;
                }
                """;
            document.head.appendChild(style);
            var container = HostTestUtilities.GetElement(document.createElement("div"));
            container.className = "calendar-row";
            var date = HostTestUtilities.GetElement(document.createElement("div"));
            var time = HostTestUtilities.GetElement(document.createElement("div"));
            container.appendChild(date);
            container.appendChild(time);
            HostTestUtilities.GetElement(document.body).appendChild(container);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.True(container.ComputedStyleValues.ContainsKey("row-gap"),
                $"Computed keys: {string.Join(", ", container.ComputedStyleValues.Keys.Order())}");
            Assert.True(container.DeclaredStyleProperties.Contains("row-gap"),
                $"Declared keys: {string.Join(", ", container.DeclaredStyleProperties.Order())}");
            Assert.Equal("12px", document.getComputedStyle(container).getPropertyValue("row-gap"));
            Assert.Equal("12px", document.getComputedStyle(container).getPropertyValue("column-gap"));
            var containerRect = container.getBoundingClientRect();
            var dateRect = date.getBoundingClientRect();
            var timeRect = time.getBoundingClientRect();
            Assert.Equal((0d, 150d), (dateRect.left - containerRect.left, dateRect.width));
            Assert.Equal((162d, 100d), (timeRect.left - containerRect.left, timeRect.width));

            var portable = AvaloniaCssLayoutProjection.Capture(
                Assert.IsType<CssLayoutPanel>(container.Control),
                new Size(containerRect.width, containerRect.height));
            var portableDate = portable.GetBox(date.Control).BorderBox;
            var portableTime = portable.GetBox(time.Control).BorderBox;
            Assert.Equal((0d, 150d), (portableDate.X, portableDate.Width));
            Assert.Equal((162d, 100d), (portableTime.X, portableTime.Width));

            container.style.setProperty("grid-gap", "8px 6px");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("8px", document.getComputedStyle(container).getPropertyValue("row-gap"));
            Assert.Equal("6px", document.getComputedStyle(container).getPropertyValue("column-gap"));
            Assert.Equal(156, time.getBoundingClientRect().left - container.getBoundingClientRect().left);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void NestedPositionedPortableGeometrySurvivesGrowAndResizeBack()
    {
        var root = new CssLayoutPanel { Width = 400, Height = 200 };
        var positioned = new CssLayoutPanel();
        var absolute = new Border();
        var fixedChild = new Border();
        CssLayout.SetDocumentViewportRoot(root, true);
        root.Children.Add(positioned);
        positioned.Children.Add(absolute);
        positioned.Children.Add(fixedChild);
        CssLayout.SetPosition(positioned, CssPosition.Relative);
        CssLayout.SetWidth(positioned, new CssLength(50, CssLengthUnit.Percent));
        CssLayout.SetHeight(positioned, new CssLength(100, CssLengthUnit.Pixel));
        CssLayout.SetMarginLeft(positioned, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetPosition(absolute, CssPosition.Absolute);
        CssLayout.SetRight(absolute, new CssLength(10, CssLengthUnit.Pixel));
        CssLayout.SetBottom(absolute, new CssLength(5, CssLengthUnit.Pixel));
        CssLayout.SetWidth(absolute, new CssLength(30, CssLengthUnit.Pixel));
        CssLayout.SetHeight(absolute, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetPosition(fixedChild, CssPosition.Fixed);
        CssLayout.SetRight(fixedChild, new CssLength(12, CssLengthUnit.Pixel));
        CssLayout.SetBottom(fixedChild, new CssLength(8, CssLengthUnit.Pixel));
        CssLayout.SetWidth(fixedChild, new CssLength(24, CssLengthUnit.Pixel));
        CssLayout.SetHeight(fixedChild, new CssLength(16, CssLengthUnit.Pixel));
        var window = new Window { Width = 400, Height = 200, Content = root };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var initial = AssertPortableTreeMatches(root, positioned, absolute, fixedChild);

            window.Width = 600;
            window.Height = 300;
            root.Width = 600;
            root.Height = 300;
            root.InvalidateMeasure();
            root.InvalidateArrange();
            Dispatcher.UIThread.RunJobs();
            using var grownFrame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var grown = AssertPortableTreeMatches(root, positioned, absolute, fixedChild);
            Assert.True(grown.Positioned.Width > initial.Positioned.Width);
            Assert.True(grown.Fixed.X > initial.Fixed.X);

            window.Width = 400;
            window.Height = 200;
            root.Width = 400;
            root.Height = 200;
            root.InvalidateMeasure();
            root.InvalidateArrange();
            Dispatcher.UIThread.RunJobs();
            using var restoredFrame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var restored = AssertPortableTreeMatches(root, positioned, absolute, fixedChild);
            Assert.Equal(initial, restored);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static PositionedFixtureGeometry AssertPortableTreeMatches(
        CssLayoutPanel root,
        Control positioned,
        Control absolute,
        Control fixedChild)
    {
        var viewport = root.Bounds.Size;
        var snapshot = AvaloniaCssLayoutProjection.Capture(root, viewport);
        static Rect Portable(AvaloniaCssLayoutSnapshot snapshot, Control control)
        {
            var box = snapshot.GetBox(control).BorderBox;
            return new Rect(box.X, box.Y, box.Width, box.Height);
        }

        var positionedBox = Portable(snapshot, positioned);
        var absoluteBox = Portable(snapshot, absolute);
        var fixedBox = Portable(snapshot, fixedChild);
        Assert.Equal(positioned.Bounds, positionedBox);
        Assert.Equal(absolute.TranslatePoint(new Point(), root), absoluteBox.TopLeft);
        Assert.Equal(absolute.Bounds.Size, absoluteBox.Size);
        Assert.Equal(fixedChild.TranslatePoint(new Point(), root), fixedBox.TopLeft);
        Assert.Equal(fixedChild.Bounds.Size, fixedBox.Size);
        return new PositionedFixtureGeometry(positionedBox, absoluteBox, fixedBox);
    }

    private readonly record struct PositionedFixtureGeometry(Rect Positioned, Rect Absolute, Rect Fixed);

    [AvaloniaFact]
    public void IntrinsicTextAndOverflowExtentUsePortableGeometry()
    {
        var root = new CssLayoutPanel { Width = 180, Height = 60 };
        root.SetOverflow("hidden", "auto");
        CssLayout.SetDisplay(root, CssDisplay.Flex);
        CssLayout.SetAlignItems(root, "flex-start");
        var text = new TextBlock
        {
            Text = "Portable intrinsic text",
            FontSize = 18,
            TextWrapping = TextWrapping.NoWrap
        };
        var wide = new Border();
        root.Children.Add(text);
        root.Children.Add(wide);
        CssLayout.SetDisplay(text, CssDisplay.InlineBlock);
        CssLayout.SetFlexShrink(text, 0);
        CssLayout.SetWidth(wide, new CssLength(220, CssLengthUnit.Pixel));
        CssLayout.SetHeight(wide, new CssLength(80, CssLengthUnit.Pixel));
        CssLayout.SetFlexShrink(wide, 0);
        var window = new Window { Width = 180, Height = 60, Content = root };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var snapshot = AvaloniaCssLayoutProjection.Capture(root, root.Bounds.Size);
            var textBox = snapshot.GetBox(text).BorderBox;
            var wideBox = snapshot.GetBox(wide).BorderBox;

            AssertRectClose(text.Bounds, new Rect(textBox.X, textBox.Y, textBox.Width, textBox.Height));
            Assert.True(textBox.Width > 100);
            AssertRectClose(wide.Bounds, new Rect(wideBox.X, wideBox.Y, wideBox.Width, wideBox.Height));
            Assert.True(root.ClipToBounds);
            Assert.True(root.ScrollExtent.Width >= wideBox.Right);
            Assert.True(root.ScrollExtent.Height >= 80);
            Assert.Equal("hidden", root.OverflowX);
            Assert.Equal("auto", root.OverflowY);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void ZeroWidthFlexWrapperDoesNotShrinkANonShrinkingFlexChildsIntrinsicWidth()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 60 };
        CssLayout.SetDisplay(root, CssDisplay.Flex);
        var zeroWidthWrapper = new CssLayoutPanel();
        CssLayout.SetDisplay(zeroWidthWrapper, CssDisplay.Flex);
        CssLayout.SetWidth(zeroWidthWrapper, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetPointerEventsNone(zeroWidthWrapper, true);
        root.Children.Add(zeroWidthWrapper);

        var overflowingButtons = new CssLayoutPanel();
        CssLayout.SetDisplay(overflowingButtons, CssDisplay.Flex);
        CssLayout.SetFlexShrink(overflowingButtons, 0);
        zeroWidthWrapper.Children.Add(overflowingButtons);
        Border? lastButton = null;
        for (var index = 0; index < 4; index++)
        {
            var button = new Border { Background = Brushes.Transparent };
            CssLayout.SetWidth(button, new CssLength(28, CssLengthUnit.Pixel));
            CssLayout.SetHeight(button, new CssLength(24, CssLengthUnit.Pixel));
            overflowingButtons.Children.Add(button);
            lastButton = button;
        }

        var window = new Window { Width = 300, Height = 60, Content = root };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(0, zeroWidthWrapper.Bounds.Width);
            Assert.Equal(112, overflowingButtons.Bounds.Width);
            Assert.Equal(28, overflowingButtons.Children[3].Bounds.Width);
            Assert.Equal(112, overflowingButtons.Children[3].Bounds.Right);
            Assert.NotNull(lastButton);
            Assert.True(zeroWidthWrapper.HitTest(new Point(98, 12)));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static void AssertRectClose(Rect expected, Rect actual)
    {
        Assert.InRange(Math.Abs(expected.X - actual.X), 0, .1);
        Assert.InRange(Math.Abs(expected.Y - actual.Y), 0, .1);
        Assert.InRange(Math.Abs(expected.Width - actual.Width), 0, .1);
        Assert.InRange(Math.Abs(expected.Height - actual.Height), 0, .1);
    }

    private static CssLayoutPanel CreateBlockFixture()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 120 };
        var first = new Border();
        var second = new Border();
        root.Children.Add(first);
        root.Children.Add(second);
        CssLayout.SetHeight(first, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetMarginLeft(first, new CssLength(5, CssLengthUnit.Pixel));
        CssLayout.SetHeight(second, new CssLength(30, CssLengthUnit.Pixel));
        CssLayout.SetPosition(second, CssPosition.Relative);
        CssLayout.SetLeft(second, new CssLength(3, CssLengthUnit.Pixel));
        return root;
    }

    private static CssLayoutPanel CreateFlexFixture()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 100 };
        CssLayout.SetDisplay(root, CssDisplay.Flex);
        CssLayout.SetAlignItems(root, "center");
        CssLayout.SetJustifyContent(root, "space-between");
        var first = new Border();
        var second = new Border();
        root.Children.Add(first);
        root.Children.Add(second);
        CssLayout.SetWidth(first, new CssLength(40, CssLengthUnit.Pixel));
        CssLayout.SetHeight(first, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetWidth(second, new CssLength(50, CssLengthUnit.Pixel));
        CssLayout.SetHeight(second, new CssLength(30, CssLengthUnit.Pixel));
        return root;
    }

    private static CssLayoutPanel CreateGridFixture()
    {
        var root = new CssLayoutPanel { Width = 300, Height = 100 };
        CssLayout.SetDisplay(root, CssDisplay.Grid);
        var first = new Border();
        var second = new Border();
        root.Children.Add(first);
        root.Children.Add(second);
        CssLayout.SetWidth(first, new CssLength(40, CssLengthUnit.Pixel));
        CssLayout.SetHeight(first, new CssLength(20, CssLengthUnit.Pixel));
        CssLayout.SetWidth(second, new CssLength(60, CssLengthUnit.Pixel));
        CssLayout.SetHeight(second, new CssLength(30, CssLengthUnit.Pixel));
        return root;
    }

    private static void AssertPortableGeometryMatches(CssLayoutPanel root, Size size)
    {
        var window = new Window { Width = size.Width, Height = size.Height, Content = root };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var portable = AvaloniaCssLayoutProjection.Capture(root, size);
            foreach (var child in root.Children)
            {
                var box = portable.GetBox(child).BorderBox;
                Assert.Equal(
                    (child.Bounds.X, child.Bounds.Y, child.Bounds.Width, child.Bounds.Height),
                    (box.X, box.Y, box.Width, box.Height));
            }
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void FlexContentBoxIncludesPaddingAndContainerBorderInSearchGeometry()
    {
        var root = new CssLayoutPanel { Width = 500, Height = 100 };
        var window = new Window
        {
            Width = 500,
            Height = 100,
            Content = root
        };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .search-row {
                    align-items: center;
                    border-bottom: 1px solid #555;
                    border-top: 1px solid #555;
                    display: flex;
                    position: relative;
                    width: 380px;
                }
                .search-box {
                    height: 24px;
                    padding: 8px 16px 8px 47px;
                    width: 100%;
                }
                .search-box input {
                    height: 100%;
                    margin: 0;
                    padding: 0;
                    width: 100%;
                }
                .search-icon {
                    height: 28px;
                    left: 15px;
                    position: absolute;
                    top: calc(50% - 14px);
                    width: 28px;
                }
                """;
            document.head.appendChild(style);

            var row = HostTestUtilities.GetElement(document.createElement("div"));
            row.className = "search-row";
            var box = HostTestUtilities.GetElement(document.createElement("div"));
            box.className = "search-box";
            var input = HostTestUtilities.GetElement(document.createElement("input"));
            var icon = HostTestUtilities.GetElement(document.createElement("div"));
            icon.className = "search-icon";
            box.appendChild(input);
            row.appendChild(box);
            row.appendChild(icon);
            HostTestUtilities.GetElement(document.body).appendChild(row);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var rowRect = row.getBoundingClientRect();
            var boxRect = box.getBoundingClientRect();
            var inputRect = input.getBoundingClientRect();
            var iconRect = icon.getBoundingClientRect();

            Assert.Equal(380, rowRect.width);
            Assert.Equal(42, rowRect.height);
            Assert.Equal((0d, 1d, 380d, 40d),
                (boxRect.left - rowRect.left, boxRect.top - rowRect.top, boxRect.width, boxRect.height));
            Assert.Equal((47d, 9d, 317d, 24d),
                (inputRect.left - rowRect.left, inputRect.top - rowRect.top, inputRect.width, inputRect.height));
            Assert.Equal((15d, 7d, 28d, 28d),
                (iconRect.left - rowRect.left, iconRect.top - rowRect.top, iconRect.width, iconRect.height));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void AutoInlineFlexUsesReplacedInputIntrinsicWidthThenShrinksToContainingBlock()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 100 };
        var window = new Window
        {
            Width = 320,
            Height = 100,
            Content = root
        };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .grid {
                    display: grid;
                    grid-template-columns: 150px 100px;
                    column-gap: 12px;
                }
                .time {
                    display: inline-block;
                    max-width: 100px;
                    position: relative;
                }
                .form-input {
                    border: 1px solid #777;
                    box-sizing: border-box;
                    display: inline-flex;
                    height: 28px;
                }
                .middle-slot {
                    display: flex;
                    flex: 1 1 auto;
                    overflow: hidden;
                }
                .form-input input {
                    border: 0;
                    display: block;
                    font: 16px monospace;
                    height: 100%;
                    min-width: 0;
                    padding: 0 5px;
                    width: 100%;
                }
                """;
            document.head.appendChild(style);

            var grid = HostTestUtilities.GetElement(document.createElement("div"));
            grid.className = "grid";
            var date = HostTestUtilities.GetElement(document.createElement("div"));
            var time = HostTestUtilities.GetElement(document.createElement("div"));
            time.className = "time";
            var formInput = HostTestUtilities.GetElement(document.createElement("div"));
            formInput.className = "form-input";
            var middleSlot = HostTestUtilities.GetElement(document.createElement("span"));
            middleSlot.className = "middle-slot";
            var input = HostTestUtilities.GetElement(document.createElement("input"));
            input.value = "00:00";
            middleSlot.appendChild(input);
            formInput.appendChild(middleSlot);
            time.appendChild(formInput);
            grid.appendChild(date);
            grid.appendChild(time);
            HostTestUtilities.GetElement(document.body).appendChild(grid);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var timeRect = time.getBoundingClientRect();
            var formRect = formInput.getBoundingClientRect();
            var middleRect = middleSlot.getBoundingClientRect();
            var inputRect = input.getBoundingClientRect();
            Assert.Equal(100, timeRect.width);
            Assert.Equal(100, formRect.width);
            Assert.Equal(98, middleRect.width);
            Assert.Equal(98, inputRect.width);

            // Exercise the legacy/native panel algorithms explicitly as well
            // as the production portable descendant path used above.
            foreach (var panel in new[]
                     {
                         Assert.IsType<CssLayoutPanel>(grid.Control),
                         Assert.IsType<CssLayoutPanel>(time.Control),
                         Assert.IsType<CssLayoutPanel>(formInput.Control),
                         Assert.IsType<CssLayoutPanel>(middleSlot.Control)
                     })
            {
                CssLayout.SetNativeLayoutHotPath(panel, true);
                panel.InvalidateMeasure();
                panel.InvalidateArrange();
            }
            root.InvalidateMeasure();
            root.InvalidateArrange();
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(100, formInput.getBoundingClientRect().width);
            Assert.Equal(98, middleSlot.getBoundingClientRect().width);
            Assert.Equal(98, input.getBoundingClientRect().width);

            var portable = AvaloniaCssLayoutProjection.Capture(
                Assert.IsType<CssLayoutPanel>(grid.Control),
                new Size(grid.getBoundingClientRect().width, grid.getBoundingClientRect().height));
            Assert.Equal(100, portable.GetBox(time.Control).BorderBox.Width);
            Assert.Equal(100, portable.GetBox(formInput.Control).BorderBox.Width);
            Assert.Equal(98, portable.GetBox(middleSlot.Control).BorderBox.Width);
            Assert.Equal(98, portable.GetBox(input.Control).BorderBox.Width);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void UnthemedTextInputUsesInheritedLineHeightAsItsIntrinsicHeight()
    {
        var root = new CssLayoutPanel { Width = 200, Height = 100 };
        var window = new Window { Width = 200, Height = 100, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .control {
                    display: inline-block;
                    font-size: 14px;
                    line-height: 18px;
                    max-width: 100px;
                }
                .shell { display: flex; }
                .shell input {
                    font-size: inherit;
                    line-height: inherit;
                    width: 100%;
                }
                """;
            document.head.appendChild(style);
            var control = HostTestUtilities.GetElement(document.createElement("div"));
            control.className = "control";
            var shell = HostTestUtilities.GetElement(document.createElement("div"));
            shell.className = "shell";
            var input = HostTestUtilities.GetElement(document.createElement("input"));
            input.value = "00:00";
            shell.appendChild(input);
            control.appendChild(shell);
            HostTestUtilities.GetElement(document.body).appendChild(control);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var inputRect = input.getBoundingClientRect();
            Assert.True(inputRect.width >= 70);
            Assert.Equal(18, inputRect.height);
            Assert.Equal(18, CssLayout.GetLineHeight(input.Control));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void FlexMarginInlineStartAutoPushesDialogButtonsToTheInlineEndInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 500, Height = 100 };
        var window = new Window
        {
            Width = 500,
            Height = 100,
            Content = root
        };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .footer {
                    display: flex;
                    height: 34px;
                    width: 340px;
                }
                .defaults {
                    flex-shrink: 0;
                    height: 34px;
                    width: 100px;
                }
                .buttons {
                    flex-shrink: 0;
                    height: 34px;
                    margin-inline-start: auto;
                    width: 130px;
                }
                """;
            document.head.appendChild(style);

            var footer = HostTestUtilities.GetElement(document.createElement("div"));
            footer.className = "footer";
            var defaults = HostTestUtilities.GetElement(document.createElement("div"));
            defaults.className = "defaults";
            var buttons = HostTestUtilities.GetElement(document.createElement("div"));
            buttons.className = "buttons";
            footer.appendChild(defaults);
            footer.appendChild(buttons);
            HostTestUtilities.GetElement(document.body).appendChild(footer);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var footerRect = footer.getBoundingClientRect();
            var defaultsRect = defaults.getBoundingClientRect();
            var buttonsRect = buttons.getBoundingClientRect();
            Assert.Equal("210px", document.getComputedStyle(buttons).getPropertyValue("margin-left"));
            Assert.Equal(0, defaultsRect.left - footerRect.left);
            Assert.Equal(210, buttonsRect.left - footerRect.left);
            Assert.Equal(footerRect.right, buttonsRect.right);

            var portable = AvaloniaCssLayoutProjection.Capture(
                Assert.IsType<CssLayoutPanel>(footer.Control),
                new Size(footerRect.width, footerRect.height));
            var portableFooter = portable.GetBox(footer.Control).BorderBox;
            var portableButtons = portable.GetBox(buttons.Control).BorderBox;
            Assert.Equal(210, portableButtons.X - portableFooter.X);
            Assert.Equal(portableFooter.Right, portableButtons.Right);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void AbsoluteAutoWidthIncludesChildMinimumWhenMinimumExceedsMaximum()
    {
        var root = new CssLayoutPanel { Width = 500, Height = 220 };
        var window = new Window { Width = 500, Height = 220, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);

        try
        {
            var document = host.Document;
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                #outer { position: absolute; height: 136px; }
                #inner { position: relative; height: 68px; min-width: 260px; max-width: 200px; }
                """;
            document.head.appendChild(style);
            var outer = HostTestUtilities.GetElement(document.createElement("div"));
            outer.id = "outer";
            var inner = HostTestUtilities.GetElement(document.createElement("div"));
            inner.id = "inner";
            outer.appendChild(inner);
            HostTestUtilities.GetElement(document.body).appendChild(outer);

            // This read occurs in the same task as the stylesheet and DOM
            // mutations. CSSOM must synchronously flush the used layout value.
            Assert.Equal("260px", document.getComputedStyle(outer).getPropertyValue("width"));
            Assert.Equal(260, outer.getBoundingClientRect().width);
            Assert.Equal(260, inner.getBoundingClientRect().width);

            var portable = AvaloniaCssLayoutProjection.Capture(root, root.Bounds.Size);
            Assert.Equal(260, portable.GetBox(outer.Control).BorderBox.Width);
            Assert.Equal(260, portable.GetBox(inner.Control).BorderBox.Width);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void UnthemedDomTextInputPaintsItsCurrentValue()
    {
        var input = new DomTextInputControl
        {
            Width = 160,
            Height = 24,
            Text = "Volume",
            FontSize = 14,
            Foreground = Brushes.White,
            Background = Brushes.Transparent
        };
        var surface = new Canvas
        {
            Width = 180,
            Height = 40,
            Background = Brushes.Black
        };
        Canvas.SetLeft(input, 10);
        Canvas.SetTop(input, 8);
        surface.Children.Add(input);
        var window = new Window
        {
            Width = 180,
            Height = 40,
            Content = surface
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var brightPixels = 0;
            for (var offset = 0; offset < pixels.Length; offset += 4)
            {
                if (pixels[offset] > 96 || pixels[offset + 1] > 96 || pixels[offset + 2] > 96)
                {
                    brightPixels++;
                }
            }

            Assert.True(brightPixels > 20, $"Expected rendered input glyphs, found {brightPixels} bright pixels.");
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [Theory]
    [InlineData("calc(calc(max(0, 1 - (3 - 3) * (3 - 3)))*38px + calc(max(0, 1 - (3 - 4) * (3 - 4)))*54px + calc(max(0, 1 - (3 - 5) * (3 - 5)))*64px)", 38)]
    [InlineData("calc(calc(max(0, 1 - (3 - 3) * (3 - 3)))*20px + calc(max(0, 1 - (3 - 4) * (3 - 4)))*28px + calc(max(0, 1 - (3 - 5) * (3 - 5)))*34px)", 20)]
    [InlineData("calc(20px - 3px * 2)", 14)]
    public void NestedCalcAndMaxResolveAbsolutePixelLengths(string css, double expected)
    {
        Assert.True(CssLayout.TryParseLength(css, out var length));
        Assert.Equal(new CssLength(expected, CssLengthUnit.Pixel), length);
    }

    [AvaloniaFact]
    public void PercentageRadiusOnAbsoluteGeneratedPseudoElementPaintsCircle()
    {
        var marker = new CssLayoutPanel
        {
            Width = 18,
            Height = 18,
            Background = Brushes.Transparent
        };
        marker.SetGeneratedPseudoElement("before", new Dictionary<string, string>
        {
            ["content"] = "\"\"",
            ["position"] = "absolute",
            ["background-color"] = "#08a081",
            ["left"] = "0",
            ["right"] = "0",
            ["top"] = "0",
            ["bottom"] = "0",
            ["border-radius"] = "50%"
        });
        marker.RefreshGeneratedPseudoElements(new Size(18, 18));
        var window = new Window
        {
            Width = 18,
            Height = 18,
            Background = Brushes.Black,
            Content = marker
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var stride = frame.PixelSize.Width * 4;
            var corner = 0;
            var center = 9 * stride + 9 * 4;

            Assert.True(pixels[corner] < 16 && pixels[corner + 1] < 16 && pixels[corner + 2] < 16,
                "A 50% pseudo-element radius must leave the marker corner transparent.");
            Assert.True(pixels[center + 1] > 96,
                "The center of the generated marker must retain its green background.");
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void RoundedOverflowClipsSquareChildToTheParentsCornerRadius()
    {
        var marker = new CssLayoutPanel
        {
            Width = 18,
            Height = 18,
            ClipToBounds = true,
            CornerRadius = new CornerRadius(9),
            Background = Brushes.Transparent
        };
        marker.Children.Add(new Border
        {
            Width = 18,
            Height = 18,
            Background = new SolidColorBrush(Color.Parse("#22ab94"))
        });
        var window = new Window
        {
            Width = 18,
            Height = 18,
            Background = Brushes.Black,
            Content = marker
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var stride = frame.PixelSize.Width * 4;
            var corner = 0;
            var center = 9 * stride + 9 * 4;

            Assert.True(pixels[corner] < 16 && pixels[corner + 1] < 16 && pixels[corner + 2] < 16,
                "overflow:hidden must clip child paint to the parent's rounded border box.");
            Assert.True(pixels[center + 1] > 96,
                "Rounded overflow clipping must preserve the center of the child paint.");
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void GeneratedPseudoElementOpacityCompositesItsBackground()
    {
        var marker = new CssLayoutPanel
        {
            Width = 18,
            Height = 18,
            Background = Brushes.Transparent
        };
        marker.SetGeneratedPseudoElement("after", new Dictionary<string, string>
        {
            ["content"] = "\"\"",
            ["position"] = "absolute",
            ["background-color"] = "#22ab94",
            ["opacity"] = ".15",
            ["left"] = "0",
            ["right"] = "0",
            ["top"] = "0",
            ["bottom"] = "0"
        });
        marker.RefreshGeneratedPseudoElements(new Size(18, 18));
        var window = new Window
        {
            Width = 18,
            Height = 18,
            Background = Brushes.Black,
            Content = marker
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            using var frame = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            var pixels = CopyPixels(frame);
            var stride = frame.PixelSize.Width * 4;
            var center = 9 * stride + 9 * 4;

            Assert.InRange(pixels[center + 1], (byte)10, (byte)60);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void DomLoadingOverlayAnimatesWithoutChangingLayoutHitTestingOrDomChildren()
    {
        var child = new Border { Background = Brushes.Crimson };
        var surface = new CssLayoutPanel
        {
            Width = 320,
            Height = 180,
            Background = Brushes.Black,
            ClipToBounds = true
        };
        surface.Children.Add(child);
        var window = new Window
        {
            Width = 320,
            Height = 180,
            Content = surface
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var childBounds = child.Bounds;
            var domControlCount = AvaloniaDomDocument.Traverse(surface).Count();
            using var before = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());

            surface.LoadingOverlayText = "Compiling JavaScript";
            surface.IsLoadingOverlayVisible = true;
            Dispatcher.UIThread.RunJobs();
            using var visible = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());

            Assert.True(surface.IsLoadingOverlayAttached);
            Assert.Equal(domControlCount, AvaloniaDomDocument.Traverse(surface).Count());
            Assert.Equal(childBounds, child.Bounds);
            Assert.True(surface.HitTest(new Point(10, 10)));
            Assert.False(CopyPixels(before).SequenceEqual(CopyPixels(visible)));

            Assert.True(surface.IsLoadingOverlayAnimationRunning);
            surface.AdvanceLoadingOverlayFrameForTest();
            Assert.NotEqual(0, surface.LoadingOverlayFrame);

            surface.IsLoadingOverlayVisible = false;
            Dispatcher.UIThread.RunJobs();
            using var after = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            Assert.False(surface.IsLoadingOverlayAttached);
            Assert.Equal(domControlCount, AvaloniaDomDocument.Traverse(surface).Count());
            Assert.Equal(childBounds, child.Bounds);
            Assert.Equal(CopyPixels(before), CopyPixels(after));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void NestedClippedAndPositionedLayersRenderIdenticallyAfterShrinkAndGrow()
    {
        var root = new CssLayoutPanel
        {
            Width = 1000,
            Height = 616,
            ClipToBounds = true,
            Background = Brushes.Black
        };
        var frame = new CssLayoutPanel { ClipToBounds = true };
        var body = new CssLayoutPanel { ClipToBounds = false };
        var viewport = new CssLayoutPanel
        {
            ClipToBounds = true,
            Background = new SolidColorBrush(Color.Parse("#101010"))
        };
        var toolbar = new CssLayoutPanel
        {
            ClipToBounds = true,
            Background = new SolidColorBrush(Color.Parse("#202020"))
        };
        var plot = new CssLayoutPanel
        {
            ClipToBounds = true,
            Background = new SolidColorBrush(Color.Parse("#111827"))
        };
        root.Children.Add(frame);
        frame.Children.Add(body);
        body.Children.Add(viewport);
        viewport.Children.Add(toolbar);
        viewport.Children.Add(plot);

        CssLayout.SetWidth(frame, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetHeight(frame, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetWidth(body, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetHeight(body, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetWidth(viewport, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetHeight(viewport, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetDocumentViewportRoot(body, true);
        CssLayout.SetHeight(toolbar, new CssLength(48, CssLengthUnit.Pixel));
        CssLayout.SetPosition(plot, CssPosition.Absolute);
        CssLayout.SetLeft(plot, new CssLength(60, CssLengthUnit.Pixel));
        CssLayout.SetRight(plot, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetTop(plot, new CssLength(48, CssLengthUnit.Pixel));
        CssLayout.SetBottom(plot, new CssLength(0, CssLengthUnit.Pixel));

        for (var index = 0; index < 12; index++)
        {
            var item = new Border
            {
                Background = index % 2 == 0 ? Brushes.Teal : Brushes.Crimson,
                Child = new TextBlock
                {
                    Text = $"Layer {index}",
                    Foreground = Brushes.White,
                    FontSize = 16
                }
            };
            CssLayout.SetPosition(item, CssPosition.Absolute);
            CssLayout.SetLeft(item, new CssLength(20 + index * 70, CssLengthUnit.Pixel));
            CssLayout.SetTop(item, new CssLength(20 + index * 32, CssLengthUnit.Pixel));
            CssLayout.SetWidth(item, new CssLength(120, CssLengthUnit.Pixel));
            CssLayout.SetHeight(item, new CssLength(26, CssLengthUnit.Pixel));
            plot.Children.Add(item);
        }

        var window = new Window
        {
            Width = 1000,
            Height = 616,
            Content = root
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var beforePlotBounds = plot.Bounds;
            using var before = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());

            Resize(window, root, 640, 480);
            Assert.Equal(new Rect(60, 48, 580, 432), plot.Bounds);
            Resize(window, root, 1000, 616);

            using var after = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            Assert.Equal(beforePlotBounds, plot.Bounds);
            Assert.Equal(before.PixelSize, after.PixelSize);
            Assert.Equal(CopyPixels(before), CopyPixels(after));
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void DispatcherRemovedOverflowPortalDoesNotCorruptSiblingDuringResize()
    {
        var root = new CssLayoutPanel
        {
            Width = 1000,
            Height = 616,
            ClipToBounds = true,
            Background = Brushes.Black
        };
        var plot = new CssLayoutPanel
        {
            ClipToBounds = true,
            Background = new SolidColorBrush(Color.Parse("#111827"))
        };
        root.Children.Add(plot);
        CssLayout.SetDocumentViewportRoot(root, true);
        CssLayout.SetPosition(plot, CssPosition.Absolute);
        CssLayout.SetLeft(plot, new CssLength(56, CssLengthUnit.Pixel));
        CssLayout.SetRight(plot, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetTop(plot, new CssLength(42, CssLengthUnit.Pixel));
        CssLayout.SetBottom(plot, new CssLength(39, CssLengthUnit.Pixel));

        for (var index = 0; index < 10; index++)
        {
            var bar = new Border
            {
                Background = index % 2 == 0 ? Brushes.Teal : Brushes.Crimson
            };
            CssLayout.SetPosition(bar, CssPosition.Absolute);
            CssLayout.SetLeft(bar, new CssLength(20 + index * 75, CssLengthUnit.Pixel));
            CssLayout.SetBottom(bar, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetWidth(bar, new CssLength(42, CssLengthUnit.Pixel));
            CssLayout.SetHeight(bar, new CssLength(80 + index * 24, CssLengthUnit.Pixel));
            plot.Children.Add(bar);
        }

        var window = new Window
        {
            Width = 1000,
            Height = 616,
            Content = root
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var beforePlotBounds = plot.Bounds;
            var beforeBarBounds = plot.Children.Select(child => child.Bounds).ToArray();
            using var before = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());

            var portal = new CssLayoutPanel { ClipToBounds = false };
            var hint = new Border
            {
                Background = new SolidColorBrush(Color.Parse("#2962ff"))
            };
            portal.Children.Add(hint);
            root.Children.Add(portal);
            CssLayout.SetPosition(portal, CssPosition.Absolute);
            CssLayout.SetLeft(portal, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetTop(portal, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetWidth(portal, new CssLength(100, CssLengthUnit.Percent));
            CssLayout.SetHeight(portal, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetPosition(hint, CssPosition.Absolute);
            CssLayout.SetLeft(hint, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetTop(hint, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetWidth(hint, new CssLength(252, CssLengthUnit.Pixel));
            CssLayout.SetHeight(hint, new CssLength(83, CssLengthUnit.Pixel));
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(new Rect(0, 0, 1000, 0), portal.Bounds);
            Assert.Equal(new Rect(0, 0, 252, 83), hint.Bounds);

            window.Width = root.Width = 640;
            window.Height = root.Height = 480;
            root.InvalidateMeasure();
            root.InvalidateArrange();
            Dispatcher.UIThread.Post(
                () => root.Children.Remove(portal),
                DispatcherPriority.Default);
            Dispatcher.UIThread.RunJobs();
            using var shrunken = window.CaptureRenderedFrame();
            Assert.NotNull(shrunken);

            Resize(window, root, 1000, 616);
            using var after = Assert.IsAssignableFrom<Bitmap>(window.CaptureRenderedFrame());
            Assert.Equal(beforePlotBounds, plot.Bounds);
            Assert.Equal(beforeBarBounds, plot.Children.Select(child => child.Bounds).ToArray());
            Assert.Equal(before.PixelSize, after.PixelSize);
            Assert.Equal(CopyPixels(before), CopyPixels(after));
        }
        finally
        {
            window.Close();
        }
    }

    private static void Resize(Window window, Control root, double width, double height)
    {
        window.Width = width;
        window.Height = height;
        root.Width = width;
        root.Height = height;
        root.InvalidateMeasure();
        root.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
        using var frame = window.CaptureRenderedFrame();
        Assert.NotNull(frame);
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

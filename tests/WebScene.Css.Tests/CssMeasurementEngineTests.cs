using WebScene.Core;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssMeasurementEngineTests
{
    [Fact]
    public void BlockMeasurementOwnsBoxSizingChromeAndMargins()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Padding = CssLayoutEdges.All(CssLayoutLength.Pixels(10)),
            Border = CssLayoutEdges.All(CssLayoutLength.Pixels(2))
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(20),
            Padding = CssLayoutEdges.All(CssLayoutLength.Pixels(5)),
            Border = CssLayoutEdges.All(CssLayoutLength.Pixels(1)),
            Margin = CssLayoutEdges.All(CssLayoutLength.Pixels(3))
        }));
        var measurer = new RecordingMeasurer();

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(200, 100), measurer);

        Assert.Equal(new WebSceneSize(92, 62), desired);
        Assert.Equal(new WebSceneSize(62, 32), measurer.Constraints[2]);
    }

    [Fact]
    public void InlineRunsWrapWhileNoWrapKeepsOneLine()
    {
        var root = new CssLayoutNode(1);
        root.Add(Inline(2));
        root.Add(Inline(3));
        root.Add(Inline(4));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(60, 10)),
            (3, new WebSceneSize(60, 12)), (4, new WebSceneSize(20, 8)));
        var engine = new CssMeasurementEngine();

        Assert.Equal(new WebSceneSize(80, 22), engine.Measure(root, new WebSceneSize(100, 100), measurer));

        var noWrap = new CssLayoutNode(10, new CssLayoutStyle { NoWrap = true });
        noWrap.Add(Inline(11));
        noWrap.Add(Inline(12));
        var noWrapMeasurer = new RecordingMeasurer(
            (11, new WebSceneSize(60, 10)), (12, new WebSceneSize(60, 12)));
        Assert.Equal(new WebSceneSize(100, 12), engine.Measure(noWrap, new WebSceneSize(100, 100), noWrapMeasurer));
    }

    [Fact]
    public void CollapsedWhitespaceBetweenInlineSiblingsContributesOneAdvance()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle { NoWrap = true });
        root.Add(Inline(2));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle { Display = CssLayoutDisplay.Inline })
        {
            IsText = true,
            IsCollapsibleWhitespace = true,
            CollapsedWhitespaceWidth = 5
        });
        root.Add(Inline(4));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(20, 10)),
            (4, new WebSceneSize(20, 10)));

        var desired = new CssMeasurementEngine().Measure(
            root,
            new WebSceneSize(100, 100),
            measurer);

        Assert.Equal(new WebSceneSize(45, 10), desired);
    }

    [Fact]
    public void PercentageAgainstInfiniteAvailableSizeIsMeasuredAsAuto()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Percent(50),
            Height = CssLayoutLength.Percent(25)
        }));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(70, 20)));

        var desired = new CssMeasurementEngine().Measure(
            root,
            new WebSceneSize(double.PositiveInfinity, double.PositiveInfinity),
            measurer);

        Assert.Equal(new WebSceneSize(70, 20), desired);
        Assert.True(double.IsPositiveInfinity(measurer.Constraints[2].Width));
        Assert.True(double.IsPositiveInfinity(measurer.Constraints[2].Height));
    }

    [Fact]
    public void MinimumWidthWinsWhenItExceedsMaximumWidth()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            MinWidth = CssLayoutLength.Pixels(260),
            MaxWidth = CssLayoutLength.Pixels(200),
            Height = CssLayoutLength.Pixels(68)
        }));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(260, 68)));

        var desired = new CssMeasurementEngine().Measure(
            root,
            new WebSceneSize(double.PositiveInfinity, double.PositiveInfinity),
            measurer);

        Assert.Equal(260, measurer.Constraints[2].Width);
        Assert.Equal(new WebSceneSize(260, 68), desired);
    }

    [Fact]
    public void PositionedChildrenAreMeasuredButExcludedFromDesiredSize()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Position = CssLayoutPosition.Absolute
        }));
        root.Add(new CssLayoutNode(3));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(300, 200)), (3, new WebSceneSize(40, 15)));

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(500, 500), measurer);

        Assert.Equal(new WebSceneSize(40, 15), desired);
        Assert.True(double.IsPositiveInfinity(measurer.Constraints[2].Width));
        Assert.True(double.IsPositiveInfinity(measurer.Constraints[2].Height));
    }

    [Fact]
    public void FlexMeasurementUsesBasisMarginsGapAndCrossSize()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            ColumnGap = CssLayoutLength.Pixels(8)
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            FlexBasis = CssLayoutLength.Pixels(30),
            Margin = CssLayoutEdges.All(CssLayoutLength.Pixels(2))
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(40),
            Height = CssLayoutLength.Pixels(18)
        }));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(20, 12)));

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(200, 100), measurer);

        Assert.Equal(new WebSceneSize(82, 18), desired);
    }

    [Fact]
    public void FlexMeasurementAccumulatesWrappedLineCrossSizesAndCrossGap()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            FlexWrap = CssLayoutFlexWrap.Wrap,
            ColumnGap = CssLayoutLength.Pixels(5),
            RowGap = CssLayoutLength.Pixels(3)
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(60),
            Height = CssLayoutLength.Pixels(10)
        }));
        root.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(12)
        }));

        var desired = new CssMeasurementEngine().Measure(
            root,
            new WebSceneSize(100, 100),
            new RecordingMeasurer());

        Assert.Equal(new WebSceneSize(60, 25), desired);
    }

    [Fact]
    public void GridSumsFlowWidthsAndIgnoresHiddenAndPositionedItems()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Grid });
        root.Add(new CssLayoutNode(2));
        root.Add(new CssLayoutNode(3));
        root.Add(new CssLayoutNode(4, new CssLayoutStyle { Display = CssLayoutDisplay.None }));
        root.Add(new CssLayoutNode(5, new CssLayoutStyle { Position = CssLayoutPosition.Fixed }));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(30, 10)),
            (3, new WebSceneSize(40, 20)),
            (5, new WebSceneSize(500, 500)));

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(200, 100), measurer);

        Assert.Equal(new WebSceneSize(70, 20), desired);
        Assert.DoesNotContain(4, measurer.Constraints.Keys);
        Assert.DoesNotContain(5, measurer.Constraints.Keys);
    }

    [Fact]
    public void AutoFractionGridMeasuresPairedRowsAndFullWidthSpans()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.InlineGrid,
            GridTemplateColumns = "auto 1fr"
        });
        root.Add(new CssLayoutNode(2));
        root.Add(new CssLayoutNode(3));
        root.Add(new CssLayoutNode(4, new CssLayoutStyle { GridColumn = "1 / -1" }));
        root.Add(new CssLayoutNode(5));
        root.Add(new CssLayoutNode(6));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(80, 34)),
            (3, new WebSceneSize(100, 34)),
            (4, new WebSceneSize(160, 18)),
            (5, new WebSceneSize(70, 50)),
            (6, new WebSceneSize(90, 50)));

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(200, 200), measurer);

        Assert.Equal(new WebSceneSize(180, 102), desired);
    }

    [Fact]
    public void FixedPixelGridTracksConstrainAutoChildrenAndIncludeColumnGap()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Grid,
            GridTemplateColumns = "150px 100px",
            ColumnGap = CssLayoutLength.Pixels(12)
        });
        root.Add(new CssLayoutNode(2));
        root.Add(new CssLayoutNode(3));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(40, 28)),
            (3, new WebSceneSize(20, 28)));

        var desired = new CssMeasurementEngine().Measure(root, new WebSceneSize(262, 100), measurer);

        Assert.Equal(new WebSceneSize(262, 28), desired);
        Assert.Equal(150, measurer.Constraints[2].Width);
        Assert.Equal(100, measurer.Constraints[3].Width);
    }

    [Fact]
    public void TableMeasurementSharesIntrinsicColumnWidthsAcrossRows()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        var body = new CssLayoutNode(2, new CssLayoutStyle { Display = CssLayoutDisplay.TableRowGroup });
        var firstRow = new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableRow,
            Height = CssLayoutLength.Pixels(32)
        });
        firstRow.Add(new CssLayoutNode(4, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell }));
        firstRow.Add(new CssLayoutNode(5, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell }));
        var secondRow = new CssLayoutNode(6, new CssLayoutStyle { Display = CssLayoutDisplay.TableRow });
        secondRow.Add(new CssLayoutNode(7, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell }));
        secondRow.Add(new CssLayoutNode(8, new CssLayoutStyle { Display = CssLayoutDisplay.TableCell }));
        body.Add(firstRow);
        body.Add(secondRow);
        table.Add(body);
        var measurer = new RecordingMeasurer(
            (4, new WebSceneSize(36, 20)),
            (5, new WebSceneSize(80, 18)),
            (7, new WebSceneSize(6, 13)),
            (8, new WebSceneSize(218, 13)));

        var desired = new CssMeasurementEngine().Measure(
            table,
            new WebSceneSize(220, double.PositiveInfinity),
            measurer);

        Assert.Equal(new WebSceneSize(254, 45), desired);
        Assert.Equal(36, measurer.Constraints[4].Width);
        Assert.Equal(218, measurer.Constraints[5].Width);
        Assert.Equal(36, measurer.Constraints[7].Width);
        Assert.Equal(218, measurer.Constraints[8].Width);
    }

    [Fact]
    public void TableMeasurementGeneratesImplicitRowForDirectCells()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        table.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(3.6)
        }));
        table.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(3.6)
        }));
        var measurer = new RecordingMeasurer(
            (2, new WebSceneSize(0, 20)),
            (3, new WebSceneSize(0, 20)));

        var desired = new CssMeasurementEngine().Measure(
            table,
            new WebSceneSize(100, double.PositiveInfinity),
            measurer);

        Assert.Equal(7.2, desired.Width, precision: 8);
        Assert.Equal(20, desired.Height);
        Assert.Equal(3.6, measurer.Constraints[2].Width, precision: 8);
        Assert.Equal(3.6, measurer.Constraints[3].Width, precision: 8);
    }

    [Fact]
    public void ImplicitTableRowSolvesPercentageTrackAgainstShrinkToFitTableWidth()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        table.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Percent(50),
            Height = CssLayoutLength.Pixels(0)
        }));
        table.Add(new CssLayoutNode(3, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.TableCell,
            Width = CssLayoutLength.Pixels(50),
            Height = CssLayoutLength.Pixels(100)
        }));
        var measurer = new RecordingMeasurer();

        var desired = new CssMeasurementEngine().Measure(
            table,
            new WebSceneSize(400, double.PositiveInfinity),
            measurer);

        Assert.Equal(new WebSceneSize(100, 100), desired);
        Assert.Equal(50, measurer.Constraints[2].Width);
        Assert.Equal(50, measurer.Constraints[3].Width);
    }

    [Fact]
    public void TableMeasurementGeneratesAnonymousWrappersForImproperFlexChild()
    {
        var table = new CssLayoutNode(1, new CssLayoutStyle { Display = CssLayoutDisplay.Table });
        table.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            Height = CssLayoutLength.Pixels(38)
        }));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(240, 38)));

        var desired = new CssMeasurementEngine().Measure(
            table,
            new WebSceneSize(500, 38),
            measurer);

        Assert.Equal(new WebSceneSize(240, 38), desired);
        Assert.Equal(500, measurer.Constraints[2].Width);
        Assert.Equal(38, measurer.Constraints[2].Height);
    }

    [Theory]
    [InlineData(CssLayoutDisplay.TableColumn)]
    [InlineData(CssLayoutDisplay.TableColumnGroup)]
    public void TableTrackBoxesSuppressDescendantMeasurement(CssLayoutDisplay display)
    {
        var track = new CssLayoutNode(1, new CssLayoutStyle { Display = display });
        track.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(100),
            Height = CssLayoutLength.Pixels(100)
        }));
        var measurer = new RecordingMeasurer((2, new WebSceneSize(100, 100)));

        var desired = new CssMeasurementEngine().Measure(
            track,
            new WebSceneSize(200, 200),
            measurer);

        Assert.Equal(WebSceneSize.Empty, desired);
        Assert.Empty(measurer.Constraints);
    }

    [Fact]
    public void ZeroLineHeightAndInvalidInputsAreHandledDeterministically()
    {
        var root = new CssLayoutNode(1);
        root.Add(new CssLayoutNode(2) { HasZeroLineHeight = true });
        var measurer = new RecordingMeasurer((2, new WebSceneSize(25, 40)));
        var engine = new CssMeasurementEngine();

        Assert.Equal(new WebSceneSize(25, 0), engine.Measure(root, new WebSceneSize(100, 100), measurer));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            engine.Measure(root, new WebSceneSize(double.NaN, 1), measurer));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            engine.Measure(root, new WebSceneSize(-1, 1), measurer));
        Assert.Throws<ArgumentNullException>(() => engine.Measure(null!, new WebSceneSize(1, 1), measurer));
        Assert.Throws<ArgumentNullException>(() => engine.Measure(root, new WebSceneSize(1, 1), null!));
    }

    private static CssLayoutNode Inline(long id)
        => new(id, new CssLayoutStyle { Display = CssLayoutDisplay.InlineBlock });

    private sealed class RecordingMeasurer(params (long Id, WebSceneSize Size)[] sizes) : ICssIntrinsicMeasurer
    {
        private readonly Dictionary<long, WebSceneSize> _sizes = sizes.ToDictionary(static item => item.Id, static item => item.Size);

        public Dictionary<long, WebSceneSize> Constraints { get; } = [];

        public WebSceneSize Measure(long nodeId, WebSceneSize availableSize)
        {
            Constraints[nodeId] = availableSize;
            return _sizes.GetValueOrDefault(nodeId);
        }
    }
}

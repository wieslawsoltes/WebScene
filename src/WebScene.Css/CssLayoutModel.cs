using WebScene.Core;

namespace WebScene.Css;

public enum CssLayoutDisplay
{
    Block,
    Inline,
    InlineBlock,
    Flex,
    Grid,
    InlineFlex,
    InlineGrid,
    Table,
    InlineTable,
    TableRowGroup,
    TableHeaderGroup,
    TableFooterGroup,
    TableRow,
    TableCell,
    TableColumnGroup,
    TableColumn,
    TableCaption,
    ListItem,
    None
}

public readonly record struct CssLayoutListMarker(
    long Id,
    WebSceneSize IntrinsicSize,
    double LineHeight,
    double InlineAdvance,
    bool Outside);

public enum CssLayoutVerticalAlign
{
    Baseline,
    Top,
    Middle,
    Bottom
}

public enum CssLayoutPosition
{
    Static,
    Relative,
    Absolute,
    Fixed
}

public enum CssLayoutOverflow
{
    Visible,
    Hidden,
    Clip,
    Auto,
    Scroll
}

public enum CssLayoutBoxSizing
{
    ContentBox,
    BorderBox
}

public enum CssTableColumnWidthKind
{
    Auto,
    Length,
    Percent,
    Zero
}

public readonly record struct CssTableColumnWidthConstraint(
    CssTableColumnWidthKind Kind,
    double Weight);

public enum CssLayoutFlexDirection
{
    Row,
    RowReverse,
    Column,
    ColumnReverse
}

public enum CssLayoutFlexWrap
{
    NoWrap,
    Wrap,
    WrapReverse
}

public enum CssLayoutAlignContent
{
    Stretch,
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
}

public enum CssLayoutJustifyContent
{
    FlexStart,
    FlexEnd,
    Center,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
}

public enum CssLayoutAlignment
{
    Auto,
    Stretch,
    FlexStart,
    FlexEnd,
    Center,
    Baseline
}

public enum CssLayoutLengthUnit
{
    Pixel,
    Percent,
    Auto
}

/// <summary>
/// A deferred CSS length. Percentage and pixel terms are retained independently
/// so values such as <c>calc(50% - 14px)</c> resolve against the correct containing box.
/// </summary>
public readonly record struct CssLayoutLength(double Value, CssLayoutLengthUnit Unit)
{
    public bool IsAuto => Unit == CssLayoutLengthUnit.Auto;

    public double PixelOffset { get; init; }

    public double? Resolve(double reference)
        => IsAuto
            ? null
            : Unit == CssLayoutLengthUnit.Percent
                ? reference * Value / 100d + PixelOffset
                : Value;

    public static CssLayoutLength Pixels(double value) => new(value, CssLayoutLengthUnit.Pixel);

    public static CssLayoutLength Percent(double value, double pixelOffset = 0)
        => new(value, CssLayoutLengthUnit.Percent) { PixelOffset = pixelOffset };

    public static CssLayoutLength Auto { get; } = new(0, CssLayoutLengthUnit.Auto);
}

public readonly record struct CssLayoutEdges(
    CssLayoutLength Top,
    CssLayoutLength Right,
    CssLayoutLength Bottom,
    CssLayoutLength Left)
{
    public static CssLayoutEdges Zero { get; } = All(CssLayoutLength.Pixels(0));

    public static CssLayoutEdges All(CssLayoutLength value) => new(value, value, value, value);

    internal ResolvedCssEdges Resolve(double widthReference, double heightReference)
        => new(
            Top.Resolve(heightReference) ?? 0,
            Right.Resolve(widthReference) ?? 0,
            Bottom.Resolve(heightReference) ?? 0,
            Left.Resolve(widthReference) ?? 0);
}

public sealed record CssLayoutStyle
{
    public CssLayoutDisplay Display { get; init; } = CssLayoutDisplay.Block;

    public bool FixedTableLayout { get; init; }

    public CssLayoutPosition Position { get; init; }

    public CssLayoutOverflow OverflowX { get; init; }

    public CssLayoutOverflow OverflowY { get; init; }

    public CssLayoutBoxSizing BoxSizing { get; init; }

    public CssLayoutFlexDirection FlexDirection { get; init; }

    public CssLayoutFlexWrap FlexWrap { get; init; }

    public CssLayoutAlignContent AlignContent { get; init; }

    public CssLayoutJustifyContent JustifyContent { get; init; }

    public CssLayoutAlignment AlignItems { get; init; } = CssLayoutAlignment.Stretch;

    public CssLayoutAlignment AlignSelf { get; init; } = CssLayoutAlignment.Auto;

    public CssLayoutVerticalAlign VerticalAlign { get; init; }

    public bool NoWrap { get; init; }

    /// <summary>
    /// True when an auto-height normal-flow containing block has no definite
    /// height against which in-flow percentage-height children can resolve.
    /// </summary>
    public bool PercentageHeightBasisIsIndefinite { get; init; }

    public CssLayoutLength Width { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength Height { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength MinWidth { get; init; } = CssLayoutLength.Pixels(0);

    public bool HasExplicitMinWidth { get; init; }

    public CssLayoutLength MinHeight { get; init; } = CssLayoutLength.Pixels(0);

    public bool HasExplicitMinHeight { get; init; }

    public CssLayoutLength MaxWidth { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength MaxHeight { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength Left { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength Top { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength Right { get; init; } = CssLayoutLength.Auto;

    public CssLayoutLength Bottom { get; init; } = CssLayoutLength.Auto;

    /// <summary>
    /// Used translation applied after positioned-box size and inset resolution.
    /// Percentage values resolve against the positioned box itself.
    /// </summary>
    public CssLayoutLength TranslateX { get; init; } = CssLayoutLength.Pixels(0);

    public CssLayoutLength TranslateY { get; init; } = CssLayoutLength.Pixels(0);

    public CssLayoutEdges Margin { get; init; } = CssLayoutEdges.Zero;

    public CssLayoutEdges Padding { get; init; } = CssLayoutEdges.Zero;

    public CssLayoutEdges Border { get; init; } = CssLayoutEdges.Zero;

    public CssLayoutLength RowGap { get; init; } = CssLayoutLength.Pixels(0);

    public CssLayoutLength ColumnGap { get; init; } = CssLayoutLength.Pixels(0);

    public string GridTemplateColumns { get; init; } = string.Empty;

    public string GridTemplateRows { get; init; } = string.Empty;

    public string GridColumn { get; init; } = string.Empty;

    public double FlexGrow { get; init; }

    public double FlexShrink { get; init; } = 1;

    public CssLayoutLength FlexBasis { get; init; } = CssLayoutLength.Auto;

    public int Order { get; init; }

    /// <summary>
    /// Generated marker geometry for a list-item principal box. The marker is
    /// not a child node and therefore never participates in DOM child layout.
    /// </summary>
    public CssLayoutListMarker? ListMarker { get; init; }
}

/// <summary>
/// Framework-neutral input tree. A backend supplies intrinsic sizes; all CSS box
/// geometry is produced by <see cref="CssMeasurementEngine"/> and
/// <see cref="CssArrangementEngine"/>.
/// </summary>
public sealed class CssLayoutNode
{
    private readonly List<CssLayoutNode> _children = [];

    public CssLayoutNode(long id, CssLayoutStyle? style = null)
    {
        if (id == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(id), "Layout node IDs must be non-zero.");
        }

        Id = id;
        Style = style ?? new CssLayoutStyle();
    }

    public long Id { get; }

    public CssLayoutStyle Style { get; }

    public WebSceneSize IntrinsicSize { get; init; }

    public bool IsText { get; init; }

    /// <summary>
    /// First typographic baseline measured from the node's border-box start edge.
    /// A null value asks layout to synthesize the baseline from the border-box end edge.
    /// </summary>
    public double? FirstBaseline { get; init; }

    public bool IsCollapsibleWhitespace { get; init; }

    public double CollapsedWhitespaceWidth { get; init; }

    public bool HasZeroLineHeight { get; init; }

    /// <summary>
    /// True for a generated line-break opportunity such as HTML <c>br</c>.
    /// The node has no principal dimensions but terminates the current inline
    /// line before subsequent content is placed.
    /// </summary>
    public bool ForcesLineBreak { get; init; }

    public bool CanBreakBefore { get; set; } = true;

    public IReadOnlyList<CssLayoutNode> Children => _children;

    public void Add(CssLayoutNode child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (ReferenceEquals(child, this))
        {
            throw new InvalidOperationException("A layout node cannot contain itself.");
        }

        _children.Add(child);
    }
}

public readonly record struct CssLayoutBox(
    WebSceneRect MarginBox,
    WebSceneRect BorderBox,
    WebSceneRect PaddingBox,
    WebSceneRect ContentBox);

public sealed class CssLayoutSnapshot
{
    private readonly Dictionary<long, CssLayoutBox> _boxes;

    internal CssLayoutSnapshot(Dictionary<long, CssLayoutBox> boxes, WebSceneSize desiredSize)
    {
        _boxes = boxes;
        DesiredSize = desiredSize;
    }

    public WebSceneSize DesiredSize { get; }

    public IReadOnlyDictionary<long, CssLayoutBox> Boxes => _boxes;

    public CssLayoutBox this[long id] => _boxes[id];
}

internal readonly record struct ResolvedCssEdges(double Top, double Right, double Bottom, double Left)
{
    public double Horizontal => Left + Right;

    public double Vertical => Top + Bottom;
}

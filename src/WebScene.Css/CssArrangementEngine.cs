using WebScene.Core;

namespace WebScene.Css;

/// <summary>
/// Final CSS arrangement pass. Intrinsic sizes come from the backend measurement
/// contract; this portable pass is the sole producer of content/padding/border/margin
/// geometry for block, inline, flex, grid and positioned children.
/// </summary>
public sealed class CssArrangementEngine
{
    public CssLayoutSnapshot Arrange(CssLayoutNode root, WebSceneSize finalBorderSize)
    {
        var viewport = new WebSceneRect(0, 0, finalBorderSize.Width, finalBorderSize.Height);
        return Arrange(root, finalBorderSize, viewport, viewport);
    }

    /// <summary>
    /// Arranges a projected subtree whose nearest absolute and fixed containing blocks
    /// may live above the projected root. All rectangles use the root's coordinate space.
    /// </summary>
    public CssLayoutSnapshot Arrange(
        CssLayoutNode root,
        WebSceneSize finalBorderSize,
        WebSceneRect inheritedAbsoluteContainingBlock,
        WebSceneRect fixedContainingBlock)
    {
        ArgumentNullException.ThrowIfNull(root);
        if (!IsValid(finalBorderSize))
        {
            throw new ArgumentOutOfRangeException(nameof(finalBorderSize));
        }
        if (!IsValid(inheritedAbsoluteContainingBlock))
        {
            throw new ArgumentOutOfRangeException(nameof(inheritedAbsoluteContainingBlock));
        }
        if (!IsValid(fixedContainingBlock))
        {
            throw new ArgumentOutOfRangeException(nameof(fixedContainingBlock));
        }

        var boxes = new Dictionary<long, CssLayoutBox>();
        var viewport = new WebSceneRect(0, 0, finalBorderSize.Width, finalBorderSize.Height);
        var rootBox = IsNonRenderedTableTrack(root.Style.Display)
            ? EmptyBox()
            : CreateBox(root, viewport, viewport.Size, includeMargin: false);
        boxes.Add(root.Id, rootBox);
        AddListMarkerBox(root, rootBox, boxes);
        ArrangeChildren(
            root,
            rootBox.ContentBox,
            rootBox.PaddingBox,
            inheritedAbsoluteContainingBlock,
            fixedContainingBlock,
            boxes,
            tableColumns: null,
            tableCellVerticalAlign: CssLayoutVerticalAlign.Baseline);
        return new CssLayoutSnapshot(boxes, finalBorderSize);
    }

    private static void ArrangeChildren(
        CssLayoutNode parent,
        WebSceneRect content,
        WebSceneRect paddingBox,
        WebSceneRect inheritedAbsoluteContainingBlock,
        WebSceneRect fixedContainingBlock,
        Dictionary<long, CssLayoutBox> boxes,
        IReadOnlyList<double>? tableColumns,
        CssLayoutVerticalAlign tableCellVerticalAlign)
    {
        var noBoxChildren = parent.Children
            .Where(child => IsSuppressedTableChild(parent, child)
                            || IsNonRenderedTableTrack(child.Style.Display))
            .ToHashSet();
        foreach (var child in noBoxChildren)
        {
            AddEmptySubtree(child, boxes);
        }

        var flow = parent.Children
            .Where(child => !noBoxChildren.Contains(child)
                            && (child.Style.Display != CssLayoutDisplay.None
                                || child.IsCollapsibleWhitespace)
                                   && child.Style.Position is CssLayoutPosition.Static or CssLayoutPosition.Relative)
            .Select(static (child, index) => new IndexedNode(child, index))
            .OrderBy(static item => item.Node.Style.Order)
            .ThenBy(static item => item.Index)
            .Select(static item => item.Node)
            .ToArray();
        var positioned = parent.Children
            .Where(child => !noBoxChildren.Contains(child)
                            && child.Style.Display != CssLayoutDisplay.None
                                   && child.Style.Position is CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)
            .Select(static (child, index) => new IndexedNode(child, index))
            .OrderBy(static item => item.Node.Style.Order)
            .ThenBy(static item => item.Index)
            .Select(static item => item.Node)
            .ToArray();
        var staticPositions = new Dictionary<long, WebScenePoint>();
        var descendantTableColumns = tableColumns;

        switch (parent.Style.Display)
        {
            case CssLayoutDisplay.Flex:
            case CssLayoutDisplay.InlineFlex:
                ArrangeFlex(flow, parent.Style, content, boxes);
                break;
            case CssLayoutDisplay.Grid:
            case CssLayoutDisplay.InlineGrid:
                ArrangeGrid(flow, parent.Style, content, boxes);
                break;
            case CssLayoutDisplay.Table:
            case CssLayoutDisplay.InlineTable:
                if (HasDirectTableModel(parent))
                {
                    descendantTableColumns = ResolveTableColumns(parent, content.Size);
                    ArrangeTable(parent, descendantTableColumns, content, boxes);
                }
                else
                {
                    ArrangeBlock(
                        parent.Children
                            .Where(child => !noBoxChildren.Contains(child)
                                            && (child.Style.Display != CssLayoutDisplay.None
                                                || child.IsCollapsibleWhitespace))
                            .ToArray(),
                        parent.Style,
                        content,
                        boxes,
                        staticPositions,
                        CssLayoutVerticalAlign.Top);
                }
                break;
            case CssLayoutDisplay.TableRowGroup:
            case CssLayoutDisplay.TableHeaderGroup:
            case CssLayoutDisplay.TableFooterGroup:
                descendantTableColumns ??= ResolveTableColumns(parent, content.Size);
                ArrangeTableRowGroup(parent, descendantTableColumns, content, boxes);
                break;
            case CssLayoutDisplay.TableRow:
                descendantTableColumns ??= ResolveTableColumns(parent, content.Size);
                ArrangeEffectiveTableRow(
                    CssTableModelFixup.CreateRow(parent, parent.Children),
                    descendantTableColumns,
                    content,
                    boxes);
                break;
            default:
                ArrangeBlock(
                    parent.Children
                        .Where(child => !noBoxChildren.Contains(child)
                                        && (child.Style.Display != CssLayoutDisplay.None
                                            || child.IsCollapsibleWhitespace))
                        .ToArray(),
                    parent.Style,
                    content,
                    boxes,
                    staticPositions,
                    parent.Style.Display == CssLayoutDisplay.TableCell
                        ? tableCellVerticalAlign
                        : CssLayoutVerticalAlign.Top);
                break;
        }

        var absoluteContainingBlock = parent.Style.Position == CssLayoutPosition.Static
            ? inheritedAbsoluteContainingBlock
            : paddingBox;
        foreach (var child in positioned)
        {
            ArrangePositioned(
                child,
                child.Style.Position == CssLayoutPosition.Fixed ? fixedContainingBlock : absoluteContainingBlock,
                boxes,
                staticPositions.TryGetValue(child.Id, out var staticPosition) ? staticPosition : null);
        }

        foreach (var child in parent.Children)
        {
            if (noBoxChildren.Contains(child))
            {
                continue;
            }
            if (!boxes.TryGetValue(child.Id, out var childBox)
                || child.Style.Display == CssLayoutDisplay.None)
            {
                if (child.Style.Display == CssLayoutDisplay.None)
                {
                    boxes.TryAdd(child.Id, new CssLayoutBox(
                        WebSceneRect.Empty,
                        WebSceneRect.Empty,
                        WebSceneRect.Empty,
                        WebSceneRect.Empty));
                }
                continue;
            }

            AddListMarkerBox(child, childBox, boxes);

            var childAbsoluteContainingBlock = child.Style.Position == CssLayoutPosition.Static
                ? absoluteContainingBlock
                : childBox.PaddingBox;
            ArrangeChildren(
                child,
                childBox.ContentBox,
                childBox.PaddingBox,
                childAbsoluteContainingBlock,
                fixedContainingBlock,
                boxes,
                descendantTableColumns,
                parent.Style.Display == CssLayoutDisplay.TableRow
                    || parent.Style.Display is CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable
                    && child.Style.Display == CssLayoutDisplay.TableCell
                    ? child.Style.VerticalAlign == CssLayoutVerticalAlign.Baseline
                        ? parent.Style.VerticalAlign
                        : child.Style.VerticalAlign
                    : tableCellVerticalAlign);
        }
    }

    private static void AddListMarkerBox(
        CssLayoutNode node,
        CssLayoutBox principal,
        IDictionary<long, CssLayoutBox> boxes)
    {
        if (node.Style.Display != CssLayoutDisplay.ListItem
            || node.Style.ListMarker is not { } marker
            || marker.IntrinsicSize.Width <= 0
            || marker.IntrinsicSize.Height <= 0)
        {
            return;
        }

        var markerOrigin = marker.Outside
            ? new WebScenePoint(principal.BorderBox.X, principal.ContentBox.Y)
            : new WebScenePoint(principal.ContentBox.X, principal.ContentBox.Y);
        var lineHeight = Math.Min(
            Math.Max(0, principal.ContentBox.Height),
            Math.Max(marker.IntrinsicSize.Height, marker.LineHeight));
        var rect = new WebSceneRect(
            marker.Outside
                ? principal.BorderBox.X - 9 - marker.IntrinsicSize.Width
                : markerOrigin.X,
            markerOrigin.Y + Math.Max(0, (lineHeight - marker.IntrinsicSize.Height) / 2),
            marker.IntrinsicSize.Width,
            marker.IntrinsicSize.Height);
        boxes[marker.Id] = new CssLayoutBox(rect, rect, rect, rect);
    }

    private static void ArrangeBlock(
        IReadOnlyList<CssLayoutNode> children,
        CssLayoutStyle parentStyle,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes,
        Dictionary<long, WebScenePoint> staticPositions,
        CssLayoutVerticalAlign verticalAlign)
    {
        var flowHeight = verticalAlign is CssLayoutVerticalAlign.Middle or CssLayoutVerticalAlign.Bottom
            ? ResolveBlockFlowHeight(children, content.Size)
            : 0;
        var verticalOffset = verticalAlign switch
        {
            CssLayoutVerticalAlign.Middle => Math.Max(0, (content.Height - flowHeight) / 2),
            CssLayoutVerticalAlign.Bottom => Math.Max(0, content.Height - flowHeight),
            _ => 0
        };
        var flowY = content.Y + verticalOffset;
        CssLayoutListMarker? insideMarker =
            parentStyle.Display == CssLayoutDisplay.ListItem
            && parentStyle.ListMarker is { Outside: false } marker
                ? marker
                : null;
        var inlineX = content.X + (insideMarker?.InlineAdvance ?? 0);
        var inlineLineHeight = insideMarker is { } initialMarker
            ? Math.Max(initialMarker.IntrinsicSize.Height, initialMarker.LineHeight)
            : 0;
        double? pendingBlockMargin = null;
        var canCollapseTopMargin = CanCollapseBlockChildMargin(parentStyle, top: true, content.Size);
        var ownMargin = parentStyle.Margin.Resolve(content.Width, content.Height);
        CssLayoutNode? previous = null;
        for (var childIndex = 0; childIndex < children.Count; childIndex++)
        {
            var child = children[childIndex];
            if (child.IsCollapsibleWhitespace)
            {
                if (HasInlineContentOnBothSides(children, childIndex))
                {
                    inlineX += child.CollapsedWhitespaceWidth;
                }
                continue;
            }
            if (child.ForcesLineBreak)
            {
                boxes.Add(child.Id, CreateBox(
                    child,
                    new WebSceneRect(inlineX, flowY, 0, 0),
                    content.Size));
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
                previous = null;
                continue;
            }
            var positioned = child.Style.Position is CssLayoutPosition.Absolute or CssLayoutPosition.Fixed;
            var metrics = ResolveMetrics(
                child,
                content.Size,
                resolvePercentageHeight: !parentStyle.PercentageHeightBasisIsIndefinite);
            // A projected DOM text node is an anonymous inline box regardless
            // of the default element-style display carried by its backend
            // control.
            var inline = child.IsText || IsInline(child.Style.Display);
            var autoWidth = inline
                ? child.IntrinsicSize.Width
                : Math.Max(0, content.Width - metrics.Margin.Horizontal);
            if (metrics.OuterWidth is null && UsesShrinkToFitInlineWidth(child.Style.Display))
            {
                autoWidth = Math.Min(
                    child.IntrinsicSize.Width,
                    Math.Max(0, content.Width - metrics.Margin.Horizontal));
            }
            var width = metrics.OuterWidth ?? autoWidth;
            var height = metrics.OuterHeight ?? child.IntrinsicSize.Height;
            width = Clamp(width, metrics.MinOuterWidth, metrics.MaxOuterWidth);
            height = Clamp(height, metrics.MinOuterHeight, metrics.MaxOuterHeight);
            var usedMarginLeft = metrics.Margin.Left;
            var usedMarginRight = metrics.Margin.Right;
            if (!inline)
            {
                var leftIsAuto = child.Style.Margin.Left.IsAuto;
                var rightIsAuto = child.Style.Margin.Right.IsAuto;
                if (leftIsAuto || rightIsAuto)
                {
                    var remainingInlineSpace = Math.Max(
                        0,
                        content.Width - width
                        - (leftIsAuto ? 0 : usedMarginLeft)
                        - (rightIsAuto ? 0 : usedMarginRight));
                    if (leftIsAuto && rightIsAuto)
                    {
                        usedMarginLeft = remainingInlineSpace / 2;
                        usedMarginRight = remainingInlineSpace / 2;
                    }
                    else if (leftIsAuto)
                    {
                        usedMarginLeft = remainingInlineSpace;
                    }
                    else
                    {
                        usedMarginRight = remainingInlineSpace;
                    }
                }
            }
            var lineBoxHeight = child.HasZeroLineHeight ? 0 : height;

            // The static position for an inline-level out-of-flow box is the
            // current insertion point. Do not line-wrap it based on the
            // positioned box's own width because it does not participate in
            // normal-flow line breaking.
            if (!positioned
                && !parentStyle.NoWrap
                && inline
                && inlineX > content.X
                && inlineX + metrics.Margin.Left + width + metrics.Margin.Right > content.Right
                && (previous is null || child.CanBreakBefore))
            {
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
            }

            if (!inline && inlineX > content.X)
            {
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
            }

            if (inline && inlineLineHeight == 0 && pendingBlockMargin is { } precedingBlockMargin)
            {
                flowY += precedingBlockMargin;
                pendingBlockMargin = null;
            }

            if (positioned)
            {
                staticPositions[child.Id] = new WebScenePoint(
                    inline ? inlineX : content.X,
                    flowY);
                continue;
            }

            var x = inline ? inlineX + usedMarginLeft : content.X + usedMarginLeft;
            var collapsedBefore = !inline
                ? pendingBlockMargin is { } previousBottomMargin
                    ? CollapseVerticalMargins(previousBottomMargin, metrics.Margin.Top)
                    : canCollapseTopMargin
                      && flowY == content.Y + verticalOffset
                      && ParentMarginDominatesChildMargin(ownMargin.Top, metrics.Margin.Top)
                        ? 0
                        : metrics.Margin.Top
                : metrics.Margin.Top;
            var y = flowY + collapsedBefore;
            ApplyRelativeOffset(
                child.Style,
                content,
                parentStyle.PercentageHeightBasisIsIndefinite,
                ref x,
                ref y);
            var paintY = child.IsText
                ? y + (lineBoxHeight - height) / 2d
                : y;
            boxes.Add(child.Id, CreateBox(child, new WebSceneRect(x, paintY, width, height), content.Size));

            if (inline)
            {
                inlineX += usedMarginLeft + width + usedMarginRight;
                inlineLineHeight = Math.Max(
                    inlineLineHeight,
                    Math.Max(
                        insideMarker?.LineHeight ?? 0,
                        metrics.Margin.Top + lineBoxHeight + metrics.Margin.Bottom));
            }
            else
            {
                flowY += collapsedBefore + lineBoxHeight;
                pendingBlockMargin = metrics.Margin.Bottom;
            }
            previous = child;
        }
    }

    private static bool CanCollapseBlockChildMargin(
        CssLayoutStyle style,
        bool top,
        WebSceneSize reference)
    {
        if (style.Display is not (CssLayoutDisplay.Block or CssLayoutDisplay.ListItem)
            || style.OverflowX != CssLayoutOverflow.Visible
            || style.OverflowY != CssLayoutOverflow.Visible
            || !style.Height.IsAuto
            || (style.MinHeight.Resolve(reference.Height) ?? 0) > 0)
        {
            return false;
        }

        var padding = style.Padding.Resolve(reference.Width, reference.Height);
        var border = style.Border.Resolve(reference.Width, reference.Height);
        return top
            ? padding.Top == 0 && border.Top == 0
            : padding.Bottom == 0 && border.Bottom == 0;
    }

    private static double CollapseVerticalMargins(double first, double second)
        => first >= 0 && second >= 0
            ? Math.Max(first, second)
            : first <= 0 && second <= 0
                ? Math.Min(first, second)
                : first + second;

    private static bool ParentMarginDominatesChildMargin(double parent, double child)
        => CollapseVerticalMargins(parent, child) == parent;

    private static void ArrangeTable(
        CssLayoutNode table,
        IReadOnlyList<double> columns,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        var y = content.Y;
        var width = columns.Sum();
        var stretchSingleRow = CssTableModelFixup.EnumerateRows(table, tableRoot: true).Take(2).Count() == 1;
        var improper = new List<CssLayoutNode>();
        foreach (var child in table.Children.Where(CssTableModelFixup.IsFlowChild))
        {
            if (child.Style.Display == CssLayoutDisplay.TableRow)
            {
                FlushImproper();
                var row = CssTableModelFixup.CreateRow(child, child.Children);
                var height = ResolveTableRowHeight(row, columns, content.Size);
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                boxes.Add(child.Id, CreateBox(child, new WebSceneRect(content.X, y, width, height), content.Size));
                y += height;
            }
            else if (child.Style.Display is CssLayoutDisplay.TableRowGroup
                     or CssLayoutDisplay.TableHeaderGroup
                     or CssLayoutDisplay.TableFooterGroup)
            {
                FlushImproper();
                var height = CssTableModelFixup.EnumerateRows(child, tableRoot: false)
                    .Sum(row => ResolveTableRowHeight(row, columns, content.Size));
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                boxes.Add(child.Id, CreateBox(child, new WebSceneRect(content.X, y, width, height), content.Size));
                y += height;
            }
            else if (CssTableModelFixup.IsProperTableRootChild(child.Style.Display))
            {
                FlushImproper();
            }
            else
            {
                improper.Add(child);
            }
        }
        FlushImproper();

        void FlushImproper()
        {
            if (improper.Count == 0) return;
            if (improper.All(static child => child.IsCollapsibleWhitespace))
            {
                improper.Clear();
                return;
            }
            var row = CssTableModelFixup.CreateRow(null, improper);
            var height = ResolveTableRowHeight(row, columns, content.Size);
            if (stretchSingleRow) height = Math.Max(height, content.Height);
            ArrangeEffectiveTableRow(row, columns, new WebSceneRect(content.X, y, width, height), boxes);
            y += height;
            improper.Clear();
        }
    }

    private static void ArrangeTableRowGroup(
        CssLayoutNode group,
        IReadOnlyList<double> columns,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        var y = content.Y;
        var width = columns.Sum();
        var stretchSingleRow = CssTableModelFixup.EnumerateRows(group, tableRoot: false).Take(2).Count() == 1;
        var improper = new List<CssLayoutNode>();
        foreach (var child in group.Children.Where(CssTableModelFixup.IsFlowChild))
        {
            if (child.Style.Display == CssLayoutDisplay.TableRow)
            {
                FlushImproper();
                var row = CssTableModelFixup.CreateRow(child, child.Children);
                var height = ResolveTableRowHeight(row, columns, content.Size);
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                boxes.Add(child.Id, CreateBox(child, new WebSceneRect(content.X, y, width, height), content.Size));
                y += height;
            }
            else
            {
                improper.Add(child);
            }
        }
        FlushImproper();

        void FlushImproper()
        {
            if (improper.Count == 0) return;
            if (improper.All(static child => child.IsCollapsibleWhitespace))
            {
                improper.Clear();
                return;
            }
            var row = CssTableModelFixup.CreateRow(null, improper);
            var height = ResolveTableRowHeight(row, columns, content.Size);
            if (stretchSingleRow) height = Math.Max(height, content.Height);
            ArrangeEffectiveTableRow(row, columns, new WebSceneRect(content.X, y, width, height), boxes);
            y += height;
            improper.Clear();
        }
    }

    private static void ArrangeEffectiveTableRow(
        CssEffectiveTableRow row,
        IReadOnlyList<double> columns,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        var x = content.X;
        var column = 0;
        foreach (var effectiveCell in row.Cells)
        {
            var width = column < columns.Count ? columns[column] : 0;
            if (effectiveCell.Cell is null)
            {
                ArrangeAnonymousTableCell(effectiveCell.Children, new WebSceneRect(x, content.Y, width, content.Height), boxes);
                x += width;
                column++;
                continue;
            }
            var cell = effectiveCell.Cell;
            var metrics = ResolveMetrics(cell, content.Size);
            var borderWidth = Math.Max(0, width - metrics.Margin.Horizontal);
            var borderHeight = Math.Max(0, content.Height - metrics.Margin.Vertical);
            boxes.Add(cell.Id, CreateBox(
                cell,
                new WebSceneRect(
                    x + metrics.Margin.Left,
                    content.Y + metrics.Margin.Top,
                    borderWidth,
                    borderHeight),
                content.Size));
            x += width;
            column++;
        }
    }

    private static void ArrangeAnonymousTableCell(
        IReadOnlyList<CssLayoutNode> children,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        var flow = children.Where(CssTableModelFixup.IsFlowChild)
            .Where(static child => child.Style.Display is not (
                CssLayoutDisplay.TableColumn or CssLayoutDisplay.TableColumnGroup))
            .ToArray();
        var x = content.X;
        var y = content.Y;
        var lineHeight = 0d;
        for (var index = 0; index < flow.Length; index++)
        {
            var child = flow[index];
            if (child.IsCollapsibleWhitespace)
            {
                if (HasInlineContentOnBothSides(flow, index)) x += child.CollapsedWhitespaceWidth;
                continue;
            }
            var metrics = ResolveMetrics(child, content.Size);
            var inline = child.IsText || IsInline(child.Style.Display);
            if (!inline && x > content.X)
            {
                y += lineHeight;
                x = content.X;
                lineHeight = 0;
            }
            var width = metrics.OuterWidth ?? (inline
                ? child.IntrinsicSize.Width
                : UsesShrinkToFitInlineWidth(child.Style.Display)
                    ? Math.Min(content.Width, child.IntrinsicSize.Width)
                    : content.Width);
            var height = metrics.OuterHeight ?? child.IntrinsicSize.Height;
            boxes.Add(child.Id, CreateBox(child, new WebSceneRect(inline ? x : content.X, y, width, height), content.Size));
            if (inline)
            {
                x += width;
                lineHeight = Math.Max(lineHeight, height);
            }
            else
            {
                y += height;
            }
        }
    }

    private static bool HasInlineContentOnBothSides(IReadOnlyList<CssLayoutNode> children, int index)
    {
        CssLayoutNode? previous = null;
        for (var cursor = index - 1; cursor >= 0; cursor--)
        {
            if (children[cursor].IsCollapsibleWhitespace) continue;
            previous = children[cursor];
            break;
        }
        CssLayoutNode? next = null;
        for (var cursor = index + 1; cursor < children.Count; cursor++)
        {
            if (children[cursor].IsCollapsibleWhitespace) continue;
            next = children[cursor];
            break;
        }
        return previous is not null && next is not null
               && (previous.IsText || IsInline(previous.Style.Display))
               && (next.IsText || IsInline(next.Style.Display));
    }

    private static double[] ResolveTableColumns(CssLayoutNode root, WebSceneSize reference)
    {
        var rows = (root.Style.Display == CssLayoutDisplay.TableRow
                ? [CssTableModelFixup.CreateRow(root, root.Children)]
                : CssTableModelFixup.EnumerateRows(
                    root,
                    tableRoot: root.Style.Display is CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable))
            .ToArray();
        var tracks = root.Style.Display is CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable
            ? CssTableModelFixup.EnumerateColumnTracks(root).ToArray()
            : [];
        var columnCount = Math.Max(
            tracks.Length,
            rows.Length == 0 ? 0 : rows.Max(static row => row.Cells.Count));
        var columns = new double[columnCount];
        for (var column = 0; column < tracks.Length; column++)
        {
            columns[column] = Math.Max(
                columns[column],
                tracks[column].Style.Width.Resolve(reference.Width) ?? 0);
        }
        foreach (var row in rows)
        {
            var column = 0;
            foreach (var effectiveCell in row.Cells)
            {
                if (effectiveCell.Cell is null)
                {
                    columns[column] = Math.Max(columns[column], ResolveAnonymousCellIntrinsicWidth(effectiveCell.Children));
                    column++;
                    continue;
                }
                var cell = effectiveCell.Cell;
                var metrics = ResolveMetrics(cell, reference);
                var width = Clamp(
                                metrics.OuterWidth ?? cell.IntrinsicSize.Width,
                                metrics.MinOuterWidth,
                                metrics.MaxOuterWidth)
                            + metrics.Margin.Horizontal;
                columns[column] = Math.Max(columns[column], width);
                column++;
            }
        }
        if (root.Style.Display is CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable)
        {
            // `table-layout: fixed` only establishes fixed mode when the
            // table itself has a definite specified inline size.
            if (root.Style.FixedTableLayout && !root.Style.Width.IsAuto)
            {
                var constraints = ResolveFixedTableColumnConstraints(rows, tracks, columns.Length);
                ApplyFixedTableBaseAssignments(columns, constraints, reference);
                CssTableWidthDistribution.DistributeFixedExcess(
                    columns,
                    constraints.Select(static constraint => constraint.Distribution).ToArray(),
                    reference.Width);
            }
            else
            {
                DistributeSingleColumnTableExcess(columns, reference.Width);
            }
        }
        return columns;
    }

    private static FixedTableColumnConstraint[] ResolveFixedTableColumnConstraints(
        IReadOnlyList<CssEffectiveTableRow> rows,
        IReadOnlyList<CssLayoutNode> tracks,
        int columnCount)
    {
        var constraints = Enumerable.Range(0, columnCount)
            .Select(static _ => FixedTableColumnConstraint.Auto)
            .ToArray();
        for (var column = 0; column < tracks.Count && column < constraints.Length; column++)
        {
            if (!tracks[column].Style.Width.IsAuto)
            {
                constraints[column] = CreateFixedTableColumnConstraint(tracks[column]);
            }
        }

        var firstRow = rows.FirstOrDefault();
        if (firstRow is null)
        {
            return constraints;
        }
        for (var column = 0; column < firstRow.Cells.Count && column < constraints.Length; column++)
        {
            var cell = firstRow.Cells[column].Cell;
            if (constraints[column].Distribution.Kind == CssTableColumnWidthKind.Auto
                && cell is not null
                && !cell.Style.Width.IsAuto)
            {
                constraints[column] = CreateFixedTableColumnConstraint(cell);
            }
        }
        return constraints;
    }

    private static FixedTableColumnConstraint CreateFixedTableColumnConstraint(CssLayoutNode source)
    {
        var width = source.Style.Width;
        var kind = width.Unit switch
        {
            CssLayoutLengthUnit.Percent when width.Value > 0 => CssTableColumnWidthKind.Percent,
            CssLayoutLengthUnit.Pixel when width.Value > 0 => CssTableColumnWidthKind.Length,
            CssLayoutLengthUnit.Auto => CssTableColumnWidthKind.Auto,
            _ => CssTableColumnWidthKind.Zero
        };
        return new FixedTableColumnConstraint(
            source,
            width,
            new CssTableColumnWidthConstraint(kind, Math.Max(0, width.Value)));
    }

    private static void ApplyFixedTableBaseAssignments(
        double[] columns,
        IReadOnlyList<FixedTableColumnConstraint> constraints,
        WebSceneSize reference)
    {
        for (var column = 0; column < columns.Length; column++)
        {
            var constraint = constraints[column];
            columns[column] = constraint.Distribution.Kind switch
            {
                CssTableColumnWidthKind.Auto or CssTableColumnWidthKind.Zero => 0,
                // CSS fixed layout resolves percentage cells without adding
                // their border/padding contribution to the percentage base.
                CssTableColumnWidthKind.Percent => Math.Max(0, constraint.Width.Resolve(reference.Width) ?? 0),
                CssTableColumnWidthKind.Length when constraint.Source is { } source
                                                   && source.Style.Display == CssLayoutDisplay.TableCell
                    => Math.Max(0, ResolveMetrics(source, reference).OuterWidth ?? 0),
                CssTableColumnWidthKind.Length
                    => Math.Max(0, constraint.Width.Resolve(reference.Width) ?? 0),
                _ => 0
            };
        }
    }

    private static void DistributeSingleColumnTableExcess(double[] columns, double usedWidth)
    {
        // Multi-column excess distribution is deliberately left to the full
        // fixed/percent/auto track algorithm. A sole column unambiguously owns
        // all positive excess in the table's finalized used content width.
        if (columns.Length == 1
            && double.IsFinite(usedWidth)
            && usedWidth > columns[0])
        {
            columns[0] = usedWidth;
        }
    }

    private static double ResolveTableRowHeight(
        CssEffectiveTableRow row,
        IReadOnlyList<double> columns,
        WebSceneSize reference)
    {
        var height = row.Row is null
            ? 0
            : ResolveMetrics(row.Row, reference).OuterHeight ?? row.Row.IntrinsicSize.Height;
        var column = 0;
        foreach (var effectiveCell in row.Cells)
        {
            var width = column < columns.Count ? columns[column] : reference.Width;
            if (effectiveCell.Cell is null)
            {
                height = Math.Max(height, ResolveAnonymousCellIntrinsicHeight(effectiveCell.Children));
                column++;
                continue;
            }
            var cell = effectiveCell.Cell;
            var metrics = ResolveMetrics(cell, new WebSceneSize(width, reference.Height));
            height = Math.Max(
                height,
                Clamp(
                    metrics.OuterHeight ?? cell.IntrinsicSize.Height,
                    metrics.MinOuterHeight,
                    metrics.MaxOuterHeight) + metrics.Margin.Vertical);
            column++;
        }
        return height;
    }

    private static double ResolveAnonymousCellIntrinsicWidth(IReadOnlyList<CssLayoutNode> children)
    {
        var width = 0d;
        var line = 0d;
        foreach (var child in children.Where(CssTableModelFixup.IsFlowChild))
        {
            if (child.IsCollapsibleWhitespace) continue;
            if (child.IsText || IsInline(child.Style.Display)) line += child.IntrinsicSize.Width;
            else
            {
                width = Math.Max(width, line);
                line = 0;
                width = Math.Max(width, child.IntrinsicSize.Width);
            }
        }
        return Math.Max(width, line);
    }

    private static double ResolveAnonymousCellIntrinsicHeight(IReadOnlyList<CssLayoutNode> children)
    {
        var height = 0d;
        var lineHeight = 0d;
        foreach (var child in children.Where(CssTableModelFixup.IsFlowChild))
        {
            if (child.IsCollapsibleWhitespace) continue;
            if (child.IsText || IsInline(child.Style.Display)) lineHeight = Math.Max(lineHeight, child.IntrinsicSize.Height);
            else
            {
                height += lineHeight + child.IntrinsicSize.Height;
                lineHeight = 0;
            }
        }
        return height + lineHeight;
    }

    private static bool HasDirectTableModel(CssLayoutNode table)
        => CssTableModelFixup.EnumerateRows(table, tableRoot: true).Any();

    private static bool IsNonRenderedTableTrack(CssLayoutDisplay display)
        => display is CssLayoutDisplay.TableColumn or CssLayoutDisplay.TableColumnGroup;

    private static bool IsSuppressedTableChild(CssLayoutNode parent, CssLayoutNode child)
        => parent.Style.Display == CssLayoutDisplay.TableColumn
           || parent.Style.Display == CssLayoutDisplay.TableColumnGroup
           && child.Style.Display != CssLayoutDisplay.TableColumn;

    private static void AddEmptySubtree(
        CssLayoutNode node,
        Dictionary<long, CssLayoutBox> boxes)
    {
        boxes.TryAdd(node.Id, EmptyBox());
        foreach (var child in node.Children)
        {
            AddEmptySubtree(child, boxes);
        }
    }

    private static CssLayoutBox EmptyBox()
        => new(WebSceneRect.Empty, WebSceneRect.Empty, WebSceneRect.Empty, WebSceneRect.Empty);

    private static double ResolveBlockFlowHeight(
        IReadOnlyList<CssLayoutNode> children,
        WebSceneSize reference)
    {
        var height = 0d;
        var inlineHeight = 0d;
        foreach (var child in children.Where(static child =>
                     child.Style.Display != CssLayoutDisplay.None
                     && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)))
        {
            var metrics = ResolveMetrics(child, reference);
            var childHeight = Clamp(
                                  metrics.OuterHeight ?? child.IntrinsicSize.Height,
                                  metrics.MinOuterHeight,
                                  metrics.MaxOuterHeight)
                              + metrics.Margin.Vertical;
            if (IsInline(child.Style.Display))
            {
                inlineHeight = Math.Max(inlineHeight, childHeight);
            }
            else
            {
                height += inlineHeight + childHeight;
                inlineHeight = 0;
            }
        }
        return height + inlineHeight;
    }

    private static void ArrangeFlex(
        IReadOnlyList<CssLayoutNode> children,
        CssLayoutStyle style,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        if (children.Count == 0)
        {
            return;
        }

        var row = style.FlexDirection is CssLayoutFlexDirection.Row or CssLayoutFlexDirection.RowReverse;
        var reverse = style.FlexDirection is CssLayoutFlexDirection.RowReverse or CssLayoutFlexDirection.ColumnReverse;
        var items = children.Select(child => CreateFlexItem(child, content.Size, row)).ToList();
        var mainSize = row ? content.Width : content.Height;
        var crossSize = row ? content.Height : content.Width;
        var mainGap = Math.Max(0, (row ? style.ColumnGap.Resolve(mainSize) : style.RowGap.Resolve(mainSize)) ?? 0);
        var crossGap = Math.Max(0, (row ? style.RowGap.Resolve(crossSize) : style.ColumnGap.Resolve(crossSize)) ?? 0);
        var lines = BuildFlexLines(items, mainSize, mainGap, style.FlexWrap != CssLayoutFlexWrap.NoWrap);

        foreach (var line in lines)
        {
            ResolveFlexibleMainSizes(line.Items, mainSize, mainGap);
            ResolveFlexLineCrossSize(line, style, row);
            if (reverse)
            {
                line.Items.Reverse();
            }
        }

        var naturalCross = lines.Sum(static line => line.CrossSize)
                           + crossGap * Math.Max(0, lines.Count - 1);
        var crossFree = Math.Max(0, crossSize - naturalCross);
        var crossOffset = 0d;
        var betweenLines = crossGap;
        if (style.FlexWrap == CssLayoutFlexWrap.NoWrap)
        {
            lines[0].CrossSize = crossSize;
            crossFree = 0;
        }
        else if (style.AlignContent == CssLayoutAlignContent.Stretch && lines.Count > 0)
        {
            var extra = crossFree / lines.Count;
            foreach (var line in lines)
            {
                line.CrossSize += extra;
            }
            crossFree = 0;
        }
        else
        {
            (crossOffset, betweenLines) = ResolveContentAlignment(
                style.AlignContent,
                crossFree,
                lines.Count,
                crossGap);
        }

        var crossCursor = crossOffset;
        foreach (var line in lines)
        {
            var lineCrossPosition = style.FlexWrap == CssLayoutFlexWrap.WrapReverse
                ? crossSize - crossCursor - line.CrossSize
                : crossCursor;
            var free = mainSize - FlexLineOccupiedMain(line.Items, mainGap);
            var (offset, between) = ResolveJustification(
                style.JustifyContent,
                Math.Max(0, free),
                line.Items.Count,
                mainGap);
            var cursor = offset;
            foreach (var item in line.Items)
            {
                cursor += item.MainMarginStart;
                var alignment = item.Node.Style.AlignSelf == CssLayoutAlignment.Auto
                    ? style.AlignItems
                    : item.Node.Style.AlignSelf;
                var cross = item.Cross;
                if (!item.HasExplicitCross && alignment == CssLayoutAlignment.Stretch)
                {
                    cross = Math.Max(0, line.CrossSize - item.CrossMarginStart - item.CrossMarginEnd);
                }
                var itemCrossPosition = alignment switch
                {
                    CssLayoutAlignment.Center =>
                        (line.CrossSize - cross - item.CrossMarginStart - item.CrossMarginEnd) / 2
                        + item.CrossMarginStart,
                    CssLayoutAlignment.FlexEnd => line.CrossSize - cross - item.CrossMarginEnd,
                    CssLayoutAlignment.Baseline when row =>
                        line.FirstBaseline - ResolveFlexItemBaseline(item),
                    _ => item.CrossMarginStart
                };

                var resolvedCross = lineCrossPosition + itemCrossPosition;
                var x = row ? content.X + cursor : content.X + resolvedCross;
                var y = row ? content.Y + resolvedCross : content.Y + cursor;
                var width = row ? item.Main : cross;
                var height = row ? cross : item.Main;
                ApplyRelativeOffset(
                    item.Node.Style,
                    content,
                    style.PercentageHeightBasisIsIndefinite,
                    ref x,
                    ref y);
                boxes.Add(item.Node.Id, CreateBox(item.Node, new WebSceneRect(x, y, width, height), content.Size));
                cursor += item.Main + item.MainMarginEnd + between;
            }

            crossCursor += line.CrossSize + betweenLines;
        }
    }

    private static void ResolveFlexLineCrossSize(
        FlexLine line,
        CssLayoutStyle containerStyle,
        bool row)
    {
        if (!row)
        {
            return;
        }

        var baselineItems = line.Items
            .Where(item => ResolveFlexItemAlignment(item, containerStyle) == CssLayoutAlignment.Baseline)
            .ToArray();
        if (baselineItems.Length == 0)
        {
            return;
        }

        var firstBaseline = baselineItems.Max(item =>
            item.CrossMarginStart + ResolveFlexItemBaseline(item));
        var afterBaseline = baselineItems.Max(item =>
            item.CrossMarginEnd + Math.Max(0, item.Cross - ResolveFlexItemBaseline(item)));
        line.FirstBaseline = firstBaseline;
        line.CrossSize = Math.Max(line.CrossSize, firstBaseline + afterBaseline);
    }

    private static CssLayoutAlignment ResolveFlexItemAlignment(
        FlexItem item,
        CssLayoutStyle containerStyle)
        => item.Node.Style.AlignSelf == CssLayoutAlignment.Auto
            ? containerStyle.AlignItems
            : item.Node.Style.AlignSelf;

    private static double ResolveFlexItemBaseline(FlexItem item)
        => item.Node.FirstBaseline is { } baseline && double.IsFinite(baseline)
            ? Math.Max(0, baseline)
            : item.Cross;

    private static List<FlexLine> BuildFlexLines(
        IReadOnlyList<FlexItem> items,
        double mainSize,
        double gap,
        bool wraps)
    {
        var lines = new List<FlexLine>();
        var line = new FlexLine();
        foreach (var item in items)
        {
            var outerMain = item.Main + item.MainMarginStart + item.MainMarginEnd;
            var candidate = FlexLineOccupiedMain(line.Items, gap)
                            + (line.Items.Count > 0 ? gap : 0)
                            + outerMain;
            if (wraps && line.Items.Count > 0 && candidate > mainSize)
            {
                lines.Add(line);
                line = new FlexLine();
            }

            line.Items.Add(item);
            line.CrossSize = Math.Max(
                line.CrossSize,
                item.Cross + item.CrossMarginStart + item.CrossMarginEnd);
        }

        if (line.Items.Count > 0)
        {
            lines.Add(line);
        }

        return lines;
    }

    private static double FlexLineOccupiedMain(IReadOnlyList<FlexItem> items, double gap)
        => items.Sum(static item => item.Main + item.MainMarginStart + item.MainMarginEnd)
           + gap * Math.Max(0, items.Count - 1);

    private static void ResolveFlexibleMainSizes(List<FlexItem> items, double mainSize, double gap)
    {
        var free = mainSize - FlexLineOccupiedMain(items, gap);
        var resolvedFlexibleLengths = false;
        if (free > 0)
        {
            var grow = items.Sum(static item => item.Grow);
            if (grow > 0)
            {
                foreach (var item in items)
                {
                    item.Main += free * item.Grow / grow;
                }
                free = 0;
                resolvedFlexibleLengths = true;
            }
        }
        else if (free < 0)
        {
            var deficit = -free;
            var shrinkable = items.Where(static item => item.Shrink > 0 && item.Main > item.MinimumMain).ToList();
            while (deficit > .001 && shrinkable.Count > 0)
            {
                var weight = shrinkable.Sum(static item => item.Shrink * Math.Max(0, item.Main));
                if (weight <= 0)
                {
                    break;
                }

                var applied = 0d;
                foreach (var item in shrinkable)
                {
                    var before = item.Main;
                    item.Main = Math.Max(item.MinimumMain, before - deficit * item.Shrink * before / weight);
                    applied += before - item.Main;
                }

                if (applied <= .001)
                {
                    break;
                }
                deficit = Math.Max(0, deficit - applied);
                shrinkable.RemoveAll(static item => item.Main <= item.MinimumMain + .001);
            }
            free = -deficit;
            resolvedFlexibleLengths = true;
        }

        if (resolvedFlexibleLengths)
        {
            var finalMainSizes = items.Select(static item => item.Main).ToArray();
            CssSubpixelFlexAllocator.QuantizeFinalMainSizes(finalMainSizes);
            for (var index = 0; index < items.Count; index++)
            {
                items[index].Main = finalMainSizes[index];
            }
        }

        if (free <= 0)
        {
            return;
        }

        var autoMarginCount = items.Sum(static item =>
            (item.MainMarginStartIsAuto ? 1 : 0)
            + (item.MainMarginEndIsAuto ? 1 : 0));
        if (autoMarginCount == 0)
        {
            return;
        }

        var autoMargin = free / autoMarginCount;
        foreach (var item in items)
        {
            if (item.MainMarginStartIsAuto) item.MainMarginStart = autoMargin;
            if (item.MainMarginEndIsAuto) item.MainMarginEnd = autoMargin;
        }
    }

    private static void ArrangeGrid(
        IEnumerable<CssLayoutNode> children,
        CssLayoutStyle style,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        if (TryParseFixedPixelTracks(style.GridTemplateColumns, out var fixedTracks))
        {
            _ = TryParseFixedPixelTracks(style.GridTemplateRows, out var fixedRowTracks);
            ArrangeFixedPixelGrid(children, style, content, boxes, fixedTracks, fixedRowTracks);
            return;
        }

        if (UsesAutoFractionColumns(style))
        {
            ArrangeAutoFractionGrid(children, style, content, boxes);
            return;
        }

        var x = content.X;
        foreach (var child in children)
        {
            var metrics = ResolveMetrics(child, content.Size);
            var width = Clamp(metrics.OuterWidth ?? child.IntrinsicSize.Width, metrics.MinOuterWidth, metrics.MaxOuterWidth);
            var height = Clamp(metrics.OuterHeight ?? child.IntrinsicSize.Height, metrics.MinOuterHeight, metrics.MaxOuterHeight);
            var childX = x + metrics.Margin.Left;
            var childY = content.Y + metrics.Margin.Top;
            ApplyRelativeOffset(
                child.Style,
                content,
                style.PercentageHeightBasisIsIndefinite,
                ref childX,
                ref childY);
            boxes.Add(child.Id, CreateBox(child, new WebSceneRect(childX, childY, width, height), content.Size));
            x += metrics.Margin.Left + width + metrics.Margin.Right;
        }
    }

    private static void ArrangeFixedPixelGrid(
        IEnumerable<CssLayoutNode> children,
        CssLayoutStyle style,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes,
        IReadOnlyList<double> tracks,
        IReadOnlyList<double> fixedRowTracks)
    {
        var placements = new List<(CssLayoutNode Node, int Row, int Column, bool Full, ResolvedMetrics Metrics)>();
        var row = 0;
        var column = 0;
        foreach (var child in children)
        {
            var full = tracks.Count == 2 && SpansBothColumns(child.Style.GridColumn);
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            placements.Add((child, row, column, full, ResolveMetrics(child, content.Size)));
            if (full)
            {
                row++;
                column = 0;
            }
            else if (++column == tracks.Count)
            {
                row++;
                column = 0;
            }
        }

        var usedRowCount = placements.Count == 0 ? 0 : placements.Max(item => item.Row) + 1;
        var rowHeights = new double[Math.Max(fixedRowTracks.Count, usedRowCount)];
        for (var index = 0; index < fixedRowTracks.Count; index++)
        {
            rowHeights[index] = fixedRowTracks[index];
        }
        foreach (var item in placements)
        {
            if (item.Row >= fixedRowTracks.Count)
            {
                rowHeights[item.Row] = Math.Max(
                    rowHeights[item.Row],
                    (item.Metrics.OuterHeight ?? item.Node.IntrinsicSize.Height) + item.Metrics.Margin.Vertical);
            }
        }
        var columnGap = Math.Max(0, style.ColumnGap.Resolve(content.Width) ?? 0);
        var rowGap = Math.Max(0, style.RowGap.Resolve(content.Height) ?? 0);
        var columnOffsets = new double[tracks.Count];
        for (var index = 1; index < columnOffsets.Length; index++)
        {
            columnOffsets[index] = columnOffsets[index - 1] + tracks[index - 1] + columnGap;
        }
        var rowOffsets = new double[rowHeights.Length];
        for (var index = 1; index < rowOffsets.Length; index++)
        {
            rowOffsets[index] = rowOffsets[index - 1] + rowHeights[index - 1] + rowGap;
        }
        foreach (var item in placements)
        {
            var trackWidth = item.Full
                ? tracks.Sum() + columnGap * Math.Max(0, tracks.Count - 1)
                : tracks[item.Column];
            var x = content.X + (item.Full ? 0 : columnOffsets[item.Column]) + item.Metrics.Margin.Left;
            var y = content.Y + rowOffsets[item.Row] + item.Metrics.Margin.Top;
            var width = item.Metrics.OuterWidth ?? Math.Max(0, trackWidth - item.Metrics.Margin.Horizontal);
            var height = item.Metrics.OuterHeight
                         ?? Math.Max(0, rowHeights[item.Row] - item.Metrics.Margin.Vertical);
            ApplyRelativeOffset(
                item.Node.Style,
                content,
                style.PercentageHeightBasisIsIndefinite,
                ref x,
                ref y);
            boxes.Add(item.Node.Id, CreateBox(item.Node, new WebSceneRect(x, y, width, height), content.Size));
        }
    }

    private static void ArrangeAutoFractionGrid(
        IEnumerable<CssLayoutNode> children,
        CssLayoutStyle style,
        WebSceneRect content,
        Dictionary<long, CssLayoutBox> boxes)
    {
        var placements = new List<(CssLayoutNode Node, int Row, int Column, bool Full, ResolvedMetrics Metrics)>();
        var row = 0;
        var column = 0;
        var firstTrackWidth = 0d;
        foreach (var child in children)
        {
            var metrics = ResolveMetrics(child, content.Size);
            var full = SpansBothColumns(child.Style.GridColumn);
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            placements.Add((child, row, column, full, metrics));
            if (!full && column == 0)
            {
                firstTrackWidth = Math.Max(
                    firstTrackWidth,
                    (metrics.OuterWidth ?? child.IntrinsicSize.Width) + metrics.Margin.Horizontal);
            }
            if (full)
            {
                row++;
                column = 0;
            }
            else if (++column == 2)
            {
                row++;
                column = 0;
            }
        }

        firstTrackWidth = Math.Min(content.Width, firstTrackWidth);
        var columnGap = Math.Max(0, style.ColumnGap.Resolve(content.Width) ?? 0);
        var secondTrackWidth = Math.Max(0, content.Width - firstTrackWidth - columnGap);
        var rowHeights = new double[placements.Count == 0 ? 0 : placements.Max(item => item.Row) + 1];
        foreach (var item in placements)
        {
            var height = (item.Metrics.OuterHeight ?? item.Node.IntrinsicSize.Height) + item.Metrics.Margin.Vertical;
            rowHeights[item.Row] = Math.Max(rowHeights[item.Row], height);
        }

        var rowGap = Math.Max(0, style.RowGap.Resolve(content.Height) ?? 0);
        var rowOffsets = new double[rowHeights.Length];
        for (var index = 1; index < rowOffsets.Length; index++)
        {
            rowOffsets[index] = rowOffsets[index - 1] + rowHeights[index - 1] + rowGap;
        }
        foreach (var item in placements)
        {
            var trackX = item.Full || item.Column == 0
                ? content.X
                : content.X + firstTrackWidth + columnGap;
            var trackWidth = item.Full
                ? content.Width
                : item.Column == 0 ? firstTrackWidth : secondTrackWidth;
            var x = trackX + item.Metrics.Margin.Left;
            var y = content.Y + rowOffsets[item.Row] + item.Metrics.Margin.Top;
            var width = item.Metrics.OuterWidth
                        ?? Math.Max(0, trackWidth - item.Metrics.Margin.Horizontal);
            var height = item.Metrics.OuterHeight
                         ?? Math.Max(0, rowHeights[item.Row] - item.Metrics.Margin.Vertical);
            ApplyRelativeOffset(
                item.Node.Style,
                content,
                style.PercentageHeightBasisIsIndefinite,
                ref x,
                ref y);
            boxes.Add(item.Node.Id, CreateBox(item.Node, new WebSceneRect(x, y, width, height), content.Size));
        }
    }

    private static bool UsesAutoFractionColumns(CssLayoutStyle style)
        => string.Equals(
            string.Join(' ', style.GridTemplateColumns.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)),
            "auto 1fr",
            StringComparison.OrdinalIgnoreCase);

    private static bool TryParseFixedPixelTracks(string value, out double[] tracks)
    {
        var tokens = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        tracks = new double[tokens.Length];
        if (tokens.Length == 0) return false;
        for (var index = 0; index < tokens.Length; index++)
        {
            var token = tokens[index].Trim();
            if (!token.EndsWith("px", StringComparison.OrdinalIgnoreCase)
                || !double.TryParse(
                    token.AsSpan(0, token.Length - 2),
                    System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture,
                    out var parsedValue)
                || !double.IsFinite(parsedValue)
                || parsedValue < 0)
            {
                tracks = [];
                return false;
            }
            tracks[index] = parsedValue;
        }
        return true;
    }

    private static bool SpansBothColumns(string value)
    {
        var normalized = value.Replace(" ", string.Empty, StringComparison.Ordinal);
        return normalized is "1/3" or "1/-1";
    }

    private static void ArrangePositioned(
        CssLayoutNode child,
        WebSceneRect containingBlock,
        Dictionary<long, CssLayoutBox> boxes,
        WebScenePoint? staticPosition = null)
    {
        var metrics = ResolveMetrics(child, containingBlock.Size);
        var left = child.Style.Left.Resolve(containingBlock.Width);
        var right = child.Style.Right.Resolve(containingBlock.Width);
        var top = child.Style.Top.Resolve(containingBlock.Height);
        var bottom = child.Style.Bottom.Resolve(containingBlock.Height);
        var width = metrics.OuterWidth
                    ?? (left.HasValue && right.HasValue
                        ? Math.Max(0, containingBlock.Width - left.Value - right.Value - metrics.Margin.Horizontal)
                        : child.IntrinsicSize.Width);
        var height = metrics.OuterHeight
                     ?? (top.HasValue && bottom.HasValue
                         ? Math.Max(0, containingBlock.Height - top.Value - bottom.Value - metrics.Margin.Vertical)
                         : child.IntrinsicSize.Height);
        width = Clamp(width, metrics.MinOuterWidth, metrics.MaxOuterWidth);
        height = Clamp(height, metrics.MinOuterHeight, metrics.MaxOuterHeight);
        var x = left.HasValue
            ? containingBlock.X + left.Value + metrics.Margin.Left
            : right.HasValue
                ? containingBlock.Right - width - right.Value - metrics.Margin.Right
                : (staticPosition?.X ?? containingBlock.X) + metrics.Margin.Left;
        var y = top.HasValue
            ? containingBlock.Y + top.Value + metrics.Margin.Top
            : bottom.HasValue
                ? containingBlock.Bottom - height - bottom.Value - metrics.Margin.Bottom
                : (staticPosition?.Y ?? containingBlock.Y) + metrics.Margin.Top;
        x += child.Style.TranslateX.Resolve(width) ?? 0;
        y += child.Style.TranslateY.Resolve(height) ?? 0;
        boxes.Add(child.Id, CreateBox(child, new WebSceneRect(x, y, width, height), containingBlock.Size));
    }

    private static FlexItem CreateFlexItem(CssLayoutNode child, WebSceneSize content, bool row)
    {
        var metrics = ResolveMetrics(child, content);
        var explicitMain = row ? metrics.OuterWidth : metrics.OuterHeight;
        var explicitCross = row ? metrics.OuterHeight : metrics.OuterWidth;
        var intrinsicMain = row ? child.IntrinsicSize.Width : child.IntrinsicSize.Height;
        var intrinsicCross = row ? child.IntrinsicSize.Height : child.IntrinsicSize.Width;
        var basis = child.Style.FlexBasis.Resolve(row ? content.Width : content.Height);
        var hasExplicitMinimum = row
            ? child.Style.HasExplicitMinWidth
            : child.Style.HasExplicitMinHeight;
        var minimum = hasExplicitMinimum
            ? row ? metrics.MinOuterWidth : metrics.MinOuterHeight
            : null;
        var maximum = row ? metrics.MaxOuterWidth : metrics.MaxOuterHeight;
        var crossMinimum = row ? metrics.MinOuterHeight : metrics.MinOuterWidth;
        var crossMaximum = row ? metrics.MaxOuterHeight : metrics.MaxOuterWidth;
        var main = Clamp(basis ?? explicitMain ?? intrinsicMain, minimum, maximum);
        var chrome = row
            ? metrics.Padding.Horizontal + metrics.Border.Horizontal
            : metrics.Padding.Vertical + metrics.Border.Vertical;
        var isEmptyCssBox = child.Children.Count == 0 && !child.IsText;
        var automaticMinimum = hasExplicitMinimum
            ? 0
            : IsScrollableOverflow(row ? child.Style.OverflowX : child.Style.OverflowY)
                ? 0
                // The automatic flex minimum is content based and capped by
                // the specified size suggestion. Treating the flex base size
                // itself as the minimum makes an explicitly sized empty item
                // impossible to shrink (for example 80px + 80px in 100px).
                : Math.Min(main, Math.Max(chrome, isEmptyCssBox ? 0 : intrinsicMain));
        return new FlexItem(child)
        {
            Main = main,
            MinimumMain = minimum ?? automaticMinimum,
            Cross = Clamp(explicitCross ?? intrinsicCross, crossMinimum, crossMaximum),
            HasExplicitCross = explicitCross.HasValue,
            Grow = Math.Max(0, child.Style.FlexGrow),
            Shrink = Math.Max(0, child.Style.FlexShrink),
            MainMarginStart = row ? metrics.Margin.Left : metrics.Margin.Top,
            MainMarginEnd = row ? metrics.Margin.Right : metrics.Margin.Bottom,
            MainMarginStartIsAuto = (row ? child.Style.Margin.Left : child.Style.Margin.Top).IsAuto,
            MainMarginEndIsAuto = (row ? child.Style.Margin.Right : child.Style.Margin.Bottom).IsAuto,
            CrossMarginStart = row ? metrics.Margin.Top : metrics.Margin.Left,
            CrossMarginEnd = row ? metrics.Margin.Bottom : metrics.Margin.Right
        };
    }

    private static ResolvedMetrics ResolveMetrics(
        CssLayoutNode node,
        WebSceneSize reference,
        bool resolvePercentageHeight = true)
    {
        var style = node.Style;
        var margin = style.Margin.Resolve(reference.Width, reference.Height);
        var padding = style.Padding.Resolve(reference.Width, reference.Height);
        var border = style.Border.Resolve(reference.Width, reference.Height);
        var chromeWidth = padding.Horizontal + border.Horizontal;
        var chromeHeight = padding.Vertical + border.Vertical;
        return new ResolvedMetrics(
            margin,
            padding,
            border,
            ToOuter(style.Width.Resolve(reference.Width), chromeWidth, style.BoxSizing),
            ToOuter(ResolveVertical(style.Height, reference.Height, resolvePercentageHeight), chromeHeight, style.BoxSizing),
            ToOuter(style.MinWidth.Resolve(reference.Width), chromeWidth, style.BoxSizing),
            ToOuter(ResolveVertical(style.MinHeight, reference.Height, resolvePercentageHeight), chromeHeight, style.BoxSizing),
            ToOuter(style.MaxWidth.Resolve(reference.Width), chromeWidth, style.BoxSizing),
            ToOuter(ResolveVertical(style.MaxHeight, reference.Height, resolvePercentageHeight), chromeHeight, style.BoxSizing));
    }

    private static double? ResolveVertical(
        CssLayoutLength length,
        double reference,
        bool resolvePercentageHeight)
        => !resolvePercentageHeight && length.Unit == CssLayoutLengthUnit.Percent
            ? null
            : length.Resolve(reference);

    private static bool IsScrollableOverflow(CssLayoutOverflow overflow)
        => overflow is CssLayoutOverflow.Hidden or CssLayoutOverflow.Auto or CssLayoutOverflow.Scroll;

    private static CssLayoutBox CreateBox(
        CssLayoutNode node,
        WebSceneRect borderBox,
        WebSceneSize reference,
        bool includeMargin = true)
    {
        var metrics = ResolveMetrics(node, reference);
        var paddingBox = Inset(borderBox, metrics.Border);
        var contentBox = Inset(paddingBox, metrics.Padding);
        var marginBox = includeMargin ? Expand(borderBox, metrics.Margin) : borderBox;
        return new CssLayoutBox(marginBox, borderBox, paddingBox, contentBox);
    }

    private static void ApplyRelativeOffset(
        CssLayoutStyle style,
        WebSceneRect content,
        bool percentageHeightBasisIsIndefinite,
        ref double x,
        ref double y)
    {
        if (style.Position != CssLayoutPosition.Relative)
        {
            return;
        }
        x += style.Left.Resolve(content.Width) ?? 0;
        y += ResolveVertical(
                 style.Top,
                 content.Height,
                 resolvePercentageHeight: !percentageHeightBasisIsIndefinite)
             ?? 0;
    }

    private static (double Offset, double Between) ResolveJustification(
        CssLayoutJustifyContent value,
        double free,
        int count,
        double gap)
        => value switch
        {
            CssLayoutJustifyContent.FlexEnd => (free, gap),
            CssLayoutJustifyContent.Center => (free / 2, gap),
            CssLayoutJustifyContent.SpaceBetween when count > 1 => (0, gap + free / (count - 1)),
            CssLayoutJustifyContent.SpaceAround when count > 0 => (free / count / 2, gap + free / count),
            CssLayoutJustifyContent.SpaceEvenly when count > 0 => (free / (count + 1), gap + free / (count + 1)),
            _ => (0, gap)
        };

    private static (double Offset, double Between) ResolveContentAlignment(
        CssLayoutAlignContent value,
        double free,
        int count,
        double gap)
        => value switch
        {
            CssLayoutAlignContent.FlexEnd => (free, gap),
            CssLayoutAlignContent.Center => (free / 2, gap),
            CssLayoutAlignContent.SpaceBetween when count > 1 => (0, gap + free / (count - 1)),
            CssLayoutAlignContent.SpaceAround when count > 0 => (free / count / 2, gap + free / count),
            CssLayoutAlignContent.SpaceEvenly when count > 0 => (free / (count + 1), gap + free / (count + 1)),
            _ => (0, gap)
        };

    private static double? ToOuter(double? value, double chrome, CssLayoutBoxSizing boxSizing)
        => value.HasValue
            ? Math.Max(0, value.Value + (boxSizing == CssLayoutBoxSizing.ContentBox ? chrome : 0))
            : null;

    private static double Clamp(double value, double? minimum, double? maximum)
        => Math.Max(minimum ?? 0, Math.Min(maximum ?? double.PositiveInfinity, value));

    private static WebSceneRect Inset(WebSceneRect rect, ResolvedCssEdges edges)
        => new(
            rect.X + edges.Left,
            rect.Y + edges.Top,
            Math.Max(0, rect.Width - edges.Horizontal),
            Math.Max(0, rect.Height - edges.Vertical));

    private static WebSceneRect Expand(WebSceneRect rect, ResolvedCssEdges edges)
        => new(
            rect.X - edges.Left,
            rect.Y - edges.Top,
            rect.Width + edges.Horizontal,
            rect.Height + edges.Vertical);

    private static bool IsInline(CssLayoutDisplay display)
        => display is CssLayoutDisplay.Inline
            or CssLayoutDisplay.InlineBlock
            or CssLayoutDisplay.InlineFlex
            or CssLayoutDisplay.InlineGrid
            or CssLayoutDisplay.InlineTable;

    private static bool UsesShrinkToFitInlineWidth(CssLayoutDisplay display)
        => display is CssLayoutDisplay.InlineBlock
            or CssLayoutDisplay.InlineFlex
            or CssLayoutDisplay.InlineGrid
            or CssLayoutDisplay.InlineTable
            or CssLayoutDisplay.Table;

    private static bool IsValid(WebSceneSize size)
        => double.IsFinite(size.Width) && double.IsFinite(size.Height) && size.Width >= 0 && size.Height >= 0;

    private static bool IsValid(WebSceneRect rect)
        => double.IsFinite(rect.X)
           && double.IsFinite(rect.Y)
           && IsValid(rect.Size);

    private readonly record struct IndexedNode(CssLayoutNode Node, int Index);

    private readonly record struct FixedTableColumnConstraint(
        CssLayoutNode? Source,
        CssLayoutLength Width,
        CssTableColumnWidthConstraint Distribution)
    {
        internal static FixedTableColumnConstraint Auto { get; } = new(
            null,
            CssLayoutLength.Auto,
            new CssTableColumnWidthConstraint(CssTableColumnWidthKind.Auto, 0));
    }

    private readonly record struct ResolvedMetrics(
        ResolvedCssEdges Margin,
        ResolvedCssEdges Padding,
        ResolvedCssEdges Border,
        double? OuterWidth,
        double? OuterHeight,
        double? MinOuterWidth,
        double? MinOuterHeight,
        double? MaxOuterWidth,
        double? MaxOuterHeight);

    private sealed class FlexItem(CssLayoutNode node)
    {
        internal CssLayoutNode Node { get; } = node;
        internal double Main { get; set; }
        internal double MinimumMain { get; init; }
        internal double Cross { get; init; }
        internal bool HasExplicitCross { get; init; }
        internal double Grow { get; init; }
        internal double Shrink { get; init; }
        internal double MainMarginStart { get; set; }
        internal double MainMarginEnd { get; set; }
        internal bool MainMarginStartIsAuto { get; init; }
        internal bool MainMarginEndIsAuto { get; init; }
        internal double CrossMarginStart { get; init; }
        internal double CrossMarginEnd { get; init; }
    }

    private sealed class FlexLine
    {
        internal List<FlexItem> Items { get; } = [];
        internal double CrossSize { get; set; }
        internal double FirstBaseline { get; set; }
    }
}

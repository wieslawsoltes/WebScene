using WebScene.Core;

namespace WebScene.Css;

/// <summary>Backend intrinsic measurement callback used by portable CSS measurement.</summary>
public interface ICssIntrinsicMeasurer
{
    WebSceneSize Measure(long nodeId, WebSceneSize availableSize);
}

/// <summary>
/// Produces CSS desired size and backend measurement constraints without UI-framework
/// types. Backends only answer intrinsic-size requests; CSS sizing policy remains here.
/// </summary>
public sealed class CssMeasurementEngine
{
    public WebSceneSize Measure(CssLayoutNode root, WebSceneSize availableSize, ICssIntrinsicMeasurer measurer)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentNullException.ThrowIfNull(measurer);
        if (!IsMeasureSize(availableSize))
        {
            throw new ArgumentOutOfRangeException(nameof(availableSize));
        }

        // Column and column-group boxes describe table tracks, but do not
        // produce rendered geometry of their own. CSS table fix-up also
        // suppresses their non-track descendants instead of measuring those
        // descendants as ordinary block content.
        if (IsNonRenderedTableTrack(root.Style.Display))
        {
            return WebSceneSize.Empty;
        }

        var border = root.Style.Border.Resolve(FiniteOrZero(availableSize.Width), FiniteOrZero(availableSize.Height));
        var padding = root.Style.Padding.Resolve(FiniteOrZero(availableSize.Width), FiniteOrZero(availableSize.Height));
        var chromeWidth = border.Horizontal + padding.Horizontal;
        var chromeHeight = border.Vertical + padding.Vertical;
        var contentAvailable = new WebSceneSize(
            SubtractFinite(availableSize.Width, chromeWidth),
            SubtractFinite(availableSize.Height, chromeHeight));
        var contentDesired = root.Style.Display switch
        {
            CssLayoutDisplay.Flex or CssLayoutDisplay.InlineFlex
                => MeasureFlex(root, contentAvailable, measurer, chromeWidth, chromeHeight),
            CssLayoutDisplay.Grid or CssLayoutDisplay.InlineGrid
                => MeasureGrid(root, contentAvailable, measurer),
            CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable
                => MeasureTable(root, contentAvailable, measurer),
            CssLayoutDisplay.TableRowGroup or CssLayoutDisplay.TableHeaderGroup or CssLayoutDisplay.TableFooterGroup
                => MeasureTableRowGroup(root, contentAvailable, measurer),
            CssLayoutDisplay.TableRow
                => MeasureTableRow(root, contentAvailable, measurer),
            _ => MeasureBlock(root, contentAvailable, measurer)
        };

        var preserveIntrinsicWidth = root.Style.Display is CssLayoutDisplay.Table or CssLayoutDisplay.InlineTable;
        return new WebSceneSize(
            preserveIntrinsicWidth
                ? contentDesired.Width + chromeWidth
                : LimitToAvailable(contentDesired.Width + chromeWidth, availableSize.Width),
            LimitToAvailable(contentDesired.Height + chromeHeight, availableSize.Height));
    }

    private static WebSceneSize MeasureTable(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var rows = CssTableModelFixup.EnumerateRows(root, tableRoot: true).ToArray();
        if (rows.Length == 0)
        {
            // CSS table fix-up wraps improper direct children in anonymous
            // cell and row boxes. For a table made entirely from such content,
            // its intrinsic size is the normal-flow size of that anonymous
            // cell rather than an empty table.
            return MeasureBlock(root, available, measurer);
        }

        var columns = ResolveIntrinsicTableColumns(rows, available, measurer, root);
        var tableWidth = columns.Sum();
        var tableHeight = 0d;
        foreach (var row in rows)
        {
            tableHeight += MeasureTableRowWithColumns(row, columns, available.Width, available.Height, measurer).Height;
        }

        return new WebSceneSize(tableWidth, tableHeight);
    }

    private static WebSceneSize MeasureTableRowGroup(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var rows = CssTableModelFixup.EnumerateRows(root, tableRoot: false).ToArray();
        var columns = ResolveIntrinsicTableColumns(rows, available, measurer);
        return new WebSceneSize(
            columns.Sum(),
            rows.Sum(row => MeasureTableRowWithColumns(row, columns, available.Width, available.Height, measurer).Height));
    }

    private static WebSceneSize MeasureTableRow(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var row = CssTableModelFixup.CreateRow(root, root.Children);
        var columns = ResolveIntrinsicTableColumns([row], available, measurer);
        return MeasureTableRowWithColumns(row, columns, available.Width, available.Height, measurer);
    }

    private static double[] ResolveIntrinsicTableColumns(
        IReadOnlyList<CssEffectiveTableRow> rows,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer,
        CssLayoutNode? table = null)
    {
        var tracks = table is null ? [] : CssTableModelFixup.EnumerateColumnTracks(table).ToArray();
        var columnCount = Math.Max(
            tracks.Length,
            rows.Count == 0 ? 0 : rows.Max(static row => row.Cells.Count));
        var columns = new double[columnCount];
        var percentages = new double[columnCount];
        var percentageOffsets = new double[columnCount];
        for (var column = 0; column < tracks.Length; column++)
        {
            var track = tracks[column];
            if (track.Style.Width.Unit == CssLayoutLengthUnit.Percent)
            {
                percentages[column] = Math.Max(0, track.Style.Width.Value) / 100d;
                percentageOffsets[column] = track.Style.Width.PixelOffset;
            }
            else
            {
                columns[column] = Math.Max(
                    columns[column],
                    ResolveForMeasure(track.Style.Width, available.Width) ?? 0);
            }
        }
        foreach (var row in rows)
        {
            var column = 0;
            foreach (var effectiveCell in row.Cells)
            {
                if (effectiveCell.Cell is null)
                {
                    columns[column] = Math.Max(
                        columns[column],
                        MeasureAnonymousTableCell(
                            effectiveCell.Children,
                            available,
                            measurer).Width);
                    column++;
                    continue;
                }
                var cell = effectiveCell.Cell;
                var metrics = ResolveChrome(cell.Style, available.Width, available.Height);
                var intrinsic = measurer.Measure(
                    cell.Id,
                    new WebSceneSize(double.PositiveInfinity, double.PositiveInfinity));
                var percentageWidth = cell.Style.Width.Unit == CssLayoutLengthUnit.Percent;
                var declared = percentageWidth
                    ? null
                    : ToOuter(
                        ResolveForMeasure(cell.Style.Width, available.Width),
                        metrics.HorizontalChrome,
                        cell.Style.BoxSizing);
                var width = Constrain(
                                declared ?? intrinsic.Width,
                                ToOuter(
                                    ResolveForMeasure(cell.Style.MinWidth, available.Width),
                                    metrics.HorizontalChrome,
                                    cell.Style.BoxSizing),
                                ToOuter(
                                    ResolveForMeasure(cell.Style.MaxWidth, available.Width),
                                    metrics.HorizontalChrome,
                                    cell.Style.BoxSizing))
                            + metrics.Margin.Horizontal;
                columns[column] = Math.Max(columns[column], FiniteOrZero(width));
                if (percentageWidth)
                {
                    percentages[column] = Math.Max(
                        percentages[column],
                        Math.Max(0, cell.Style.Width.Value) / 100d);
                    percentageOffsets[column] = Math.Max(
                        percentageOffsets[column],
                        cell.Style.Width.PixelOffset
                        + (cell.Style.BoxSizing == CssLayoutBoxSizing.ContentBox
                            ? metrics.HorizontalChrome
                            : 0)
                        + metrics.Margin.Horizontal);
                }
                column++;
            }
        }
        ResolvePercentageTableTracks(columns, percentages, percentageOffsets, available.Width);
        return columns;
    }

    private static void ResolvePercentageTableTracks(
        double[] columns,
        IReadOnlyList<double> percentages,
        IReadOnlyList<double> offsets,
        double availableWidth)
    {
        var tableWidth = columns.Sum();
        for (var iteration = 0; iteration < columns.Length + 2; iteration++)
        {
            var activePercent = 0d;
            var fixedNumerator = 0d;
            for (var column = 0; column < columns.Length; column++)
            {
                var percentageCandidate = percentages[column] * tableWidth + offsets[column];
                if (percentages[column] > 0 && percentageCandidate > columns[column])
                {
                    activePercent += percentages[column];
                    fixedNumerator += offsets[column];
                }
                else
                {
                    fixedNumerator += columns[column];
                }
            }

            var nextWidth = activePercent < 1
                ? fixedNumerator / (1 - activePercent)
                : double.IsFinite(availableWidth)
                    ? availableWidth
                    : tableWidth;
            if (Math.Abs(nextWidth - tableWidth) < 0.0000001)
            {
                tableWidth = nextWidth;
                break;
            }
            tableWidth = nextWidth;
        }

        for (var column = 0; column < columns.Length; column++)
        {
            columns[column] = Math.Max(
                columns[column],
                percentages[column] * tableWidth + offsets[column]);
        }
    }

    private static WebSceneSize MeasureTableRowWithColumns(
        CssEffectiveTableRow row,
        IReadOnlyList<double> columns,
        double availableWidth,
        double availableHeight,
        ICssIntrinsicMeasurer measurer)
    {
        var height = row.Row is null
            ? 0
            : ResolveForMeasure(row.Row.Style.Height, availableHeight) ?? 0;
        var column = 0;
        foreach (var effectiveCell in row.Cells)
        {
            var columnWidth = column < columns.Count ? columns[column] : 0;
            if (effectiveCell.Cell is null)
            {
                height = Math.Max(
                    height,
                    MeasureAnonymousTableCell(
                        effectiveCell.Children,
                        new WebSceneSize(
                            double.IsFinite(availableWidth)
                                ? Math.Max(columnWidth, availableWidth)
                                : columnWidth,
                            availableHeight),
                        measurer).Height);
                column++;
                continue;
            }
            var cell = effectiveCell.Cell;
            var metrics = ResolveChrome(cell.Style, columnWidth, availableHeight);
            var measured = measurer.Measure(
                cell.Id,
                new WebSceneSize(
                    Math.Max(0, columnWidth - metrics.Margin.Horizontal),
                    double.PositiveInfinity));
            var declaredHeight = ToOuter(
                ResolveForMeasure(cell.Style.Height, availableHeight),
                metrics.VerticalChrome,
                cell.Style.BoxSizing);
            height = Math.Max(height, (declaredHeight ?? measured.Height) + metrics.Margin.Vertical);
            column++;
        }
        return new WebSceneSize(columns.Sum(), height);
    }

    private static WebSceneSize MeasureAnonymousTableCell(
        IReadOnlyList<CssLayoutNode> children,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var flow = children.Where(CssTableModelFixup.IsFlowChild)
            .Where(static child => child.Style.Display is not (
                CssLayoutDisplay.TableColumn or CssLayoutDisplay.TableColumnGroup))
            .ToArray();
        var desiredWidth = 0d;
        var desiredHeight = 0d;
        var inlineWidth = 0d;
        var inlineHeight = 0d;
        for (var index = 0; index < flow.Length; index++)
        {
            var child = flow[index];
            if (child.IsCollapsibleWhitespace)
            {
                if (HasInlineContentOnBothSides(flow, index))
                    inlineWidth += child.CollapsedWhitespaceWidth;
                continue;
            }
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(available.Width, available.Height));
            if (IsInline(child.Style.Display))
            {
                inlineWidth += measured.Width;
                inlineHeight = Math.Max(inlineHeight, measured.Height);
            }
            else
            {
                desiredWidth = Math.Max(desiredWidth, inlineWidth);
                desiredHeight += inlineHeight;
                inlineWidth = 0;
                inlineHeight = 0;
                desiredWidth = Math.Max(desiredWidth, measured.Width);
                desiredHeight += measured.Height;
            }
        }
        return new WebSceneSize(Math.Max(desiredWidth, inlineWidth), desiredHeight + inlineHeight);
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
               && IsInline(previous.Style.Display) && IsInline(next.Style.Display);
    }

    private static WebSceneSize MeasureBlock(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var availableWidth = FiniteOrInfinity(available.Width);
        var availableHeight = FiniteOrInfinity(available.Height);
        var desiredWidth = 0d;
        var desiredHeight = 0d;
        CssLayoutListMarker? insideMarker =
            root.Style.Display == CssLayoutDisplay.ListItem
            && root.Style.ListMarker is { Outside: false } marker
                ? marker
                : null;
        var inlineWidth = insideMarker?.InlineAdvance ?? 0;
        var inlineHeight = insideMarker is { } initialMarker
            ? Math.Max(initialMarker.IntrinsicSize.Height, initialMarker.LineHeight)
            : 0;
        double? pendingBlockMargin = null;
        var canCollapseTopMargin = CanCollapseBlockChildMargin(root.Style, top: true, available);
        var canCollapseBottomMargin = CanCollapseBlockChildMargin(root.Style, top: false, available);
        var ownMargin = root.Style.Margin.Resolve(available.Width, available.Height);

        void FlushInlineLine()
        {
            if (inlineHeight > 0 && pendingBlockMargin is { } precedingBlockMargin)
            {
                desiredHeight += precedingBlockMargin;
                pendingBlockMargin = null;
            }
            desiredWidth = Math.Max(desiredWidth, inlineWidth);
            desiredHeight += inlineHeight;
            inlineWidth = 0;
            inlineHeight = 0;
        }

        var flow = root.Children.Where(static child =>
                (child.Style.Display != CssLayoutDisplay.None || child.IsCollapsibleWhitespace)
                && !IsNonRenderedTableTrack(child.Style.Display))
            .ToArray();
        for (var childIndex = 0; childIndex < flow.Length; childIndex++)
        {
            var child = flow[childIndex];
            if (child.IsCollapsibleWhitespace)
            {
                if (HasInlineContentOnBothSides(flow, childIndex))
                {
                    inlineWidth += child.CollapsedWhitespaceWidth;
                }
                continue;
            }
            if (child.ForcesLineBreak)
            {
                FlushInlineLine();
                continue;
            }
            var style = child.Style;
            var positioned = style.Position is CssLayoutPosition.Absolute or CssLayoutPosition.Fixed;
            var width = ResolveForMeasure(style.Width, availableWidth);
            var height = ResolveForMeasure(style.Height, availableHeight);
            var minWidth = ResolveForMeasure(style.MinWidth, availableWidth);
            var minHeight = ResolveForMeasure(style.MinHeight, availableHeight);
            var maxWidth = ResolveForMeasure(style.MaxWidth, availableWidth);
            var maxHeight = ResolveForMeasure(style.MaxHeight, availableHeight);
            var metrics = ResolveChrome(style, availableWidth, availableHeight);
            var outerWidth = ToOuter(width, metrics.HorizontalChrome, style.BoxSizing);
            var outerHeight = ToOuter(height, metrics.VerticalChrome, style.BoxSizing);
            var inline = child.IsText || IsInline(style.Display);
            var positionedAutoWidth = positioned
                                      && !width.HasValue
                                      && !(ResolveForMeasure(style.Left, availableWidth).HasValue
                                           && ResolveForMeasure(style.Right, availableWidth).HasValue);
            var positionedAutoHeight = positioned
                                       && !height.HasValue
                                       && !(ResolveForMeasure(style.Top, availableHeight).HasValue
                                            && ResolveForMeasure(style.Bottom, availableHeight).HasValue);
            var wrappingTextWidth = child.IsText
                                    && !root.Style.NoWrap
                                    && double.IsFinite(availableWidth)
                ? Math.Max(0, availableWidth - inlineWidth - metrics.Margin.Horizontal)
                : double.PositiveInfinity;
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(
                    positionedAutoWidth
                        ? double.PositiveInfinity
                        : Constrain(
                            outerWidth ?? (inline
                                ? wrappingTextWidth
                                : SubtractFinite(availableWidth, metrics.Margin.Horizontal)),
                            minWidth,
                            maxWidth),
                    positionedAutoHeight
                        ? double.PositiveInfinity
                        : Constrain(
                            outerHeight ?? double.PositiveInfinity,
                            minHeight,
                            maxHeight)));
            if (positioned)
            {
                continue;
            }

            var usedOuterWidth = Constrain(
                outerWidth ?? measured.Width,
                ToOuter(minWidth, metrics.HorizontalChrome, style.BoxSizing),
                ToOuter(maxWidth, metrics.HorizontalChrome, style.BoxSizing));
            var usedOuterHeight = Constrain(
                outerHeight ?? measured.Height,
                ToOuter(minHeight, metrics.VerticalChrome, style.BoxSizing),
                ToOuter(maxHeight, metrics.VerticalChrome, style.BoxSizing));
            var itemWidth = FiniteOrZero(usedOuterWidth) + metrics.Margin.Horizontal;
            var itemHeight = (child.HasZeroLineHeight ? 0 : FiniteOrZero(usedOuterHeight))
                             + metrics.Margin.Vertical;
            if (inline)
            {
                if (!root.Style.NoWrap
                    && inlineWidth > 0
                    && double.IsFinite(availableWidth)
                    && inlineWidth + itemWidth > availableWidth)
                {
                    FlushInlineLine();
                }
                inlineWidth += itemWidth;
                inlineHeight = Math.Max(
                    inlineHeight,
                    Math.Max(insideMarker?.LineHeight ?? 0, itemHeight));
            }
            else
            {
                FlushInlineLine();
                desiredWidth = Math.Max(desiredWidth, itemWidth);
                var collapsedBefore = pendingBlockMargin is { } previousBottomMargin
                    ? CollapseVerticalMargins(previousBottomMargin, metrics.Margin.Top)
                    : canCollapseTopMargin
                      && desiredHeight == 0
                      && ParentMarginDominatesChildMargin(ownMargin.Top, metrics.Margin.Top)
                        ? 0
                        : metrics.Margin.Top;
                desiredHeight += collapsedBefore
                                 + (child.HasZeroLineHeight ? 0 : FiniteOrZero(usedOuterHeight));
                pendingBlockMargin = metrics.Margin.Bottom;
            }
        }

        FlushInlineLine();
        if (pendingBlockMargin is { } trailingBlockMargin
            && (!canCollapseBottomMargin
                || !ParentMarginDominatesChildMargin(ownMargin.Bottom, trailingBlockMargin)))
        {
            desiredHeight += trailingBlockMargin;
        }
        return new WebSceneSize(
            LimitToAvailable(FiniteOrZero(desiredWidth), availableWidth),
            LimitToAvailable(FiniteOrZero(desiredHeight), availableHeight));
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

    private static WebSceneSize MeasureFlex(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer,
        double chromeWidth,
        double chromeHeight)
    {
        var row = root.Style.FlexDirection is CssLayoutFlexDirection.Row or CssLayoutFlexDirection.RowReverse;
        var availableWidth = FiniteOrInfinity(available.Width);
        var availableHeight = FiniteOrInfinity(available.Height);
        var availableMain = row ? availableWidth : availableHeight;
        var wraps = root.Style.FlexWrap != CssLayoutFlexWrap.NoWrap && double.IsFinite(availableMain);
        var mainGap = Math.Max(0, ResolveForMeasure(
            row ? root.Style.ColumnGap : root.Style.RowGap,
            availableMain) ?? 0);
        var crossGap = Math.Max(0, ResolveForMeasure(
            row ? root.Style.RowGap : root.Style.ColumnGap,
            row ? availableHeight : availableWidth) ?? 0);
        var lineMain = 0d;
        var lineCross = 0d;
        var lineCount = 0;
        var measuredMain = 0d;
        var measuredCross = 0d;
        var measuredLineCount = 0;
        var containerChrome = row ? chromeWidth : chromeHeight;
        var containerMinMain = ToContent(
            ResolveForMeasure(
                row ? root.Style.MinWidth : root.Style.MinHeight,
                availableMain),
            containerChrome,
            root.Style.BoxSizing);
        var containerMaxMain = ToContent(
            ResolveForMeasure(
                row ? root.Style.MaxWidth : root.Style.MaxHeight,
                availableMain),
            containerChrome,
            root.Style.BoxSizing);

        void FlushLine()
        {
            if (lineCount == 0)
            {
                return;
            }

            measuredMain = Math.Max(measuredMain, lineMain);
            measuredCross += (measuredLineCount > 0 ? crossGap : 0) + lineCross;
            measuredLineCount++;
            lineMain = 0;
            lineCross = 0;
            lineCount = 0;
        }

        foreach (var child in root.Children.Where(static child =>
                     child.Style.Display != CssLayoutDisplay.None
                     && !IsNonRenderedTableTrack(child.Style.Display)))
        {
            var style = child.Style;
            var positioned = style.Position is CssLayoutPosition.Absolute or CssLayoutPosition.Fixed;
            var width = ResolveForMeasure(style.Width, availableWidth);
            var height = ResolveForMeasure(style.Height, availableHeight);
            var metrics = ResolveChrome(style, availableWidth, availableHeight);
            var outerWidth = ToOuter(width, metrics.HorizontalChrome, style.BoxSizing);
            var outerHeight = ToOuter(height, metrics.VerticalChrome, style.BoxSizing);
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(
                    Constrain(
                        outerWidth ?? availableWidth,
                        ToOuter(ResolveForMeasure(style.MinWidth, availableWidth), metrics.HorizontalChrome, style.BoxSizing),
                        ToOuter(ResolveForMeasure(style.MaxWidth, availableWidth), metrics.HorizontalChrome, style.BoxSizing)),
                    Constrain(
                        outerHeight ?? availableHeight,
                        ToOuter(ResolveForMeasure(style.MinHeight, availableHeight), metrics.VerticalChrome, style.BoxSizing),
                        ToOuter(ResolveForMeasure(style.MaxHeight, availableHeight), metrics.VerticalChrome, style.BoxSizing))));
            if (positioned)
            {
                continue;
            }

            var basis = ResolveForMeasure(style.FlexBasis, row ? availableWidth : availableHeight);
            var itemMain = basis ?? (row ? outerWidth ?? measured.Width : outerHeight ?? measured.Height);
            var itemCross = row ? outerHeight ?? measured.Height : outerWidth ?? measured.Width;
            var outerMain = FiniteOrZero(itemMain) + (row ? metrics.Margin.Horizontal : metrics.Margin.Vertical);
            var outerCross = FiniteOrZero(itemCross) + (row ? metrics.Margin.Vertical : metrics.Margin.Horizontal);
            var candidateMain = lineMain + (lineCount > 0 ? mainGap : 0) + outerMain;
            if (wraps && lineCount > 0 && candidateMain > availableMain)
            {
                FlushLine();
                candidateMain = outerMain;
            }

            lineMain = candidateMain;
            lineCross = Math.Max(lineCross, outerCross);
            lineCount++;
        }

        FlushLine();
        measuredMain = Constrain(measuredMain, containerMinMain, containerMaxMain);
        var desired = row
            ? new WebSceneSize(measuredMain, measuredCross)
            : new WebSceneSize(measuredCross, measuredMain);
        return new WebSceneSize(
            LimitToAvailable(desired.Width, availableWidth),
            LimitToAvailable(desired.Height, availableHeight));
    }

    private static WebSceneSize MeasureGrid(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        if (TryParseFixedPixelTracks(root.Style.GridTemplateColumns, out var fixedTracks))
        {
            _ = TryParseFixedPixelTracks(root.Style.GridTemplateRows, out var fixedRowTracks);
            return MeasureFixedPixelGrid(root, available, measurer, fixedTracks, fixedRowTracks);
        }

        if (UsesAutoFractionColumns(root.Style))
        {
            return MeasureAutoFractionGrid(root, available, measurer);
        }

        var availableWidth = FiniteOrInfinity(available.Width);
        var availableHeight = FiniteOrInfinity(available.Height);
        var width = 0d;
        var height = 0d;
        foreach (var child in root.Children.Where(static child =>
                     child.Style.Display != CssLayoutDisplay.None
                     && !IsNonRenderedTableTrack(child.Style.Display)
                     && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)))
        {
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(
                    ResolveForMeasure(child.Style.Width, availableWidth) ?? availableWidth,
                    ResolveForMeasure(child.Style.Height, availableHeight) ?? availableHeight));
            width += measured.Width;
            height = Math.Max(height, measured.Height);
        }
        return new WebSceneSize(
            LimitToAvailable(width, availableWidth),
            LimitToAvailable(height, availableHeight));
    }

    private static WebSceneSize MeasureFixedPixelGrid(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer,
        IReadOnlyList<double> tracks,
        IReadOnlyList<double> fixedRowTracks)
    {
        var availableWidth = FiniteOrInfinity(available.Width);
        var availableHeight = FiniteOrInfinity(available.Height);
        var columnGap = Math.Max(0, ResolveForMeasure(root.Style.ColumnGap, availableWidth) ?? 0);
        var rowGap = Math.Max(0, ResolveForMeasure(root.Style.RowGap, availableHeight) ?? 0);
        var rows = fixedRowTracks.ToList();
        var row = 0;
        var column = 0;
        foreach (var child in root.Children.Where(static child =>
                     child.Style.Display != CssLayoutDisplay.None
                     && !IsNonRenderedTableTrack(child.Style.Display)
                     && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)))
        {
            var full = tracks.Count == 2 && SpansBothColumns(child.Style.GridColumn);
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            var metrics = ResolveChrome(child.Style, availableWidth, availableHeight);
            var trackWidth = full
                ? tracks.Sum() + columnGap * Math.Max(0, tracks.Count - 1)
                : tracks[column];
            var declaredWidth = ResolveForMeasure(child.Style.Width, trackWidth);
            var rowTrackHeight = row < fixedRowTracks.Count ? fixedRowTracks[row] : (double?)null;
            var declaredHeight = ResolveForMeasure(child.Style.Height, rowTrackHeight ?? availableHeight);
            var childWidth = declaredWidth.HasValue
                ? ToOuter(declaredWidth, metrics.HorizontalChrome, child.Style.BoxSizing) ?? trackWidth
                : Math.Max(0, trackWidth - metrics.Margin.Horizontal);
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(
                    childWidth,
                    declaredHeight ?? (rowTrackHeight.HasValue
                        ? Math.Max(0, rowTrackHeight.Value - metrics.Margin.Vertical)
                        : availableHeight)));
            var itemHeight = (ToOuter(
                                  declaredHeight,
                                  metrics.VerticalChrome,
                                  child.Style.BoxSizing)
                              ?? measured.Height) + metrics.Margin.Vertical;
            while (rows.Count <= row) rows.Add(0);
            if (row >= fixedRowTracks.Count)
            {
                rows[row] = Math.Max(rows[row], itemHeight);
            }
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

        var desiredWidth = tracks.Sum() + columnGap * Math.Max(0, tracks.Count - 1);
        var desiredHeight = rows.Sum() + rowGap * Math.Max(0, rows.Count - 1);
        return new WebSceneSize(
            LimitToAvailable(desiredWidth, availableWidth),
            LimitToAvailable(desiredHeight, availableHeight));
    }

    private static WebSceneSize MeasureAutoFractionGrid(
        CssLayoutNode root,
        WebSceneSize available,
        ICssIntrinsicMeasurer measurer)
    {
        var availableWidth = FiniteOrInfinity(available.Width);
        var availableHeight = FiniteOrInfinity(available.Height);
        var firstTrack = 0d;
        var secondTrack = 0d;
        var fullWidth = 0d;
        var rows = new List<double>();
        var row = 0;
        var column = 0;
        foreach (var child in root.Children.Where(static child =>
                     child.Style.Display != CssLayoutDisplay.None
                     && !IsNonRenderedTableTrack(child.Style.Display)
                     && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)))
        {
            var metrics = ResolveChrome(child.Style, availableWidth, availableHeight);
            var measured = measurer.Measure(
                child.Id,
                new WebSceneSize(
                    ResolveForMeasure(child.Style.Width, availableWidth) ?? availableWidth,
                    ResolveForMeasure(child.Style.Height, availableHeight) ?? availableHeight));
            var itemWidth = (ToOuter(
                             ResolveForMeasure(child.Style.Width, availableWidth),
                             metrics.HorizontalChrome,
                             child.Style.BoxSizing)
                         ?? measured.Width) + metrics.Margin.Horizontal;
            var itemHeight = (ToOuter(
                              ResolveForMeasure(child.Style.Height, availableHeight),
                              metrics.VerticalChrome,
                              child.Style.BoxSizing)
                          ?? measured.Height) + metrics.Margin.Vertical;
            var full = SpansBothColumns(child.Style.GridColumn);
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            while (rows.Count <= row) rows.Add(0);
            rows[row] = Math.Max(rows[row], itemHeight);
            if (full)
            {
                fullWidth = Math.Max(fullWidth, itemWidth);
                row++;
                column = 0;
            }
            else
            {
                if (column == 0) firstTrack = Math.Max(firstTrack, itemWidth);
                else secondTrack = Math.Max(secondTrack, itemWidth);
                if (++column == 2)
                {
                    row++;
                    column = 0;
                }
            }
        }

        var columnGap = Math.Max(0, ResolveForMeasure(root.Style.ColumnGap, availableWidth) ?? 0);
        var rowGap = Math.Max(0, ResolveForMeasure(root.Style.RowGap, availableHeight) ?? 0);
        var desiredWidth = Math.Max(fullWidth, firstTrack + columnGap + secondTrack);
        var desiredHeight = rows.Sum() + rowGap * Math.Max(0, rows.Count - 1);
        return new WebSceneSize(
            LimitToAvailable(desiredWidth, availableWidth),
            LimitToAvailable(desiredHeight, availableHeight));
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

    private static ResolvedMeasureMetrics ResolveChrome(CssLayoutStyle style, double width, double height)
    {
        var widthReference = FiniteOrZero(width);
        var heightReference = FiniteOrZero(height);
        var margin = style.Margin.Resolve(widthReference, heightReference);
        var padding = style.Padding.Resolve(widthReference, heightReference);
        var border = style.Border.Resolve(widthReference, heightReference);
        return new ResolvedMeasureMetrics(
            margin,
            padding.Horizontal + border.Horizontal,
            padding.Vertical + border.Vertical);
    }

    private static double? ResolveForMeasure(CssLayoutLength length, double reference)
        => !double.IsFinite(reference) && length.Unit == CssLayoutLengthUnit.Percent
            ? null
            : length.Resolve(reference);

    private static double? ToOuter(double? value, double chrome, CssLayoutBoxSizing boxSizing)
        => value.HasValue && boxSizing == CssLayoutBoxSizing.ContentBox ? value + chrome : value;

    private static double? ToContent(double? value, double chrome, CssLayoutBoxSizing boxSizing)
        => value.HasValue && boxSizing == CssLayoutBoxSizing.BorderBox
            ? Math.Max(0, value.Value - chrome)
            : value;

    private static double Constrain(double value, double? minimum, double? maximum)
    {
        value = double.IsNaN(value) ? 0 : Math.Max(0, value);
        // CSS 2.1 width constraint order is significant: apply max-width,
        // then min-width. Therefore min-width wins when the declared minimum
        // is greater than the declared maximum.
        if (maximum.HasValue)
        {
            value = Math.Min(value, Math.Max(0, maximum.Value));
        }
        if (minimum.HasValue)
        {
            value = Math.Max(value, Math.Max(0, minimum.Value));
        }
        return value;
    }

    private static double SubtractFinite(double value, double amount)
        => double.IsFinite(value) ? Math.Max(0, value - amount) : value;

    private static double LimitToAvailable(double value, double available)
        => double.IsFinite(available) ? Math.Min(value, available) : value;

    private static double FiniteOrZero(double value) => double.IsFinite(value) ? value : 0;

    private static double FiniteOrInfinity(double value)
        => double.IsFinite(value) ? Math.Max(0, value) : double.PositiveInfinity;

    private static bool IsInline(CssLayoutDisplay display)
        => display is CssLayoutDisplay.Inline
            or CssLayoutDisplay.InlineBlock
            or CssLayoutDisplay.InlineFlex
            or CssLayoutDisplay.InlineGrid
            or CssLayoutDisplay.InlineTable;

    private static bool IsNonRenderedTableTrack(CssLayoutDisplay display)
        => display is CssLayoutDisplay.TableColumn or CssLayoutDisplay.TableColumnGroup;

    private static bool IsMeasureSize(WebSceneSize size)
        => !double.IsNaN(size.Width)
           && !double.IsNaN(size.Height)
           && size.Width >= 0
           && size.Height >= 0;

    private readonly record struct ResolvedMeasureMetrics(
        ResolvedCssEdges Margin,
        double HorizontalChrome,
        double VerticalChrome);
}

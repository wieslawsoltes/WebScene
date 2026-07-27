namespace WebScene.Css;

internal sealed record CssEffectiveTableCell(
    CssLayoutNode? Cell,
    IReadOnlyList<CssLayoutNode> Children);

internal sealed record CssEffectiveTableRow(
    CssLayoutNode? Row,
    IReadOnlyList<CssEffectiveTableCell> Cells);

internal static class CssTableModelFixup
{
    internal static IEnumerable<CssEffectiveTableRow> EnumerateRows(
        CssLayoutNode root,
        bool tableRoot)
    {
        var improper = new List<CssLayoutNode>();
        foreach (var child in root.Children.Where(IsFlowChild))
        {
            var display = child.Style.Display;
            if (display == CssLayoutDisplay.TableRow)
            {
                foreach (var anonymous in FlushImproper()) yield return anonymous;
                yield return CreateRow(child, child.Children);
            }
            else if (display is CssLayoutDisplay.TableRowGroup
                     or CssLayoutDisplay.TableHeaderGroup
                     or CssLayoutDisplay.TableFooterGroup)
            {
                foreach (var anonymous in FlushImproper()) yield return anonymous;
                foreach (var row in EnumerateRows(child, tableRoot: false)) yield return row;
            }
            else if (tableRoot && IsProperTableRootChild(display))
            {
                foreach (var anonymous in FlushImproper()) yield return anonymous;
            }
            else
            {
                improper.Add(child);
            }
        }
        foreach (var anonymous in FlushImproper()) yield return anonymous;

        IEnumerable<CssEffectiveTableRow> FlushImproper()
        {
            if (improper.Count == 0) yield break;
            if (improper.All(static child => child.IsCollapsibleWhitespace))
            {
                improper.Clear();
                yield break;
            }
            yield return CreateRow(null, improper.ToArray());
            improper.Clear();
        }
    }

    internal static CssEffectiveTableRow CreateRow(
        CssLayoutNode? row,
        IEnumerable<CssLayoutNode> children)
    {
        var cells = new List<CssEffectiveTableCell>();
        var improper = new List<CssLayoutNode>();
        foreach (var child in children.Where(IsFlowChild))
        {
            if (child.Style.Display == CssLayoutDisplay.TableCell)
            {
                FlushImproper();
                cells.Add(new CssEffectiveTableCell(child, [child]));
            }
            else
            {
                improper.Add(child);
            }
        }
        FlushImproper();
        return new CssEffectiveTableRow(row, cells);

        void FlushImproper()
        {
            if (improper.Count == 0) return;
            if (improper.All(static child => child.IsCollapsibleWhitespace))
            {
                improper.Clear();
                return;
            }
            cells.Add(new CssEffectiveTableCell(null, improper.ToArray()));
            improper.Clear();
        }
    }

    internal static IEnumerable<CssLayoutNode> EnumerateColumnTracks(CssLayoutNode table)
    {
        foreach (var child in table.Children)
        {
            if (child.Style.Display == CssLayoutDisplay.TableColumn)
            {
                yield return child;
            }
            else if (child.Style.Display == CssLayoutDisplay.TableColumnGroup)
            {
                foreach (var column in child.Children.Where(static child =>
                             child.Style.Display == CssLayoutDisplay.TableColumn))
                {
                    yield return column;
                }
            }
        }
    }

    internal static bool IsFlowChild(CssLayoutNode child)
        => (child.Style.Display != CssLayoutDisplay.None || child.IsCollapsibleWhitespace)
           && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed);

    internal static bool IsProperTableRootChild(CssLayoutDisplay display)
        => display is CssLayoutDisplay.TableRow
            or CssLayoutDisplay.TableRowGroup
            or CssLayoutDisplay.TableHeaderGroup
            or CssLayoutDisplay.TableFooterGroup
            or CssLayoutDisplay.TableColumn
            or CssLayoutDisplay.TableColumnGroup
            or CssLayoutDisplay.TableCaption;
}

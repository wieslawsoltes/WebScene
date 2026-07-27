using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using WebScene.Core;

namespace JavaScript.Avalonia;

/// <summary>
/// Builds the portable R3 layout input from Avalonia's compatibility attached
/// properties. During migration this enables deterministic dual-run comparisons;
/// the resulting snapshot becomes the arrangement source as each layout slice reaches parity.
/// </summary>
internal static class AvaloniaCssLayoutProjection
{
    internal static Size Measure(CssLayoutPanel root, Size availableSize)
    {
        ArgumentNullException.ThrowIfNull(root);
        var controls = new Dictionary<long, Control>();
        var pseudoIds = new Dictionary<(CssLayoutPanel Host, bool Before), long>();
        long nextId = 0;
        var portableRoot = Build(
            root,
            isRoot: false,
            availableSize,
            controls,
            pseudoIds,
            ref nextId,
            remainingDepth: 1);
        var measured = new CssMeasurementEngine().Measure(
            portableRoot,
            new WebSceneSize(availableSize.Width, availableSize.Height),
            new AvaloniaIntrinsicMeasurer(controls, pseudoIds));
        return new Size(measured.Width, measured.Height);
    }

    internal static AvaloniaCssLayoutSnapshot Capture(CssLayoutPanel root, Size viewport)
        => Capture(
            root,
            viewport,
            new Rect(0, 0, viewport.Width, viewport.Height),
            new Rect(0, 0, viewport.Width, viewport.Height));

    internal static AvaloniaCssLayoutSnapshot Capture(
        CssLayoutPanel root,
        Size viewport,
        Rect inheritedAbsoluteContainingBlock,
        Rect fixedContainingBlock)
    {
        ArgumentNullException.ThrowIfNull(root);
        var controls = new Dictionary<long, Control>();
        var pseudoIds = new Dictionary<(CssLayoutPanel Host, bool Before), long>();
        long nextId = 0;
        var portableRoot = Build(
            root,
            isRoot: true,
            viewport,
            controls,
            pseudoIds,
            ref nextId,
            remainingDepth: int.MaxValue);
        var snapshot = new CssArrangementEngine().Arrange(
            portableRoot,
            new WebSceneSize(viewport.Width, viewport.Height),
            Convert(inheritedAbsoluteContainingBlock),
            Convert(fixedContainingBlock));
        return new AvaloniaCssLayoutSnapshot(snapshot, controls, pseudoIds);
    }

    /// <summary>
    /// Captures the root and its direct children for an Avalonia ArrangeOverride.
    /// Descendant panels receive their own arrange pass, so projecting their complete
    /// subtrees here repeats the same work at every ancestor.
    /// </summary>
    internal static AvaloniaCssLayoutSnapshot CaptureDirect(
        CssLayoutPanel root,
        Size viewport,
        Rect inheritedAbsoluteContainingBlock,
        Rect fixedContainingBlock)
    {
        ArgumentNullException.ThrowIfNull(root);
        var controls = new Dictionary<long, Control>();
        var pseudoIds = new Dictionary<(CssLayoutPanel Host, bool Before), long>();
        long nextId = 0;
        var portableRoot = Build(
            root,
            isRoot: true,
            viewport,
            controls,
            pseudoIds,
            ref nextId,
            remainingDepth: 1);
        var snapshot = new CssArrangementEngine().Arrange(
            portableRoot,
            new WebSceneSize(viewport.Width, viewport.Height),
            Convert(inheritedAbsoluteContainingBlock),
            Convert(fixedContainingBlock));
        return new AvaloniaCssLayoutSnapshot(snapshot, controls, pseudoIds);
    }

    private static WebSceneRect Convert(Rect rect)
        => new(rect.X, rect.Y, rect.Width, rect.Height);

    private static CssLayoutNode Build(
        Control control,
        bool isRoot,
        Size viewport,
        Dictionary<long, Control> controls,
        Dictionary<(CssLayoutPanel Host, bool Before), long> pseudoIds,
        ref long nextId,
        int remainingDepth)
    {
        var id = ++nextId;
        controls[id] = control;
        var style = CreateStyle(control, isRoot, viewport);
        if (control is CssLayoutPanel { ListMarker: { } marker } markerHost
            && CssLayout.GetDisplay(markerHost) == CssDisplay.ListItem)
        {
            style = style with
            {
                ListMarker = new CssLayoutListMarker(
                    -id,
                    new WebSceneSize(marker.Size.Width, marker.Size.Height),
                    marker.LineHeight,
                    marker.InlineAdvance,
                    marker.Position == CssListStylePosition.Outside)
            };
        }
        var desired = control.DesiredSize;
        var children = new List<CssLayoutNode>();

        if (remainingDepth > 0 && control is Panel panel)
        {
            Control? previous = null;
            foreach (var child in panel.Children)
            {
                if (child is IDomInfrastructureControl)
                {
                    continue;
                }
                var childNode = SetBreakBefore(Build(
                        child,
                        isRoot: false,
                        viewport,
                        controls,
                        pseudoIds,
                        ref nextId,
                        remainingDepth == int.MaxValue ? int.MaxValue : remainingDepth - 1),
                    CanBreakBefore(previous, child));
                children.Add(childNode);
                previous = child;
            }
        }

        var intrinsicSize = ResolveProjectedIntrinsicSize(control, style, desired);
        var node = new CssLayoutNode(id, style)
        {
            IntrinsicSize = intrinsicSize,
            IsText = control is TextBlock,
            FirstBaseline = CssLayoutPanel.ResolveFirstBaseline(
                control,
                viewport.Width,
                viewport.Height),
            IsCollapsibleWhitespace = control is TextBlock { Text: { } text }
                                      && string.IsNullOrWhiteSpace(text)
                                      && control is not DomTextBlockControl { CssAllowsLayout: false },
            CollapsedWhitespaceWidth = control is TextBlock whitespace
                ? Math.Max(3, whitespace.FontSize * 0.25)
                : 0,
            HasZeroLineHeight = control is TextBlock && CssLayout.GetLineHeight(control) is <= 0,
            ForcesLineBreak = control is DomLineBreakControl
        };
        if (control is CssLayoutPanel generatedHost
            && remainingDepth > 0
            && ShouldProjectGeneratedPseudo(generatedHost.BeforePseudoElement, style))
        {
            var beforeId = ++nextId;
            pseudoIds[(generatedHost, true)] = beforeId;
            node.Add(CreateGeneratedPseudoNode(beforeId, generatedHost.BeforePseudoElement!));
        }
        foreach (var child in children)
        {
            node.Add(child);
        }
        if (control is CssLayoutPanel afterHost
            && remainingDepth > 0
            && ShouldProjectGeneratedPseudo(afterHost.AfterPseudoElement, style))
        {
            var afterId = ++nextId;
            pseudoIds[(afterHost, false)] = afterId;
            node.Add(CreateGeneratedPseudoNode(afterId, afterHost.AfterPseudoElement!));
        }

        return node;
    }

    private static bool ShouldProjectGeneratedPseudo(
        CssGeneratedPseudoElement? generated,
        CssLayoutStyle hostStyle)
        => generated is not null
           && (!generated.IsInFlow
               || generated.IsFlowBlock
               || generated.IsInlineText
               || hostStyle.Display is CssLayoutDisplay.Flex or CssLayoutDisplay.InlineFlex);

    private static bool IsInline(CssLayoutDisplay display)
        => display is CssLayoutDisplay.Inline
            or CssLayoutDisplay.InlineBlock
            or CssLayoutDisplay.InlineFlex
            or CssLayoutDisplay.InlineGrid
            or CssLayoutDisplay.InlineTable;

    private static CssLayoutNode CreateGeneratedPseudoNode(long id, CssGeneratedPseudoElement generated)
        => new(id, new CssLayoutStyle
        {
            Display = generated.IsInFlow && !generated.IsFlowBlock
                ? CssLayoutDisplay.Inline
                : CssLayoutDisplay.Block,
            Position = generated.IsInFlow ? CssLayoutPosition.Static : CssLayoutPosition.Absolute,
            Width = Convert(generated.Width),
            Height = Convert(generated.Height),
            Left = Convert(generated.Left),
            Top = Convert(generated.Top),
            Right = Convert(generated.Right),
            Bottom = Convert(generated.Bottom),
            TranslateX = Convert(generated.TranslateX, defaultToZero: true),
            TranslateY = Convert(generated.TranslateY, defaultToZero: true),
            Margin = new CssLayoutEdges(
                Convert(generated.MarginTop, defaultToZero: true),
                Convert(generated.MarginRight, defaultToZero: true),
                Convert(generated.MarginBottom, defaultToZero: true),
                Convert(generated.MarginLeft, defaultToZero: true)),
            AlignSelf = ParseAlignment(generated.AlignSelf, CssLayoutAlignment.Auto),
            FlexGrow = generated.FlexGrow,
            FlexShrink = generated.FlexShrink,
            Order = generated.Order
        })
        {
            IntrinsicSize = new WebSceneSize(generated.IntrinsicSize.Width, generated.IntrinsicSize.Height)
        };

    private static WebSceneSize ResolveProjectedIntrinsicSize(
        Control control,
        CssLayoutStyle style,
        Size desired)
    {
        var desiredWidth = double.IsFinite(desired.Width) ? Math.Max(0, desired.Width) : 0;
        var desiredHeight = double.IsFinite(desired.Height) ? Math.Max(0, desired.Height) : 0;
        if (control is CssLayoutPanel inlinePanel
            && style.Display is CssLayoutDisplay.Inline or CssLayoutDisplay.InlineBlock)
        {
            var inlineWidth = 0d;
            var inlineHeight = 0d;
            var blockWidth = 0d;
            var blockHeight = 0d;
            var hasFlowContent = false;

            void AccumulateGenerated(CssGeneratedPseudoElement? generated)
            {
                if (generated?.IsInFlow != true
                    || (!generated.IsFlowBlock && !generated.IsInlineText))
                {
                    return;
                }

                hasFlowContent = true;
                var margin = generated.ResolveMargin(0, 0);
                var width = generated.IntrinsicSize.Width + margin.Left + margin.Right;
                var height = generated.IntrinsicSize.Height + margin.Top + margin.Bottom;
                if (generated.IsFlowBlock)
                {
                    blockWidth = Math.Max(blockWidth, inlineWidth);
                    blockHeight += inlineHeight;
                    inlineWidth = 0;
                    inlineHeight = 0;
                    blockWidth = Math.Max(blockWidth, width);
                    blockHeight += height;
                }
                else
                {
                    inlineWidth += width;
                    inlineHeight = Math.Max(inlineHeight, height);
                }
            }

            AccumulateGenerated(inlinePanel.BeforePseudoElement);
            foreach (var child in inlinePanel.Children)
            {
                if (!child.IsVisible
                    || child is IDomInfrastructureControl)
                {
                    continue;
                }

                var childStyle = CreateStyle(child, isRoot: false, default);
                if (childStyle.Display == CssLayoutDisplay.None
                    || childStyle.Position is CssLayoutPosition.Absolute or CssLayoutPosition.Fixed)
                {
                    continue;
                }

                hasFlowContent = true;
                var childIntrinsic = ResolveProjectedIntrinsicSize(
                    child,
                    childStyle,
                    child.DesiredSize);
                var margin = childStyle.Margin.Resolve(0, 0);
                var width = childIntrinsic.Width + margin.Horizontal;
                var height = childIntrinsic.Height + margin.Vertical;
                if (child is TextBlock || IsInline(childStyle.Display))
                {
                    inlineWidth += width;
                    inlineHeight = Math.Max(inlineHeight, height);
                }
                else
                {
                    blockWidth = Math.Max(blockWidth, inlineWidth);
                    blockHeight += inlineHeight;
                    inlineWidth = 0;
                    inlineHeight = 0;
                    blockWidth = Math.Max(blockWidth, width);
                    blockHeight += height;
                }
            }
            AccumulateGenerated(inlinePanel.AfterPseudoElement);

            if (hasFlowContent)
            {
                var padding = style.Padding.Resolve(0, 0);
                var border = style.Border.Resolve(0, 0);
                return new WebSceneSize(
                    Math.Max(blockWidth, inlineWidth) + padding.Horizontal + border.Horizontal,
                    blockHeight + inlineHeight + padding.Vertical + border.Vertical);
            }
        }

        if (style.Display is not (CssLayoutDisplay.Flex or CssLayoutDisplay.InlineFlex)
            || style.FlexDirection is not (CssLayoutFlexDirection.Row or CssLayoutFlexDirection.RowReverse))
        {
            return new WebSceneSize(desiredWidth, desiredHeight);
        }

        if (control is not Panel panel)
        {
            return new WebSceneSize(desiredWidth, desiredHeight);
        }

        var flowChildren = panel.Children
            .Select(child => new
            {
                Control = child,
                Style = CreateStyle(child, isRoot: false, default),
                Intrinsic = ResolveProjectedIntrinsicSize(
                    child,
                    CreateStyle(child, isRoot: false, default),
                    child.DesiredSize)
            })
            .Where(child =>
                child.Style.Display != CssLayoutDisplay.None
                && child.Style.Position is not (CssLayoutPosition.Absolute or CssLayoutPosition.Fixed))
            .ToArray();
        if (flowChildren.Length == 0)
        {
            return new WebSceneSize(desiredWidth, desiredHeight);
        }

        var contentWidth = 0d;
        var contentHeight = 0d;
        foreach (var child in flowChildren)
        {
            var margin = child.Style.Margin.Resolve(0, 0);
            var padding = child.Style.Padding.Resolve(0, 0);
            var border = child.Style.Border.Resolve(0, 0);
            var declaredWidth = child.Style.Width.Resolve(0);
            var declaredHeight = child.Style.Height.Resolve(0);
            var outerWidth = declaredWidth.HasValue
                ? child.Style.BoxSizing == CssLayoutBoxSizing.BorderBox
                    ? declaredWidth.Value
                    : declaredWidth.Value + padding.Horizontal + border.Horizontal
                : child.Intrinsic.Width;
            var outerHeight = declaredHeight.HasValue
                ? child.Style.BoxSizing == CssLayoutBoxSizing.BorderBox
                    ? declaredHeight.Value
                    : declaredHeight.Value + padding.Vertical + border.Vertical
                : child.Intrinsic.Height;
            contentWidth += Math.Max(0, outerWidth) + margin.Horizontal;
            contentHeight = Math.Max(contentHeight, Math.Max(0, outerHeight) + margin.Vertical);
        }

        if (flowChildren.Length > 1)
        {
            contentWidth += Math.Max(0, style.ColumnGap.Resolve(0) ?? 0) * (flowChildren.Length - 1);
        }
        var ownPadding = style.Padding.Resolve(0, 0);
        var ownBorder = style.Border.Resolve(0, 0);
        return new WebSceneSize(
            Math.Max(desiredWidth, contentWidth + ownPadding.Horizontal + ownBorder.Horizontal),
            Math.Max(desiredHeight, contentHeight + ownPadding.Vertical + ownBorder.Vertical));
    }

    private static CssLayoutStyle CreateStyle(Control control, bool isRoot, Size viewport)
    {
        var border = control switch
        {
            CssLayoutPanel panel => panel.BorderThickness,
            Border borderControl => borderControl.BorderThickness,
            TemplatedControl templated => templated.BorderThickness,
            _ => default
        };
        return new CssLayoutStyle
        {
            Display = !control.IsVisible
                ? CssLayoutDisplay.None
                : CssLayout.GetDisplay(control) switch
            {
                CssDisplay.Inline => CssLayoutDisplay.Inline,
                CssDisplay.InlineBlock => CssLayoutDisplay.InlineBlock,
                CssDisplay.Flex => CssLayoutDisplay.Flex,
                CssDisplay.Grid => CssLayoutDisplay.Grid,
                CssDisplay.InlineFlex => CssLayoutDisplay.InlineFlex,
                CssDisplay.InlineGrid => CssLayoutDisplay.InlineGrid,
                CssDisplay.Table => CssLayoutDisplay.Table,
                CssDisplay.InlineTable => CssLayoutDisplay.InlineTable,
                CssDisplay.TableRowGroup => CssLayoutDisplay.TableRowGroup,
                CssDisplay.TableHeaderGroup => CssLayoutDisplay.TableHeaderGroup,
                CssDisplay.TableFooterGroup => CssLayoutDisplay.TableFooterGroup,
                CssDisplay.TableRow => CssLayoutDisplay.TableRow,
                CssDisplay.TableCell => CssLayoutDisplay.TableCell,
                CssDisplay.TableColumnGroup => CssLayoutDisplay.TableColumnGroup,
                CssDisplay.TableColumn => CssLayoutDisplay.TableColumn,
                CssDisplay.TableCaption => CssLayoutDisplay.TableCaption,
                CssDisplay.ListItem => CssLayoutDisplay.ListItem,
                _ => CssLayoutDisplay.Block
            },
            FixedTableLayout = CssLayout.GetFixedTableLayout(control),
            Position = CssLayout.GetPosition(control) switch
            {
                CssPosition.Relative => CssLayoutPosition.Relative,
                CssPosition.Absolute => CssLayoutPosition.Absolute,
                CssPosition.Fixed => CssLayoutPosition.Fixed,
                _ => CssLayoutPosition.Static
            },
            OverflowX = ParseOverflow(control is CssLayoutPanel overflowPanelX ? overflowPanelX.OverflowX : null),
            OverflowY = ParseOverflow(control is CssLayoutPanel overflowPanelY ? overflowPanelY.OverflowY : null),
            BoxSizing = CssLayout.GetBoxSizing(control) == CssBoxSizing.BorderBox
                ? CssLayoutBoxSizing.BorderBox
                : CssLayoutBoxSizing.ContentBox,
            FlexDirection = CssLayout.GetFlexDirection(control) switch
            {
                CssFlexDirection.RowReverse => CssLayoutFlexDirection.RowReverse,
                CssFlexDirection.Column => CssLayoutFlexDirection.Column,
                CssFlexDirection.ColumnReverse => CssLayoutFlexDirection.ColumnReverse,
                _ => CssLayoutFlexDirection.Row
            },
            FlexWrap = CssLayout.GetFlexWrap(control) switch
            {
                CssFlexWrap.Wrap => CssLayoutFlexWrap.Wrap,
                CssFlexWrap.WrapReverse => CssLayoutFlexWrap.WrapReverse,
                _ => CssLayoutFlexWrap.NoWrap
            },
            JustifyContent = ParseJustification(CssLayout.GetJustifyContent(control)),
            AlignContent = ParseContentAlignment(CssLayout.GetAlignContent(control)),
            AlignItems = ParseAlignment(CssLayout.GetAlignItems(control), CssLayoutAlignment.Stretch),
            AlignSelf = ParseAlignment(CssLayout.GetAlignSelf(control), CssLayoutAlignment.Auto),
            VerticalAlign = CssLayout.GetVerticalAlign(control) switch
            {
                CssVerticalAlign.Top => CssLayoutVerticalAlign.Top,
                CssVerticalAlign.Middle => CssLayoutVerticalAlign.Middle,
                CssVerticalAlign.Bottom => CssLayoutVerticalAlign.Bottom,
                _ => CssLayoutVerticalAlign.Baseline
            },
            NoWrap = CssLayout.GetNoWrap(control),
            PercentageHeightBasisIsIndefinite = HasIndefinitePercentageHeightBasis(control),
            Width = isRoot ? CssLayoutLength.Pixels(viewport.Width) : Convert(CssLayout.GetWidth(control)),
            Height = isRoot ? CssLayoutLength.Pixels(viewport.Height) : Convert(CssLayout.GetHeight(control)),
            MinWidth = Convert(CssLayout.GetMinWidth(control), defaultToZero: true),
            HasExplicitMinWidth = CssLayout.GetMinWidth(control) is { IsAuto: false },
            MinHeight = Convert(CssLayout.GetMinHeight(control), defaultToZero: true),
            HasExplicitMinHeight = CssLayout.GetMinHeight(control) is { IsAuto: false },
            MaxWidth = Convert(CssLayout.GetMaxWidth(control)),
            MaxHeight = Convert(CssLayout.GetMaxHeight(control)),
            Left = Convert(CssLayout.GetLeft(control)),
            Top = Convert(CssLayout.GetTop(control)),
            Right = Convert(CssLayout.GetRight(control)),
            Bottom = Convert(CssLayout.GetBottom(control)),
            Margin = new CssLayoutEdges(
                Convert(CssLayout.GetMarginTop(control), defaultToZero: true),
                Convert(CssLayout.GetMarginRight(control), defaultToZero: true),
                Convert(CssLayout.GetMarginBottom(control), defaultToZero: true),
                Convert(CssLayout.GetMarginLeft(control), defaultToZero: true)),
            Padding = new CssLayoutEdges(
                Convert(CssLayout.GetPaddingTop(control), defaultToZero: true),
                Convert(CssLayout.GetPaddingRight(control), defaultToZero: true),
                Convert(CssLayout.GetPaddingBottom(control), defaultToZero: true),
                Convert(CssLayout.GetPaddingLeft(control), defaultToZero: true)),
            Border = new CssLayoutEdges(
                CssLayoutLength.Pixels(border.Top),
                CssLayoutLength.Pixels(border.Right),
                CssLayoutLength.Pixels(border.Bottom),
                CssLayoutLength.Pixels(border.Left)),
            RowGap = Convert(CssLayout.GetRowGap(control), defaultToZero: true),
            ColumnGap = Convert(CssLayout.GetColumnGap(control), defaultToZero: true),
            GridTemplateColumns = CssLayout.GetGridTemplateColumns(control) ?? string.Empty,
            GridTemplateRows = CssLayout.GetGridTemplateRows(control) ?? string.Empty,
            GridColumn = CssLayout.GetGridColumn(control) ?? string.Empty,
            FlexGrow = CssLayout.GetFlexGrow(control),
            FlexShrink = CssLayout.GetFlexShrink(control),
            FlexBasis = Convert(CssLayout.GetFlexBasis(control)),
            Order = CssLayout.GetOrder(control)
        };
    }

    private static bool HasIndefinitePercentageHeightBasis(Control control)
    {
        // Keep computed CSS definiteness separate from the finite viewport
        // supplied to the portable root. A directly hosted `height:auto` DOM
        // root remains indefinite for in-flow percentage-height descendants.
        if (CssLayout.GetDocumentViewportRoot(control)
            || CssLayout.GetHeight(control) is { IsAuto: false })
        {
            return false;
        }

        var position = CssLayout.GetPosition(control);
        if (position is CssPosition.Absolute or CssPosition.Fixed
            && CssLayout.GetTop(control) is { IsAuto: false }
            && CssLayout.GetBottom(control) is { IsAuto: false })
        {
            return false;
        }

        if (control.Parent is not CssLayoutPanel parent)
        {
            // Preserve the native embedding contract in the portable lane as
            // well: Grid/Border/presenter-hosted DOM surfaces have a definite
            // used height, while a directly TopLevel-hosted body remains auto.
            return (control.Parent is null or TopLevel)
                   && CssLayout.GetCssHeightIsAuto(control);
        }

        // Flex/grid layout can establish a definite used item size through
        // main-axis distribution or cross-axis stretching even when the item
        // itself has `height:auto`. Ordinary block flow cannot.
        return CssLayout.GetDisplay(parent) is not (
            CssDisplay.Flex or CssDisplay.InlineFlex
            or CssDisplay.Grid or CssDisplay.InlineGrid
            or CssDisplay.Table or CssDisplay.InlineTable
            or CssDisplay.TableRow);
    }

    private static CssLayoutNode SetBreakBefore(CssLayoutNode node, bool canBreakBefore)
    {
        node.CanBreakBefore = canBreakBefore;
        return node;
    }

    private static CssLayoutOverflow ParseOverflow(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "hidden" => CssLayoutOverflow.Hidden,
            "clip" => CssLayoutOverflow.Clip,
            "auto" => CssLayoutOverflow.Auto,
            "scroll" => CssLayoutOverflow.Scroll,
            _ => CssLayoutOverflow.Visible
        };

    private static bool CanBreakBefore(Control? previous, Control current)
    {
        if (previous is not TextBlock previousText || current is not TextBlock currentText)
        {
            return true;
        }

        var left = previousText.Text;
        var right = currentText.Text;
        return string.IsNullOrEmpty(left)
               || string.IsNullOrEmpty(right)
               || char.IsWhiteSpace(left[^1])
               || char.IsWhiteSpace(right[0]);
    }

    private static CssLayoutJustifyContent ParseJustification(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "flex-end" or "end" => CssLayoutJustifyContent.FlexEnd,
            "center" => CssLayoutJustifyContent.Center,
            "space-between" => CssLayoutJustifyContent.SpaceBetween,
            "space-around" => CssLayoutJustifyContent.SpaceAround,
            "space-evenly" => CssLayoutJustifyContent.SpaceEvenly,
            _ => CssLayoutJustifyContent.FlexStart
        };

    private static CssLayoutAlignContent ParseContentAlignment(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "flex-start" or "start" => CssLayoutAlignContent.FlexStart,
            "flex-end" or "end" => CssLayoutAlignContent.FlexEnd,
            "center" => CssLayoutAlignContent.Center,
            "space-between" => CssLayoutAlignContent.SpaceBetween,
            "space-around" => CssLayoutAlignContent.SpaceAround,
            "space-evenly" => CssLayoutAlignContent.SpaceEvenly,
            _ => CssLayoutAlignContent.Stretch
        };

    private static CssLayoutAlignment ParseAlignment(string? value, CssLayoutAlignment defaultValue)
        => value?.Trim().ToLowerInvariant() switch
        {
            "auto" => CssLayoutAlignment.Auto,
            "normal" or "stretch" => CssLayoutAlignment.Stretch,
            "flex-start" or "start" => CssLayoutAlignment.FlexStart,
            "flex-end" or "end" => CssLayoutAlignment.FlexEnd,
            "center" => CssLayoutAlignment.Center,
            "baseline" or "first baseline" or "last baseline" => CssLayoutAlignment.Baseline,
            _ => defaultValue
        };

    private static CssLayoutLength Convert(CssLength? length, bool defaultToZero = false)
    {
        if (!length.HasValue)
        {
            return defaultToZero ? CssLayoutLength.Pixels(0) : CssLayoutLength.Auto;
        }

        return length.Value.Unit switch
        {
            CssLengthUnit.Percent => CssLayoutLength.Percent(length.Value.Value, length.Value.PixelOffset),
            CssLengthUnit.Auto => CssLayoutLength.Auto,
            _ => CssLayoutLength.Pixels(length.Value.Value)
        };
    }

    private sealed class AvaloniaIntrinsicMeasurer(
        IReadOnlyDictionary<long, Control> controls,
        IReadOnlyDictionary<(CssLayoutPanel Host, bool Before), long> pseudoIds)
        : ICssIntrinsicMeasurer
    {
        public WebSceneSize Measure(long nodeId, WebSceneSize availableSize)
        {
            if (!controls.TryGetValue(nodeId, out var control))
            {
                foreach (var pair in pseudoIds)
                {
                    if (pair.Value != nodeId)
                    {
                        continue;
                    }

                    var generated = pair.Key.Before
                        ? pair.Key.Host.BeforePseudoElement
                        : pair.Key.Host.AfterPseudoElement;
                    return generated is null
                        ? WebSceneSize.Empty
                        : new WebSceneSize(
                            generated.IntrinsicSize.Width,
                            generated.IntrinsicSize.Height);
                }
                return new WebSceneSize(0, 0);
            }
            control.Measure(new Size(availableSize.Width, availableSize.Height));
            return new WebSceneSize(control.DesiredSize.Width, control.DesiredSize.Height);
        }
    }
}

internal sealed class AvaloniaCssLayoutSnapshot
{
    private readonly Dictionary<Control, long> _controlIds;
    private readonly IReadOnlyDictionary<(CssLayoutPanel Host, bool Before), long> _pseudoIds;

    internal AvaloniaCssLayoutSnapshot(
        CssLayoutSnapshot portable,
        IReadOnlyDictionary<long, Control> controls,
        IReadOnlyDictionary<(CssLayoutPanel Host, bool Before), long> pseudoIds)
    {
        Portable = portable;
        _pseudoIds = pseudoIds;
        _controlIds = new Dictionary<Control, long>(ReferenceEqualityComparer.Instance);
        foreach (var pair in controls)
        {
            _controlIds.Add(pair.Value, pair.Key);
        }
    }

    internal CssLayoutSnapshot Portable { get; }

    internal CssLayoutBox GetBox(Control control)
    {
        if (_controlIds.TryGetValue(control, out var id))
        {
            return Portable[id];
        }

        throw new KeyNotFoundException("Control was not part of the projected layout tree.");
    }

    internal bool TryGetPseudoBox(CssLayoutPanel host, bool before, out CssLayoutBox box)
    {
        if (_pseudoIds.TryGetValue((host, before), out var id))
        {
            box = Portable[id];
            return true;
        }

        box = default;
        return false;
    }

    internal bool TryGetListMarkerBox(CssLayoutPanel host, out CssLayoutBox box)
    {
        if (_controlIds.TryGetValue(host, out var id)
            && Portable.Boxes.TryGetValue(-id, out box))
        {
            return true;
        }

        box = default;
        return false;
    }
}

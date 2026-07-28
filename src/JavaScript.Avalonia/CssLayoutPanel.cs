using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.TextFormatting;
using Avalonia.Rendering;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace JavaScript.Avalonia;

/// <summary>
/// A deliberately small CSS-like layout panel used by dynamically-created DOM
/// containers. It provides the containing-block and absolute-positioning
/// semantics required by browser-oriented libraries without pretending to be
/// a complete CSS implementation.
/// </summary>
public class CssLayoutPanel : Panel, ICustomHitTest
{
    private static readonly bool s_forcePortableArrangement =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_ENABLE_PORTABLE_LAYOUT"),
            "1",
            StringComparison.Ordinal);

    public static readonly StyledProperty<Thickness> BorderThicknessProperty =
        AvaloniaProperty.Register<CssLayoutPanel, Thickness>(nameof(BorderThickness));

    public static readonly StyledProperty<IBrush?> BorderBrushProperty =
        AvaloniaProperty.Register<CssLayoutPanel, IBrush?>(nameof(BorderBrush));

    public static readonly StyledProperty<CornerRadius> CornerRadiusProperty =
        AvaloniaProperty.Register<CssLayoutPanel, CornerRadius>(nameof(CornerRadius));

    public static readonly StyledProperty<IBrush?> OutlineBrushProperty =
        AvaloniaProperty.Register<CssLayoutPanel, IBrush?>(nameof(OutlineBrush));

    public static readonly StyledProperty<double> OutlineWidthProperty =
        AvaloniaProperty.Register<CssLayoutPanel, double>(nameof(OutlineWidth));

    public static readonly StyledProperty<double> OutlineOffsetProperty =
        AvaloniaProperty.Register<CssLayoutPanel, double>(nameof(OutlineOffset));

    public static readonly StyledProperty<string> OutlineStyleProperty =
        AvaloniaProperty.Register<CssLayoutPanel, string>(nameof(OutlineStyle), "none");

    private CssGeneratedPseudoElement? _beforePseudoElement;
    private CssGeneratedPseudoElement? _afterPseudoElement;
    private string? _beforePseudoElementSignature;
    private string? _afterPseudoElementSignature;
    private IBrush? _regularBackground;
    private DrawingBrush? _generatedBackground;
    private DrawingBrush? _roundedBackground;
    private bool _refreshingPaintBackground;
    private Geometry? _roundedOverflowClip;
    private Size _generatedBackgroundSize;
    private Size _layoutContainingBlockSize;
    private Point? _layoutSlotPosition;
    private Rect? _portableAbsoluteContainingBlock;
    private Rect? _portableFixedContainingBlock;
    private DomLoadingOverlayControl? _loadingOverlay;
    private DomScrollIndicatorControl? _scrollIndicator;
    private DomBorderOverlayControl? _borderOverlay;
    private DomOutlineOverlayControl? _outlineOverlay;
    private DomGeneratedTextControl? _beforePseudoTextControl;
    private DomGeneratedTextControl? _afterPseudoTextControl;
    private DomGeneratedBackgroundControl? _beforePseudoBackgroundControl;
    private DomGeneratedBackgroundControl? _afterPseudoBackgroundControl;
    private bool _isLoadingOverlayVisible;
    private string _loadingOverlayText = "Compiling JavaScript";
    private string _overflowX = "visible";
    private string _overflowY = "visible";
    private bool _overflowRequiresClip;
    private bool _scrollIndicatorVisible = true;
    private Vector _scrollOffset;
    private Size _scrollExtent;
    private Size? _documentScrollViewport;
    private IBrush? _borderTopBrush;
    private IBrush? _borderRightBrush;
    private IBrush? _borderBottomBrush;
    private IBrush? _borderLeftBrush;
    private CssListMarker? _listMarker;
    private DomListMarkerControl? _listMarkerControl;

    public CssLayoutPanel()
    {
        Children.CollectionChanged += (_, change) =>
        {
            if (change.NewItems is null)
            {
                return;
            }

            var isFlexFormattingContext = CssLayout.GetDisplay(this) is CssDisplay.Flex or CssDisplay.InlineFlex;
            foreach (var childPanel in change.NewItems
                         .OfType<Control>()
                         .Where(static child => child is not IDomInfrastructureControl)
                         .OfType<CssLayoutPanel>())
            {
                ApplyCssLayoutRounding(childPanel, isFlexFormattingContext);
            }
        };
        AttachedToVisualTree += (_, _) =>
        {
            RefreshCssLayoutRoundingContext();
            UpdateLoadingOverlay();
            UpdateBorderOverlay();
            UpdateOutlineOverlay();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            DetachLoadingOverlay();
            DetachBorderOverlay();
            DetachOutlineOverlay();
        };
    }

    static CssLayoutPanel()
    {
        AffectsRender<CssLayoutPanel>(
            BorderThicknessProperty,
            BorderBrushProperty,
            CornerRadiusProperty,
            OutlineBrushProperty,
            OutlineWidthProperty,
            OutlineOffsetProperty,
            OutlineStyleProperty);
        AffectsMeasure<CssLayoutPanel>(BorderThicknessProperty);
    }

    public Thickness BorderThickness
    {
        get => GetValue(BorderThicknessProperty);
        set => SetValue(BorderThicknessProperty, value);
    }

    public IBrush? BorderBrush
    {
        get => GetValue(BorderBrushProperty);
        set => SetValue(BorderBrushProperty, value);
    }

    public CornerRadius CornerRadius
    {
        get => GetValue(CornerRadiusProperty);
        set => SetValue(CornerRadiusProperty, value);
    }

    public IBrush? OutlineBrush
    {
        get => GetValue(OutlineBrushProperty);
        set => SetValue(OutlineBrushProperty, value);
    }

    public double OutlineWidth
    {
        get => GetValue(OutlineWidthProperty);
        set => SetValue(OutlineWidthProperty, value);
    }

    public double OutlineOffset
    {
        get => GetValue(OutlineOffsetProperty);
        set => SetValue(OutlineOffsetProperty, value);
    }

    public string OutlineStyle
    {
        get => GetValue(OutlineStyleProperty);
        set => SetValue(OutlineStyleProperty, value);
    }

    internal void SetOutline(IBrush? brush, double width, double offset, string style)
    {
        var normalizedWidth = double.IsFinite(width) ? Math.Max(0, width) : 0;
        var normalizedOffset = double.IsFinite(offset) ? offset : 0;
        var normalizedStyle = string.IsNullOrWhiteSpace(style) ? "none" : style.Trim().ToLowerInvariant();
        if (normalizedStyle is not ("solid" or "auto") || normalizedWidth <= 0 || brush is null)
        {
            OutlineStyle = "none";
            OutlineWidth = normalizedWidth;
            OutlineOffset = normalizedOffset;
            OutlineBrush = brush;
            return;
        }

        // Publish the enabling style last so a live change never creates an
        // intermediate overlay with the previous declaration's geometry.
        OutlineBrush = brush;
        OutlineWidth = normalizedWidth;
        OutlineOffset = normalizedOffset;
        OutlineStyle = normalizedStyle;
    }

    internal CssListMarker? ListMarker => _listMarker;

    internal void SetListMarker(CssListMarker? marker)
    {
        if (Equals(_listMarker, marker)) return;
        _listMarker = marker;
        if (marker is null)
        {
            if (_listMarkerControl is not null)
            {
                Children.Remove(_listMarkerControl);
                _listMarkerControl = null;
            }
        }
        else if (_listMarkerControl is null)
        {
            _listMarkerControl = new DomListMarkerControl
            {
                Marker = marker,
                IsHitTestVisible = false,
                Focusable = false
            };
            CssLayout.SetPosition(_listMarkerControl, CssPosition.Absolute);
            Children.Add(_listMarkerControl);
        }
        else
        {
            _listMarkerControl.Marker = marker;
            _listMarkerControl.InvalidateVisual();
        }
        InvalidateArrange();
    }

    internal bool TryResolveListMarkerRect(Size itemSize, out Rect rect)
    {
        if (_listMarker is not { } marker || marker.Type == CssListStyleType.None)
        {
            rect = default;
            return false;
        }

        var border = BorderThickness;
        var padding = ResolvePadding(this, itemSize.Width, itemSize.Height);
        var contentOrigin = new Point(
            border.Left + padding.Left,
            border.Top + padding.Top);
        var contentHeight = Math.Max(
            0,
            itemSize.Height
            - border.Top
            - padding.Top
            - padding.Bottom
            - border.Bottom);
        var lineHeight = Math.Min(
            contentHeight,
            Math.Max(marker.Size.Height, marker.LineHeight));
        var x = marker.Position == CssListStylePosition.Outside
            ? -9 - marker.Size.Width
            : contentOrigin.X;
        var y = contentOrigin.Y + Math.Max(0, (lineHeight - marker.Size.Height) / 2);
        rect = new Rect(x, y, marker.Size.Width, marker.Size.Height);
        return rect.Width > 0 && rect.Height > 0;
    }

    internal IBrush? BorderTopBrush => _borderTopBrush ?? BorderBrush;
    internal IBrush? BorderRightBrush => _borderRightBrush ?? BorderBrush;
    internal IBrush? BorderBottomBrush => _borderBottomBrush ?? BorderBrush;
    internal IBrush? BorderLeftBrush => _borderLeftBrush ?? BorderBrush;

    internal void SetBorderBrushes(IBrush? top, IBrush? right, IBrush? bottom, IBrush? left)
    {
        _borderTopBrush = top;
        _borderRightBrush = right;
        _borderBottomBrush = bottom;
        _borderLeftBrush = left;
        UpdateBorderOverlay();
        _borderOverlay?.InvalidateVisual();
        InvalidateVisual();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == CssLayout.DisplayProperty
            || change.Property == CssLayout.WidthProperty
            || change.Property == CssLayout.HeightProperty
            || change.Property == CssLayout.LeftProperty
            || change.Property == CssLayout.TopProperty
            || change.Property == CssLayout.RightProperty
            || change.Property == CssLayout.BottomProperty)
        {
            RefreshCssLayoutRoundingContext();
        }
        if (change.Property == BorderThicknessProperty
            || change.Property == BorderBrushProperty
            || change.Property == CornerRadiusProperty)
        {
            UpdateBorderOverlay();
        }
        if (change.Property == OutlineBrushProperty
            || change.Property == OutlineWidthProperty
            || change.Property == OutlineOffsetProperty
            || change.Property == OutlineStyleProperty)
        {
            UpdateOutlineOverlay();
        }
        if (change.Property == CornerRadiusProperty
            || change.Property == ClipToBoundsProperty)
        {
            UpdateRoundedOverflowClip(Bounds.Size);
        }
        if (!_refreshingPaintBackground
            && (change.Property == BackgroundProperty
                || change.Property == CornerRadiusProperty))
        {
            RefreshGeneratedPseudoElements();
        }
    }

    private void UpdateBorderOverlay()
    {
        var thickness = BorderThickness;
        var hasBorder = (thickness.Top > 0 && BorderTopBrush is not null)
                        || (thickness.Right > 0 && BorderRightBrush is not null)
                        || (thickness.Bottom > 0 && BorderBottomBrush is not null)
                        || (thickness.Left > 0 && BorderLeftBrush is not null);
        if (!hasBorder || VisualRoot is null)
        {
            DetachBorderOverlay();
            return;
        }
        if (_borderOverlay is null)
        {
            _borderOverlay = new DomBorderOverlayControl(this)
            {
                IsHitTestVisible = false,
                Focusable = false
            };
            _borderOverlay.SetValue(Canvas.ZIndexProperty, int.MaxValue - 2);
            CssLayout.SetPosition(_borderOverlay, CssPosition.Absolute);
            CssLayout.SetLeft(_borderOverlay, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetRight(_borderOverlay, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetTop(_borderOverlay, new CssLength(0, CssLengthUnit.Pixel));
            CssLayout.SetBottom(_borderOverlay, new CssLength(0, CssLengthUnit.Pixel));
            Children.Add(_borderOverlay);
        }
        _borderOverlay.InvalidateVisual();
    }

    private void DetachBorderOverlay()
    {
        if (_borderOverlay is null) return;
        Children.Remove(_borderOverlay);
        _borderOverlay = null;
    }

    private void UpdateOutlineOverlay()
    {
        var style = OutlineStyle.Trim().ToLowerInvariant();
        var hasOutline = (style is "solid" or "auto")
                         && OutlineWidth > 0
                         && OutlineBrush is not null;
        if (!hasOutline || VisualRoot is null)
        {
            DetachOutlineOverlay();
            return;
        }

        if (_outlineOverlay is null)
        {
            _outlineOverlay = new DomOutlineOverlayControl(this)
            {
                IsHitTestVisible = false,
                Focusable = false
            };
            _outlineOverlay.SetValue(Canvas.ZIndexProperty, int.MaxValue - 1);
            CssLayout.SetPosition(_outlineOverlay, CssPosition.Absolute);
            Children.Add(_outlineOverlay);
        }

        if (Bounds.Width > 0 && Bounds.Height > 0)
        {
            ArrangeOutlineOverlay(Bounds.Size);
        }
        _outlineOverlay.InvalidateVisual();
    }

    private void DetachOutlineOverlay()
    {
        if (_outlineOverlay is null) return;
        Children.Remove(_outlineOverlay);
        _outlineOverlay = null;
    }

    private void ArrangeOutlineOverlay(Size ownerSize)
    {
        if (_outlineOverlay is null) return;
        var extent = Math.Max(0, OutlineOffset + OutlineWidth);
        var rect = new Rect(
            -extent,
            -extent,
            Math.Max(0, ownerSize.Width + extent * 2),
            Math.Max(0, ownerSize.Height + extent * 2));
        _outlineOverlay.Measure(rect.Size);
        _outlineOverlay.Arrange(rect);
    }

    /// <summary>
    /// Draws a non-layout, non-hit-test loading scrim over this DOM surface. The
    /// indicator is an infrastructure child explicitly excluded by the DOM bridge, so
    /// browser geometry, sibling relationships, selectors, and hit testing are unchanged.
    /// </summary>
    public bool IsLoadingOverlayVisible
    {
        get => _isLoadingOverlayVisible;
        set
        {
            if (_isLoadingOverlayVisible == value)
            {
                return;
            }

            _isLoadingOverlayVisible = value;
            UpdateLoadingOverlay();
        }
    }

    public string LoadingOverlayText
    {
        get => _loadingOverlayText;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "Loading" : value.Trim();
            if (string.Equals(_loadingOverlayText, next, StringComparison.Ordinal))
            {
                return;
            }

            _loadingOverlayText = next;
            if (_loadingOverlay is not null)
            {
                _loadingOverlay.Text = next;
            }
        }
    }

    internal int LoadingOverlayFrame => _loadingOverlay?.Frame ?? 0;

    internal bool IsLoadingOverlayAttached
        => _loadingOverlay is not null && Children.Contains(_loadingOverlay);

    internal bool IsLoadingOverlayAnimationRunning => _loadingOverlay?.IsRunning == true;

    internal void AdvanceLoadingOverlayFrameForTest() => _loadingOverlay?.AdvanceFrame();

    internal CssGeneratedPseudoElement? BeforePseudoElement => _beforePseudoElement;

    internal CssGeneratedPseudoElement? AfterPseudoElement => _afterPseudoElement;

    public bool HitTest(Point point)
    {
        if (!CssLayout.GetPointerEventsNone(this)
            && TryResolveListMarkerRect(Bounds.Size, out var markerRect)
            && markerRect.Contains(point))
        {
            // ::marker belongs to the list item's principal box for pointer
            // targeting even though its generated paint is not a DOM child.
            return true;
        }
        return ContainsLocalPoint(this, point, includeOverflow: !ClipToBounds);
    }

    internal Vector ScrollOffset => _scrollOffset;

    internal Size ScrollExtent => _scrollExtent;

    internal Size ScrollViewport => Bounds.Size;

    internal string OverflowX => _overflowX;

    internal string OverflowY => _overflowY;

    internal void SetOverflow(string? overflowX, string? overflowY)
    {
        var nextX = NormalizeOverflow(overflowX);
        var nextY = NormalizeOverflow(overflowY);
        var modesChanged = !string.Equals(_overflowX, nextX, StringComparison.Ordinal)
                           || !string.Equals(_overflowY, nextY, StringComparison.Ordinal);
        _overflowX = nextX;
        _overflowY = nextY;
        _overflowRequiresClip = nextX != "visible" || nextY != "visible";
        RefreshOverflowClipForFixedDescendants();

        if (!modesChanged)
        {
            return;
        }

        UpdateScrollIndicator();
        InvalidateArrange();
    }

    internal void SetScrollIndicatorVisibility(bool visible)
    {
        if (_scrollIndicatorVisible == visible)
        {
            return;
        }

        _scrollIndicatorVisible = visible;
        UpdateScrollIndicator();
    }

    internal void RefreshOverflowClipForFixedDescendants()
    {
        // A viewport-fixed descendant is laid out against the document viewport
        // and escapes intermediate overflow clips. Keep the document viewport
        // itself clipped, but do not let a scrollable menu ancestor hide a
        // fixed child submenu that is intentionally placed beside it.
        var shouldClip = _overflowRequiresClip
                         && (CssLayout.GetDocumentViewportRoot(this)
                             || !ContainsViewportFixedDescendant(this));
        if (ClipToBounds != shouldClip)
        {
            ClipToBounds = shouldClip;
        }
    }

    private static bool ContainsViewportFixedDescendant(CssLayoutPanel panel)
    {
        foreach (var child in panel.Children)
        {
            if (child is IDomInfrastructureControl)
            {
                continue;
            }
            if (CssLayout.GetPosition(child) == CssPosition.Fixed)
            {
                return true;
            }
            if (child is CssLayoutPanel childPanel
                && !CssLayout.GetDocumentViewportRoot(childPanel)
                && ContainsViewportFixedDescendant(childPanel))
            {
                return true;
            }
        }

        return false;
    }

    internal void SetScrollOffset(Vector value)
    {
        ApplyScrollOffset(ClampScrollOffset(value));
    }

    internal void SetDocumentScrollOffset(Vector value, Size viewport)
    {
        _documentScrollViewport = new Size(
            double.IsFinite(viewport.Width) ? Math.Max(0, viewport.Width) : 0,
            double.IsFinite(viewport.Height) ? Math.Max(0, viewport.Height) : 0);
        ApplyScrollOffset(ClampScrollOffset(value));
    }

    private void ApplyScrollOffset(Vector next)
    {
        if (AreClose(_scrollOffset.X, next.X) && AreClose(_scrollOffset.Y, next.Y))
        {
            return;
        }

        _scrollOffset = next;
        InvalidateArrange();
        // Arrange invalidation is local in Avalonia. DOM geometry reads flush
        // from the retained document root, so mark the visual ancestor chain
        // as well or a same-task getBoundingClientRect() can observe the old
        // descendant positions after scrollTop changes.
        for (var current = this.GetVisualParent() as Control; current is not null;
             current = current.GetVisualParent() as Control)
        {
            current.InvalidateArrange();
        }
        _scrollIndicator?.InvalidateVisual();
    }

    internal bool ScrollBy(Vector delta)
    {
        var before = _scrollOffset;
        SetScrollOffset(before + delta);
        return !AreClose(before.X, _scrollOffset.X) || !AreClose(before.Y, _scrollOffset.Y);
    }

    private bool CanScrollHorizontally => _overflowX is "auto" or "scroll";

    private bool CanScrollVertically => _overflowY is "auto" or "scroll";

    private static string NormalizeOverflow(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "auto" => "auto",
            "scroll" => "scroll",
            "hidden" => "hidden",
            "clip" => "clip",
            _ => "visible"
        };

    private void UpdateScrollIndicator()
    {
        if (!CanScrollVertically || !_scrollIndicatorVisible)
        {
            if (_scrollIndicator is not null)
            {
                Children.Remove(_scrollIndicator);
                _scrollIndicator = null;
            }
            if (!CanScrollVertically && _documentScrollViewport is null)
            {
                _scrollOffset = new Vector(_scrollOffset.X, 0);
            }
            return;
        }

        if (_scrollIndicator is not null)
        {
            return;
        }

        var indicator = new DomScrollIndicatorControl(this)
        {
            IsHitTestVisible = false,
            Focusable = false
        };
        indicator.SetValue(Canvas.ZIndexProperty, int.MaxValue - 1);
        CssLayout.SetPosition(indicator, CssPosition.Absolute);
        CssLayout.SetLeft(indicator, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetRight(indicator, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetTop(indicator, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetBottom(indicator, new CssLength(0, CssLengthUnit.Pixel));
        Children.Add(indicator);
        _scrollIndicator = indicator;
    }

    private Vector ClampScrollOffset(Vector value)
    {
        var viewport = _documentScrollViewport ?? Bounds.Size;
        var maximumX = (CanScrollHorizontally || _documentScrollViewport is not null)
            ? Math.Max(0, _scrollExtent.Width - viewport.Width)
            : 0;
        var maximumY = (CanScrollVertically || _documentScrollViewport is not null)
            ? Math.Max(0, _scrollExtent.Height - viewport.Height)
            : 0;
        return new Vector(
            Math.Clamp(double.IsFinite(value.X) ? value.X : 0, 0, maximumX),
            Math.Clamp(double.IsFinite(value.Y) ? value.Y : 0, 0, maximumY));
    }

    private static bool AreClose(double left, double right)
        => Math.Abs(left - right) < 0.001;

    private void UpdateLoadingOverlay()
    {
        if (!_isLoadingOverlayVisible || VisualRoot is null)
        {
            DetachLoadingOverlay();
            return;
        }
        if (_loadingOverlay is not null)
        {
            return;
        }

        var overlay = new DomLoadingOverlayControl
        {
            Text = _loadingOverlayText,
            IsHitTestVisible = false,
            Focusable = false
        };
        overlay.SetValue(Canvas.ZIndexProperty, int.MaxValue);
        CssLayout.SetPosition(overlay, CssPosition.Absolute);
        CssLayout.SetLeft(overlay, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetRight(overlay, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetTop(overlay, new CssLength(0, CssLengthUnit.Pixel));
        CssLayout.SetBottom(overlay, new CssLength(0, CssLengthUnit.Pixel));
        Children.Add(overlay);
        _loadingOverlay = overlay;
        overlay.Start();
        if (Bounds.Width > 0 && Bounds.Height > 0)
        {
            // Loading state can be toggled between compositor frames. Give the
            // infrastructure layer its current viewport immediately so the
            // first capture/paint does not depend on the animation timer's
            // first tick to trigger another layout pass.
            overlay.Measure(Bounds.Size);
            overlay.Arrange(new Rect(Bounds.Size));
        }
        InvalidateVisual();
    }

    private void DetachLoadingOverlay()
    {
        if (_loadingOverlay is null)
        {
            return;
        }

        _loadingOverlay.Stop();
        Children.Remove(_loadingOverlay);
        _loadingOverlay = null;
    }

    private static bool ContainsLocalPoint(Control control, Point point, bool includeOverflow)
    {
        var insideBounds = new Rect(control.Bounds.Size).Contains(point);
        if (insideBounds && !CssLayout.GetPointerEventsNone(control))
        {
            return true;
        }

        if (control is not Panel panel || (!insideBounds && !includeOverflow))
        {
            return false;
        }

        foreach (var child in panel.Children)
        {
            if (!child.IsVisible || !child.IsHitTestVisible)
            {
                continue;
            }

            var childPoint = control.TranslatePoint(point, child) ?? point - child.Bounds.Position;
            if (ContainsLocalPoint(
                    child,
                    childPoint,
                    includeOverflow: child is CssLayoutPanel cssChild && !cssChild.ClipToBounds))
            {
                return true;
            }
        }

        return false;
    }

    internal bool SetGeneratedPseudoElement(string name, IReadOnlyDictionary<string, string>? values)
    {
        var signature = CssGeneratedPseudoElement.CreateSignature(values);
        var generated = CssGeneratedPseudoElement.Create(values);
        if (string.Equals(name, "before", StringComparison.OrdinalIgnoreCase))
        {
            if (string.Equals(_beforePseudoElementSignature, signature, StringComparison.Ordinal))
            {
                return false;
            }
            _beforePseudoElementSignature = signature;
            _beforePseudoElement = generated;
            UpdateGeneratedBackgroundControl(ref _beforePseudoBackgroundControl, generated, before: true);
            UpdateGeneratedTextControl(ref _beforePseudoTextControl, generated, before: true);
        }
        else
        {
            if (string.Equals(_afterPseudoElementSignature, signature, StringComparison.Ordinal))
            {
                return false;
            }
            _afterPseudoElementSignature = signature;
            _afterPseudoElement = generated;
            UpdateGeneratedBackgroundControl(ref _afterPseudoBackgroundControl, generated, before: false);
            UpdateGeneratedTextControl(ref _afterPseudoTextControl, generated, before: false);
        }

        InvalidateMeasure();
        InvalidateArrange();
        CssLayout.InvalidateParent(this);
        InvalidateVisual();
        return true;
    }

    private void UpdateGeneratedBackgroundControl(
        ref DomGeneratedBackgroundControl? control,
        CssGeneratedPseudoElement? generated,
        bool before)
    {
        if (generated is not { Background: not null, IsPaintVisible: true }
            || generated.IsInFlow && !generated.HasText)
        {
            if (control is not null)
            {
                Children.Remove(control);
                control = null;
            }
            return;
        }

        control ??= new DomGeneratedBackgroundControl(before)
        {
            Focusable = false,
            IsHitTestVisible = false,
            // A generated flex item uses the same fractional edge raster as
            // an authored principal box in a flex formatting context.
            UseLayoutRounding = false
        };
        control.Generated = generated;
        control.Background = generated.Background;
        control.SetValue(Canvas.ZIndexProperty, generated.ZIndex ?? -1);
        if (!Children.Contains(control))
        {
            Children.Add(control);
        }
        control.InvalidateVisual();
    }

    private void UpdateGeneratedTextControl(
        ref DomGeneratedTextControl? control,
        CssGeneratedPseudoElement? generated,
        bool before)
    {
        if (generated?.HasText != true)
        {
            if (control is not null)
            {
                Children.Remove(control);
                control = null;
            }
            return;
        }

        control ??= new DomGeneratedTextControl(this, before)
        {
            Focusable = false,
            IsHitTestVisible = false
        };
        control.Generated = generated;
        control.SetValue(Canvas.ZIndexProperty, generated.ZIndex ?? 1);
        if (!Children.Contains(control))
        {
            Children.Add(control);
        }
        control.InvalidateVisual();
    }

    internal bool TryResolveGeneratedPaintRect(
        CssGeneratedPseudoElement generated,
        bool before,
        out Rect rect)
    {
        // ArrangeOverride runs before Avalonia commits Bounds.Size. The current
        // pass's containing-block size is therefore authoritative for generated
        // positioned paint created during that same pass.
        var size = _layoutContainingBlockSize.Width > 0 || _layoutContainingBlockSize.Height > 0
            ? _layoutContainingBlockSize
            : Bounds.Size;
        var border = BorderThickness;
        var padding = ResolvePadding(this, size.Width, size.Height);
        var content = new Rect(
            border.Left + padding.Left,
            border.Top + padding.Top,
            Math.Max(0, size.Width - border.Left - padding.Left - padding.Right - border.Right),
            Math.Max(0, size.Height - border.Top - padding.Top - padding.Bottom - border.Bottom));
        var absoluteContainingBlock = CssLayout.GetPosition(this) != CssPosition.Static
                                      || CssLayout.GetDocumentViewportRoot(this)
            ? new Rect(
                border.Left,
                border.Top,
                Math.Max(0, size.Width - border.Left - border.Right),
                Math.Max(0, size.Height - border.Top - border.Bottom))
            : UsePortableArrangement && _portableAbsoluteContainingBlock is { } portableContainingBlock
                ? portableContainingBlock
                : ResolvePositionedContainingBlock(CssPosition.Absolute, size);
        return generated.TryResolvePaintRect(absoluteContainingBlock, content, before, out rect);
    }

    internal void RefreshGeneratedPseudoElements(Size? requestedSize = null)
    {
        if (!_refreshingPaintBackground
            && !ReferenceEquals(base.Background, _generatedBackground)
            && !ReferenceEquals(base.Background, _roundedBackground))
        {
            _regularBackground = base.Background;
        }

        var size = requestedSize ?? Bounds.Size;
        if (size.Width <= 0 || size.Height <= 0)
        {
            return;
        }

        if (_beforePseudoElement is null && _afterPseudoElement is null)
        {
            _generatedBackground = null;
            var radius = CornerRadius;
            if (_regularBackground is not null
                && (radius.TopLeft > 0
                    || radius.TopRight > 0
                    || radius.BottomRight > 0
                    || radius.BottomLeft > 0))
            {
                var roundedDrawing = new DrawingGroup();
                roundedDrawing.Children.Add(new GeometryDrawing
                {
                    Brush = _regularBackground,
                    Geometry = CreateRoundedRectGeometry(size, radius)
                });
                _roundedBackground = new DrawingBrush(roundedDrawing) { Stretch = Stretch.Fill };
                _refreshingPaintBackground = true;
                base.Background = _roundedBackground;
                _refreshingPaintBackground = false;
            }
            else
            {
                _roundedBackground = null;
                _refreshingPaintBackground = true;
                base.Background = _regularBackground;
                _refreshingPaintBackground = false;
            }
            _generatedBackgroundSize = size;
            return;
        }

        var drawings = new DrawingGroup();
        drawings.Children.Add(new GeometryDrawing
        {
            Brush = _regularBackground ?? Brushes.Transparent,
            Geometry = CreateRoundedRectGeometry(size, CornerRadius)
        });
        var border = BorderThickness;
        var padding = ResolvePadding(this, size.Width, size.Height);
        var content = new Rect(
            border.Left + padding.Left,
            border.Top + padding.Top,
            Math.Max(0, size.Width - border.Left - padding.Left - padding.Right - border.Right),
            Math.Max(0, size.Height - border.Top - padding.Top - padding.Bottom - border.Bottom));
        // An absolutely positioned pseudo-element uses the originating
        // positioned element's padding box as its containing block. Its origin
        // is therefore the inner border edge and percentage sizes/insets use
        // the border-box size with both border axes removed.
        var absoluteContainingBlock = CssLayout.GetPosition(this) != CssPosition.Static
                                      || CssLayout.GetDocumentViewportRoot(this)
            ? new Rect(
                border.Left,
                border.Top,
                Math.Max(0, size.Width - border.Left - border.Right),
                Math.Max(0, size.Height - border.Top - border.Bottom))
            : new Rect(size);
        AddGeneratedDrawing(drawings, _beforePseudoElement, absoluteContainingBlock, content, before: true);
        AddGeneratedDrawing(drawings, _afterPseudoElement, absoluteContainingBlock, content, before: false);
        _generatedBackground = new DrawingBrush(drawings) { Stretch = Stretch.Fill };
        _roundedBackground = null;
        _generatedBackgroundSize = size;
        _refreshingPaintBackground = true;
        base.Background = _generatedBackground;
        _refreshingPaintBackground = false;
    }

    private static void AddGeneratedDrawing(
        DrawingGroup drawings,
        CssGeneratedPseudoElement? generated,
        Rect absoluteContainingBlock,
        Rect content,
        bool before)
    {
        if (generated?.IsPaintVisible != true)
        {
            return;
        }

        if (!generated.TryResolvePaintRect(absoluteContainingBlock, content, before, out var rect))
        {
            return;
        }
        if (generated.Background is not null && !generated.HasText && generated.IsInFlow)
        {
            drawings.Children.Add(new GeometryDrawing
            {
                Brush = generated.Background,
                Geometry = new RectangleGeometry(
                    rect,
                    generated.ResolveCornerRadius(rect.Width),
                    generated.ResolveCornerRadius(rect.Height))
            });
        }
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        RefreshTextNodeWhitespaceBoundaries();
        var border = BorderThickness;
        var padding = ResolvePadding(
            this,
            double.IsFinite(availableSize.Width) ? availableSize.Width : 0,
            double.IsFinite(availableSize.Height) ? availableSize.Height : 0);
        var contentAvailable = new Size(
            SubtractFinite(availableSize.Width, border.Left + padding.Left + padding.Right + border.Right),
            SubtractFinite(availableSize.Height, border.Top + padding.Top + padding.Bottom + border.Bottom));
        var disp = CssLayout.GetDisplay(this);
        if (IsNonRenderedTableTrack(this))
        {
            foreach (var child in Children)
            {
                child.Measure(default);
            }
            return default;
        }
        Size contentDesired;
        if (disp is CssDisplay.Table or CssDisplay.InlineTable)
            contentDesired = MeasureTable(contentAvailable);
        else if (disp is CssDisplay.TableRowGroup or CssDisplay.TableHeaderGroup or CssDisplay.TableFooterGroup)
            contentDesired = MeasureTableRowGroup(contentAvailable);
        else if (disp == CssDisplay.TableRow)
            contentDesired = MeasureTableRow(contentAvailable);
        else if (UsePortableArrangement)
            return AvaloniaCssLayoutProjection.Measure(this, availableSize);
        else if (disp is CssDisplay.Flex or CssDisplay.InlineFlex)
            contentDesired = MeasureFlex(contentAvailable);
        else if (disp is CssDisplay.Grid or CssDisplay.InlineGrid)
            contentDesired = MeasureSimpleGrid(contentAvailable);
        else
            contentDesired = MeasureBlock(contentAvailable);

        var desired = new Size(
            contentDesired.Width + border.Left + padding.Left + padding.Right + border.Right,
            contentDesired.Height + border.Top + padding.Top + padding.Bottom + border.Bottom);
        var preserveIntrinsicTableWidth = disp is CssDisplay.Table or CssDisplay.InlineTable;
        return new Size(
            double.IsFinite(availableSize.Width) && !preserveIntrinsicTableWidth ? Math.Min(desired.Width, availableSize.Width) : desired.Width,
            double.IsFinite(availableSize.Height) ? Math.Min(desired.Height, availableSize.Height) : desired.Height);
    }

    private Size MeasureTable(Size availableSize)
    {
        var rows = EnumerateEffectiveTableRows(this, tableRoot: true).ToArray();
        if (rows.Length == 0)
        {
            return MeasureBlock(availableSize);
        }

        var columns = ResolveIntrinsicTableColumns(rows, availableSize, this);
        SetTableColumns(this, columns);
        var width = columns.Sum();
        var height = rows.Sum(row => MeasureEffectiveTableRow(row, columns, availableSize.Height).Height);
        return new Size(width, height);
    }

    private Size MeasureTableRowGroup(Size availableSize)
    {
        var rows = EnumerateEffectiveTableRows(this, tableRoot: false).ToArray();
        var columns = CssLayout.GetTableColumnWidths(this)
                      ?? ResolveIntrinsicTableColumns(rows, availableSize);
        var width = columns.Sum();
        var height = rows.Sum(row => MeasureEffectiveTableRow(row, columns, availableSize.Height).Height);
        return new Size(width, height);
    }

    private Size MeasureTableRow(Size availableSize)
    {
        var row = CreateEffectiveTableRow(this, Children);
        var columns = CssLayout.GetTableColumnWidths(this)
                      ?? ResolveIntrinsicTableColumns([row], availableSize);
        return MeasureEffectiveTableRow(row, columns, availableSize.Height);
    }

    private static double[] ResolveIntrinsicTableColumns(
        IReadOnlyList<EffectiveTableRow> rows,
        Size availableSize,
        CssLayoutPanel? table = null)
    {
        var tracks = table is null ? [] : EnumerateTableColumnTracks(table).ToArray();
        var count = Math.Max(
            tracks.Length,
            rows.Count == 0 ? 0 : rows.Max(row => row.Cells.Count));
        var columns = new double[count];
        var percentages = new double[count];
        var percentageOffsets = new double[count];
        for (var column = 0; column < tracks.Length; column++)
        {
            var width = CssLayout.GetWidth(tracks[column]);
            if (width is { Unit: CssLengthUnit.Percent } percentage)
            {
                percentages[column] = Math.Max(0, percentage.Value) / 100d;
                percentageOffsets[column] = percentage.PixelOffset;
            }
            else
            {
                columns[column] = Math.Max(
                    columns[column],
                    CssLayout.Resolve(width, FiniteOrZero(availableSize.Width)) ?? 0);
            }
        }
        foreach (var row in rows)
        {
            var column = 0;
            foreach (var effectiveCell in row.Cells)
            {
                if (effectiveCell.Cell is null)
                {
                    var anonymousSize = MeasureAnonymousTableCell(
                        effectiveCell.Children,
                        new Size(double.PositiveInfinity, double.PositiveInfinity));
                    columns[column] = Math.Max(columns[column], anonymousSize.Width);
                    column++;
                    continue;
                }

                var cell = effectiveCell.Cell;
                cell.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
                var padding = ResolvePadding(cell, FiniteOrZero(availableSize.Width), FiniteOrZero(availableSize.Height));
                var border = ResolveBorder(cell);
                var width = CssLayout.GetWidth(cell);
                var percentageWidth = width is { Unit: CssLengthUnit.Percent };
                var horizontalChrome = padding.Left + padding.Right + border.Left + border.Right;
                var declared = percentageWidth
                    ? null
                    : ToOuterSize(
                        cell,
                        CssLayout.Resolve(width, FiniteOrZero(availableSize.Width)),
                        horizontalChrome);
                var minimum = ToOuterSize(
                    cell,
                    CssLayout.Resolve(CssLayout.GetMinWidth(cell), FiniteOrZero(availableSize.Width)),
                    padding.Left + padding.Right + border.Left + border.Right);
                var maximum = ToOuterSize(
                    cell,
                    CssLayout.Resolve(CssLayout.GetMaxWidth(cell), FiniteOrZero(availableSize.Width)),
                    padding.Left + padding.Right + border.Left + border.Right);
                columns[column] = Math.Max(
                    columns[column],
                    Constrain(declared ?? cell.DesiredSize.Width, minimum, maximum));
                if (percentageWidth && width is { } percentage)
                {
                    percentages[column] = Math.Max(
                        percentages[column],
                        Math.Max(0, percentage.Value) / 100d);
                    percentageOffsets[column] = Math.Max(
                        percentageOffsets[column],
                        percentage.PixelOffset
                        + (CssLayout.GetBoxSizing(cell) == CssBoxSizing.ContentBox
                            ? horizontalChrome
                            : 0));
                }
                column++;
            }
        }
        ResolvePercentageTableTracks(columns, percentages, percentageOffsets, availableSize.Width);
        return columns;
    }

    private static Size MeasureEffectiveTableRow(
        EffectiveTableRow row,
        IReadOnlyList<double> columns,
        double availableHeight)
    {
        var rowHeight = row.Row is null
            ? 0
            : CssLayout.Resolve(CssLayout.GetHeight(row.Row), availableHeight) ?? 0;
        var column = 0;
        foreach (var effectiveCell in row.Cells)
        {
            var width = column < columns.Count ? columns[column] : 0;
            if (effectiveCell.Cell is null)
            {
                rowHeight = Math.Max(
                    rowHeight,
                    MeasureAnonymousTableCell(
                        effectiveCell.Children,
                        new Size(width, double.PositiveInfinity)).Height);
                column++;
                continue;
            }

            var cell = effectiveCell.Cell;
            cell.Measure(new Size(width, double.PositiveInfinity));
            var padding = ResolvePadding(cell, width, FiniteOrZero(availableHeight));
            var border = ResolveBorder(cell);
            var verticalChrome = padding.Top + padding.Bottom + border.Top + border.Bottom;
            var declaredHeight = ToOuterSize(
                cell,
                ResolveForMeasure(CssLayout.GetHeight(cell), availableHeight),
                verticalChrome);
            var minimumHeight = ToOuterSize(
                cell,
                ResolveForMeasure(CssLayout.GetMinHeight(cell), availableHeight),
                verticalChrome);
            var maximumHeight = ToOuterSize(
                cell,
                ResolveForMeasure(CssLayout.GetMaxHeight(cell), availableHeight),
                verticalChrome);
            rowHeight = Math.Max(
                rowHeight,
                Constrain(declaredHeight ?? cell.DesiredSize.Height, minimumHeight, maximumHeight));
            column++;
        }
        return new Size(columns.Sum(), rowHeight);
    }

    private static Size MeasureAnonymousTableCell(
        IReadOnlyList<Control> children,
        Size availableSize)
    {
        var flowChildren = children.Where(IsAnonymousTableCellFlowChild).ToArray();
        var desiredWidth = 0d;
        var desiredHeight = 0d;
        var inlineWidth = 0d;
        var inlineHeight = 0d;
        for (var index = 0; index < flowChildren.Length; index++)
        {
            var child = flowChildren[index];
            if (IsCollapsibleWhitespaceText(child))
            {
                if (HasInlineContentOnBothSides(flowChildren, index))
                {
                    inlineWidth += ResolveCollapsedWhitespaceWidth(child);
                }
                continue;
            }
            child.Measure(new Size(availableSize.Width, double.PositiveInfinity));
            var inline = IsInlineLevel(child);
            if (!inline && inlineWidth > 0)
            {
                desiredWidth = Math.Max(desiredWidth, inlineWidth);
                desiredHeight += inlineHeight;
                inlineWidth = 0;
                inlineHeight = 0;
            }
            if (inline)
            {
                inlineWidth += child.DesiredSize.Width;
                inlineHeight = Math.Max(inlineHeight, child.DesiredSize.Height);
            }
            else
            {
                desiredWidth = Math.Max(desiredWidth, child.DesiredSize.Width);
                desiredHeight += child.DesiredSize.Height;
            }
        }
        return new Size(
            Math.Max(desiredWidth, inlineWidth),
            desiredHeight + inlineHeight);
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

    private static IEnumerable<EffectiveTableRow> EnumerateEffectiveTableRows(
        CssLayoutPanel root,
        bool tableRoot)
    {
        var improper = new List<Control>();
        foreach (var child in root.Children.Where(IsTableModelFlowChild))
        {
            var display = CssLayout.GetDisplay(child);
            if (display == CssDisplay.TableRow && child is CssLayoutPanel row)
            {
                foreach (var anonymous in FlushImproper()) yield return anonymous;
                yield return CreateEffectiveTableRow(row, row.Children);
            }
            else if (display is CssDisplay.TableRowGroup
                     or CssDisplay.TableHeaderGroup
                     or CssDisplay.TableFooterGroup
                     && child is CssLayoutPanel group)
            {
                foreach (var anonymous in FlushImproper()) yield return anonymous;
                foreach (var groupRow in EnumerateEffectiveTableRows(group, tableRoot: false))
                {
                    yield return groupRow;
                }
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

        IEnumerable<EffectiveTableRow> FlushImproper()
        {
            if (improper.Count == 0) yield break;
            if (improper.All(IsCollapsibleWhitespaceText))
            {
                improper.Clear();
                yield break;
            }
            yield return CreateEffectiveTableRow(null, improper.ToArray());
            improper.Clear();
        }
    }

    private static EffectiveTableRow CreateEffectiveTableRow(
        CssLayoutPanel? row,
        IEnumerable<Control> children)
    {
        var cells = new List<EffectiveTableCell>();
        var improper = new List<Control>();
        foreach (var child in children.Where(IsTableModelFlowChild))
        {
            if (CssLayout.GetDisplay(child) == CssDisplay.TableCell
                && child is CssLayoutPanel cell)
            {
                FlushImproper();
                cells.Add(new EffectiveTableCell(cell, [cell]));
            }
            else
            {
                improper.Add(child);
            }
        }
        FlushImproper();
        return new EffectiveTableRow(row, cells);

        void FlushImproper()
        {
            if (improper.Count == 0) return;
            if (improper.All(IsCollapsibleWhitespaceText))
            {
                improper.Clear();
                return;
            }
            cells.Add(new EffectiveTableCell(null, improper.ToArray()));
            improper.Clear();
        }
    }

    private static IEnumerable<CssLayoutPanel> EnumerateTableColumnTracks(CssLayoutPanel table)
    {
        foreach (var child in table.Children.OfType<CssLayoutPanel>())
        {
            var display = CssLayout.GetDisplay(child);
            if (display == CssDisplay.TableColumn)
            {
                yield return child;
            }
            else if (display == CssDisplay.TableColumnGroup)
            {
                foreach (var column in child.Children.OfType<CssLayoutPanel>()
                             .Where(column => CssLayout.GetDisplay(column) == CssDisplay.TableColumn))
                {
                    yield return column;
                }
            }
        }
    }

    private static bool HasDirectTableModel(CssLayoutPanel table)
        => EnumerateEffectiveTableRows(table, tableRoot: true).Any();

    private static bool IsTableFlowChild(Control child)
        => child.IsVisible
           && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)
           && CssLayout.GetDisplay(child) is CssDisplay.TableRow
               or CssDisplay.TableRowGroup
               or CssDisplay.TableHeaderGroup
               or CssDisplay.TableFooterGroup;

    private static bool IsProperTableRootChild(CssDisplay display)
        => display is CssDisplay.TableRow
            or CssDisplay.TableRowGroup
            or CssDisplay.TableHeaderGroup
            or CssDisplay.TableFooterGroup
            or CssDisplay.TableColumn
            or CssDisplay.TableColumnGroup
            or CssDisplay.TableCaption;

    private static bool IsAnonymousTableCellFlowChild(Control child)
        => (child.IsVisible || IsCollapsibleWhitespaceText(child))
           && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)
           && !IsNonRenderedTableTrack(child);

    private static bool IsTableModelFlowChild(Control child)
        => (child.IsVisible || IsCollapsibleWhitespaceText(child))
           && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed);

    private static bool IsGridFlowChild(Control child)
        => child.IsVisible
           && !IsCollapsibleWhitespaceText(child)
           && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed);

    private static bool IsCollapsibleWhitespaceText(Control child)
        => child is TextBlock { Text: { } text }
           && string.IsNullOrWhiteSpace(text)
           && child is not DomTextBlockControl { CssAllowsLayout: false };

    private void RefreshTextNodeWhitespaceBoundaries()
    {
        foreach (var text in Children.OfType<DomTextBlockControl>())
        {
            text.RefreshWhitespaceBoundaries(
                StartsAtFormattingLine(text),
                EndsAtFormattingLine(text));
        }
    }

    private static bool StartsAtFormattingLine(Control control)
    {
        for (var current = control; current.GetVisualParent() is Panel parent; current = parent)
        {
            var index = parent.Children.IndexOf(current);
            for (var cursor = index - 1; cursor >= 0; cursor--)
            {
                var sibling = parent.Children[cursor];
                if (IsIgnorableBoundarySibling(sibling)) continue;
                return !IsInlineFormattingParticipant(sibling);
            }

            if (!IsInlineFormattingParticipant(parent)) return true;
        }

        return true;
    }

    private static bool EndsAtFormattingLine(Control control)
    {
        for (var current = control; current.GetVisualParent() is Panel parent; current = parent)
        {
            var index = parent.Children.IndexOf(current);
            for (var cursor = index + 1; cursor < parent.Children.Count; cursor++)
            {
                var sibling = parent.Children[cursor];
                if (IsIgnorableBoundarySibling(sibling)) continue;
                return !IsInlineFormattingParticipant(sibling);
            }

            if (!IsInlineFormattingParticipant(parent)) return true;
        }

        return true;
    }

    private static bool IsIgnorableBoundarySibling(Control control)
        => control is TextBlock { Text: { } text } && string.IsNullOrWhiteSpace(text)
           || !control.IsVisible
           || CssLayout.GetPosition(control) is CssPosition.Absolute or CssPosition.Fixed;

    private static bool IsInlineFormattingParticipant(Control control)
        => control is TextBlock || IsInlineLevel(control);

    private static double ResolveCollapsedWhitespaceWidth(Control child)
        => child is TextBlock text ? Math.Max(3, text.FontSize * 0.25) : 0;

    private static bool HasInlineContentOnBothSides(
        IReadOnlyList<Control> children,
        int whitespaceIndex)
    {
        Control? previous = null;
        for (var index = whitespaceIndex - 1; index >= 0; index--)
        {
            if (IsCollapsibleWhitespaceText(children[index])) continue;
            previous = children[index];
            break;
        }
        Control? next = null;
        for (var index = whitespaceIndex + 1; index < children.Count; index++)
        {
            if (IsCollapsibleWhitespaceText(children[index])) continue;
            next = children[index];
            break;
        }
        return previous is not null && next is not null
               && IsInlineFormattingParticipant(previous)
               && IsInlineFormattingParticipant(next);
    }

    private sealed record EffectiveTableRow(
        CssLayoutPanel? Row,
        IReadOnlyList<EffectiveTableCell> Cells);

    private sealed record EffectiveTableCell(
        CssLayoutPanel? Cell,
        IReadOnlyList<Control> Children);

    private static void SetTableColumns(CssLayoutPanel root, double[] columns)
    {
        CssLayout.SetTableColumnWidths(root, columns);
        foreach (var child in root.Children.OfType<CssLayoutPanel>())
        {
            if (CssLayout.GetDisplay(child) is CssDisplay.TableRow
                or CssDisplay.TableRowGroup
                or CssDisplay.TableHeaderGroup
                or CssDisplay.TableFooterGroup)
            {
                SetTableColumns(child, columns);
            }
        }
    }

    private Size MeasureBlock(Size availableSize)
    {
        var availableWidth = FiniteOrInfinity(availableSize.Width);
        var availableHeight = FiniteOrInfinity(availableSize.Height);
        // A finite measure constraint does not make an auto-height block's
        // containing-block height definite. Percentage-height descendants
        // therefore contribute their intrinsic/auto height during this pass;
        // they resolve against the block's used height later during arrange.
        // Treating the constraint as a percentage basis makes an auto-height
        // flex item consume the entire flex container (for example, a 100%
        // scroll wrapper inside an intrinsically sized tab strip).
        var childPercentageHeightBasis = CssLayout.GetHeight(this) is not { IsAuto: false }
            ? double.PositiveInfinity
            : availableHeight;
        var desiredWidth = Math.Max(
            _beforePseudoElement?.ResolveFlowMinimumWidth(availableWidth) ?? 0,
            _afterPseudoElement?.ResolveFlowMinimumWidth(availableWidth) ?? 0);
        var desiredHeight = _beforePseudoElement?.ResolveFlowOuterHeight(availableHeight) ?? 0;
        var insideMarker = CssLayout.GetDisplay(this) == CssDisplay.ListItem
                           && _listMarker is { Position: CssListStylePosition.Inside } marker
            ? marker
            : null;
        var inlineWidth = insideMarker?.InlineAdvance ?? 0;
        var inlineHeight = insideMarker?.FormattingLineHeight ?? 0;
        double? pendingBlockMargin = null;
        var canCollapseTopMargin = CanCollapseBlockChildMargin(top: true, availableWidth, availableHeight);
        var canCollapseBottomMargin = CanCollapseBlockChildMargin(top: false, availableWidth, availableHeight);
        var ownMargin = ResolveMargin(this, availableWidth, availableHeight);

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

        void AccumulateInlineGenerated(CssGeneratedPseudoElement? generated)
        {
            if (generated?.IsInFlow != true
                || generated.IsFlowBlock
                || !generated.IsInlineText)
            {
                return;
            }

            var size = generated.ResolveFlowSize(availableWidth, availableHeight);
            var margin = generated.ResolveMargin(availableWidth, availableHeight);
            var itemWidth = size.Width + margin.Left + margin.Right;
            var itemHeight = size.Height + margin.Top + margin.Bottom;
            if (!CssLayout.GetNoWrap(this)
                && inlineWidth > 0
                && double.IsFinite(availableWidth)
                && inlineWidth + itemWidth > availableWidth)
            {
                FlushInlineLine();
            }

            inlineWidth += itemWidth;
            inlineHeight = Math.Max(inlineHeight, itemHeight);
        }

        AccumulateInlineGenerated(_beforePseudoElement);
        for (var childIndex = 0; childIndex < Children.Count; childIndex++)
        {
            var child = Children[childIndex];
            if (IsCollapsibleWhitespaceText(child))
            {
                if (HasInlineContentOnBothSides(Children, childIndex))
                {
                    inlineWidth += ResolveCollapsedWhitespaceWidth(child);
                }
                continue;
            }
            if (!child.IsVisible || child is IDomInfrastructureControl)
            {
                continue;
            }

            if (child is DomLineBreakControl)
            {
                child.Measure(default);
                FlushInlineLine();
                continue;
            }

            var position = CssLayout.GetPosition(child);
            // A percentage has no definite value while its containing block is
            // being measured with infinity. Treat it as auto for this pass; an
            // arranged parent will resolve it against a finite final size.
            var width = ResolveForMeasure(CssLayout.GetWidth(child), availableWidth);
            var height = ResolveForMeasure(CssLayout.GetHeight(child), childPercentageHeightBasis);
            var minWidth = ResolveForMeasure(CssLayout.GetMinWidth(child), availableWidth);
            var minHeight = ResolveForMeasure(CssLayout.GetMinHeight(child), childPercentageHeightBasis);
            var maxWidth = ResolveForMeasure(CssLayout.GetMaxWidth(child), availableWidth);
            var maxHeight = ResolveForMeasure(CssLayout.GetMaxHeight(child), childPercentageHeightBasis);
            var margin = ResolveMargin(child, availableWidth, availableHeight);
            var padding = ResolvePadding(child, availableWidth, availableHeight);
            var border = ResolveBorder(child);
            var outerWidth = ToOuterSize(child, width, padding.Left + padding.Right + border.Left + border.Right);
            var outerHeight = ToOuterSize(child, height, padding.Top + padding.Bottom + border.Top + border.Bottom);

            // DOM text nodes generate anonymous inline boxes in a block
            // formatting context. They remain inline even when element boxes
            // are siblings; treating mixed text as a block loses the current
            // line used by following auto-inset positioned descendants.
            var inlineLevel = child is TextBlock || IsInlineLevel(child);
            var positionedAutoWidth = position is CssPosition.Absolute or CssPosition.Fixed
                                      && width is null
                                      && !(ResolveForMeasure(CssLayout.GetLeft(child), availableWidth).HasValue
                                           && ResolveForMeasure(CssLayout.GetRight(child), availableWidth).HasValue);
            var positionedAutoHeight = position is CssPosition.Absolute or CssPosition.Fixed
                                       && height is null
                                       && !(ResolveForMeasure(CssLayout.GetTop(child), availableHeight).HasValue
                                            && ResolveForMeasure(CssLayout.GetBottom(child), availableHeight).HasValue);
            var wrappingTextWidth = child is TextBlock
                                    && !CssLayout.GetNoWrap(this)
                                    && double.IsFinite(availableWidth)
                ? Math.Max(0, availableWidth - inlineWidth - margin.Left - margin.Right)
                : double.PositiveInfinity;
            child.Measure(new Size(
                positionedAutoWidth
                    ? double.PositiveInfinity
                    : Constrain(outerWidth ?? (inlineLevel ? wrappingTextWidth : SubtractFinite(availableWidth, margin.Left + margin.Right)), minWidth, maxWidth),
                positionedAutoHeight
                    ? double.PositiveInfinity
                    : Constrain(outerHeight ?? double.PositiveInfinity, minHeight, maxHeight)));

            if (position is CssPosition.Absolute or CssPosition.Fixed)
            {
                continue;
            }

            var usedOuterWidth = Constrain(
                outerWidth ?? child.DesiredSize.Width,
                ToOuterSize(child, minWidth, padding.Left + padding.Right + border.Left + border.Right),
                ToOuterSize(child, maxWidth, padding.Left + padding.Right + border.Left + border.Right));
            var usedOuterHeight = Constrain(
                outerHeight ?? child.DesiredSize.Height,
                ToOuterSize(child, minHeight, padding.Top + padding.Bottom + border.Top + border.Bottom),
                ToOuterSize(child, maxHeight, padding.Top + padding.Bottom + border.Top + border.Bottom));
            var itemWidth = FiniteOrZero(usedOuterWidth) + margin.Left + margin.Right;
            var itemHeight = ResolveLineBoxHeight(
                                 child,
                                 FiniteOrZero(usedOuterHeight))
                             + margin.Top + margin.Bottom;
            if (inlineLevel)
            {
                if (!CssLayout.GetNoWrap(this) && inlineWidth > 0 && double.IsFinite(availableWidth) && inlineWidth + itemWidth > availableWidth)
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
                    ? CollapseVerticalMargins(previousBottomMargin, margin.Top)
                    : canCollapseTopMargin
                      && desiredHeight == 0
                      && ParentMarginDominatesChildMargin(ownMargin.Top, margin.Top)
                        ? 0
                        : margin.Top;
                desiredHeight += collapsedBefore
                                 + ResolveLineBoxHeight(child, FiniteOrZero(usedOuterHeight));
                pendingBlockMargin = margin.Bottom;
            }
        }

        AccumulateInlineGenerated(_afterPseudoElement);
        FlushInlineLine();
        if (pendingBlockMargin is { } trailingBlockMargin
            && (!canCollapseBottomMargin
                || !ParentMarginDominatesChildMargin(ownMargin.Bottom, trailingBlockMargin)))
        {
            desiredHeight += trailingBlockMargin;
        }
        desiredHeight += _afterPseudoElement?.ResolveFlowOuterHeight(availableHeight) ?? 0;

        desiredWidth = FiniteOrZero(desiredWidth);
        desiredHeight = FiniteOrZero(desiredHeight);
        return new Size(
            double.IsFinite(availableWidth) ? Math.Min(desiredWidth, availableWidth) : desiredWidth,
            double.IsFinite(availableHeight) ? Math.Min(desiredHeight, availableHeight) : desiredHeight);
    }

    private Size MeasureFlex(Size availableSize)
    {
        var row = CssLayout.GetFlexDirection(this) is CssFlexDirection.Row or CssFlexDirection.RowReverse;
        var availableWidth = FiniteOrInfinity(availableSize.Width);
        var availableHeight = FiniteOrInfinity(availableSize.Height);
        var availableMain = row ? availableWidth : availableHeight;
        var wraps = CssLayout.GetFlexWrap(this) != CssFlexWrap.NoWrap && double.IsFinite(availableMain);
        var mainGap = Math.Max(0, CssLayout.Resolve(
            row ? CssLayout.GetColumnGap(this) : CssLayout.GetRowGap(this),
            availableMain) ?? 0);
        var crossGap = Math.Max(0, CssLayout.Resolve(
            row ? CssLayout.GetRowGap(this) : CssLayout.GetColumnGap(this),
            row ? availableHeight : availableWidth) ?? 0);
        var lineMain = 0d;
        var lineCross = 0d;
        var lineCount = 0;
        var measuredMain = 0d;
        var measuredCross = 0d;
        var measuredLineCount = 0;
        var lineFirstBaseline = 0d;
        var lineAfterBaseline = 0d;

        var ownPadding = ResolvePadding(this, availableWidth, availableHeight);
        var ownBorder = ResolveBorder(this);
        var containerChrome = row
            ? ownPadding.Left + ownPadding.Right + ownBorder.Left + ownBorder.Right
            : ownPadding.Top + ownPadding.Bottom + ownBorder.Top + ownBorder.Bottom;
        var containerMinMain = ToContentSize(
            this,
            ResolveForMeasure(row ? CssLayout.GetMinWidth(this) : CssLayout.GetMinHeight(this), availableMain),
            containerChrome);
        var containerMaxMain = ToContentSize(
            this,
            ResolveForMeasure(row ? CssLayout.GetMaxWidth(this) : CssLayout.GetMaxHeight(this), availableMain),
            containerChrome);

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
            lineFirstBaseline = 0;
            lineAfterBaseline = 0;
        }

        void AccumulateItem(
            double itemMain,
            double itemCross,
            Thickness margin,
            double? firstBaseline,
            bool baselineAligned)
        {
            var outerMain = FiniteOrZero(itemMain) + (row ? margin.Left + margin.Right : margin.Top + margin.Bottom);
            var outerCross = FiniteOrZero(itemCross) + (row ? margin.Top + margin.Bottom : margin.Left + margin.Right);
            var candidateMain = lineMain + (lineCount > 0 ? mainGap : 0) + outerMain;
            if (wraps && lineCount > 0 && candidateMain > availableMain)
            {
                FlushLine();
                candidateMain = outerMain;
            }

            lineMain = candidateMain;
            lineCross = Math.Max(lineCross, outerCross);
            if (row && baselineAligned)
            {
                var baseline = firstBaseline is { } value && double.IsFinite(value)
                    ? Math.Max(0, value)
                    : FiniteOrZero(itemCross);
                lineFirstBaseline = Math.Max(lineFirstBaseline, margin.Top + baseline);
                lineAfterBaseline = Math.Max(
                    lineAfterBaseline,
                    margin.Bottom + Math.Max(0, FiniteOrZero(itemCross) - baseline));
                lineCross = Math.Max(lineCross, lineFirstBaseline + lineAfterBaseline);
            }
            lineCount++;
        }

        void MeasureGenerated(CssGeneratedPseudoElement? generated)
        {
            if (generated is null || !generated.IsInFlow)
            {
                return;
            }

            var itemSize = generated.ResolveFlowSize(availableWidth, availableHeight);
            AccumulateItem(
                row ? itemSize.Width : itemSize.Height,
                row ? itemSize.Height : itemSize.Width,
                generated.ResolveMargin(availableWidth, availableHeight),
                generated.FirstBaseline,
                IsBaselineAlignment(ResolveFlexAlignment(generated.AlignSelf)));
        }

        MeasureGenerated(_beforePseudoElement);
        foreach (var child in Children.Where(static child =>
                     child.IsVisible && child is not IDomInfrastructureControl))
        {
            var absolute = CssLayout.GetPosition(child) is CssPosition.Absolute or CssPosition.Fixed;
            var width = ResolveForMeasure(CssLayout.GetWidth(child), availableWidth);
            var height = ResolveForMeasure(CssLayout.GetHeight(child), availableHeight);
            var minW = ResolveForMeasure(CssLayout.GetMinWidth(child), availableWidth);
            var minH = ResolveForMeasure(CssLayout.GetMinHeight(child), availableHeight);
            var maxW = ResolveForMeasure(CssLayout.GetMaxWidth(child), availableWidth);
            var maxH = ResolveForMeasure(CssLayout.GetMaxHeight(child), availableHeight);
            var margin = ResolveMargin(child, availableWidth, availableHeight);
            var padding = ResolvePadding(child, availableWidth, availableHeight);
            var border = ResolveBorder(child);
            var horizontalInsets = padding.Left + padding.Right + border.Left + border.Right;
            var verticalInsets = padding.Top + padding.Bottom + border.Top + border.Bottom;
            var outerWidth = ToOuterSize(child, width, horizontalInsets);
            var outerHeight = ToOuterSize(child, height, verticalInsets);

            var measW = Constrain(
                outerWidth ?? availableWidth,
                ToOuterSize(child, minW, horizontalInsets),
                ToOuterSize(child, maxW, horizontalInsets));
            var measH = Constrain(
                outerHeight ?? availableHeight,
                ToOuterSize(child, minH, verticalInsets),
                ToOuterSize(child, maxH, verticalInsets));
            child.Measure(new Size(measW, measH));
            if (absolute) continue;

            var basis = ResolveForMeasure(CssLayout.GetFlexBasis(child), row ? availableWidth : availableHeight);
            var itemMain = basis ?? (row ? outerWidth ?? child.DesiredSize.Width : outerHeight ?? child.DesiredSize.Height);
            var itemCross = row ? outerHeight ?? child.DesiredSize.Height : outerWidth ?? child.DesiredSize.Width;
            AccumulateItem(
                itemMain,
                itemCross,
                margin,
                ResolveFirstBaseline(child, availableWidth, availableHeight),
                IsBaselineAlignment(ResolveFlexAlignment(CssLayout.GetAlignSelf(child))));
        }
        MeasureGenerated(_afterPseudoElement);

        FlushLine();
        measuredMain = Constrain(measuredMain, containerMinMain, containerMaxMain);

        var desired = row ? new Size(measuredMain, measuredCross) : new Size(measuredCross, measuredMain);
        return new Size(
            double.IsFinite(availableWidth) ? Math.Min(desired.Width, availableWidth) : desired.Width,
            double.IsFinite(availableHeight) ? Math.Min(desired.Height, availableHeight) : desired.Height);
    }

    private Size MeasureSimpleGrid(Size availableSize)
    {
        if (TryParseFixedPixelGridTracks(CssLayout.GetGridTemplateColumns(this), out var fixedTracks))
        {
            _ = TryParseFixedPixelGridTracks(CssLayout.GetGridTemplateRows(this), out var fixedRowTracks);
            return MeasureFixedPixelGrid(availableSize, fixedTracks, fixedRowTracks);
        }

        if (UsesAutoFractionGrid())
        {
            return MeasureAutoFractionGrid(availableSize);
        }

        // Very basic grid for UI chrome (toolbars often 1-row grids or simple auto).
        // Place children "horizontally", height = max child, width sum (or available).
        var availableWidth = FiniteOrInfinity(availableSize.Width);
        var availableHeight = FiniteOrInfinity(availableSize.Height);
        double w = 0, h = 0;
        foreach (var child in Children.Where(c => c.IsVisible))
        {
            var pos = CssLayout.GetPosition(child);
            if (pos is CssPosition.Absolute or CssPosition.Fixed) continue;
            var cw = ResolveForMeasure(CssLayout.GetWidth(child), availableWidth);
            var ch = ResolveForMeasure(CssLayout.GetHeight(child), availableHeight);
            child.Measure(new Size(cw ?? availableWidth, ch ?? availableHeight));
            w += child.DesiredSize.Width;
            h = Math.Max(h, child.DesiredSize.Height);
        }
        return new Size(
            double.IsFinite(availableWidth) ? Math.Min(w, availableWidth) : w,
            double.IsFinite(availableHeight) ? Math.Min(h, availableHeight) : h);
    }

    private Size MeasureFixedPixelGrid(
        Size availableSize,
        IReadOnlyList<double> tracks,
        IReadOnlyList<double> fixedRowTracks)
    {
        var availableWidth = FiniteOrInfinity(availableSize.Width);
        var availableHeight = FiniteOrInfinity(availableSize.Height);
        var columnGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetColumnGap(this), availableWidth) ?? 0);
        var rowGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetRowGap(this), availableHeight) ?? 0);
        var rows = fixedRowTracks.ToList();
        var row = 0;
        var column = 0;
        foreach (var child in Children.Where(IsGridFlowChild))
        {
            var full = tracks.Count == 2 && GridSpansBothColumns(CssLayout.GetGridColumn(child));
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            var margin = ResolveMargin(child, availableWidth, availableHeight);
            var trackWidth = full
                ? tracks.Sum() + columnGap * Math.Max(0, tracks.Count - 1)
                : tracks[column];
            var width = ResolveForMeasure(CssLayout.GetWidth(child), trackWidth);
            var rowTrackHeight = row < fixedRowTracks.Count ? fixedRowTracks[row] : (double?)null;
            var height = ResolveForMeasure(CssLayout.GetHeight(child), rowTrackHeight ?? availableHeight);
            child.Measure(new Size(
                width ?? Math.Max(0, trackWidth - margin.Left - margin.Right),
                height ?? (rowTrackHeight.HasValue
                    ? Math.Max(0, rowTrackHeight.Value - margin.Top - margin.Bottom)
                    : availableHeight)));
            var itemHeight = (height ?? child.DesiredSize.Height) + margin.Top + margin.Bottom;
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
        return new Size(
            double.IsFinite(availableWidth) ? Math.Min(desiredWidth, availableWidth) : desiredWidth,
            double.IsFinite(availableHeight) ? Math.Min(desiredHeight, availableHeight) : desiredHeight);
    }

    private Size MeasureAutoFractionGrid(Size availableSize)
    {
        var availableWidth = FiniteOrInfinity(availableSize.Width);
        var availableHeight = FiniteOrInfinity(availableSize.Height);
        var first = 0d;
        var second = 0d;
        var fullWidth = 0d;
        var rows = new List<double>();
        var row = 0;
        var column = 0;
        foreach (var child in Children.Where(child =>
                     child.IsVisible
                     && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)))
        {
            var width = ResolveForMeasure(CssLayout.GetWidth(child), availableWidth);
            var height = ResolveForMeasure(CssLayout.GetHeight(child), availableHeight);
            child.Measure(new Size(width ?? availableWidth, height ?? availableHeight));
            var margin = ResolveMargin(child, availableWidth, availableHeight);
            var itemWidth = (width ?? child.DesiredSize.Width) + margin.Left + margin.Right;
            var itemHeight = (height ?? child.DesiredSize.Height) + margin.Top + margin.Bottom;
            var full = GridSpansBothColumns(CssLayout.GetGridColumn(child));
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
                if (column == 0) first = Math.Max(first, itemWidth);
                else second = Math.Max(second, itemWidth);
                if (++column == 2)
                {
                    row++;
                    column = 0;
                }
            }
        }
        var columnGap = CssLayout.Resolve(CssLayout.GetColumnGap(this), availableWidth) ?? 0;
        var rowGap = CssLayout.Resolve(CssLayout.GetRowGap(this), availableHeight) ?? 0;
        var desiredWidth = Math.Max(fullWidth, first + Math.Max(0, columnGap) + second);
        var desiredHeight = rows.Sum() + Math.Max(0, rowGap) * Math.Max(0, rows.Count - 1);
        return new Size(
            double.IsFinite(availableWidth) ? Math.Min(desiredWidth, availableWidth) : desiredWidth,
            double.IsFinite(availableHeight) ? Math.Min(desiredHeight, availableHeight) : desiredHeight);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        // Publish the current pass before arranging descendants. TopLevel
        // ClientSize is committed later during native resize, while CSS
        // absolute/fixed descendants must resolve against this pass's initial
        // containing block immediately.
        _layoutContainingBlockSize = finalSize;
        var border = BorderThickness;
        var padding = ResolvePadding(this, finalSize.Width, finalSize.Height);
        var declaredContentWidth = CssLayout.GetBoxSizing(this) == CssBoxSizing.ContentBox
            ? CssLayout.Resolve(CssLayout.GetWidth(this), finalSize.Width)
            : null;
        var declaredContentHeight = CssLayout.GetBoxSizing(this) == CssBoxSizing.ContentBox
            ? CssLayout.Resolve(CssLayout.GetHeight(this), finalSize.Height)
            : null;
        if (Parent is CssLayoutPanel flexParent
            && CssLayout.GetDisplay(flexParent) is CssDisplay.Flex or CssDisplay.InlineFlex)
        {
            // The flex algorithm determines the used outer size on the main
            // axis. A content-box item's content must therefore be derived from
            // that used size after padding, rather than re-resolving its CSS
            // percentage against its own final border box.
            if (CssLayout.GetFlexDirection(flexParent) is CssFlexDirection.Row or CssFlexDirection.RowReverse)
            {
                declaredContentWidth = null;
            }
            else
            {
                declaredContentHeight = null;
            }
        }
        var content = new Rect(
            border.Left + padding.Left,
            border.Top + padding.Top,
            Math.Max(0, declaredContentWidth ?? finalSize.Width - border.Left - padding.Left - padding.Right - border.Right),
            Math.Max(0, declaredContentHeight ?? finalSize.Height - border.Top - padding.Top - padding.Bottom - border.Bottom));

        var disp = CssLayout.GetDisplay(this);
        Dictionary<Control, Point>? nativeStaticPositions = null;
        if (IsNonRenderedTableTrack(this))
        {
            foreach (var child in Children)
            {
                child.Arrange(default);
            }
        }
        else if (disp is CssDisplay.Table or CssDisplay.InlineTable)
        {
            if (HasDirectTableModel(this))
            {
                ArrangeTable(content);
            }
            else
            {
                nativeStaticPositions = new Dictionary<Control, Point>();
                ArrangeBlock(content, nativeStaticPositions);
            }
        }
        else if (disp is CssDisplay.TableRowGroup or CssDisplay.TableHeaderGroup or CssDisplay.TableFooterGroup)
        {
            ArrangeTableRowGroup(content);
        }
        else if (disp == CssDisplay.TableRow)
        {
            ArrangeTableRow(content);
        }
        else if (UsePortableArrangement)
        {
            ArrangePortableFlow(
                finalSize,
                _portableAbsoluteContainingBlock
                ?? ResolvePositionedContainingBlock(CssPosition.Absolute, finalSize),
                _portableFixedContainingBlock
                ?? ResolvePositionedContainingBlock(CssPosition.Fixed, finalSize));
        }
        else if (disp is CssDisplay.Flex or CssDisplay.InlineFlex)
        {
            ArrangeFlex(content);
        }
        else if (disp is CssDisplay.Grid or CssDisplay.InlineGrid)
        {
            ArrangeSimpleGrid(content);
        }
        else
        {
            nativeStaticPositions = new Dictionary<Control, Point>();
            ArrangeBlock(content, nativeStaticPositions);
        }

        if (!UsePortableArrangement)
        {
            foreach (var child in Children.Where(child =>
                         child.IsVisible
                         && child is not IDomInfrastructureControl
                         && CssLayout.GetPosition(child) is CssPosition.Absolute or CssPosition.Fixed))
            {
                ArrangeAbsolute(
                    child,
                    ResolvePositionedContainingBlock(CssLayout.GetPosition(child), finalSize),
                    nativeStaticPositions?.TryGetValue(child, out var staticPosition) == true
                        ? staticPosition
                        : null);
            }
        }

        // Infrastructure paint belongs to this DOM surface itself. It is not a
        // CSS positioned descendant: resolving absolute inset:0 through the
        // ancestor containing-block chain makes a static element's border,
        // loading scrim, or scroll indicator viewport-sized. The portable
        // projection deliberately excludes these non-DOM controls, so arrange
        // them explicitly against the owner's border box in both paths.
        foreach (var infrastructure in Children.Where(static child =>
                     child.IsVisible && child is IDomInfrastructureControl))
        {
            if (ReferenceEquals(infrastructure, _outlineOverlay))
            {
                ArrangeOutlineOverlay(finalSize);
                continue;
            }
            if (ReferenceEquals(infrastructure, _listMarkerControl)
                && TryResolveListMarkerRect(finalSize, out var markerRect))
            {
                infrastructure.Measure(markerRect.Size);
                infrastructure.Arrange(markerRect);
                continue;
            }
            if (infrastructure is DomGeneratedBackgroundControl generatedBackground
                && TryResolveGeneratedPaintRect(
                    generatedBackground.Generated,
                    generatedBackground.Before,
                    out var generatedRect))
            {
                var generatedRadius = generatedBackground.Generated.ResolveCornerRadius(generatedRect.Width);
                generatedBackground.CornerRadius = new CornerRadius(generatedRadius);
                generatedBackground.Measure(generatedRect.Size);
                generatedBackground.Arrange(generatedRect);
                continue;
            }
            infrastructure.Measure(finalSize);
            infrastructure.Arrange(new Rect(finalSize));
        }

        var extentWidth = content.Width;
        var extentHeight = content.Height;
        foreach (var child in Children)
        {
            if (!child.IsVisible || child is IDomInfrastructureControl)
            {
                continue;
            }

            extentWidth = Math.Max(extentWidth, child.Bounds.Right - content.X);
            extentHeight = Math.Max(extentHeight, child.Bounds.Bottom - content.Y);
        }

        var nextExtent = new Size(
            Math.Max(finalSize.Width, extentWidth + border.Left + padding.Left + padding.Right + border.Right),
            Math.Max(finalSize.Height, extentHeight + border.Top + padding.Top + padding.Bottom + border.Bottom));
        if (_scrollExtent != nextExtent)
        {
            _scrollExtent = nextExtent;
            _scrollIndicator?.InvalidateVisual();
        }

        var clampedOffset = ClampScrollOffset(_scrollOffset);
        if (clampedOffset != _scrollOffset)
        {
            _scrollOffset = clampedOffset;
            _scrollIndicator?.InvalidateVisual();
        }

        if (_scrollOffset != default)
        {
            foreach (var child in Children)
            {
                if (!child.IsVisible
                    || child is IDomInfrastructureControl
                    || CssLayout.GetPosition(child) == CssPosition.Fixed)
                {
                    continue;
                }

                child.Arrange(new Rect(
                    child.Bounds.X - _scrollOffset.X,
                    child.Bounds.Y - _scrollOffset.Y,
                    child.Bounds.Width,
                    child.Bounds.Height));
            }
        }

        var radius = CornerRadius;
        if (_generatedBackgroundSize != finalSize
            && (_beforePseudoElement is not null
                || _afterPseudoElement is not null
                || radius.TopLeft > 0
                || radius.TopRight > 0
                || radius.BottomRight > 0
                || radius.BottomLeft > 0))
        {
            RefreshGeneratedPseudoElements(finalSize);
        }
        UpdateRoundedOverflowClip(finalSize);
        return finalSize;
    }

    private void ArrangeTable(Rect content)
    {
        var columns = CssLayout.GetTableColumnWidths(this) ?? [];
        // CSS Tables fixed mode also requires a definite specified table
        // width; `table-layout: fixed; width: auto` remains auto layout.
        if (CssLayout.GetFixedTableLayout(this)
            && CssLayout.GetWidth(this) is { IsAuto: false })
        {
            var rows = EnumerateEffectiveTableRows(this, tableRoot: true).ToArray();
            var tracks = EnumerateTableColumnTracks(this).ToArray();
            var constraints = ResolveFixedTableColumnConstraints(rows, tracks, columns.Length);
            ApplyFixedTableBaseAssignments(columns, constraints, content.Size);
            CssTableWidthDistribution.DistributeFixedExcess(
                columns,
                constraints.Select(static constraint => constraint.Distribution).ToArray(),
                content.Width);
        }
        else
        {
            DistributeSingleColumnTableExcess(columns, content.Width);
        }
        var stretchSingleRow = EnumerateEffectiveTableRows(this, tableRoot: true).Take(2).Count() == 1;
        var width = columns.Sum();
        var y = content.Y;
        var improper = new List<Control>();
        foreach (var child in Children.Where(child =>
                     child.IsVisible
                     && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)))
        {
            var display = CssLayout.GetDisplay(child);
            if (display == CssDisplay.TableRow && child is CssLayoutPanel row)
            {
                FlushImproper();
                var effectiveRow = CreateEffectiveTableRow(row, row.Children);
                var height = MeasureEffectiveTableRow(effectiveRow, columns, content.Height).Height;
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                row.Arrange(new Rect(content.X, y, width, height));
                y += height;
            }
            else if (display is CssDisplay.TableRowGroup
                     or CssDisplay.TableHeaderGroup
                     or CssDisplay.TableFooterGroup
                     && child is CssLayoutPanel group)
            {
                FlushImproper();
                var height = EnumerateEffectiveTableRows(group, tableRoot: false)
                    .Sum(row => MeasureEffectiveTableRow(row, columns, content.Height).Height);
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                group.Arrange(new Rect(content.X, y, width, height));
                y += height;
            }
            else if (IsProperTableRootChild(display))
            {
                FlushImproper();
                if (IsNonRenderedTableTrack(child)) child.Arrange(default);
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
            if (improper.All(IsCollapsibleWhitespaceText))
            {
                improper.Clear();
                return;
            }
            var row = CreateEffectiveTableRow(null, improper);
            var height = MeasureEffectiveTableRow(row, columns, content.Height).Height;
            if (stretchSingleRow) height = Math.Max(height, content.Height);
            ArrangeEffectiveTableRow(row, columns, new Rect(content.X, y, width, height));
            y += height;
            improper.Clear();
        }
    }

    private void ArrangeTableRowGroup(Rect content)
    {
        var columns = CssLayout.GetTableColumnWidths(this) ?? [];
        var stretchSingleRow = EnumerateEffectiveTableRows(this, tableRoot: false).Take(2).Count() == 1;
        var width = columns.Sum();
        var y = content.Y;
        var improper = new List<Control>();
        foreach (var child in Children.Where(IsTableModelFlowChild))
        {
            if (CssLayout.GetDisplay(child) == CssDisplay.TableRow
                && child is CssLayoutPanel row)
            {
                FlushImproper();
                var effectiveRow = CreateEffectiveTableRow(row, row.Children);
                var height = MeasureEffectiveTableRow(effectiveRow, columns, content.Height).Height;
                if (stretchSingleRow) height = Math.Max(height, content.Height);
                row.Arrange(new Rect(content.X, y, width, height));
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
            if (improper.All(IsCollapsibleWhitespaceText))
            {
                improper.Clear();
                return;
            }
            var row = CreateEffectiveTableRow(null, improper);
            var height = MeasureEffectiveTableRow(row, columns, content.Height).Height;
            if (stretchSingleRow) height = Math.Max(height, content.Height);
            ArrangeEffectiveTableRow(row, columns, new Rect(content.X, y, width, height));
            y += height;
            improper.Clear();
        }
    }

    private void ArrangeTableRow(Rect content)
    {
        var columns = CssLayout.GetTableColumnWidths(this) ?? [];
        ArrangeEffectiveTableRow(CreateEffectiveTableRow(this, Children), columns, content);
    }

    private static void ArrangeEffectiveTableRow(
        EffectiveTableRow row,
        IReadOnlyList<double> columns,
        Rect content)
    {
        var x = content.X;
        var column = 0;
        foreach (var effectiveCell in row.Cells)
        {
            var width = column < columns.Count ? columns[column] : 0;
            if (effectiveCell.Cell is { } cell)
            {
                cell.Arrange(new Rect(x, content.Y, width, content.Height));
            }
            else
            {
                ArrangeAnonymousTableCell(
                    effectiveCell.Children,
                    new Rect(x, content.Y, width, content.Height));
            }
            x += width;
            column++;
        }
    }

    private static void ArrangeAnonymousTableCell(
        IReadOnlyList<Control> children,
        Rect content)
    {
        var flowChildren = children.Where(IsAnonymousTableCellFlowChild).ToArray();
        var x = content.X;
        var y = content.Y;
        var lineHeight = 0d;
        for (var index = 0; index < flowChildren.Length; index++)
        {
            var child = flowChildren[index];
            if (IsCollapsibleWhitespaceText(child))
            {
                if (HasInlineContentOnBothSides(flowChildren, index))
                {
                    x += ResolveCollapsedWhitespaceWidth(child);
                }
                continue;
            }

            var inline = IsInlineLevel(child);
            if (!inline && x > content.X)
            {
                y += lineHeight;
                x = content.X;
                lineHeight = 0;
            }
            var width = inline
                ? child.DesiredSize.Width
                : CssLayout.Resolve(CssLayout.GetWidth(child), content.Width) is null
                  && !UsesShrinkToFitInlineWidth(child)
                    ? content.Width
                    : Math.Min(content.Width, child.DesiredSize.Width);
            var height = child.DesiredSize.Height;
            child.Arrange(new Rect(inline ? x : content.X, y, width, height));
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

    private static void DistributeSingleColumnTableExcess(double[] columns, double usedWidth)
    {
        // The multi-column distribution algorithm depends on fixed/percent/auto
        // track classification. For the exact one-column case there is no such
        // ambiguity: the sole grid column receives all positive excess from the
        // table's finalized used content width.
        if (columns.Length == 1
            && double.IsFinite(usedWidth)
            && usedWidth > columns[0])
        {
            columns[0] = usedWidth;
        }
    }

    private static FixedTableColumnConstraint[] ResolveFixedTableColumnConstraints(
        IReadOnlyList<EffectiveTableRow> rows,
        IReadOnlyList<CssLayoutPanel> tracks,
        int columnCount)
    {
        var constraints = new FixedTableColumnConstraint[columnCount];
        for (var column = 0; column < tracks.Count && column < constraints.Length; column++)
        {
            if (CssLayout.GetWidth(tracks[column]) is { IsAuto: false })
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
                && CssLayout.GetWidth(cell) is { IsAuto: false })
            {
                constraints[column] = CreateFixedTableColumnConstraint(cell);
            }
        }
        return constraints;
    }

    private static FixedTableColumnConstraint CreateFixedTableColumnConstraint(Control source)
    {
        var width = CssLayout.GetWidth(source);
        var kind = width switch
        {
            { Unit: CssLengthUnit.Percent, Value: > 0 } => CssTableColumnWidthKind.Percent,
            { Unit: CssLengthUnit.Pixel, Value: > 0 } => CssTableColumnWidthKind.Length,
            { IsAuto: true } or null => CssTableColumnWidthKind.Auto,
            _ => CssTableColumnWidthKind.Zero
        };
        return new FixedTableColumnConstraint(
            source,
            width,
            new CssTableColumnWidthConstraint(kind, Math.Max(0, width?.Value ?? 0)));
    }

    private static void ApplyFixedTableBaseAssignments(
        double[] columns,
        IReadOnlyList<FixedTableColumnConstraint> constraints,
        Size reference)
    {
        for (var column = 0; column < columns.Length; column++)
        {
            var constraint = constraints[column];
            columns[column] = constraint.Distribution.Kind switch
            {
                CssTableColumnWidthKind.Auto or CssTableColumnWidthKind.Zero => 0,
                // In fixed mode a percentage cell's border and padding do not
                // augment its percentage base assignment.
                CssTableColumnWidthKind.Percent
                    => Math.Max(0, CssLayout.Resolve(constraint.Width, reference.Width) ?? 0),
                CssTableColumnWidthKind.Length when constraint.Source is { } source
                                                   && CssLayout.GetDisplay(source) == CssDisplay.TableCell
                    => ResolveFixedCellLengthBase(source, reference),
                CssTableColumnWidthKind.Length
                    => Math.Max(0, CssLayout.Resolve(constraint.Width, reference.Width) ?? 0),
                _ => 0
            };
        }
    }

    private static double ResolveFixedCellLengthBase(Control cell, Size reference)
    {
        var padding = ResolvePadding(cell, reference.Width, reference.Height);
        var border = ResolveBorder(cell);
        return Math.Max(
            0,
            ToOuterSize(
                cell,
                CssLayout.Resolve(CssLayout.GetWidth(cell), reference.Width),
                padding.Left + padding.Right + border.Left + border.Right) ?? 0);
    }

    private readonly record struct FixedTableColumnConstraint(
        Control? Source,
        CssLength? Width,
        CssTableColumnWidthConstraint Distribution);

    private void UpdateRoundedOverflowClip(Size size)
    {
        var radius = CornerRadius;
        if (!ClipToBounds
            || size.Width <= 0
            || size.Height <= 0
            || (radius.TopLeft <= 0
                && radius.TopRight <= 0
                && radius.BottomRight <= 0
                && radius.BottomLeft <= 0))
        {
            if (ReferenceEquals(Clip, _roundedOverflowClip))
            {
                Clip = null;
            }
            _roundedOverflowClip = null;
            return;
        }

        var geometry = CreateRoundedRectGeometry(size, radius);
        _roundedOverflowClip = geometry;
        Clip = geometry;
    }

    private static Geometry CreateRoundedRectGeometry(Size size, CornerRadius radius)
    {
        var topLeft = Math.Max(0, radius.TopLeft);
        var topRight = Math.Max(0, radius.TopRight);
        var bottomRight = Math.Max(0, radius.BottomRight);
        var bottomLeft = Math.Max(0, radius.BottomLeft);
        var scale = Math.Min(1d, Math.Min(
            Ratio(size.Width, topLeft + topRight, bottomLeft + bottomRight),
            Ratio(size.Height, topLeft + bottomLeft, topRight + bottomRight)));
        topLeft *= scale;
        topRight *= scale;
        bottomRight *= scale;
        bottomLeft *= scale;

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(new Point(topLeft, 0), true);
            context.LineTo(new Point(size.Width - topRight, 0));
            AddCorner(context, new Point(size.Width, topRight), topRight);
            context.LineTo(new Point(size.Width, size.Height - bottomRight));
            AddCorner(context, new Point(size.Width - bottomRight, size.Height), bottomRight);
            context.LineTo(new Point(bottomLeft, size.Height));
            AddCorner(context, new Point(0, size.Height - bottomLeft), bottomLeft);
            context.LineTo(new Point(0, topLeft));
            AddCorner(context, new Point(topLeft, 0), topLeft);
            context.EndFigure(true);
        }

        return geometry;

        static double Ratio(double available, double firstSum, double secondSum)
        {
            var maximum = Math.Max(firstSum, secondSum);
            return maximum > 0 ? available / maximum : 1;
        }

        static void AddCorner(StreamGeometryContext context, Point end, double radius)
        {
            if (radius <= 0)
            {
                context.LineTo(end);
                return;
            }
            context.ArcTo(end, new Size(radius, radius), 0, false, SweepDirection.Clockwise);
        }
    }

    private bool UsePortableArrangement
        => s_forcePortableArrangement || !CssLayout.GetNativeLayoutHotPath(this);

    private void RefreshCssLayoutRoundingContext()
    {
        var parentIsFlexFormattingContext = Parent is Control parentControl
                                            && CssLayout.GetDisplay(parentControl) is CssDisplay.Flex or CssDisplay.InlineFlex;
        ApplyCssLayoutRounding(this, parentIsFlexFormattingContext);

        var isFlexFormattingContext = CssLayout.GetDisplay(this) is CssDisplay.Flex or CssDisplay.InlineFlex;
        foreach (var childPanel in Children
                     .Where(static child => child is not IDomInfrastructureControl)
                     .OfType<CssLayoutPanel>())
        {
            ApplyCssLayoutRounding(childPanel, isFlexFormattingContext);
        }
    }

    private static void ApplyCssLayoutRounding(CssLayoutPanel panel, bool parentIsFlexFormattingContext)
    {
        // CSS used geometry retains subpixel precision even in a block formatting
        // context. Keep integral block boxes on Avalonia's normal snapped path, but
        // do not round an explicitly fractional or percentage-sized principal box.
        var useLayoutRounding = !parentIsFlexFormattingContext
                                && !RequiresSubpixelGeometry(panel);
        if (panel.UseLayoutRounding != useLayoutRounding)
        {
            panel.UseLayoutRounding = useLayoutRounding;
        }

        static bool RequiresSubpixelGeometry(Control control)
        {
            // Table cells and their inline-block references must stay on the
            // same snapping policy until the table track allocator publishes
            // a shared subpixel grid. Mixing policies makes equal authored
            // widths rasterize differently.
            if (CssLayout.GetDisplay(control) is not (CssDisplay.Block
                or CssDisplay.ListItem
                or CssDisplay.Grid))
            {
                return false;
            }

            static bool Requires(CssLength? length)
                => length is { IsAuto: false } value
                   && (value.Unit == CssLengthUnit.Pixel
                           && Math.Abs(value.Value - Math.Round(value.Value)) > 0.000001
                       || Math.Abs(value.PixelOffset - Math.Round(value.PixelOffset)) > 0.000001);

            return Requires(CssLayout.GetWidth(control))
                   || Requires(CssLayout.GetHeight(control))
                   || Requires(CssLayout.GetLeft(control))
                   || Requires(CssLayout.GetTop(control))
                   || Requires(CssLayout.GetRight(control))
                   || Requires(CssLayout.GetBottom(control));
        }
    }

    private void ArrangePortableFlow(
        Size finalSize,
        Rect inheritedAbsoluteContainingBlock,
        Rect fixedContainingBlock)
    {
        var flexFormattingContext = CssLayout.GetDisplay(this) is CssDisplay.Flex or CssDisplay.InlineFlex;
        var arrangement = AvaloniaCssLayoutProjection.CaptureDirect(
            this,
            finalSize,
            inheritedAbsoluteContainingBlock,
            fixedContainingBlock);
        var pseudoGeometryChanged = false;
        if (_beforePseudoElement?.IsInFlow == true
            && arrangement.TryGetPseudoBox(this, before: true, out var beforeBox))
        {
            var rect = beforeBox.BorderBox;
            _beforePseudoElement.SetArrangedFlowRect(new Rect(rect.X, rect.Y, rect.Width, rect.Height));
            pseudoGeometryChanged = true;
        }
        if (_afterPseudoElement?.IsInFlow == true
            && arrangement.TryGetPseudoBox(this, before: false, out var afterBox))
        {
            var rect = afterBox.BorderBox;
            _afterPseudoElement.SetArrangedFlowRect(new Rect(rect.X, rect.Y, rect.Width, rect.Height));
            pseudoGeometryChanged = true;
        }
        if (pseudoGeometryChanged)
        {
            _generatedBackgroundSize = default;
        }
        foreach (var child in Children)
        {
            if (!child.IsVisible
                || child is IDomInfrastructureControl)
            {
                continue;
            }

            var box = arrangement.GetBox(child).BorderBox;
            if (child is CssLayoutPanel childPanel)
            {
                var childOrigin = new Vector(box.X, box.Y);
                var isDocumentViewport = CssLayout.GetDocumentViewportRoot(child);
                childPanel._portableAbsoluteContainingBlock = CssLayout.GetPosition(child) != CssPosition.Static
                                                              || isDocumentViewport
                    ? new Rect(0, 0, box.Width, box.Height)
                    : TranslateContainingBlock(inheritedAbsoluteContainingBlock, childOrigin);
                childPanel._portableFixedContainingBlock = isDocumentViewport
                    ? new Rect(0, 0, box.Width, box.Height)
                    : TranslateContainingBlock(fixedContainingBlock, childOrigin);
            }
            var borderBox = new Rect(box.X, box.Y, box.Width, box.Height);
            if (flexFormattingContext)
            {
                ArrangeFlexChildBorderBox(child, borderBox);
            }
            else
            {
                child.Arrange(borderBox);
            }
        }
    }

    private static Rect TranslateContainingBlock(Rect containingBlock, Vector childOrigin)
        => new(
            containingBlock.X - childOrigin.X,
            containingBlock.Y - childOrigin.Y,
            containingBlock.Width,
            containingBlock.Height);

    private static void ArrangeChild(Control child, Rect rect)
    {
        // CSS layout has already resolved the margin and produced this
        // border-box rectangle. Avalonia's ArrangeCore independently deflates
        // an arrange slot by Control.Margin, so pass the inverse slot or an
        // ordinary block margin is applied a second time.
        if (child is CssLayoutPanel panel)
        {
            panel._layoutSlotPosition = rect.Position;
        }
        var margin = child.Margin;
        child.Arrange(new Rect(
            rect.X - margin.Left,
            rect.Y - margin.Top,
            Math.Max(0, rect.Width + margin.Left + margin.Right),
            Math.Max(0, rect.Height + margin.Top + margin.Bottom)));
    }

    private static void ArrangeFlexChildBorderBox(Control child, Rect borderBox)
    {
        // The flex algorithm has already resolved CSS margins and produced the
        // child's border box. Avalonia's ArrangeCore independently deflates an
        // arrange slot by Control.Margin, so pass its inverse slot here to
        // avoid applying the same CSS margin a second time.
        if (child is CssLayoutPanel panel)
        {
            panel._layoutSlotPosition = borderBox.Position;
        }
        var margin = child.Margin;
        child.Arrange(new Rect(
            borderBox.X - margin.Left,
            borderBox.Y - margin.Top,
            Math.Max(0, borderBox.Width + margin.Left + margin.Right),
            Math.Max(0, borderBox.Height + margin.Top + margin.Bottom)));
    }

    private Rect ResolvePositionedContainingBlock(CssPosition childPosition, Size finalSize)
    {
        var offset = new Vector(0, 0);
        Control? current = this;
        while (current is not null)
        {
            if ((childPosition == CssPosition.Absolute
                 && (CssLayout.GetPosition(current) != CssPosition.Static
                     || CssLayout.GetDocumentViewportRoot(current)))
                || (childPosition == CssPosition.Fixed
                    && (current is TopLevel || CssLayout.GetDocumentViewportRoot(current))))
            {
                // ArrangeOverride runs before Avalonia commits this control's new
                // Bounds. Its current Bounds may therefore describe the previous
                // layout pass; finalSize is authoritative for the local containing
                // block being arranged now.
                var size = ReferenceEquals(current, this)
                    ? finalSize
                    : current is CssLayoutPanel panel && panel._layoutContainingBlockSize.Width > 0
                        && panel._layoutContainingBlockSize.Height > 0
                        ? panel._layoutContainingBlockSize
                        : current.Bounds.Size;
                if (size.Width <= 0 || size.Height <= 0)
                {
                    size = current.DesiredSize;
                }
                return new Rect(-offset.X, -offset.Y, size.Width, size.Height);
            }

            if (current.GetVisualParent() is not Control parent)
            {
                break;
            }

            offset += current is CssLayoutPanel panelWithSlot
                      && panelWithSlot._layoutSlotPosition is { } slotPosition
                ? (Vector)slotPosition
                : current.Bounds.Position;
            current = parent;
        }

        if (TopLevel.GetTopLevel(this) is { } topLevel)
        {
            var viewportOffset = new Vector(0, 0);
            for (Control? descendant = this; descendant is not null && !ReferenceEquals(descendant, topLevel); descendant = descendant.GetVisualParent() as Control)
            {
                viewportOffset += descendant is CssLayoutPanel panelWithSlot
                                  && panelWithSlot._layoutSlotPosition is { } slotPosition
                    ? (Vector)slotPosition
                    : descendant.Bounds.Position;
            }
            return new Rect(-viewportOffset.X, -viewportOffset.Y, topLevel.ClientSize.Width, topLevel.ClientSize.Height);
        }

        return new Rect(0, 0, finalSize.Width, finalSize.Height);
    }

    private void ArrangeSimpleGrid(Rect content)
    {
        if (TryParseFixedPixelGridTracks(CssLayout.GetGridTemplateColumns(this), out var fixedTracks))
        {
            _ = TryParseFixedPixelGridTracks(CssLayout.GetGridTemplateRows(this), out var fixedRowTracks);
            ArrangeFixedPixelGrid(content, fixedTracks, fixedRowTracks);
            return;
        }

        if (UsesAutoFractionGrid())
        {
            ArrangeAutoFractionGrid(content);
            return;
        }

        double x = content.X;
        foreach (var child in Children)
        {
            if (!child.IsVisible || CssLayout.GetPosition(child) is CssPosition.Absolute or CssPosition.Fixed) continue;
            var cw = child.DesiredSize.Width;
            var ch = child.DesiredSize.Height;
            child.Arrange(new Rect(x, content.Y, cw, ch));
            x += cw;
        }
        // absolutes handled after
    }

    private void ArrangeFixedPixelGrid(
        Rect content,
        IReadOnlyList<double> tracks,
        IReadOnlyList<double> fixedRowTracks)
    {
        var placements = new List<(Control Child, int Row, int Column, bool Full, Thickness Margin)>();
        var row = 0;
        var column = 0;
        foreach (var child in Children.Where(child =>
                     child.IsVisible
                     && !IsCollapsibleWhitespaceText(child)
                     && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)))
        {
            var full = tracks.Count == 2 && GridSpansBothColumns(CssLayout.GetGridColumn(child));
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            placements.Add((child, row, column, full, ResolveMargin(child, content.Width, content.Height)));
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
                    item.Child.DesiredSize.Height + item.Margin.Top + item.Margin.Bottom);
            }
        }
        var columnGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetColumnGap(this), content.Width) ?? 0);
        var rowGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetRowGap(this), content.Height) ?? 0);
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
            var explicitWidth = CssLayout.Resolve(CssLayout.GetWidth(item.Child), trackWidth);
            var explicitHeight = CssLayout.Resolve(CssLayout.GetHeight(item.Child), content.Height);
            item.Child.Arrange(new Rect(
                content.X + (item.Full ? 0 : columnOffsets[item.Column]) + item.Margin.Left,
                content.Y + rowOffsets[item.Row] + item.Margin.Top,
                explicitWidth ?? Math.Max(0, trackWidth - item.Margin.Left - item.Margin.Right),
                explicitHeight ?? Math.Max(0, rowHeights[item.Row] - item.Margin.Top - item.Margin.Bottom)));
        }
    }

    private void ArrangeAutoFractionGrid(Rect content)
    {
        var placements = new List<(Control Child, int Row, int Column, bool Full, Thickness Margin)>();
        var row = 0;
        var column = 0;
        var firstWidth = 0d;
        foreach (var child in Children.Where(child =>
                     child.IsVisible
                     && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed)))
        {
            var margin = ResolveMargin(child, content.Width, content.Height);
            var full = GridSpansBothColumns(CssLayout.GetGridColumn(child));
            if (full && column != 0)
            {
                row++;
                column = 0;
            }
            placements.Add((child, row, column, full, margin));
            if (!full && column == 0)
            {
                firstWidth = Math.Max(firstWidth, child.DesiredSize.Width + margin.Left + margin.Right);
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
        var columnGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetColumnGap(this), content.Width) ?? 0);
        firstWidth = Math.Min(content.Width, firstWidth);
        var secondWidth = Math.Max(0, content.Width - firstWidth - columnGap);
        var rowHeights = new double[placements.Count == 0 ? 0 : placements.Max(item => item.Row) + 1];
        foreach (var item in placements)
        {
            rowHeights[item.Row] = Math.Max(
                rowHeights[item.Row],
                item.Child.DesiredSize.Height + item.Margin.Top + item.Margin.Bottom);
        }
        var rowGap = Math.Max(0, CssLayout.Resolve(CssLayout.GetRowGap(this), content.Height) ?? 0);
        var offsets = new double[rowHeights.Length];
        for (var index = 1; index < offsets.Length; index++)
        {
            offsets[index] = offsets[index - 1] + rowHeights[index - 1] + rowGap;
        }
        foreach (var item in placements)
        {
            var trackX = item.Full || item.Column == 0
                ? content.X
                : content.X + firstWidth + columnGap;
            var trackWidth = item.Full ? content.Width : item.Column == 0 ? firstWidth : secondWidth;
            item.Child.Arrange(new Rect(
                trackX + item.Margin.Left,
                content.Y + offsets[item.Row] + item.Margin.Top,
                Math.Max(0, trackWidth - item.Margin.Left - item.Margin.Right),
                Math.Max(0, rowHeights[item.Row] - item.Margin.Top - item.Margin.Bottom)));
        }
    }

    private bool UsesAutoFractionGrid()
        => string.Equals(
            string.Join(' ', (CssLayout.GetGridTemplateColumns(this) ?? string.Empty)
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)),
            "auto 1fr",
            StringComparison.OrdinalIgnoreCase);

    private static bool TryParseFixedPixelGridTracks(string? value, out double[] tracks)
    {
        var tokens = (value ?? string.Empty)
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
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
                    out var trackValue)
                || !double.IsFinite(trackValue)
                || trackValue < 0)
            {
                tracks = [];
                return false;
            }
            tracks[index] = trackValue;
        }
        return true;
    }

    private static bool GridSpansBothColumns(string? value)
    {
        var normalized = (value ?? string.Empty).Replace(" ", string.Empty, StringComparison.Ordinal);
        return normalized is "1/3" or "1/-1";
    }

    private void ArrangeBlock(Rect content, Dictionary<Control, Point> staticPositions)
    {
        var effectiveVerticalAlign = CssLayout.GetVerticalAlign(this);
        if (effectiveVerticalAlign == CssVerticalAlign.Baseline
            && Parent is Control parent
            && CssLayout.GetDisplay(parent) == CssDisplay.TableRow)
        {
            effectiveVerticalAlign = CssLayout.GetVerticalAlign(parent);
        }
        var blockFlowHeight = CssLayout.GetDisplay(this) == CssDisplay.TableCell
                              && effectiveVerticalAlign is CssVerticalAlign.Middle or CssVerticalAlign.Bottom
            ? MeasureBlock(content.Size).Height
            : 0;
        var verticalOffset = effectiveVerticalAlign switch
        {
            CssVerticalAlign.Middle => Math.Max(0, (content.Height - blockFlowHeight) / 2),
            CssVerticalAlign.Bottom => Math.Max(0, content.Height - blockFlowHeight),
            _ => 0
        };
        var flowY = content.Y + verticalOffset
                    + (_beforePseudoElement?.ResolveFlowOuterHeight(content.Height) ?? 0);
        var insideMarker = CssLayout.GetDisplay(this) == CssDisplay.ListItem
                           && _listMarker is { Position: CssListStylePosition.Inside } marker
            ? marker
            : null;
        var inlineX = content.X + (insideMarker?.InlineAdvance ?? 0);
        var inlineLineHeight = insideMarker?.FormattingLineHeight ?? 0;
        double? pendingBlockMargin = null;
        var canCollapseTopMargin = CanCollapseBlockChildMargin(top: true, content.Width, content.Height);
        var ownMargin = ResolveMargin(this, content.Width, content.Height);
        var percentageHeightBasis = HasIndefinitePercentageHeightBasis(this)
            ? double.PositiveInfinity
            : content.Height;
        var floatBandY = flowY;
        var floatBandHeight = 0d;
        var leftFloatEdge = content.X;
        var rightFloatEdge = content.Right;
        var hasFloatBand = false;

        void ArrangeInlineGenerated(CssGeneratedPseudoElement? generated)
        {
            if (generated?.IsInFlow != true
                || generated.IsFlowBlock
                || !generated.IsInlineText)
            {
                return;
            }

            var size = generated.ResolveFlowSize(content.Width, content.Height);
            var margin = generated.ResolveMargin(content.Width, content.Height);
            var itemWidth = size.Width + margin.Left + margin.Right;
            if (!CssLayout.GetNoWrap(this)
                && inlineX > content.X
                && inlineX + itemWidth > content.Right)
            {
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
            }

            generated.SetArrangedFlowRect(new Rect(
                inlineX + margin.Left,
                flowY + margin.Top,
                size.Width,
                size.Height));
            inlineX += itemWidth;
            inlineLineHeight = Math.Max(
                inlineLineHeight,
                size.Height + margin.Top + margin.Bottom);
            _generatedBackgroundSize = default;
        }

        _beforePseudoElement?.ClearArrangedFlowRect();
        _afterPseudoElement?.ClearArrangedFlowRect();
        ArrangeInlineGenerated(_beforePseudoElement);
        Control? previousFlowChild = null;
        for (var childIndex = 0; childIndex < Children.Count; childIndex++)
        {
            var child = Children[childIndex];
            if (IsCollapsibleWhitespaceText(child))
            {
                if (HasInlineContentOnBothSides(Children, childIndex))
                {
                    inlineX += ResolveCollapsedWhitespaceWidth(child);
                }
                continue;
            }
            if (!child.IsVisible || child is IDomInfrastructureControl)
            {
                continue;
            }
            if (IsNonRenderedTableTrack(child))
            {
                child.Arrange(default);
                continue;
            }

            if (child is DomLineBreakControl)
            {
                child.Arrange(new Rect(inlineX, flowY, 0, 0));
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
                previousFlowChild = null;
                continue;
            }

            var position = CssLayout.GetPosition(child);
            var positioned = position is CssPosition.Absolute or CssPosition.Fixed;
            var width = CssLayout.Resolve(CssLayout.GetWidth(child), content.Width);
            var height = ResolveForMeasure(CssLayout.GetHeight(child), percentageHeightBasis);
            var minWidth = CssLayout.Resolve(CssLayout.GetMinWidth(child), content.Width);
            var minHeight = ResolveForMeasure(CssLayout.GetMinHeight(child), percentageHeightBasis);
            var maxWidth = CssLayout.Resolve(CssLayout.GetMaxWidth(child), content.Width);
            var maxHeight = ResolveForMeasure(CssLayout.GetMaxHeight(child), percentageHeightBasis);
            var margin = ResolveMargin(child, content.Width, content.Height);
            var padding = ResolvePadding(child, content.Width, content.Height);
            var border = ResolveBorder(child);
            var inlineLevel = child is TextBlock || IsInlineLevel(child);

            // Block-level DOM elements fill their containing block by default.
            var autoWidth = inlineLevel
                ? child.DesiredSize.Width
                : Math.Max(0, content.Width - margin.Left - margin.Right);
            if (width is null && UsesShrinkToFitInlineWidth(child))
            {
                autoWidth = Math.Min(
                    child.DesiredSize.Width,
                    Math.Max(0, content.Width - margin.Left - margin.Right));
            }
            var arrangedWidth = ToOuterSize(child, width, padding.Left + padding.Right + border.Left + border.Right)
                                ?? autoWidth;
            var arrangedHeight = ToOuterSize(child, height, padding.Top + padding.Bottom + border.Top + border.Bottom) ?? child.DesiredSize.Height;
            arrangedWidth = Constrain(arrangedWidth, minWidth, maxWidth);
            arrangedHeight = Constrain(arrangedHeight, minHeight, maxHeight);
            var usedMarginLeft = margin.Left;
            var usedMarginRight = margin.Right;
            if (!inlineLevel)
            {
                var leftIsAuto = CssLayout.GetMarginLeft(child) is { IsAuto: true };
                var rightIsAuto = CssLayout.GetMarginRight(child) is { IsAuto: true };
                if (leftIsAuto || rightIsAuto)
                {
                    var remainingInlineSpace = Math.Max(
                        0,
                        content.Width - arrangedWidth
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
            var lineBoxHeight = ResolveLineBoxHeight(child, arrangedHeight);
            var floating = positioned ? CssFloat.None : CssLayout.GetFloat(child);

            // This bounded float formatting path owns consecutive left/right
            // block floats on an otherwise empty line. Broader float intrusion,
            // clear, shape-outside, and bidi behavior remain outside this slice.
            if (floating != CssFloat.None)
            {
                if (inlineX > content.X)
                {
                    flowY += inlineLineHeight;
                    inlineX = content.X;
                    inlineLineHeight = 0;
                }
                if (!hasFloatBand)
                {
                    floatBandY = flowY;
                    floatBandHeight = 0;
                    leftFloatEdge = content.X;
                    rightFloatEdge = content.Right;
                    hasFloatBand = true;
                }

                var outerWidth = margin.Left + arrangedWidth + margin.Right;
                var outerHeight = margin.Top + lineBoxHeight + margin.Bottom;
                if (rightFloatEdge - leftFloatEdge + 0.01 < outerWidth
                    && floatBandHeight > 0)
                {
                    floatBandY += floatBandHeight;
                    floatBandHeight = 0;
                    leftFloatEdge = content.X;
                    rightFloatEdge = content.Right;
                }

                var floatX = floating == CssFloat.Left
                    ? leftFloatEdge + margin.Left
                    : rightFloatEdge - margin.Right - arrangedWidth;
                var floatY = floatBandY + margin.Top;
                if (position == CssPosition.Relative)
                {
                    floatX += CssLayout.Resolve(CssLayout.GetLeft(child), content.Width) ?? 0;
                    floatY += ResolveRelativeInset(CssLayout.GetTop(child), percentageHeightBasis);
                }
                ArrangeChild(child, new Rect(floatX, floatY, arrangedWidth, arrangedHeight));
                if (floating == CssFloat.Left)
                {
                    leftFloatEdge += outerWidth;
                }
                else
                {
                    rightFloatEdge -= outerWidth;
                }
                floatBandHeight = Math.Max(floatBandHeight, outerHeight);
                previousFlowChild = child;
                continue;
            }

            // End the bounded consecutive-float slice by keeping later normal
            // flow below its occupied band. Text intrusion around floats is a
            // separate compatibility capability.
            if (hasFloatBand)
            {
                flowY = Math.Max(flowY, floatBandY + floatBandHeight);
                inlineX = content.X;
                inlineLineHeight = 0;
                hasFloatBand = false;
            }

            // An out-of-flow inline-level positioned box records the current
            // line's hypothetical insertion point. Its own used width must
            // not force that point onto a new line: absolutely positioned
            // boxes do not participate in line breaking.
            if (!positioned
                && !CssLayout.GetNoWrap(this)
                && inlineLevel
                && inlineX > content.X
                && inlineX + margin.Left + arrangedWidth + margin.Right > content.Right
                && CanBreakBetween(previousFlowChild, child))
            {
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
            }

            if (!inlineLevel && inlineX > content.X)
            {
                flowY += inlineLineHeight;
                inlineX = content.X;
                inlineLineHeight = 0;
            }

            if (inlineLevel && inlineLineHeight == 0 && pendingBlockMargin is { } precedingBlockMargin)
            {
                flowY += precedingBlockMargin;
                pendingBlockMargin = null;
            }

            if (positioned)
            {
                staticPositions[child] = new Point(
                    inlineLevel ? inlineX : content.X,
                    flowY);
                continue;
            }

            var x = inlineLevel ? inlineX + usedMarginLeft : content.X + usedMarginLeft;
            var collapsedBefore = !inlineLevel
                ? pendingBlockMargin is { } previousBottomMargin
                    ? CollapseVerticalMargins(previousBottomMargin, margin.Top)
                    : canCollapseTopMargin
                      && flowY == content.Y + verticalOffset
                      && ParentMarginDominatesChildMargin(ownMargin.Top, margin.Top)
                        ? 0
                        : margin.Top
                : margin.Top;
            var y = flowY + collapsedBefore;

            if (position == CssPosition.Relative)
            {
                x += CssLayout.Resolve(CssLayout.GetLeft(child), content.Width) ?? 0d;
                y += ResolveRelativeInset(CssLayout.GetTop(child), percentageHeightBasis);
            }

            // Overflow clips paint and hit testing at the containing block; it
            // does not resize an overflowing descendant's layout box. Keeping
            // the full used size here is also what gives scrollWidth/Height a
            // stable extent and lets client geometry move with scrollTop.
            var visualWidth = arrangedWidth;
            var visualHeight = arrangedHeight;
            var visualY = child is TextBlock
                ? y + ((lineBoxHeight - visualHeight) / 2d)
                : y;
            ArrangeChild(child, new Rect(x, visualY, visualWidth, visualHeight));
            if (inlineLevel)
            {
                inlineX += usedMarginLeft + arrangedWidth + usedMarginRight;
                inlineLineHeight = Math.Max(
                    inlineLineHeight,
                    Math.Max(
                        insideMarker?.LineHeight ?? 0,
                        margin.Top + lineBoxHeight + margin.Bottom));
            }
            else
            {
                flowY += collapsedBefore + lineBoxHeight;
                pendingBlockMargin = margin.Bottom;
            }
            previousFlowChild = child;
        }
        ArrangeInlineGenerated(_afterPseudoElement);
    }

    private bool CanCollapseBlockChildMargin(bool top, double widthReference, double heightReference)
    {
        if (CssLayout.GetDisplay(this) is not (CssDisplay.Block or CssDisplay.ListItem)
            || _overflowX != "visible"
            || _overflowY != "visible"
            || CssLayout.GetHeight(this) is { IsAuto: false }
            || (CssLayout.Resolve(CssLayout.GetMinHeight(this), heightReference) ?? 0) > 0
            || top && _beforePseudoElement is not null
            || !top && _afterPseudoElement is not null)
        {
            return false;
        }

        var padding = ResolvePadding(this, widthReference, heightReference);
        var border = BorderThickness;
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

    private static bool CanBreakBetween(Control? previous, Control current)
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

    private static double ResolveLineBoxHeight(Control child, double fallback)
        => child is TextBlock && CssLayout.GetLineHeight(child) is <= 0
            ? 0
            : fallback;

    private static bool IsInlineLevel(Control control)
        => CssLayout.GetDisplay(control) is CssDisplay.Inline or CssDisplay.InlineBlock or CssDisplay.InlineFlex or CssDisplay.InlineGrid or CssDisplay.InlineTable;

    private static bool IsNonRenderedTableTrack(Control control)
        => CssLayout.GetDisplay(control) is CssDisplay.TableColumn or CssDisplay.TableColumnGroup;

    private static bool UsesShrinkToFitInlineWidth(Control control)
        => CssLayout.GetDisplay(control) is CssDisplay.InlineBlock or CssDisplay.InlineFlex or CssDisplay.InlineGrid or CssDisplay.InlineTable or CssDisplay.Table;

    private void ArrangeFlex(Rect content)
    {
        var direction = CssLayout.GetFlexDirection(this);
        var row = direction is CssFlexDirection.Row or CssFlexDirection.RowReverse;
        var reverse = direction is CssFlexDirection.RowReverse or CssFlexDirection.ColumnReverse;
        _beforePseudoElement?.ClearArrangedFlowRect();
        _afterPseudoElement?.ClearArrangedFlowRect();
        var indexedItems = Children
            .Select((child, index) => new { Child = child, Index = index })
            .Where(item => item.Child.IsVisible
                           && item.Child is not IDomInfrastructureControl
                           && CssLayout.GetPosition(item.Child) is not (CssPosition.Absolute or CssPosition.Fixed))
            .Select(item => (Item: CreateFlexItem(item.Child, content, row), Order: CssLayout.GetOrder(item.Child), Index: item.Index))
            .ToList();
        if (_beforePseudoElement?.IsInFlow == true)
        {
            indexedItems.Add((CreateFlexItem(_beforePseudoElement, content, row), _beforePseudoElement.Order, -1));
        }
        if (_afterPseudoElement?.IsInFlow == true)
        {
            indexedItems.Add((CreateFlexItem(_afterPseudoElement, content, row), _afterPseudoElement.Order, int.MaxValue));
        }
        var items = indexedItems
            .OrderBy(item => item.Order)
            .ThenBy(item => item.Index)
            .Select(item => item.Item)
            .ToList();

        if (items.Count == 0) return;

        var mainSize = row ? content.Width : content.Height;
        var crossSize = row ? content.Height : content.Width;
        var mainGap = Math.Max(0, CssLayout.Resolve(row ? CssLayout.GetColumnGap(this) : CssLayout.GetRowGap(this), mainSize) ?? 0);
        var crossGap = Math.Max(0, CssLayout.Resolve(row ? CssLayout.GetRowGap(this) : CssLayout.GetColumnGap(this), crossSize) ?? 0);
        var wrap = CssLayout.GetFlexWrap(this);
        var lines = BuildFlexLines(items, mainSize, mainGap, wrap != CssFlexWrap.NoWrap);
        foreach (var line in lines)
        {
            ResolveFlexibleMainSizes(line.Items, mainSize, mainGap);
            ResolveFlexLineBaseline(line, row);
            if (reverse) line.Items.Reverse();
        }

        var naturalCross = lines.Sum(line => line.CrossSize) + crossGap * Math.Max(0, lines.Count - 1);
        var crossFree = Math.Max(0, crossSize - naturalCross);
        var crossOffset = 0d;
        var betweenLines = crossGap;
        if (wrap == CssFlexWrap.NoWrap)
        {
            lines[0].CrossSize = crossSize;
            crossFree = 0;
        }
        else
        {
            var alignContent = CssLayout.GetAlignContent(this)?.Trim().ToLowerInvariant();
            if (alignContent is null or "" or "normal" or "stretch")
            {
                var extra = crossFree / lines.Count;
                foreach (var line in lines) line.CrossSize += extra;
                crossFree = 0;
            }
            else
            {
                (crossOffset, betweenLines) = ResolveContentAlignment(
                    alignContent,
                    crossFree,
                    lines.Count,
                    crossGap);
            }
        }

        var crossCursor = crossOffset;
        var percentageHeightBasis = HasIndefinitePercentageHeightBasis(this)
            ? double.PositiveInfinity
            : content.Height;
        foreach (var line in lines)
        {
            var lineCrossPosition = wrap == CssFlexWrap.WrapReverse
                ? crossSize - crossCursor - line.CrossSize
                : crossCursor;
            var free = mainSize - FlexLineOccupiedMain(line.Items, mainGap);
            var (offset, between) = ResolveJustification(
                CssLayout.GetJustifyContent(this),
                Math.Max(0, free),
                line.Items.Count,
                mainGap);
            var cursor = offset;
            foreach (var item in line.Items)
            {
                cursor += item.MainMarginStart;
                var align = item.AlignSelf;
                if (align is "auto" or "normal" or null) align = CssLayout.GetAlignItems(this);
                if (align is "normal" or null) align = "stretch";

                var itemCross = item.Cross;
                if (!item.HasExplicitCross && align == "stretch")
                {
                    itemCross = Math.Max(0, line.CrossSize - item.CrossMarginStart - item.CrossMarginEnd);
                }
                var itemCrossPosition = align switch
                {
                    "center" => (line.CrossSize - itemCross - item.CrossMarginStart - item.CrossMarginEnd) / 2 + item.CrossMarginStart,
                    "flex-end" or "end" => line.CrossSize - itemCross - item.CrossMarginEnd,
                    "baseline" or "first baseline" or "last baseline" when row =>
                        line.FirstBaseline - ResolveFlexItemBaseline(item),
                    _ => item.CrossMarginStart
                };

                var resolvedCross = lineCrossPosition + itemCrossPosition;
                var x = row ? content.X + cursor : content.X + resolvedCross;
                var y = row ? content.Y + resolvedCross : content.Y + cursor;
                var width = row ? item.Main : itemCross;
                var height = row ? itemCross : item.Main;
                if (item.Child is not null && CssLayout.GetPosition(item.Child) == CssPosition.Relative)
                {
                    x += CssLayout.Resolve(CssLayout.GetLeft(item.Child), content.Width) ?? 0;
                    y += ResolveRelativeInset(CssLayout.GetTop(item.Child), percentageHeightBasis);
                }
                var arranged = new Rect(x, y, width, height);
                if (item.Child is not null)
                {
                    ArrangeFlexChildBorderBox(item.Child, arranged);
                }
                else
                {
                    item.Generated!.SetArrangedFlowRect(arranged);
                    _generatedBackgroundSize = default;
                }
                cursor += item.Main + item.MainMarginEnd + between;
            }

            crossCursor += line.CrossSize + betweenLines;
        }
    }

    private string? ResolveFlexAlignment(string? alignSelf)
    {
        var align = alignSelf?.Trim().ToLowerInvariant();
        if (align is null or "" or "auto" or "normal")
        {
            align = CssLayout.GetAlignItems(this)?.Trim().ToLowerInvariant();
        }
        return align is "normal" ? "stretch" : align;
    }

    private static bool IsBaselineAlignment(string? alignment)
        => alignment is "baseline" or "first baseline" or "last baseline";

    private void ResolveFlexLineBaseline(FlexLine line, bool row)
    {
        if (!row)
        {
            return;
        }

        var baselineItems = line.Items
            .Where(item => IsBaselineAlignment(ResolveFlexAlignment(item.AlignSelf)))
            .ToArray();
        if (baselineItems.Length == 0)
        {
            return;
        }

        line.FirstBaseline = baselineItems.Max(item =>
            item.CrossMarginStart + ResolveFlexItemBaseline(item));
        var afterBaseline = baselineItems.Max(item =>
            item.CrossMarginEnd + Math.Max(0, item.Cross - ResolveFlexItemBaseline(item)));
        line.CrossSize = Math.Max(line.CrossSize, line.FirstBaseline + afterBaseline);
    }

    private static double ResolveFlexItemBaseline(FlexItem item)
        => item.FirstBaseline is { } baseline && double.IsFinite(baseline)
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
            line.CrossSize = Math.Max(line.CrossSize, item.Cross + item.CrossMarginStart + item.CrossMarginEnd);
        }

        if (line.Items.Count > 0) lines.Add(line);
        return lines;
    }

    private static double FlexLineOccupiedMain(IReadOnlyList<FlexItem> items, double gap)
        => items.Sum(item => item.Main + item.MainMarginStart + item.MainMarginEnd)
           + gap * Math.Max(0, items.Count - 1);

    private static void ResolveFlexibleMainSizes(List<FlexItem> items, double mainSize, double gap)
    {
        var free = mainSize - FlexLineOccupiedMain(items, gap);
        var resolvedFlexibleLengths = false;
        if (free > 0)
        {
            var grow = items.Sum(item => item.Grow);
            if (grow > 0)
            {
                foreach (var item in items) item.Main += free * item.Grow / grow;
                free = 0;
                resolvedFlexibleLengths = true;
            }
        }
        else if (free < 0)
        {
            var deficit = -free;
            var shrinkable = items.Where(item => item.Shrink > 0 && item.Main > item.MinimumMain).ToList();
            while (deficit > 0.001 && shrinkable.Count > 0)
            {
                var shrinkWeight = shrinkable.Sum(item => item.Shrink * Math.Max(0, item.Main));
                if (shrinkWeight <= 0) break;
                var applied = 0d;
                foreach (var item in shrinkable)
                {
                    var before = item.Main;
                    item.Main = Math.Max(
                        item.MinimumMain,
                        before - deficit * item.Shrink * Math.Max(0, before) / shrinkWeight);
                    applied += before - item.Main;
                }

                if (applied <= 0.001) break;
                deficit = Math.Max(0, deficit - applied);
                shrinkable.RemoveAll(item => item.Main <= item.MinimumMain + 0.001);
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

        if (free <= 0) return;
        var autoMarginCount = items.Sum(item =>
            (item.MainMarginStartIsAuto ? 1 : 0)
            + (item.MainMarginEndIsAuto ? 1 : 0));
        if (autoMarginCount == 0) return;
        var autoMargin = free / autoMarginCount;
        foreach (var item in items)
        {
            if (item.MainMarginStartIsAuto) item.MainMarginStart = autoMargin;
            if (item.MainMarginEndIsAuto) item.MainMarginEnd = autoMargin;
        }
    }

    private static FlexItem CreateFlexItem(Control child, Rect content, bool row)
    {
        var margin = ResolveMargin(child, content.Width, content.Height);
        var explicitWidth = CssLayout.Resolve(CssLayout.GetWidth(child), content.Width);
        var explicitHeight = CssLayout.Resolve(CssLayout.GetHeight(child), content.Height);
        var padding = ResolvePadding(child, content.Width, content.Height);
        var border = ResolveBorder(child);
        var horizontalInsets = padding.Left + padding.Right + border.Left + border.Right;
        var verticalInsets = padding.Top + padding.Bottom + border.Top + border.Bottom;
        var outerWidth = ToOuterSize(child, explicitWidth, horizontalInsets);
        var outerHeight = ToOuterSize(child, explicitHeight, verticalInsets);
        var basis = CssLayout.Resolve(CssLayout.GetFlexBasis(child), row ? content.Width : content.Height);
        var intrinsicMain = row ? GetUnbreakableInlineWidth(child) : 0;
        var main = basis ?? (row
            ? outerWidth ?? Math.Max(child.DesiredSize.Width, intrinsicMain)
            : outerHeight ?? child.DesiredSize.Height);
        var cross = row ? outerHeight ?? child.DesiredSize.Height : outerWidth ?? child.DesiredSize.Width;
        var minMain = ToOuterSize(
            child,
            CssLayout.Resolve(row ? CssLayout.GetMinWidth(child) : CssLayout.GetMinHeight(child), row ? content.Width : content.Height),
            row ? horizontalInsets : verticalInsets);
        var maxMain = ToOuterSize(
            child,
            CssLayout.Resolve(row ? CssLayout.GetMaxWidth(child) : CssLayout.GetMaxHeight(child), row ? content.Width : content.Height),
            row ? horizontalInsets : verticalInsets);
        var minCross = ToOuterSize(
            child,
            CssLayout.Resolve(row ? CssLayout.GetMinHeight(child) : CssLayout.GetMinWidth(child), row ? content.Height : content.Width),
            row ? verticalInsets : horizontalInsets);
        var maxCross = ToOuterSize(
            child,
            CssLayout.Resolve(row ? CssLayout.GetMaxHeight(child) : CssLayout.GetMaxWidth(child), row ? content.Height : content.Width),
            row ? verticalInsets : horizontalInsets);
        var constrainedMain = Constrain(main, minMain, maxMain);
        var mainLength = row ? CssLayout.GetWidth(child) : CssLayout.GetHeight(child);
        var paddingMain = row
            ? horizontalInsets
            : verticalInsets;
        // Empty authored flex boxes have a zero min-content contribution and
        // must remain shrinkable (the upstream flex-wrap cases rely on this).
        // Preserve the established intrinsic minimum for populated controls;
        // their compact toolbar/legend geometry depends on that protection.
        var isEmptyCssBox = child is CssLayoutPanel panel
                            && panel.Children.All(static candidate =>
                                candidate is IDomInfrastructureControl || !candidate.IsVisible);
        var hasScrollableMainOverflow = child is CssLayoutPanel overflowPanel
                                        && IsScrollableOverflow(row ? overflowPanel.OverflowX : overflowPanel.OverflowY);
        var automaticMinimum = (row ? CssLayout.GetMinWidth(child) : CssLayout.GetMinHeight(child)) is not { IsAuto: false }
            ? hasScrollableMainOverflow
                ? 0
                : mainLength is { Unit: CssLengthUnit.Percent } || isEmptyCssBox
                ? Math.Min(constrainedMain, paddingMain + intrinsicMain)
                : constrainedMain
            : 0;
        return new FlexItem(child)
        {
            Main = constrainedMain,
            MinimumMain = minMain ?? automaticMinimum,
            Cross = Constrain(cross, minCross, maxCross),
            HasExplicitCross = row ? outerHeight.HasValue : outerWidth.HasValue,
            Grow = Math.Max(0, CssLayout.GetFlexGrow(child)),
            Shrink = Math.Max(0, CssLayout.GetFlexShrink(child)),
            MainMarginStart = row ? margin.Left : margin.Top,
            MainMarginEnd = row ? margin.Right : margin.Bottom,
            MainMarginStartIsAuto = (row ? CssLayout.GetMarginLeft(child) : CssLayout.GetMarginTop(child)) is { IsAuto: true },
            MainMarginEndIsAuto = (row ? CssLayout.GetMarginRight(child) : CssLayout.GetMarginBottom(child)) is { IsAuto: true },
            CrossMarginStart = row ? margin.Top : margin.Left,
            CrossMarginEnd = row ? margin.Bottom : margin.Right,
            FirstBaseline = ResolveFirstBaseline(child, content.Width, content.Height)
        };
    }

    private static FlexItem CreateFlexItem(CssGeneratedPseudoElement generated, Rect content, bool row)
    {
        var size = generated.ResolveFlowSize(content.Width, content.Height);
        var margin = generated.ResolveMargin(content.Width, content.Height);
        var main = row ? size.Width : size.Height;
        var cross = row ? size.Height : size.Width;
        return new FlexItem(generated)
        {
            Main = main,
            MinimumMain = 0,
            Cross = cross,
            HasExplicitCross = row ? generated.Height.HasValue : generated.Width.HasValue,
            Grow = generated.FlexGrow,
            Shrink = generated.FlexShrink,
            MainMarginStart = row ? margin.Left : margin.Top,
            MainMarginEnd = row ? margin.Right : margin.Bottom,
            CrossMarginStart = row ? margin.Top : margin.Left,
            CrossMarginEnd = row ? margin.Bottom : margin.Right,
            AlignSelf = generated.AlignSelf,
            FirstBaseline = generated.FirstBaseline
        };
    }

    private static double GetUnbreakableInlineWidth(Control control)
    {
        if (control is not CssLayoutPanel panel)
        {
            return 0;
        }

        if (CssLayout.GetDisplay(panel) is CssDisplay.Flex or CssDisplay.InlineFlex
            && CssLayout.GetFlexDirection(panel) is CssFlexDirection.Row or CssFlexDirection.RowReverse)
        {
            var flowChildren = panel.Children
                .Where(child =>
                    child.IsVisible
                    && CssLayout.GetPosition(child) is not (CssPosition.Absolute or CssPosition.Fixed))
                .ToArray();
            if (flowChildren.Length == 0)
            {
                return 0;
            }

            var contentWidth = 0d;
            foreach (var child in flowChildren)
            {
                var margin = ResolveMargin(child, 0, 0);
                var padding = ResolvePadding(child, 0, 0);
                var border = ResolveBorder(child);
                var explicitWidth = ResolveForMeasure(CssLayout.GetWidth(child), double.PositiveInfinity);
                var outerWidth = ToOuterSize(
                    child,
                    explicitWidth,
                    padding.Left + padding.Right + border.Left + border.Right);
                var intrinsicWidth = Math.Max(child.DesiredSize.Width, GetUnbreakableInlineWidth(child));
                contentWidth += (outerWidth ?? intrinsicWidth) + margin.Left + margin.Right;
            }

            if (flowChildren.Length > 1)
            {
                contentWidth += Math.Max(
                    0,
                    CssLayout.Resolve(CssLayout.GetColumnGap(panel), 0) ?? 0) * (flowChildren.Length - 1);
            }

            var panelPadding = ResolvePadding(panel, 0, 0);
            var panelBorder = ResolveBorder(panel);
            return contentWidth
                   + panelPadding.Left + panelPadding.Right
                   + panelBorder.Left + panelBorder.Right;
        }

        if (panel.Children.Count < 2)
        {
            return 0;
        }

        var textChildren = panel.Children.OfType<TextBlock>().ToArray();
        if (textChildren.Length != panel.Children.Count || textChildren.Length < 2)
        {
            return 0;
        }

        for (var index = 1; index < textChildren.Length; index++)
        {
            if (CanBreakBetween(textChildren[index - 1], textChildren[index]))
            {
                return 0;
            }
        }

        return textChildren.Sum(text => text.DesiredSize.Width);
    }

    private static bool IsScrollableOverflow(string? value)
        => value is "hidden" or "auto" or "scroll";

    internal static double? ResolveFirstBaseline(
        Control control,
        double widthReference,
        double heightReference)
    {
        if (control is TextBlock text)
        {
            var baseline = text.TextLayout.Baseline;
            if (CssLayout.GetLineHeight(text) is { } lineHeight
                && double.IsFinite(lineHeight)
                && lineHeight > text.FontSize)
            {
                // CSS distributes positive line-height leading equally above
                // and below the font's em box. Avalonia's TextLayout baseline
                // reports the glyph-run baseline without that CSS half-leading.
                baseline += (lineHeight - text.FontSize) / 2;
            }
            var adjustment = TextBlock.GetBaselineOffset(text);
            if (double.IsFinite(adjustment))
            {
                baseline += adjustment;
            }
            return double.IsFinite(baseline) ? Math.Max(0, baseline) : null;
        }

        if (control is not Panel panel)
        {
            return null;
        }

        var border = ResolveBorder(control);
        var padding = ResolvePadding(control, widthReference, heightReference);
        foreach (var child in panel.Children)
        {
            if (!child.IsVisible
                || child is IDomInfrastructureControl
                || CssLayout.GetPosition(child) is CssPosition.Absolute or CssPosition.Fixed)
            {
                continue;
            }

            var childBaseline = ResolveFirstBaseline(child, widthReference, heightReference);
            if (!childBaseline.HasValue)
            {
                continue;
            }

            var margin = ResolveMargin(child, widthReference, heightReference);
            return border.Top + padding.Top + margin.Top + childBaseline.Value;
        }

        return null;
    }

    private void ArrangeAbsolute(Control child, Rect content, Point? staticPosition = null)
    {
        var left = CssLayout.Resolve(CssLayout.GetLeft(child), content.Width);
        var right = CssLayout.Resolve(CssLayout.GetRight(child), content.Width);
        var top = CssLayout.Resolve(CssLayout.GetTop(child), content.Height);
        var bottom = CssLayout.Resolve(CssLayout.GetBottom(child), content.Height);
        var width = CssLayout.Resolve(CssLayout.GetWidth(child), content.Width);
        var height = CssLayout.Resolve(CssLayout.GetHeight(child), content.Height);
        var minWidth = CssLayout.Resolve(CssLayout.GetMinWidth(child), content.Width);
        var minHeight = CssLayout.Resolve(CssLayout.GetMinHeight(child), content.Height);
        var maxWidth = CssLayout.Resolve(CssLayout.GetMaxWidth(child), content.Width);
        var maxHeight = CssLayout.Resolve(CssLayout.GetMaxHeight(child), content.Height);
        var margin = ResolveMargin(child, content.Width, content.Height);
        var padding = ResolvePadding(child, content.Width, content.Height);
        var border = ResolveBorder(child);
        var arrangedWidth = ToOuterSize(child, width, padding.Left + padding.Right + border.Left + border.Right)
                            ?? (left.HasValue && right.HasValue
                                ? content.Width - left.Value - right.Value - margin.Left - margin.Right
                                : child.DesiredSize.Width);
        var arrangedHeight = ToOuterSize(child, height, padding.Top + padding.Bottom + border.Top + border.Bottom)
                             ?? (top.HasValue && bottom.HasValue
                                 ? content.Height - top.Value - bottom.Value - margin.Top - margin.Bottom
                                 : child.DesiredSize.Height);
        arrangedWidth = Constrain(arrangedWidth, minWidth, maxWidth);
        arrangedHeight = Constrain(arrangedHeight, minHeight, maxHeight);
        var x = left.HasValue
            ? content.X + left.Value + margin.Left
            : right.HasValue
                ? content.Right - arrangedWidth - right.Value - margin.Right
                : (staticPosition?.X ?? content.X) + margin.Left;
        var y = top.HasValue
            ? content.Y + top.Value + margin.Top
            : bottom.HasValue
                ? content.Bottom - arrangedHeight - bottom.Value - margin.Bottom
                : (staticPosition?.Y ?? content.Y) + margin.Top;
        child.Arrange(new Rect(x, y, arrangedWidth, arrangedHeight));
    }

    private static (double Offset, double Between) ResolveJustification(string? value, double free, int count, double gap)
        => value switch
        {
            "center" => (free / 2, gap),
            "flex-end" or "end" => (free, gap),
            "space-between" when count > 1 => (0, gap + free / (count - 1)),
            "space-around" => (free / count / 2, gap + free / count),
            "space-evenly" => (free / (count + 1), gap + free / (count + 1)),
            _ => (0, gap)
        };

    private static (double Offset, double Between) ResolveContentAlignment(
        string value,
        double free,
        int count,
        double gap)
        => value switch
        {
            "center" => (free / 2, gap),
            "flex-end" or "end" => (free, gap),
            "space-between" when count > 1 => (0, gap + free / (count - 1)),
            "space-around" when count > 0 => (free / count / 2, gap + free / count),
            "space-evenly" when count > 0 => (free / (count + 1), gap + free / (count + 1)),
            _ => (0, gap)
        };

    private sealed class FlexItem
    {
        public FlexItem(Control child)
        {
            Child = child;
            AlignSelf = CssLayout.GetAlignSelf(child);
        }

        public FlexItem(CssGeneratedPseudoElement generated) => Generated = generated;

        public Control? Child { get; }
        public CssGeneratedPseudoElement? Generated { get; }
        public string? AlignSelf { get; set; }
        public double Main { get; set; }
        public double MinimumMain { get; set; }
        public double Cross { get; set; }
        public bool HasExplicitCross { get; set; }
        public double Grow { get; set; }
        public double Shrink { get; set; }
        public double MainMarginStart { get; set; }
        public double MainMarginEnd { get; set; }
        public bool MainMarginStartIsAuto { get; set; }
        public bool MainMarginEndIsAuto { get; set; }
        public double CrossMarginStart { get; set; }
        public double CrossMarginEnd { get; set; }
        public double? FirstBaseline { get; set; }
    }

    private sealed class FlexLine
    {
        public List<FlexItem> Items { get; } = [];
        public double CrossSize { get; set; }
        public double FirstBaseline { get; set; }
    }

    private static double FiniteOrInfinity(double value)
        => double.IsFinite(value) ? Math.Max(0, value) : double.PositiveInfinity;

    private static double? ResolveForMeasure(CssLength? length, double reference)
    {
        var value = CssLayout.Resolve(length, reference);
        return value.HasValue && double.IsFinite(value.Value) ? Math.Max(0, value.Value) : null;
    }

    private static double ResolveRelativeInset(CssLength? length, double reference)
    {
        var value = CssLayout.Resolve(length, reference);
        return value.HasValue && double.IsFinite(value.Value) ? value.Value : 0;
    }

    private static double FiniteOrZero(double value)
        => double.IsFinite(value) ? Math.Max(0, value) : 0d;

    private static double SubtractFinite(double value, double amount)
        => double.IsFinite(value) ? Math.Max(0, value - amount) : value;

    private static Thickness ResolveMargin(Control control, double width, double height)
        => ResolveBox(control, width, height, allowNegative: true,
            CssLayout.GetMarginTop, CssLayout.GetMarginRight, CssLayout.GetMarginBottom, CssLayout.GetMarginLeft);

    private static Thickness ResolvePadding(Control control, double width, double height)
        => ResolveBox(control, width, height, allowNegative: false,
            CssLayout.GetPaddingTop, CssLayout.GetPaddingRight, CssLayout.GetPaddingBottom, CssLayout.GetPaddingLeft);

    private static Thickness ResolveBorder(Control control)
        => control switch
        {
            CssLayoutPanel panel => panel.BorderThickness,
            Border border => border.BorderThickness,
            TemplatedControl templated => templated.BorderThickness,
            _ => default
        };

    private static Thickness ResolveBox(
        Control control,
        double width,
        double height,
        bool allowNegative,
        Func<Control, CssLength?> top,
        Func<Control, CssLength?> right,
        Func<Control, CssLength?> bottom,
        Func<Control, CssLength?> left)
    {
        double Resolve(Func<Control, CssLength?> get, double reference)
        {
            var value = CssLayout.Resolve(get(control), reference) ?? 0;
            return allowNegative ? value : Math.Max(0, value);
        }

        return new Thickness(
            Resolve(left, width), Resolve(top, height),
            Resolve(right, width), Resolve(bottom, height));
    }

    private static double? ToOuterSize(Control control, double? cssSize, double padding)
        => cssSize.HasValue && CssLayout.GetBoxSizing(control) == CssBoxSizing.ContentBox
            ? cssSize.Value + padding
            : cssSize;

    private static double? ToContentSize(Control control, double? cssSize, double chrome)
        => cssSize.HasValue && CssLayout.GetBoxSizing(control) == CssBoxSizing.BorderBox
            ? Math.Max(0, cssSize.Value - chrome)
            : cssSize;

    private static bool HasIndefinitePercentageHeightBasis(Control control)
    {
        // A finite native host constraint does not make a DOM element whose
        // computed CSS height is auto into a definite percentage basis. The
        // cascade flag distinguishes that case from manually constructed
        // Avalonia panels, which retain the existing default-definite contract.
        // A virtual iframe document root is the browsing context's viewport
        // boundary and therefore supplies a definite percentage basis even
        // when the reused DOM body wrapper computes `height:auto`.
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
            // A DOM surface embedded below native Avalonia layout receives a
            // definite used height from that host, regardless of whether the
            // immediate native container is a Border, Grid, or presenter. A
            // directly TopLevel-hosted DOM body has no such embedding contract
            // and retains CSS `height:auto` semantics.
            return (control.Parent is null or TopLevel)
                   && CssLayout.GetCssHeightIsAuto(control);
        }

        return CssLayout.GetDisplay(parent) is not (
            CssDisplay.Flex or CssDisplay.InlineFlex
            or CssDisplay.Grid or CssDisplay.InlineGrid
            or CssDisplay.Table or CssDisplay.InlineTable
            or CssDisplay.TableRow);
    }

    private static double Constrain(double value, double? minimum, double? maximum)
    {
        if (double.IsNaN(value))
        {
            value = 0;
        }

        value = Math.Max(0, value);
        // CSS resolves max-* before min-*; the minimum consequently wins when
        // authors provide contradictory constraints (min-width > max-width).
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
}

internal sealed class CssGeneratedPseudoElement
{
    private static readonly string[] s_signatureProperties =
    [
        "content", "position", "visibility", "display", "background-color", "opacity",
        "color", "font-family", "font-size", "font-style", "font-weight", "line-height",
        "left", "right", "top", "bottom", "width", "height", "transform", "z-index",
        "border-radius", "border-top-left-radius",
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "align-self", "order", "flex-grow", "flex-shrink"
    ];

    private Rect? _arrangedFlowRect;
    private FormattedText? _formattedText;

    private CssGeneratedPseudoElement() { }

    public IBrush? Background { get; private init; }
    public CssLength? Left { get; private init; }
    public CssLength? Right { get; private init; }
    public CssLength? Top { get; private init; }
    public CssLength? Bottom { get; private init; }
    public CssLength? Width { get; private init; }
    public CssLength? Height { get; private init; }
    public CssLength? TranslateX { get; private init; }
    public CssLength? TranslateY { get; private init; }
    public CssLength? CornerRadius { get; private init; }
    public CssLength? MarginTop { get; private init; }
    public CssLength? MarginRight { get; private init; }
    public CssLength? MarginBottom { get; private init; }
    public CssLength? MarginLeft { get; private init; }
    public string? AlignSelf { get; private init; }
    public int Order { get; private init; }
    public int? ZIndex { get; private init; }
    public double FlexGrow { get; private init; }
    public double FlexShrink { get; private init; } = 1;
    public bool IsPaintVisible { get; private init; } = true;
    public bool IsInFlow { get; private init; }
    public bool IsFlowBlock { get; private init; }
    public bool IsInlineText { get; private init; }
    public Size IntrinsicSize { get; private init; }
    public bool HasText => _formattedText is not null;
    public double? FirstBaseline { get; private init; }

    public double ResolveLeft(double reference) => Math.Max(0, CssLayout.Resolve(Left, reference) ?? 0);
    public double ResolveRight(double reference) => Math.Max(0, CssLayout.Resolve(Right, reference) ?? 0);
    public double ResolveTop(double reference) => Math.Max(0, CssLayout.Resolve(Top, reference) ?? 0);
    public double ResolveBottom(double reference) => Math.Max(0, CssLayout.Resolve(Bottom, reference) ?? 0);
    public double ResolveCornerRadius(double reference)
        => Math.Max(0, CssLayout.Resolve(CornerRadius, reference) ?? 0);

    public Rect ResolveRect(Size containingSize)
    {
        var resolvedLeft = CssLayout.Resolve(Left, containingSize.Width);
        var resolvedRight = CssLayout.Resolve(Right, containingSize.Width);
        var resolvedTop = CssLayout.Resolve(Top, containingSize.Height);
        var resolvedBottom = CssLayout.Resolve(Bottom, containingSize.Height);
        var resolvedWidth = CssLayout.Resolve(Width, containingSize.Width);
        var resolvedHeight = CssLayout.Resolve(Height, containingSize.Height);
        var marginLeft = CssLayout.Resolve(MarginLeft, containingSize.Width) ?? 0;
        var marginRight = CssLayout.Resolve(MarginRight, containingSize.Width) ?? 0;
        var marginTop = CssLayout.Resolve(MarginTop, containingSize.Height) ?? 0;
        var marginBottom = CssLayout.Resolve(MarginBottom, containingSize.Height) ?? 0;
        var width = Math.Max(0, resolvedWidth
                                ?? containingSize.Width - (resolvedLeft ?? 0) - (resolvedRight ?? 0)
                                - marginLeft - marginRight);
        var height = Math.Max(0, resolvedHeight
                                 ?? containingSize.Height - (resolvedTop ?? 0) - (resolvedBottom ?? 0)
                                 - marginTop - marginBottom);
        var left = resolvedLeft.HasValue
            ? resolvedLeft.Value + marginLeft
            : resolvedRight.HasValue && resolvedWidth.HasValue
                ? containingSize.Width - resolvedRight.Value - width - marginRight
                : marginLeft;
        var top = resolvedTop.HasValue
            ? resolvedTop.Value + marginTop
            : resolvedBottom.HasValue && resolvedHeight.HasValue
                ? containingSize.Height - resolvedBottom.Value - height - marginBottom
                : marginTop;

        // Percentages in translate() resolve against the generated box itself,
        // not its originating element. Component libraries center small radio dots
        // inside an 18px marker with left/top:50% and translate(-50%,-50%).
        left += CssLayout.Resolve(TranslateX, width) ?? 0;
        top += CssLayout.Resolve(TranslateY, height) ?? 0;
        return new Rect(left, top, width, height);
    }

    public Rect ResolveRect(Rect containingBlock)
    {
        var local = ResolveRect(containingBlock.Size);
        return new Rect(
            containingBlock.X + local.X,
            containingBlock.Y + local.Y,
            local.Width,
            local.Height);
    }

    public Size ResolveFlowSize(double widthReference, double heightReference)
        => new(
            Math.Max(0, CssLayout.Resolve(Width, widthReference) ?? IntrinsicSize.Width),
            Math.Max(0, CssLayout.Resolve(Height, heightReference) ?? IntrinsicSize.Height));

    public void DrawText(DrawingContext context, Point origin)
    {
        if (_formattedText is not null)
        {
            context.DrawText(_formattedText, origin);
        }
    }

    public Thickness ResolveMargin(double widthReference, double heightReference)
        => new(
            Math.Max(0, CssLayout.Resolve(MarginLeft, widthReference) ?? 0),
            Math.Max(0, CssLayout.Resolve(MarginTop, heightReference) ?? 0),
            Math.Max(0, CssLayout.Resolve(MarginRight, widthReference) ?? 0),
            Math.Max(0, CssLayout.Resolve(MarginBottom, heightReference) ?? 0));

    public void SetArrangedFlowRect(Rect rect) => _arrangedFlowRect = rect;

    public void ClearArrangedFlowRect() => _arrangedFlowRect = null;

    public bool TryResolvePaintRect(Rect absoluteContainingBlock, Rect content, bool before, out Rect rect)
    {
        if (_arrangedFlowRect is { } arranged)
        {
            rect = arranged;
            return true;
        }
        if (IsFlowBlock)
        {
            rect = ResolveFlowRect(content, before);
            return true;
        }
        if (!IsInFlow)
        {
            rect = ResolveRect(absoluteContainingBlock);
            return true;
        }

        rect = default;
        return false;
    }

    public double ResolveFlowOuterHeight(double reference)
    {
        if (!IsFlowBlock)
        {
            return 0;
        }

        return Math.Max(0, CssLayout.Resolve(MarginTop, reference) ?? 0)
               + Math.Max(0, CssLayout.Resolve(Height, reference) ?? IntrinsicSize.Height)
               + Math.Max(0, CssLayout.Resolve(MarginBottom, reference) ?? 0);
    }

    public double ResolveFlowMinimumWidth(double reference)
    {
        if (!IsFlowBlock)
        {
            return 0;
        }

        return Math.Max(0, CssLayout.Resolve(MarginLeft, reference) ?? 0)
               + Math.Max(0, CssLayout.Resolve(Width, reference) ?? IntrinsicSize.Width)
               + Math.Max(0, CssLayout.Resolve(MarginRight, reference) ?? 0);
    }

    public Rect ResolveFlowRect(Rect content, bool before)
    {
        var marginTop = Math.Max(0, CssLayout.Resolve(MarginTop, content.Height) ?? 0);
        var marginRight = Math.Max(0, CssLayout.Resolve(MarginRight, content.Width) ?? 0);
        var marginBottom = Math.Max(0, CssLayout.Resolve(MarginBottom, content.Height) ?? 0);
        var marginLeft = Math.Max(0, CssLayout.Resolve(MarginLeft, content.Width) ?? 0);
        var width = Math.Max(0, CssLayout.Resolve(Width, content.Width)
                                ?? content.Width - marginLeft - marginRight);
        var height = Math.Max(0, CssLayout.Resolve(Height, content.Height) ?? IntrinsicSize.Height);
        var y = before
            ? content.Y + marginTop
            : content.Bottom - marginTop - height - marginBottom;
        return new Rect(content.X + marginLeft, y, width, height);
    }

    public static CssGeneratedPseudoElement? Create(IReadOnlyDictionary<string, string>? values)
    {
        if (values is null
            || !values.TryGetValue("content", out var content)
            || string.Equals(content.Trim(), "none", StringComparison.OrdinalIgnoreCase)
            || string.Equals(content.Trim(), "normal", StringComparison.OrdinalIgnoreCase)
            || (values.TryGetValue("display", out var display)
                && string.Equals(display.Trim(), "none", StringComparison.OrdinalIgnoreCase)))
        {
            return null;
        }

        var visibilityValue = values.TryGetValue("visibility", out var visibility)
            ? visibility.Trim()
            : string.Empty;
        var paintVisible = !string.Equals(visibilityValue, "hidden", StringComparison.OrdinalIgnoreCase)
                           && !string.Equals(visibilityValue, "collapse", StringComparison.OrdinalIgnoreCase);
        var absolute = values.TryGetValue("position", out var position)
                       && string.Equals(position.Trim(), "absolute", StringComparison.OrdinalIgnoreCase);
        var flowBlock = !absolute
                        && values.TryGetValue("display", out var displayValue)
                        && string.Equals(displayValue.Trim(), "block", StringComparison.OrdinalIgnoreCase);
        var inlineText = !absolute
                         && (!values.TryGetValue("display", out var inlineDisplayValue)
                             || string.Equals(inlineDisplayValue.Trim(), "inline", StringComparison.OrdinalIgnoreCase));
        var inFlow = !absolute;

        IBrush? background = null;
        if (values.TryGetValue("background-color", out var backgroundValue))
        {
            if (CssValueParser.TryParseColor(backgroundValue, out var functionalColor))
            {
                background = new SolidColorBrush(functionalColor);
            }
            else
            {
                try { background = Brush.Parse(backgroundValue); } catch { }
            }
        }
        var opacity = 1d;
        if (values.TryGetValue("opacity", out var opacityValue)
            && double.TryParse(opacityValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsedOpacity))
        {
            opacity = Math.Clamp(parsedOpacity, 0, 1);
        }
        if (background is ISolidColorBrush solid && opacity < 1)
        {
            var color = solid.Color;
            background = new SolidColorBrush(Color.FromArgb(
                (byte)Math.Round(color.A * opacity),
                color.R,
                color.G,
                color.B));
        }

        static CssLength? Length(IReadOnlyDictionary<string, string> source, string name)
            => source.TryGetValue(name, out var value) && CssLayout.TryParseLength(value, out var length)
                ? length
                : null;
        CssLength? radius = null;
        if (values.TryGetValue("border-radius", out var radiusValue)
            || values.TryGetValue("border-top-left-radius", out radiusValue))
        {
            var first = radiusValue.Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
            if (CssLayout.TryParseLength(first, out var parsedRadius))
            {
                radius = parsedRadius;
            }
        }
        ParseTranslation(values.GetValueOrDefault("transform"), out var translateX, out var translateY);
        var order = 0;
        if (values.TryGetValue("order", out var orderValue))
        {
            int.TryParse(orderValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out order);
        }
        int? zIndex = null;
        if (values.TryGetValue("z-index", out var zIndexValue)
            && int.TryParse(
                zIndexValue,
                NumberStyles.Integer,
                CultureInfo.InvariantCulture,
                out var parsedZIndex))
        {
            zIndex = parsedZIndex;
        }
        var flexGrow = ParseNonNegativeNumber(values.GetValueOrDefault("flex-grow"), 0);
        var flexShrink = ParseNonNegativeNumber(values.GetValueOrDefault("flex-shrink"), 1);
        var formattedText = CreateFormattedLiteralText(values, content, opacity);

        return new CssGeneratedPseudoElement
        {
            Background = background,
            Left = Length(values, "left"),
            Right = Length(values, "right"),
            Top = Length(values, "top"),
            Bottom = Length(values, "bottom"),
            Width = Length(values, "width"),
            Height = Length(values, "height"),
            TranslateX = translateX,
            TranslateY = translateY,
            CornerRadius = radius,
            MarginTop = Length(values, "margin-top"),
            MarginRight = Length(values, "margin-right"),
            MarginBottom = Length(values, "margin-bottom"),
            MarginLeft = Length(values, "margin-left"),
            AlignSelf = values.GetValueOrDefault("align-self"),
            Order = order,
            ZIndex = zIndex,
            FlexGrow = flexGrow,
            FlexShrink = flexShrink,
            IsPaintVisible = paintVisible,
            IsInFlow = inFlow,
            IsFlowBlock = flowBlock,
            IsInlineText = inlineText,
            _formattedText = formattedText,
            FirstBaseline = formattedText?.Baseline,
            IntrinsicSize = formattedText is null
                ? default
                : new Size(formattedText.WidthIncludingTrailingWhitespace, formattedText.Height)
        };

        static double ParseNonNegativeNumber(string? value, double fallback)
            => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
                ? Math.Max(0, parsed)
                : fallback;
    }

    private static FormattedText? CreateFormattedLiteralText(
        IReadOnlyDictionary<string, string> values,
        string content,
        double opacity)
    {
        if (!TryDecodeLiteralContent(content, out var text) || text.Length == 0)
        {
            return null;
        }

        var fontSize = 16d;
        if (values.TryGetValue("font-size", out var sizeValue)
            && CssLayout.TryParseLength(sizeValue, out var sizeLength)
            && sizeLength is { Unit: CssLengthUnit.Pixel } parsedSize)
        {
            fontSize = Math.Max(1, parsedSize.Value);
        }
        var fontStyle = values.GetValueOrDefault("font-style", "normal").Trim().ToLowerInvariant() switch
        {
            "italic" => FontStyle.Italic,
            "oblique" => FontStyle.Oblique,
            _ => FontStyle.Normal
        };
        var weightValue = values.GetValueOrDefault("font-weight", "400").Trim();
        var fontWeight = weightValue.ToLowerInvariant() switch
        {
            "bold" or "bolder" => FontWeight.Bold,
            "lighter" => FontWeight.Light,
            _ when int.TryParse(weightValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var numeric)
                => (FontWeight)Math.Clamp(numeric, 1, 1000),
            _ => FontWeight.Normal
        };
        var family = CssFontResolver.Resolve(values.GetValueOrDefault("font-family", "sans-serif")).Family;
        IBrush brush = Brushes.Black;
        if (values.TryGetValue("color", out var colorValue)
            && CssValueParser.TryParseColor(colorValue, out var color))
        {
            brush = new SolidColorBrush(color);
        }
        if (brush is ISolidColorBrush solid && opacity < 1)
        {
            var solidColor = solid.Color;
            brush = new SolidColorBrush(Color.FromArgb(
                (byte)Math.Round(solidColor.A * opacity), solidColor.R, solidColor.G, solidColor.B));
        }
        var formatted = new FormattedText(
            text,
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            new Typeface(family, fontStyle, fontWeight),
            fontSize,
            brush);
        if (values.TryGetValue("line-height", out var lineHeightValue)
            && CssLayout.TryParseLength(lineHeightValue, out var lineHeight)
            && lineHeight is { Unit: CssLengthUnit.Pixel } parsedLineHeight)
        {
            formatted.LineHeight = Math.Max(0, parsedLineHeight.Value);
        }
        return formatted;
    }

    private static bool TryDecodeLiteralContent(string source, out string value)
    {
        value = string.Empty;
        var text = source.Trim();
        if (text.Length < 2 || text[0] is not ('\'' or '"') || text[^1] != text[0])
        {
            return false;
        }

        var builder = new System.Text.StringBuilder(text.Length - 2);
        for (var index = 1; index < text.Length - 1; index++)
        {
            var current = text[index];
            if (current != '\\')
            {
                builder.Append(current);
                continue;
            }
            if (++index >= text.Length - 1)
            {
                continue;
            }

            var escapeStart = index;
            var digits = 0;
            while (index < text.Length - 1
                   && digits < 6
                   && Uri.IsHexDigit(text[index]))
            {
                index++;
                digits++;
            }
            if (digits > 0)
            {
                var scalar = int.Parse(text.AsSpan(escapeStart, digits), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                builder.Append(char.ConvertFromUtf32(Math.Clamp(scalar, 0, 0x10ffff)));
                if (index < text.Length - 1 && char.IsWhiteSpace(text[index]))
                {
                    continue;
                }
                index--;
                continue;
            }

            builder.Append(text[index]);
        }
        value = builder.ToString();
        return true;
    }

    private static void ParseTranslation(string? transform, out CssLength? x, out CssLength? y)
    {
        x = null;
        y = null;
        if (string.IsNullOrWhiteSpace(transform))
        {
            return;
        }

        if (ParseFunction("translate", transform, out var combinedX, out var combinedY))
        {
            x = combinedX;
            y = combinedY;
        }
        if (ParseFunction("translateX", transform, out var axisX, out _))
        {
            x = axisX;
        }
        if (ParseFunction("translateY", transform, out var axisY, out _))
        {
            y = axisY;
        }

        static bool ParseFunction(
            string name,
            string source,
            out CssLength? first,
            out CssLength? second)
        {
            first = null;
            second = null;
            var start = source.IndexOf(name + "(", StringComparison.OrdinalIgnoreCase);
            if (start < 0)
            {
                return false;
            }

            start += name.Length + 1;
            var end = source.IndexOf(')', start);
            if (end < 0)
            {
                return false;
            }

            var arguments = source[start..end]
                .Split([',', ' '], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (arguments.Length > 0 && CssLayout.TryParseLength(arguments[0], out var parsedFirst))
            {
                first = parsedFirst;
            }
            if (arguments.Length > 1 && CssLayout.TryParseLength(arguments[1], out var parsedSecond))
            {
                second = parsedSecond;
            }
            return first.HasValue;
        }
    }

    internal static string? CreateSignature(IReadOnlyDictionary<string, string>? values)
    {
        if (values is null)
        {
            return null;
        }

        var builder = new System.Text.StringBuilder();
        foreach (var property in s_signatureProperties)
        {
            if (values.TryGetValue(property, out var value))
            {
                builder.Append(property).Append('=').Append(value).Append(';');
            }
        }
        return builder.Length == 0 ? null : builder.ToString();
    }
}

/// <summary>
/// Infrastructure paint for a DOM surface. The DOM bridge excludes this marker from
/// traversal, sibling relationships, selectors, and element counts.
/// </summary>
internal interface IDomInfrastructureControl
{
}

internal sealed class DomGeneratedBackgroundControl : CssLayoutPanel, IDomInfrastructureControl
{
    internal DomGeneratedBackgroundControl(bool before) => Before = before;

    internal bool Before { get; }

    internal CssGeneratedPseudoElement Generated { get; set; } = null!;
}

internal sealed class DomGeneratedTextControl : Control, IDomInfrastructureControl
{
    private readonly CssLayoutPanel _owner;
    private readonly bool _before;

    internal DomGeneratedTextControl(CssLayoutPanel owner, bool before)
    {
        _owner = owner;
        _before = before;
    }

    internal CssGeneratedPseudoElement Generated { get; set; } = null!;

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Generated.IsPaintVisible
            && _owner.TryResolveGeneratedPaintRect(Generated, _before, out var rect))
        {
            // Authored text inside a flex-item principal box is clipped at the
            // box edge. Preserve that same edge behavior for generated text.
            using (context.PushClip(rect))
            {
                Generated.DrawText(context, rect.TopLeft);
            }
        }
    }
}

internal sealed class DomListMarkerControl : Control, IDomInfrastructureControl
{
    internal required CssListMarker Marker { get; set; }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var marker = Marker;
        var rect = new Rect(Bounds.Size);
        switch (marker.Type)
        {
            case CssListStyleType.Disc:
                DrawShape(brush: marker.Brush);
                break;
            case CssListStyleType.Circle:
                DrawShape(pen: new Pen(marker.Brush, 1));
                break;
            case CssListStyleType.Square:
                var square = ShapeRect();
                context.FillRectangle(marker.Brush, square);
                break;
            case CssListStyleType.Decimal:
                var text = new FormattedText(
                    marker.Text,
                    CultureInfo.CurrentCulture,
                    FlowDirection.LeftToRight,
                    marker.Typeface,
                    marker.FontSize,
                    marker.Brush);
                context.DrawText(text, default);
                break;
        }

        return;

        Rect ShapeRect()
        {
            var side = Math.Min(rect.Width, Math.Max(5, marker.FontSize * 0.375));
            return new Rect(
                (rect.Width - side) / 2,
                (rect.Height - side) / 2,
                side,
                side);
        }

        void DrawShape(IBrush? brush = null, Pen? pen = null)
        {
            var shape = ShapeRect();
            context.DrawEllipse(brush, pen, shape.Center, shape.Width / 2, shape.Height / 2);
        }
    }
}

internal sealed class DomBorderOverlayControl : Control, IDomInfrastructureControl
{
    private readonly CssLayoutPanel _owner;

    internal DomBorderOverlayControl(CssLayoutPanel owner) => _owner = owner;

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var thickness = _owner.BorderThickness;
        var topBrush = _owner.BorderTopBrush;
        var rightBrush = _owner.BorderRightBrush;
        var bottomBrush = _owner.BorderBottomBrush;
        var leftBrush = _owner.BorderLeftBrush;
        if (Bounds.Width <= 0 || Bounds.Height <= 0) return;

        var radius = _owner.CornerRadius;
        if (IsUniform(thickness)
            && thickness.Left > 0
            && BrushesMatch(topBrush, rightBrush)
            && BrushesMatch(topBrush, bottomBrush)
            && BrushesMatch(topBrush, leftBrush)
            && topBrush is not null
            && (radius.TopLeft > 0
                || radius.TopRight > 0
                || radius.BottomRight > 0
                || radius.BottomLeft > 0))
        {
            var half = thickness.Left / 2d;
            var rect = new Rect(
                half,
                half,
                Math.Max(0, Bounds.Width - thickness.Left),
                Math.Max(0, Bounds.Height - thickness.Left));
            var innerRadius = new CornerRadius(
                Math.Max(0, radius.TopLeft - half),
                Math.Max(0, radius.TopRight - half),
                Math.Max(0, radius.BottomRight - half),
                Math.Max(0, radius.BottomLeft - half));
            context.DrawRectangle(
                null,
                new Pen(topBrush, thickness.Left),
                new RoundedRect(rect, innerRadius));
            return;
        }

        // Preserve independent CSS side widths and colors. Avalonia's single
        // BorderBrush cannot model a transparent spacing edge next to painted
        // edges, a shape used by compact inline legend values.
        if (thickness.Top > 0 && topBrush is not null)
            context.FillRectangle(topBrush, new Rect(0, 0, Bounds.Width, Math.Min(thickness.Top, Bounds.Height)));
        if (thickness.Bottom > 0 && bottomBrush is not null)
            context.FillRectangle(bottomBrush, new Rect(0, Math.Max(0, Bounds.Height - thickness.Bottom), Bounds.Width, Math.Min(thickness.Bottom, Bounds.Height)));
        if (thickness.Left > 0 && leftBrush is not null)
            context.FillRectangle(leftBrush, new Rect(0, 0, Math.Min(thickness.Left, Bounds.Width), Bounds.Height));
        if (thickness.Right > 0 && rightBrush is not null)
            context.FillRectangle(rightBrush, new Rect(Math.Max(0, Bounds.Width - thickness.Right), 0, Math.Min(thickness.Right, Bounds.Width), Bounds.Height));

        static bool IsUniform(Thickness value)
            => Math.Abs(value.Left - value.Top) < 0.001
               && Math.Abs(value.Left - value.Right) < 0.001
               && Math.Abs(value.Left - value.Bottom) < 0.001;

        static bool BrushesMatch(IBrush? first, IBrush? second)
            => ReferenceEquals(first, second)
               || (first is ISolidColorBrush firstSolid
                   && second is ISolidColorBrush secondSolid
                   && firstSolid.Color == secondSolid.Color
                   && Math.Abs(firstSolid.Opacity - secondSolid.Opacity) < 0.001);
    }
}

internal sealed class DomOutlineOverlayControl : Control, IDomInfrastructureControl
{
    private readonly CssLayoutPanel _owner;

    internal DomOutlineOverlayControl(CssLayoutPanel owner) => _owner = owner;

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var brush = _owner.OutlineBrush;
        var width = _owner.OutlineWidth;
        if (brush is null
            || width <= 0
            || _owner.OutlineStyle.Trim().ToLowerInvariant() is not ("solid" or "auto")
            || Bounds.Width <= 0
            || Bounds.Height <= 0)
        {
            return;
        }

        var offset = _owner.OutlineOffset;
        var extent = Math.Max(0, offset + width);
        var centerInset = extent - offset - width / 2d;
        var rect = new Rect(
            centerInset,
            centerInset,
            Math.Max(0, Bounds.Width - centerInset * 2),
            Math.Max(0, Bounds.Height - centerInset * 2));
        if (rect.Width <= 0 || rect.Height <= 0) return;

        var sourceRadius = _owner.CornerRadius;
        var radiusExpansion = offset + width / 2d;
        var outlineRadius = new CornerRadius(
            Math.Max(0, sourceRadius.TopLeft + radiusExpansion),
            Math.Max(0, sourceRadius.TopRight + radiusExpansion),
            Math.Max(0, sourceRadius.BottomRight + radiusExpansion),
            Math.Max(0, sourceRadius.BottomLeft + radiusExpansion));
        context.DrawRectangle(
            null,
            new Pen(brush, width),
            new RoundedRect(rect, outlineRadius));
    }
}

internal sealed class DomScrollIndicatorControl : Control, IDomInfrastructureControl
{
    private static readonly IBrush ThumbBrush =
        new SolidColorBrush(Color.FromArgb(196, 99, 99, 99));
    private readonly CssLayoutPanel _owner;

    public DomScrollIndicatorControl(CssLayoutPanel owner) => _owner = owner;

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var viewport = _owner.ScrollViewport;
        var extent = _owner.ScrollExtent;
        if (viewport.Height <= 0 || extent.Height <= viewport.Height + 0.5)
        {
            return;
        }

        const double width = 5;
        const double inset = 1;
        var trackHeight = Math.Max(0, Bounds.Height - inset * 2);
        if (trackHeight <= 0)
        {
            return;
        }
        var thumbHeight = Math.Clamp(
            trackHeight * viewport.Height / extent.Height,
            Math.Min(24, trackHeight),
            trackHeight);
        var maximumOffset = Math.Max(0, extent.Height - viewport.Height);
        var thumbTravel = Math.Max(0, trackHeight - thumbHeight);
        var thumbTop = inset + (maximumOffset <= 0
            ? 0
            : thumbTravel * _owner.ScrollOffset.Y / maximumOffset);
        context.DrawRectangle(
            ThumbBrush,
            null,
            new Rect(Math.Max(0, Bounds.Width - width), thumbTop, width - inset, thumbHeight),
            2,
            2);
    }
}

internal sealed class DomLoadingOverlayControl : Control, IDomInfrastructureControl
{
    private static readonly IBrush ScrimBrush = new SolidColorBrush(Color.FromArgb(214, 6, 14, 16));
    private static readonly IBrush SpinnerBrush = new SolidColorBrush(Color.FromRgb(41, 182, 246));
    private static readonly IBrush TextBrush = new SolidColorBrush(Color.FromRgb(236, 242, 244));
    private static readonly Typeface TextTypeface = new(FontFamily.Default, FontStyle.Normal, FontWeight.Medium);
    private DispatcherTimer? _timer;
    private string _text = "Compiling JavaScript";

    internal DomLoadingOverlayControl()
    {
        DetachedFromVisualTree += (_, _) => Stop();
    }

    internal string Text
    {
        get => _text;
        set
        {
            var next = string.IsNullOrWhiteSpace(value) ? "Loading" : value.Trim();
            if (string.Equals(_text, next, StringComparison.Ordinal))
            {
                return;
            }
            _text = next;
            InvalidateVisual();
        }
    }

    internal int Frame { get; private set; }

    internal bool IsRunning => _timer is not null;

    internal void Start()
    {
        if (_timer is not null)
        {
            return;
        }

        Frame = 0;
        var timer = new DispatcherTimer(DispatcherPriority.Render)
        {
            Interval = TimeSpan.FromMilliseconds(80)
        };
        timer.Tick += OnTick;
        _timer = timer;
        timer.Start();
        InvalidateVisual();
    }

    internal void Stop()
    {
        if (_timer is null)
        {
            return;
        }
        _timer.Stop();
        _timer.Tick -= OnTick;
        _timer = null;
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return;
        }

        var bounds = new Rect(Bounds.Size);
        context.FillRectangle(ScrimBrush, bounds);

        const int dotCount = 10;
        const double orbitRadius = 18;
        const double dotRadius = 3;
        var text = new FormattedText(
            _text,
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            TextTypeface,
            15,
            TextBrush);
        var groupHeight = orbitRadius * 2 + 14 + text.Height;
        var spinnerCenter = new Point(
            bounds.Center.X,
            Math.Max(orbitRadius + 8, bounds.Center.Y - groupHeight / 2 + orbitRadius));
        for (var index = 0; index < dotCount; index++)
        {
            var position = (index + Frame) % dotCount;
            var angle = index * Math.PI * 2 / dotCount - Math.PI / 2;
            var opacity = 0.18 + 0.82 * (position + 1d) / dotCount;
            using (context.PushOpacity(opacity))
            {
                context.DrawEllipse(
                    SpinnerBrush,
                    null,
                    new Point(
                        spinnerCenter.X + Math.Cos(angle) * orbitRadius,
                        spinnerCenter.Y + Math.Sin(angle) * orbitRadius),
                    dotRadius,
                    dotRadius);
            }
        }

        context.DrawText(
            text,
            new Point(
                Math.Max(8, bounds.Center.X - text.Width / 2),
                spinnerCenter.Y + orbitRadius + 14));
    }

    private void OnTick(object? sender, EventArgs e)
    {
        AdvanceFrame();
    }

    internal void AdvanceFrame()
    {
        Frame = (Frame + 1) % 10;
        InvalidateVisual();
    }
}

/// <summary>
/// Multi-child visual host for an HTML button. Unlike Avalonia's ContentControl-
/// based Button, an HTML button must retain every DOM child (for example an SVG
/// icon followed by a text label).
/// </summary>
internal sealed class DomButtonControl : CssLayoutPanel
{
    public DomButtonControl()
    {
        Focusable = true;
        IsTabStop = true;
    }
}

/// <summary>
/// Principal-box marker for HTML <c>br</c>. Its parent inline formatting
/// context consumes the forced break; the control itself paints no box.
/// </summary>
internal sealed class DomLineBreakControl : CssLayoutPanel;

/// <summary>
/// TextBlock projection for DOM text nodes. CSS system-family aliases can have
/// different horizontal advances in Chromium and Avalonia even when both map
/// to the platform default face. Keep measurement and paint on the same scale
/// without consuming RenderTransform, which belongs to the CSS transform model.
/// </summary>
internal sealed class DomTextBlockControl : TextBlock
{
    private double _fontWidthScale = 1d;
    private double _wordSpacing;
    private string _whiteSpacePresentationText = string.Empty;
    private bool _collapsesWhiteSpace = true;
    private bool _cssAllowsLayout = true;

    public DomTextBlockControl()
    {
        // CSS inline layout retains fractional glyph advances. Avalonia's
        // inherited layout rounding otherwise rounds every text run to a whole
        // device pixel after MeasureOverride and accumulates width drift.
        UseLayoutRounding = false;
    }

    internal double FontWidthScale
    {
        get => _fontWidthScale;
        set
        {
            var normalized = double.IsFinite(value) && value > 0 ? value : 1d;
            if (Math.Abs(_fontWidthScale - normalized) < 0.0001)
            {
                return;
            }
            _fontWidthScale = normalized;
            InvalidateMeasure();
            InvalidateVisual();
        }
    }

    internal double WordSpacing
    {
        get => _wordSpacing;
        set
        {
            var normalized = double.IsFinite(value) ? value : 0;
            if (Math.Abs(_wordSpacing - normalized) < 0.0001)
            {
                return;
            }
            _wordSpacing = normalized;
            InvalidateMeasure();
            InvalidateVisual();
        }
    }

    internal bool CssAllowsLayout => _cssAllowsLayout;

    internal void SetWhiteSpacePresentation(string text, bool collapsesWhiteSpace)
    {
        _whiteSpacePresentationText = text ?? string.Empty;
        _collapsesWhiteSpace = collapsesWhiteSpace;
        RefreshWhitespaceBoundaries(trimStart: false, trimEnd: false);
    }

    internal void RefreshWhitespaceBoundaries(bool trimStart, bool trimEnd)
    {
        var next = _whiteSpacePresentationText;
        if (_collapsesWhiteSpace)
        {
            if (trimStart) next = next.TrimStart(' ');
            if (trimEnd) next = next.TrimEnd(' ');
        }
        if (!string.Equals(Text, next, StringComparison.Ordinal))
        {
            Text = next;
        }
        IsVisible = _cssAllowsLayout && !string.IsNullOrWhiteSpace(next);
    }

    internal void SetCssAllowsLayout(bool value)
    {
        if (_cssAllowsLayout == value) return;
        _cssAllowsLayout = value;
        RefreshWhitespaceBoundaries(trimStart: false, trimEnd: false);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var measured = base.MeasureOverride(UnscaleWidth(availableSize));
        // TextBlock rounds its desired width up to a device pixel. Browsers
        // retain fractional glyph advances for inline layout, so use the
        // underlying TextLayout width while preserving Avalonia's line height.
        return new Size(
            Math.Max(
                0,
                TextLayout.WidthIncludingTrailingWhitespace * _fontWidthScale
                + ResolveWordSpacingAdvance()),
            measured.Height);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        if (Math.Abs(_fontWidthScale - 1d) < 0.0001)
        {
            return base.ArrangeOverride(finalSize);
        }

        base.ArrangeOverride(UnscaleWidth(finalSize));
        return finalSize;
    }

    protected override void RenderTextLayout(DrawingContext context, Point origin)
    {
        if (!UsesCustomWordSpacing
            && Math.Abs(_fontWidthScale - 1d) < 0.0001)
        {
            base.RenderTextLayout(context, origin);
            return;
        }

        using (context.PushTransform(Matrix.CreateScale(_fontWidthScale, 1d)))
        {
            var scaledOrigin = new Point(origin.X / _fontWidthScale, origin.Y);
            if (UsesCustomWordSpacing)
            {
                DrawWordSpacedText(
                    context,
                    scaledOrigin,
                    _wordSpacing / _fontWidthScale);
            }
            else
            {
                base.RenderTextLayout(context, scaledOrigin);
            }
        }
    }

    private bool UsesCustomWordSpacing
        => Math.Abs(_wordSpacing) >= 0.0001
           && TextWrapping == TextWrapping.NoWrap
           && FlowDirection == FlowDirection.LeftToRight
           && (Text?.Any(IsWordSeparator) ?? false);

    private double ResolveWordSpacingAdvance()
        => UsesCustomWordSpacing
            ? (Text?.Count(IsWordSeparator) ?? 0) * _wordSpacing
            : 0;

    private void DrawWordSpacedText(
        DrawingContext context,
        Point origin,
        double unscaledWordSpacing)
    {
        var text = Text ?? string.Empty;
        var x = origin.X;
        for (var start = 0; start < text.Length;)
        {
            var separator = IsWordSeparator(text[start]);
            var end = start + 1;
            while (end < text.Length && IsWordSeparator(text[end]) == separator)
            {
                end++;
            }

            var segment = text[start..end];
            var layout = CreateSegmentLayout(segment);
            layout.Draw(context, new Point(x, origin.Y));
            x += layout.WidthIncludingTrailingWhitespace;
            if (separator)
            {
                x += segment.Length * unscaledWordSpacing;
            }
            start = end;
        }
    }

    private TextLayout CreateSegmentLayout(string text)
        => new(
            text,
            new Typeface(FontFamily, FontStyle, FontWeight, FontStretch),
            FontSize,
            Foreground ?? Brushes.Black,
            TextAlignment.Left,
            TextWrapping.NoWrap,
            TextTrimming.None,
            TextDecorations,
            FlowDirection,
            double.PositiveInfinity,
            double.PositiveInfinity,
            LineHeight,
            LetterSpacing,
            1,
            null);

    private static bool IsWordSeparator(char value)
        => value is ' ' or '\u00a0';

    private Size UnscaleWidth(Size size)
        => new(double.IsFinite(size.Width) ? size.Width / _fontWidthScale : size.Width, size.Height);
}

/// <summary>
/// HTML text controls remain pointer targets across their full CSS box even
/// when author styles make their native Avalonia background transparent.
/// </summary>
internal sealed class DomTextInputControl : TextBox, ICustomHitTest
{
    private const int DefaultHtmlSize = 20;
    private string _placeholderText = string.Empty;
    private readonly DispatcherTimer _fallbackCaretTimer;
    private bool _fallbackCaretVisible;
    private int _htmlSize = DefaultHtmlSize;
    private bool _usesTextIntrinsicSize = true;

    internal Action<KeyEventArgs>? BeforeNativeKeyDown { get; set; }

    internal Action? NativeKeyEditCompleted { get; set; }

    public DomTextInputControl()
    {
        // A host is not required to install an Avalonia ControlTheme. In that
        // configuration TextBox still edits correctly but has no presenter to
        // invalidate or paint its text, so keep the fallback renderer live.
        _fallbackCaretTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(530), DispatcherPriority.Render, (_, _) =>
        {
            _fallbackCaretVisible = !_fallbackCaretVisible;
            InvalidateVisual();
        });
        TextChanged += (_, _) => ResetFallbackCaret();
        GotFocus += (_, _) => StartFallbackCaret();
        LostFocus += (_, _) => StopFallbackCaret();
        DetachedFromVisualTree += (_, _) => StopFallbackCaret();
    }

    public string PlaceholderText
    {
        get => _placeholderText;
        set
        {
            var next = value ?? string.Empty;
            if (string.Equals(_placeholderText, next, StringComparison.Ordinal))
            {
                return;
            }

            _placeholderText = next;
            InvalidateVisual();
        }
    }

    /// <summary>
    /// HTML text inputs have a preferred inline size even when they have no
    /// value (the <c>size</c> attribute defaults to 20). Avalonia's unthemed
    /// TextBox otherwise reports zero intrinsic width, which makes auto-sized
    /// inline-block and inline-flex ancestors collapse during shrink-to-fit.
    /// </summary>
    internal int HtmlSize
    {
        get => _htmlSize;
        set
        {
            var normalized = value > 0 ? value : DefaultHtmlSize;
            if (_htmlSize == normalized)
            {
                return;
            }
            _htmlSize = normalized;
            InvalidateMeasure();
        }
    }

    internal bool UsesTextIntrinsicSize
    {
        get => _usesTextIntrinsicSize;
        set
        {
            if (_usesTextIntrinsicSize == value)
            {
                return;
            }
            _usesTextIntrinsicSize = value;
            InvalidateMeasure();
        }
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var measured = base.MeasureOverride(availableSize);
        if (!_usesTextIntrinsicSize)
        {
            return measured;
        }
        // HTML defines size in average character columns. A half-em average
        // advance matches the browser UA sizing model closely enough for the
        // intrinsic contribution; author CSS still controls the final used
        // width and shrink-to-fit clamps it to the containing block.
        var preferredWidth = Math.Max(0, _htmlSize * FontSize * 0.5)
                             + Math.Max(0, Padding.Left)
                             + Math.Max(0, Padding.Right)
                             + Math.Max(0, BorderThickness.Left)
                             + Math.Max(0, BorderThickness.Right);
        var preferredHeight = Math.Max(0, CssLayout.GetLineHeight(this) ?? FontSize * 1.2)
                              + Math.Max(0, Padding.Top)
                              + Math.Max(0, Padding.Bottom)
                              + Math.Max(0, BorderThickness.Top)
                              + Math.Max(0, BorderThickness.Bottom);
        var width = Math.Max(measured.Width, preferredWidth);
        var height = Math.Max(measured.Height, preferredHeight);
        if (double.IsFinite(availableSize.Width))
        {
            width = Math.Min(width, Math.Max(0, availableSize.Width));
        }
        if (double.IsFinite(availableSize.Height))
        {
            height = Math.Min(height, Math.Max(0, availableSize.Height));
        }
        return new Size(width, height);
    }

    public bool HitTest(Point point)
        => !CssLayout.GetPointerEventsNone(this)
           && new Rect(Bounds.Size).Contains(point);

    internal bool FallbackCaretVisible => _fallbackCaretVisible;

    internal void AdvanceFallbackCaretForTest()
    {
        _fallbackCaretVisible = !_fallbackCaretVisible;
        InvalidateVisual();
    }

    private void StartFallbackCaret()
    {
        _fallbackCaretVisible = true;
        if (!this.GetVisualChildren().Any())
        {
            _fallbackCaretTimer.Start();
        }
        InvalidateVisual();
    }

    private void StopFallbackCaret()
    {
        _fallbackCaretTimer.Stop();
        _fallbackCaretVisible = false;
        InvalidateVisual();
    }

    private void ResetFallbackCaret()
    {
        if (IsFocused)
        {
            _fallbackCaretVisible = true;
            _fallbackCaretTimer.Stop();
            if (!this.GetVisualChildren().Any())
            {
                _fallbackCaretTimer.Start();
            }
        }
        InvalidateVisual();
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        var textBeforeEdit = Text;
        BeforeNativeKeyDown?.Invoke(e);
        if (e.Handled)
        {
            return;
        }

        base.OnKeyDown(e);
        ApplyFallbackKeyDefault(e);
        if (!string.Equals(textBeforeEdit, Text, StringComparison.Ordinal))
        {
            NativeKeyEditCompleted?.Invoke();
        }
    }

    private bool ApplyFallbackKeyDefault(KeyEventArgs e)
    {
        if (e.Handled || this.GetVisualChildren().Any())
        {
            return false;
        }

        // TextBox delegates editing/navigation keys to its template presenter.
        // WEBSCENE can run without an Avalonia theme, in which case the fallback
        // renderer has no presenter and those native defaults become no-ops.
        // Keep the single-line HTML input semantics available in that mode.
        var text = Text ?? string.Empty;
        var selectionStart = Math.Clamp(Math.Min(SelectionStart, SelectionEnd), 0, text.Length);
        var selectionEnd = Math.Clamp(Math.Max(SelectionStart, SelectionEnd), 0, text.Length);
        var hasSelection = selectionStart != selectionEnd;
        var extendSelection = (e.KeyModifiers & KeyModifiers.Shift) != 0;
        var handled = true;
        switch (e.Key)
        {
            case Key.Left:
                MoveFallbackCaret(
                    hasSelection && !extendSelection ? selectionStart : Math.Max(0, CaretIndex - 1),
                    extendSelection,
                    selectionStart,
                    selectionEnd);
                break;
            case Key.Right:
                MoveFallbackCaret(
                    hasSelection && !extendSelection ? selectionEnd : Math.Min(text.Length, CaretIndex + 1),
                    extendSelection,
                    selectionStart,
                    selectionEnd);
                break;
            case Key.Up:
            case Key.Home:
                MoveFallbackCaret(0, extendSelection, selectionStart, selectionEnd);
                break;
            case Key.Down:
            case Key.End:
                MoveFallbackCaret(text.Length, extendSelection, selectionStart, selectionEnd);
                break;
            case Key.Back when !IsReadOnly:
                if (hasSelection)
                {
                    ReplaceFallbackSelection(text, selectionStart, selectionEnd);
                }
                else if (CaretIndex > 0)
                {
                    var index = Math.Min(CaretIndex, text.Length) - 1;
                    Text = text.Remove(index, 1);
                    SetFallbackCaret(index);
                }
                break;
            case Key.Delete when !IsReadOnly:
                if (hasSelection)
                {
                    ReplaceFallbackSelection(text, selectionStart, selectionEnd);
                }
                else if (CaretIndex < text.Length)
                {
                    var index = Math.Max(0, CaretIndex);
                    Text = text.Remove(index, 1);
                    SetFallbackCaret(index);
                }
                break;
            default:
                handled = false;
                break;
        }

        if (handled)
        {
            e.Handled = true;
            ResetFallbackCaret();
        }

        return handled;
    }

    private void ReplaceFallbackSelection(string text, int selectionStart, int selectionEnd)
    {
        Text = text.Remove(selectionStart, selectionEnd - selectionStart);
        SetFallbackCaret(selectionStart);
    }

    private void SetFallbackCaret(int index)
    {
        CaretIndex = index;
        SelectionStart = index;
        SelectionEnd = index;
    }

    private void MoveFallbackCaret(
        int index,
        bool extendSelection,
        int selectionStart,
        int selectionEnd)
    {
        if (!extendSelection)
        {
            SetFallbackCaret(index);
            return;
        }

        var anchor = selectionStart == selectionEnd
            ? CaretIndex
            : CaretIndex <= selectionStart
                ? selectionEnd
                : selectionStart;
        CaretIndex = index;
        SelectionStart = Math.Min(anchor, index);
        SelectionEnd = Math.Max(anchor, index);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (this.GetVisualChildren().Any())
        {
            // A themed TextBox owns text, selection, and caret rendering.
            return;
        }

        var isPlaceholder = string.IsNullOrEmpty(Text);
        var displayText = isPlaceholder ? _placeholderText : Text!;
        if (!isPlaceholder && PasswordChar != '\0')
        {
            displayText = new string(PasswordChar, displayText.Length);
        }

        var brush = Foreground ?? Brushes.Black;
        var formatted = new FormattedText(
            displayText,
            CultureInfo.CurrentCulture,
            FlowDirection,
            new Typeface(FontFamily, FontStyle, FontWeight, FontStretch),
            FontSize,
            brush);

        var contentLeft = Math.Max(0, Padding.Left);
        var contentWidth = Math.Max(0, Bounds.Width - Padding.Left - Padding.Right);
        var originX = TextAlignment switch
        {
            TextAlignment.Center => contentLeft + Math.Max(0, (contentWidth - formatted.Width) / 2),
            TextAlignment.Right or TextAlignment.End => contentLeft + Math.Max(0, contentWidth - formatted.Width),
            _ when formatted.Width > contentWidth => contentLeft - (formatted.Width - contentWidth),
            _ => contentLeft
        };
        var originY = Math.Max(0, (Bounds.Height - formatted.Height) / 2);
        using var clip = context.PushClip(new Rect(
            contentLeft,
            0,
            contentWidth,
            Math.Max(0, Bounds.Height)));
        if (isPlaceholder)
        {
            if (!string.IsNullOrEmpty(displayText))
            {
                using (context.PushOpacity(0.55))
                {
                    context.DrawText(formatted, new Point(originX, originY));
                }
            }
        }
        else
        {
            context.DrawText(formatted, new Point(originX, originY));
        }
        if (IsFocused && _fallbackCaretVisible && SelectionStart == SelectionEnd)
        {
            var caretIndex = isPlaceholder ? 0 : Math.Clamp(CaretIndex, 0, displayText.Length);
            var caretPrefix = new FormattedText(
                isPlaceholder ? string.Empty : displayText[..caretIndex],
                CultureInfo.CurrentCulture,
                FlowDirection,
                new Typeface(FontFamily, FontStyle, FontWeight, FontStretch),
                FontSize,
                brush);
            var caretX = originX + caretPrefix.Width;
            context.DrawLine(
                new Pen(CaretBrush ?? brush, 1),
                new Point(caretX, originY),
                new Point(caretX, originY + formatted.Height));
        }
    }
}

public enum CssPosition
{
    Static,
    Relative,
    Absolute,
    Fixed
}

public enum CssFloat
{
    None,
    Left,
    Right
}

public enum CssDisplay
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
    ListItem
}

internal enum CssListStyleType
{
    Disc,
    Circle,
    Square,
    Decimal,
    None
}

internal enum CssListStylePosition
{
    Outside,
    Inside
}

internal sealed record CssListMarker(
    CssListStyleType Type,
    CssListStylePosition Position,
    string Text,
    IBrush Brush,
    Typeface Typeface,
    double FontSize,
    double LineHeight,
    Size Size)
{
    /// <summary>
    /// Used inline width of the marker including its marker suffix. This is
    /// deliberately distinct from <see cref="Size"/>, which is the smaller
    /// glyph paint box used by the infrastructure control.
    /// </summary>
    internal double InlineAdvance { get; } = ResolveInlineAdvance(
        Type,
        Brush,
        Typeface,
        FontSize,
        Size);

    internal double FormattingLineHeight => Math.Max(Size.Height, LineHeight);

    private static double ResolveInlineAdvance(
        CssListStyleType type,
        IBrush brush,
        Typeface typeface,
        double fontSize,
        Size size)
    {
        if (type == CssListStyleType.None)
        {
            return 0;
        }

        // A symbolic marker has an em-sized marker formatting box even though
        // the painted disc/square is much smaller. Decimal marker text already
        // carries its glyph advance in size.Width. Both are followed by the
        // resolved font's separator-space advance.
        var space = new FormattedText(
            " ",
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            typeface,
            fontSize,
            brush);
        var markerContentAdvance = type == CssListStyleType.Decimal
            ? size.Width
            : Math.Max(size.Width, fontSize);
        return markerContentAdvance + space.WidthIncludingTrailingWhitespace;
    }
}

public enum CssVerticalAlign
{
    Baseline,
    Top,
    Middle,
    Bottom
}

public enum CssBoxSizing
{
    ContentBox,
    BorderBox
}

public enum CssFlexDirection
{
    Row,
    RowReverse,
    Column,
    ColumnReverse
}

public enum CssFlexWrap
{
    NoWrap,
    Wrap,
    WrapReverse
}

public enum CssLengthUnit
{
    Pixel,
    Percent,
    Auto
}

public readonly record struct CssLength(double Value, CssLengthUnit Unit)
{
    public bool IsAuto => Unit == CssLengthUnit.Auto;

    // A percentage and an absolute offset must remain deferred until the
    // containing block is known (for example, calc(50% - 14px)).
    public double PixelOffset { get; init; }
}

/// <summary>
/// Layout metadata shared between the DOM style adapter and <see cref="CssLayoutPanel"/>.
/// Keeping percentages as percentages until arrange time is essential: their containing
/// block can change after a stylesheet mutation or a resize.
/// </summary>
public static class CssLayout
{
    public static readonly AttachedProperty<CssDisplay> DisplayProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssDisplay>("Display");
    public static readonly AttachedProperty<CssPosition> PositionProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssPosition>("Position");
    public static readonly AttachedProperty<CssFloat> FloatProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssFloat>("Float");
    public static readonly AttachedProperty<CssBoxSizing> BoxSizingProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssBoxSizing>("BoxSizing");
    public static readonly AttachedProperty<bool> FixedTableLayoutProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>("FixedTableLayout");
    public static readonly AttachedProperty<bool> NoWrapProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>("NoWrap");
    public static readonly AttachedProperty<bool> DocumentViewportRootProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>("DocumentViewportRoot");
    public static readonly AttachedProperty<bool> CssHeightIsAutoProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>("CssHeightIsAuto");
    public static readonly AttachedProperty<bool> NativeLayoutHotPathProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>(
            "NativeLayoutHotPath",
            inherits: true);
    public static readonly AttachedProperty<bool> PointerEventsNoneProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, bool>("PointerEventsNone");
    public static readonly AttachedProperty<double?> LineHeightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, double?>("LineHeight");
    public static readonly AttachedProperty<CssVerticalAlign> VerticalAlignProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssVerticalAlign>("VerticalAlign");
    internal static readonly AttachedProperty<double[]?> TableColumnWidthsProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, double[]?>("TableColumnWidths");

    public static readonly AttachedProperty<CssLength?> LeftProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Left");
    public static readonly AttachedProperty<CssLength?> TopProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Top");
    public static readonly AttachedProperty<CssLength?> RightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Right");
    public static readonly AttachedProperty<CssLength?> BottomProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Bottom");
    public static readonly AttachedProperty<CssLength?> WidthProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Width");
    public static readonly AttachedProperty<CssLength?> HeightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("Height");
    public static readonly AttachedProperty<CssLength?> MinWidthProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MinWidth");
    public static readonly AttachedProperty<CssLength?> MinHeightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MinHeight");
    public static readonly AttachedProperty<CssLength?> MaxWidthProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MaxWidth");
    public static readonly AttachedProperty<CssLength?> MaxHeightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MaxHeight");

    public static readonly AttachedProperty<double> FlexGrowProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, double>("FlexGrow");
    public static readonly AttachedProperty<double> FlexShrinkProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, double>("FlexShrink", defaultValue: 1);
    public static readonly AttachedProperty<CssLength?> FlexBasisProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("FlexBasis");
    public static readonly AttachedProperty<CssFlexDirection> FlexDirectionProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssFlexDirection>("FlexDirection");
    public static readonly AttachedProperty<CssFlexWrap> FlexWrapProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssFlexWrap>("FlexWrap");
    public static readonly AttachedProperty<string?> JustifyContentProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("JustifyContent");
    public static readonly AttachedProperty<string?> AlignContentProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("AlignContent");
    public static readonly AttachedProperty<string?> AlignItemsProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("AlignItems");
    public static readonly AttachedProperty<string?> AlignSelfProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("AlignSelf");
    public static readonly AttachedProperty<int> OrderProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, int>("Order");
    public static readonly AttachedProperty<CssLength?> RowGapProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("RowGap");
    public static readonly AttachedProperty<CssLength?> ColumnGapProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("ColumnGap");
    public static readonly AttachedProperty<string?> GridTemplateColumnsProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("GridTemplateColumns");
    public static readonly AttachedProperty<string?> GridTemplateRowsProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("GridTemplateRows");
    public static readonly AttachedProperty<string?> GridColumnProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, string?>("GridColumn");

    public static readonly AttachedProperty<CssLength?> MarginTopProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MarginTop");
    public static readonly AttachedProperty<CssLength?> MarginRightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MarginRight");
    public static readonly AttachedProperty<CssLength?> MarginBottomProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MarginBottom");
    public static readonly AttachedProperty<CssLength?> MarginLeftProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("MarginLeft");
    public static readonly AttachedProperty<CssLength?> PaddingTopProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("PaddingTop");
    public static readonly AttachedProperty<CssLength?> PaddingRightProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("PaddingRight");
    public static readonly AttachedProperty<CssLength?> PaddingBottomProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("PaddingBottom");
    public static readonly AttachedProperty<CssLength?> PaddingLeftProperty =
        AvaloniaProperty.RegisterAttached<CssLayoutPanel, Control, CssLength?>("PaddingLeft");

    public static CssDisplay GetDisplay(Control control) => control.GetValue(DisplayProperty);
    public static void SetDisplay(Control control, CssDisplay value) => control.SetValue(DisplayProperty, value);
    public static CssPosition GetPosition(Control control) => control.GetValue(PositionProperty);
    public static void SetPosition(Control control, CssPosition value) => control.SetValue(PositionProperty, value);
    public static CssFloat GetFloat(Control control) => control.GetValue(FloatProperty);
    public static void SetFloat(Control control, CssFloat value) => control.SetValue(FloatProperty, value);
    public static CssBoxSizing GetBoxSizing(Control control) => control.GetValue(BoxSizingProperty);
    public static void SetBoxSizing(Control control, CssBoxSizing value) => control.SetValue(BoxSizingProperty, value);
    public static bool GetFixedTableLayout(Control control) => control.GetValue(FixedTableLayoutProperty);
    public static void SetFixedTableLayout(Control control, bool value) => control.SetValue(FixedTableLayoutProperty, value);
    public static bool GetNoWrap(Control control) => control.GetValue(NoWrapProperty);
    public static void SetNoWrap(Control control, bool value) => control.SetValue(NoWrapProperty, value);
    public static bool GetDocumentViewportRoot(Control control) => control.GetValue(DocumentViewportRootProperty);
    public static void SetDocumentViewportRoot(Control control, bool value) => control.SetValue(DocumentViewportRootProperty, value);
    public static bool GetCssHeightIsAuto(Control control) => control.GetValue(CssHeightIsAutoProperty);
    public static void SetCssHeightIsAuto(Control control, bool value) => control.SetValue(CssHeightIsAutoProperty, value);
    public static bool GetNativeLayoutHotPath(Control control) => control.GetValue(NativeLayoutHotPathProperty);
    public static void SetNativeLayoutHotPath(Control control, bool value) => control.SetValue(NativeLayoutHotPathProperty, value);
    public static bool GetPointerEventsNone(Control control) => control.GetValue(PointerEventsNoneProperty);
    public static void SetPointerEventsNone(Control control, bool value) => control.SetValue(PointerEventsNoneProperty, value);
    public static double? GetLineHeight(Control control) => control.GetValue(LineHeightProperty);
    public static void SetLineHeight(Control control, double? value) => control.SetValue(LineHeightProperty, value);
    public static CssVerticalAlign GetVerticalAlign(Control control) => control.GetValue(VerticalAlignProperty);
    public static void SetVerticalAlign(Control control, CssVerticalAlign value) => control.SetValue(VerticalAlignProperty, value);
    internal static double[]? GetTableColumnWidths(Control control) => control.GetValue(TableColumnWidthsProperty);
    internal static void SetTableColumnWidths(Control control, double[]? value) => control.SetValue(TableColumnWidthsProperty, value);

    public static CssLength? GetLeft(Control control) => control.GetValue(LeftProperty);
    public static void SetLeft(Control control, CssLength? value) => control.SetValue(LeftProperty, value);
    public static CssLength? GetTop(Control control) => control.GetValue(TopProperty);
    public static void SetTop(Control control, CssLength? value) => control.SetValue(TopProperty, value);
    public static CssLength? GetRight(Control control) => control.GetValue(RightProperty);
    public static void SetRight(Control control, CssLength? value) => control.SetValue(RightProperty, value);
    public static CssLength? GetBottom(Control control) => control.GetValue(BottomProperty);
    public static void SetBottom(Control control, CssLength? value) => control.SetValue(BottomProperty, value);
    public static CssLength? GetWidth(Control control) => control.GetValue(WidthProperty);
    public static void SetWidth(Control control, CssLength? value) => control.SetValue(WidthProperty, value);
    public static CssLength? GetHeight(Control control) => control.GetValue(HeightProperty);
    public static void SetHeight(Control control, CssLength? value) => control.SetValue(HeightProperty, value);
    public static CssLength? GetMinWidth(Control control) => control.GetValue(MinWidthProperty);
    public static void SetMinWidth(Control control, CssLength? value) => control.SetValue(MinWidthProperty, value);
    public static CssLength? GetMinHeight(Control control) => control.GetValue(MinHeightProperty);
    public static void SetMinHeight(Control control, CssLength? value) => control.SetValue(MinHeightProperty, value);
    public static CssLength? GetMaxWidth(Control control) => control.GetValue(MaxWidthProperty);
    public static void SetMaxWidth(Control control, CssLength? value) => control.SetValue(MaxWidthProperty, value);
    public static CssLength? GetMaxHeight(Control control) => control.GetValue(MaxHeightProperty);
    public static void SetMaxHeight(Control control, CssLength? value) => control.SetValue(MaxHeightProperty, value);

    public static double GetFlexGrow(Control control) => control.GetValue(FlexGrowProperty);
    public static void SetFlexGrow(Control control, double value) => control.SetValue(FlexGrowProperty, value);
    public static double GetFlexShrink(Control control) => control.GetValue(FlexShrinkProperty);
    public static void SetFlexShrink(Control control, double value) => control.SetValue(FlexShrinkProperty, value);
    public static CssLength? GetFlexBasis(Control control) => control.GetValue(FlexBasisProperty);
    public static void SetFlexBasis(Control control, CssLength? value) => control.SetValue(FlexBasisProperty, value);
    public static CssFlexDirection GetFlexDirection(Control control) => control.GetValue(FlexDirectionProperty);
    public static void SetFlexDirection(Control control, CssFlexDirection value) => control.SetValue(FlexDirectionProperty, value);
    public static CssFlexWrap GetFlexWrap(Control control) => control.GetValue(FlexWrapProperty);
    public static void SetFlexWrap(Control control, CssFlexWrap value) => control.SetValue(FlexWrapProperty, value);
    public static string? GetJustifyContent(Control control) => control.GetValue(JustifyContentProperty);
    public static void SetJustifyContent(Control control, string? value) => control.SetValue(JustifyContentProperty, value);
    public static string? GetAlignContent(Control control) => control.GetValue(AlignContentProperty);
    public static void SetAlignContent(Control control, string? value) => control.SetValue(AlignContentProperty, value);
    public static string? GetAlignItems(Control control) => control.GetValue(AlignItemsProperty);
    public static void SetAlignItems(Control control, string? value) => control.SetValue(AlignItemsProperty, value);
    public static string? GetAlignSelf(Control control) => control.GetValue(AlignSelfProperty);
    public static void SetAlignSelf(Control control, string? value) => control.SetValue(AlignSelfProperty, value);
    public static int GetOrder(Control control) => control.GetValue(OrderProperty);
    public static void SetOrder(Control control, int value) => control.SetValue(OrderProperty, value);
    public static CssLength? GetRowGap(Control control) => control.GetValue(RowGapProperty);
    public static void SetRowGap(Control control, CssLength? value) => control.SetValue(RowGapProperty, value);
    public static CssLength? GetColumnGap(Control control) => control.GetValue(ColumnGapProperty);
    public static void SetColumnGap(Control control, CssLength? value) => control.SetValue(ColumnGapProperty, value);
    public static string? GetGridTemplateColumns(Control control) => control.GetValue(GridTemplateColumnsProperty);
    public static void SetGridTemplateColumns(Control control, string? value) => control.SetValue(GridTemplateColumnsProperty, value);
    public static string? GetGridTemplateRows(Control control) => control.GetValue(GridTemplateRowsProperty);
    public static void SetGridTemplateRows(Control control, string? value) => control.SetValue(GridTemplateRowsProperty, value);
    public static string? GetGridColumn(Control control) => control.GetValue(GridColumnProperty);
    public static void SetGridColumn(Control control, string? value) => control.SetValue(GridColumnProperty, value);

    public static CssLength? GetMarginTop(Control control) => control.GetValue(MarginTopProperty);
    public static void SetMarginTop(Control control, CssLength? value) => control.SetValue(MarginTopProperty, value);
    public static CssLength? GetMarginRight(Control control) => control.GetValue(MarginRightProperty);
    public static void SetMarginRight(Control control, CssLength? value) => control.SetValue(MarginRightProperty, value);
    public static CssLength? GetMarginBottom(Control control) => control.GetValue(MarginBottomProperty);
    public static void SetMarginBottom(Control control, CssLength? value) => control.SetValue(MarginBottomProperty, value);
    public static CssLength? GetMarginLeft(Control control) => control.GetValue(MarginLeftProperty);
    public static void SetMarginLeft(Control control, CssLength? value) => control.SetValue(MarginLeftProperty, value);
    public static CssLength? GetPaddingTop(Control control) => control.GetValue(PaddingTopProperty);
    public static void SetPaddingTop(Control control, CssLength? value) => control.SetValue(PaddingTopProperty, value);
    public static CssLength? GetPaddingRight(Control control) => control.GetValue(PaddingRightProperty);
    public static void SetPaddingRight(Control control, CssLength? value) => control.SetValue(PaddingRightProperty, value);
    public static CssLength? GetPaddingBottom(Control control) => control.GetValue(PaddingBottomProperty);
    public static void SetPaddingBottom(Control control, CssLength? value) => control.SetValue(PaddingBottomProperty, value);
    public static CssLength? GetPaddingLeft(Control control) => control.GetValue(PaddingLeftProperty);
    public static void SetPaddingLeft(Control control, CssLength? value) => control.SetValue(PaddingLeftProperty, value);

    public static CssPosition ParsePosition(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("relative", StringComparison.OrdinalIgnoreCase)) return CssPosition.Relative;
        if (normalized.Equals("absolute", StringComparison.OrdinalIgnoreCase)) return CssPosition.Absolute;
        if (normalized.Equals("fixed", StringComparison.OrdinalIgnoreCase)) return CssPosition.Fixed;
        return CssPosition.Static;
    }

    public static CssFloat ParseFloat(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("left", StringComparison.OrdinalIgnoreCase)) return CssFloat.Left;
        if (normalized.Equals("right", StringComparison.OrdinalIgnoreCase)) return CssFloat.Right;
        return CssFloat.None;
    }

    public static CssDisplay ParseDisplay(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("flex", StringComparison.OrdinalIgnoreCase)) return CssDisplay.Flex;
        if (normalized.Equals("inline-flex", StringComparison.OrdinalIgnoreCase)) return CssDisplay.InlineFlex;
        if (normalized.Equals("grid", StringComparison.OrdinalIgnoreCase)) return CssDisplay.Grid;
        if (normalized.Equals("inline-grid", StringComparison.OrdinalIgnoreCase)) return CssDisplay.InlineGrid;
        if (normalized.Equals("table", StringComparison.OrdinalIgnoreCase)) return CssDisplay.Table;
        if (normalized.Equals("inline-table", StringComparison.OrdinalIgnoreCase)) return CssDisplay.InlineTable;
        if (normalized.Equals("table-row-group", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableRowGroup;
        if (normalized.Equals("table-header-group", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableHeaderGroup;
        if (normalized.Equals("table-footer-group", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableFooterGroup;
        if (normalized.Equals("table-row", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableRow;
        if (normalized.Equals("table-cell", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableCell;
        if (normalized.Equals("table-column-group", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableColumnGroup;
        if (normalized.Equals("table-column", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableColumn;
        if (normalized.Equals("table-caption", StringComparison.OrdinalIgnoreCase)) return CssDisplay.TableCaption;
        if (normalized.Equals("list-item", StringComparison.OrdinalIgnoreCase)) return CssDisplay.ListItem;
        if (normalized.Equals("inline", StringComparison.OrdinalIgnoreCase)) return CssDisplay.Inline;
        if (normalized.Equals("inline-block", StringComparison.OrdinalIgnoreCase)) return CssDisplay.InlineBlock;
        return CssDisplay.Block;
    }

    public static CssVerticalAlign ParseVerticalAlign(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("top", StringComparison.OrdinalIgnoreCase)) return CssVerticalAlign.Top;
        if (normalized.Equals("middle", StringComparison.OrdinalIgnoreCase)) return CssVerticalAlign.Middle;
        if (normalized.Equals("bottom", StringComparison.OrdinalIgnoreCase)) return CssVerticalAlign.Bottom;
        return CssVerticalAlign.Baseline;
    }

    public static CssBoxSizing ParseBoxSizing(string? value)
        => value.AsSpan().Trim().Equals("border-box", StringComparison.OrdinalIgnoreCase)
            ? CssBoxSizing.BorderBox
            : CssBoxSizing.ContentBox;

    public static CssFlexDirection ParseFlexDirection(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("row-reverse", StringComparison.OrdinalIgnoreCase)) return CssFlexDirection.RowReverse;
        if (normalized.Equals("column", StringComparison.OrdinalIgnoreCase)) return CssFlexDirection.Column;
        if (normalized.Equals("column-reverse", StringComparison.OrdinalIgnoreCase)) return CssFlexDirection.ColumnReverse;
        return CssFlexDirection.Row;
    }

    public static CssFlexWrap ParseFlexWrap(string? value)
    {
        var normalized = value.AsSpan().Trim();
        if (normalized.Equals("wrap-reverse", StringComparison.OrdinalIgnoreCase)) return CssFlexWrap.WrapReverse;
        if (normalized.Equals("wrap", StringComparison.OrdinalIgnoreCase)) return CssFlexWrap.Wrap;
        return CssFlexWrap.NoWrap;
    }

    public static bool TryParseLength(string? value, out CssLength? length)
    {
        if (!CssLengthParser.TryParse(value, out var portable))
        {
            length = null;
            return false;
        }

        length = portable is null
            ? null
            : new CssLength(
                portable.Value.Value,
                portable.Value.Unit switch
                {
                    CssLayoutLengthUnit.Percent => CssLengthUnit.Percent,
                    CssLayoutLengthUnit.Auto => CssLengthUnit.Auto,
                    _ => CssLengthUnit.Pixel
                })
            {
                PixelOffset = portable.Value.PixelOffset
            };
        return true;
    }

    public static bool TryParseAbsoluteLength(string? value, out double pixels)
        => CssLengthParser.TryParseAbsoluteLength(value, out pixels);

    public static double? Resolve(CssLength? length, double reference)
    {
        if (!length.HasValue || length.Value.IsAuto)
        {
            return null;
        }

        return length.Value.Unit == CssLengthUnit.Percent
            ? reference * length.Value.Value / 100d + length.Value.PixelOffset
            : length.Value.Value;
    }

    public static void SetOverflow(Control control, string? overflowX, string? overflowY)
    {
        if (control is CssLayoutPanel panel)
        {
            panel.SetOverflow(overflowX, overflowY);
        }
    }

    public static void InvalidateParent(Control control)
    {
        // A CSS length mutation can change the size of every containing
        // layout, not just the immediately enclosing panel.  Invalidating the
        // complete ancestor chain keeps incremental style recomputation and
        // layout invalidation in agreement, especially when the element is
        // nested inside multiple CSS layout panels.
        for (var parent = control.Parent; parent is not null; parent = parent.Parent)
        {
            if (parent is CssLayoutPanel cssParent)
            {
                cssParent.InvalidateMeasure();
                cssParent.InvalidateArrange();
            }
        }
    }
}

using System;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using SkiaSharp;
using Svg.Skia;
using WebScene.Core;
using WebScene.Graphics;

namespace JavaScript.Avalonia;

/// <summary>
/// Lightweight retained SVG viewport for DOM-authored icon geometry. It is
/// intentionally focused on the SVG primitives used by application chrome;
/// the browser DOM remains the owner of structure, attributes, and styling.
/// </summary>
internal sealed class SvgLayoutPanel : Panel, IDisposable
{
    private string? _compiledMarkup;
    private SvgSkiaResource? _skiaResource;
    private Func<string>? _skiaMarkupProvider;
    private bool _markupDirty = true;
    private readonly SvgSkiaSurface _skiaSurface;
    private readonly AvaloniaSvgSceneSurface _sceneSurface;
    private Func<SvgScene>? _sceneProvider;

    public SvgLayoutPanel()
    {
        _skiaSurface = new SvgSkiaSurface(this);
        _sceneSurface = new AvaloniaSvgSceneSurface { IsHitTestVisible = false, Focusable = false };
        Children.Add(_sceneSurface);
        Children.Add(_skiaSurface);
    }

    internal Func<SvgScene>? SceneProvider
    {
        get => _sceneProvider;
        set
        {
            _sceneProvider = value;
            _sceneSurface.SceneProvider = value;
        }
    }

    internal int SceneBuildCount => _sceneSurface.SceneBuildCount;

    internal Func<string>? SkiaMarkupProvider
    {
        get => _skiaMarkupProvider;
        set
        {
            _skiaMarkupProvider = value;
            _markupDirty = true;
        }
    }

    internal int MarkupSerializationCount { get; private set; }

    internal int CompilationCount { get; private set; }

    public Rect? ViewBox { get; set; }

    public bool StretchViewBox { get; set; }

    public IBrush? CurrentColor { get; set; }

    public IBrush? Fill { get; set; }

    public IBrush? Stroke { get; set; }

    public bool FillUsesCurrentColor { get; set; }

    public bool StrokeUsesCurrentColor { get; set; }

    public bool SuppressFill { get; set; }

    protected override Size MeasureOverride(Size availableSize)
    {
        var natural = ViewBox?.Size ?? new Size(0, 0);
        var constraint = new Size(
            double.IsFinite(availableSize.Width) ? availableSize.Width : natural.Width,
            double.IsFinite(availableSize.Height) ? availableSize.Height : natural.Height);
        foreach (var child in Children)
        {
            if (ReferenceEquals(child, _skiaSurface) || ReferenceEquals(child, _sceneSurface)) continue;
            child.Measure(constraint);
        }

        return natural;
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        foreach (var child in Children)
        {
            if (ReferenceEquals(child, _skiaSurface) || ReferenceEquals(child, _sceneSurface)) continue;
            child.Arrange(new Rect(finalSize));
        }

        _sceneSurface.Measure(finalSize);
        _sceneSurface.Arrange(new Rect(finalSize));
        _skiaSurface.Measure(finalSize);
        _skiaSurface.Arrange(new Rect(finalSize));

        return finalSize;
    }

    internal void RenderSkia(DrawingContext context)
    {
        if (_skiaMarkupProvider is null)
        {
            return;
        }

        if (_markupDirty || _skiaResource is null)
        {
            var markup = _skiaMarkupProvider();
            MarkupSerializationCount++;
            _markupDirty = false;
            if (!string.Equals(markup, _compiledMarkup, StringComparison.Ordinal))
            {
                SKSvg? next = new SKSvg();
                try
                {
                    if (next.FromSvg(markup) is not null)
                    {
                        var nextResource = new SvgSkiaResource(next);
                        next = null;
                        var previous = _skiaResource;
                        _skiaResource = nextResource;
                        _compiledMarkup = markup;
                        CompilationCount++;
                        previous?.Release();
                    }
                }
                finally
                {
                    next?.Dispose();
                }
            }
        }

        if (_skiaResource?.Svg.Picture is { } picture)
        {
            // Svg.Skia's picture cull rectangle can be the tight bounds of the
            // painted paths rather than the SVG viewport. Scaling that tight
            // rectangle to the control bounds enlarges and shifts sparse icons
            // such as compact undo/redo arrows. The browser scales the
            // declared viewBox, so retain that viewport whenever it is known.
            var source = ViewBox is { } viewBox
                ? new SKRect(
                    (float)viewBox.Left,
                    (float)viewBox.Top,
                    (float)viewBox.Right,
                    (float)viewBox.Bottom)
                : picture.CullRect;
            context.Custom(new SvgSkiaDrawOperation(new Rect(Bounds.Size), _skiaResource, source));
        }
    }

    public void Dispose()
    {
        _skiaResource?.Release();
        _skiaResource = null;
    }

    internal void InvalidateSkiaVisual()
    {
        _markupDirty = true;
        _skiaSurface.InvalidateVisual();
    }

    internal void InvalidateSceneVisual()
    {
        InvalidateVisual();
        _sceneSurface.InvalidateScene();
    }

    internal Matrix GetViewBoxTransform(Size viewport)
    {
        var source = ViewBox ?? new Rect(viewport);
        if (source.Width <= 0 || source.Height <= 0 || viewport.Width <= 0 || viewport.Height <= 0)
        {
            return Matrix.Identity;
        }

        var scaleX = viewport.Width / source.Width;
        var scaleY = viewport.Height / source.Height;
        var offsetX = 0d;
        var offsetY = 0d;
        if (!StretchViewBox)
        {
            var scale = Math.Min(scaleX, scaleY);
            scaleX = scaleY = scale;
            offsetX = (viewport.Width - source.Width * scale) / 2d;
            offsetY = (viewport.Height - source.Height * scale) / 2d;
        }

        return new Matrix(
            scaleX, 0,
            0, scaleY,
            offsetX - source.X * scaleX,
            offsetY - source.Y * scaleY);
    }
}

internal sealed class SvgSkiaSurface : Control
{
    private readonly SvgLayoutPanel _owner;

    public SvgSkiaSurface(SvgLayoutPanel owner)
    {
        _owner = owner;
        IsHitTestVisible = false;
        Focusable = false;
    }

    public override void Render(DrawingContext context) => _owner.RenderSkia(context);
}

internal abstract class SvgGeometryControl : Control
{
    public IBrush? Fill { get; set; }

    public IBrush? Stroke { get; set; }

    public double StrokeThickness { get; set; } = 1d;

    public bool FillUsesCurrentColor { get; set; }

    public bool StrokeUsesCurrentColor { get; set; }

    public bool SuppressFill { get; set; }

    protected bool HasSkiaRoot()
    {
        var current = Parent as Control;
        while (current is not null)
        {
            if (current is SvgLayoutPanel svg
                && (svg.SkiaMarkupProvider is not null || svg.SceneProvider is not null)) return true;
            current = current.Parent as Control;
        }

        return false;
    }

    protected (IBrush? Fill, Pen? Pen) ResolvePaint()
    {
        var currentColor = FindCurrentColor();
        var fill = FillUsesCurrentColor
            ? currentColor
            : SuppressFill ? null : Fill ?? FindInheritedFill(currentColor);
        var stroke = StrokeUsesCurrentColor
            ? currentColor
            : Stroke ?? FindInheritedStroke(currentColor);
        return (fill, stroke is null ? null : new Pen(stroke, Math.Max(0, StrokeThickness)));
    }

    protected Matrix ResolveTransform()
    {
        var current = Parent as Control;
        while (current is not null)
        {
            if (current is SvgLayoutPanel { ViewBox: not null } svg)
            {
                return svg.GetViewBoxTransform(Bounds.Size);
            }

            current = current.Parent as Control;
        }

        return Matrix.Identity;
    }

    private IBrush FindCurrentColor()
    {
        var current = Parent as Control;
        while (current is not null)
        {
            if (current is SvgLayoutPanel { CurrentColor: not null } svg)
            {
                return svg.CurrentColor;
            }

            current = current.Parent as Control;
        }

        return Brushes.Black;
    }

    private IBrush? FindInheritedFill(IBrush currentColor)
    {
        var current = Parent as Control;
        while (current is not null)
        {
            if (current is SvgLayoutPanel svg)
            {
                if (svg.SuppressFill) return null;
                if (svg.FillUsesCurrentColor) return currentColor;
                if (svg.Fill is not null) return svg.Fill;
            }

            current = current.Parent as Control;
        }

        return Brushes.Black;
    }

    private IBrush? FindInheritedStroke(IBrush currentColor)
    {
        var current = Parent as Control;
        while (current is not null)
        {
            if (current is SvgLayoutPanel svg)
            {
                if (svg.StrokeUsesCurrentColor) return currentColor;
                if (svg.Stroke is not null) return svg.Stroke;
            }

            current = current.Parent as Control;
        }

        return null;
    }
}

internal sealed class SvgPathControl : SvgGeometryControl
{
    public Geometry? Data { get; set; }

    public override void Render(DrawingContext context)
    {
        if (HasSkiaRoot()) return;
        if (Data is null)
        {
            return;
        }

        var (fill, pen) = ResolvePaint();
        using (context.PushTransform(ResolveTransform()))
        {
            context.DrawGeometry(fill, pen, Data);
        }
    }
}

internal sealed class SvgCircleControl : SvgGeometryControl
{
    public double CenterX { get; set; }

    public double CenterY { get; set; }

    public double Radius { get; set; }

    public override void Render(DrawingContext context)
    {
        if (HasSkiaRoot()) return;
        if (Radius <= 0)
        {
            return;
        }

        var (fill, pen) = ResolvePaint();
        using (context.PushTransform(ResolveTransform()))
        {
            context.DrawEllipse(fill, pen, new Point(CenterX, CenterY), Radius, Radius);
        }
    }
}

internal sealed class SvgSkiaDrawOperation : ICustomDrawOperation
{
    private SvgSkiaResource? _resource;
    private readonly SKRect _source;

    public SvgSkiaDrawOperation(Rect bounds, SvgSkiaResource resource, SKRect source)
    {
        Bounds = bounds;
        _resource = resource.Acquire();
        _source = source;
    }

    public Rect Bounds { get; }

    public bool HitTest(Point p) => false;

    public bool Equals(ICustomDrawOperation? other) => false;

    public void Dispose()
    {
        Interlocked.Exchange(ref _resource, null)?.Release();
    }

    public void Render(ImmediateDrawingContext context)
    {
        var resource = _resource;
        if (resource is null
            || context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature)) is not ISkiaSharpApiLeaseFeature feature
            || _source.Width <= 0 || _source.Height <= 0)
        {
            return;
        }

        using var lease = feature.Lease();
        var canvas = lease.SkCanvas;
        canvas.Save();
        try
        {
            var scale = Math.Min(Bounds.Width / _source.Width, Bounds.Height / _source.Height);
            var x = (Bounds.Width - _source.Width * scale) / 2d;
            var y = (Bounds.Height - _source.Height * scale) / 2d;
            canvas.Translate((float)x, (float)y);
            canvas.Scale((float)scale);
            canvas.Translate(-_source.Left, -_source.Top);
            resource.Svg.Draw(canvas);
        }
        finally
        {
            canvas.Restore();
        }
    }
}

internal sealed class SvgSkiaResource
{
    private int _references = 1;

    public SvgSkiaResource(SKSvg svg) => Svg = svg;

    public SKSvg Svg { get; }

    internal bool IsDisposed { get; private set; }

    public SvgSkiaResource Acquire()
    {
        while (true)
        {
            var references = Volatile.Read(ref _references);
            if (references <= 0)
            {
                throw new ObjectDisposedException(nameof(SvgSkiaResource));
            }
            if (Interlocked.CompareExchange(ref _references, references + 1, references) == references)
            {
                return this;
            }
        }
    }

    public void Release()
    {
        if (Interlocked.Decrement(ref _references) != 0)
        {
            return;
        }

        Svg.Dispose();
        IsDisposed = true;
    }
}

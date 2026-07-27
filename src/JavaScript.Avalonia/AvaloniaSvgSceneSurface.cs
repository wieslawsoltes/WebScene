using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using WebScene.Core;

namespace JavaScript.Avalonia;

/// <summary>Avalonia replay adapter for the portable retained SVG scene model.</summary>
internal sealed class AvaloniaSvgSceneSurface : Control, IWebSceneSvgSceneRenderer, IDomInfrastructureControl
{
    private SvgScene? _scene;
    private Func<SvgScene>? _sceneProvider;
    private bool _sceneDirty;

    internal Func<SvgScene>? SceneProvider
    {
        get => _sceneProvider;
        set
        {
            _sceneProvider = value;
            _sceneDirty = true;
            InvalidateVisual();
        }
    }

    internal int SceneBuildCount { get; private set; }

    internal void InvalidateScene()
    {
        _sceneDirty = true;
        InvalidateVisual();
    }

    public void Render(SvgScene scene, WebSceneSize surfaceSize)
    {
        _scene = scene ?? throw new ArgumentNullException(nameof(scene));
        Width = surfaceSize.Width;
        Height = surfaceSize.Height;
        InvalidateVisual();
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        if (_sceneProvider is not null && _sceneDirty)
        {
            _scene = _sceneProvider();
            SceneBuildCount++;
            _sceneDirty = false;
        }

        if (_scene is not null)
        {
            var viewBox = _scene.ViewBox;
            var viewport = Bounds.Size;
            if (viewBox.Width <= 0 || viewBox.Height <= 0 || viewport.Width <= 0 || viewport.Height <= 0)
            {
                return;
            }

            var scaleX = viewport.Width / viewBox.Width;
            var scaleY = viewport.Height / viewBox.Height;
            var offsetX = 0d;
            var offsetY = 0d;
            if (!_scene.StretchViewBox)
            {
                var scale = Math.Min(scaleX, scaleY);
                scaleX = scaleY = scale;
                offsetX = (viewport.Width - viewBox.Width * scale) / 2d;
                offsetY = (viewport.Height - viewBox.Height * scale) / 2d;
            }

            using (context.PushTransform(new Matrix(
                       scaleX,
                       0,
                       0,
                       scaleY,
                       offsetX - viewBox.X * scaleX,
                       offsetY - viewBox.Y * scaleY)))
            {
                RenderNode(context, _scene.Root);
            }
        }
    }

    private static void RenderNode(DrawingContext context, SvgSceneNode node)
    {
        var transform = new Matrix(
            node.Transform.M11,
            node.Transform.M12,
            node.Transform.M21,
            node.Transform.M22,
            node.Transform.M31,
            node.Transform.M32);
        IDisposable? transformScope = transform.IsIdentity ? null : context.PushTransform(transform);
        using (transformScope)
        {

            var fillBrush = ProjectPaint(node.Fill);
            var strokeBrush = ProjectPaint(node.Stroke);
            var pen = strokeBrush is null || node.StrokeWidth <= 0
                ? null
                : new Pen(strokeBrush, node.StrokeWidth);
            var bounds = new Rect(node.Bounds.X, node.Bounds.Y, node.Bounds.Width, node.Bounds.Height);
            switch (node.Kind)
            {
            case SvgSceneNodeKind.Circle:
                context.DrawEllipse(
                    fillBrush,
                    pen,
                    bounds.Center,
                    bounds.Width / 2,
                    bounds.Height / 2);
                break;
            case SvgSceneNodeKind.Rectangle:
                context.DrawRectangle(fillBrush, pen, bounds);
                break;
            case SvgSceneNodeKind.Path when !string.IsNullOrWhiteSpace(node.PathData):
                try
                {
                    context.DrawGeometry(fillBrush, pen, Geometry.Parse(node.PathData));
                }
                catch
                {
                    // Invalid SVG path data is ignored by browser renderers.
                }
                break;
            case SvgSceneNodeKind.Text when !string.IsNullOrEmpty(node.Text):
                var text = new FormattedText(
                    node.Text,
                    System.Globalization.CultureInfo.CurrentCulture,
                    FlowDirection.LeftToRight,
                    Typeface.Default,
                    Math.Max(1, bounds.Height),
                    fillBrush);
                context.DrawText(text, bounds.TopLeft);
                break;
            case SvgSceneNodeKind.Image when node.Resource.TryGet<IImage>(out var image):
                var sourceSize = image!.Size;
                if (sourceSize.Width > 0 && sourceSize.Height > 0
                    && bounds.Width > 0 && bounds.Height > 0)
                {
                    context.DrawImage(image, new Rect(sourceSize), bounds);
                }
                break;
            }

            foreach (var child in node.Children)
            {
                RenderNode(context, child);
            }
        }
    }

    private static IBrush? ProjectPaint(SvgPaint? paint)
    {
        if (paint is null)
        {
            return null;
        }

        var color = paint.Color;
        return new SolidColorBrush(Color.FromArgb(
            (byte)Math.Round(color.A * Math.Clamp(paint.Opacity, 0, 1)),
            color.R,
            color.G,
            color.B));
    }
}

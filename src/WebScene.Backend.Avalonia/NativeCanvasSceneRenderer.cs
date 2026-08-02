using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed unsafe class NativeCanvasSceneRenderer
{
    private const uint CanvasCommandEvenOdd = 1u << 16;
    private const uint SceneCheckpoint = 1;
    private const uint SceneDomReplacement = 2;
    private const uint LayerReplace = 1;
    private const uint LayerRemove = 2;

    private readonly Dictionary<uint, RetainedLayer> s_layers = new();
    private readonly List<RetainedLayer> s_orderedLayers = [];
    private readonly Dictionary<StringKey, string> s_strings = new();
    private readonly Dictionary<string, SKTypeface> s_typefaces = new(StringComparer.Ordinal);
    private readonly Dictionary<string, SharedSvgPictureLease> s_svgPictures =
        new(StringComparer.Ordinal);
    private NativeTextShaping.WebTypefaceRegistry? _webTypefaces;
    private SKPicture? s_domBackdropPicture;
    private SKPicture? s_domOverlayPicture;
    private uint s_domCommandCount;
    private ulong s_revision;
    private long s_totalCommandCount;
    public static long RejectedDiffCount;

    public long TotalCommandCount => s_totalCommandCount;

    internal void SetWebTypefaceRegistry(
        NativeTextShaping.WebTypefaceRegistry? registry)
        => _webTypefaces = registry;

    internal NativeRendererMemoryMetrics ReadMemoryMetrics()
    {
        ulong logicalBitmapBytes = 0;
        ulong isolationBitmapBytes = 0;
        var isolationLayerCount = 0;
        foreach (var layer in s_layers.Values)
        {
            var layerBytes = checked((ulong)layer.BitmapWidth * layer.BitmapHeight * 4U);
            logicalBitmapBytes += layerBytes;
            if (layer.RequiresIsolation)
            {
                isolationLayerCount++;
                isolationBitmapBytes += layerBytes;
            }
        }
        ulong stringBytes = 0;
        foreach (var value in s_strings.Values)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }
        foreach (var value in s_typefaces.Keys)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }
        foreach (var value in s_svgPictures.Keys)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }

        return new NativeRendererMemoryMetrics(
            s_layers.Count,
            s_totalCommandCount,
            logicalBitmapBytes,
            isolationLayerCount,
            isolationBitmapBytes,
            s_domCommandCount,
            s_strings.Count,
            stringBytes,
            s_typefaces.Count,
            s_svgPictures.Count,
            SharedSvgPictureCache.EntryCount,
            SharedSvgPictureCache.ReferenceCount,
            SharedSvgPictureCache.MemoryHitCount);
    }

    public bool ApplyDiffAndRender(SKCanvas canvas, NativeSceneView* view)
    {
        if (!ApplyDiff(view))
        {
            return false;
        }
        RenderRetained(
            canvas,
            view->Header.ViewportWidth,
            view->Header.ViewportHeight,
            null);
        return true;
    }

    public bool ApplyDiff(NativeSceneView* view)
    {
        var header = view->Header;
        var checkpoint = (header.Flags & SceneCheckpoint) != 0;
        if (checkpoint)
        {
            Reset();
        }
        else if (header.Revision != s_revision && header.BaseRevision != s_revision)
        {
            Interlocked.Increment(ref RejectedDiffCount);
            return false;
        }

        if (header.Revision != s_revision)
        {
            if ((header.Flags & SceneDomReplacement) != 0)
            {
                s_domBackdropPicture?.Dispose();
                s_domOverlayPicture?.Dispose();
                s_domBackdropPicture = CompileDom(view, foreground: false);
                s_domOverlayPicture = CompileDom(view, foreground: true);
                s_domCommandCount = header.CommandCount;
            }

            var layerOrderChanged = false;
            var changes = new ReadOnlySpan<NativeCanvasLayer>(
                view->CanvasLayers,
                checked((int)header.CanvasLayerCount));
            foreach (ref readonly var change in changes)
            {
                if ((change.Flags & LayerRemove) != 0)
                {
                    if (s_layers.Remove(change.NodeId, out var removed))
                    {
                        s_totalCommandCount -= removed.CommandCount;
                        removed.Dispose();
                        layerOrderChanged = true;
                    }
                    continue;
                }
                if ((change.Flags & LayerReplace) == 0 || !ValidateLayer(view, change))
                {
                    return false;
                }
                var replacement = CompileLayer(view, change);
                var orderChanged = true;
                if (s_layers.Remove(change.NodeId, out var previous))
                {
                    orderChanged =
                        previous.ZOrder != replacement.ZOrder
                        || !ReplaceOrderedLayer(previous, replacement);
                    s_totalCommandCount -= previous.CommandCount;
                    previous.Dispose();
                }
                s_layers[change.NodeId] = replacement;
                s_totalCommandCount += replacement.CommandCount;
                layerOrderChanged |= orderChanged;
            }
            if (layerOrderChanged)
            {
                RebuildLayerOrder();
            }
            s_revision = header.Revision;
        }

        return true;
    }

    public void RenderRetained(
        SKCanvas canvas,
        float viewportWidth,
        float viewportHeight,
        Func<SKRect, bool>? intersects)
    {
        if (s_domBackdropPicture is not null
            && (intersects is null || intersects(new SKRect(0, 0, viewportWidth, viewportHeight))))
        {
            canvas.DrawPicture(s_domBackdropPicture);
        }
        foreach (var layer in s_orderedLayers)
        {
            if (layer.Width <= 0 || layer.Height <= 0
                || layer.BitmapWidth == 0 || layer.BitmapHeight == 0)
            {
                continue;
            }
            if (intersects is not null
                && !intersects(new SKRect(layer.X, layer.Y, layer.X + layer.Width, layer.Y + layer.Height)))
            {
                continue;
            }
            var save = canvas.Save();
            canvas.ClipRect(new SKRect(layer.X, layer.Y, layer.X + layer.Width, layer.Y + layer.Height));
            if (layer.RequiresIsolation)
            {
                // Browser canvases are independent transparent bitmaps. A
                // destructive operation must affect this canvas only, then the
                // result is source-over composited with lower siblings.
                canvas.SaveLayer();
            }
            canvas.Translate(layer.X, layer.Y);
            canvas.Scale(layer.Width / layer.BitmapWidth, layer.Height / layer.BitmapHeight);
            canvas.DrawPicture(layer.Picture);
            canvas.RestoreToCount(save);
        }
        if (s_domOverlayPicture is not null
            && (intersects is null || intersects(new SKRect(0, 0, viewportWidth, viewportHeight))))
        {
            canvas.DrawPicture(s_domOverlayPicture);
        }
    }

    private static bool ValidateLayer(NativeSceneView* view, in NativeCanvasLayer layer)
        => layer.CommandOffset <= view->CanvasCommandCount
            && layer.CommandCount <= view->CanvasCommandCount - layer.CommandOffset
            && layer.StringOffset <= view->StringCount
            && layer.StringCount <= view->StringCount - layer.StringOffset;

    private SKPicture CompileDom(NativeSceneView* view, bool foreground)
    {
        using var recorder = new SKPictureRecorder();
        var canvas = recorder.BeginRecording(new SKRect(
            0,
            0,
            Math.Max(1, view->Header.ViewportWidth),
            Math.Max(1, view->Header.ViewportHeight)));
        var commands = new ReadOnlySpan<SceneCommand>(
            view->Commands,
            checked((int)view->Header.CommandCount));
        using var fill = new SKPaint { Style = SKPaintStyle.Fill };
        using var stroke = new SKPaint { Style = SKPaintStyle.Stroke };
        using var opacity = new SKPaint();
        using var textPaint = new SKPaint
        {
            IsAntialias = true,
            Style = SKPaintStyle.Fill,
            TextAlign = SKTextAlign.Left
        };
        var textShapers = new Dictionary<string, SKShaper>(StringComparer.Ordinal);
        try
        {
            foreach (ref readonly var command in commands)
            {
                switch (command.Kind)
                {
                    case 30:
                        opacity.Color = new SKColor(
                            255,
                            255,
                            255,
                            (byte)(command.Rgba & 0xff));
                        canvas.SaveLayer(opacity);
                        break;
                    case 31:
                        canvas.Restore();
                        break;
                    case 15:
                        canvas.Save();
                        canvas.Translate(command.X, command.Y);
                        canvas.Scale(command.Width, command.Height);
                        canvas.Translate(-command.X, -command.Y);
                        break;
                    case 16:
                        canvas.Restore();
                        break;
                    case 19:
                        canvas.Save();
                        canvas.Translate(command.X, command.Y);
                        canvas.RotateDegrees(command.StrokeWidth);
                        canvas.Translate(-command.X, -command.Y);
                        break;
                    case 20:
                        canvas.Restore();
                        break;
                    case 17 when !foreground:
                    case 18 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        DrawDomShadow(canvas, command);
                        break;
                    case 1 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = false;
                        fill.Color = Rgba(command.Rgba);
                        canvas.DrawRect(command.X, command.Y, command.Width, command.Height, fill);
                        break;
                    case 2 when !foreground:
                        NativeSceneDrawOperation.LineCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(0.1f, command.Flags / 100f);
                        canvas.DrawLine(command.X, command.Y, command.Width, command.Height, stroke);
                        break;
                    case 3 when foreground:
                        NativeSceneDrawOperation.TextCommandCount++;
                        DrawDomText(canvas, view, command, textPaint, textShapers);
                        break;
                    case 4 when foreground:
                    case 5 when foreground:
                        NativeSceneDrawOperation.SvgCommandCount++;
                        DrawDomSvgPath(canvas, view, command, command.Kind == 5);
                        break;
                    case 6 when foreground:
                        NativeSceneDrawOperation.SvgCommandCount++;
                        DrawDomSvg(canvas, view, command);
                        break;
                    case 7 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = true;
                        fill.Color = Rgba(command.Rgba);
                        DrawDomRoundedRect(canvas, command, fill);
                        break;
                    case 8 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(
                            0.1f,
                            command.StrokeWidth > 0
                                ? command.StrokeWidth
                                : (command.Flags & 0xffff) / 100f);
                        DrawDomRoundedBorder(canvas, command, stroke);
                        break;
                    case 9 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = false;
                        fill.Color = Rgba(command.Rgba);
                        canvas.DrawRect(command.X, command.Y, command.Width, command.Height, fill);
                        break;
                    case 10 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = true;
                        fill.Color = Rgba(command.Rgba);
                        DrawDomRoundedRect(canvas, command, fill);
                        break;
                    case 11 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(
                            0.1f,
                            command.StrokeWidth > 0
                                ? command.StrokeWidth
                                : (command.Flags & 0xffff) / 100f);
                        DrawDomRoundedBorder(canvas, command, stroke);
                        break;
                    case 14 when foreground:
                        NativeSceneDrawOperation.LineCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(0.1f, command.Flags / 100f);
                        canvas.DrawLine(command.X, command.Y, command.Width, command.Height, stroke);
                        break;
                    case 12:
                        canvas.Save();
                        ClipDomRoundedRect(canvas, command);
                        break;
                    case 13:
                        canvas.Restore();
                        break;
                }
            }
        }
        finally
        {
            foreach (var shaper in textShapers.Values)
            {
                shaper.Dispose();
            }
        }
        return recorder.EndRecording();
    }

    private static void DrawDomRoundedRect(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint)
    {
        var topLeft = command.RadiusTopLeft;
        var topRight = command.RadiusTopRight;
        var bottomRight = command.RadiusBottomRight;
        var bottomLeft = command.RadiusBottomLeft;
        if (topLeft <= 0 && topRight <= 0 && bottomRight <= 0 && bottomLeft <= 0)
        {
            var legacyRadius = (command.Flags >> 16) / 100f;
            topLeft = topRight = bottomRight = bottomLeft = legacyRadius;
        }

        if (Math.Abs(topLeft - topRight) < 0.001f
            && Math.Abs(topLeft - bottomRight) < 0.001f
            && Math.Abs(topLeft - bottomLeft) < 0.001f)
        {
            canvas.DrawRoundRect(
                command.X,
                command.Y,
                command.Width,
                command.Height,
                topLeft,
                topLeft,
                paint);
            return;
        }

        using var rounded = new SKRoundRect();
        var radii = new SKPoint[4]
        {
            new(topLeft, topLeft),
            new(topRight, topRight),
            new(bottomRight, bottomRight),
            new(bottomLeft, bottomLeft)
        };
        rounded.SetRectRadii(
            new SKRect(
                command.X,
                command.Y,
                command.X + command.Width,
                command.Y + command.Height),
            radii);
        canvas.DrawRoundRect(rounded, paint);
    }

    private const uint DomBorderTop = 1u << 28;
    private const uint DomBorderRight = 1u << 29;
    private const uint DomBorderBottom = 1u << 30;
    private const uint DomBorderLeft = 1u << 31;
    private const uint DomBorderColorPartition = 1u << 27;
    private const uint DomBorderSideMask = DomBorderTop
        | DomBorderRight
        | DomBorderBottom
        | DomBorderLeft;

    private static void DrawDomRoundedBorder(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint)
    {
        var sides = command.Flags & DomBorderSideMask;
        if (sides == 0)
        {
            DrawDomRoundedRect(canvas, command, paint);
            return;
        }

        if ((command.Flags & DomBorderColorPartition) == 0)
        {
            DrawDomRoundedBorderSides(canvas, command, paint, sides);
            return;
        }

        var halfStroke = paint.StrokeWidth * 0.5f;
        var outerLeft = command.X - halfStroke;
        var outerTop = command.Y - halfStroke;
        var outerRight = command.X + command.Width + halfStroke;
        var outerBottom = command.Y + command.Height + halfStroke;
        var centerX = (outerLeft + outerRight) * 0.5f;
        var centerY = (outerTop + outerBottom) * 0.5f;
        var roundedCommand = command;

        DrawSide(DomBorderTop, outerLeft, outerTop, outerRight, outerTop);
        DrawSide(DomBorderRight, outerRight, outerTop, outerRight, outerBottom);
        DrawSide(DomBorderBottom, outerRight, outerBottom, outerLeft, outerBottom);
        DrawSide(DomBorderLeft, outerLeft, outerBottom, outerLeft, outerTop);

        void DrawSide(uint side, float firstX, float firstY, float secondX, float secondY)
        {
            if ((sides & side) == 0) return;
            using var wedge = new SKPath();
            wedge.MoveTo(firstX, firstY);
            wedge.LineTo(secondX, secondY);
            wedge.LineTo(centerX, centerY);
            wedge.Close();
            canvas.Save();
            canvas.ClipPath(wedge, SKClipOperation.Intersect, antialias: true);
            DrawDomRoundedRect(canvas, roundedCommand, paint);
            canvas.Restore();
        }
    }

    private static void DrawDomRoundedBorderSides(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint,
        uint sides)
    {
        var left = command.X;
        var top = command.Y;
        var right = command.X + command.Width;
        var bottom = command.Y + command.Height;
        var topLeft = command.RadiusTopLeft;
        var topRight = command.RadiusTopRight;
        var bottomRight = command.RadiusBottomRight;
        var bottomLeft = command.RadiusBottomLeft;
        const float arcHandle = 0.55228475f;
        using var path = new SKPath();

        if ((sides & DomBorderTop) != 0)
        {
            path.MoveTo(left + topLeft, top);
            path.LineTo(right - topRight, top);
        }
        if ((sides & DomBorderRight) != 0)
        {
            path.MoveTo(right, top + topRight);
            path.LineTo(right, bottom - bottomRight);
        }
        if ((sides & DomBorderBottom) != 0)
        {
            path.MoveTo(right - bottomRight, bottom);
            path.LineTo(left + bottomLeft, bottom);
        }
        if ((sides & DomBorderLeft) != 0)
        {
            path.MoveTo(left, bottom - bottomLeft);
            path.LineTo(left, top + topLeft);
        }

        AppendCorner(DomBorderTop, DomBorderLeft, topLeft,
            left + topLeft, top, left, top + topLeft, true, true);
        AppendCorner(DomBorderTop, DomBorderRight, topRight,
            right - topRight, top, right, top + topRight, false, true);
        AppendCorner(DomBorderRight, DomBorderBottom, bottomRight,
            right, bottom - bottomRight, right - bottomRight, bottom, false, false);
        AppendCorner(DomBorderBottom, DomBorderLeft, bottomLeft,
            left + bottomLeft, bottom, left, bottom - bottomLeft, true, false);
        canvas.DrawPath(path, paint);

        void AppendCorner(
            uint firstSide,
            uint secondSide,
            float radius,
            float startX,
            float startY,
            float endX,
            float endY,
            bool leftCorner,
            bool topCorner)
        {
            if ((sides & (firstSide | secondSide)) != (firstSide | secondSide) || radius <= 0) return;
            path.MoveTo(startX, startY);
            var control = radius * arcHandle;
            if (topCorner && leftCorner)
                path.CubicTo(startX - control, startY, endX, endY - control, endX, endY);
            else if (topCorner)
                path.CubicTo(startX + control, startY, endX, endY - control, endX, endY);
            else if (leftCorner)
                path.CubicTo(startX - control, startY, endX, endY + control, endX, endY);
            else
                path.CubicTo(startX, startY + control, endX + control, endY, endX, endY);
        }
    }

    private static void ClipDomRoundedRect(SKCanvas canvas, in SceneCommand command)
    {
        if (command.RadiusTopLeft <= 0
            && command.RadiusTopRight <= 0
            && command.RadiusBottomRight <= 0
            && command.RadiusBottomLeft <= 0)
        {
            canvas.ClipRect(
                new SKRect(
                    command.X,
                    command.Y,
                    command.X + command.Width,
                    command.Y + command.Height),
                SKClipOperation.Intersect,
                antialias: false);
            return;
        }

        using var rounded = new SKRoundRect();
        rounded.SetRectRadii(
            new SKRect(
                command.X,
                command.Y,
                command.X + command.Width,
                command.Y + command.Height),
            [
                new(command.RadiusTopLeft, command.RadiusTopLeft),
                new(command.RadiusTopRight, command.RadiusTopRight),
                new(command.RadiusBottomRight, command.RadiusBottomRight),
                new(command.RadiusBottomLeft, command.RadiusBottomLeft)
            ]);
        canvas.ClipRoundRect(rounded, SKClipOperation.Intersect, antialias: true);
    }

    private void DrawDomText(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command,
        SKPaint paint,
        Dictionary<string, SKShaper> shapers)
    {
        var resource = DomStringAt(view, command.Flags);
        var parts = resource.Split('\t', 6);
        if (parts.Length != 6
            || !float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var fontSize)
            || fontSize <= 0)
        {
            return;
        }
        var lineHeight = float.TryParse(
            parts[1],
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out var parsedLineHeight)
            && parsedLineHeight > 0
            ? parsedLineHeight
            : fontSize * 1.2f;
        var fontWeight = int.TryParse(
            parts[2],
            NumberStyles.Integer,
            CultureInfo.InvariantCulture,
            out var parsedWeight)
            ? Math.Clamp(parsedWeight, 1, 1000)
            : 400;
        var typeface = NativeTextShaping.ResolveTypeface(
            parts[4],
            fontWeight,
            _webTypefaces);
        paint.Color = Rgba(command.Rgba);
        paint.TextSize = fontSize;
        paint.Typeface = typeface;
        var shaperKey = parts[4] + '\t' + fontWeight.ToString(CultureInfo.InvariantCulture);
        if (!shapers.TryGetValue(shaperKey, out var shaper))
        {
            shaper = new SKShaper(typeface);
            shapers.Add(shaperKey, shaper);
        }
        var featureFlags = NativeTextShaping.ResolveFeatureFlags(
            parts[5],
            parts[4],
            0,
            _webTypefaces);
        var tabularDigitScale = NativeTextShaping.ResolveTabularDigitScale(
            parts[4],
            _webTypefaces);
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            parts[5],
            paint,
            featureFlags,
            tabularDigitScale);
        var widthScale = (featureFlags & NativeTextShaping.TabularNumerals) != 0
            ? NativeTextShaping.ResolveWidthScale(
                parts[4],
                fontSize,
                fontWeight,
                _webTypefaces)
            : 1f;
        var renderedWidth = shapedWidth * widthScale;
        var x = parts[3] switch
        {
            "center" => command.X + (command.Width - renderedWidth) * 0.5f,
            "right" or "end" => command.X + command.Width - renderedWidth,
            _ => command.X,
        };
        paint.GetFontMetrics(out var metrics);
        var glyphHeight = metrics.Descent - metrics.Ascent;
        var contentHeight = Math.Min(Math.Max(lineHeight, glyphHeight), Math.Max(lineHeight, command.Height));
        var baseline = command.Y
            + Math.Max(0, (command.Height - contentHeight) * 0.5f)
            + (contentHeight - glyphHeight) * 0.5f
            - metrics.Ascent
            + (parsedLineHeight == 0 ? 3f : 0f);
        canvas.Save();
        canvas.Scale(widthScale, 1f, x, baseline);
        NativeTextShaping.DrawShapedText(
            canvas,
            shaper,
            parts[5],
            x,
            baseline,
            paint,
            featureFlags,
            tabularDigitScale);
        canvas.Restore();
    }

    private static void DrawDomShadow(SKCanvas canvas, in SceneCommand command)
    {
        using var blur = command.StrokeWidth > 0
            ? SKMaskFilter.CreateBlur(SKBlurStyle.Normal, Math.Max(0.1f, command.StrokeWidth * 0.5f))
            : null;
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Style = SKPaintStyle.Fill,
            Color = Rgba(command.Rgba),
            MaskFilter = blur
        };
        DrawDomRoundedRect(canvas, command, paint);
    }

    private static void DrawDomSvgPath(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command,
        bool stroke)
    {
        var resource = DomStringAt(view, command.Flags);
        var parts = resource.Split('\t', 4);
        if (parts.Length != 4 || command.Width <= 0 || command.Height <= 0)
        {
            return;
        }
        var viewBox = ParseSvgNumbers(parts[0]);
        if (viewBox.Length < 4 || viewBox[2] == 0 || viewBox[3] == 0)
        {
            return;
        }
        using var path = SKPath.ParseSvgPathData(parts[3]);
        if (path is null)
        {
            return;
        }
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Style = stroke ? SKPaintStyle.Stroke : SKPaintStyle.Fill,
            Color = Rgba(command.Rgba),
            StrokeWidth = stroke
                && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var width)
                    ? Math.Max(0.1f, width)
                    : 1
        };
        var save = canvas.Save();
        try
        {
            ApplyDomRotation(canvas, command);
            canvas.Translate(command.X, command.Y);
            canvas.Scale(command.Width / viewBox[2], command.Height / viewBox[3]);
            canvas.Translate(-viewBox[0], -viewBox[1]);
            ApplySvgTransform(canvas, parts[2]);
            canvas.DrawPath(path, paint);
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
    }

    private void DrawDomSvg(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command)
    {
        var resource = DomStringAt(view, command.Flags);
        var separator = resource.IndexOf('\t');
        if (separator <= 0 || separator == resource.Length - 1
            || command.Width <= 0 || command.Height <= 0)
        {
            return;
        }
        var viewBox = ParseSvgNumbers(resource[..separator]);
        if (viewBox.Length < 4 || viewBox[2] == 0 || viewBox[3] == 0)
        {
            return;
        }
        var markup = resource[(separator + 1)..];
        if (!s_svgPictures.TryGetValue(markup, out var svg))
        {
            var acquired = SharedSvgPictureCache.Acquire(markup);
            if (acquired is null)
            {
                return;
            }
            svg = acquired;
            s_svgPictures.Add(svg.Markup, svg);
        }

        var save = canvas.Save();
        try
        {
            ApplyDomRotation(canvas, command);
            canvas.Translate(command.X, command.Y);
            canvas.Scale(command.Width / viewBox[2], command.Height / viewBox[3]);
            canvas.Translate(-viewBox[0], -viewBox[1]);
            canvas.DrawPicture(svg.Picture);
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
    }

    private static void ApplyDomRotation(SKCanvas canvas, in SceneCommand command)
    {
        if (Math.Abs(command.StrokeWidth) < 0.001f)
        {
            return;
        }
        canvas.RotateDegrees(
            command.StrokeWidth,
            command.X + command.Width / 2,
            command.Y + command.Height / 2);
    }

    private static float[] ParseSvgNumbers(string value)
        => value.Split(
                [' ', ','],
                StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Select(item => float.TryParse(
                    item,
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsed)
                ? parsed
                : float.NaN)
            .Where(float.IsFinite)
            .ToArray();

    private static void ApplySvgTransform(SKCanvas canvas, string transform)
    {
        var cursor = 0;
        while (cursor < transform.Length)
        {
            while (cursor < transform.Length && char.IsWhiteSpace(transform[cursor])) cursor++;
            var open = transform.IndexOf('(', cursor);
            if (open < 0) break;
            var close = transform.IndexOf(')', open + 1);
            if (close < 0) break;
            var operation = transform[cursor..open].Trim();
            var values = ParseSvgNumbers(transform[(open + 1)..close]);
            switch (operation)
            {
                case "translate" when values.Length >= 1:
                    canvas.Translate(values[0], values.Length >= 2 ? values[1] : 0);
                    break;
                case "scale" when values.Length >= 1:
                    canvas.Scale(values[0], values.Length >= 2 ? values[1] : values[0]);
                    break;
                case "rotate" when values.Length >= 1:
                    if (values.Length >= 3)
                    {
                        canvas.Translate(values[1], values[2]);
                        canvas.RotateDegrees(values[0]);
                        canvas.Translate(-values[1], -values[2]);
                    }
                    else
                    {
                        canvas.RotateDegrees(values[0]);
                    }
                    break;
                case "matrix" when values.Length >= 6:
                    var matrix = new SKMatrix
                    {
                        ScaleX = values[0],
                        SkewY = values[1],
                        SkewX = values[2],
                        ScaleY = values[3],
                        TransX = values[4],
                        TransY = values[5],
                        Persp2 = 1
                    };
#if WEBSCENE_UNO
                    canvas.Concat(in matrix);
#else
                    canvas.Concat(ref matrix);
#endif
                    break;
            }
            cursor = close + 1;
        }
    }

    private static string DomStringAt(NativeSceneView* view, uint index)
    {
        if (index >= view->StringCount || view->Strings == null || view->StringBytes == null)
        {
            return string.Empty;
        }
        var descriptor = view->Strings[index];
        if (descriptor.ByteOffset > view->StringByteCount
            || descriptor.ByteLength > view->StringByteCount - descriptor.ByteOffset)
        {
            return string.Empty;
        }
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
            view->StringBytes + descriptor.ByteOffset,
            checked((int)descriptor.ByteLength)));
    }

    private RetainedLayer CompileLayer(NativeSceneView* view, in NativeCanvasLayer layer)
    {
        var requiresIsolation = RequiresIsolation(view, layer);
        using var recorder = new SKPictureRecorder();
        var canvas = recorder.BeginRecording(new SKRect(
            0,
            0,
            Math.Max(1, layer.BitmapWidth),
            Math.Max(1, layer.BitmapHeight)));
        Replay(canvas, view, layer, skipLeadingClears: !requiresIsolation);
        var picture = recorder.EndRecording();
        DumpLayerIfRequested(view, layer, picture);
        return new RetainedLayer(
            layer.NodeId,
            layer.Generation,
            layer.Reserved,
            layer.X,
            layer.Y,
            layer.Width,
            layer.Height,
            layer.BitmapWidth,
            layer.BitmapHeight,
            layer.CommandCount,
            requiresIsolation,
            picture);
    }

    private bool RequiresIsolation(
        NativeSceneView* view,
        in NativeCanvasLayer layer)
    {
        var hasDrawn = false;
        var commands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        foreach (ref readonly var command in commands)
        {
            switch (command.Kind)
            {
                // A clear before the first draw is a no-op on the initially
                // transparent browser canvas and is omitted from the picture.
                case 24 when hasDrawn:
                // drawImage(canvas) needs source-bitmap isolation semantics.
                case 27:
                    return true;
                case 53:
                    var composite = StringAt(view, layer, command.ResourceId);
                    if (!string.IsNullOrEmpty(composite)
                        && !string.Equals(
                            composite,
                            "source-over",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                    break;
                case >= 20 and <= 29 when command.Kind != 24:
                    hasDrawn = true;
                    break;
            }
        }
        return false;
    }

    private void DumpLayerIfRequested(
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        SKPicture picture)
    {
        var directory = Environment.GetEnvironmentVariable("WEBSCENE_PROBE_DUMP_LAYERS");
        if (string.IsNullOrWhiteSpace(directory)
            || layer.BitmapWidth == 0
            || layer.BitmapHeight == 0
            || layer.BitmapWidth > 16_384
            || layer.BitmapHeight > 16_384)
        {
            return;
        }

        Directory.CreateDirectory(directory);
        using var surface = SKSurface.Create(new SKImageInfo(
            checked((int)layer.BitmapWidth),
            checked((int)layer.BitmapHeight),
            SKColorType.Rgba8888,
            SKAlphaType.Premul));
        if (surface is null)
        {
            return;
        }
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.DrawPicture(picture);
        surface.Canvas.Flush();
        using var image = surface.Snapshot();
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        var path = Path.Combine(
            directory,
            $"canvas-{layer.NodeId}-generation-{layer.Generation}.png");
        using var stream = File.Create(path);
        data.SaveTo(stream);

        using var commands = new StreamWriter(Path.ChangeExtension(path, ".fill-rects.tsv"));
        commands.WriteLine("index\tfillStyle\tx\ty\twidth\theight\ttransformedX\ttransformedY\ttransformedWidth\ttransformedHeight");
        var fillStyle = "#000000";
        var transform = CanvasAffine.Identity;
        var transforms = new Stack<CanvasAffine>();
        var layerCommands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        using (var trace = new StreamWriter(Path.ChangeExtension(path, ".commands.tsv")))
        {
            trace.WriteLine("index\tkind\tresourceId\tv0\tv1\tv2\tv3\tv4\tv5\tv6\tv7\tresource");
            for (var index = 0; index < layerCommands.Length; ++index)
            {
                ref readonly var command = ref layerCommands[index];
                var resource = command.Kind is 25 or 26 or 28 or 29 or 40 or 41 or 43 or 44
                    or 48 or 49 or 50 or 52 or 53 or 54
                    ? StringAt(view, layer, command.ResourceId)
                    : string.Empty;
                trace.WriteLine(string.Create(
                    CultureInfo.InvariantCulture,
                    $"{index}\t{command.Kind}\t{command.ResourceId}\t{command.V0}\t{command.V1}\t{command.V2}\t{command.V3}\t{command.V4}\t{command.V5}\t{command.V6}\t{command.V7}\t{resource.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ')}"));
            }
        }
        for (var index = 0; index < layerCommands.Length; ++index)
        {
            ref readonly var command = ref layerCommands[index];
            switch (command.Kind)
            {
                case 1:
                    transforms.Push(transform);
                    break;
                case 2 when transforms.Count != 0:
                    transform = transforms.Pop();
                    break;
                case 3:
                    transform = CanvasAffine.Identity;
                    break;
                case 4:
                    transform = CanvasAffine.From(command);
                    break;
                case 5:
                    transform = transform.Multiply(CanvasAffine.From(command));
                    break;
                case 6:
                    transform = transform.Multiply(new CanvasAffine(1, 0, 0, 1, command.V0, command.V1));
                    break;
                case 7:
                    transform = transform.Multiply(new CanvasAffine(command.V0, 0, 0, command.V1, 0, 0));
                    break;
                case 8:
                    transform = transform.Multiply(new CanvasAffine(
                        Math.Cos(command.V0),
                        Math.Sin(command.V0),
                        -Math.Sin(command.V0),
                        Math.Cos(command.V0),
                        0,
                        0));
                    break;
                case 40:
                    fillStyle = StringAt(view, layer, command.ResourceId);
                    break;
                case 22:
                {
                    var first = transform.Map(command.V0, command.V1);
                    var second = transform.Map(command.V0 + command.V2, command.V1 + command.V3);
                    var left = Math.Min(first.X, second.X);
                    var top = Math.Min(first.Y, second.Y);
                    commands.WriteLine(string.Create(
                        CultureInfo.InvariantCulture,
                        $"{index}\t{fillStyle}\t{command.V0}\t{command.V1}\t{command.V2}\t{command.V3}" +
                        $"\t{left}\t{top}\t{Math.Abs(second.X - first.X)}\t{Math.Abs(second.Y - first.Y)}"));
                    break;
                }
            }
        }
    }

    private void Replay(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        bool skipLeadingClears = false)
    {
        var state = CanvasState.Default;
        var states = new Stack<CanvasState>();
        var hasDrawn = false;
        using var path = new SKPath();
        var commands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        foreach (ref readonly var command in commands)
        {
            switch (command.Kind)
            {
                case 1:
                    states.Push(state);
                    canvas.Save();
                    break;
                case 2:
                    if (states.Count != 0)
                    {
                        state = states.Pop();
                        canvas.Restore();
                    }
                    break;
                case 3:
                    canvas.ResetMatrix();
                    break;
                case 4:
                    canvas.SetMatrix(ToMatrix(command));
                    break;
                case 5:
                {
                    var matrix = ToMatrix(command);
#if WEBSCENE_UNO
                    canvas.Concat(in matrix);
#else
                    canvas.Concat(ref matrix);
#endif
                    break;
                }
                case 6: canvas.Translate((float)command.V0, (float)command.V1); break;
                case 7: canvas.Scale((float)command.V0, (float)command.V1); break;
                case 8: canvas.RotateRadians((float)command.V0); break;
                case 9: path.Reset(); break;
                case 10: path.Close(); break;
                case 11: path.MoveTo((float)command.V0, (float)command.V1); break;
                case 12: path.LineTo((float)command.V0, (float)command.V1); break;
                case 13:
                    path.CubicTo(
                        (float)command.V0, (float)command.V1,
                        (float)command.V2, (float)command.V3,
                        (float)command.V4, (float)command.V5);
                    break;
                case 14:
                    path.QuadTo(
                        (float)command.V0, (float)command.V1,
                        (float)command.V2, (float)command.V3);
                    break;
                case 15:
                    AppendArc(path, command);
                    break;
                case 16:
                    path.ArcTo(
                        new SKPoint((float)command.V0, (float)command.V1),
                        new SKPoint((float)command.V2, (float)command.V3),
                        (float)Math.Max(0, command.V4));
                    break;
                case 17:
                    path.AddRect(new SKRect(
                        (float)command.V0,
                        (float)command.V1,
                        (float)(command.V0 + command.V2),
                        (float)(command.V1 + command.V3)));
                    break;
                case 18:
                    canvas.ClipPath(path, SKClipOperation.Intersect, true);
                    break;
                case 19:
                {
                    var count = Math.Clamp((int)command.V0, 0, 7);
                    state.LineDash = count switch
                    {
                        0 => [],
                        1 => [command.V1],
                        2 => [command.V1, command.V2],
                        3 => [command.V1, command.V2, command.V3],
                        4 => [command.V1, command.V2, command.V3, command.V4],
                        5 => [command.V1, command.V2, command.V3, command.V4, command.V5],
                        6 => [command.V1, command.V2, command.V3, command.V4, command.V5, command.V6],
                        _ => [command.V1, command.V2, command.V3, command.V4, command.V5, command.V6, command.V7]
                    };
                    break;
                }
                case 20:
                    using (var stroke = CreatePaint(state, false, SKPaintStyle.Stroke))
                    {
                        canvas.DrawPath(path, stroke);
                    }
                    hasDrawn = true;
                    break;
                case 21:
                    using (var fill = CreatePaint(state, true, SKPaintStyle.Fill))
                    {
                        path.FillType = (command.Flags & CanvasCommandEvenOdd) != 0
                            ? SKPathFillType.EvenOdd
                            : SKPathFillType.Winding;
                        canvas.DrawPath(path, fill);
                    }
                    hasDrawn = true;
                    break;
                case 22:
                    using (var fill = CreatePaint(state, true, SKPaintStyle.Fill))
                    {
                        canvas.DrawRect(ToRect(command), fill);
                    }
                    hasDrawn = true;
                    break;
                case 23:
                    using (var stroke = CreatePaint(state, false, SKPaintStyle.Stroke))
                    {
                        canvas.DrawRect(ToRect(command), stroke);
                    }
                    hasDrawn = true;
                    break;
                case 24 when skipLeadingClears && !hasDrawn:
                    break;
                case 24:
                    using (var clear = new SKPaint { BlendMode = SKBlendMode.Clear, Style = SKPaintStyle.Fill })
                    {
                        canvas.DrawRect(ToRect(command), clear);
                    }
                    break;
                case 25:
                    DrawText(canvas, view, layer, command, state, false);
                    hasDrawn = true;
                    break;
                case 26:
                    DrawText(canvas, view, layer, command, state, true);
                    hasDrawn = true;
                    break;
                case 27:
                    DrawCanvas(canvas, command, state);
                    hasDrawn = true;
                    break;
                case 28:
                    DrawSvgCanvasPath(canvas, view, layer, command, state, true);
                    hasDrawn = true;
                    break;
                case 29:
                    DrawSvgCanvasPath(canvas, view, layer, command, state, false);
                    hasDrawn = true;
                    break;
                case 40: state.FillStyle = StringAt(view, layer, command.ResourceId); break;
                case 41: state.StrokeStyle = StringAt(view, layer, command.ResourceId); break;
                case 42: state.LineWidth = command.V0; break;
                case 43: state.LineCap = StringAt(view, layer, command.ResourceId); break;
                case 44: state.LineJoin = StringAt(view, layer, command.ResourceId); break;
                case 45: state.MiterLimit = command.V0; break;
                case 46: state.GlobalAlpha = Math.Clamp(command.V0, 0, 1); break;
                case 47: state.LineDashOffset = command.V0; break;
                case 48: state.Font = StringAt(view, layer, command.ResourceId); break;
                case 49: state.TextAlign = StringAt(view, layer, command.ResourceId); break;
                case 50: state.TextBaseline = StringAt(view, layer, command.ResourceId); break;
                case 51: state.ImageSmoothingEnabled = command.V0 != 0; break;
                case 52: state.ImageSmoothingQuality = StringAt(view, layer, command.ResourceId); break;
                case 53: state.Composite = StringAt(view, layer, command.ResourceId); break;
                case 54: state.ShadowColor = StringAt(view, layer, command.ResourceId); break;
                case 55: state.ShadowBlur = command.V0; break;
                case 56: state.ShadowOffsetX = command.V0; break;
                case 57: state.ShadowOffsetY = command.V0; break;
            }
        }
    }

    private void DrawSvgCanvasPath(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        in NativeCanvasCommand command,
        in CanvasState state,
        bool fill)
    {
        using var path = SKPath.ParseSvgPathData(StringAt(view, layer, command.ResourceId));
        if (path is null)
        {
            return;
        }
        path.FillType = fill && (command.Flags & CanvasCommandEvenOdd) != 0
            ? SKPathFillType.EvenOdd
            : SKPathFillType.Winding;
        if ((command.Flags & 0xFFFFu) >= 6u)
        {
            var matrix = ToMatrix(command);
            path.Transform(matrix);
        }
        using var paint = CreatePaint(
            state,
            fill,
            fill ? SKPaintStyle.Fill : SKPaintStyle.Stroke);
        canvas.DrawPath(path, paint);
    }

    private void DrawCanvas(SKCanvas canvas, in NativeCanvasCommand command, in CanvasState state)
    {
        if (!s_layers.TryGetValue(command.ResourceId, out var source)
            || command.V2 == 0 || command.V3 == 0)
        {
            return;
        }
        var destination = new SKRect(
            (float)command.V4,
            (float)command.V5,
            (float)(command.V4 + command.V6),
            (float)(command.V5 + command.V7));
        var save = canvas.Save();
        canvas.ClipRect(destination);
        canvas.Translate((float)command.V4, (float)command.V5);
        canvas.Scale((float)(command.V6 / command.V2), (float)(command.V7 / command.V3));
        canvas.Translate((float)-command.V0, (float)-command.V1);
        using var paint = CreatePaint(state, true, SKPaintStyle.Fill);
        canvas.DrawPicture(source.Picture, paint);
        canvas.RestoreToCount(save);
    }

    private void DrawText(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        in NativeCanvasCommand command,
        in CanvasState state,
        bool stroke)
    {
        var text = StringAt(view, layer, command.ResourceId);
        if (text.Length == 0) return;
        using var paint = CreatePaint(
            state,
            !stroke,
            stroke ? SKPaintStyle.Stroke : SKPaintStyle.Fill);
        ConfigureFont(paint, state.Font);
        paint.TextAlign = state.TextAlign switch
        {
            "center" => SKTextAlign.Center,
            "right" or "end" => SKTextAlign.Right,
            _ => SKTextAlign.Left
        };
        var y = (float)command.V1;
        var metrics = paint.FontMetrics;
        y += state.TextBaseline switch
        {
            "top" => -metrics.Top,
            "hanging" => -metrics.Ascent * 0.8f,
            "middle" => -(metrics.Ascent + metrics.Descent) / 2,
            "bottom" or "ideographic" => -metrics.Bottom,
            _ => 0
        };
        canvas.DrawText(text, (float)command.V0, y, paint);
    }

    private static SKPaint CreatePaint(in CanvasState state, bool fill, SKPaintStyle style)
    {
        var color = ParseColor(fill ? state.FillStyle : state.StrokeStyle);
        color = color.WithAlpha((byte)Math.Clamp(
            Math.Round(color.Alpha * state.GlobalAlpha),
            0,
            255));
        var paint = new SKPaint
        {
            IsAntialias = true,
            Style = style,
            Color = color,
            StrokeWidth = (float)Math.Max(0, state.LineWidth),
            StrokeMiter = (float)Math.Max(0, state.MiterLimit),
            StrokeCap = state.LineCap switch
            {
                "round" => SKStrokeCap.Round,
                "square" => SKStrokeCap.Square,
                _ => SKStrokeCap.Butt
            },
            StrokeJoin = state.LineJoin switch
            {
                "round" => SKStrokeJoin.Round,
                "bevel" => SKStrokeJoin.Bevel,
                _ => SKStrokeJoin.Miter
            },
            BlendMode = BlendMode(state.Composite)
        };
        if (!fill && state.LineDash is { Length: > 0 })
        {
            paint.PathEffect = SKPathEffect.CreateDash(
                state.LineDash.Select(static value => (float)value).ToArray(),
                (float)state.LineDashOffset);
        }
        return paint;
    }

    private void ConfigureFont(SKPaint paint, string font)
    {
        var px = font.IndexOf("px", StringComparison.OrdinalIgnoreCase);
        var size = 10f;
        var family = "sans-serif";
        if (px > 0)
        {
            var start = px - 1;
            while (start >= 0 && (char.IsDigit(font[start]) || font[start] is '.' or '-' or '+')) start--;
            if (float.TryParse(
                    font.AsSpan(start + 1, px - start - 1),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsedSize)
                && parsedSize > 0)
            {
                size = parsedSize;
            }
            var families = font[(px + 2)..].Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            if (families.Length != 0)
            {
                family = families[0].Trim('"', '\'');
            }
        }
        if (_webTypefaces?.TryResolve(family, out var webTypeface) == true)
        {
            paint.Typeface = webTypeface;
        }
        else
        {
            if (!s_typefaces.TryGetValue(family, out var typeface))
            {
                typeface = SKTypeface.FromFamilyName(family)
                    ?? SKTypeface.Default;
                s_typefaces[family] = typeface;
            }
            paint.Typeface = typeface;
        }
        paint.TextSize = size;
    }

    private static void AppendArc(SKPath path, in NativeCanvasCommand command)
    {
        var radius = Math.Abs(command.V2);
        if (radius <= 0) return;
        var start = command.V3;
        var end = command.V4;
        var anticlockwise = command.V5 != 0;
        const double Tau = Math.PI * 2;
        var sweep = end - start;
        if (!anticlockwise)
        {
            while (sweep < 0) sweep += Tau;
            sweep = Math.Min(sweep, Tau);
        }
        else
        {
            while (sweep > 0) sweep -= Tau;
            sweep = Math.Max(sweep, -Tau);
        }
        if (Math.Abs(Math.Abs(sweep) - Tau) < 0.000001)
        {
            path.AddCircle(
                (float)command.V0,
                (float)command.V1,
                (float)radius,
                anticlockwise ? SKPathDirection.CounterClockwise : SKPathDirection.Clockwise);
            return;
        }
        var oval = new SKRect(
            (float)(command.V0 - radius),
            (float)(command.V1 - radius),
            (float)(command.V0 + radius),
            (float)(command.V1 + radius));
        path.ArcTo(oval, (float)(start * 180 / Math.PI), (float)(sweep * 180 / Math.PI), false);
    }

    private static SKMatrix ToMatrix(in NativeCanvasCommand command)
        => new()
        {
            ScaleX = (float)command.V0,
            SkewY = (float)command.V1,
            SkewX = (float)command.V2,
            ScaleY = (float)command.V3,
            TransX = (float)command.V4,
            TransY = (float)command.V5,
            Persp2 = 1
        };

    private static SKRect ToRect(in NativeCanvasCommand command)
        => new(
            (float)command.V0,
            (float)command.V1,
            (float)(command.V0 + command.V2),
            (float)(command.V1 + command.V3));

    private string StringAt(NativeSceneView* view, in NativeCanvasLayer layer, uint localIndex)
    {
        if (localIndex >= layer.StringCount) return string.Empty;
        var key = new StringKey(layer.NodeId, layer.Generation, localIndex);
        if (s_strings.TryGetValue(key, out var cached)) return cached;
        var globalIndex = layer.StringOffset + localIndex;
        if (globalIndex >= view->StringCount) return string.Empty;
        var descriptor = view->Strings[globalIndex];
        if (descriptor.ByteOffset > view->StringByteCount
            || descriptor.ByteLength > view->StringByteCount - descriptor.ByteOffset)
        {
            return string.Empty;
        }
        var value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
            view->StringBytes + descriptor.ByteOffset,
            checked((int)descriptor.ByteLength)));
        if (s_strings.Count >= 16_384) s_strings.Clear();
        s_strings[key] = value;
        return value;
    }

    private static SKColor ParseColor(string value)
    {
        if (CssColorParser.TryParseColor(value, out var parsed))
        {
            return new SKColor(parsed.R, parsed.G, parsed.B, parsed.A);
        }

        return SKColors.Black;
    }

    private static SKBlendMode BlendMode(string value)
        => value switch
        {
            "copy" => SKBlendMode.Src,
            "destination-over" => SKBlendMode.DstOver,
            "source-in" => SKBlendMode.SrcIn,
            "destination-in" => SKBlendMode.DstIn,
            "source-out" => SKBlendMode.SrcOut,
            "destination-out" => SKBlendMode.DstOut,
            "source-atop" => SKBlendMode.SrcATop,
            "destination-atop" => SKBlendMode.DstATop,
            "xor" => SKBlendMode.Xor,
            "lighter" => SKBlendMode.Plus,
            "multiply" => SKBlendMode.Multiply,
            "screen" => SKBlendMode.Screen,
            "overlay" => SKBlendMode.Overlay,
            "darken" => SKBlendMode.Darken,
            "lighten" => SKBlendMode.Lighten,
            "color-dodge" => SKBlendMode.ColorDodge,
            "color-burn" => SKBlendMode.ColorBurn,
            "hard-light" => SKBlendMode.HardLight,
            "soft-light" => SKBlendMode.SoftLight,
            "difference" => SKBlendMode.Difference,
            "exclusion" => SKBlendMode.Exclusion,
            "hue" => SKBlendMode.Hue,
            "saturation" => SKBlendMode.Saturation,
            "color" => SKBlendMode.Color,
            "luminosity" => SKBlendMode.Luminosity,
            _ => SKBlendMode.SrcOver
        };

    private static SKColor Rgba(uint rgba)
        => new(
            (byte)(rgba >> 24),
            (byte)(rgba >> 16),
            (byte)(rgba >> 8),
            (byte)rgba);

    internal void Reset()
    {
        s_domBackdropPicture?.Dispose();
        s_domBackdropPicture = null;
        s_domOverlayPicture?.Dispose();
        s_domOverlayPicture = null;
        s_domCommandCount = 0;
        foreach (var layer in s_layers.Values) layer.Dispose();
        s_layers.Clear();
        s_orderedLayers.Clear();
        foreach (var typeface in s_typefaces.Values) typeface.Dispose();
        s_typefaces.Clear();
        foreach (var svg in s_svgPictures.Values) svg.Dispose();
        s_svgPictures.Clear();
        s_strings.Clear();
        s_revision = 0;
        s_totalCommandCount = 0;
    }

    private void RebuildLayerOrder()
    {
        s_orderedLayers.Clear();
        s_orderedLayers.AddRange(s_layers.Values);
        s_orderedLayers.Sort(static (left, right) =>
        {
            var zOrder = left.ZOrder.CompareTo(right.ZOrder);
            return zOrder != 0
                ? zOrder
                : left.NodeId.CompareTo(right.NodeId);
        });
    }

    private bool ReplaceOrderedLayer(
        RetainedLayer previous,
        RetainedLayer replacement)
    {
        for (var index = 0; index < s_orderedLayers.Count; index++)
        {
            if (!ReferenceEquals(s_orderedLayers[index], previous))
            {
                continue;
            }
            s_orderedLayers[index] = replacement;
            return true;
        }
        return false;
    }

    private readonly record struct StringKey(uint NodeId, ulong Generation, uint Index);

    private readonly record struct CanvasAffine(
        double A,
        double B,
        double C,
        double D,
        double E,
        double F)
    {
        public static CanvasAffine Identity => new(1, 0, 0, 1, 0, 0);

        public static CanvasAffine From(in NativeCanvasCommand command)
            => new(command.V0, command.V1, command.V2, command.V3, command.V4, command.V5);

        public CanvasAffine Multiply(in CanvasAffine value)
            => new(
                A * value.A + C * value.B,
                B * value.A + D * value.B,
                A * value.C + C * value.D,
                B * value.C + D * value.D,
                A * value.E + C * value.F + E,
                B * value.E + D * value.F + F);

        public (double X, double Y) Map(double x, double y)
            => (A * x + C * y + E, B * x + D * y + F);
    }

    private sealed record RetainedLayer(
        uint NodeId,
        ulong Generation,
        uint ZOrder,
        float X,
        float Y,
        float Width,
        float Height,
        uint BitmapWidth,
        uint BitmapHeight,
        uint CommandCount,
        bool RequiresIsolation,
        SKPicture Picture) : IDisposable
    {
        public void Dispose() => Picture.Dispose();
    }

    private struct CanvasState
    {
        public string FillStyle;
        public string StrokeStyle;
        public string LineCap;
        public string LineJoin;
        public string Font;
        public string TextAlign;
        public string TextBaseline;
        public string ImageSmoothingQuality;
        public string Composite;
        public string ShadowColor;
        public double LineWidth;
        public double MiterLimit;
        public double GlobalAlpha;
        public double LineDashOffset;
        public double[] LineDash;
        public double ShadowBlur;
        public double ShadowOffsetX;
        public double ShadowOffsetY;
        public bool ImageSmoothingEnabled;

        public static CanvasState Default => new()
        {
            FillStyle = "#000000",
            StrokeStyle = "#000000",
            LineCap = "butt",
            LineJoin = "miter",
            Font = "10px sans-serif",
            TextAlign = "start",
            TextBaseline = "alphabetic",
            ImageSmoothingQuality = "low",
            Composite = "source-over",
            ShadowColor = "rgba(0, 0, 0, 0)",
            LineWidth = 1,
            MiterLimit = 10,
            GlobalAlpha = 1,
            LineDash = [],
            ImageSmoothingEnabled = true
        };
    }
}

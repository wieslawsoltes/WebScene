using System;
using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace JavaScript.Avalonia;

/// <summary>
/// Avalonia projection of the portable retained Canvas display list. Compiled
/// geometry, text, brushes and bitmaps are backend caches keyed by immutable
/// portable commands; the portable list remains the render authority.
/// </summary>
internal static class AvaloniaCanvasDisplayListRenderer
{
    private static readonly ConditionalWeakTable<CanvasDisplayCommand, CompiledCommand> s_compiled = new();
    private static readonly ConditionalWeakTable<CanvasStateModel, CompiledState> s_compiledStates = new();

    public static void Render(
        DrawingContext context,
        CanvasDisplayListModel displayList,
        CanvasDrawingSurface owner,
        Rect canvasRect)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(displayList);
        ArgumentNullException.ThrowIfNull(owner);

        var commands = displayList.Commands;
        if (commands.Count == 0)
        {
            return;
        }

        var hasPartialClear = false;
        for (var index = 0; index < commands.Count; index++)
        {
            if (commands[index].Kind == CanvasDisplayCommandKind.ClearRectangle)
            {
                hasPartialClear = true;
                break;
            }
        }

        if (!hasPartialClear)
        {
            for (var index = 0; index < commands.Count; index++)
            {
                RenderCommand(context, GetCompiled(commands[index]), owner, canvasRect);
            }

            return;
        }

        var clearedAfter = new Geometry?[commands.Count];
        Geometry? cleared = null;
        for (var index = commands.Count - 1; index >= 0; index--)
        {
            var compiled = GetCompiled(commands[index]);
            if (compiled.Command.Kind == CanvasDisplayCommandKind.ClearRectangle)
            {
                var clearGeometry = compiled.ClearGeometry!;
                cleared = cleared is null
                    ? clearGeometry
                    : new CombinedGeometry(GeometryCombineMode.Union, cleared, clearGeometry);
                continue;
            }

            clearedAfter[index] = cleared;
        }

        for (var index = 0; index < commands.Count; index++)
        {
            var compiled = GetCompiled(commands[index]);
            if (compiled.Command.Kind == CanvasDisplayCommandKind.ClearRectangle)
            {
                continue;
            }

            if (clearedAfter[index] is not { } exclusion)
            {
                RenderCommand(context, compiled, owner, canvasRect);
                continue;
            }

            var drawable = new CombinedGeometry(
                GeometryCombineMode.Exclude,
                new RectangleGeometry(canvasRect),
                exclusion);
            using (context.PushGeometryClip(drawable))
            {
                RenderCommand(context, compiled, owner, canvasRect);
            }
        }
    }

    private static CompiledCommand GetCompiled(CanvasDisplayCommand command)
        => s_compiled.GetValue(command, static source => new CompiledCommand(source));

    private static void RenderCommand(
        DrawingContext context,
        CompiledCommand compiled,
        CanvasDrawingSurface owner,
        Rect canvasRect)
    {
        IDisposable? transform = null;
        IDisposable? clip = null;
        IDisposable? opacity = null;

        try
        {
            if (compiled.State.ClipGeometry is not null)
            {
                clip = context.PushGeometryClip(compiled.State.ClipGeometry);
            }

            if (!compiled.State.Transform.IsIdentity)
            {
                transform = context.PushTransform(compiled.State.Transform);
            }

            if (compiled.State.GlobalAlpha < 1)
            {
                opacity = context.PushOpacity(compiled.State.GlobalAlpha);
            }

            RenderCore(context, compiled, owner, canvasRect);
        }
        finally
        {
            opacity?.Dispose();
            transform?.Dispose();
            clip?.Dispose();
        }
    }

    private static void RenderCore(
        DrawingContext context,
        CompiledCommand compiled,
        CanvasDrawingSurface owner,
        Rect canvasRect)
    {
        var command = compiled.Command;
        switch (command.Kind)
        {
            case CanvasDisplayCommandKind.FillRectangle:
                if (compiled.State.FillBrush is not null
                    && command.Rectangle.Width > 0
                    && command.Rectangle.Height > 0)
                {
                    context.FillRectangle(compiled.State.FillBrush, ToRect(command.Rectangle));
                }
                break;

            case CanvasDisplayCommandKind.StrokeRectangle:
                DrawStrokeRectangle(context, command, compiled.StrokePen);
                break;

            case CanvasDisplayCommandKind.FillPath:
                if (compiled.State.FillBrush is not null && compiled.PathGeometry is not null)
                {
                    context.DrawGeometry(compiled.State.FillBrush, null, compiled.PathGeometry);
                }
                break;

            case CanvasDisplayCommandKind.StrokePath:
                var pathPen = compiled.StrokePen;
                if (pathPen is not null && compiled.PathGeometry is not null)
                {
                    context.DrawGeometry(null, pathPen, compiled.PathGeometry);
                }
                break;

            case CanvasDisplayCommandKind.FillText:
                DrawText(context, command, compiled.State, owner, compiled.State.FillBrush);
                break;

            case CanvasDisplayCommandKind.StrokeText:
                DrawText(context, command, compiled.State, owner, compiled.State.StrokeBrush);
                break;

            case CanvasDisplayCommandKind.DrawImage:
                DrawImage(context, command, owner, canvasRect);
                break;

            case CanvasDisplayCommandKind.PutImageData:
                if (compiled.PixelBitmap is not null
                    && HasArea(command.SourceRectangle)
                    && HasArea(command.DestinationRectangle))
                {
                    context.DrawImage(
                        compiled.PixelBitmap,
                        ToRect(command.SourceRectangle),
                        ToRect(command.DestinationRectangle));
                }
                break;

            case CanvasDisplayCommandKind.ClearRectangle:
                // Clear commands are resolved while traversing the retained list.
                break;
        }
    }

    private static void DrawStrokeRectangle(
        DrawingContext context,
        CanvasDisplayCommand command,
        Pen? pen)
    {
        var rect = ToRect(command.Rectangle);
        if (rect.Width == 0 && rect.Height == 0)
        {
            return;
        }

        if (pen is null)
        {
            return;
        }

        if (rect.Width == 0)
        {
            context.DrawLine(pen, rect.TopLeft, rect.BottomLeft);
        }
        else if (rect.Height == 0)
        {
            context.DrawLine(pen, rect.TopLeft, rect.TopRight);
        }
        else
        {
            context.DrawRectangle(null, pen, rect);
        }
    }

    private static void DrawText(
        DrawingContext context,
        CanvasDisplayCommand command,
        CanvasStateSnapshot state,
        CanvasDrawingSurface owner,
        IImmutableBrush? brush)
    {
        if (string.IsNullOrEmpty(command.Text) || brush is null)
        {
            return;
        }

        var formatted = owner.GetCachedFormattedText(command.Text, state, brush);
        var origin = AdjustTextOrigin(command.Origin, formatted, state);
        if (Math.Abs(state.FontWidthScale - 1) < 0.0001)
        {
            context.DrawText(formatted, origin);
        }
        else
        {
            using (context.PushTransform(Matrix.CreateScale(state.FontWidthScale, 1)))
            {
                context.DrawText(formatted, new Point(origin.X / state.FontWidthScale, origin.Y));
            }
        }
    }

    private static Point AdjustTextOrigin(
        WebScene.Core.WebScenePoint authoredOrigin,
        FormattedText formatted,
        CanvasStateSnapshot state)
    {
        var x = authoredOrigin.X;
        var y = authoredOrigin.Y;
        switch (state.TextAlign)
        {
            case CanvasTextAlign.Center:
                x -= formatted.Width * state.FontWidthScale / 2;
                break;
            case CanvasTextAlign.Right:
            case CanvasTextAlign.End:
                x -= formatted.Width * state.FontWidthScale;
                break;
        }

        switch (state.TextBaseline)
        {
            case CanvasTextBaseline.Hanging:
                y += formatted.Baseline * 0.2;
                break;
            case CanvasTextBaseline.Middle:
                y -= formatted.Height / 2;
                break;
            case CanvasTextBaseline.Alphabetic:
                y -= formatted.Baseline;
                break;
            case CanvasTextBaseline.Bottom:
                y -= formatted.Height;
                break;
            case CanvasTextBaseline.Ideographic:
                y -= formatted.Height * 0.9;
                break;
        }

        return new Point(x, y);
    }

    private static void DrawImage(
        DrawingContext context,
        CanvasDisplayCommand command,
        CanvasDrawingSurface owner,
        Rect canvasRect)
    {
        if (!HasArea(command.SourceRectangle) || !HasArea(command.DestinationRectangle))
        {
            return;
        }

        var source = ToRect(command.SourceRectangle);
        var destination = ToRect(command.DestinationRectangle);
        if (command.Resource.TryGet<IImage>(out var image))
        {
            context.DrawImage(image!, source, destination);
            return;
        }

        if (!command.Resource.TryGet<CanvasDisplayListModel>(out var nestedDisplayList))
        {
            return;
        }

        var scaleX = destination.Width / source.Width;
        var scaleY = destination.Height / source.Height;
        var transform = new Matrix(
            scaleX,
            0,
            0,
            scaleY,
            destination.X - source.X * scaleX,
            destination.Y - source.Y * scaleY);

        using (context.PushClip(destination))
        using (context.PushTransform(transform))
        {
            Render(context, nestedDisplayList!, owner, source);
        }
    }

    private static Rect ToRect(WebScene.Core.WebSceneRect rect)
        => new(rect.X, rect.Y, rect.Width, rect.Height);

    private static bool HasArea(WebScene.Core.WebSceneRect rect)
        => rect.Width > 0 && rect.Height > 0;

    private sealed class CompiledCommand
    {
        public CompiledCommand(CanvasDisplayCommand command)
        {
            Command = command;
            var compiledState = s_compiledStates.GetValue(
                command.State,
                static state => new CompiledState(state));
            State = compiledState.State;
            StrokePen = compiledState.StrokePen;
            if (command.Path is not null)
            {
                PathGeometry = CanvasPathBuilder.BuildGeometry(command.Path);
            }

            if (command.Kind == CanvasDisplayCommandKind.ClearRectangle)
            {
                Geometry geometry = new RectangleGeometry(ToRect(command.Rectangle));
                if (!State.Transform.IsIdentity)
                {
                    geometry.Transform = new MatrixTransform(State.Transform);
                }

                if (State.ClipGeometry is not null)
                {
                    geometry = new CombinedGeometry(
                        GeometryCombineMode.Intersect,
                        geometry,
                        State.ClipGeometry);
                }

                ClearGeometry = geometry;
            }

            if (command.Kind == CanvasDisplayCommandKind.PutImageData
                && command.ImageData is { Width: > 0, Height: > 0 } imageData)
            {
                PixelBitmap = CanvasBitmapFactory.CreateWriteableBitmap(
                    imageData.Width,
                    imageData.Height,
                    imageData.RgbaPixels);
            }
        }

        public CanvasDisplayCommand Command { get; }
        public CanvasStateSnapshot State { get; }
        public Pen? StrokePen { get; }
        public Geometry? PathGeometry { get; }
        public Geometry? ClearGeometry { get; }
        public WriteableBitmap? PixelBitmap { get; }
    }

    private sealed class CompiledState
    {
        public CompiledState(CanvasStateModel model)
        {
            State = new CanvasStateSnapshot(model);
            StrokePen = State.CreatePen();
        }

        public CanvasStateSnapshot State { get; }
        public Pen? StrokePen { get; }
    }
}

using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using WebScene.JavaScript;

namespace JavaScript.Avalonia;



internal sealed class CanvasDrawingSurface : Control
{
    private static readonly bool s_disableFillRectCoalescing =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_FILL_RECT_COALESCING"), "1", StringComparison.Ordinal);
    private static readonly bool s_enableCanvasInvalidationSuppression =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_ENABLE_CANVAS_INVALIDATION_SUPPRESSION"),
            "1",
            StringComparison.Ordinal);
    private static readonly bool s_enableNativeCanvasHotPath =
        !string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_AVALONIA_NATIVE_CANVAS_HOTPATH"),
            "1",
            StringComparison.Ordinal);
    private static readonly bool s_disableLineDashReplayDeduplication =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CANVAS_LINE_DASH_REPLAY_DEDUP"),
            "1",
            StringComparison.Ordinal);

    private readonly List<CanvasDrawCommand> _commands = new();
    private CanvasDisplayListModel _portableDisplayList = new();
    private bool _portableDisplayListDirty;
    private bool _portableDisplayListObserved;
    private readonly List<CanvasTextMeasurement> _textMeasurements = new();
    private readonly Dictionary<CanvasTextLayoutKey, FormattedText> _formattedTextCache = new();
    private CanvasRenderingContext2D? _context;
    private CanvasWebGlFrame? _webGlFrame;
    private bool _visualInvalidated;
    private long _renderTicks;
    private long _renderCount;
    private long _renderedCommandCount;
    private long _fullClearCount;
    private long _partialClearCount;
    private int _logicalCommandCount;

    private double _virtualWidth = 300;
    private double _virtualHeight = 150;

    public double VirtualWidth
    {
        get => _virtualWidth;
        set => _virtualWidth = value;
    }

    public double VirtualHeight
    {
        get => _virtualHeight;
        set => _virtualHeight = value;
    }

    internal IReadOnlyList<CanvasDrawCommand> Commands => _commands;
    internal static bool EnableNativeCanvasHotPath => s_enableNativeCanvasHotPath;
    internal static bool EnableLineDashReplayDeduplication => !s_disableLineDashReplayDeduplication;
    internal CanvasDisplayListModel PortableDisplayList
    {
        get
        {
            EnsurePortableDisplayList();
            _portableDisplayListObserved = true;
            return _portableDisplayList;
        }
    }
    internal int LogicalCommandCount => _logicalCommandCount;
    internal IReadOnlyList<CanvasTextMeasurement> TextMeasurements => _textMeasurements;
    internal string LastWebGlRenderBackend { get; private set; } = "not rendered";
    internal long RenderCount => _renderCount;
    internal long RenderedCommandCount => _renderedCommandCount;
    internal TimeSpan RenderDuration => TimeSpan.FromSeconds((double)_renderTicks / Stopwatch.Frequency);
    internal long FullClearCount => _fullClearCount;
    internal long PartialClearCount => _partialClearCount;
    internal IReadOnlyDictionary<string, CanvasCallMetric> FastCallMetrics =>
        _context?.FastCallMetrics ?? CanvasRenderingContext2D.EmptyFastCallMetrics;

    public CanvasDrawingSurface()
    {
        IsHitTestVisible = false;
        Focusable = false;
        ClipToBounds = true;
    }

    internal CanvasRenderingContext2D Context => _context ??= new CanvasRenderingContext2D(this);


    internal void AddCommand(CanvasDrawCommand command)
    {
        if (command is null)
        {
            return;
        }

        _commands.Add(command);
        if (s_enableNativeCanvasHotPath && !_portableDisplayListObserved)
        {
            _portableDisplayListDirty = true;
        }
        else
        {
            _portableDisplayList.AddRetained(command.PortableCommand);
        }
        _logicalCommandCount += command.LogicalCommandCount;
        InvalidateCanvasVisual();
    }

    internal bool TryAppendFillRect(Rect rect, CanvasStateSnapshot state)
    {
        if (s_disableFillRectCoalescing
            || _commands.Count == 0
            || _commands[^1] is not FillRectCommand fillRect
            || !fillRect.TryAppend(rect, state))
        {
            return false;
        }

        _logicalCommandCount++;
        if (s_enableNativeCanvasHotPath && !_portableDisplayListObserved)
        {
            _portableDisplayListDirty = true;
        }
        else
        {
            _portableDisplayList.AddRetained(fillRect.CreatePortableCommand(rect));
        }
        InvalidateCanvasVisual();
        return true;
    }

    internal void RecordTextMeasurement(string text, string font, double width)
    {
        const int limit = 2048;
        if (_textMeasurements.Count == limit)
        {
            _textMeasurements.RemoveRange(0, limit / 4);
        }

        _textMeasurements.Add(new CanvasTextMeasurement(text, font, width));
    }

    internal void ClearAll()
    {
        _fullClearCount++;
        if (_commands.Count == 0)
        {
            _portableDisplayList = new CanvasDisplayListModel();
            _portableDisplayListDirty = false;
            _logicalCommandCount = 0;
            return;
        }

        _commands.Clear();
        _portableDisplayList = new CanvasDisplayListModel();
        _portableDisplayListDirty = false;
        _logicalCommandCount = 0;
        InvalidateCanvasVisual();
    }

    internal void ClearRect(Rect rect, CanvasStateSnapshot state)
    {
        if (rect.Width <= 0 || rect.Height <= 0)
        {
            return;
        }

        _partialClearCount++;
        AddCommand(new CanvasClearRectCommand(rect, state));
    }

    internal void SetWebGlFrame(CanvasWebGlFrame? frame)
    {
        _webGlFrame = frame;
        InvalidateCanvasVisual();
    }

    internal void SetWebGlRenderBackend(string backend)
    {
        LastWebGlRenderBackend = backend;
    }

    internal static bool CollectRenderMetrics { get; set; }

    public override void Render(DrawingContext context)
    {
        if (!CollectRenderMetrics)
        {
            RenderCore(context);
            return;
        }

        var started = Stopwatch.GetTimestamp();
        try
        {
            RenderCore(context);
        }
        finally
        {
            _renderCount++;
            _renderedCommandCount += LogicalCommandCount;
            _renderTicks += Stopwatch.GetTimestamp() - started;
        }
    }

    private void RenderCore(DrawingContext context)
    {
        base.Render(context);
        _visualInvalidated = false;

        var bounds = new Rect(Bounds.Size);
        if (bounds.Width <= 0 || bounds.Height <= 0)
        {
            return;
        }

        // AdornerLayer clipping follows explicit clip ancestors; the canvas must
        // also clip its own Render output so oversized draws cannot cover siblings.
        using (context.PushClip(bounds))
        {
            // HTML canvas is an atomic hit-test box even when every pixel is
            // transparent. Avalonia otherwise hit-tests only retained drawing
            // content, allowing a painted backing canvas to win over a later
            // transparent interaction canvas at the same bounds.
            context.DrawRectangle(Brushes.Transparent, null, bounds);
            if (VirtualWidth > 0 && VirtualHeight > 0)
            {
                double scaleX = bounds.Width / VirtualWidth;
                double scaleY = bounds.Height / VirtualHeight;
                bool needsScale = Math.Abs(scaleX - 1.0) > 0.001 || Math.Abs(scaleY - 1.0) > 0.001;
                if (needsScale)
                {
                    // Only push a non-identity scale when virtual backing intentionally differs from DIP layout size
                    // (e.g. high-dpi compensation path). When they match we draw 1:1 to avoid squish.
                    using (context.PushTransform(Matrix.CreateScale(scaleX, scaleY)))
                    {
                        RenderCommands(context);
                    }
                }
                else
                {
                    RenderCommands(context);
                }
            }
            else
            {
                RenderCommands(context);
            }

            if (_webGlFrame is not null)
            {
                context.Custom(new CanvasWebGlDrawOperation(this, bounds, _webGlFrame));
            }
        }
    }

    internal void RenderCommands(DrawingContext context)
    {
        var canvasRect = new Rect(0, 0, VirtualWidth, VirtualHeight);
        if (s_enableNativeCanvasHotPath && !_portableDisplayListObserved)
        {
            RenderCommandList(context, _commands, canvasRect);
            return;
        }

        EnsurePortableDisplayList();
        AvaloniaCanvasDisplayListRenderer.Render(context, _portableDisplayList, this, canvasRect);
    }

    internal byte[] CapturePng()
    {
        var width = Math.Max(1, (int)Math.Ceiling(VirtualWidth));
        var height = Math.Max(1, (int)Math.Ceiling(VirtualHeight));
        using var bitmap = new RenderTargetBitmap(new PixelSize(width, height), new Vector(96, 96));
        using (var context = bitmap.CreateDrawingContext(clear: true))
        {
            if (Commands.Count == 0
                && Context.TryGetRgbaPixels(out var pixelWidth, out var pixelHeight, out var pixels))
            {
                using var pixelBitmap = CanvasBitmapFactory.CreateWriteableBitmap(pixelWidth, pixelHeight, pixels);
                context.DrawImage(
                    pixelBitmap,
                    new Rect(0, 0, pixelWidth, pixelHeight),
                    new Rect(0, 0, width, height));
            }
            else
            {
                RenderCommands(context);
            }
        }

        using var stream = new MemoryStream();
        bitmap.Save(stream);
        return stream.ToArray();
    }

    internal static void RenderCommandList(
        DrawingContext context,
        IReadOnlyList<CanvasDrawCommand> commands,
        Rect canvasRect)
    {
        if (commands.Count == 0)
        {
            return;
        }

        // Most chart frames are complete redraws. Once a full opaque fill or
        // clear has superseded the old command list, there is no partial-clear
        // geometry to resolve and allocating a Geometry slot for every command
        // only adds per-frame GC pressure.
        var hasPartialClear = false;
        for (var i = 0; i < commands.Count; i++)
        {
            if (commands[i] is CanvasClearRectCommand)
            {
                hasPartialClear = true;
                break;
            }
        }

        if (!hasPartialClear)
        {
            for (var i = 0; i < commands.Count; i++)
            {
                commands[i].Render(context);
            }

            return;
        }

        var clearedAfter = new Geometry?[commands.Count];
        Geometry? cleared = null;
        for (var i = commands.Count - 1; i >= 0; i--)
        {
            if (commands[i] is CanvasClearRectCommand clear)
            {
                var clearGeometry = clear.ClearGeometry;
                cleared = cleared is null
                    ? clearGeometry
                    : new CombinedGeometry(GeometryCombineMode.Union, cleared, clearGeometry);
                continue;
            }
            clearedAfter[i] = cleared;
        }

        for (var i = 0; i < commands.Count; i++)
        {
            var command = commands[i];
            if (command is CanvasClearRectCommand)
            {
                continue;
            }

            if (clearedAfter[i] is not { } exclusion)
            {
                command.Render(context);
                continue;
            }

            var drawable = new CombinedGeometry(
                GeometryCombineMode.Exclude,
                new RectangleGeometry(canvasRect),
                exclusion);
            using (context.PushGeometryClip(drawable))
            {
                command.Render(context);
            }
        }
    }

    internal void InvalidateCanvasVisual()
    {
        // Avalonia already coalesces visual invalidations. The additional
        // WebScene flag can span an intermediate compositor render while a
        // Canvas2D frame is still being replayed, suppressing the final damage
        // request and leaving correctly updated retained commands only partly
        // visible. Keep the old path solely as a diagnostic control.
        if (s_enableCanvasInvalidationSuppression && _visualInvalidated)
        {
            return;
        }

        _visualInvalidated = true;
        InvalidateVisual();
    }

    /// <summary>
    /// Scales coordinate values in existing draw commands by the given factor.
    /// Used when correcting virtual (backing) size for chart layers so that render matches DIP layout * dpr.
    /// </summary>
    internal void ScaleCommands(double factor)
    {
        if (!double.IsFinite(factor) || factor == 1.0 || factor <= 0)
            return;

        foreach (var cmd in _commands)
        {
            cmd.Scale(factor);
        }
        if (s_enableNativeCanvasHotPath && !_portableDisplayListObserved)
        {
            _portableDisplayListDirty = true;
        }
        else
        {
            RebuildPortableDisplayList();
        }
        InvalidateCanvasVisual();
    }

    private void EnsurePortableDisplayList()
    {
        if (_portableDisplayListDirty)
        {
            RebuildPortableDisplayList();
        }
    }

    private void RebuildPortableDisplayList()
    {
        _portableDisplayList = CreatePortableDisplayListSnapshot(_commands);
        _portableDisplayListDirty = false;
    }

    internal static CanvasDisplayListModel CreatePortableDisplayListSnapshot(
        IReadOnlyList<CanvasDrawCommand> commands)
    {
        var displayList = new CanvasDisplayListModel();
        foreach (var command in commands)
        {
            foreach (var portableCommand in command.GetPortableCommands())
            {
                displayList.AddRetained(portableCommand);
            }
        }
        return displayList;
    }

    internal FormattedText CreateFormattedText(string text, CanvasStateSnapshot state)
        => GetCachedFormattedText(text, state, state.FillBrush);

    internal FormattedText GetCachedFormattedText(string text, CanvasStateSnapshot state, IImmutableBrush? brush)
    {
        if (brush is null)
        {
            return new FormattedText(
                text ?? string.Empty,
                CultureInfo.CurrentCulture,
                FlowDirection.LeftToRight,
                state.Typeface,
                state.FontSize,
                null);
        }

        var key = new CanvasTextLayoutKey(text ?? string.Empty, state.Typeface, state.FontSize, brush);
        if (_formattedTextCache.TryGetValue(key, out var cached))
        {
            return cached;
        }

        if (_formattedTextCache.Count >= 2048)
        {
            _formattedTextCache.Clear();
        }

        var formatted = new FormattedText(
            text ?? string.Empty,
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            state.Typeface,
            state.FontSize,
            brush);
        formatted.SetForegroundBrush(brush);
        _formattedTextCache[key] = formatted;
        return formatted;
    }

    private readonly record struct CanvasTextLayoutKey(
        string Text,
        Typeface Typeface,
        double FontSize,
        IImmutableBrush Brush);
}

internal abstract class CanvasDrawCommand
{
    private CanvasDisplayCommand? _portableCommand;

    protected CanvasDrawCommand(CanvasStateSnapshot state)
    {
        State = state;
    }

    protected CanvasStateSnapshot State { get; }

    internal virtual void Scale(double factor) { }

    internal CanvasStateSnapshot Snapshot => State;

    internal CanvasDisplayCommand PortableCommand
        => _portableCommand ??= CreatePortableCommand();

    protected abstract CanvasDisplayCommand CreatePortableCommand();

    protected void InvalidatePortableCommand() => _portableCommand = null;

    internal virtual IEnumerable<CanvasDisplayCommand> GetPortableCommands()
    {
        yield return PortableCommand;
    }

    protected static WebScene.Core.WebSceneRect ToPortableRect(Rect rect)
        => new(rect.X, rect.Y, rect.Width, rect.Height);

    protected static WebScene.Core.WebScenePoint ToPortablePoint(Point point)
        => new(point.X, point.Y);

    internal virtual int LogicalCommandCount => 1;

    internal virtual void AppendDiagnosticHash(ref HashCode hash)
    {
        hash.Add(GetType().Name, StringComparer.Ordinal);
        hash.Add(GetBounds());
    }

    public void Render(DrawingContext context)
    {
        IDisposable? transform = null;
        IDisposable? clip = null;
        IDisposable? opacity = null;

        try
        {
            if (State.ClipGeometry is not null)
            {
                clip = context.PushGeometryClip(State.ClipGeometry);
            }

            if (!State.Transform.IsIdentity)
            {
                transform = context.PushTransform(State.Transform);
            }

            if (State.GlobalAlpha < 1.0)
            {
                opacity = context.PushOpacity(State.GlobalAlpha);
            }

            RenderCore(context);
        }
        finally
        {
            opacity?.Dispose();
            transform?.Dispose();
            clip?.Dispose();
        }
    }

    protected abstract void RenderCore(DrawingContext context);

    public virtual Rect? GetBounds() => null;
}

internal sealed class CanvasClearRectCommand : CanvasDrawCommand
{
    private Rect _rect;

    public CanvasClearRectCommand(Rect rect, CanvasStateSnapshot state)
        : base(state)
    {
        _rect = rect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.ClearRectangle,
            State.Model,
            Rectangle: ToPortableRect(_rect));

    public Geometry ClearGeometry
    {
        get
        {
            Geometry geometry = new RectangleGeometry(_rect);
            if (!State.Transform.IsIdentity)
            {
                geometry.Transform = new MatrixTransform(State.Transform);
            }
            if (State.ClipGeometry is not null)
            {
                geometry = new CombinedGeometry(GeometryCombineMode.Intersect, geometry, State.ClipGeometry);
            }
            return geometry;
        }
    }

    protected override void RenderCore(DrawingContext context)
    {
        // Clear commands affect the visibility of earlier commands and are
        // applied by CanvasDrawingSurface.RenderCommandList.
    }

    public override Rect? GetBounds() => _rect;

    internal override void Scale(double factor)
    {
        if (factor != 1.0 && double.IsFinite(factor))
        {
            _rect = new Rect(_rect.X * factor, _rect.Y * factor, _rect.Width * factor, _rect.Height * factor);
            InvalidatePortableCommand();
        }
    }
}

internal sealed class FillRectCommand : CanvasDrawCommand
{
    private Rect _rect;
    private List<Rect>? _additionalRects;

    public FillRectCommand(Rect rect, CanvasStateSnapshot state)
        : base(state)
    {
        _rect = rect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => CreatePortableCommand(_rect);

    protected override void RenderCore(DrawingContext context)
    {
        if (State.FillBrush is not null && _rect.Width > 0 && _rect.Height > 0)
        {
            context.FillRectangle(State.FillBrush, _rect);
            if (_additionalRects is not null)
            {
                foreach (var rect in _additionalRects)
                {
                    context.FillRectangle(State.FillBrush, rect);
                }
            }
        }
    }

    internal int RectangleCount => 1 + (_additionalRects?.Count ?? 0);

    internal IEnumerable<Rect> Rectangles
    {
        get
        {
            yield return _rect;
            if (_additionalRects is not null)
            {
                foreach (var rect in _additionalRects)
                {
                    yield return rect;
                }
            }
        }
    }

    internal override int LogicalCommandCount => RectangleCount;

    internal bool TryAppend(Rect rect, CanvasStateSnapshot state)
    {
        if (!State.HasSameRenderingState(state))
        {
            return false;
        }

        (_additionalRects ??= new List<Rect>(8)).Add(rect);
        return true;
    }

    internal CanvasDisplayCommand CreatePortableCommand(Rect rect)
        => new(
            CanvasDisplayCommandKind.FillRectangle,
            State.Model,
            Rectangle: ToPortableRect(rect));

    internal override IEnumerable<CanvasDisplayCommand> GetPortableCommands()
    {
        foreach (var rect in Rectangles)
        {
            yield return CreatePortableCommand(rect);
        }
    }

    internal override void AppendDiagnosticHash(ref HashCode hash)
    {
        foreach (var rect in Rectangles)
        {
            hash.Add(nameof(FillRectCommand), StringComparer.Ordinal);
            hash.Add((Rect?)rect);
        }
    }

    public override Rect? GetBounds()
    {
        if (_additionalRects is null)
        {
            return _rect;
        }

        var left = _rect.Left;
        var top = _rect.Top;
        var right = _rect.Right;
        var bottom = _rect.Bottom;
        foreach (var rect in _additionalRects)
        {
            left = Math.Min(left, rect.Left);
            top = Math.Min(top, rect.Top);
            right = Math.Max(right, rect.Right);
            bottom = Math.Max(bottom, rect.Bottom);
        }
        return new Rect(left, top, right - left, bottom - top);
    }

    internal override void Scale(double factor)
    {
        if (factor == 1.0 || !double.IsFinite(factor))
        {
            return;
        }

        _rect = ScaleRect(_rect, factor);
        InvalidatePortableCommand();
        if (_additionalRects is not null)
        {
            for (var index = 0; index < _additionalRects.Count; index++)
            {
                _additionalRects[index] = ScaleRect(_additionalRects[index], factor);
            }
        }
    }

    private static Rect ScaleRect(Rect rect, double factor)
        => new(rect.X * factor, rect.Y * factor, rect.Width * factor, rect.Height * factor);
}

internal sealed class StrokeRectCommand : CanvasDrawCommand
{
    private Rect _rect;

    public StrokeRectCommand(Rect rect, CanvasStateSnapshot state)
        : base(state)
    {
        _rect = rect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.StrokeRectangle,
            State.Model,
            Rectangle: ToPortableRect(_rect));

    protected override void RenderCore(DrawingContext context)
    {
        if (_rect.Width == 0 && _rect.Height == 0)
        {
            return;
        }

        var pen = State.CreatePen();
        if (pen is null)
        {
            return;
        }

        if (_rect.Width == 0)
        {
            context.DrawLine(pen, _rect.TopLeft, _rect.BottomLeft);
        }
        else if (_rect.Height == 0)
        {
            context.DrawLine(pen, _rect.TopLeft, _rect.TopRight);
        }
        else
        {
            context.DrawRectangle(null, pen, _rect);
        }
    }

    public override Rect? GetBounds()
    {
        var pen = State.CreatePen();
        return pen is null ? _rect : _rect.Inflate(pen.Thickness / 2);
    }
}

internal sealed class FillPathCommand : CanvasDrawCommand
{
    private readonly CanvasPathModel _path;
    private Geometry? _geometry;

    public FillPathCommand(CanvasPathModel path, CanvasStateSnapshot state)
        : base(state)
    {
        ArgumentNullException.ThrowIfNull(path);
        _path = path.Snapshot();
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.FillPath,
            State.Model,
            Path: _path);

    protected override void RenderCore(DrawingContext context)
    {
        if (State.FillBrush is not null)
        {
            context.DrawGeometry(State.FillBrush, null, Geometry);
        }
    }

    internal Geometry Geometry
        => _geometry ??= CanvasPathBuilder.BuildGeometry(_path);

    public override Rect? GetBounds() => Geometry.Bounds;
}

internal sealed class StrokePathCommand : CanvasDrawCommand
{
    private readonly CanvasPathModel _path;
    private Geometry? _geometry;

    public StrokePathCommand(CanvasPathModel path, CanvasStateSnapshot state)
        : base(state)
    {
        ArgumentNullException.ThrowIfNull(path);
        _path = path.Snapshot();
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.StrokePath,
            State.Model,
            Path: _path);

    protected override void RenderCore(DrawingContext context)
    {
        var pen = State.CreatePen();
        if (pen is null)
        {
            return;
        }

        context.DrawGeometry(null, pen, Geometry);
    }

    internal Geometry Geometry
        => _geometry ??= CanvasPathBuilder.BuildGeometry(_path);

    public override Rect? GetBounds()
    {
        var pen = State.CreatePen();
        if (pen is null)
        {
            return Geometry.Bounds;
        }

        return Geometry.GetRenderBounds(pen);
    }
}

internal sealed class FillTextCommand : CanvasDrawCommand
{
    private readonly CanvasDrawingSurface _owner;
    private readonly string _text;
    private readonly Point _origin;
    private FormattedText? _formattedText;

    public FillTextCommand(CanvasDrawingSurface owner, string text, Point origin, CanvasStateSnapshot state)
        : base(state)
    {
        _owner = owner ?? throw new ArgumentNullException(nameof(owner));
        _text = text ?? string.Empty;
        _origin = origin;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.FillText,
            State.Model,
            Text: _text,
            Origin: ToPortablePoint(_origin));

    protected override void RenderCore(DrawingContext context)
    {
        if (string.IsNullOrEmpty(_text) || State.FillBrush is null)
        {
            return;
        }

        var formatted = GetFormattedText();
        var origin = AdjustOrigin(formatted);
        if (Math.Abs(State.FontWidthScale - 1d) < 0.0001)
        {
            context.DrawText(formatted, origin);
        }
        else
        {
            using (context.PushTransform(Matrix.CreateScale(State.FontWidthScale, 1d)))
            {
                context.DrawText(formatted, new Point(origin.X / State.FontWidthScale, origin.Y));
            }
        }
    }

    private Point AdjustOrigin(FormattedText formatted)
    {
        var x = _origin.X;
        var y = _origin.Y;

        switch (State.TextAlign)
        {
            case CanvasTextAlign.Center:
                x -= formatted.Width * State.FontWidthScale / 2;
                break;
            case CanvasTextAlign.Right:
            case CanvasTextAlign.End:
                x -= formatted.Width * State.FontWidthScale;
                break;
            case CanvasTextAlign.Left:
            case CanvasTextAlign.Start:
            default:
                break;
        }

        switch (State.TextBaseline)
        {
            case CanvasTextBaseline.Top:
                break;
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

    public override Rect? GetBounds()
    {
        if (string.IsNullOrEmpty(_text))
        {
            return null;
        }

        var formatted = GetFormattedText();

        var origin = AdjustOrigin(formatted);
        return new Rect(origin, new Size(formatted.Width * State.FontWidthScale, formatted.Height));
    }

    private FormattedText GetFormattedText()
    {
        if (_formattedText is not null)
        {
            return _formattedText;
        }

        return _formattedText = _owner.GetCachedFormattedText(_text, State, State.FillBrush);
    }
}

internal sealed class StrokeTextCommand : CanvasDrawCommand
{
    private readonly CanvasDrawingSurface _owner;
    private readonly string _text;
    private readonly Point _origin;
    private FormattedText? _formattedText;

    public StrokeTextCommand(CanvasDrawingSurface owner, string text, Point origin, CanvasStateSnapshot state)
        : base(state)
    {
        _owner = owner ?? throw new ArgumentNullException(nameof(owner));
        _text = text ?? string.Empty;
        _origin = origin;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.StrokeText,
            State.Model,
            Text: _text,
            Origin: ToPortablePoint(_origin));

    protected override void RenderCore(DrawingContext context)
    {
        if (string.IsNullOrEmpty(_text) || State.StrokeBrush is null)
        {
            return;
        }

        var formatted = GetFormattedText();
        var origin = AdjustOrigin(formatted);
        if (Math.Abs(State.FontWidthScale - 1d) < 0.0001)
        {
            context.DrawText(formatted, origin);
        }
        else
        {
            using (context.PushTransform(Matrix.CreateScale(State.FontWidthScale, 1d)))
            {
                context.DrawText(formatted, new Point(origin.X / State.FontWidthScale, origin.Y));
            }
        }
    }

    private Point AdjustOrigin(FormattedText formatted)
    {
        var x = _origin.X;
        var y = _origin.Y;

        switch (State.TextAlign)
        {
            case CanvasTextAlign.Center:
                x -= formatted.Width * State.FontWidthScale / 2;
                break;
            case CanvasTextAlign.Right:
            case CanvasTextAlign.End:
                x -= formatted.Width * State.FontWidthScale;
                break;
            case CanvasTextAlign.Left:
            case CanvasTextAlign.Start:
            default:
                break;
        }

        switch (State.TextBaseline)
        {
            case CanvasTextBaseline.Top:
                break;
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

    public override Rect? GetBounds()
    {
        if (string.IsNullOrEmpty(_text))
        {
            return null;
        }

        var formatted = GetFormattedText();

        return new Rect(AdjustOrigin(formatted), new Size(formatted.Width * State.FontWidthScale, formatted.Height));
    }

    private FormattedText GetFormattedText()
    {
        if (_formattedText is not null)
        {
            return _formattedText;
        }

        return _formattedText = _owner.GetCachedFormattedText(_text, State, State.StrokeBrush);
    }
}

internal sealed class DrawImageCommand : CanvasDrawCommand
{
    private readonly IImage _image;
    private readonly Rect _sourceRect;
    private readonly Rect _destinationRect;

    public DrawImageCommand(IImage image, Rect sourceRect, Rect destinationRect, CanvasStateSnapshot state)
        : base(state)
    {
        _image = image ?? throw new ArgumentNullException(nameof(image));
        _sourceRect = sourceRect;
        _destinationRect = destinationRect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.DrawImage,
            State.Model,
            SourceRectangle: ToPortableRect(_sourceRect),
            DestinationRectangle: ToPortableRect(_destinationRect),
            Resource: WebScene.Core.WebSceneBackendHandle.Create(_image));

    protected override void RenderCore(DrawingContext context)
    {
        context.DrawImage(_image, _sourceRect, _destinationRect);
    }

    public override Rect? GetBounds() => _destinationRect;
}

internal sealed class DrawRgbaPixelsCommand : CanvasDrawCommand
{
    private readonly CanvasImageDataModel _imageData;
    private readonly WriteableBitmap _bitmap;
    private readonly Rect _sourceRect;
    private readonly Rect _destinationRect;

    public DrawRgbaPixelsCommand(CanvasImageDataModel imageData, Rect sourceRect, Rect destinationRect, CanvasStateSnapshot state)
        : base(state)
    {
        _imageData = imageData ?? throw new ArgumentNullException(nameof(imageData));
        _bitmap = CanvasBitmapFactory.CreateWriteableBitmap(imageData.Width, imageData.Height, imageData.RgbaPixels);
        _sourceRect = sourceRect;
        _destinationRect = destinationRect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.PutImageData,
            State.Model,
            ImageData: _imageData,
            SourceRectangle: ToPortableRect(_sourceRect),
            DestinationRectangle: ToPortableRect(_destinationRect));

    internal CanvasImageDataModel ImageData => _imageData;

    protected override void RenderCore(DrawingContext context)
    {
        if (_sourceRect.Width <= 0 || _sourceRect.Height <= 0 || _destinationRect.Width <= 0 || _destinationRect.Height <= 0)
        {
            return;
        }

        context.DrawImage(_bitmap, _sourceRect, _destinationRect);
    }

    public override Rect? GetBounds() => _destinationRect;
}

internal sealed class DrawCanvasSurfaceCommand : CanvasDrawCommand
{
    private readonly IReadOnlyList<CanvasDrawCommand> _commands;
    private readonly Rect _sourceRect;
    private readonly Rect _destinationRect;

    public DrawCanvasSurfaceCommand(CanvasDrawingSurface surface, Rect sourceRect, Rect destinationRect, CanvasStateSnapshot state)
        : base(state)
    {
        ArgumentNullException.ThrowIfNull(surface);
        _commands = new List<CanvasDrawCommand>(surface.Commands);
        _sourceRect = sourceRect;
        _destinationRect = destinationRect;
    }

    protected override CanvasDisplayCommand CreatePortableCommand()
        => new(
            CanvasDisplayCommandKind.DrawImage,
            State.Model,
            SourceRectangle: ToPortableRect(_sourceRect),
            DestinationRectangle: ToPortableRect(_destinationRect),
            Resource: WebScene.Core.WebSceneBackendHandle.Create(
                CanvasDrawingSurface.CreatePortableDisplayListSnapshot(_commands)));

    protected override void RenderCore(DrawingContext context)
    {
        if (_sourceRect.Width <= 0 || _sourceRect.Height <= 0 || _destinationRect.Width <= 0 || _destinationRect.Height <= 0)
        {
            return;
        }

        var scaleX = _destinationRect.Width / _sourceRect.Width;
        var scaleY = _destinationRect.Height / _sourceRect.Height;
        var transform = new Matrix(
            scaleX,
            0,
            0,
            scaleY,
            _destinationRect.X - (_sourceRect.X * scaleX),
            _destinationRect.Y - (_sourceRect.Y * scaleY));

        using (context.PushClip(_destinationRect))
        using (context.PushTransform(transform))
        {
            CanvasDrawingSurface.RenderCommandList(context, _commands, _sourceRect);
        }
    }

    public override Rect? GetBounds() => _destinationRect;
}

public sealed class CanvasTextMetrics
{
    public CanvasTextMetrics(double width)
    {
        this.width = width;
    }

    public double width { get; }
}

public sealed class CanvasDomMatrix
{
    public CanvasDomMatrix(Matrix matrix)
    {
        a = matrix.M11;
        b = matrix.M12;
        c = matrix.M21;
        d = matrix.M22;
        e = matrix.M31;
        f = matrix.M32;
        m11 = a;
        m12 = b;
        m21 = c;
        m22 = d;
        m41 = e;
        m42 = f;
        is2D = true;
        isIdentity = matrix.IsIdentity;
    }

    public double a { get; }

    public double b { get; }

    public double c { get; }

    public double d { get; }

    public double e { get; }

    public double f { get; }

    public double m11 { get; }

    public double m12 { get; }

    public double m21 { get; }

    public double m22 { get; }

    public double m41 { get; }

    public double m42 { get; }

    public bool is2D { get; }

    public bool isIdentity { get; }

    public double[] toFloat32Array() => new[] { a, b, 0, 0, c, d, 0, 0, 0, 0, 1, 0, e, f, 0, 1 };

    public double[] toFloat64Array() => toFloat32Array();
}

public sealed class CanvasImageData
{
    public CanvasImageData(int width, int height)
        : this(width, height, new byte[Math.Max(0, width) * Math.Max(0, height) * 4])
    {
    }

    public CanvasImageData(int width, int height, byte[] data)
    {
        var normalizedWidth = Math.Max(0, width);
        var normalizedHeight = Math.Max(0, height);
        var expectedLength = checked(normalizedWidth * normalizedHeight * 4);
        var normalizedData = data ?? Array.Empty<byte>();
        if (normalizedData.Length != expectedLength)
        {
            var copy = new byte[expectedLength];
            normalizedData.AsSpan(0, Math.Min(normalizedData.Length, expectedLength)).CopyTo(copy);
            normalizedData = copy;
        }

        Model = new CanvasImageDataModel(normalizedWidth, normalizedHeight, normalizedData);
    }

    internal CanvasImageDataModel Model { get; }

    public int width => Model.Width;

    public int height => Model.Height;

    public byte[] data => Model.RgbaPixels;
}

public sealed class CanvasRenderingContext2D : IWebSceneCanvasTextTarget, IWebSceneCanvasBatchTarget
{
    private readonly CanvasDrawingSurface _owner;
    private readonly Dictionary<string, CanvasCallMetric> _fastCallMetrics = new(StringComparer.Ordinal);
    private readonly CanvasPathBuilder _path = new();
    private CanvasState _state = CanvasState.CreateDefault();
    private readonly Dictionary<CanvasStateCacheKey, CanvasStateSnapshot> _stateSnapshotCache = new();
    private readonly Stack<CanvasState> _stateStack = new();
    private byte[]? _rgbaPixels;
    private int _rgbaWidth;
    private int _rgbaHeight;

    internal CanvasRenderingContext2D(CanvasDrawingSurface owner)
    {
        _owner = owner ?? throw new ArgumentNullException(nameof(owner));
    }

    internal static IReadOnlyDictionary<string, CanvasCallMetric> EmptyFastCallMetrics { get; } =
        new Dictionary<string, CanvasCallMetric>();

    internal IReadOnlyDictionary<string, CanvasCallMetric> FastCallMetrics => _fastCallMetrics;

    public object? canvas { get; set; }

    internal int CommandCount => _owner.LogicalCommandCount;

    public object? fillStyle
    {
        get => _state.FillStyleValue;
        set => _state.SetFillStyle(value);
    }

    public object? strokeStyle
    {
        get => _state.StrokeStyleValue;
        set => _state.SetStrokeStyle(value);
    }

    public double lineWidth
    {
        get => _state.LineWidth;
        set => _state.LineWidth = value > 0 ? value : _state.LineWidth;
    }

    public string lineCap
    {
        get => _state.LineCap.ToString().ToLowerInvariant();
        set => _state.LineCap = CanvasStrokeParser.ParseLineCap(value, _state.LineCap);
    }

    public string lineJoin
    {
        get => _state.LineJoin.ToString().ToLowerInvariant();
        set => _state.LineJoin = CanvasStrokeParser.ParseLineJoin(value, _state.LineJoin);
    }

    public double miterLimit
    {
        get => _state.MiterLimit;
        set => _state.MiterLimit = value > 0 ? value : _state.MiterLimit;
    }

    public double globalAlpha
    {
        get => _state.GlobalAlpha;
        set => _state.GlobalAlpha = Math.Clamp(value, 0.0, 1.0);
    }

    public double lineDashOffset
    {
        get => _state.LineDashOffset;
        set => _state.LineDashOffset = value;
    }

    public string font
    {
        get => _state.Font;
        set => _state.UpdateFont(value, GetFontRegistry());
    }

    private CssFontFaceRegistry? GetFontRegistry()
        => canvas is AvaloniaDomElement element ? element.ownerDocument.FontFaces : null;

    public string textAlign
    {
        get => CanvasTextParser.ToString(_state.TextAlign);
        set => _state.TextAlign = CanvasTextParser.ParseAlign(value, _state.TextAlign);
    }

    public string textBaseline
    {
        get => CanvasTextParser.ToString(_state.TextBaseline);
        set => _state.TextBaseline = CanvasTextParser.ParseBaseline(value, _state.TextBaseline);
    }

    public bool imageSmoothingEnabled
    {
        get => _state.ImageSmoothingEnabled;
        set => _state.ImageSmoothingEnabled = value;
    }

    public string imageSmoothingQuality
    {
        get => _state.ImageSmoothingQuality;
        set
        {
            if (value is "low" or "medium" or "high")
            {
                _state.ImageSmoothingQuality = value;
            }
        }
    }

    public string globalCompositeOperation
    {
        get => _state.CompositeOperation;
        set => _state.CompositeOperation = value ?? "source-over";
    }

    public string shadowColor
    {
        get => _state.ShadowColor;
        set => _state.ShadowColor = value ?? string.Empty;
    }

    public double shadowBlur
    {
        get => _state.ShadowBlur;
        set => _state.ShadowBlur = value;
    }

    public double shadowOffsetX
    {
        get => _state.ShadowOffsetX;
        set => _state.ShadowOffsetX = value;
    }

    public double shadowOffsetY
    {
        get => _state.ShadowOffsetY;
        set => _state.ShadowOffsetY = value;
    }

    public double[] mozCurrentTransform => ToCanvasMatrix(_state.Transform);

    public double[] mozCurrentTransformInverse => ToCanvasMatrix(InvertOrIdentity(_state.Transform));

    public void save()
    {
        _stateStack.Push(_state.Clone());
    }

    public void restore()
    {
        if (_stateStack.Count > 0)
        {
            _state = _stateStack.Pop();
        }
    }

    public void resetTransform()
    {
        _state.Transform = Matrix.Identity;
    }

    public void setTransform(double a, double b, double c, double d, double e, double f)
    {
        _state.Transform = new Matrix(a, b, c, d, e, f);
    }

    public void setTransform(object? transform)
    {
        if (CanvasMatrixParser.TryReadMatrix(transform, out var matrix))
        {
            _state.Transform = matrix;
        }
    }

    public void transform(double a, double b, double c, double d, double e, double f)
    {
        var m = new Matrix(a, b, c, d, e, f);
        // Canvas2D post-multiplies its current transform in column-vector
        // notation. Avalonia matrices transform row vectors, so the equivalent
        // composition order is reversed.
        _state.Transform = m * _state.Transform;
    }

    public void translate(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        var translation = Matrix.CreateTranslation(x, y);
        _state.Transform = translation * _state.Transform;
    }

    public void scale(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        var scale = Matrix.CreateScale(x, y);
        _state.Transform = scale * _state.Transform;
    }

    public void rotate(double angle)
    {
        if (!double.IsFinite(angle))
        {
            return;
        }

        var rotation = Matrix.CreateRotation(angle);
        _state.Transform = rotation * _state.Transform;
    }

    public void beginPath()
    {
        _path.Clear();
    }

    public void closePath()
    {
        _path.ClosePath();
    }

    public void moveTo(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        _path.MoveTo(x, y);
    }

    public void lineTo(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        _path.LineTo(x, y);
    }

    public void bezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y)
    {
        _path.CubicBezierTo(cp1x, cp1y, cp2x, cp2y, x, y);
    }

    public void quadraticCurveTo(double cpx, double cpy, double x, double y)
    {
        _path.QuadraticBezierTo(cpx, cpy, x, y);
    }

    public void arc(double x, double y, double radius, double startAngle, double endAngle, bool counterclockwise = false)
    {
        _path.Arc(x, y, radius, startAngle, endAngle, counterclockwise);
    }

    public void arcTo(double x1, double y1, double x2, double y2, double radius)
    {
        _path.ArcTo(x1, y1, x2, y2, radius);
    }

    public void rect(double x, double y, double width, double height)
    {
        _path.Rect(x, y, width, height);
    }

    public void clip()
    {
        if (_path.IsEmpty)
        {
            return;
        }

        var retainedPath = _path.Model.Snapshot();
        _state.Clips.Add(new CanvasClipModel(
            retainedPath,
            CanvasState.ToPortableTransform(_state.Transform)));

        Geometry geometry = CanvasPathBuilder.BuildGeometry(retainedPath);
        if (!_state.Transform.IsIdentity)
        {
            geometry.Transform = new MatrixTransform(_state.Transform);
        }
        _state.ClipGeometry = _state.ClipGeometry is null
            ? geometry
            : new CombinedGeometry(GeometryCombineMode.Intersect, _state.ClipGeometry, geometry);
    }

    public void clip(string fillRule)
    {
        clip();
    }

    public void clip(object? pathOrFillRule)
    {
        clip();
    }

    public void clip(object? path, object? fillRule)
    {
        clip();
    }

    public void setLineDash(object? segments)
    {
        SetLineDash(CanvasStrokeParser.ParseLineDash(segments));
    }

    internal void SetLineDash(ReadOnlySpan<double> segments)
    {
        var validCount = 0;
        for (var index = 0; index < segments.Length; index++)
        {
            if (double.IsFinite(segments[index]) && segments[index] >= 0)
            {
                validCount++;
            }
        }

        if (_state.LineDash.Length == validCount)
        {
            var currentIndex = 0;
            var equal = true;
            for (var index = 0; index < segments.Length; index++)
            {
                var segment = segments[index];
                if (!double.IsFinite(segment) || segment < 0)
                {
                    continue;
                }

                if (_state.LineDash[currentIndex++] != segment)
                {
                    equal = false;
                    break;
                }
            }

            if (equal)
            {
                return;
            }
        }

        if (validCount == 0)
        {
            _state.LineDash = Array.Empty<double>();
            return;
        }

        var normalized = new double[validCount];
        var destination = 0;
        for (var index = 0; index < segments.Length; index++)
        {
            var segment = segments[index];
            if (double.IsFinite(segment) && segment >= 0)
            {
                normalized[destination++] = segment;
            }
        }

        _state.LineDash = normalized;
    }

    public double[] getLineDash()
    {
        return _state.LineDash.Length == 0 ? Array.Empty<double>() : (double[])_state.LineDash.Clone();
    }

    public CanvasGradient createLinearGradient(double x0, double y0, double x1, double y1)
    {
        return new CanvasLinearGradient(x0, y0, x1, y1);
    }

    public CanvasGradient createRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1)
    {
        return new CanvasRadialGradient(x0, y0, r0, x1, y1, r1);
    }

    public CanvasPattern? createPattern(object image, string repetition)
    {
        if (!TryCreatePatternSource(image, out var source, out var width, out var height))
        {
            return null;
        }

        return new CanvasPattern(source, width, height, repetition);
    }

    public CanvasPattern? createPattern(object image, object? repetition)
        => createPattern(image, Convert.ToString(repetition, CultureInfo.InvariantCulture) ?? "repeat");

    public void stroke()
    {
        if (_path.IsEmpty)
        {
            return;
        }

        _owner.AddCommand(new StrokePathCommand(_path.Model, CreateSnapshot()));
    }

    // Libraries can pass either the active path or a retained Path2D object.
    public void stroke(object? path)
    {
        if (CanvasPath2D.TryResolve(path, out var path2D))
        {
            _owner.AddCommand(new StrokePathCommand(path2D.Model, CreateSnapshot()));
            return;
        }

        stroke();
    }

    public void stroke(object? path, object? options)
    {
        stroke(path);
    }

    public void fill()
    {
        if (_path.IsEmpty)
        {
            return;
        }

        _owner.AddCommand(new FillPathCommand(_path.Model, CreateSnapshot()));
    }

    public void fill(object? pathOrFillRule)
    {
        if (CanvasPath2D.TryResolve(pathOrFillRule, out var path2D))
        {
            _owner.AddCommand(new FillPathCommand(path2D.Model, CreateSnapshot()));
            return;
        }

        fill();
    }

    public void fill(object? path, object? fillRule)
    {
        fill(path);
    }

    public void fillRect(double x, double y, double width, double height)
    {
        if (!TryCreateFiniteRect(x, y, width, height, allowSingleDegenerateDimension: false, out var rect))
        {
            return;
        }

        if (CanDiscardPriorCommandsForFullFill(rect))
        {
            _owner.ClearAll();
        }

        var state = CreateSnapshot();
        if (!_owner.TryAppendFillRect(rect, state))
        {
            _owner.AddCommand(new FillRectCommand(rect, state));
        }
    }

    public void strokeRect(double x, double y, double width, double height)
    {
        if (!TryCreateFiniteRect(x, y, width, height, allowSingleDegenerateDimension: true, out var rect))
        {
            return;
        }

        _owner.AddCommand(new StrokeRectCommand(rect, CreateSnapshot()));
    }

    public void clearRect(double x, double y, double width, double height)
    {
        if (!TryCreateFiniteRect(x, y, width, height, allowSingleDegenerateDimension: false, out var rect))
        {
            return;
        }

        ClearRgbaPixels(rect);

        var canvasRect = new Rect(0, 0, GetCanvasPixelWidth(0), GetCanvasPixelHeight(0));
        if (_state.Clips.Count == 0 && TransformedRectCoversCanvas(rect, _state.Transform, canvasRect))
        {
            _owner.ClearAll();
            return;
        }

        _owner.ClearRect(rect, CreateSnapshot());
    }

    private static bool TryCreateFiniteRect(
        double x,
        double y,
        double width,
        double height,
        bool allowSingleDegenerateDimension,
        out Rect rect)
    {
        rect = default;
        if (!double.IsFinite(x)
            || !double.IsFinite(y)
            || !double.IsFinite(width)
            || !double.IsFinite(height)
            || (allowSingleDegenerateDimension
                ? width == 0 && height == 0
                : width == 0 || height == 0))
        {
            return false;
        }

        if (width < 0)
        {
            x += width;
            width = -width;
        }

        if (height < 0)
        {
            y += height;
            height = -height;
        }

        if (!double.IsFinite(x)
            || !double.IsFinite(y)
            || !double.IsFinite(width)
            || !double.IsFinite(height))
        {
            return false;
        }

        rect = new Rect(x, y, width, height);
        return true;
    }

    private static bool TransformedRectCoversCanvas(Rect rect, Matrix transform, Rect canvasRect)
    {
        const double epsilon = 0.001;
        if (Math.Abs(transform.M12) > epsilon || Math.Abs(transform.M21) > epsilon)
        {
            return false;
        }

        var first = transform.Transform(rect.TopLeft);
        var second = transform.Transform(rect.BottomRight);
        var left = Math.Min(first.X, second.X) - epsilon;
        var top = Math.Min(first.Y, second.Y) - epsilon;
        var right = Math.Max(first.X, second.X) + epsilon;
        var bottom = Math.Max(first.Y, second.Y) + epsilon;
        return left <= canvasRect.Left
               && top <= canvasRect.Top
               && right >= canvasRect.Right
               && bottom >= canvasRect.Bottom;
    }

    private bool CanDiscardPriorCommandsForFullFill(Rect rect)
    {
        var replacesCanvas = string.Equals(globalCompositeOperation, "source-over", StringComparison.OrdinalIgnoreCase)
                             || string.Equals(globalCompositeOperation, "copy", StringComparison.OrdinalIgnoreCase);
        if (!replacesCanvas
            || _state.GlobalAlpha < 1.0
            || _state.Clips.Count != 0
            || shadowBlur != 0
            || shadowOffsetX != 0
            || shadowOffsetY != 0
            || _state.FillBrush is not ImmutableSolidColorBrush solidBrush
            || solidBrush.Color.A != byte.MaxValue)
        {
            return false;
        }

        var canvasRect = new Rect(0, 0, GetCanvasPixelWidth(0), GetCanvasPixelHeight(0));
        return TransformedRectCoversCanvas(rect, _state.Transform, canvasRect);
    }

    public void fillText(string text, double x, double y)
    {
        if (string.IsNullOrEmpty(text) || !double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        var origin = new Point(x, y);
        _owner.AddCommand(new FillTextCommand(_owner, text, origin, CreateSnapshot()));
    }

    public void fillText(string text, double x, double y, double maxWidth)
    {
        fillText(text, x, y);
    }

    public void fillText(string text, double x, double y, object? maxWidth)
    {
        fillText(text, x, y);
    }

    public void strokeText(string text, double x, double y)
    {
        if (string.IsNullOrEmpty(text) || !double.IsFinite(x) || !double.IsFinite(y))
        {
            return;
        }

        var origin = new Point(x, y);
        _owner.AddCommand(new StrokeTextCommand(_owner, text, origin, CreateSnapshot()));
    }

    public void strokeText(string text, double x, double y, double maxWidth)
    {
        strokeText(text, x, y);
    }

    public void strokeText(string text, double x, double y, object? maxWidth)
    {
        strokeText(text, x, y);
    }

    public object measureText(string text)
    {
        return new CanvasTextMetrics(MeasureTextWidthCore(text));
    }

    double IWebSceneCanvasTextTarget.MeasureTextWidth(string text)
        => MeasureTextWidthCore(text);

    int IWebSceneCanvasBatchTarget.ReplayCanvasBatch(
        ReadOnlySpan<double> values,
        IReadOnlyList<string> strings)
        => AvaloniaCanvasBatchReplay.Replay(this, values, strings);

    private double MeasureTextWidthCore(string text)
    {
        text ??= string.Empty;
        var formatted = _owner.CreateFormattedText(text, CreateSnapshot());
        var width = formatted.Width * _state.FontWidthScale;
        _owner.RecordTextMeasurement(text, _state.Font, width);
        return width;
    }

    public object getTransform()
        => new CanvasDomMatrix(_state.Transform);

    public CanvasImageData createImageData(double sw, double sh)
        => new(Math.Max(0, (int)Math.Round(sw)), Math.Max(0, (int)Math.Round(sh)));

    public CanvasImageData createImageData(CanvasImageData imageData)
        => new(imageData?.width ?? 0, imageData?.height ?? 0);

    public CanvasImageData getImageData(double sx, double sy, double sw, double sh)
    {
        var width = Math.Max(0, (int)Math.Round(sw));
        var height = Math.Max(0, (int)Math.Round(sh));
        var data = new byte[width * height * 4];
        if (_rgbaPixels is null || width == 0 || height == 0)
        {
            return new CanvasImageData(width, height, data);
        }

        var sourceX = (int)Math.Round(sx);
        var sourceY = (int)Math.Round(sy);
        for (var y = 0; y < height; y++)
        {
            var srcY = sourceY + y;
            if (srcY < 0 || srcY >= _rgbaHeight)
            {
                continue;
            }

            for (var x = 0; x < width; x++)
            {
                var srcX = sourceX + x;
                if (srcX < 0 || srcX >= _rgbaWidth)
                {
                    continue;
                }

                var source = ((srcY * _rgbaWidth) + srcX) * 4;
                var target = ((y * width) + x) * 4;
                data[target] = _rgbaPixels[source];
                data[target + 1] = _rgbaPixels[source + 1];
                data[target + 2] = _rgbaPixels[source + 2];
                data[target + 3] = _rgbaPixels[source + 3];
            }
        }

        return new CanvasImageData(width, height, data);
    }

    public void putImageData(CanvasImageData imageData, double dx, double dy)
        => PutImageData(imageData, dx, dy);

    public void putImageData(CanvasImageData imageData, double dx, double dy, double dirtyX, double dirtyY, double dirtyWidth, double dirtyHeight)
        => PutImageData(imageData, dx + dirtyX, dy + dirtyY, dirtyX, dirtyY, dirtyWidth, dirtyHeight);

    internal bool TryGetRgbaPixels(out int width, out int height, out byte[] pixels)
    {
        width = _rgbaWidth;
        height = _rgbaHeight;
        pixels = _rgbaPixels is null ? Array.Empty<byte>() : (byte[])_rgbaPixels.Clone();
        return width > 0 && height > 0 && pixels.Length == width * height * 4;
    }

    internal void ResetForCanvasResize()
    {
        _rgbaPixels = null;
        _rgbaWidth = 0;
        _rgbaHeight = 0;
        _state = CanvasState.CreateDefault();
        _stateStack.Clear();
        _path.Clear();
        _owner.ClearAll();
    }

    public void drawImage(object image, double dx, double dy)
    {
        if (TryResolveCanvasSurface(image, out var surface, out var canvasSourceRect))
        {
            DrawCanvasSurface(surface, canvasSourceRect, new Rect(dx, dy, canvasSourceRect.Width, canvasSourceRect.Height));
            return;
        }

        if (TryResolveImagePixels(image, out var pixelWidth, out var pixelHeight, out var pixelData))
        {
            var pixelSourceRect = new Rect(0, 0, pixelWidth, pixelHeight);
            BlitRgbaPixels(pixelData, pixelWidth, pixelHeight, pixelSourceRect, new Rect(dx, dy, pixelSourceRect.Width, pixelSourceRect.Height), CreateSnapshot());
            return;
        }

        if (!TryResolveImage(image, out var resolved, out var sourceRect))
        {
            return;
        }

        var destRect = new Rect(dx, dy, sourceRect.Width, sourceRect.Height);
        if (destRect.Width <= 0 || destRect.Height <= 0)
        {
            return;
        }

        _owner.AddCommand(new DrawImageCommand(resolved, sourceRect, destRect, CreateSnapshot()));
    }


    public void drawImage(object image, double dx, double dy, double dWidth, double dHeight)
    {
        if (dWidth <= 0 || dHeight <= 0)
        {
            return;
        }

        if (TryResolveCanvasSurface(image, out var surface, out var canvasSourceRect))
        {
            DrawCanvasSurface(surface, canvasSourceRect, new Rect(dx, dy, dWidth, dHeight));
            return;
        }

        if (TryResolveImagePixels(image, out var pixelWidth, out var pixelHeight, out var pixelData))
        {
            BlitRgbaPixels(pixelData, pixelWidth, pixelHeight, new Rect(0, 0, pixelWidth, pixelHeight), new Rect(dx, dy, dWidth, dHeight), CreateSnapshot());
            return;
        }

        if (!TryResolveImage(image, out var resolved, out var sourceRect))
        {
            return;
        }

        var destRect = new Rect(dx, dy, dWidth, dHeight);
        _owner.AddCommand(new DrawImageCommand(resolved, sourceRect, destRect, CreateSnapshot()));
    }

    public void drawImage(object image, double sx, double sy, double sWidth, double sHeight, double dx, double dy, double dWidth, double dHeight)
    {
        if (sWidth <= 0 || sHeight <= 0 || dWidth <= 0 || dHeight <= 0)
        {
            return;
        }

        if (TryResolveCanvasSurface(image, out var surface, out var canvasSourceRect))
        {
            var canvasCrop = new Rect(sx, sy, sWidth, sHeight);
            var canvasBounded = canvasCrop.Intersect(canvasSourceRect);
            if (canvasBounded.Width <= 0 || canvasBounded.Height <= 0)
            {
                return;
            }

            DrawCanvasSurface(surface, canvasBounded, new Rect(dx, dy, dWidth, dHeight));
            return;
        }

        if (TryResolveImagePixels(image, out var pixelWidth, out var pixelHeight, out var pixelData))
        {
            var pixelSourceRect = new Rect(sx, sy, sWidth, sHeight).Intersect(new Rect(0, 0, pixelWidth, pixelHeight));
            if (pixelSourceRect.Width > 0 && pixelSourceRect.Height > 0)
            {
                BlitRgbaPixels(pixelData, pixelWidth, pixelHeight, pixelSourceRect, new Rect(dx, dy, dWidth, dHeight), CreateSnapshot());
            }

            return;
        }

        if (!TryResolveImage(image, out var resolved, out var fullSourceRect))
        {
            return;
        }

        var crop = new Rect(sx, sy, sWidth, sHeight);
        var bounded = crop.Intersect(fullSourceRect);
        if (bounded.Width <= 0 || bounded.Height <= 0)
        {
            return;
        }

        var destRect = new Rect(dx, dy, dWidth, dHeight);
        _owner.AddCommand(new DrawImageCommand(resolved, bounded, destRect, CreateSnapshot()));
    }

    private void PutImageData(CanvasImageData? imageData, double dx, double dy)
        => PutImageData(imageData, dx, dy, 0, 0, imageData?.width ?? 0, imageData?.height ?? 0);

    private void PutImageData(CanvasImageData? imageData, double dx, double dy, double sourceX, double sourceY, double sourceWidth, double sourceHeight)
    {
        if (imageData is null || imageData.width <= 0 || imageData.height <= 0 || imageData.data.Length < imageData.width * imageData.height * 4)
        {
            return;
        }

        var sourceRect = new Rect(sourceX, sourceY, sourceWidth, sourceHeight).Intersect(new Rect(0, 0, imageData.width, imageData.height));
        if (sourceRect.Width <= 0 || sourceRect.Height <= 0)
        {
            return;
        }

        BlitRgbaPixels(
            imageData.data,
            imageData.width,
            imageData.height,
            sourceRect,
            new Rect(dx, dy, sourceRect.Width, sourceRect.Height),
            CreateSnapshot(Matrix.Identity, ignoreClip: true, globalAlpha: 1.0));
    }

    private void BlitRgbaPixels(byte[] sourcePixels, int sourceWidth, int sourceHeight, Rect sourceRect, Rect destinationRect, CanvasStateSnapshot drawState)
    {
        if (sourceWidth <= 0 || sourceHeight <= 0 || sourcePixels.Length < sourceWidth * sourceHeight * 4 || sourceRect.Width <= 0 || sourceRect.Height <= 0 || destinationRect.Width <= 0 || destinationRect.Height <= 0)
        {
            return;
        }

        var canvasWidth = GetCanvasPixelWidth(destinationRect.Right);
        var canvasHeight = GetCanvasPixelHeight(destinationRect.Bottom);
        EnsureRgbaBuffer(canvasWidth, canvasHeight);

        var startX = Math.Max(0, (int)Math.Floor(destinationRect.X));
        var startY = Math.Max(0, (int)Math.Floor(destinationRect.Y));
        var endX = Math.Min(_rgbaWidth, (int)Math.Ceiling(destinationRect.Right));
        var endY = Math.Min(_rgbaHeight, (int)Math.Ceiling(destinationRect.Bottom));

        for (var y = startY; y < endY; y++)
        {
            var sourceYRatio = (y + 0.5 - destinationRect.Y) / destinationRect.Height;
            var srcY = (int)Math.Floor(sourceRect.Y + (sourceYRatio * sourceRect.Height));
            srcY = Math.Clamp(srcY, 0, sourceHeight - 1);

            for (var x = startX; x < endX; x++)
            {
                var sourceXRatio = (x + 0.5 - destinationRect.X) / destinationRect.Width;
                var srcX = (int)Math.Floor(sourceRect.X + (sourceXRatio * sourceRect.Width));
                srcX = Math.Clamp(srcX, 0, sourceWidth - 1);

                var source = ((srcY * sourceWidth) + srcX) * 4;
                var target = ((y * _rgbaWidth) + x) * 4;
                _rgbaPixels![target] = sourcePixels[source];
                _rgbaPixels[target + 1] = sourcePixels[source + 1];
                _rgbaPixels[target + 2] = sourcePixels[source + 2];
                _rgbaPixels[target + 3] = sourcePixels[source + 3];
            }
        }

        var retainedPixels = new byte[checked(sourceWidth * sourceHeight * 4)];
        sourcePixels.AsSpan(0, retainedPixels.Length).CopyTo(retainedPixels);
        _owner.AddCommand(new DrawRgbaPixelsCommand(
            new CanvasImageDataModel(sourceWidth, sourceHeight, retainedPixels),
            sourceRect,
            destinationRect,
            drawState));
    }

    private void ClearRgbaPixels(Rect rect)
    {
        if (_rgbaPixels is null || _rgbaWidth <= 0 || _rgbaHeight <= 0 || rect.Width <= 0 || rect.Height <= 0)
        {
            return;
        }

        var canvasRect = new Rect(0, 0, _rgbaWidth, _rgbaHeight);
        if (rect.Contains(canvasRect))
        {
            _rgbaPixels = null;
            _rgbaWidth = 0;
            _rgbaHeight = 0;
            return;
        }

        var clipped = rect.Intersect(canvasRect);
        if (clipped.Width <= 0 || clipped.Height <= 0)
        {
            return;
        }

        var startX = Math.Max(0, (int)Math.Floor(clipped.X));
        var startY = Math.Max(0, (int)Math.Floor(clipped.Y));
        var endX = Math.Min(_rgbaWidth, (int)Math.Ceiling(clipped.Right));
        var endY = Math.Min(_rgbaHeight, (int)Math.Ceiling(clipped.Bottom));
        for (var y = startY; y < endY; y++)
        {
            var offset = ((y * _rgbaWidth) + startX) * 4;
            Array.Clear(_rgbaPixels, offset, Math.Max(0, endX - startX) * 4);
        }
    }

    private int GetCanvasPixelWidth(double minimumWidth)
    {
        var width = canvas is AvaloniaDomElement element ? element.width : _owner.Bounds.Width;
        return Math.Max(1, (int)Math.Ceiling(Math.Max(width, minimumWidth)));
    }

    private int GetCanvasPixelHeight(double minimumHeight)
    {
        var height = canvas is AvaloniaDomElement element ? element.height : _owner.Bounds.Height;
        return Math.Max(1, (int)Math.Ceiling(Math.Max(height, minimumHeight)));
    }

    private void EnsureRgbaBuffer(int width, int height)
    {
        if (_rgbaPixels is not null && _rgbaWidth == width && _rgbaHeight == height)
        {
            return;
        }

        var oldPixels = _rgbaPixels;
        var oldWidth = _rgbaWidth;
        var oldHeight = _rgbaHeight;
        _rgbaWidth = width;
        _rgbaHeight = height;
        _rgbaPixels = new byte[width * height * 4];

        if (oldPixels is null)
        {
            return;
        }

        var copyWidth = Math.Min(oldWidth, width);
        var copyHeight = Math.Min(oldHeight, height);
        for (var y = 0; y < copyHeight; y++)
        {
            Array.Copy(oldPixels, y * oldWidth * 4, _rgbaPixels, y * width * 4, copyWidth * 4);
        }
    }

    private void DrawCanvasSurface(CanvasDrawingSurface surface, Rect sourceRect, Rect destinationRect)
    {
        if (ReferenceEquals(surface, _owner) || sourceRect.Width <= 0 || sourceRect.Height <= 0 || destinationRect.Width <= 0 || destinationRect.Height <= 0)
        {
            return;
        }

        _owner.AddCommand(new DrawCanvasSurfaceCommand(surface, sourceRect, destinationRect, CreateSnapshot()));
    }

    private static bool TryResolveImagePixels(object image, out int width, out int height, out byte[] pixels)
    {
        if (image is AvaloniaDomElement element &&
            CanvasContextBridge.TryGetSurface(element.Control, out var canvasSurface) &&
            canvasSurface.Context.TryGetRgbaPixels(out width, out height, out pixels))
        {
            return true;
        }

        if (AvaloniaDomImageElement.TryGetRgbaPixels(image, out width, out height, out pixels))
        {
            return true;
        }

        width = 0;
        height = 0;
        pixels = Array.Empty<byte>();
        return false;
    }

    private static bool TryCreatePatternSource(
        object image,
        [NotNullWhen(true)] out IImageBrushSource? source,
        out double width,
        out double height)
    {
        if (TryResolveCanvasSurface(image, out var surface, out var sourceRect))
        {
            source = RasterizeCanvasSurface(surface, sourceRect);
            width = sourceRect.Width;
            height = sourceRect.Height;
            return width > 0 && height > 0;
        }

        if (TryResolveImagePixels(image, out var pixelWidth, out var pixelHeight, out var pixelData))
        {
            source = CanvasBitmapFactory.CreateWriteableBitmap(pixelWidth, pixelHeight, pixelData);
            width = pixelWidth;
            height = pixelHeight;
            return true;
        }

        if (TryResolveImage(image, out var resolved, out var resolvedRect) &&
            resolved is IImageBrushSource imageBrushSource)
        {
            source = imageBrushSource;
            width = resolvedRect.Width;
            height = resolvedRect.Height;
            return width > 0 && height > 0;
        }

        source = null;
        width = 0;
        height = 0;
        return false;
    }

    private static RenderTargetBitmap RasterizeCanvasSurface(CanvasDrawingSurface surface, Rect sourceRect)
    {
        var width = Math.Max(1, (int)Math.Ceiling(sourceRect.Width));
        var height = Math.Max(1, (int)Math.Ceiling(sourceRect.Height));
        var bitmap = new RenderTargetBitmap(new PixelSize(width, height), new Vector(96, 96));
        using var context = bitmap.CreateDrawingContext(clear: true);

        using (context.PushTransform(Matrix.CreateTranslation(-sourceRect.X, -sourceRect.Y)))
        {
            if (surface.Commands.Count == 0 && surface.Context.TryGetRgbaPixels(out var pixelWidth, out var pixelHeight, out var pixels))
            {
                using var pixelBitmap = CanvasBitmapFactory.CreateWriteableBitmap(pixelWidth, pixelHeight, pixels);
                context.DrawImage(pixelBitmap, new Rect(0, 0, pixelWidth, pixelHeight), new Rect(0, 0, pixelWidth, pixelHeight));
            }
            else
            {
                surface.RenderCommands(context);
            }
        }

        return bitmap;
    }

    private static bool TryResolveCanvasSurface(object image, [NotNullWhen(true)] out CanvasDrawingSurface? surface, out Rect sourceRect)
    {
        if (image is AvaloniaDomElement element && CanvasContextBridge.TryGetSurface(element.Control, out surface))
        {
            var width = GetCanvasDimension(element.width, element.offsetWidth, element.Control.Bounds.Width);
            var height = GetCanvasDimension(element.height, element.offsetHeight, element.Control.Bounds.Height);
            sourceRect = new Rect(0, 0, width, height);
            return sourceRect.Width > 0 && sourceRect.Height > 0;
        }

        surface = null;
        sourceRect = default;
        return false;
    }

    private static double GetCanvasDimension(params double[] candidates)
    {
        foreach (var candidate in candidates)
        {
            if (double.IsFinite(candidate) && candidate > 0)
            {
                return candidate;
            }
        }

        return 0;
    }

    private CanvasStateSnapshot CreateSnapshot()
        => CreateSnapshot(
            _state.Transform,
            ignoreClip: false,
            globalAlpha: _state.GlobalAlpha);

    private CanvasStateSnapshot CreateSnapshot(
        Matrix transform,
        bool ignoreClip,
        double globalAlpha)
    {
        var key = new CanvasStateCacheKey(
            _state.FillPaint,
            _state.StrokePaint,
            _state.LineWidth,
            _state.LineCap,
            _state.LineJoin,
            _state.MiterLimit,
            globalAlpha,
            _state.LineDash,
            _state.LineDashOffset,
            transform,
            ignoreClip ? null : _state.Clips,
            ignoreClip ? 0 : _state.Clips.Count,
            _state.Font,
            _state.TextAlign,
            _state.TextBaseline,
            _state.ImageSmoothingEnabled,
            _state.ImageSmoothingQuality,
            _state.CompositeOperation,
            _state.ShadowColor,
            _state.ShadowBlur,
            _state.ShadowOffsetX,
            _state.ShadowOffsetY);
        if (_stateSnapshotCache.TryGetValue(key, out var cached))
        {
            return cached;
        }

        var snapshot = CanvasDrawingSurface.EnableNativeCanvasHotPath
            ? _state.CreateNativeSnapshot(transform, ignoreClip, globalAlpha)
            : _state.CreateSnapshot(transform, ignoreClip, globalAlpha);
        const int capacity = 4096;
        if (_stateSnapshotCache.Count >= capacity)
        {
            _stateSnapshotCache.Clear();
        }
        _stateSnapshotCache.Add(key, snapshot);
        return snapshot;
    }

    private static double[] ToCanvasMatrix(Matrix matrix)
        => new[] { matrix.M11, matrix.M12, matrix.M21, matrix.M22, matrix.M31, matrix.M32 };

    private static Matrix InvertOrIdentity(Matrix matrix)
    {
        var a = matrix.M11;
        var b = matrix.M12;
        var c = matrix.M21;
        var d = matrix.M22;
        var e = matrix.M31;
        var f = matrix.M32;
        var determinant = (a * d) - (b * c);
        if (!double.IsFinite(determinant) || Math.Abs(determinant) < double.Epsilon)
        {
            return Matrix.Identity;
        }

        return new Matrix(
            d / determinant,
            -b / determinant,
            -c / determinant,
            a / determinant,
            ((c * f) - (d * e)) / determinant,
            ((b * e) - (a * f)) / determinant);
    }

    private static bool TryResolveImage(object image, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out IImage? resolved, out Rect sourceRect)
    {
        resolved = image switch
        {
            IImage img => img,
            Image control when control.Source is IImage src => src,
            _ => null
        };

        if (resolved is null)
        {
            sourceRect = default;
            return false;
        }

        var size = resolved.Size;
        sourceRect = new Rect(0, 0, size.Width, size.Height);
        return sourceRect.Width > 0 && sourceRect.Height > 0;
    }

    private readonly record struct CanvasStateCacheKey(
        CanvasPaintModel FillPaint,
        CanvasPaintModel StrokePaint,
        double LineWidth,
        CanvasLineCap LineCap,
        CanvasLineJoin LineJoin,
        double MiterLimit,
        double GlobalAlpha,
        double[] LineDash,
        double LineDashOffset,
        Matrix Transform,
        List<CanvasClipModel>? Clips,
        int ClipCount,
        string Font,
        CanvasTextAlign TextAlign,
        CanvasTextBaseline TextBaseline,
        bool ImageSmoothingEnabled,
        string ImageSmoothingQuality,
        string CompositeOperation,
        string ShadowColor,
        double ShadowBlur,
        double ShadowOffsetX,
        double ShadowOffsetY);
}

internal sealed class CanvasCallMetric
{
    internal long Count;
    internal long Ticks;
    internal long AllocatedBytes;
}

internal static class CanvasBitmapFactory
{
    public static WriteableBitmap CreateWriteableBitmap(int width, int height, byte[] rgbaPixels)
    {
        var bitmap = new WriteableBitmap(
            new PixelSize(width, height),
            new Vector(96, 96),
            PixelFormat.Bgra8888,
            AlphaFormat.Premul);

        using var framebuffer = bitmap.Lock();
        var bgraRow = new byte[width * 4];
        for (var y = 0; y < height; y++)
        {
            var sourceOffset = y * width * 4;
            for (var x = 0; x < width; x++)
            {
                var source = sourceOffset + x * 4;
                var target = x * 4;
                var alpha = rgbaPixels[source + 3];
                bgraRow[target] = Premultiply(rgbaPixels[source + 2], alpha);
                bgraRow[target + 1] = Premultiply(rgbaPixels[source + 1], alpha);
                bgraRow[target + 2] = Premultiply(rgbaPixels[source], alpha);
                bgraRow[target + 3] = alpha;
            }

            Marshal.Copy(bgraRow, 0, IntPtr.Add(framebuffer.Address, y * framebuffer.RowBytes), bgraRow.Length);
        }

        return bitmap;
    }

    private static byte Premultiply(byte channel, byte alpha)
    {
        if (alpha >= byte.MaxValue)
        {
            return channel;
        }

        return alpha == 0 ? (byte)0 : (byte)((channel * alpha + 127) / 255);
    }
}

internal sealed record CanvasTextMeasurement(string Text, string Font, double Width);

internal sealed class CanvasState
{
    public CanvasPaintModel FillPaint { get; set; } = new CanvasColorPaintModel(WebScene.Core.WebSceneColor.FromRgb(0, 0, 0));
    public CanvasPaintModel StrokePaint { get; set; } = new CanvasColorPaintModel(WebScene.Core.WebSceneColor.FromRgb(0, 0, 0));
    public IImmutableBrush? FillBrush => CanvasPaintParser.Project(FillPaint);
    public IImmutableBrush? StrokeBrush => CanvasPaintParser.Project(StrokePaint);
    public object? FillStyleValue { get; set; } = "#000000";
    public object? StrokeStyleValue { get; set; } = "#000000";
    public double LineWidth { get; set; } = 1.0;
    public CanvasLineCap LineCap { get; set; } = CanvasLineCap.Butt;
    public CanvasLineJoin LineJoin { get; set; } = CanvasLineJoin.Miter;
    public double MiterLimit { get; set; } = 10.0;
    public double GlobalAlpha { get; set; } = 1.0;
    public double[] LineDash { get; set; } = Array.Empty<double>();
    public double LineDashOffset { get; set; }
    public Matrix Transform { get; set; } = Matrix.Identity;
    public List<CanvasClipModel> Clips { get; set; } = new();
    public Geometry? ClipGeometry { get; set; }
    public Typeface Typeface { get; set; } = new Typeface("Segoe UI");
    public double FontSize { get; set; } = 16.0;
    public double FontWidthScale { get; set; } = 1.0;
    public CanvasTextAlign TextAlign { get; set; } = CanvasTextAlign.Start;
    public CanvasTextBaseline TextBaseline { get; set; } = CanvasTextBaseline.Alphabetic;
    public string Font { get; set; } = "16px Segoe UI";
    public bool ImageSmoothingEnabled { get; set; } = true;
    public string ImageSmoothingQuality { get; set; } = "low";
    public string CompositeOperation { get; set; } = "source-over";
    public string ShadowColor { get; set; } = string.Empty;
    public double ShadowBlur { get; set; }
    public double ShadowOffsetX { get; set; }
    public double ShadowOffsetY { get; set; }

    public static CanvasState CreateDefault() => new();

    public CanvasState Clone() => new()
    {
        FillPaint = FillPaint,
        StrokePaint = StrokePaint,
        FillStyleValue = FillStyleValue,
        StrokeStyleValue = StrokeStyleValue,
        LineWidth = LineWidth,
        LineCap = LineCap,
        LineJoin = LineJoin,
        MiterLimit = MiterLimit,
        GlobalAlpha = GlobalAlpha,
        LineDash = LineDash.Length == 0 ? Array.Empty<double>() : (double[])LineDash.Clone(),
        LineDashOffset = LineDashOffset,
        Transform = Transform,
        // Clip entries are immutable snapshots captured by clip(). Saving state only
        // needs a private list container; deep-copying every retained path here made
        // chart crosshair and pan frames allocate hundreds of megabytes.
        Clips = Clips.Count == 0
            ? new List<CanvasClipModel>()
            : new List<CanvasClipModel>(Clips),
        ClipGeometry = ClipGeometry,
        Typeface = Typeface,
        FontSize = FontSize,
        FontWidthScale = FontWidthScale,
        TextAlign = TextAlign,
        TextBaseline = TextBaseline,
        Font = Font,
        ImageSmoothingEnabled = ImageSmoothingEnabled,
        ImageSmoothingQuality = ImageSmoothingQuality,
        CompositeOperation = CompositeOperation,
        ShadowColor = ShadowColor,
        ShadowBlur = ShadowBlur,
        ShadowOffsetX = ShadowOffsetX,
        ShadowOffsetY = ShadowOffsetY
    };

    public void SetFillStyle(object? value)
    {
        if (CanvasPaintParser.TryCreatePaint(value, FillPaint, out var paint, out var stored))
        {
            FillPaint = paint;
            FillStyleValue = stored;
        }
    }

    public void SetStrokeStyle(object? value)
    {
        if (CanvasPaintParser.TryCreatePaint(value, StrokePaint, out var paint, out var stored))
        {
            StrokePaint = paint;
            StrokeStyleValue = stored;
        }
    }

    public CanvasStateSnapshot CreateSnapshot() => new(this);

    public CanvasStateSnapshot CreateSnapshot(Matrix transform) => new(this, transform);

    public CanvasStateSnapshot CreateSnapshot(Matrix transform, bool ignoreClip, double globalAlpha) => new(this, transform, ignoreClip, globalAlpha);

    public CanvasStateSnapshot CreateNativeSnapshot(Matrix transform, bool ignoreClip, double globalAlpha)
        => new(this, transform, ignoreClip, globalAlpha, deferPortableModel: false);

    internal CanvasStateModel CreatePortableModel(Matrix transform, bool ignoreClip, double globalAlpha)
        => new()
        {
            Transform = ToPortableTransform(transform),
            FillStyle = FillPaint,
            StrokeStyle = StrokePaint,
            GlobalAlpha = Math.Clamp(globalAlpha, 0.0, 1.0),
            CompositeOperation = ParseCompositeOperation(CompositeOperation),
            LineWidth = LineWidth,
            LineCap = LineCap,
            LineJoin = LineJoin,
            MiterLimit = MiterLimit,
            LineDash = LineDash.Length == 0 ? Array.Empty<double>() : LineDash,
            LineDashOffset = LineDashOffset,
            Font = Font,
            TextAlign = TextAlign,
            TextBaseline = TextBaseline,
            ImageSmoothingEnabled = ImageSmoothingEnabled,
            ImageSmoothingQuality = ParseImageSmoothingQuality(ImageSmoothingQuality),
            Shadow = new CanvasShadowModel(
                ToPortableColor(CanvasColorParser.ParseColor(ShadowColor, Colors.Transparent)),
                ShadowBlur,
                ShadowOffsetX,
                ShadowOffsetY),
            // CanvasClipModel instances already own path snapshots. The portable
            // state may safely share those immutable entries while detaching the
            // mutable list container.
            Clips = ignoreClip || Clips.Count == 0
                ? Array.Empty<CanvasClipModel>()
                : Clips.ToArray()
        };

    internal static GraphicsTransform ToPortableTransform(Matrix transform)
        => new(transform.M11, transform.M12, transform.M21, transform.M22, transform.M31, transform.M32);

    internal static Matrix ToAvaloniaTransform(GraphicsTransform transform)
        => new(transform.M11, transform.M12, transform.M21, transform.M22, transform.M31, transform.M32);

    internal static WebScene.Core.WebSceneColor ToPortableColor(Color color)
        => new(color.A, color.R, color.G, color.B);

    internal static CanvasCompositeOperation ParseCompositeOperation(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "source-in" => CanvasCompositeOperation.SourceIn,
            "source-out" => CanvasCompositeOperation.SourceOut,
            "source-atop" => CanvasCompositeOperation.SourceAtop,
            "destination-over" => CanvasCompositeOperation.DestinationOver,
            "destination-in" => CanvasCompositeOperation.DestinationIn,
            "destination-out" => CanvasCompositeOperation.DestinationOut,
            "destination-atop" => CanvasCompositeOperation.DestinationAtop,
            "lighter" => CanvasCompositeOperation.Lighter,
            "copy" => CanvasCompositeOperation.Copy,
            "xor" => CanvasCompositeOperation.Xor,
            "multiply" => CanvasCompositeOperation.Multiply,
            "screen" => CanvasCompositeOperation.Screen,
            "overlay" => CanvasCompositeOperation.Overlay,
            "darken" => CanvasCompositeOperation.Darken,
            "lighten" => CanvasCompositeOperation.Lighten,
            _ => CanvasCompositeOperation.SourceOver
        };

    internal static CanvasImageSmoothingQuality ParseImageSmoothingQuality(string? value)
        => value switch
        {
            "medium" => CanvasImageSmoothingQuality.Medium,
            "high" => CanvasImageSmoothingQuality.High,
            _ => CanvasImageSmoothingQuality.Low
        };

    public void UpdateFont(string? value, CssFontFaceRegistry? fontFaces = null)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return;
        }

        Font = value;
        CanvasFontParser.Apply(this, value, fontFaces);
    }
}

internal readonly struct CanvasStateSnapshot
{
    private static readonly ConcurrentDictionary<string, CanvasFontProjection> s_fontProjections =
        new(StringComparer.Ordinal);
    private readonly CanvasStateModel? _model;
    private readonly CanvasPaintModel _fillPaint;
    private readonly CanvasPaintModel _strokePaint;
    private readonly CanvasLineCap _portableLineCap;
    private readonly CanvasLineJoin _portableLineJoin;
    private readonly CanvasCompositeOperation _compositeOperation;
    private readonly bool _imageSmoothingEnabled;
    private readonly CanvasImageSmoothingQuality _imageSmoothingQuality;
    private readonly string _font;
    private readonly Color _shadowColor;
    private readonly double _shadowBlur;
    private readonly double _shadowOffsetX;
    private readonly double _shadowOffsetY;
    private readonly IReadOnlyList<CanvasClipModel>? _portableClips;
    private readonly List<CanvasClipModel>? _nativeClips;
    private readonly int _nativeClipCount;

    public CanvasStateSnapshot(CanvasState state)
        : this(state, state.Transform)
    {
    }

    public CanvasStateSnapshot(CanvasState state, Matrix transform)
        : this(state, transform, false, state.GlobalAlpha)
    {
    }

    public CanvasStateSnapshot(CanvasState state, Matrix transform, bool ignoreClip, double globalAlpha)
        : this(state, transform, ignoreClip, globalAlpha, deferPortableModel: false)
    {
    }

    internal CanvasStateSnapshot(
        CanvasState state,
        Matrix transform,
        bool ignoreClip,
        double globalAlpha,
        bool deferPortableModel)
    {
        ArgumentNullException.ThrowIfNull(state);
        _model = deferPortableModel
            ? null
            : state.CreatePortableModel(transform, ignoreClip, globalAlpha);
        _fillPaint = state.FillPaint;
        _strokePaint = state.StrokePaint;
        _portableLineCap = state.LineCap;
        _portableLineJoin = state.LineJoin;
        _compositeOperation = CanvasState.ParseCompositeOperation(state.CompositeOperation);
        _imageSmoothingEnabled = state.ImageSmoothingEnabled;
        _imageSmoothingQuality = CanvasState.ParseImageSmoothingQuality(state.ImageSmoothingQuality);
        _font = state.Font;
        _shadowColor = CanvasColorParser.ParseColor(state.ShadowColor, Colors.Transparent);
        _shadowBlur = state.ShadowBlur;
        _shadowOffsetX = state.ShadowOffsetX;
        _shadowOffsetY = state.ShadowOffsetY;
        _portableClips = null;
        _nativeClips = ignoreClip ? null : state.Clips;
        _nativeClipCount = ignoreClip ? 0 : state.Clips.Count;
        FillBrush = state.FillBrush;
        StrokeBrush = state.StrokeBrush;
        LineWidth = state.LineWidth;
        LineCap = state.LineCap switch
        {
            CanvasLineCap.Round => PenLineCap.Round,
            CanvasLineCap.Square => PenLineCap.Square,
            _ => PenLineCap.Flat
        };
        LineJoin = state.LineJoin switch
        {
            CanvasLineJoin.Round => PenLineJoin.Round,
            CanvasLineJoin.Bevel => PenLineJoin.Bevel,
            _ => PenLineJoin.Miter
        };
        MiterLimit = state.MiterLimit;
        GlobalAlpha = Math.Clamp(globalAlpha, 0.0, 1.0);
        LineDash = state.LineDash.Length == 0 ? Array.Empty<double>() : state.LineDash;
        LineDashOffset = state.LineDashOffset;
        Transform = transform;
        ClipGeometry = ignoreClip ? null : state.ClipGeometry;
        Typeface = state.Typeface;
        FontSize = state.FontSize;
        FontWidthScale = state.FontWidthScale;
        TextAlign = state.TextAlign;
        TextBaseline = state.TextBaseline;
    }

    /// <summary>
    /// Projects a portable retained state into Avalonia resources. Renderers use
    /// this constructor so the backend model is a cache, never semantic authority.
    /// </summary>
    public CanvasStateSnapshot(CanvasStateModel model)
    {
        ArgumentNullException.ThrowIfNull(model);
        _model = model;
        _fillPaint = model.FillStyle;
        _strokePaint = model.StrokeStyle;
        _portableLineCap = model.LineCap;
        _portableLineJoin = model.LineJoin;
        _compositeOperation = model.CompositeOperation;
        _imageSmoothingEnabled = model.ImageSmoothingEnabled;
        _imageSmoothingQuality = model.ImageSmoothingQuality;
        _font = model.Font;
        _shadowColor = Color.FromArgb(
            model.Shadow.Color.A,
            model.Shadow.Color.R,
            model.Shadow.Color.G,
            model.Shadow.Color.B);
        _shadowBlur = model.Shadow.Blur;
        _shadowOffsetX = model.Shadow.OffsetX;
        _shadowOffsetY = model.Shadow.OffsetY;
        _portableClips = model.Clips;
        _nativeClips = null;
        _nativeClipCount = 0;
        FillBrush = CanvasPaintParser.Project(model.FillStyle);
        StrokeBrush = CanvasPaintParser.Project(model.StrokeStyle);
        LineWidth = model.LineWidth;
        LineCap = model.LineCap switch
        {
            CanvasLineCap.Round => PenLineCap.Round,
            CanvasLineCap.Square => PenLineCap.Square,
            _ => PenLineCap.Flat
        };
        LineJoin = model.LineJoin switch
        {
            CanvasLineJoin.Round => PenLineJoin.Round,
            CanvasLineJoin.Bevel => PenLineJoin.Bevel,
            _ => PenLineJoin.Miter
        };
        MiterLimit = model.MiterLimit;
        GlobalAlpha = model.GlobalAlpha;
        LineDash = model.LineDash.Count == 0 ? Array.Empty<double>() : model.LineDash as double[] ?? [.. model.LineDash];
        LineDashOffset = model.LineDashOffset;
        Transform = CanvasState.ToAvaloniaTransform(model.Transform);
        ClipGeometry = ProjectClipGeometry(model.Clips);
        var font = s_fontProjections.GetOrAdd(
            model.Font,
            static value =>
            {
                var state = CanvasState.CreateDefault();
                state.UpdateFont(value);
                return new CanvasFontProjection(state.Typeface, state.FontSize, state.FontWidthScale);
            });
        Typeface = font.Typeface;
        FontSize = font.Size;
        FontWidthScale = font.WidthScale;
        TextAlign = model.TextAlign;
        TextBaseline = model.TextBaseline;
    }

    public CanvasStateModel Model
        => _model ?? new CanvasStateModel
        {
            Transform = CanvasState.ToPortableTransform(Transform),
            FillStyle = _fillPaint,
            StrokeStyle = _strokePaint,
            GlobalAlpha = GlobalAlpha,
            CompositeOperation = _compositeOperation,
            LineWidth = LineWidth,
            LineCap = _portableLineCap,
            LineJoin = _portableLineJoin,
            MiterLimit = MiterLimit,
            LineDash = LineDash,
            LineDashOffset = LineDashOffset,
            Font = _font,
            TextAlign = TextAlign,
            TextBaseline = TextBaseline,
            ImageSmoothingEnabled = _imageSmoothingEnabled,
            ImageSmoothingQuality = _imageSmoothingQuality,
            Shadow = new CanvasShadowModel(
                CanvasState.ToPortableColor(_shadowColor),
                _shadowBlur,
                _shadowOffsetX,
                _shadowOffsetY),
            Clips = CapturePortableClips()
        };
    public IImmutableBrush? FillBrush { get; }
    public IImmutableBrush? StrokeBrush { get; }
    public double LineWidth { get; }
    public PenLineCap LineCap { get; }
    public PenLineJoin LineJoin { get; }
    public double MiterLimit { get; }
    public double GlobalAlpha { get; }
    public double[] LineDash { get; }
    public double LineDashOffset { get; }
    public Matrix Transform { get; }
    public Geometry? ClipGeometry { get; }
    public Typeface Typeface { get; }
    public double FontSize { get; }
    public double FontWidthScale { get; }
    public CanvasTextAlign TextAlign { get; }
    public CanvasTextBaseline TextBaseline { get; }

    private IReadOnlyList<CanvasClipModel> CapturePortableClips()
    {
        if (_portableClips is not null)
        {
            return _portableClips;
        }
        if (_nativeClips is null || _nativeClipCount == 0)
        {
            return Array.Empty<CanvasClipModel>();
        }

        var clips = new CanvasClipModel[_nativeClipCount];
        _nativeClips.CopyTo(0, clips, 0, _nativeClipCount);
        return clips;
    }

    private static Geometry? ProjectClipGeometry(IReadOnlyList<CanvasClipModel> clips)
    {
        Geometry? result = null;
        foreach (var clip in clips)
        {
            Geometry geometry = CanvasPathBuilder.BuildGeometry(clip.Path);
            var transform = CanvasState.ToAvaloniaTransform(clip.Transform);
            if (!transform.IsIdentity)
            {
                geometry.Transform = new MatrixTransform(transform);
            }

            result = result is null
                ? geometry
                : new CombinedGeometry(GeometryCombineMode.Intersect, result, geometry);
        }

        return result;
    }

    internal bool HasSameRenderingState(CanvasStateSnapshot other)
        => Equals(_fillPaint, other._fillPaint)
           && Equals(_strokePaint, other._strokePaint)
           && LineWidth.Equals(other.LineWidth)
           && LineCap == other.LineCap
           && LineJoin == other.LineJoin
           && MiterLimit.Equals(other.MiterLimit)
           && GlobalAlpha.Equals(other.GlobalAlpha)
           && ReferenceEquals(LineDash, other.LineDash)
           && LineDashOffset.Equals(other.LineDashOffset)
           && Transform.Equals(other.Transform)
           && ReferenceEquals(ClipGeometry, other.ClipGeometry)
           && Typeface.Equals(other.Typeface)
           && FontSize.Equals(other.FontSize)
           && FontWidthScale.Equals(other.FontWidthScale)
           && TextAlign == other.TextAlign
           && TextBaseline == other.TextBaseline;

    public Pen? CreatePen()
    {
        if (StrokeBrush is null || LineWidth <= 0)
        {
            return null;
        }

        var pen = new Pen(StrokeBrush, LineWidth)
        {
            LineCap = LineCap,
            LineJoin = LineJoin,
            MiterLimit = MiterLimit
        };

        if (LineDash.Length > 0 || LineDashOffset != 0)
        {
            pen.DashStyle = new ImmutableDashStyle(LineDash, LineDashOffset);
        }

        return pen;
    }

    private readonly record struct CanvasFontProjection(Typeface Typeface, double Size, double WidthScale);
}

internal static class CanvasStrokeParser
{
    public static CanvasLineCap ParseLineCap(string? value, CanvasLineCap current)
    {
        return value?.Trim().ToLowerInvariant() switch
        {
            "round" => CanvasLineCap.Round,
            "square" => CanvasLineCap.Square,
            "butt" => CanvasLineCap.Butt,
            _ => current
        };
    }

    public static CanvasLineJoin ParseLineJoin(string? value, CanvasLineJoin current)
    {
        return value?.Trim().ToLowerInvariant() switch
        {
            "round" => CanvasLineJoin.Round,
            "bevel" => CanvasLineJoin.Bevel,
            "miter" => CanvasLineJoin.Miter,
            _ => current
        };
    }

    public static double[] ParseLineDash(object? value)
    {
        if (value is null || value is string || value is not IEnumerable enumerable)
        {
            return Array.Empty<double>();
        }

        var result = new List<double>();
        foreach (var item in enumerable)
        {
            if (!TryConvertToDouble(item, out var segment))
            {
                continue;
            }

            if (!double.IsFinite(segment) || segment < 0)
            {
                continue;
            }

            result.Add(segment);
        }

        return result.Count == 0 ? Array.Empty<double>() : result.ToArray();
    }

    private static bool TryConvertToDouble(object? value, out double result)
    {
        switch (value)
        {
            case null:
                result = 0;
                return false;
            case double doubleValue:
                result = doubleValue;
                return true;
            case float floatValue:
                result = floatValue;
                return true;
            case int intValue:
                result = intValue;
                return true;
            case long longValue:
                result = longValue;
                return true;
            case decimal decimalValue:
                result = (double)decimalValue;
                return true;
            default:
                try
                {
                    result = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                    return true;
                }
                catch
                {
                    result = 0;
                    return false;
                }
        }
    }
}

internal static class CanvasTextParser
{
    public static CanvasTextAlign ParseAlign(string? value, CanvasTextAlign current)
    {
        return value?.Trim().ToLowerInvariant() switch
        {
            "start" => CanvasTextAlign.Start,
            "end" => CanvasTextAlign.End,
            "left" => CanvasTextAlign.Left,
            "right" => CanvasTextAlign.Right,
            "center" => CanvasTextAlign.Center,
            _ => current
        };
    }

    public static string ToString(CanvasTextAlign align) => align switch
    {
        CanvasTextAlign.Start => "start",
        CanvasTextAlign.End => "end",
        CanvasTextAlign.Left => "left",
        CanvasTextAlign.Right => "right",
        CanvasTextAlign.Center => "center",
        _ => "start"
    };

    public static CanvasTextBaseline ParseBaseline(string? value, CanvasTextBaseline current)
    {
        return value?.Trim().ToLowerInvariant() switch
        {
            "alphabetic" => CanvasTextBaseline.Alphabetic,
            "top" => CanvasTextBaseline.Top,
            "hanging" => CanvasTextBaseline.Hanging,
            "middle" => CanvasTextBaseline.Middle,
            "ideographic" => CanvasTextBaseline.Ideographic,
            "bottom" => CanvasTextBaseline.Bottom,
            _ => current
        };
    }

    public static string ToString(CanvasTextBaseline baseline) => baseline switch
    {
        CanvasTextBaseline.Alphabetic => "alphabetic",
        CanvasTextBaseline.Top => "top",
        CanvasTextBaseline.Hanging => "hanging",
        CanvasTextBaseline.Middle => "middle",
        CanvasTextBaseline.Ideographic => "ideographic",
        CanvasTextBaseline.Bottom => "bottom",
        _ => "alphabetic"
    };
}

internal static class CanvasColorParser
{
    public static IImmutableBrush Parse(string? value, IImmutableBrush? current)
    {
        var fallback = current is ImmutableSolidColorBrush solid ? solid.Color : Colors.Black;
        var color = ParseColor(value, fallback);
        return new ImmutableSolidColorBrush(color);
    }

    public static Color ParseColor(string? value, Color fallback)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return fallback;
        }

        if (string.Equals(value, "transparent", StringComparison.OrdinalIgnoreCase))
        {
            return Colors.Transparent;
        }

        if (CssValueParser.TryParseColor(value, out var functionalColor))
        {
            return functionalColor;
        }

        try
        {
            var brush = Brush.Parse(value);
            if (brush is ISolidColorBrush solid)
            {
                return solid.Color;
            }
        }
        catch
        {
        }

        if (Color.TryParse(value, out var parsed))
        {
            return parsed;
        }

        return fallback;
    }
}

internal static class CanvasPaintParser
{
    private static readonly ConditionalWeakTable<CanvasPaintModel, ProjectedPaint> s_projectedPaints = new();

    public static bool TryCreatePaint(object? value, CanvasPaintModel current, out CanvasPaintModel paint, out object? stored)
    {
        switch (value)
        {
            case null:
                paint = current;
                stored = null;
                return false;
            case string s:
                var fallback = Project(current) is ImmutableSolidColorBrush solid
                    ? solid.Color
                    : Colors.Black;
                var parsed = CanvasColorParser.ParseColor(s, fallback);
                paint = new CanvasColorPaintModel(CanvasState.ToPortableColor(parsed));
                stored = s;
                return true;
            case CanvasGradient gradient:
                paint = new CanvasGradientPaintModel(gradient.Model);
                stored = gradient;
                return true;
            case CanvasPattern pattern:
                paint = pattern.Model;
                stored = pattern;
                return true;
            case IImmutableBrush immutable:
                paint = immutable is ImmutableSolidColorBrush immutableSolid
                    ? new CanvasColorPaintModel(CanvasState.ToPortableColor(immutableSolid.Color))
                    : new CanvasBackendPaintModel(WebScene.Core.WebSceneBackendHandle.Create(immutable));
                stored = immutable;
                return true;
            case ISolidColorBrush solidBrush:
                paint = new CanvasColorPaintModel(CanvasState.ToPortableColor(solidBrush.Color));
                stored = solidBrush;
                return true;
            default:
                var asString = value?.ToString();
                if (!string.IsNullOrWhiteSpace(asString))
                {
                    var currentColor = Project(current) is ImmutableSolidColorBrush currentSolid
                        ? currentSolid.Color
                        : Colors.Black;
                    paint = new CanvasColorPaintModel(CanvasState.ToPortableColor(
                        CanvasColorParser.ParseColor(asString, currentColor)));
                    stored = asString;
                    return true;
                }

                paint = current;
                stored = value;
                return false;
        }
    }

    public static IImmutableBrush? Project(CanvasPaintModel paint)
        => s_projectedPaints.GetValue(
            paint,
            static model => new ProjectedPaint(ProjectCore(model))).Brush;

    private static IImmutableBrush? ProjectCore(CanvasPaintModel paint)
        => paint switch
        {
            CanvasColorPaintModel color => new ImmutableSolidColorBrush(Color.FromArgb(
                color.Color.A,
                color.Color.R,
                color.Color.G,
                color.Color.B)),
            CanvasGradientPaintModel gradient => CanvasGradient.Project(gradient.Gradient),
            CanvasPatternPaintModel pattern => CanvasPattern.Project(pattern),
            CanvasBackendPaintModel backend when backend.Brush.TryGet<IImmutableBrush>(out var brush) => brush,
            _ => null
        };

    private sealed record ProjectedPaint(IImmutableBrush? Brush);
}

public sealed class CanvasPattern
{
    private readonly IImageBrushSource _source;
    private readonly double _width;
    private readonly double _height;
    private readonly string _repetition;
    private Matrix _transform = Matrix.Identity;

    internal CanvasPattern(IImageBrushSource source, double width, double height, string? repetition)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _width = Math.Max(1, width);
        _height = Math.Max(1, height);
        _repetition = string.IsNullOrWhiteSpace(repetition) ? "repeat" : repetition.Trim().ToLowerInvariant();
    }

    public void setTransform(object? transform)
    {
        if (CanvasMatrixParser.TryReadMatrix(transform, out var matrix))
        {
            _transform = matrix;
        }
    }

    internal CanvasPatternPaintModel Model => new(
        WebScene.Core.WebSceneBackendHandle.Create(_source),
        _width,
        _height,
        _repetition,
        new GraphicsTransform(
            _transform.M11,
            _transform.M12,
            _transform.M21,
            _transform.M22,
            _transform.M31,
            _transform.M32));

    internal IImmutableBrush ToImmutableBrush()
        => Project(Model);

    internal static IImmutableBrush Project(CanvasPatternPaintModel model)
    {
        if (!model.Source.TryGet<IImageBrushSource>(out var source))
        {
            return new ImmutableSolidColorBrush(Colors.Transparent);
        }

        var brush = new ImageBrush(source)
        {
            Stretch = Stretch.None,
            TileMode = GetTileMode(model.Repetition),
            SourceRect = new RelativeRect(new Rect(0, 0, 1, 1), RelativeUnit.Relative),
            DestinationRect = new RelativeRect(new Rect(0, 0, model.Width, model.Height), RelativeUnit.Absolute)
        };

        var transform = new Matrix(
            model.Transform.M11,
            model.Transform.M12,
            model.Transform.M21,
            model.Transform.M22,
            model.Transform.M31,
            model.Transform.M32);
        if (!transform.IsIdentity)
        {
            brush.Transform = new MatrixTransform(transform);
        }

        return brush.ToImmutable();
    }

    private static TileMode GetTileMode(string repetition)
        => repetition switch
        {
            "no-repeat" => TileMode.None,
            "repeat-x" => TileMode.Tile,
            "repeat-y" => TileMode.Tile,
            _ => TileMode.Tile
        };
}

internal static class CanvasMatrixParser
{
    public static bool TryReadMatrix(object? value, out Matrix matrix)
    {
        if (value is CanvasDomMatrix domMatrix)
        {
            matrix = new Matrix(domMatrix.a, domMatrix.b, domMatrix.c, domMatrix.d, domMatrix.e, domMatrix.f);
            return true;
        }

        if (value is not null && value is not string && value is IEnumerable enumerable)
        {
            var values = new List<double>(6);
            foreach (var item in enumerable)
            {
                if (TryConvertToDouble(item, out var number))
                {
                    values.Add(number);
                    if (values.Count == 6)
                    {
                        matrix = new Matrix(values[0], values[1], values[2], values[3], values[4], values[5]);
                        return true;
                    }
                }
            }
        }

        if (TryReadProperty(value, "a", out var a) &&
            TryReadProperty(value, "b", out var b) &&
            TryReadProperty(value, "c", out var c) &&
            TryReadProperty(value, "d", out var d) &&
            TryReadProperty(value, "e", out var e) &&
            TryReadProperty(value, "f", out var f))
        {
            matrix = new Matrix(a, b, c, d, e, f);
            return true;
        }

        if (TryReadProperty(value, "m11", out var m11) &&
            TryReadProperty(value, "m12", out var m12) &&
            TryReadProperty(value, "m21", out var m21) &&
            TryReadProperty(value, "m22", out var m22) &&
            TryReadProperty(value, "m41", out var m41) &&
            TryReadProperty(value, "m42", out var m42))
        {
            matrix = new Matrix(m11, m12, m21, m22, m41, m42);
            return true;
        }

        matrix = Matrix.Identity;
        return false;
    }

    private static bool TryReadProperty(object? value, string name, out double number)
    {
        number = 0;
        if (value is null)
        {
            return false;
        }

        if (value is null)
        {
            return false;
        }

        if (value is IDictionary dictionary && dictionary.Contains(name))
        {
            return TryConvertToDouble(dictionary[name], out number);
        }

        var propertyInfo = value.GetType().GetProperty(name);
        return propertyInfo is not null && TryConvertToDouble(propertyInfo.GetValue(value), out number);
    }

    private static bool TryConvertToDouble(object? value, out double number)
    {
        switch (value)
        {
            case double doubleValue:
                number = doubleValue;
                return double.IsFinite(number);
            case float floatValue:
                number = floatValue;
                return double.IsFinite(number);
            case int intValue:
                number = intValue;
                return true;
            case long longValue:
                number = longValue;
                return true;
            case decimal decimalValue:
                number = (double)decimalValue;
                return double.IsFinite(number);
            default:
                try
                {
                    number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                    return double.IsFinite(number);
                }
                catch
                {
                    number = 0;
                    return false;
                }
        }
    }
}

public abstract class CanvasGradient
{
    private readonly List<WebScene.Graphics.CanvasGradientStop> _stops = new();

    public void addColorStop(double offset, string color)
    {
        if (double.IsNaN(offset) || double.IsInfinity(offset))
        {
            return;
        }

        var clamped = Math.Clamp(offset, 0.0, 1.0);
        var parsed = CanvasColorParser.ParseColor(color, Colors.Transparent);
        _stops.Add(new WebScene.Graphics.CanvasGradientStop(
            clamped,
            new WebScene.Core.WebSceneColor(parsed.A, parsed.R, parsed.G, parsed.B)));
    }

    internal IReadOnlyList<WebScene.Graphics.CanvasGradientStop> Stops => _stops;

    internal abstract CanvasGradientModel Model { get; }

    internal static IImmutableBrush Project(CanvasGradientModel model)
    {
        ArgumentNullException.ThrowIfNull(model);
        switch (model)
        {
            case CanvasLinearGradientModel linear:
                var linearBrush = new LinearGradientBrush
                {
                    StartPoint = new RelativePoint(new Point(linear.Start.X, linear.Start.Y), RelativeUnit.Absolute),
                    EndPoint = new RelativePoint(new Point(linear.End.X, linear.End.Y), RelativeUnit.Absolute),
                    SpreadMethod = GradientSpreadMethod.Pad
                };
                linearBrush.GradientStops.Clear();
                foreach (var stop in EnumerateStops(linear))
                {
                    linearBrush.GradientStops.Add(stop);
                }
                return new ImmutableLinearGradientBrush(linearBrush);

            case CanvasRadialGradientModel radial:
                var radialBrush = new RadialGradientBrush
                {
                    GradientOrigin = new RelativePoint(new Point(radial.StartCenter.X, radial.StartCenter.Y), RelativeUnit.Absolute),
                    Center = new RelativePoint(new Point(radial.EndCenter.X, radial.EndCenter.Y), RelativeUnit.Absolute),
                    RadiusX = new RelativeScalar(radial.EndRadius, RelativeUnit.Absolute),
                    RadiusY = new RelativeScalar(radial.EndRadius, RelativeUnit.Absolute),
                    SpreadMethod = GradientSpreadMethod.Pad
                };
                radialBrush.GradientStops.Clear();
                foreach (var stop in EnumerateStops(radial))
                {
                    radialBrush.GradientStops.Add(stop);
                }
                return new ImmutableRadialGradientBrush(radialBrush);

            default:
                throw new NotSupportedException($"Unsupported Canvas gradient model {model.GetType().Name}.");
        }
    }

    protected static IEnumerable<GradientStop> EnumerateStops(CanvasGradientModel model)
    {
        if (model.Stops.Count == 0)
        {
            yield return new GradientStop(Colors.Transparent, 0);
            yield return new GradientStop(Colors.Transparent, 1);
            yield break;
        }

        foreach (var stop in model.Stops)
        {
            yield return new GradientStop(
                Color.FromArgb(stop.Color.A, stop.Color.R, stop.Color.G, stop.Color.B),
                stop.Offset);
        }
    }

    internal abstract IImmutableBrush ToImmutableBrush();
}

public sealed class CanvasLinearGradient : CanvasGradient
{
    private readonly double _x0;
    private readonly double _y0;
    private readonly double _x1;
    private readonly double _y1;

    public CanvasLinearGradient(double x0, double y0, double x1, double y1)
    {
        _x0 = x0;
        _y0 = y0;
        _x1 = x1;
        _y1 = y1;
    }

    internal override CanvasGradientModel Model => new CanvasLinearGradientModel(
        new WebScene.Core.WebScenePoint(_x0, _y0),
        new WebScene.Core.WebScenePoint(_x1, _y1),
        [.. Stops]);

    internal override IImmutableBrush ToImmutableBrush()
        => Project(Model);
}

public sealed class CanvasRadialGradient : CanvasGradient
{
    private readonly double _x0;
    private readonly double _y0;
    private readonly double _r0;
    private readonly double _x1;
    private readonly double _y1;
    private readonly double _r1;

    public CanvasRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1)
    {
        _x0 = x0;
        _y0 = y0;
        _r0 = Math.Max(0, r0);
        _x1 = x1;
        _y1 = y1;
        _r1 = Math.Max(0, r1);
    }

    internal override CanvasGradientModel Model => new CanvasRadialGradientModel(
        new WebScene.Core.WebScenePoint(_x0, _y0),
        _r0,
        new WebScene.Core.WebScenePoint(_x1, _y1),
        _r1,
        [.. Stops]);

    internal override IImmutableBrush ToImmutableBrush()
        => Project(Model);
}

internal static class CanvasFontParser
{
    public static void Apply(CanvasState state, string font, CssFontFaceRegistry? fontFaces = null)
    {
        if (state is null)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(font))
        {
            return;
        }

        var tokens = Tokenize(font);
        double size = state.FontSize;
        FontFamily family = state.Typeface.FontFamily;
        FontStyle style = state.Typeface.Style;
        FontWeight weight = state.Typeface.Weight;
        var sizeIndex = -1;

        for (var index = 0; index < tokens.Count; index++)
        {
            var token = tokens[index];
            if (TryParseSize(token, out var px))
            {
                size = px;
                sizeIndex = index;
                break;
            }
        }

        var descriptorEnd = sizeIndex >= 0 ? sizeIndex : tokens.Count;
        for (var index = 0; index < descriptorEnd; index++)
        {
            var token = tokens[index];
            if (string.Equals(token, "italic", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(token, "oblique", StringComparison.OrdinalIgnoreCase))
            {
                style = FontStyle.Italic;
            }
            else if (string.Equals(token, "normal", StringComparison.OrdinalIgnoreCase))
            {
                style = FontStyle.Normal;
                weight = FontWeight.Normal;
            }
            else if (string.Equals(token, "bold", StringComparison.OrdinalIgnoreCase))
            {
                weight = FontWeight.Bold;
            }
            else if (string.Equals(token, "bolder", StringComparison.OrdinalIgnoreCase))
            {
                weight = FontWeight.Bold;
            }
            else if (string.Equals(token, "lighter", StringComparison.OrdinalIgnoreCase))
            {
                weight = FontWeight.Light;
            }
            else if (int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out var numericWeight))
            {
                weight = (FontWeight)Math.Clamp(numericWeight, 1, 1000);
            }
        }

        if (sizeIndex >= 0 && sizeIndex + 1 < tokens.Count)
        {
            var familyStart = sizeIndex + 1;
            if (familyStart + 1 < tokens.Count && tokens[familyStart] == "/")
            {
                familyStart += 2;
            }
            var resolution = CssFontResolver.Resolve(
                JoinTokens(tokens, familyStart),
                fontFaces,
                style,
                weight,
                FontStretch.Normal);
            family = resolution.Family;
            state.FontWidthScale = resolution.ResolveWidthScale(size, weight);
        }

        state.FontSize = size;
        state.Typeface = new Typeface(family, style, weight);
    }

    private static bool TryParseSize(string token, out double size)
    {
        size = 0;
        if (string.IsNullOrWhiteSpace(token))
        {
            return false;
        }

        var slash = token.IndexOf('/');
        var sizeToken = slash >= 0 ? token[..slash] : token;
        if (!sizeToken.EndsWith("px", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        return double.TryParse(
            sizeToken.AsSpan(0, sizeToken.Length - 2),
            NumberStyles.Number,
            CultureInfo.InvariantCulture,
            out size);
    }

    private static List<string> Tokenize(string value)
    {
        var result = new List<string>();
        var start = -1;
        var quote = '\0';

        for (var index = 0; index < value.Length; index++)
        {
            var c = value[index];
            if (quote != '\0')
            {
                if (c == quote)
                {
                    quote = '\0';
                }

                continue;
            }

            if (c is '\'' or '"')
            {
                if (start < 0)
                {
                    start = index;
                }

                quote = c;
                continue;
            }

            if (char.IsWhiteSpace(c))
            {
                if (start >= 0)
                {
                    result.Add(value[start..index]);
                    start = -1;
                }

                continue;
            }

            if (start < 0)
            {
                start = index;
            }
        }

        if (start >= 0)
        {
            result.Add(value[start..]);
        }

        return result;
    }

    private static string JoinTokens(IReadOnlyList<string> tokens, int start)
    {
        if (start >= tokens.Count)
        {
            return string.Empty;
        }

        var builder = new System.Text.StringBuilder(tokens[start]);
        for (var index = start + 1; index < tokens.Count; index++)
        {
            builder.Append(' ');
            builder.Append(tokens[index]);
        }

        return builder.ToString();
    }

}

/// <summary>
/// Retained browser Path2D data. SVG path strings are parsed by Avalonia's
/// geometry parser while imperative path calls share the canvas path builder.
/// </summary>
public sealed class CanvasPath2D
{
    private readonly CanvasPathBuilder _builder;

    public CanvasPath2D(object? pathInfo = null)
    {
        if (pathInfo is string pathData && !string.IsNullOrWhiteSpace(pathData))
        {
            _builder = new CanvasPathBuilder(new CanvasPathModel(pathData));
        }
        else if (TryResolve(pathInfo, out var source))
        {
            _builder = new CanvasPathBuilder(source.Model.Snapshot());
        }
        else
        {
            _builder = new CanvasPathBuilder();
        }
    }

    internal CanvasPathModel Model => _builder.Model;

    public void closePath() => _builder.ClosePath();
    public void moveTo(double x, double y) => _builder.MoveTo(x, y);
    public void lineTo(double x, double y) => _builder.LineTo(x, y);
    public void bezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y)
        => _builder.CubicBezierTo(cp1x, cp1y, cp2x, cp2y, x, y);
    public void quadraticCurveTo(double cpx, double cpy, double x, double y)
        => _builder.QuadraticBezierTo(cpx, cpy, x, y);
    public void arc(double x, double y, double radius, double startAngle, double endAngle, bool counterClockwise = false)
        => _builder.Arc(x, y, radius, startAngle, endAngle, counterClockwise);
    public void arcTo(double x1, double y1, double x2, double y2, double radius)
        => _builder.ArcTo(x1, y1, x2, y2, radius);
    public void rect(double x, double y, double width, double height)
        => _builder.Rect(x, y, width, height);

    public void addPath(object? path, double a, double b, double c, double d, double e, double f)
    {
        if (!TryResolve(path, out var source)
            || !double.IsFinite(a)
            || !double.IsFinite(b)
            || !double.IsFinite(c)
            || !double.IsFinite(d)
            || !double.IsFinite(e)
            || !double.IsFinite(f))
        {
            return;
        }

        _builder.Model.AddPart(source.Model, new GraphicsTransform(a, b, c, d, e, f));
    }

    internal Geometry BuildGeometry() => CanvasPathBuilder.BuildGeometry(Model);

    internal static bool TryResolve(object? value, [NotNullWhen(true)] out CanvasPath2D? path)
    {
        if (value is CanvasPath2D nativePath)
        {
            path = nativePath;
            return true;
        }

        if (value is IDictionary<string, object?> genericDictionary
            && genericDictionary.TryGetValue("__webSceneNativePath2D", out var genericNative)
            && genericNative is CanvasPath2D genericDictionaryPath)
        {
            path = genericDictionaryPath;
            return true;
        }

        if (value is IDictionary dictionary
            && dictionary.Contains("__webSceneNativePath2D")
            && dictionary["__webSceneNativePath2D"] is CanvasPath2D dictionaryPath)
        {
            path = dictionaryPath;
            return true;
        }

        path = null;
        return false;
    }
}

internal sealed class CanvasPathBuilder
{
    private CanvasPathModel _model;

    internal CanvasPathBuilder(CanvasPathModel? model = null)
    {
        _model = model ?? new CanvasPathModel();
    }

    public bool IsEmpty => _model.Commands.Count == 0
                           && _model.SvgPathData is null
                           && _model.Parts.Count == 0;

    internal CanvasPathModel Model => _model;

    internal static Geometry BuildGeometry(CanvasPathModel model)
    {
        ArgumentNullException.ThrowIfNull(model);
        // Geometry projection is read-only. Retained commands and clip entries
        // already own detached path snapshots, so cloning the complete path a
        // second time here only adds hot-frame allocation.
        return new CanvasPathBuilder(model).BuildGeometry();
    }

    public void Clear() => _model = new CanvasPathModel();

    public void ClosePath() => _model.Add(new CanvasPathCommand(CanvasPathCommandKind.ClosePath));

    public void MoveTo(double x, double y)
        => _model.Add(new CanvasPathCommand(CanvasPathCommandKind.MoveTo, x, y));

    public void LineTo(double x, double y)
        => _model.Add(new CanvasPathCommand(CanvasPathCommandKind.LineTo, x, y));

    public void CubicBezierTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y)
        => _model.Add(new CanvasPathCommand(
            CanvasPathCommandKind.CubicBezierTo,
            cp1x,
            cp1y,
            cp2x,
            cp2y,
            x,
            y));

    public void QuadraticBezierTo(double cpx, double cpy, double x, double y)
        => _model.Add(new CanvasPathCommand(CanvasPathCommandKind.QuadraticBezierTo, cpx, cpy, x, y));

    public void Arc(double x, double y, double radius, double startAngle, double endAngle, bool counterClockwise)
        => _model.Add(new CanvasPathCommand(
            CanvasPathCommandKind.Arc,
            x,
            y,
            startAngle,
            endAngle,
            Radius: radius,
            Flag: counterClockwise));

    public void ArcTo(double x1, double y1, double x2, double y2, double radius)
        => _model.Add(new CanvasPathCommand(
            CanvasPathCommandKind.ArcTo,
            x1,
            y1,
            x2,
            y2,
            Radius: radius));

    public void Rect(double x, double y, double width, double height)
        => _model.Add(new CanvasPathCommand(CanvasPathCommandKind.Rect, x, y, width, height));

    private static void EnsureFigure(StreamGeometryContext context, ref Point currentPoint, ref Point figureStart, ref bool figureOpen)
    {
        if (!figureOpen)
        {
            context.BeginFigure(currentPoint, true);
            figureStart = currentPoint;
            figureOpen = true;
        }
    }

    private static bool AreClose(Point a, Point b)
        => Math.Abs(a.X - b.X) < 0.001 && Math.Abs(a.Y - b.Y) < 0.001;

    public Geometry BuildGeometry()
    {
        Geometry? result = null;
        if (_model.SvgPathData is { } pathData)
        {
            try
            {
                result = Geometry.Parse(pathData);
            }
            catch
            {
                // Browser Path2D construction is permissive for invalid path data.
            }
        }

        if (_model.Commands.Count > 0)
        {
            var commandGeometry = BuildCommandGeometry();
            result = result is null
                ? commandGeometry
                : new CombinedGeometry(GeometryCombineMode.Union, result, commandGeometry);
        }

        foreach (var part in _model.Parts)
        {
            var geometry = BuildGeometry(part.Path).Clone();
            var transform = CanvasState.ToAvaloniaTransform(part.Transform);
            if (!transform.IsIdentity)
            {
                geometry.Transform = new MatrixTransform(transform);
            }

            result = result is null
                ? geometry
                : new CombinedGeometry(GeometryCombineMode.Union, result, geometry);
        }

        return result ?? new StreamGeometry();
    }

    private StreamGeometry BuildCommandGeometry()
    {
        var geometry = new StreamGeometry();
        using (var ctx = geometry.Open())
        {
            if (TryBuildVisuallyCircularRoundedRect(ctx))
            {
                return geometry;
            }

            var current = new Point();
            var figureStart = new Point();
            var figureOpen = false;

            foreach (var cmd in _model.Commands)
            {
                switch (cmd.Kind)
                {
                    case CanvasPathCommandKind.MoveTo:
                        {
                            if (figureOpen)
                            {
                                ctx.EndFigure(false);
                            }
                            var pt = new Point(cmd.X1, cmd.Y1);
                            ctx.BeginFigure(pt, true);
                            figureOpen = true;
                            figureStart = pt;
                            current = pt;
                        }
                        break;

                    case CanvasPathCommandKind.LineTo:
                        {
                            var pt = new Point(cmd.X1, cmd.Y1);
                            EnsureFigure(ctx, ref current, ref figureStart, ref figureOpen);
                            ctx.LineTo(pt);
                            current = pt;
                        }
                        break;

                    case CanvasPathCommandKind.CubicBezierTo:
                        {
                            var cp1 = new Point(cmd.X1, cmd.Y1);
                            var cp2 = new Point(cmd.X2, cmd.Y2);
                            var pt = new Point(cmd.X3, cmd.Y3);
                            EnsureFigure(ctx, ref current, ref figureStart, ref figureOpen);
                            ctx.CubicBezierTo(cp1, cp2, pt);
                            current = pt;
                        }
                        break;

                    case CanvasPathCommandKind.QuadraticBezierTo:
                        {
                            var cp = new Point(cmd.X1, cmd.Y1);
                            var pt = new Point(cmd.X2, cmd.Y2);
                            EnsureFigure(ctx, ref current, ref figureStart, ref figureOpen);
                            ctx.QuadraticBezierTo(cp, pt);
                            current = pt;
                        }
                        break;

                    case CanvasPathCommandKind.ArcTo:
                        {
                            var corner = new Point(cmd.X1, cmd.Y1);
                            var target = new Point(cmd.X2, cmd.Y2);
                            var radius = Math.Max(0, cmd.Radius);

                            EnsureFigure(ctx, ref current, ref figureStart, ref figureOpen);

                            var v1X = current.X - corner.X;
                            var v1Y = current.Y - corner.Y;
                            var v2X = target.X - corner.X;
                            var v2Y = target.Y - corner.Y;
                            var len1 = Math.Sqrt(v1X * v1X + v1Y * v1Y);
                            var len2 = Math.Sqrt(v2X * v2X + v2Y * v2Y);

                            if (radius <= 0 || len1 <= double.Epsilon || len2 <= double.Epsilon)
                            {
                                ctx.LineTo(corner);
                                current = corner;
                                break;
                            }

                            var dot = ((v1X * v2X) + (v1Y * v2Y)) / (len1 * len2);
                            dot = Math.Clamp(dot, -1, 1);
                            var angle = Math.Acos(dot);
                            if (angle <= double.Epsilon || Math.Abs(Math.PI - angle) <= double.Epsilon)
                            {
                                ctx.LineTo(corner);
                                current = corner;
                                break;
                            }

                            var tangentDistance = radius / Math.Tan(angle / 2);
                            if (!double.IsFinite(tangentDistance) || tangentDistance <= 0)
                            {
                                ctx.LineTo(corner);
                                current = corner;
                                break;
                            }

                            var tangent1 = new Point(
                                corner.X + (v1X / len1 * tangentDistance),
                                corner.Y + (v1Y / len1 * tangentDistance));
                            var tangent2 = new Point(
                                corner.X + (v2X / len2 * tangentDistance),
                                corner.Y + (v2Y / len2 * tangentDistance));

                            var incomingX = corner.X - current.X;
                            var incomingY = corner.Y - current.Y;
                            var outgoingX = target.X - corner.X;
                            var outgoingY = target.Y - corner.Y;
                            var cross = incomingX * outgoingY - incomingY * outgoingX;
                            if (Math.Abs(cross) <= double.Epsilon)
                            {
                                ctx.LineTo(corner);
                                current = corner;
                                break;
                            }

                            ctx.LineTo(tangent1);
                            ctx.ArcTo(
                                tangent2,
                                new Size(radius, radius),
                                0,
                                false,
                                cross > 0 ? SweepDirection.Clockwise : SweepDirection.CounterClockwise);
                            current = tangent2;
                        }
                        break;

                    case CanvasPathCommandKind.Arc:
                        {
                            var center = new Point(cmd.X1, cmd.Y1);
                            var radius = Math.Abs(cmd.Radius);
                            var startAngle = cmd.X2;
                            var endAngle = cmd.Y2;
                            var counterClockwise = cmd.Flag;

                            if (radius <= 0)
                            {
                                break;
                            }

                            const double twoPi = Math.PI * 2;
                            var sweep = endAngle - startAngle;
                            if (!counterClockwise && sweep >= twoPi)
                            {
                                sweep = twoPi;
                            }
                            else if (counterClockwise && sweep <= -twoPi)
                            {
                                sweep = -twoPi;
                            }
                            else if (!counterClockwise && sweep < 0)
                            {
                                sweep += twoPi;
                            }
                            else if (counterClockwise && sweep > 0)
                            {
                                sweep -= twoPi;
                            }

                            if (Math.Abs(sweep) <= double.Epsilon)
                            {
                                break;
                            }

                            var segments = Math.Max(1, (int)Math.Ceiling(Math.Abs(sweep) / (Math.PI / 2)));
                            var step = sweep / segments;
                            var angle = startAngle;

                            Func<double, Point> getPoint = a => new Point(
                                center.X + radius * Math.Cos(a),
                                center.Y + radius * Math.Sin(a));

                            var startPoint = getPoint(angle);

                            if (!figureOpen)
                            {
                                ctx.BeginFigure(startPoint, true);
                                figureStart = startPoint;
                                figureOpen = true;
                            }
                            else if (!AreClose(current, startPoint))
                            {
                                ctx.LineTo(startPoint);
                            }

                            current = startPoint;

                            for (int i = 0; i < segments; i++)
                            {
                                var nextAngle = angle + step;
                                var endPoint = getPoint(nextAngle);
                                var k = 4.0 / 3.0 * Math.Tan(step / 4.0);
                                var control1 = new Point(
                                    current.X - radius * k * Math.Sin(angle),
                                    current.Y + radius * k * Math.Cos(angle));
                                var control2 = new Point(
                                    endPoint.X + radius * k * Math.Sin(nextAngle),
                                    endPoint.Y - radius * k * Math.Cos(nextAngle));

                                ctx.CubicBezierTo(control1, control2, endPoint);
                                current = endPoint;
                                angle = nextAngle;
                            }
                        }
                        break;

                    case CanvasPathCommandKind.Rect:
                        {
                            var r = new Rect(cmd.X1, cmd.Y1, cmd.X2, cmd.Y2);
                            if (r.Width <= 0 || r.Height <= 0)
                            {
                                break;
                            }

                            var p1 = r.TopLeft;
                            var p2 = r.TopRight;
                            var p3 = r.BottomRight;
                            var p4 = r.BottomLeft;

                            ctx.BeginFigure(p1, true);
                            ctx.LineTo(p2);
                            ctx.LineTo(p3);
                            ctx.LineTo(p4);
                            ctx.EndFigure(true);

                            figureOpen = false;
                            current = p1;
                            figureStart = p1;
                        }
                        break;

                    case CanvasPathCommandKind.ClosePath:
                        {
                            if (figureOpen)
                            {
                                ctx.EndFigure(true);
                                figureOpen = false;
                                current = figureStart;
                            }
                        }
                        break;
                }
            }

            if (figureOpen)
            {
                ctx.EndFigure(false);
            }
        }
        return geometry;
    }

    private bool TryBuildVisuallyCircularRoundedRect(StreamGeometryContext context)
    {
        // Chromium's antialiasing makes a square rounded rectangle with only a
        // one-pixel radius deficit read as a circle. Skia retains the resulting
        // two-pixel axial segments much more distinctly at high DPI, which is
        // especially visible around small canvas badges. Canonicalize only the
        // tightly constrained four-arc shape; normal rounded rectangles keep
        // their exact Canvas2D geometry.
        var commands = _model.Commands;
        if (commands.Count != 10
            || commands[0].Kind != CanvasPathCommandKind.MoveTo
            || commands[1].Kind != CanvasPathCommandKind.LineTo
            || commands[2].Kind != CanvasPathCommandKind.ArcTo
            || commands[3].Kind != CanvasPathCommandKind.LineTo
            || commands[4].Kind != CanvasPathCommandKind.ArcTo
            || commands[5].Kind != CanvasPathCommandKind.LineTo
            || commands[6].Kind != CanvasPathCommandKind.ArcTo
            || commands[7].Kind != CanvasPathCommandKind.LineTo
            || commands[8].Kind != CanvasPathCommandKind.ArcTo
            || commands[9].Kind != CanvasPathCommandKind.ClosePath)
        {
            return false;
        }

        const double tolerance = 0.001;
        var topRight = commands[2];
        var bottomRight = commands[4];
        var bottomLeft = commands[6];
        var topLeft = commands[8];
        var left = topLeft.X1;
        var top = topLeft.Y1;
        var right = topRight.X1;
        var bottom = bottomRight.Y1;
        var width = right - left;
        var height = bottom - top;
        var radius = topRight.Radius;

        static bool Close(double first, double second)
            => Math.Abs(first - second) <= tolerance;

        if (width <= 0
            || height <= 0
            || !Close(width, height)
            || radius <= 0
            || radius > width / 2 + tolerance
            || width - radius * 2 > 2 + tolerance
            || !Close(bottomRight.X1, right)
            || !Close(bottomLeft.X1, left)
            || !Close(bottomLeft.Y1, bottom)
            || !Close(topRight.Y1, top)
            || !Close(topLeft.X1, left)
            || !Close(topLeft.Y1, top)
            || !Close(bottomRight.Radius, radius)
            || !Close(bottomLeft.Radius, radius)
            || !Close(topLeft.Radius, radius)
            || !Close(commands[0].X1, left + radius)
            || !Close(commands[0].Y1, top)
            || !Close(commands[1].X1, right - radius)
            || !Close(commands[1].Y1, top)
            || !Close(topRight.X2, right)
            || !Close(topRight.Y2, top + radius)
            || !Close(commands[3].X1, right)
            || !Close(commands[3].Y1, bottom - radius)
            || !Close(bottomRight.X2, right - radius)
            || !Close(bottomRight.Y2, bottom)
            || !Close(commands[5].X1, left + radius)
            || !Close(commands[5].Y1, bottom)
            || !Close(bottomLeft.X2, left)
            || !Close(bottomLeft.Y2, bottom - radius)
            || !Close(commands[7].X1, left)
            || !Close(commands[7].Y1, top + radius)
            || !Close(topLeft.X2, left + radius)
            || !Close(topLeft.Y2, top))
        {
            return false;
        }

        var center = new Point((left + right) / 2, (top + bottom) / 2);
        var circleRadius = width / 2;
        var arcSize = new Size(circleRadius, circleRadius);
        context.BeginFigure(new Point(center.X, top), true);
        context.ArcTo(new Point(right, center.Y), arcSize, 0, false, SweepDirection.Clockwise);
        context.ArcTo(new Point(center.X, bottom), arcSize, 0, false, SweepDirection.Clockwise);
        context.ArcTo(new Point(left, center.Y), arcSize, 0, false, SweepDirection.Clockwise);
        context.ArcTo(new Point(center.X, top), arcSize, 0, false, SweepDirection.Clockwise);
        context.EndFigure(true);
        return true;
    }
}

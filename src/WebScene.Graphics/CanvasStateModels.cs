using WebScene.Core;

namespace WebScene.Graphics;

public enum CanvasLineCap
{
    Butt,
    Round,
    Square
}

public enum CanvasLineJoin
{
    Miter,
    Round,
    Bevel
}

public enum CanvasTextAlign
{
    Start,
    End,
    Left,
    Right,
    Center
}

public enum CanvasTextBaseline
{
    Top,
    Hanging,
    Middle,
    Alphabetic,
    Ideographic,
    Bottom
}

public enum CanvasImageSmoothingQuality
{
    Low,
    Medium,
    High
}

public enum CanvasCompositeOperation
{
    SourceOver,
    SourceIn,
    SourceOut,
    SourceAtop,
    DestinationOver,
    DestinationIn,
    DestinationOut,
    DestinationAtop,
    Lighter,
    Copy,
    Xor,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten
}

public abstract record CanvasPaintModel;

public sealed record CanvasColorPaintModel(WebSceneColor Color) : CanvasPaintModel;

public sealed record CanvasGradientPaintModel(CanvasGradientModel Gradient) : CanvasPaintModel;

public sealed record CanvasPatternPaintModel(
    WebSceneBackendHandle Source,
    double Width,
    double Height,
    string Repetition,
    GraphicsTransform Transform) : CanvasPaintModel;

public sealed record CanvasBackendPaintModel(WebSceneBackendHandle Brush) : CanvasPaintModel;

public sealed record CanvasClipModel(CanvasPathModel Path, GraphicsTransform Transform)
{
    public CanvasClipModel Snapshot() => new(Path.Snapshot(), Transform);
}

public sealed record CanvasShadowModel(
    WebSceneColor Color,
    double Blur,
    double OffsetX,
    double OffsetY)
{
    public static CanvasShadowModel None { get; } = new(WebSceneColor.Transparent, 0, 0, 0);
}

/// <summary>
/// Immutable, backend-neutral Canvas 2D drawing state captured by retained commands.
/// Mutable contexts replace the record when a property changes and snapshots may be
/// shared safely by render backends.
/// </summary>
public sealed record CanvasStateModel
{
    public static CanvasStateModel Default { get; } = new();

    public GraphicsTransform Transform { get; init; } = GraphicsTransform.Identity;

    public CanvasPaintModel FillStyle { get; init; } = new CanvasColorPaintModel(WebSceneColor.FromRgb(0, 0, 0));

    public CanvasPaintModel StrokeStyle { get; init; } = new CanvasColorPaintModel(WebSceneColor.FromRgb(0, 0, 0));

    public double GlobalAlpha { get; init; } = 1;

    public CanvasCompositeOperation CompositeOperation { get; init; } = CanvasCompositeOperation.SourceOver;

    public double LineWidth { get; init; } = 1;

    public CanvasLineCap LineCap { get; init; } = CanvasLineCap.Butt;

    public CanvasLineJoin LineJoin { get; init; } = CanvasLineJoin.Miter;

    public double MiterLimit { get; init; } = 10;

    public IReadOnlyList<double> LineDash { get; init; } = Array.Empty<double>();

    public double LineDashOffset { get; init; }

    public string Font { get; init; } = "10px sans-serif";

    public CanvasTextAlign TextAlign { get; init; } = CanvasTextAlign.Start;

    public CanvasTextBaseline TextBaseline { get; init; } = CanvasTextBaseline.Alphabetic;

    public bool ImageSmoothingEnabled { get; init; } = true;

    public CanvasImageSmoothingQuality ImageSmoothingQuality { get; init; } = CanvasImageSmoothingQuality.Low;

    public CanvasShadowModel Shadow { get; init; } = CanvasShadowModel.None;

    public IReadOnlyList<CanvasClipModel> Clips { get; init; } = Array.Empty<CanvasClipModel>();

    public CanvasStateModel Snapshot()
        => this with
        {
            LineDash = LineDash.ToArray(),
            Clips = Clips.Select(static clip => clip.Snapshot()).ToArray()
        };
}

public enum CanvasDisplayCommandKind
{
    FillPath,
    StrokePath,
    FillRectangle,
    StrokeRectangle,
    ClearRectangle,
    FillText,
    StrokeText,
    DrawImage,
    PutImageData
}

/// <summary>A retained Canvas operation with a complete state snapshot.</summary>
public sealed record CanvasDisplayCommand(
    CanvasDisplayCommandKind Kind,
    CanvasStateModel State,
    CanvasPathModel? Path = null,
    WebSceneRect Rectangle = default,
    string? Text = null,
    WebScenePoint Origin = default,
    CanvasImageDataModel? ImageData = null,
    WebSceneRect SourceRectangle = default,
    WebSceneRect DestinationRectangle = default,
    WebSceneBackendHandle Resource = default);

public sealed class CanvasDisplayListModel
{
    private readonly List<CanvasDisplayCommand> _commands = [];

    public IReadOnlyList<CanvasDisplayCommand> Commands => _commands;

    public void Add(CanvasDisplayCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        _commands.Add(command with
        {
            State = command.State.Snapshot(),
            Path = command.Path?.Snapshot(),
            ImageData = command.ImageData is null
                ? null
                : new CanvasImageDataModel(
                    command.ImageData.Width,
                    command.ImageData.Height,
                    (byte[])command.ImageData.RgbaPixels.Clone())
        });
    }

    /// <summary>
    /// Adds a command whose state, path and image data have already been detached
    /// from their mutable authoring objects. Backend adapters use this internal path
    /// to preserve immutable retained data without deep-copying large clip paths for
    /// every draw operation.
    /// </summary>
    internal void AddRetained(CanvasDisplayCommand command)
    {
        ArgumentNullException.ThrowIfNull(command);
        _commands.Add(command);
    }

    public CanvasDisplayListModel Snapshot()
    {
        var snapshot = new CanvasDisplayListModel();
        foreach (var command in _commands)
        {
            snapshot.Add(command);
        }
        return snapshot;
    }
}

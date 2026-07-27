using WebScene.Core;

namespace WebScene.Graphics;

public enum CanvasPathCommandKind : byte
{
    MoveTo,
    LineTo,
    CubicBezierTo,
    QuadraticBezierTo,
    Arc,
    ArcTo,
    Rect,
    ClosePath
}

public readonly record struct CanvasPathCommand(
    CanvasPathCommandKind Kind,
    double X1 = 0,
    double Y1 = 0,
    double X2 = 0,
    double Y2 = 0,
    double X3 = 0,
    double Y3 = 0,
    double Radius = 0,
    bool Flag = false);

public sealed class CanvasPathModel
{
    private readonly List<CanvasPathCommand> _commands = [];
    private readonly List<CanvasPathPartModel> _parts = [];

    public CanvasPathModel(string? svgPathData = null)
    {
        SvgPathData = string.IsNullOrWhiteSpace(svgPathData) ? null : svgPathData;
    }

    public IReadOnlyList<CanvasPathCommand> Commands => _commands;

    public string? SvgPathData { get; }

    public IReadOnlyList<CanvasPathPartModel> Parts => _parts;

    public void Add(in CanvasPathCommand command) => _commands.Add(command);

    public void AddPart(CanvasPathModel path, GraphicsTransform transform)
    {
        ArgumentNullException.ThrowIfNull(path);
        _parts.Add(new CanvasPathPartModel(path.Snapshot(), transform));
    }

    public CanvasPathModel Snapshot()
    {
        var snapshot = new CanvasPathModel(SvgPathData);
        snapshot._commands.AddRange(_commands);
        snapshot._parts.AddRange(_parts.Select(static part => part.Snapshot()));
        return snapshot;
    }
}

public sealed record CanvasPathPartModel(CanvasPathModel Path, GraphicsTransform Transform)
{
    public CanvasPathPartModel Snapshot() => new(Path.Snapshot(), Transform);
}

public readonly record struct CanvasGradientStop(double Offset, WebSceneColor Color);

public abstract record CanvasGradientModel(IReadOnlyList<CanvasGradientStop> Stops);

public sealed record CanvasLinearGradientModel(
    WebScenePoint Start,
    WebScenePoint End,
    IReadOnlyList<CanvasGradientStop> Stops) : CanvasGradientModel(Stops);

public sealed record CanvasRadialGradientModel(
    WebScenePoint StartCenter,
    double StartRadius,
    WebScenePoint EndCenter,
    double EndRadius,
    IReadOnlyList<CanvasGradientStop> Stops) : CanvasGradientModel(Stops);

public sealed class CanvasImageDataModel
{
    public CanvasImageDataModel(int width, int height, byte[] rgbaPixels)
    {
        if (width < 0 || height < 0)
        {
            throw new ArgumentOutOfRangeException(width < 0 ? nameof(width) : nameof(height));
        }
        ArgumentNullException.ThrowIfNull(rgbaPixels);
        if (rgbaPixels.Length != checked(width * height * 4))
        {
            throw new ArgumentException("RGBA data length must equal width × height × 4.", nameof(rgbaPixels));
        }

        Width = width;
        Height = height;
        RgbaPixels = rgbaPixels;
    }

    public int Width { get; }

    public int Height { get; }

    public byte[] RgbaPixels { get; }
}

public enum SvgSceneNodeKind
{
    Group,
    Path,
    Circle,
    Rectangle,
    Text,
    Image
}

public readonly record struct GraphicsTransform(double M11, double M12, double M21, double M22, double M31, double M32)
{
    public static GraphicsTransform Identity { get; } = new(1, 0, 0, 1, 0, 0);
}

public sealed record SvgPaint(WebSceneColor Color, double Opacity = 1);

public sealed class SvgSceneNode
{
    private readonly List<SvgSceneNode> _children = [];

    public SvgSceneNode(long id, SvgSceneNodeKind kind)
    {
        if (id == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(id));
        }
        Id = id;
        Kind = kind;
    }

    public long Id { get; }

    public SvgSceneNodeKind Kind { get; }

    public GraphicsTransform Transform { get; init; } = GraphicsTransform.Identity;

    public WebSceneRect Bounds { get; init; }

    public string? PathData { get; init; }

    public string? Text { get; init; }

    public string? ResourceUri { get; init; }

    /// <summary>
    /// Optional decoded backend resource. The URI remains the portable identity;
    /// adapters may attach an opaque cache handle after resolving it.
    /// </summary>
    public WebSceneBackendHandle Resource { get; init; }

    public SvgPaint? Fill { get; init; }

    public SvgPaint? Stroke { get; init; }

    public double StrokeWidth { get; init; } = 1;

    public IReadOnlyList<SvgSceneNode> Children => _children;

    public void Add(SvgSceneNode child) => _children.Add(child ?? throw new ArgumentNullException(nameof(child)));
}

public sealed record SvgScene(
    WebSceneRect ViewBox,
    SvgSceneNode Root,
    long Revision,
    bool StretchViewBox = false);

public readonly record struct TextMeasureRequest(string Text, string FontFamily, double FontSize, int FontWeight = 400);

public interface IWebSceneTextMeasurer
{
    WebSceneSize Measure(in TextMeasureRequest request);
}

public interface IWebSceneImageDecoder
{
    ValueTask<DecodedImage> DecodeAsync(ReadOnlyMemory<byte> encoded, CancellationToken cancellationToken = default);
}

public readonly record struct DecodedImage(int Width, int Height, ReadOnlyMemory<byte> RgbaPixels);

public interface IWebSceneCanvasDisplayListRenderer
{
    void Replay(ReadOnlySpan<double> values, IReadOnlyList<string> strings, WebSceneSize surfaceSize);
}

public interface IWebSceneSvgSceneRenderer
{
    void Render(SvgScene scene, WebSceneSize surfaceSize);
}

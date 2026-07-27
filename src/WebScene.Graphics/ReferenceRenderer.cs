using System.Globalization;
using System.Diagnostics.CodeAnalysis;
using WebScene.Core;

namespace WebScene.Graphics;

/// <summary>
/// Deterministic RGBA surface used by semantic and selected pixel fixtures.
/// It is intentionally not a production software renderer.
/// </summary>
public sealed class ReferencePixelSurface
{
    private readonly byte[] _pixels;

    public ReferencePixelSurface(int width, int height)
    {
        if (width < 0 || height < 0)
        {
            throw new ArgumentOutOfRangeException(width < 0 ? nameof(width) : nameof(height));
        }
        Width = width;
        Height = height;
        _pixels = new byte[checked(width * height * 4)];
    }

    public int Width { get; }

    public int Height { get; }

    public ReadOnlyMemory<byte> RgbaPixels => _pixels;

    public WebSceneColor GetPixel(int x, int y)
    {
        var offset = GetOffset(x, y);
        return new WebSceneColor(_pixels[offset + 3], _pixels[offset], _pixels[offset + 1], _pixels[offset + 2]);
    }

    public void Clear(WebSceneRect rectangle)
    {
        var bounds = PixelBounds(rectangle);
        for (var y = bounds.Top; y < bounds.Bottom; y++)
        {
            Array.Clear(_pixels, (y * Width + bounds.Left) * 4, (bounds.Right - bounds.Left) * 4);
        }
    }

    public void Fill(WebSceneRect rectangle, WebSceneColor color, double opacity = 1)
    {
        var bounds = PixelBounds(rectangle);
        var alpha = (byte)Math.Round(color.A * Math.Clamp(opacity, 0, 1));
        for (var y = bounds.Top; y < bounds.Bottom; y++)
        {
            for (var x = bounds.Left; x < bounds.Right; x++)
            {
                BlendPixel(GetOffset(x, y), color.R, color.G, color.B, alpha);
            }
        }
    }

    public void FillCircle(WebSceneRect bounds, WebSceneColor color, double opacity = 1)
    {
        var pixels = PixelBounds(bounds);
        var radiusX = bounds.Width / 2;
        var radiusY = bounds.Height / 2;
        if (radiusX <= 0 || radiusY <= 0) return;
        var centerX = bounds.X + radiusX;
        var centerY = bounds.Y + radiusY;
        var alpha = (byte)Math.Round(color.A * Math.Clamp(opacity, 0, 1));
        for (var y = pixels.Top; y < pixels.Bottom; y++)
        {
            for (var x = pixels.Left; x < pixels.Right; x++)
            {
                var normalizedX = (x + 0.5 - centerX) / radiusX;
                var normalizedY = (y + 0.5 - centerY) / radiusY;
                if (normalizedX * normalizedX + normalizedY * normalizedY <= 1)
                {
                    BlendPixel(GetOffset(x, y), color.R, color.G, color.B, alpha);
                }
            }
        }
    }

    private void BlendPixel(int offset, byte red, byte green, byte blue, byte alpha)
    {
        if (alpha == byte.MaxValue)
        {
            _pixels[offset] = red;
            _pixels[offset + 1] = green;
            _pixels[offset + 2] = blue;
            _pixels[offset + 3] = alpha;
            return;
        }
        if (alpha == 0) return;

        var destinationAlpha = _pixels[offset + 3] / 255d;
        var sourceAlpha = alpha / 255d;
        var outputAlpha = sourceAlpha + destinationAlpha * (1 - sourceAlpha);
        _pixels[offset] = BlendChannel(red, _pixels[offset], sourceAlpha, destinationAlpha, outputAlpha);
        _pixels[offset + 1] = BlendChannel(green, _pixels[offset + 1], sourceAlpha, destinationAlpha, outputAlpha);
        _pixels[offset + 2] = BlendChannel(blue, _pixels[offset + 2], sourceAlpha, destinationAlpha, outputAlpha);
        _pixels[offset + 3] = (byte)Math.Round(outputAlpha * 255);
    }

    private static byte BlendChannel(
        byte source,
        byte destination,
        double sourceAlpha,
        double destinationAlpha,
        double outputAlpha)
        => outputAlpha <= 0
            ? (byte)0
            : (byte)Math.Round(
                (source * sourceAlpha + destination * destinationAlpha * (1 - sourceAlpha)) / outputAlpha);

    private int GetOffset(int x, int y)
    {
        if ((uint)x >= (uint)Width || (uint)y >= (uint)Height)
        {
            throw new ArgumentOutOfRangeException((uint)x >= (uint)Width ? nameof(x) : nameof(y));
        }
        return (y * Width + x) * 4;
    }

    private PixelRectangle PixelBounds(WebSceneRect rectangle)
        => new(
            Math.Clamp((int)Math.Floor(rectangle.Left), 0, Width),
            Math.Clamp((int)Math.Floor(rectangle.Top), 0, Height),
            Math.Clamp((int)Math.Ceiling(rectangle.Right), 0, Width),
            Math.Clamp((int)Math.Ceiling(rectangle.Bottom), 0, Height));

    private readonly record struct PixelRectangle(int Left, int Top, int Right, int Bottom);
}

public sealed class CanvasReferenceRenderer : IWebSceneCanvasDisplayListRenderer
{
    private readonly Stack<State> _states = new();
    private ReferencePixelSurface? _surface;
    private State _state = State.Default;

    public ReferencePixelSurface Surface
        => _surface ?? throw new InvalidOperationException("No display list has been replayed.");

    public void Replay(ReadOnlySpan<double> values, IReadOnlyList<string> strings, WebSceneSize surfaceSize)
    {
        var width = CheckedDimension(surfaceSize.Width, nameof(surfaceSize.Width));
        var height = CheckedDimension(surfaceSize.Height, nameof(surfaceSize.Height));
        _surface = new ReferencePixelSurface(width, height);
        _states.Clear();
        _state = State.Default;
        var target = new ReferenceReplayTarget(this);
        CanvasPacketReplay.Replay(ref target, values, strings);
    }

    private static int CheckedDimension(double value, string name)
    {
        if (!double.IsFinite(value) || value < 0 || value > int.MaxValue || value != Math.Truncate(value))
        {
            throw new ArgumentOutOfRangeException(name);
        }
        return (int)value;
    }

    private static WebSceneColor ParseHexColor(string value)
    {
        var text = value.AsSpan().Trim();
        if (text.Length is not (7 or 9) || text[0] != '#')
        {
            throw new NotSupportedException($"Reference fixtures currently require #RRGGBB or #RRGGBBAA colors, not '{value}'.");
        }
        if (!byte.TryParse(text.Slice(1, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var red)
            || !byte.TryParse(text.Slice(3, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var green)
            || !byte.TryParse(text.Slice(5, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var blue)
            || (text.Length == 9
                && !byte.TryParse(text.Slice(7, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out _)))
        {
            throw new FormatException($"Invalid fixture color '{value}'.");
        }
        var alpha = text.Length == 9
            ? byte.Parse(text.Slice(7, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture)
            : byte.MaxValue;
        return new WebSceneColor(alpha, red, green, blue);
    }

    private readonly record struct State(WebSceneColor Fill, double GlobalAlpha)
    {
        public static State Default { get; } = new(WebSceneColor.FromRgb(0, 0, 0), 1);
    }

    [ExcludeFromCodeCoverage(Justification = "Unsupported-operation forwarding is fixture adapter boilerplate.")]
    private sealed class ReferenceReplayTarget(CanvasReferenceRenderer owner) : ICanvasPacketReplayTarget
    {
        public void Save() => owner._states.Push(owner._state);
        public void Restore()
        {
            if (owner._states.Count > 0) owner._state = owner._states.Pop();
        }
        public void FillRect(double x, double y, double width, double height)
            => owner.Surface.Fill(new WebSceneRect(x, y, width, height), owner._state.Fill, owner._state.GlobalAlpha);
        public void ClearRect(double x, double y, double width, double height)
            => owner.Surface.Clear(new WebSceneRect(x, y, width, height));
        public void SetFillStyle(string value)
            => owner._state = owner._state with { Fill = ParseHexColor(value) };
        public void SetGlobalAlpha(double value)
            => owner._state = owner._state with { GlobalAlpha = Math.Clamp(value, 0, 1) };

        public void ResetTransform() => Unsupported(CanvasCommandOpcode.ResetTransform);
        public void SetTransform(double a, double b, double c, double d, double e, double f) => Unsupported(CanvasCommandOpcode.SetTransform);
        public void Transform(double a, double b, double c, double d, double e, double f) => Unsupported(CanvasCommandOpcode.Transform);
        public void Translate(double x, double y) => Unsupported(CanvasCommandOpcode.Translate);
        public void Scale(double x, double y) => Unsupported(CanvasCommandOpcode.Scale);
        public void Rotate(double angle) => Unsupported(CanvasCommandOpcode.Rotate);
        public void BeginPath() => Unsupported(CanvasCommandOpcode.BeginPath);
        public void ClosePath() => Unsupported(CanvasCommandOpcode.ClosePath);
        public void MoveTo(double x, double y) => Unsupported(CanvasCommandOpcode.MoveTo);
        public void LineTo(double x, double y) => Unsupported(CanvasCommandOpcode.LineTo);
        public void BezierCurveTo(double cp1X, double cp1Y, double cp2X, double cp2Y, double x, double y) => Unsupported(CanvasCommandOpcode.BezierCurveTo);
        public void QuadraticCurveTo(double cpX, double cpY, double x, double y) => Unsupported(CanvasCommandOpcode.QuadraticCurveTo);
        public void Arc(double x, double y, double radius, double startAngle, double endAngle, bool counterClockwise) => Unsupported(CanvasCommandOpcode.Arc);
        public void ArcTo(double x1, double y1, double x2, double y2, double radius) => Unsupported(CanvasCommandOpcode.ArcTo);
        public void Rect(double x, double y, double width, double height) => Unsupported(CanvasCommandOpcode.Rect);
        public void Clip() => Unsupported(CanvasCommandOpcode.Clip);
        public void SetLineDash(ReadOnlySpan<double> segments) => Unsupported(CanvasCommandOpcode.SetLineDash);
        public void Stroke() => Unsupported(CanvasCommandOpcode.Stroke);
        public void Fill() => Unsupported(CanvasCommandOpcode.Fill);
        public void StrokeRect(double x, double y, double width, double height) => Unsupported(CanvasCommandOpcode.StrokeRect);
        public void FillText(string text, double x, double y) => Unsupported(CanvasCommandOpcode.FillText);
        public void StrokeText(string text, double x, double y) => Unsupported(CanvasCommandOpcode.StrokeText);
        public void SetStrokeStyle(string value) => Unsupported(CanvasCommandOpcode.StrokeStyle);
        public void SetLineWidth(double value) => Unsupported(CanvasCommandOpcode.LineWidth);
        public void SetLineCap(string value) => Unsupported(CanvasCommandOpcode.LineCap);
        public void SetLineJoin(string value) => Unsupported(CanvasCommandOpcode.LineJoin);
        public void SetMiterLimit(double value) => Unsupported(CanvasCommandOpcode.MiterLimit);
        public void SetLineDashOffset(double value) => Unsupported(CanvasCommandOpcode.LineDashOffset);
        public void SetFont(string value) => Unsupported(CanvasCommandOpcode.Font);
        public void SetTextAlign(string value) => Unsupported(CanvasCommandOpcode.TextAlign);
        public void SetTextBaseline(string value) => Unsupported(CanvasCommandOpcode.TextBaseline);
        public void SetImageSmoothingEnabled(bool value) => Unsupported(CanvasCommandOpcode.ImageSmoothingEnabled);
        public void SetImageSmoothingQuality(string value) => Unsupported(CanvasCommandOpcode.ImageSmoothingQuality);
        public void SetGlobalCompositeOperation(string value) => Unsupported(CanvasCommandOpcode.GlobalCompositeOperation);
        public void SetShadowColor(string value) => Unsupported(CanvasCommandOpcode.ShadowColor);
        public void SetShadowBlur(double value) => Unsupported(CanvasCommandOpcode.ShadowBlur);
        public void SetShadowOffsetX(double value) => Unsupported(CanvasCommandOpcode.ShadowOffsetX);
        public void SetShadowOffsetY(double value) => Unsupported(CanvasCommandOpcode.ShadowOffsetY);

        private static void Unsupported(CanvasCommandOpcode opcode)
            => throw new NotSupportedException(
                $"The deterministic reference fixture renderer does not implement {(int)opcode} ({opcode}).");
    }
}

public sealed class SvgReferenceRenderer : IWebSceneSvgSceneRenderer
{
    private ReferencePixelSurface? _surface;

    public ReferencePixelSurface Surface
        => _surface ?? throw new InvalidOperationException("No SVG scene has been rendered.");

    public void Render(SvgScene scene, WebSceneSize surfaceSize)
    {
        ArgumentNullException.ThrowIfNull(scene);
        _surface = new ReferencePixelSurface(
            checked((int)Math.Max(0, surfaceSize.Width)),
            checked((int)Math.Max(0, surfaceSize.Height)));
        RenderNode(scene.Root);
    }

    private void RenderNode(SvgSceneNode node)
    {
        if (node.Fill is { } fill)
        {
            if (node.Kind == SvgSceneNodeKind.Circle)
            {
                Surface.FillCircle(node.Bounds, fill.Color, fill.Opacity);
            }
            else if (node.Kind == SvgSceneNodeKind.Rectangle)
            {
                Surface.Fill(node.Bounds, fill.Color, fill.Opacity);
            }
        }
        foreach (var child in node.Children)
        {
            RenderNode(child);
        }
    }
}

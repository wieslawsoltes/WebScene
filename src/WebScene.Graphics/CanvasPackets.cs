using System.IO;

namespace WebScene.Graphics;

public enum CanvasCommandOpcode
{
    Save = 1,
    Restore = 2,
    ResetTransform = 3,
    SetTransform = 4,
    Transform = 5,
    Translate = 6,
    Scale = 7,
    Rotate = 8,
    BeginPath = 9,
    ClosePath = 10,
    MoveTo = 11,
    LineTo = 12,
    BezierCurveTo = 13,
    QuadraticCurveTo = 14,
    Arc = 15,
    ArcTo = 16,
    Rect = 17,
    Clip = 18,
    SetLineDash = 19,
    Stroke = 20,
    Fill = 21,
    FillRect = 22,
    StrokeRect = 23,
    ClearRect = 24,
    FillText = 25,
    StrokeText = 26,
    FillStyle = 40,
    StrokeStyle = 41,
    LineWidth = 42,
    LineCap = 43,
    LineJoin = 44,
    MiterLimit = 45,
    GlobalAlpha = 46,
    LineDashOffset = 47,
    Font = 48,
    TextAlign = 49,
    TextBaseline = 50,
    ImageSmoothingEnabled = 51,
    ImageSmoothingQuality = 52,
    GlobalCompositeOperation = 53,
    ShadowColor = 54,
    ShadowBlur = 55,
    ShadowOffsetX = 56,
    ShadowOffsetY = 57
}

/// <summary>
/// Allocation-free reader for the versioned numeric Canvas packet format.
/// Validation is centralized here so engine and backend adapters cannot diverge.
/// </summary>
public ref struct CanvasPacketReader
{
    private readonly ReadOnlySpan<double> _values;
    private readonly IReadOnlyList<string> _strings;
    private int _index;

    public CanvasPacketReader(ReadOnlySpan<double> values, IReadOnlyList<string> strings)
    {
        _values = values;
        _strings = strings ?? throw new ArgumentNullException(nameof(strings));
        _index = 0;
        CurrentOpcode = default;
        CurrentArguments = default;
    }

    public CanvasCommandOpcode CurrentOpcode { get; private set; }

    public ReadOnlySpan<double> CurrentArguments { get; private set; }

    public bool MoveNext()
    {
        if (_index == _values.Length)
        {
            CurrentArguments = default;
            return false;
        }

        if (_values.Length - _index < 2)
        {
            throw new InvalidDataException(
                $"Canvas2D batch ended with an incomplete command header at value {_index} of {_values.Length}.");
        }

        var rawOpcode = _values[_index++];
        var rawArgumentCount = _values[_index++];
        if (!IsNonNegativeInteger(rawOpcode))
        {
            throw new InvalidDataException($"Canvas2D batch contains invalid opcode {rawOpcode}.");
        }
        if (!IsNonNegativeInteger(rawArgumentCount))
        {
            throw new InvalidDataException(
                $"Canvas2D batch opcode {(int)rawOpcode} contains invalid argument count {rawArgumentCount}.");
        }

        var opcode = (int)rawOpcode;
        var argumentCount = (int)rawArgumentCount;
        if (argumentCount > _values.Length - _index)
        {
            throw new InvalidDataException(
                $"Canvas2D batch opcode {opcode} requests {argumentCount} arguments but only {_values.Length - _index} values remain.");
        }

        CurrentOpcode = (CanvasCommandOpcode)opcode;
        CurrentArguments = _values.Slice(_index, argumentCount);
        _index += argumentCount;
        ValidateArgumentCount(CurrentOpcode, CurrentArguments);
        return true;
    }

    public string ReadString(double index)
    {
        if (!IsNonNegativeInteger(index) || index >= _strings.Count)
        {
            throw new InvalidDataException(
                $"Canvas2D batch string index {index} is outside the packet string table containing {_strings.Count} entries.");
        }

        return _strings[(int)index];
    }

    private static bool IsNonNegativeInteger(double value)
        => double.IsFinite(value) && value >= 0 && value <= int.MaxValue && value == Math.Truncate(value);

    private static void ValidateArgumentCount(CanvasCommandOpcode opcode, ReadOnlySpan<double> arguments)
    {
        var expected = opcode switch
        {
            CanvasCommandOpcode.Save or CanvasCommandOpcode.Restore or CanvasCommandOpcode.ResetTransform
                or CanvasCommandOpcode.BeginPath or CanvasCommandOpcode.ClosePath or CanvasCommandOpcode.Clip
                or CanvasCommandOpcode.Stroke or CanvasCommandOpcode.Fill => 0,
            CanvasCommandOpcode.Rotate or CanvasCommandOpcode.FillStyle or CanvasCommandOpcode.StrokeStyle
                or CanvasCommandOpcode.LineWidth or CanvasCommandOpcode.LineCap or CanvasCommandOpcode.LineJoin
                or CanvasCommandOpcode.MiterLimit or CanvasCommandOpcode.GlobalAlpha or CanvasCommandOpcode.LineDashOffset
                or CanvasCommandOpcode.Font or CanvasCommandOpcode.TextAlign or CanvasCommandOpcode.TextBaseline
                or CanvasCommandOpcode.ImageSmoothingEnabled or CanvasCommandOpcode.ImageSmoothingQuality
                or CanvasCommandOpcode.GlobalCompositeOperation or CanvasCommandOpcode.ShadowColor
                or CanvasCommandOpcode.ShadowBlur or CanvasCommandOpcode.ShadowOffsetX or CanvasCommandOpcode.ShadowOffsetY => 1,
            CanvasCommandOpcode.Translate or CanvasCommandOpcode.Scale or CanvasCommandOpcode.MoveTo
                or CanvasCommandOpcode.LineTo => 2,
            CanvasCommandOpcode.FillText or CanvasCommandOpcode.StrokeText => 3,
            CanvasCommandOpcode.QuadraticCurveTo or CanvasCommandOpcode.Rect or CanvasCommandOpcode.FillRect
                or CanvasCommandOpcode.StrokeRect or CanvasCommandOpcode.ClearRect => 4,
            CanvasCommandOpcode.ArcTo => 5,
            CanvasCommandOpcode.SetTransform or CanvasCommandOpcode.Transform or CanvasCommandOpcode.BezierCurveTo
                or CanvasCommandOpcode.Arc => 6,
            CanvasCommandOpcode.SetLineDash => -1,
            _ => throw new InvalidDataException($"Canvas2D batch contains unknown opcode {(int)opcode}.")
        };

        if (expected >= 0 && arguments.Length != expected)
        {
            throw new InvalidDataException(
                $"Canvas2D batch opcode {(int)opcode} expects {expected} arguments but received {arguments.Length}.");
        }

        if (opcode == CanvasCommandOpcode.SetLineDash
            && (arguments.Length == 0 || !IsNonNegativeInteger(arguments[0]) || arguments[0] != arguments.Length - 1))
        {
            throw new InvalidDataException("Canvas2D setLineDash payload length does not match its declared element count.");
        }
    }
}

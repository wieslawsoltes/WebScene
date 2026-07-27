namespace WebScene.Graphics;

/// <summary>
/// Typed backend target for the portable Canvas packet replay pass. Packet framing,
/// validation, string-table lookup and opcode dispatch remain backend-independent.
/// </summary>
public interface ICanvasPacketReplayTarget
{
    void Save();
    void Restore();
    void ResetTransform();
    void SetTransform(double a, double b, double c, double d, double e, double f);
    void Transform(double a, double b, double c, double d, double e, double f);
    void Translate(double x, double y);
    void Scale(double x, double y);
    void Rotate(double angle);
    void BeginPath();
    void ClosePath();
    void MoveTo(double x, double y);
    void LineTo(double x, double y);
    void BezierCurveTo(double cp1X, double cp1Y, double cp2X, double cp2Y, double x, double y);
    void QuadraticCurveTo(double cpX, double cpY, double x, double y);
    void Arc(double x, double y, double radius, double startAngle, double endAngle, bool counterClockwise);
    void ArcTo(double x1, double y1, double x2, double y2, double radius);
    void Rect(double x, double y, double width, double height);
    void Clip();
    void SetLineDash(ReadOnlySpan<double> segments);
    void Stroke();
    void Fill();
    void FillRect(double x, double y, double width, double height);
    void StrokeRect(double x, double y, double width, double height);
    void ClearRect(double x, double y, double width, double height);
    void FillText(string text, double x, double y);
    void StrokeText(string text, double x, double y);
    void SetFillStyle(string value);
    void SetStrokeStyle(string value);
    void SetLineWidth(double value);
    void SetLineCap(string value);
    void SetLineJoin(string value);
    void SetMiterLimit(double value);
    void SetGlobalAlpha(double value);
    void SetLineDashOffset(double value);
    void SetFont(string value);
    void SetTextAlign(string value);
    void SetTextBaseline(string value);
    void SetImageSmoothingEnabled(bool value);
    void SetImageSmoothingQuality(string value);
    void SetGlobalCompositeOperation(string value);
    void SetShadowColor(string value);
    void SetShadowBlur(double value);
    void SetShadowOffsetX(double value);
    void SetShadowOffsetY(double value);
}

/// <summary>Portable semantic replay for a validated Canvas command packet.</summary>
public static class CanvasPacketReplay
{
    public static int Replay<TTarget>(
        ref TTarget target,
        ReadOnlySpan<double> values,
        IReadOnlyList<string> strings)
        where TTarget : ICanvasPacketReplayTarget
    {
        var operationCount = 0;
        var reader = new CanvasPacketReader(values, strings);
        while (reader.MoveNext())
        {
            ReplayOperation(ref target, reader.CurrentOpcode, reader.CurrentArguments, ref reader);
            operationCount++;
        }
        return operationCount;
    }

    private static void ReplayOperation<TTarget>(
        ref TTarget target,
        CanvasCommandOpcode opcode,
        ReadOnlySpan<double> values,
        ref CanvasPacketReader reader)
        where TTarget : ICanvasPacketReplayTarget
    {
        switch (opcode)
        {
            case CanvasCommandOpcode.Save: target.Save(); break;
            case CanvasCommandOpcode.Restore: target.Restore(); break;
            case CanvasCommandOpcode.ResetTransform: target.ResetTransform(); break;
            case CanvasCommandOpcode.SetTransform: target.SetTransform(values[0], values[1], values[2], values[3], values[4], values[5]); break;
            case CanvasCommandOpcode.Transform: target.Transform(values[0], values[1], values[2], values[3], values[4], values[5]); break;
            case CanvasCommandOpcode.Translate: target.Translate(values[0], values[1]); break;
            case CanvasCommandOpcode.Scale: target.Scale(values[0], values[1]); break;
            case CanvasCommandOpcode.Rotate: target.Rotate(values[0]); break;
            case CanvasCommandOpcode.BeginPath: target.BeginPath(); break;
            case CanvasCommandOpcode.ClosePath: target.ClosePath(); break;
            case CanvasCommandOpcode.MoveTo: target.MoveTo(values[0], values[1]); break;
            case CanvasCommandOpcode.LineTo: target.LineTo(values[0], values[1]); break;
            case CanvasCommandOpcode.BezierCurveTo: target.BezierCurveTo(values[0], values[1], values[2], values[3], values[4], values[5]); break;
            case CanvasCommandOpcode.QuadraticCurveTo: target.QuadraticCurveTo(values[0], values[1], values[2], values[3]); break;
            case CanvasCommandOpcode.Arc: target.Arc(values[0], values[1], values[2], values[3], values[4], values[5] != 0); break;
            case CanvasCommandOpcode.ArcTo: target.ArcTo(values[0], values[1], values[2], values[3], values[4]); break;
            case CanvasCommandOpcode.Rect: target.Rect(values[0], values[1], values[2], values[3]); break;
            case CanvasCommandOpcode.Clip: target.Clip(); break;
            case CanvasCommandOpcode.SetLineDash: target.SetLineDash(values.Slice(1, (int)values[0])); break;
            case CanvasCommandOpcode.Stroke: target.Stroke(); break;
            case CanvasCommandOpcode.Fill: target.Fill(); break;
            case CanvasCommandOpcode.FillRect: target.FillRect(values[0], values[1], values[2], values[3]); break;
            case CanvasCommandOpcode.StrokeRect: target.StrokeRect(values[0], values[1], values[2], values[3]); break;
            case CanvasCommandOpcode.ClearRect: target.ClearRect(values[0], values[1], values[2], values[3]); break;
            case CanvasCommandOpcode.FillText: target.FillText(reader.ReadString(values[0]), values[1], values[2]); break;
            case CanvasCommandOpcode.StrokeText: target.StrokeText(reader.ReadString(values[0]), values[1], values[2]); break;
            case CanvasCommandOpcode.FillStyle: target.SetFillStyle(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.StrokeStyle: target.SetStrokeStyle(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.LineWidth: target.SetLineWidth(values[0]); break;
            case CanvasCommandOpcode.LineCap: target.SetLineCap(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.LineJoin: target.SetLineJoin(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.MiterLimit: target.SetMiterLimit(values[0]); break;
            case CanvasCommandOpcode.GlobalAlpha: target.SetGlobalAlpha(values[0]); break;
            case CanvasCommandOpcode.LineDashOffset: target.SetLineDashOffset(values[0]); break;
            case CanvasCommandOpcode.Font: target.SetFont(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.TextAlign: target.SetTextAlign(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.TextBaseline: target.SetTextBaseline(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.ImageSmoothingEnabled: target.SetImageSmoothingEnabled(values[0] != 0); break;
            case CanvasCommandOpcode.ImageSmoothingQuality: target.SetImageSmoothingQuality(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.GlobalCompositeOperation: target.SetGlobalCompositeOperation(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.ShadowColor: target.SetShadowColor(reader.ReadString(values[0])); break;
            case CanvasCommandOpcode.ShadowBlur: target.SetShadowBlur(values[0]); break;
            case CanvasCommandOpcode.ShadowOffsetX: target.SetShadowOffsetX(values[0]); break;
            case CanvasCommandOpcode.ShadowOffsetY: target.SetShadowOffsetY(values[0]); break;
            default: throw new InvalidOperationException($"Unknown Canvas2D batch opcode {(int)opcode}.");
        }
    }
}

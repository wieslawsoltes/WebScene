using System.Runtime.CompilerServices;
using WebScene.Graphics;
using Xunit;

namespace WebScene.Graphics.Tests;

public sealed class CanvasPacketReplayTests
{
    [Fact]
    public void PortableReplayDispatchesEveryOpcodeWithTypedValues()
    {
        var values = new List<double>();
        void Add(CanvasCommandOpcode opcode, params double[] arguments)
        {
            values.Add((double)opcode);
            values.Add(arguments.Length);
            values.AddRange(arguments);
        }

        Add(CanvasCommandOpcode.Save);
        Add(CanvasCommandOpcode.Restore);
        Add(CanvasCommandOpcode.ResetTransform);
        Add(CanvasCommandOpcode.SetTransform, 1, 2, 3, 4, 5, 6);
        Add(CanvasCommandOpcode.Transform, 1, 2, 3, 4, 5, 6);
        Add(CanvasCommandOpcode.Translate, 1, 2);
        Add(CanvasCommandOpcode.Scale, 1, 2);
        Add(CanvasCommandOpcode.Rotate, 1);
        Add(CanvasCommandOpcode.BeginPath);
        Add(CanvasCommandOpcode.ClosePath);
        Add(CanvasCommandOpcode.MoveTo, 1, 2);
        Add(CanvasCommandOpcode.LineTo, 1, 2);
        Add(CanvasCommandOpcode.BezierCurveTo, 1, 2, 3, 4, 5, 6);
        Add(CanvasCommandOpcode.QuadraticCurveTo, 1, 2, 3, 4);
        Add(CanvasCommandOpcode.Arc, 1, 2, 3, 4, 5, 1);
        Add(CanvasCommandOpcode.ArcTo, 1, 2, 3, 4, 5);
        Add(CanvasCommandOpcode.Rect, 1, 2, 3, 4);
        Add(CanvasCommandOpcode.Clip);
        Add(CanvasCommandOpcode.SetLineDash, 2, 4, 8);
        Add(CanvasCommandOpcode.Stroke);
        Add(CanvasCommandOpcode.Fill);
        Add(CanvasCommandOpcode.FillRect, 1, 2, 3, 4);
        Add(CanvasCommandOpcode.StrokeRect, 1, 2, 3, 4);
        Add(CanvasCommandOpcode.ClearRect, 1, 2, 3, 4);
        Add(CanvasCommandOpcode.FillText, 0, 1, 2);
        Add(CanvasCommandOpcode.StrokeText, 1, 1, 2);
        Add(CanvasCommandOpcode.FillStyle, 2);
        Add(CanvasCommandOpcode.StrokeStyle, 3);
        Add(CanvasCommandOpcode.LineWidth, 2);
        Add(CanvasCommandOpcode.LineCap, 4);
        Add(CanvasCommandOpcode.LineJoin, 5);
        Add(CanvasCommandOpcode.MiterLimit, 4);
        Add(CanvasCommandOpcode.GlobalAlpha, .5);
        Add(CanvasCommandOpcode.LineDashOffset, 3);
        Add(CanvasCommandOpcode.Font, 6);
        Add(CanvasCommandOpcode.TextAlign, 7);
        Add(CanvasCommandOpcode.TextBaseline, 8);
        Add(CanvasCommandOpcode.ImageSmoothingEnabled, 1);
        Add(CanvasCommandOpcode.ImageSmoothingQuality, 9);
        Add(CanvasCommandOpcode.GlobalCompositeOperation, 10);
        Add(CanvasCommandOpcode.ShadowColor, 11);
        Add(CanvasCommandOpcode.ShadowBlur, 4);
        Add(CanvasCommandOpcode.ShadowOffsetX, 5);
        Add(CanvasCommandOpcode.ShadowOffsetY, 6);
        string[] strings =
        [
            "fill text", "stroke text", "#123456", "#654321", "round", "bevel",
            "12px serif", "center", "middle", "high", "multiply", "#000000"
        ];
        var target = new RecordingTarget();

        var count = CanvasPacketReplay.Replay(ref target, values.ToArray(), strings);

        Assert.Equal(Enum.GetValues<CanvasCommandOpcode>().Length, count);
        Assert.Equal(count, target.Calls.Count);
        Assert.Equal(new double[] { 4, 8 }, target.LineDash);
        Assert.True(target.CounterClockwise);
        Assert.True(target.ImageSmoothingEnabled);
        Assert.Contains("SetGlobalCompositeOperation", target.Calls);
    }

    private sealed class RecordingTarget : ICanvasPacketReplayTarget
    {
        public List<string> Calls { get; } = [];
        public double[] LineDash { get; private set; } = [];
        public bool CounterClockwise { get; private set; }
        public bool ImageSmoothingEnabled { get; private set; }

        private void Mark([CallerMemberName] string name = "") => Calls.Add(name);

        public void Save() => Mark();
        public void Restore() => Mark();
        public void ResetTransform() => Mark();
        public void SetTransform(double a, double b, double c, double d, double e, double f) => Mark();
        public void Transform(double a, double b, double c, double d, double e, double f) => Mark();
        public void Translate(double x, double y) => Mark();
        public void Scale(double x, double y) => Mark();
        public void Rotate(double angle) => Mark();
        public void BeginPath() => Mark();
        public void ClosePath() => Mark();
        public void MoveTo(double x, double y) => Mark();
        public void LineTo(double x, double y) => Mark();
        public void BezierCurveTo(double cp1X, double cp1Y, double cp2X, double cp2Y, double x, double y) => Mark();
        public void QuadraticCurveTo(double cpX, double cpY, double x, double y) => Mark();
        public void Arc(double x, double y, double radius, double startAngle, double endAngle, bool counterClockwise)
        {
            Mark();
            CounterClockwise = counterClockwise;
        }
        public void ArcTo(double x1, double y1, double x2, double y2, double radius) => Mark();
        public void Rect(double x, double y, double width, double height) => Mark();
        public void Clip() => Mark();
        public void SetLineDash(ReadOnlySpan<double> segments)
        {
            Mark();
            LineDash = segments.ToArray();
        }
        public void Stroke() => Mark();
        public void Fill() => Mark();
        public void FillRect(double x, double y, double width, double height) => Mark();
        public void StrokeRect(double x, double y, double width, double height) => Mark();
        public void ClearRect(double x, double y, double width, double height) => Mark();
        public void FillText(string text, double x, double y) => Mark();
        public void StrokeText(string text, double x, double y) => Mark();
        public void SetFillStyle(string value) => Mark();
        public void SetStrokeStyle(string value) => Mark();
        public void SetLineWidth(double value) => Mark();
        public void SetLineCap(string value) => Mark();
        public void SetLineJoin(string value) => Mark();
        public void SetMiterLimit(double value) => Mark();
        public void SetGlobalAlpha(double value) => Mark();
        public void SetLineDashOffset(double value) => Mark();
        public void SetFont(string value) => Mark();
        public void SetTextAlign(string value) => Mark();
        public void SetTextBaseline(string value) => Mark();
        public void SetImageSmoothingEnabled(bool value)
        {
            Mark();
            ImageSmoothingEnabled = value;
        }
        public void SetImageSmoothingQuality(string value) => Mark();
        public void SetGlobalCompositeOperation(string value) => Mark();
        public void SetShadowColor(string value) => Mark();
        public void SetShadowBlur(double value) => Mark();
        public void SetShadowOffsetX(double value) => Mark();
        public void SetShadowOffsetY(double value) => Mark();
    }
}

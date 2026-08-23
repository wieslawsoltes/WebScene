using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeCanvasArcTests
{
    [Fact]
    public void TradingViewCounterclockwiseFullCircleProducesSelectableHandle()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 15,
            V0 = 243.5,
            V1 = 539.5,
            V2 = 5.5,
            V3 = 0,
            V4 = Math.PI * 2,
            V5 = 1
        };
        using var path = new SKPath();

        NativeCanvasSceneRenderer.AppendArc(path, command);

        Assert.True(path.Contains(243.5f, 539.5f));
        Assert.InRange(path.Bounds.Left, 237.99f, 238.01f);
        Assert.InRange(path.Bounds.Top, 533.99f, 534.01f);
        Assert.InRange(path.Bounds.Right, 248.99f, 249.01f);
        Assert.InRange(path.Bounds.Bottom, 544.99f, 545.01f);
    }

    [Fact]
    public void FullCircleConnectsExistingSubpathToArcStart()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 15,
            V0 = 32,
            V1 = 32,
            V2 = 8,
            V3 = 0,
            V4 = Math.PI * 2,
            V5 = 0
        };
        using var path = new SKPath();
        path.MoveTo(4, 4);

        NativeCanvasSceneRenderer.AppendArc(path, command);

        using var bitmap = new SKBitmap(64, 64);
        using var canvas = new SKCanvas(bitmap);
        using var paint = new SKPaint
        {
            Color = SKColors.White,
            IsAntialias = false,
            Style = SKPaintStyle.Stroke,
            StrokeWidth = 2
        };
        canvas.Clear(SKColors.Transparent);
        canvas.DrawPath(path, paint);

        Assert.NotEqual(0, bitmap.GetPixel(22, 18).Alpha);
    }

    [Fact]
    public void ZeroRadiusConnectsExistingSubpathToCenter()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 15,
            V0 = 32,
            V1 = 32,
            V2 = 0,
            V3 = 0,
            V4 = Math.PI
        };
        using var path = new SKPath();
        path.MoveTo(4, 4);

        NativeCanvasSceneRenderer.AppendArc(path, command);

        Assert.InRange(path.Bounds.Right, 31.99f, 32.01f);
        Assert.InRange(path.Bounds.Bottom, 31.99f, 32.01f);
    }

    [Fact]
    public void ZeroSweepConnectsExistingSubpathToArcStart()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 15,
            V0 = 32,
            V1 = 32,
            V2 = 8,
            V3 = 0,
            V4 = 0
        };
        using var path = new SKPath();
        path.MoveTo(4, 4);

        NativeCanvasSceneRenderer.AppendArc(path, command);

        Assert.InRange(path.Bounds.Right, 39.99f, 40.01f);
        Assert.InRange(path.Bounds.Bottom, 31.99f, 32.01f);
    }
}

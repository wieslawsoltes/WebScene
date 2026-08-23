using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeCanvasEllipseTests
{
    [Fact]
    public void FullEllipseProducesClosedAreaAtAuthoredCenter()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 30,
            V0 = 50,
            V1 = 40,
            V2 = 20,
            V3 = 10,
            V4 = 0,
            V5 = 0,
            V6 = Math.PI * 2,
            V7 = 0
        };
        using var path = new SKPath();

        NativeCanvasSceneRenderer.AppendEllipse(path, command);

        Assert.True(path.Contains(50, 40));
        Assert.InRange(path.Bounds.Left, 29.99f, 30.01f);
        Assert.InRange(path.Bounds.Top, 29.99f, 30.01f);
        Assert.InRange(path.Bounds.Right, 69.99f, 70.01f);
        Assert.InRange(path.Bounds.Bottom, 49.99f, 50.01f);
    }

    [Fact]
    public void RotatedEllipseRetainsBothRadiiAndConnectsToExistingSubpath()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 30,
            V0 = 50,
            V1 = 40,
            V2 = 20,
            V3 = 10,
            V4 = Math.PI / 2,
            V5 = 0,
            V6 = Math.PI * 2,
            V7 = 1
        };
        using var path = new SKPath();
        path.MoveTo(0, 0);

        NativeCanvasSceneRenderer.AppendEllipse(path, command);

        Assert.True(path.Contains(50, 40));
        Assert.InRange(path.Bounds.Left, -0.01f, 0.01f);
        Assert.InRange(path.Bounds.Top, -0.01f, 0.01f);
        Assert.InRange(path.Bounds.Right, 60 - 0.01f, 60 + 0.01f);
        Assert.InRange(path.Bounds.Bottom, 60 - 0.01f, 60 + 0.01f);
    }

    [Fact]
    public void ZeroHorizontalRadiusRetainsDegenerateVerticalPath()
    {
        var command = new NativeCanvasCommand
        {
            Kind = 30,
            V0 = 50,
            V1 = 40,
            V2 = 0,
            V3 = 10,
            V4 = 0,
            V5 = 0,
            V6 = Math.PI * 2,
            V7 = 0
        };
        using var path = new SKPath();

        NativeCanvasSceneRenderer.AppendEllipse(path, command);

        Assert.True(path.PointCount > 0);
        Assert.InRange(path.Bounds.Left, 49.99f, 50.01f);
        Assert.InRange(path.Bounds.Top, 29.99f, 30.01f);
        Assert.InRange(path.Bounds.Bottom, 49.99f, 50.01f);
    }
}

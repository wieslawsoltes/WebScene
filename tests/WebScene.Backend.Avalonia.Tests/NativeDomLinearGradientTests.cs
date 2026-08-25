using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeDomLinearGradientTests
{
    [Fact]
    public void DiagonalHardStopPreservesBothColorsAtTheSharedPosition()
    {
        var command = new SceneCommand
        {
            X = 10,
            Y = 20,
            Width = 40,
            Height = 20
        };

        var parsed = NativeCanvasSceneRenderer.TryParseDomLinearGradient(
            "linear-gradient(135deg, rgb(44, 170, 160) 50%, rgb(244, 75, 79) 50%)",
            command,
            out var gradient);

        Assert.True(parsed);
        Assert.Equal(new float[] { 0.5f, 0.5f }, gradient.Positions);
        Assert.Equal(new SKColor(44, 170, 160), gradient.Colors[0]);
        Assert.Equal(new SKColor(244, 75, 79), gradient.Colors[1]);
        Assert.True(gradient.Start.X < gradient.End.X);
        Assert.True(gradient.Start.Y < gradient.End.Y);
    }

    [Fact]
    public void MissingStopPositionsAreDistributedLikeCssGradients()
    {
        var command = new SceneCommand { Width = 100, Height = 20 };

        var parsed = NativeCanvasSceneRenderer.TryParseDomLinearGradient(
            "linear-gradient(to right, red, green, blue)",
            command,
            out var gradient);

        Assert.True(parsed);
        Assert.Equal(new float[] { 0f, 0.5f, 1f }, gradient.Positions);
        Assert.True(gradient.Start.X < gradient.End.X);
        Assert.Equal(gradient.Start.Y, gradient.End.Y);
    }

    [Fact]
    public void TradingViewBackgroundShorthandUsesTheCssDefaultTopToBottomDirection()
    {
        var command = new SceneCommand
        {
            X = 10,
            Y = 20,
            Width = 40,
            Height = 24
        };

        var parsed = NativeCanvasSceneRenderer.TryParseDomLinearGradient(
            "linear-gradient(rgb(44, 170, 160), rgb(244, 75, 79))",
            command,
            out var gradient);

        Assert.True(parsed);
        Assert.Equal(new SKPoint(30, 20), gradient.Start);
        Assert.Equal(new SKPoint(30, 44), gradient.End);
        Assert.Equal(new SKColor(44, 170, 160), gradient.Colors[0]);
        Assert.Equal(new SKColor(244, 75, 79), gradient.Colors[1]);
        Assert.Equal(new float[] { 0f, 1f }, gradient.Positions);
    }

    [Fact]
    public void TradingViewOpacityRampPreservesTransparentBlackAndHorizontalDirection()
    {
        var command = new SceneCommand
        {
            X = 10,
            Y = 20,
            Width = 100,
            Height = 12
        };

        var parsed = NativeCanvasSceneRenderer.TryParseDomLinearGradient(
            "linear-gradient(90deg, transparent, #000000)",
            command,
            out var gradient);

        Assert.True(parsed);
        Assert.Equal(10, gradient.Start.X, 3);
        Assert.Equal(110, gradient.End.X, 3);
        Assert.Equal(26, gradient.Start.Y, 3);
        Assert.Equal(26, gradient.End.Y, 3);
        Assert.Equal(new SKColor(0, 0, 0, 0), gradient.Colors[0]);
        Assert.Equal(new SKColor(0, 0, 0), gradient.Colors[1]);
        Assert.Equal(new float[] { 0f, 1f }, gradient.Positions);
    }
}

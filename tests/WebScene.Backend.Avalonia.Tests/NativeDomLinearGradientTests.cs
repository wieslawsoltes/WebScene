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

    [Fact]
    public void LayeredOpacityRampKeepsCheckerboardVisibleUntilOpaqueEndpoint()
    {
        using var bitmap = new SKBitmap(64, 16, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        var command = new SceneCommand { Width = 64, Height = 16 };
        const string resource = "webscene-bg-v2\t"
            + "linear-gradient(to right, rgba(0, 0, 255, 0), rgb(0, 80, 255)), "
            + "linear-gradient(to bottom, transparent 50%, rgba(255, 255, 255, 0.45) 50%), "
            + "linear-gradient(to right, rgb(18, 18, 18) 50%, rgb(112, 112, 112) 50%)"
            + "\trepeat\t0% 0%\tauto, 8px 8px, 8px 8px";

        NativeCanvasSceneRenderer.DrawDomBackgroundForTest(canvas, resource, command);
        canvas.Flush();

        var transparentLight = bitmap.GetPixel(1, 1);
        var transparentDark = bitmap.GetPixel(1, 6);
        var intermediateLight = bitmap.GetPixel(32, 1);
        var intermediateDark = bitmap.GetPixel(32, 6);
        var opaqueLight = bitmap.GetPixel(62, 1);
        var opaqueDark = bitmap.GetPixel(62, 6);
        Assert.NotEqual(transparentLight, transparentDark);
        Assert.NotEqual(intermediateLight, intermediateDark);
        Assert.True(intermediateLight.Blue > transparentLight.Blue);
        Assert.True(intermediateDark.Blue > transparentDark.Blue);
        Assert.InRange(Math.Abs(opaqueLight.Red - opaqueDark.Red), 0, 3);
        Assert.InRange(Math.Abs(opaqueLight.Green - opaqueDark.Green), 0, 3);
        Assert.InRange(Math.Abs(opaqueLight.Blue - opaqueDark.Blue), 0, 3);
        Assert.True(opaqueLight.Blue > 230);
    }

    [Fact]
    public void DiagonalEqualPositionHardStopPaintsBothHalvesOfSplitSwatch()
    {
        using var bitmap = new SKBitmap(40, 24, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        var command = new SceneCommand { Width = 40, Height = 24 };

        NativeCanvasSceneRenderer.DrawDomBackgroundForTest(
            canvas,
            "linear-gradient(135deg, rgb(44, 170, 160) 50%, rgb(244, 75, 79) 50%)",
            command);
        canvas.Flush();

        var firstHalf = bitmap.GetPixel(4, 4);
        var secondHalf = bitmap.GetPixel(35, 19);
        Assert.Contains(firstHalf,
            new[] { new SKColor(44, 170, 160), new SKColor(244, 75, 79) });
        Assert.Contains(secondHalf,
            new[] { new SKColor(44, 170, 160), new SKColor(244, 75, 79) });
        Assert.NotEqual(firstHalf, secondHalf);
    }
}

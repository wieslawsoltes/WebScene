using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeEllipticalCornerRadiusTests
{
    [Fact]
    public void PresenterPairsCompanionVerticalRadiiWithExistingRoundedCommand()
    {
        SceneCommand[] commands =
        [
            Command(kind: 32, nodeId: 7, topLeft: 10, topRight: 15, bottomRight: 20, bottomLeft: 5),
            Command(kind: 7, nodeId: 7, topLeft: 20, topRight: 30, bottomRight: 40, bottomLeft: 10)
        ];

        var radii = NativeCanvasSceneRenderer.ResolveDomCornerRadii(commands, 1);

        Assert.Equal(new SKPoint(20, 10), radii.TopLeft);
        Assert.Equal(new SKPoint(30, 15), radii.TopRight);
        Assert.Equal(new SKPoint(40, 20), radii.BottomRight);
        Assert.Equal(new SKPoint(10, 5), radii.BottomLeft);
    }

    [Fact]
    public void ScalarCommandKeepsCircularRadiiWithoutCompanionMetadata()
    {
        SceneCommand[] commands = [Command(kind: 7, nodeId: 8, 12, 12, 12, 12)];

        var radii = NativeCanvasSceneRenderer.ResolveDomCornerRadii(commands, 0);

        Assert.Equal(new SKPoint(12, 12), radii.TopLeft);
        Assert.Equal(new SKPoint(12, 12), radii.TopRight);
        Assert.Equal(new SKPoint(12, 12), radii.BottomRight);
        Assert.Equal(new SKPoint(12, 12), radii.BottomLeft);
    }

    [Fact]
    public void PresenterIgnoresCompanionMetadataForAnotherCommand()
    {
        SceneCommand[] commands =
        [
            Command(kind: 32, nodeId: 7, topLeft: 10, topRight: 15, bottomRight: 20, bottomLeft: 5),
            Command(kind: 7, nodeId: 8, topLeft: 12, topRight: 12, bottomRight: 12, bottomLeft: 12)
        ];

        var radii = NativeCanvasSceneRenderer.ResolveDomCornerRadii(commands, 1);

        Assert.Equal(new SKPoint(12, 12), radii.TopLeft);
        Assert.Equal(new SKPoint(12, 12), radii.TopRight);
        Assert.Equal(new SKPoint(12, 12), radii.BottomRight);
        Assert.Equal(new SKPoint(12, 12), radii.BottomLeft);
    }

    private static SceneCommand Command(
        uint kind,
        uint nodeId,
        float topLeft,
        float topRight,
        float bottomRight,
        float bottomLeft)
        => new()
        {
            Kind = kind,
            NodeId = nodeId,
            X = 10,
            Y = 20,
            Width = 100,
            Height = 80,
            RadiusTopLeft = topLeft,
            RadiusTopRight = topRight,
            RadiusBottomRight = bottomRight,
            RadiusBottomLeft = bottomLeft
        };
}

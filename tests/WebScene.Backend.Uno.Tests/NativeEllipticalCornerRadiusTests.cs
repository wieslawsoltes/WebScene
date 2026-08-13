using SkiaSharp;
using WebScene.Backends.Uno.Native;
using Xunit;

namespace WebScene.Backend.Uno.Tests;

public sealed class NativeEllipticalCornerRadiusTests
{
    [Fact]
    public void UnoPresenterConsumesSharedEllipticalRadiusMetadata()
    {
        SceneCommand[] commands =
        [
            new()
            {
                Kind = 32,
                NodeId = 11,
                X = 10,
                Y = 20,
                Width = 100,
                Height = 80,
                RadiusTopLeft = 10,
                RadiusTopRight = 15,
                RadiusBottomRight = 20,
                RadiusBottomLeft = 5
            },
            new()
            {
                Kind = 7,
                NodeId = 11,
                X = 10,
                Y = 20,
                Width = 100,
                Height = 80,
                RadiusTopLeft = 20,
                RadiusTopRight = 30,
                RadiusBottomRight = 40,
                RadiusBottomLeft = 10
            }
        ];

        var radii = NativeCanvasSceneRenderer.ResolveDomCornerRadii(commands, 1);

        Assert.Equal(new SKPoint(20, 10), radii.TopLeft);
        Assert.Equal(new SKPoint(30, 15), radii.TopRight);
        Assert.Equal(new SKPoint(40, 20), radii.BottomRight);
        Assert.Equal(new SKPoint(10, 5), radii.BottomLeft);
    }
}

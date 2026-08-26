using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeCanvasPathReplayTests
{
    [Fact]
    public void PathGeometryRetainsTransformActiveDuringConstruction()
    {
        var commands = new[]
        {
            new NativeCanvasCommand
            {
                Kind = 4,
                V0 = 2,
                V3 = 1
            },
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand
            {
                Kind = 15,
                V0 = 20,
                V1 = 20,
                V2 = 10,
                V3 = 0,
                V4 = Math.PI * 2
            },
            new NativeCanvasCommand { Kind = 3 },
            new NativeCanvasCommand { Kind = 20 }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(60, 20).Alpha);
        Assert.Equal(0, bitmap.GetPixel(30, 20).Alpha);
    }

    [Fact]
    public void ExistingPathIsRebasedWhenCurrentTransformChanges()
    {
        var commands = new[]
        {
            new NativeCanvasCommand
            {
                Kind = 4,
                V0 = 1,
                V3 = 1,
                V4 = 10
            },
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand
            {
                Kind = 15,
                V0 = 20,
                V1 = 20,
                V2 = 10,
                V3 = 0,
                V4 = Math.PI * 2
            },
            new NativeCanvasCommand
            {
                Kind = 4,
                V0 = 2,
                V3 = 1
            },
            new NativeCanvasCommand { Kind = 20 }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(40, 20).Alpha);
        Assert.Equal(0, bitmap.GetPixel(50, 20).Alpha);
    }

    [Fact]
    public void RestoreDoesNotMovePathConstructedUnderSavedTransform()
    {
        var commands = new[]
        {
            new NativeCanvasCommand { Kind = 1 },
            new NativeCanvasCommand { Kind = 7, V0 = 2, V1 = 1 },
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand
            {
                Kind = 15,
                V0 = 20,
                V1 = 20,
                V2 = 10,
                V3 = 0,
                V4 = Math.PI * 2
            },
            new NativeCanvasCommand { Kind = 2 },
            new NativeCanvasCommand { Kind = 20 }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(60, 20).Alpha);
        Assert.Equal(0, bitmap.GetPixel(30, 20).Alpha);
    }

    [Fact]
    public void PathCanCombineSegmentsAuthoredUnderDifferentTransforms()
    {
        var commands = new[]
        {
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand { Kind = 11, V0 = 4, V1 = 4 },
            new NativeCanvasCommand { Kind = 7, V0 = 2, V1 = 2 },
            new NativeCanvasCommand { Kind = 12, V0 = 20, V1 = 20 },
            new NativeCanvasCommand { Kind = 3 },
            new NativeCanvasCommand { Kind = 42, V0 = 2 },
            new NativeCanvasCommand { Kind = 20 }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(39, 39).Alpha);
    }

    [Fact]
    public void FillRectNormalizesNegativeDimensions()
    {
        var commands = new[]
        {
            new NativeCanvasCommand
            {
                Kind = 22,
                V0 = 30,
                V1 = 30,
                V2 = -20,
                V3 = -20
            }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(20, 20).Alpha);
        Assert.Equal(0, bitmap.GetPixel(35, 35).Alpha);
    }

    [Fact]
    public void EvenOddClipPunchesLabelGapThroughHorizontalLine()
    {
        const uint evenOdd = 1u << 16;
        var commands = new[]
        {
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand { Kind = 17, V0 = 0, V1 = 0, V2 = 100, V3 = 100 },
            new NativeCanvasCommand { Kind = 17, V0 = 30, V1 = 35, V2 = 40, V3 = 30 },
            new NativeCanvasCommand { Kind = 18, Flags = evenOdd },
            new NativeCanvasCommand { Kind = 9 },
            new NativeCanvasCommand { Kind = 11, V0 = 0, V1 = 50 },
            new NativeCanvasCommand { Kind = 12, V0 = 100, V1 = 50 },
            new NativeCanvasCommand { Kind = 42, V0 = 4 },
            new NativeCanvasCommand { Kind = 20 }
        };

        using var bitmap = Render(commands);

        Assert.NotEqual(0, bitmap.GetPixel(15, 50).Alpha);
        Assert.Equal(0, bitmap.GetPixel(50, 50).Alpha);
        Assert.NotEqual(0, bitmap.GetPixel(85, 50).Alpha);
    }

    private static SKBitmap Render(NativeCanvasCommand[] commands)
    {
        var renderer = new NativeCanvasSceneRenderer();
        try
        {
            var layer = new NativeCanvasLayer
            {
                NodeId = 1,
                Flags = 1,
                CommandCount = checked((uint)commands.Length),
                Width = 100,
                Height = 100,
                BitmapWidth = 100,
                BitmapHeight = 100,
                Generation = 1
            };
            var bitmap = new SKBitmap(100, 100);
            using var canvas = new SKCanvas(bitmap);
            canvas.Clear(SKColors.Transparent);
            fixed (NativeCanvasCommand* commandPointer = commands)
            {
                var layerPointer = &layer;
                var view = new NativeSceneView
                {
                    Header = new SceneHeader
                    {
                        Revision = 1,
                        CanvasLayerCount = 1,
                        Flags = 1,
                        ViewportWidth = 100,
                        ViewportHeight = 100
                    },
                    CanvasLayers = layerPointer,
                    CanvasCommands = commandPointer,
                    CanvasCommandCount = checked((uint)commands.Length)
                };
                Assert.True(renderer.ApplyDiffAndRender(canvas, &view));
            }
            return bitmap;
        }
        finally
        {
            renderer.Reset();
        }
    }
}

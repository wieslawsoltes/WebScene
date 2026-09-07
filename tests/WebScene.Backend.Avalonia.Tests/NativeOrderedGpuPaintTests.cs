using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeOrderedGpuPaintTests
{
    [Fact]
    public void OrderedReplayPreservesInterleavingAndUpdatesImagesWithoutRecompilingDom()
    {
        var renderer = new NativeCanvasSceneRenderer();
        var commands = stackalloc SceneCommand[] {
            new() { Kind = 1, Width = 20, Height = 10, Rgba = 0x0000ffff },
            new() { Kind = 12, X = 2, Width = 16, Height = 10 },
            new() { Kind = 30, Rgba = 128 },
            new() { Kind = 256, Width = 12, Height = 10, Rgba = 0 },
            new() { Kind = 31 }, new() { Kind = 13 },
            new() { Kind = 9, X = 6, Width = 4, Height = 10, Rgba = 0xffff00ff },
            new() { Kind = 256, X = 14, Width = 6, Height = 10, Rgba = 1 }
        };
        var scene = new NativeSceneView { Commands = commands,
            Header = new SceneHeader { Revision = 1, Flags = 3, CommandCount = 8, ViewportWidth = 20, ViewportHeight = 10 } };
        try
        {
            Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
            using var pixels = new SKBitmap(20, 10);
            using var canvas = new SKCanvas(pixels);
            foreach (var foreground in new[] { SKColors.Green, SKColors.White })
            {
                var originalSaveCount = canvas.SaveCount;
                renderer.RenderRetained(canvas, 20, 10, null, (slot, rect) =>
                {
                    using var paint = new SKPaint { Color = slot == 0 ? SKColors.Red : foreground };
                    canvas.DrawRect(rect, paint);
                });
                Assert.Equal(originalSaveCount, canvas.SaveCount);
                Assert.Equal(SKColors.Blue, pixels.GetPixel(0, 5));
                var blended = pixels.GetPixel(3, 5);
                Assert.InRange((int)blended.Red, 127, 129);
                Assert.InRange((int)blended.Blue, 126, 128);
                Assert.Equal(SKColors.Yellow, pixels.GetPixel(7, 5));
                Assert.Equal(foreground, pixels.GetPixel(16, 5));
            }
        }
        finally { renderer.Reset(); }
    }

    [Fact]
    public void OrderedGpuTransformAndFailedDrawRestoreHostState()
    {
        var renderer = new NativeCanvasSceneRenderer();
        var commands = stackalloc SceneCommand[] {
            new() { Kind = 15, Width = 2, Height = 2 },
            new() { Kind = 256, X = 2, Y = 2, Width = 3, Height = 3 },
            new() { Kind = 16 }
        };
        var scene = new NativeSceneView { Commands = commands,
            Header = new SceneHeader { Revision = 1, Flags = 3, CommandCount = 3, ViewportWidth = 20, ViewportHeight = 20 } };
        try
        {
            Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
            using var bitmap = new SKBitmap(20, 20);
            using var canvas = new SKCanvas(bitmap);
            canvas.Clear(SKColors.Blue);
            var save = canvas.SaveCount;
            var matrix = canvas.TotalMatrix;
            renderer.RenderRetained(canvas, 20, 20, null, (_, rect) =>
            {
                using var paint = new SKPaint { Color = SKColors.Red };
                canvas.DrawRect(rect, paint);
            });
            Assert.Equal(SKColors.Red, bitmap.GetPixel(5, 5));
            Assert.Equal(SKColors.Blue, bitmap.GetPixel(2, 2));
            Assert.Throws<ApplicationException>(() => renderer.RenderRetained(canvas, 20, 20, null,
                (_, _) => throw new ApplicationException("Image unavailable")));
            Assert.Equal(save, canvas.SaveCount);
            Assert.Equal(matrix, canvas.TotalMatrix);
            commands[2].Kind = 13; // A clip pop cannot close the scale scope.
            Assert.False(renderer.ApplyDiff(&scene, orderedGpuImages: true));
        }
        finally { renderer.Reset(); }
    }

    [Fact]
    public void OrderedModeRejectsUnplacedLegacyCanvasLayers()
    {
        var renderer = new NativeCanvasSceneRenderer();
        var scene = new NativeSceneView { Header = new SceneHeader { Flags = 3, CanvasLayerCount = 1 } };
        Assert.False(renderer.ApplyDiff(&scene, orderedGpuImages: true));
    }
}

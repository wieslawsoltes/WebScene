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
    public void CanvasLayerSlotsInterleaveWithGpuAndKeepIncrementalPlacement()
    {
        var renderer = new NativeCanvasSceneRenderer();
        var commands = stackalloc SceneCommand[] {
            new() { Kind = 256, Width = 20, Height = 10, Rgba = 0 },
            new() { Kind = 257, NodeId = 7 },
            new() { Kind = 256, X = 8, Width = 4, Height = 10, Rgba = 1 },
            new() { Kind = 9, X = 14, Width = 3, Height = 10, Rgba = 0xffff00ff }
        };
        var canvasCommand = new NativeCanvasCommand { Kind = 22, V2 = 8, V3 = 10 };
        var layer = new NativeCanvasLayer { NodeId = 7, Flags = 1, CommandCount = 1,
            X = 4, Width = 8, Height = 10, BitmapWidth = 8, BitmapHeight = 10, Generation = 1 };
        var scene = new NativeSceneView { Commands = commands, CanvasLayers = &layer,
            CanvasCommands = &canvasCommand, CanvasCommandCount = 1,
            Header = new SceneHeader { Revision = 1, Flags = 3, CommandCount = 4,
                CanvasLayerCount = 1, ViewportWidth = 20, ViewportHeight = 10 } };
        try
        {
            Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
            using var bitmap = new SKBitmap(20, 10);
            using var canvas = new SKCanvas(bitmap);
            void Draw() => renderer.RenderRetained(canvas, 20, 10, null, (slot, rect) =>
            {
                using var paint = new SKPaint { Color = slot == 0 ? SKColors.Red : SKColors.Green };
                canvas.DrawRect(rect, paint);
            });
            Draw();
            Assert.Equal(SKColors.Red, bitmap.GetPixel(1, 5));
            Assert.Equal(SKColors.Black, bitmap.GetPixel(5, 5));
            Assert.Equal(SKColors.Green, bitmap.GetPixel(9, 5));
            Assert.Equal(SKColors.Yellow, bitmap.GetPixel(15, 5));
            // Change only the layer layout; retain the compiled DOM/GPU paint slots.
            layer.X = 2; layer.Width = 4; layer.Generation = 2;
            scene.Header.Flags = 0; scene.Header.Revision = 2; scene.Header.BaseRevision = 1;
            scene.Header.CommandCount = 0;
            Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
            Draw();
            Assert.Equal(SKColors.Black, bitmap.GetPixel(3, 5));
            Assert.Equal(SKColors.Red, bitmap.GetPixel(7, 5));
            // Removing the layer while leaving a live marker is rejected atomically.
            layer.Flags = 2; scene.Header.Revision = 3; scene.Header.BaseRevision = 2;
            Assert.False(renderer.ApplyDiff(&scene, orderedGpuImages: true));
            Draw();
            Assert.Equal(SKColors.Black, bitmap.GetPixel(3, 5));
        }
        finally { renderer.Reset(); }
    }

    [Fact]
    public void TextAcrossGpuBoundaryMatchesUnsegmentedReplayAfterCompilation()
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes("16\t20\t400\tleft\tsans-serif\tauto\tKestrel 0123");
        fixed (byte* data = bytes)
        {
            var resource = new NativeSceneString { ByteLength = (uint)bytes.Length };
            var commands = stackalloc SceneCommand[] {
                new() { Kind = 3, X = 2, Y = 2, Width = 120, Height = 24, Rgba = 0x000000ff },
                new() { Kind = 256 }, // Separates pictures without painting pixels.
                new() { Kind = 3, X = 2, Y = 30, Width = 120, Height = 24, Rgba = 0x000000ff }
            };
            var scene = new NativeSceneView { Commands = commands, Strings = &resource,
                StringCount = 1, StringBytes = data, StringByteCount = (uint)bytes.Length,
                Header = new SceneHeader { Revision = 1, Flags = 3, CommandCount = 3,
                    ViewportWidth = 128, ViewportHeight = 60 } };
            using var segmented = new SKBitmap(128, 60);
            using var reference = new SKBitmap(128, 60);
            var renderer = new NativeCanvasSceneRenderer();
            try
            {
                Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
                // All compilation shapers have been disposed before replay.
                using (var canvas = new SKCanvas(segmented))
                {
                    canvas.Clear(SKColors.White);
                    renderer.RenderRetained(canvas, 128, 60, null, (_, _) => { });
                }
                commands[1].Kind = 0;
                scene.Header.Revision = 2;
                Assert.True(renderer.ApplyDiff(&scene, orderedGpuImages: true));
                using (var canvas = new SKCanvas(reference))
                {
                    canvas.Clear(SKColors.White);
                    renderer.RenderRetained(canvas, 128, 60, null, (_, _) => { });
                }
                var ink = 0;
                for (var y = 0; y < 60; ++y)
                    for (var x = 0; x < 128; ++x)
                    {
                        Assert.Equal(reference.GetPixel(x, y), segmented.GetPixel(x, y));
                        if (segmented.GetPixel(x, y) != SKColors.White) ++ink;
                    }
                Assert.True(ink > 100, "Both comparisons must contain rendered glyphs.");
            }
            finally { renderer.Reset(); }
        }
    }

    [Fact]
    public void OrderedModeRejectsUnplacedLegacyCanvasLayers()
    {
        var renderer = new NativeCanvasSceneRenderer();
        var scene = new NativeSceneView { Header = new SceneHeader { Flags = 3, CanvasLayerCount = 1 } };
        Assert.False(renderer.ApplyDiff(&scene, orderedGpuImages: true));
    }
}

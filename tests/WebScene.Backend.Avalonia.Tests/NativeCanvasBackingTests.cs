using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

[Collection("Native web-font cache")]
public sealed unsafe class NativeCanvasBackingTests
{
    private static void Apply(NativeCanvasSceneRenderer renderer, NativeCanvasCommand[] commands,
        ulong revision, ulong generation = 1, byte[]? checkpoint = null)
    {
        checkpoint ??= [];
        fixed (NativeCanvasCommand* data = commands)
        fixed (byte* bytes = checkpoint)
        {
            var resource = new NativeSceneString { ByteLength = (uint)checkpoint.Length };
            var layer = new NativeCanvasLayer { NodeId = 7, Flags = 1, Generation = generation,
                CommandCount = (uint)commands.Length, StringCount = checkpoint.Length == 0 ? 0u : 1u,
                Width = 20, Height = 10, BitmapWidth = 20, BitmapHeight = 10 };
            var scene = new NativeSceneView { StructSize = (uint)sizeof(NativeSceneView), AbiVersion = 2,
                CanvasLayers = &layer, CanvasCommands = data, CanvasCommandCount = (uint)commands.Length,
                Strings = &resource, StringBytes = bytes, StringCount = layer.StringCount, StringByteCount = (uint)checkpoint.Length,
                Header = new SceneHeader { Revision = revision, BaseRevision = revision - 1,
                    Flags = revision == 1 ? 1u : 0u, CanvasLayerCount = 1, ViewportWidth = 20, ViewportHeight = 10 } };
            Assert.True(renderer.ApplyDiff(&scene));
        }
    }

    [Fact]
    public void RasterCheckpointPreservesFractionalClearPathTransformAndExport()
    {
        var bounded = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = true };
        var full = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = false };
        var original = new List<NativeCanvasCommand> {
            new() { Kind=22,V0=19,V2=1,V3=10 }, new() { Kind=6,V0=.25,V1=.5 },
            new() { Kind=11,V0=1,V1=1 }, new() { Kind=12,V0=10,V1=1 }
        };
        try
        {
            Apply(bounded,original.ToArray(),1);
            Apply(full,original.ToArray(),1);
            var checkpoint=bounded.EncodeRetainedCanvasCheckpoint(7);
            var continued = new List<NativeCanvasCommand> { new() { Kind=58 } };
            for (ulong revision=2;revision<14;revision++)
            {
                NativeCanvasCommand[] tail = [new() { Kind=24,V2=19.25,V3=10 },
                    new() { Kind=12,V0=revision%17,V1=1 }, new() { Kind=20 }];
                continued.AddRange(tail);
                original.AddRange(tail);Apply(full,original.ToArray(),revision);
                Apply(bounded,continued.ToArray(),revision,2,checkpoint);
                var expected=Pixels(full);var actual=Pixels(bounded);
                for(var pixel=0;pixel<expected.Length;pixel++)
                    Assert.True(Math.Abs(expected[pixel].Alpha-actual[pixel].Alpha)<=1,
                        $"revision={revision} pixel={pixel} expected={expected[pixel]} actual={actual[pixel]}");
                using var exported=SKBitmap.Decode(bounded.CaptureCanvasPng(7));
                Assert.Equal(actual,exported.Pixels);
            }
        }
        finally { bounded.Reset();full.Reset(); }
    }

    private static SKColor[] Pixels(NativeCanvasSceneRenderer renderer)
    {
        using var bitmap = new SKBitmap(20, 10);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.Transparent);
        renderer.RenderRetained(canvas, 20, 10, null);
        return bitmap.Pixels;
    }

    [Fact]
    public void AppendedCommandsPreservePathsStatePartialClearsAndCpuExport()
    {
        var cached = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = true };
        var full = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = false };
        var commands = new List<NativeCanvasCommand> { new() { Kind = 11, V0 = 1, V1 = 1 } };
        try
        {
            for (ulong revision = 1; revision <= 270; ++revision)
            {
                commands.Add(new() { Kind = 24, V2 = 19.5, V3 = 10 });
                commands.Add(new() { Kind = 12, V0 = revision % 16 + 1, V1 = revision % 8 + 1 });
                commands.Add(new() { Kind = 20 });
                commands.Add(new() { Kind = 1 });
                commands.Add(new() { Kind = 6, V0 = 0.25, V1 = 0.5 });
                commands.Add(new() { Kind = 22, V0 = 19, V2 = 1, V3 = 10 });
                commands.Add(new() { Kind = 2 });
                var data = commands.ToArray();
                Apply(cached, data, revision); Apply(full, data, revision);
                if (revision is 1 or 2 or 128 or 257 or 270) Assert.Equal(Pixels(full), Pixels(cached));
            }
            Assert.True(cached.ResumedCanvasCompilations > 260);
            Assert.Equal(full.CaptureCanvasPng(7), cached.CaptureCanvasPng(7));
        }
        finally { cached.Reset(); full.Reset(); }
    }

    [Fact]
    public void ChangedPrefixesGenerationAndScaleInvalidateContinuation()
    {
        var renderer = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = true };
        NativeCanvasCommand[] commands = [new() { Kind = 22, V2 = 4, V3 = 10 }];
        try
        {
            Apply(renderer, commands, 1);
            commands[0].V2 = 12;
            Apply(renderer, commands, 2);
            Assert.Equal(0, renderer.ResumedCanvasCompilations);
            Assert.Equal(SKColors.Black, Pixels(renderer)[8]);
            Apply(renderer, commands, 3, generation: 2);
            Assert.Equal(0, renderer.ResumedCanvasCompilations);
            renderer.SetPresenterDeviceScaleFactor(1.75);
            Apply(renderer, commands, 4, generation: 2);
            Assert.Equal(0, renderer.ResumedCanvasCompilations);
            Apply(renderer, commands, 5, generation: 2);
            Assert.Equal(1, renderer.ResumedCanvasCompilations);
        }
        finally { renderer.Reset(); }
    }

    [Theory]
    [InlineData(1u)] // Active save stack cannot be reconstructed from paint state alone.
    [InlineData(18u)] // A clip path requires complete replay, even if later restored.
    [InlineData(27u)] // Another canvas can change without changing these commands.
    [InlineData(31u)] // External image dependencies are not immutable command resources.
    public void StateAndImageDependenciesUseFullReplay(uint unsupported)
    {
        var renderer = new NativeCanvasSceneRenderer { UseIncrementalCanvasBacking = true };
        NativeCanvasCommand[] commands = [new() { Kind = unsupported }];
        try
        {
            Apply(renderer, commands, 1); Apply(renderer, commands, 2);
            Assert.Equal(0, renderer.ResumedCanvasCompilations);
        }
        finally { renderer.Reset(); }
    }
}

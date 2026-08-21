using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeCanvasSceneRendererCullingTests
{
    [Theory]
    [InlineData(0, 0, 10, 10)]
    [InlineData(-5, -5, 10, 10)]
    [InlineData(95, 95, 10, 10)]
    public void IntersectsViewport_keeps_visible_and_partially_visible_layers(
        float x,
        float y,
        float width,
        float height)
    {
        Assert.True(NativeCanvasSceneRenderer.IntersectsViewport(
            x,
            y,
            width,
            height,
            viewportWidth: 100,
            viewportHeight: 100));
    }

    [Theory]
    [InlineData(-10, 0, 10, 10)]
    [InlineData(0, -10, 10, 10)]
    [InlineData(100, 0, 10, 10)]
    [InlineData(0, 100, 10, 10)]
    public void IntersectsViewport_culls_fully_separated_layers(
        float x,
        float y,
        float width,
        float height)
    {
        Assert.False(NativeCanvasSceneRenderer.IntersectsViewport(
            x,
            y,
            width,
            height,
            viewportWidth: 100,
            viewportHeight: 100));
    }

    [Fact]
    public void IntersectsViewport_preserves_non_finite_geometry()
    {
        Assert.True(NativeCanvasSceneRenderer.IntersectsViewport(
            float.NaN,
            0,
            10,
            10,
            viewportWidth: 100,
            viewportHeight: 100));
        Assert.True(NativeCanvasSceneRenderer.IntersectsViewport(
            0,
            0,
            float.PositiveInfinity,
            10,
            viewportWidth: 100,
            viewportHeight: 100));
    }

    [Fact]
    public void Incremental_replacement_and_reposition_preserve_order_identity()
    {
        var renderer = new NativeCanvasSceneRenderer();
        try
        {
            var layers = Enumerable.Range(0, 4)
                .Select(index => Layer(index, index, checked((uint)index)))
                .ToArray();
            var commands = Commands(4);
            Assert.True(Apply(renderer, layers, commands, 1, 0, checkpoint: true));
            Assert.True(renderer.HasConsistentLayerOrder());

            layers = [Layer(0, 0, 10)];
            commands = Commands(1);
            Assert.True(Apply(renderer, layers, commands, 2, 1, checkpoint: false));
            Assert.True(renderer.HasConsistentLayerOrder());

            layers = [Layer(0, 0, 10, generation: 3)];
            Assert.True(Apply(renderer, layers, commands, 3, 2, checkpoint: false));
            Assert.True(renderer.HasConsistentLayerOrder());

            layers = [Layer(0, 0, 0, generation: 4)];
            Assert.True(Apply(renderer, layers, commands, 4, 3, checkpoint: false));
            Assert.True(renderer.HasConsistentLayerOrder());
        }
        finally
        {
            renderer.Reset();
        }
    }

    [Fact]
    public void Invalid_checkpoint_is_rejected_before_live_scene_reset()
    {
        var renderer = new NativeCanvasSceneRenderer();
        try
        {
            var layers = Enumerable.Range(0, 4)
                .Select(index => Layer(index, index, checked((uint)index)))
                .ToArray();
            Assert.True(Apply(renderer, layers, Commands(4), 1, 0, checkpoint: true));
            Assert.Equal(4, renderer.TotalCommandCount);

            var malformed = Layer(0, commandOffset: 1, zOrder: 0);
            Assert.False(Apply(
                renderer,
                [malformed],
                Commands(1),
                revision: 2,
                baseRevision: 0,
                checkpoint: true));
            Assert.Equal(4, renderer.TotalCommandCount);
            Assert.True(renderer.HasConsistentLayerOrder());
        }
        finally
        {
            renderer.Reset();
        }
    }

    private static NativeCanvasLayer Layer(
        int index,
        int commandOffset,
        uint zOrder,
        ulong generation = 1)
        => new()
        {
            NodeId = checked((uint)index + 1),
            Flags = 1,
            CommandOffset = checked((uint)commandOffset),
            CommandCount = 1,
            Reserved = zOrder,
            Width = 8,
            Height = 8,
            BitmapWidth = 8,
            BitmapHeight = 8,
            Generation = generation
        };

    private static NativeCanvasCommand[] Commands(int count)
        => Enumerable.Range(0, count)
            .Select(_ => new NativeCanvasCommand
            {
                Kind = 22,
                V2 = 8,
                V3 = 8
            })
            .ToArray();

    private static bool Apply(
        NativeCanvasSceneRenderer renderer,
        NativeCanvasLayer[] layers,
        NativeCanvasCommand[] commands,
        ulong revision,
        ulong baseRevision,
        bool checkpoint)
    {
        fixed (NativeCanvasLayer* layerPointer = layers)
        fixed (NativeCanvasCommand* commandPointer = commands)
        {
            var view = new NativeSceneView
            {
                Header = new SceneHeader
                {
                    Revision = revision,
                    BaseRevision = baseRevision,
                    CanvasLayerCount = checked((uint)layers.Length),
                    Flags = checkpoint ? 1U : 0
                },
                CanvasLayers = layerPointer,
                CanvasCommands = commandPointer,
                CanvasCommandCount = checked((uint)commands.Length)
            };
            return renderer.ApplyDiff(&view);
        }
    }
}

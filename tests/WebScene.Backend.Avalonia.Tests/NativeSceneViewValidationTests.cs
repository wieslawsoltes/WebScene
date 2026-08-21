using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeSceneViewValidationTests
{
    [Fact]
    public void RemovalOnlyCanvasDiffDoesNotRequireCommandBuffer()
    {
        var removal = new NativeCanvasLayer
        {
            NodeId = 42,
            Flags = 2
        };
        var view = new NativeSceneView
        {
            StructSize = (uint)sizeof(NativeSceneView),
            AbiVersion = 2,
            Header = new SceneHeader
            {
                Revision = 2,
                BaseRevision = 1,
                CanvasLayerCount = 1
            },
            CanvasLayers = &removal,
            CanvasCommands = null,
            CanvasCommandCount = 0
        };

        Assert.True(NativeSceneViewValidation.IsValid(&view));
    }

    [Fact]
    public void DeclaredCanvasCommandsStillRequireCommandBuffer()
    {
        var removal = new NativeCanvasLayer
        {
            NodeId = 42,
            Flags = 2
        };
        var view = new NativeSceneView
        {
            StructSize = (uint)sizeof(NativeSceneView),
            AbiVersion = 2,
            Header = new SceneHeader
            {
                Revision = 2,
                BaseRevision = 1,
                CanvasLayerCount = 1
            },
            CanvasLayers = &removal,
            CanvasCommands = null,
            CanvasCommandCount = 1
        };

        Assert.False(NativeSceneViewValidation.IsValid(&view));
    }
}

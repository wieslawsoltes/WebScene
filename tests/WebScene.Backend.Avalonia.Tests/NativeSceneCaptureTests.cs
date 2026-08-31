using SkiaSharp;
using System.Text;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeSceneCaptureTests
{
    [Fact]
    public unsafe void RequestedDetachedCanvasCaptureUsesItsOwnBitmapAndDrawImageSources()
    {
        const uint offscreenCanvasLayer = 1u << 31;
        var renderer = new NativeCanvasSceneRenderer();
        try
        {
            var layers = new[]
            {
                new NativeCanvasLayer
                {
                    NodeId = 41,
                    Flags = 1,
                    CommandOffset = 0,
                    CommandCount = 1,
                    Reserved = offscreenCanvasLayer,
                    BitmapWidth = 8,
                    BitmapHeight = 6,
                    Generation = 1
                },
                new NativeCanvasLayer
                {
                    NodeId = 42,
                    Flags = 1,
                    CommandOffset = 1,
                    CommandCount = 1,
                    Reserved = offscreenCanvasLayer | 1,
                    BitmapWidth = 6,
                    BitmapHeight = 4,
                    Generation = 1
                }
            };
            var commands = new[]
            {
                new NativeCanvasCommand
                {
                    Kind = 22,
                    V0 = 0,
                    V1 = 0,
                    V2 = 8,
                    V3 = 6
                },
                new NativeCanvasCommand
                {
                    Kind = 27,
                    ResourceId = 41,
                    V0 = 0,
                    V1 = 0,
                    V2 = 8,
                    V3 = 6,
                    V4 = 1,
                    V5 = 1,
                    V6 = 4,
                    V7 = 2
                }
            };

            fixed (NativeCanvasLayer* layerPointer = layers)
            fixed (NativeCanvasCommand* commandPointer = commands)
            {
                var view = new NativeSceneView
                {
                    Header = new SceneHeader
                    {
                        Revision = 1,
                        CanvasLayerCount = 2,
                        Flags = 1,
                        ViewportWidth = 100,
                        ViewportHeight = 100
                    },
                    CanvasLayers = layerPointer,
                    CanvasCommands = commandPointer,
                    CanvasCommandCount = 2
                };
                Assert.True(renderer.ApplyDiff(&view));
            }

            var png = renderer.CaptureCanvasPng(42);
            Assert.NotNull(png);
            using var bitmap = SKBitmap.Decode(png);
            Assert.NotNull(bitmap);
            Assert.Equal(6, bitmap.Width);
            Assert.Equal(4, bitmap.Height);
            Assert.Equal(0, bitmap.GetPixel(0, 0).Alpha);
            Assert.Equal(255, bitmap.GetPixel(2, 2).Alpha);
            Assert.Null(renderer.CaptureCanvasPng(999));
        }
        finally
        {
            renderer.Reset();
        }
    }

    [Fact]
    public unsafe void ReverseOrderedNestedCanvasSourcesCompileBeforeTheirWrappers()
    {
        const uint offscreenCanvasLayer = 1u << 31;
        var renderer = new NativeCanvasSceneRenderer();
        try
        {
            // TradingView creates the exported canvas and pixel-ratio wrapper
            // before the axis backing canvas. Node order is therefore the
            // reverse of drawImage dependency order.
            var layers = new[]
            {
                Layer(nodeId: 101, commandOffset: 0, zOrder: 0),
                Layer(nodeId: 102, commandOffset: 1, zOrder: 1),
                Layer(nodeId: 103, commandOffset: 2, zOrder: 2)
            };
            var commands = new[]
            {
                DrawCanvas(sourceNodeId: 102),
                DrawCanvas(sourceNodeId: 103),
                new NativeCanvasCommand
                {
                    Kind = 22,
                    V0 = 0,
                    V1 = 0,
                    V2 = 8,
                    V3 = 6
                }
            };

            fixed (NativeCanvasLayer* layerPointer = layers)
            fixed (NativeCanvasCommand* commandPointer = commands)
            {
                var view = new NativeSceneView
                {
                    Header = new SceneHeader
                    {
                        Revision = 1,
                        CanvasLayerCount = 3,
                        Flags = 1,
                        ViewportWidth = 100,
                        ViewportHeight = 100
                    },
                    CanvasLayers = layerPointer,
                    CanvasCommands = commandPointer,
                    CanvasCommandCount = 3
                };
                Assert.True(renderer.ApplyDiff(&view));
            }

            var png = renderer.CaptureCanvasPng(101);
            Assert.NotNull(png);
            using var bitmap = SKBitmap.Decode(png);
            Assert.NotNull(bitmap);
            Assert.Equal(255, bitmap.GetPixel(3, 2).Alpha);
        }
        finally
        {
            renderer.Reset();
        }

        static NativeCanvasLayer Layer(uint nodeId, uint commandOffset, uint zOrder)
            => new()
            {
                NodeId = nodeId,
                Flags = 1,
                CommandOffset = commandOffset,
                CommandCount = 1,
                Reserved = offscreenCanvasLayer | zOrder,
                BitmapWidth = 8,
                BitmapHeight = 6,
                Generation = 1
            };

        static NativeCanvasCommand DrawCanvas(uint sourceNodeId)
            => new()
            {
                Kind = 27,
                ResourceId = sourceNodeId,
                V0 = 0,
                V1 = 0,
                V2 = 8,
                V3 = 6,
                V4 = 0,
                V5 = 0,
                V6 = 8,
                V7 = 6
            };
    }

    [Fact]
    public unsafe void SvgBlobImageDrawsIntoDetachedExportCanvas()
    {
        const uint offscreenCanvasLayer = 1u << 31;
        var renderer = new NativeCanvasSceneRenderer();
        var resource = Encoding.UTF8.GetBytes(
            "0 0 10 4\t<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 4\">" +
            "<rect width=\"10\" height=\"4\" fill=\"#2962ff\"/></svg>");
        var strings = new[]
        {
            new NativeSceneString { ByteOffset = 0, ByteLength = (uint)resource.Length }
        };
        var layers = new[]
        {
            new NativeCanvasLayer
            {
                NodeId = 201,
                Flags = 1,
                CommandOffset = 0,
                CommandCount = 1,
                StringOffset = 0,
                StringCount = 1,
                Reserved = offscreenCanvasLayer,
                BitmapWidth = 14,
                BitmapHeight = 6,
                Generation = 1
            }
        };
        var commands = new[]
        {
            new NativeCanvasCommand
            {
                Kind = 31,
                ResourceId = 0,
                V0 = 0,
                V1 = 0,
                V2 = 10,
                V3 = 4,
                V4 = 2,
                V5 = 1,
                V6 = 10,
                V7 = 4
            }
        };
        try
        {
            fixed (NativeCanvasLayer* layerPointer = layers)
            fixed (NativeCanvasCommand* commandPointer = commands)
            fixed (NativeSceneString* stringPointer = strings)
            fixed (byte* resourcePointer = resource)
            {
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
                    CanvasCommandCount = 1,
                    Strings = stringPointer,
                    StringCount = 1,
                    StringBytes = resourcePointer,
                    StringByteCount = (uint)resource.Length
                };
                Assert.True(renderer.ApplyDiff(&view));
            }

            var png = renderer.CaptureCanvasPng(201);
            Assert.NotNull(png);
            using var bitmap = SKBitmap.Decode(png);
            Assert.NotNull(bitmap);
            Assert.Equal(0, bitmap.GetPixel(0, 0).Alpha);
            var painted = bitmap.GetPixel(6, 3);
            Assert.Equal(255, painted.Alpha);
            Assert.True(painted.Blue > 200);
        }
        finally
        {
            renderer.Reset();
        }
    }

    [Fact]
    public async Task CompositionCaptureCompletesFromRetainedRendererWithoutDrivingSceneLane()
    {
        var instrumentation = new NativePerformanceInstrumentation();
        var mailbox = new NativeScenePublicationMailbox();
        mailbox.Publish();
        var handler = new NativeSceneCompositionHandler(
            IntPtr.Zero,
            new NativeSceneRenderObserver(instrumentation),
            mailbox,
            new NativeSceneUiWakeGate(),
            instrumentation,
            static () => { },
            deviceScaleFactor: 1);
        var request = new NativeSceneCaptureRequest(width: 13, height: 7);

        handler.OnMessage(request);

        var png = await request.Completion.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.NotNull(png);
        using var bitmap = SKBitmap.Decode(png);
        Assert.NotNull(bitmap);
        Assert.Equal(13, bitmap.Width);
        Assert.Equal(7, bitmap.Height);
        Assert.Equal(1, mailbox.PendingCount);
    }

    [Fact]
    public async Task CaptureRequestCompletesOnlyOnce()
    {
        var request = new NativeSceneCaptureRequest(width: 1, height: 1);

        Assert.True(request.TrySetResult([1, 2, 3]));
        Assert.False(request.TrySetResult([4, 5, 6]));

        Assert.Equal([1, 2, 3], await request.Completion);
    }
}

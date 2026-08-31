using SkiaSharp;
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

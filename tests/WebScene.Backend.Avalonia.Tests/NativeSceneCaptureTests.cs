using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeSceneCaptureTests
{
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

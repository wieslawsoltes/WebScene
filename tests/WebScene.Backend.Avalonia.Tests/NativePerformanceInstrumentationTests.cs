using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativePerformanceInstrumentationTests
{
    [Fact]
    public void DisabledInstrumentationRetainsOnlyFunctionalRenderState()
    {
        var instrumentation = new NativePerformanceInstrumentation();
        var observer = new NativeSceneRenderObserver(instrumentation);
        var header = new SceneHeader
        {
            Revision = 7,
            ViewportHeight = 480
        };

        observer.RecordScheduling("frame", 2, 7, false);
        observer.RecordPresented();
        observer.RecordRendered(header);

        Assert.False(instrumentation.IsEnabled);
        Assert.Equal(1, observer.RenderedSceneCount);
        Assert.NotEqual(0, observer.FirstRenderedSceneTimestamp);
        Assert.Empty(observer.SchedulingSamples);
        Assert.Empty(observer.Presentations);
        Assert.Empty(observer.RenderedScenes);
        Assert.Empty(observer.RenderedViewportHeights);
    }

    [Fact]
    public void EnablingInstrumentationCapturesBoundedPresenterDetails()
    {
        var instrumentation = new NativePerformanceInstrumentation();
        var observer = new NativeSceneRenderObserver(instrumentation);
        instrumentation.Enable();
        var header = new SceneHeader
        {
            Revision = 11,
            ConsumedInputSequence = 23,
            ViewportHeight = 720
        };

        observer.RecordPresented();
        observer.RecordRendered(header);

        for (var i = 0; i < 4100; ++i)
            observer.RecordScheduling("frame", i % 3, (ulong)i, false);
        Assert.Equal(4096, observer.SchedulingSamples.Length);
        Assert.Equal(4UL, observer.SchedulingSamples[0].Revision);
        Assert.Equal(4099UL, observer.SchedulingSamples[^1].Revision);

        Assert.True(instrumentation.IsEnabled);
        Assert.Single(observer.Presentations);
        var rendered = Assert.Single(observer.RenderedScenes);
        Assert.Equal(11UL, rendered.Revision);
        Assert.Equal(23UL, rendered.ConsumedInputSequence);
        Assert.Equal(new[] { 720 }, observer.RenderedViewportHeights);
    }
}

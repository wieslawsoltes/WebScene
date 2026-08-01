using System.Diagnostics;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class NativePerformanceSnapshotTests
{
    [Fact]
    public void SinceReportsMonotonicWorkAndPreservesProcessScope()
    {
        var baseline = Snapshot(
            contextId: 7,
            timestamp: 100,
            publishedScenes: 10,
            renderedScenes: 8,
            compositionRenders: 20,
            outstandingResults: 1,
            timersScheduled: 4);
        var current = Snapshot(
            contextId: 7,
            timestamp: 100 + Stopwatch.Frequency,
            publishedScenes: 16,
            renderedScenes: 13,
            compositionRenders: 29,
            outstandingResults: 0,
            timersScheduled: 11);

        var delta = current.Since(baseline);

        Assert.Equal(TimeSpan.FromSeconds(1), delta.Elapsed);
        Assert.Equal(6UL, delta.PublishedScenes);
        Assert.Equal(5, delta.RenderedScenes);
        Assert.Equal(9, delta.CompositionRenders);
        Assert.Equal(7UL, delta.TimersScheduled);
        Assert.Equal(0UL, current.InteropPool.OutstandingResults);
    }

    [Fact]
    public void SinceRejectsAnotherLoadedContext()
    {
        var baseline = Snapshot(1, 100, 0, 0, 0, 0);
        var current = Snapshot(2, 200, 0, 0, 0, 0);

        Assert.Throws<ArgumentException>(() => current.Since(baseline));
    }

    [Fact]
    public void SinceSaturatesResetOrWrappedCounters()
    {
        var baseline = Snapshot(1, 100, 10, 10, 10, 0);
        var current = Snapshot(1, 200, 2, 2, 2, 0);

        var delta = current.Since(baseline);

        Assert.Equal(0UL, delta.PublishedScenes);
        Assert.Equal(0, delta.RenderedScenes);
        Assert.Equal(0, delta.CompositionRenders);
    }

    private static NativeWebScenePerformanceSnapshot Snapshot(
        long contextId,
        long timestamp,
        ulong publishedScenes,
        long renderedScenes,
        long compositionRenders,
        ulong outstandingResults,
        ulong timersScheduled = 0)
    {
        var engine = new EngineMetrics
        {
            PublishedScenes = publishedScenes
        };
        var interop = new NativeInteropPoolMetrics
        {
            OutstandingResults = outstandingResults
        };
        return new NativeWebScenePerformanceSnapshot(
            ContextId: contextId,
            Timestamp: timestamp,
            Engine: engine,
            InputDispatch: default,
            AnimationFrames: default,
            SceneFlow: default,
            ResizeFrames: default,
            ResourceCache: default,
            RuntimeWork: new RuntimeWorkMetrics
            {
                TimersScheduled = timersScheduled
            },
            ProcessCache: null,
            Memory: null,
            InteropPool: interop,
            RendererMemory: default,
            Surface: new NativeSurfacePerformanceMetrics(
                RenderedScenes: renderedScenes,
                RoutedInputEvents: 0,
                AcceptedInputEvents: 0,
                CompositionUiWakes: 0,
                PendingCompositionPublications: 0,
                ResizePublicationNotifications: 0),
            ProcessWebTypefaces: default,
            ProcessComposition: new NativeCompositionFlowMetrics(
                AnimationFrames: 0,
                Renders: compositionRenders,
                AppliedDiffs: 0,
                InvalidationCalls: 0,
                DamageRectangles: 0,
                FullInvalidations: 0,
                SuppressedLiveResizeAnimationFrames: 0,
                SubmittedAnimationFrames: 0,
                SkippedEmptyAnimationFrames: 0,
                RenderCallbacks: 0,
                UnchangedRenderCallbacks: 0,
                LastAnimationFrameDemand: 0));
    }
}

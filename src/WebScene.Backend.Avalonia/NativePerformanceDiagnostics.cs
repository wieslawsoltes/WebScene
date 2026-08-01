using System.Diagnostics;

namespace WebScene.Backends.Avalonia.Native;

/// <summary>
/// A point-in-time, allocation-only-on-request snapshot of native WebScene work.
/// All values come from monotonic counters or retained-size gauges. Capturing the
/// first snapshot opts this context into the detailed runtime-work counters; contexts
/// that are never sampled retain the default disabled hot path.
/// </summary>
public sealed record NativeWebScenePerformanceSnapshot(
    long ContextId,
    long Timestamp,
    EngineMetrics Engine,
    InputDispatchMetrics InputDispatch,
    AnimationFrameMetrics AnimationFrames,
    SceneFlowMetrics SceneFlow,
    ResizeFrameMetrics ResizeFrames,
    ResourceCacheMetrics ResourceCache,
    RuntimeWorkMetrics? RuntimeWork,
    ProcessCacheMetrics? ProcessCache,
    EngineMemoryMetrics? Memory,
    NativeInteropPoolMetrics InteropPool,
    NativeRendererMemoryMetrics RendererMemory,
    NativeSurfacePerformanceMetrics Surface,
    NativeTextShaping.WebTypefaceCacheMetrics ProcessWebTypefaces,
    NativeCompositionFlowMetrics ProcessComposition)
{
    /// <summary>
    /// Produces the monotonic work performed after <paramref name="baseline"/>.
    /// Gauges such as heap size and outstanding leases remain available on the two
    /// snapshots and are intentionally not subtracted.
    /// </summary>
    public NativeWebSceneWorkDelta Since(
        NativeWebScenePerformanceSnapshot baseline)
    {
        ArgumentNullException.ThrowIfNull(baseline);
        if (ContextId == 0 || baseline.ContextId != ContextId)
        {
            throw new ArgumentException(
                "Performance snapshots must belong to the same loaded WebScene context.",
                nameof(baseline));
        }
        if (Timestamp < baseline.Timestamp)
        {
            throw new ArgumentException(
                "The performance baseline must not be newer than the current snapshot.",
                nameof(baseline));
        }

        return new NativeWebSceneWorkDelta(
            Elapsed: TimeSpan.FromSeconds(
                (Timestamp - baseline.Timestamp) / (double)Stopwatch.Frequency),
            EnqueuedInputs: Difference(
                Engine.EnqueuedInputs,
                baseline.Engine.EnqueuedInputs),
            DroppedInputs: Difference(
                Engine.DroppedInputs,
                baseline.Engine.DroppedInputs),
            ConsumedInputs: Difference(
                Engine.ConsumedInputs,
                baseline.Engine.ConsumedInputs),
            ExecutedScripts: Difference(
                Engine.ExecutedScripts,
                baseline.Engine.ExecutedScripts),
            ScriptErrors: Difference(
                Engine.ScriptErrors,
                baseline.Engine.ScriptErrors),
            LayoutPasses: Difference(
                Engine.LayoutPasses,
                baseline.Engine.LayoutPasses),
            AppliedAnimationFrames: Difference(
                Engine.AppliedAnimationFrames,
                baseline.Engine.AppliedAnimationFrames),
            CoalescedAnimationFrames: Difference(
                Engine.CoalescedAnimationFrames,
                baseline.Engine.CoalescedAnimationFrames),
            PublicationAttempts: Difference(
                SceneFlow.PublicationAttempts,
                baseline.SceneFlow.PublicationAttempts),
            BlockedPublications: Difference(
                SceneFlow.BlockedPublications,
                baseline.SceneFlow.BlockedPublications),
            PublishedScenes: Difference(
                Engine.PublishedScenes,
                baseline.Engine.PublishedScenes),
            AcquiredScenes: Difference(
                Engine.AcquiredScenes,
                baseline.Engine.AcquiredScenes),
            AcknowledgedScenes: Difference(
                SceneFlow.AcknowledgedScenes,
                baseline.SceneFlow.AcknowledgedScenes),
            RenderedScenes: Difference(
                Surface.RenderedScenes,
                baseline.Surface.RenderedScenes),
            CompositionUiWakes: Difference(
                Surface.CompositionUiWakes,
                baseline.Surface.CompositionUiWakes),
            RoutedInputEvents: Difference(
                Surface.RoutedInputEvents,
                baseline.Surface.RoutedInputEvents),
            AcceptedInputEvents: Difference(
                Surface.AcceptedInputEvents,
                baseline.Surface.AcceptedInputEvents),
            ResourceRequests: Difference(
                ResourceCache.Requests,
                baseline.ResourceCache.Requests),
            ResourceHits: Difference(
                ResourceCache.Hits,
                baseline.ResourceCache.Hits),
            ResourceMisses: Difference(
                ResourceCache.Misses,
                baseline.ResourceCache.Misses),
            InteropPoolHits: Difference(
                InteropPool.PoolHits,
                baseline.InteropPool.PoolHits),
            InteropPoolMisses: Difference(
                InteropPool.PoolMisses,
                baseline.InteropPool.PoolMisses),
            InteropRequestPoolHits: Difference(
                InteropPool.RequestPoolHits,
                baseline.InteropPool.RequestPoolHits),
            InteropRequestPoolMisses: Difference(
                InteropPool.RequestPoolMisses,
                baseline.InteropPool.RequestPoolMisses),
            TimersScheduled: WorkDifference(
                baseline,
                static value => value.TimersScheduled),
            TimersFired: WorkDifference(
                baseline,
                static value => value.TimersFired),
            TimersCancelled: WorkDifference(
                baseline,
                static value => value.TimersCancelled),
            LateTimers: WorkDifference(
                baseline,
                static value => value.LateTimers),
            TotalTimerLatenessNanoseconds: WorkDifference(
                baseline,
                static value => value.TotalTimerLatenessNanoseconds),
            AnimationFramesRequested: WorkDifference(
                baseline,
                static value => value.AnimationFramesRequested),
            AnimationFramesInvoked: WorkDifference(
                baseline,
                static value => value.AnimationFramesInvoked),
            AnimationFramesCancelled: WorkDifference(
                baseline,
                static value => value.AnimationFramesCancelled),
            MicrotaskCheckpoints: WorkDifference(
                baseline,
                static value => value.MicrotaskCheckpoints),
            WorkerWaits: WorkDifference(
                baseline,
                static value => value.WorkerWaits),
            WorkerSignalledWakes: WorkDifference(
                baseline,
                static value => value.WorkerSignalledWakes),
            WorkerTimeoutWakes: WorkDifference(
                baseline,
                static value => value.WorkerTimeoutWakes),
            SceneBuilds: WorkDifference(
                baseline,
                static value => value.SceneBuilds),
            NoDamageSceneBuilds: WorkDifference(
                baseline,
                static value => value.NoDamageSceneBuilds),
            FullCheckpointSceneBuilds: WorkDifference(
                baseline,
                static value => value.FullCheckpointSceneBuilds),
            ArbitraryEvaluationCalls: WorkDifference(
                baseline,
                static value => value.ArbitraryEvaluationCalls),
            GeneratedInvokeCalls: WorkDifference(
                baseline,
                static value => value.GeneratedInvokeCalls),
            GeneratedCallbackCalls: WorkDifference(
                baseline,
                static value => value.GeneratedCallbackCalls),
            ArbitraryEvaluationSourceBytes: WorkDifference(
                baseline,
                static value => value.ArbitraryEvaluationSourceBytes),
            GeneratedRequestBytes: WorkDifference(
                baseline,
                static value => value.GeneratedRequestBytes),
            WebTypefaceCacheHits: Difference(
                ProcessWebTypefaces.Hits,
                baseline.ProcessWebTypefaces.Hits),
            WebTypefaceCacheMisses: Difference(
                ProcessWebTypefaces.Misses,
                baseline.ProcessWebTypefaces.Misses),
            CompositionAnimationFrames: Difference(
                ProcessComposition.AnimationFrames,
                baseline.ProcessComposition.AnimationFrames),
            CompositionRenders: Difference(
                ProcessComposition.Renders,
                baseline.ProcessComposition.Renders),
            CompositionAppliedDiffs: Difference(
                ProcessComposition.AppliedDiffs,
                baseline.ProcessComposition.AppliedDiffs),
            CompositionInvalidations: Difference(
                ProcessComposition.InvalidationCalls,
                baseline.ProcessComposition.InvalidationCalls),
            CompositionFullInvalidations: Difference(
                ProcessComposition.FullInvalidations,
                baseline.ProcessComposition.FullInvalidations),
            CompositionSubmittedAnimationFrames: Difference(
                ProcessComposition.SubmittedAnimationFrames,
                baseline.ProcessComposition.SubmittedAnimationFrames),
            CompositionSkippedEmptyAnimationFrames: Difference(
                ProcessComposition.SkippedEmptyAnimationFrames,
                baseline.ProcessComposition.SkippedEmptyAnimationFrames),
            CompositionRenderCallbacks: Difference(
                ProcessComposition.RenderCallbacks,
                baseline.ProcessComposition.RenderCallbacks),
            CompositionUnchangedRenderCallbacks: Difference(
                ProcessComposition.UnchangedRenderCallbacks,
                baseline.ProcessComposition.UnchangedRenderCallbacks));
    }

    private static ulong Difference(ulong current, ulong baseline)
        => current >= baseline ? current - baseline : 0;

    private static long Difference(long current, long baseline)
        => current >= baseline ? current - baseline : 0;

    private ulong WorkDifference(
        NativeWebScenePerformanceSnapshot baseline,
        Func<RuntimeWorkMetrics, ulong> select)
    {
        if (RuntimeWork is not { } current
            || baseline.RuntimeWork is not { } previous)
        {
            return 0;
        }
        return Difference(select(current), select(previous));
    }
}

/// <summary>
/// Counters owned by one Avalonia projection surface.
/// </summary>
public readonly record struct NativeSurfacePerformanceMetrics(
    long RenderedScenes,
    long RoutedInputEvents,
    long AcceptedInputEvents,
    long CompositionUiWakes,
    long PendingCompositionPublications,
    long ResizePublicationNotifications);

/// <summary>
/// Monotonic work performed between two snapshots of one loaded context.
/// Composition values are process-wide because Avalonia's custom-visual callbacks
/// are currently counted globally; the other values are context-scoped.
/// </summary>
public readonly record struct NativeWebSceneWorkDelta(
    TimeSpan Elapsed,
    ulong EnqueuedInputs,
    ulong DroppedInputs,
    ulong ConsumedInputs,
    ulong ExecutedScripts,
    ulong ScriptErrors,
    ulong LayoutPasses,
    ulong AppliedAnimationFrames,
    ulong CoalescedAnimationFrames,
    ulong PublicationAttempts,
    ulong BlockedPublications,
    ulong PublishedScenes,
    ulong AcquiredScenes,
    ulong AcknowledgedScenes,
    long RenderedScenes,
    long CompositionUiWakes,
    long RoutedInputEvents,
    long AcceptedInputEvents,
    ulong ResourceRequests,
    ulong ResourceHits,
    ulong ResourceMisses,
    ulong InteropPoolHits,
    ulong InteropPoolMisses,
    ulong InteropRequestPoolHits,
    ulong InteropRequestPoolMisses,
    ulong TimersScheduled,
    ulong TimersFired,
    ulong TimersCancelled,
    ulong LateTimers,
    ulong TotalTimerLatenessNanoseconds,
    ulong AnimationFramesRequested,
    ulong AnimationFramesInvoked,
    ulong AnimationFramesCancelled,
    ulong MicrotaskCheckpoints,
    ulong WorkerWaits,
    ulong WorkerSignalledWakes,
    ulong WorkerTimeoutWakes,
    ulong SceneBuilds,
    ulong NoDamageSceneBuilds,
    ulong FullCheckpointSceneBuilds,
    ulong ArbitraryEvaluationCalls,
    ulong GeneratedInvokeCalls,
    ulong GeneratedCallbackCalls,
    ulong ArbitraryEvaluationSourceBytes,
    ulong GeneratedRequestBytes,
    long WebTypefaceCacheHits,
    long WebTypefaceCacheMisses,
    long CompositionAnimationFrames,
    long CompositionRenders,
    long CompositionAppliedDiffs,
    long CompositionInvalidations,
    long CompositionFullInvalidations,
    long CompositionSubmittedAnimationFrames,
    long CompositionSkippedEmptyAnimationFrames,
    long CompositionRenderCallbacks,
    long CompositionUnchangedRenderCallbacks);

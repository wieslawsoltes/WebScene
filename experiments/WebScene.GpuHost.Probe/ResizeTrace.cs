using System.Text.Json.Serialization;
using WebScene.Backends.Avalonia.Native;

// Typed diagnostics keep the resize workload usable in a reflection-free AOT host.
internal sealed record ResizeSizeSample(long timestamp, double width, double height,
    long requestedAt = 0, string? reason = null);
internal sealed record ResizeTrace(long traceStarted, long inputEnded, long timestampFrequency,
    List<ResizeSizeSample> submittedSizes, List<ResizeSizeSample> nativeWindowResizes,
    List<ResizeSizeSample> surfaceSizeChanges, NativeResizeSubmissionSample[] nativeSubmissions,
    string diagnostics, NativeWebScenePerformanceSnapshot baseline, NativeWebScenePerformanceSnapshot after,
    NativeScenePublicationSample[] publications, NativeSceneRenderSample[] renderedScenes,
    NativeSceneSchedulingSample[] scheduling,
    bool physicalPresentationVerified = false, bool nativeUserDragVerified = false);
internal sealed record PointerMoveTrace(ulong sequence, long submittedAt, int step, double x, double y);
internal sealed record PerformanceTrace(NativeWebScenePerformanceSnapshot baseline,
    NativeWebScenePerformanceSnapshot after, NativeWebSceneWorkDelta delta, double elapsedMilliseconds = 0);
internal sealed record PanTrace(long timestampFrequency, int panInputHz, string panPath,
    bool highResolutionInput, int panCycles, long traceStarted, List<PointerMoveTrace> submittedMoves,
    NativeScenePublicationSample[] publications, NativeSceneRenderSample[] renderedScenes,
    NativeSceneSchedulingSample[] scheduling, long[] drawCallbackCompletions,
    bool physicalPresentationVerified = false);
internal sealed record SidebarTrace(long traceStarted, long timestampFrequency, bool properties,
    double originalWidth, double width, System.Text.Json.JsonElement initialGeometry,
    NativeWebScenePerformanceSnapshot baseline, NativeWebScenePerformanceSnapshot after,
    NativeWebSceneWorkDelta delta, List<PointerMoveTrace> submittedMoves,
    NativeScenePublicationSample[] publications, NativeSceneRenderSample[] renderedScenes,
    NativeSceneSchedulingSample[] scheduling, bool physicalPresentationVerified = false);
internal sealed record DrawTrace(long frequency, long[] timestamps, bool physicalPresentationVerified = false);
[JsonSerializable(typeof(PerformanceTrace))]
[JsonSerializable(typeof(PanTrace))]
[JsonSerializable(typeof(SidebarTrace))]
[JsonSerializable(typeof(DrawTrace))]
[JsonSourceGenerationOptions(IncludeFields = true)]
[JsonSerializable(typeof(ResizeTrace))]
internal partial class ResizeTraceJsonContext : JsonSerializerContext;

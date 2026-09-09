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
[JsonSourceGenerationOptions(IncludeFields = true)]
[JsonSerializable(typeof(ResizeTrace))]
internal partial class ResizeTraceJsonContext : JsonSerializerContext;

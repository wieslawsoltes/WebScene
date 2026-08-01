using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
using JavaScript.Avalonia;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

[StructLayout(LayoutKind.Sequential)]
internal struct InputEvent
{
    public uint Kind;
    public uint Flags;
    public ulong Sequence;
    public double X;
    public double Y;
    public double DeltaX;
    public double DeltaY;
}

internal enum NativePreferredColorScheme : uint
{
    Light = 0,
    Dark = 1
}

internal static class NativeFrameInput
{
    private const uint Frame = 5;

    public static void Submit(IntPtr engine, double timestampMilliseconds)
    {
        if (engine == IntPtr.Zero) return;
        var input = new InputEvent
        {
            Kind = Frame,
            X = timestampMilliseconds
        };
        NativeWebSceneApi.EngineEnqueue(engine, in input);
    }
}
internal struct SceneHeader
{
    public ulong Revision;
    public ulong BaseRevision;
    public ulong ConsumedInputSequence;
    public float ViewportWidth;
    public float ViewportHeight;
    public uint CommandCount;
    public uint CanvasLayerCount;
    public uint DamageRectCount;
    public uint Flags;
    public ulong ContentHash;
}

[StructLayout(LayoutKind.Sequential)]
internal struct SceneCommand
{
    public uint Kind;
    public uint Flags;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint Rgba;
    public uint NodeId;
    public float RadiusTopLeft;
    public float RadiusTopRight;
    public float RadiusBottomRight;
    public float RadiusBottomLeft;
    public float StrokeWidth;
}

[StructLayout(LayoutKind.Sequential)]
public struct CanvasLayout
{
    public uint NodeId;
    public uint Flags;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint BitmapWidth;
    public uint BitmapHeight;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCanvasLayer
{
    public uint NodeId;
    public uint Flags;
    public uint CommandOffset;
    public uint CommandCount;
    public uint StringOffset;
    public uint StringCount;
    public uint Reserved;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint BitmapWidth;
    public uint BitmapHeight;
    public ulong Generation;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCanvasCommand
{
    public uint Kind;
    public uint Flags;
    public uint ResourceId;
    public uint Reserved;
    public double V0;
    public double V1;
    public double V2;
    public double V3;
    public double V4;
    public double V5;
    public double V6;
    public double V7;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneString
{
    public uint ByteOffset;
    public uint ByteLength;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDamageRect
{
    public float X;
    public float Y;
    public float Width;
    public float Height;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSceneView
{
    public uint StructSize;
    public uint AbiVersion;
    public SceneHeader Header;
    public SceneCommand* Commands;
    public NativeCanvasLayer* CanvasLayers;
    public NativeCanvasCommand* CanvasCommands;
    public NativeSceneString* Strings;
    public byte* StringBytes;
    public NativeDamageRect* DamageRects;
    public void* LeaseToken;
    public uint CanvasCommandCount;
    public uint StringCount;
    public uint StringByteCount;
    public uint Reserved;
}

public enum NativeInteropValueKind : uint
{
    Undefined = 0,
    Null = 1,
    Boolean = 2,
    Number = 3,
    String = 4,
    Array = 5,
    Object = 6,
    Handle = 7
}

internal enum NativeInteropResultStatus : uint
{
    Succeeded = 0,
    JavaScriptError = 1,
    Cancelled = 2,
    InvalidRequest = 3
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteropValueData
{
    public NativeInteropValueKind Kind;
    public uint Flags;
    public uint Offset;
    public uint Length;
    public ulong Payload;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteropEdgeData
{
    public uint NameOffset;
    public uint NameLength;
    public uint ValueIndex;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropEvaluateRequest
{
    public uint StructSize;
    public uint Version;
    public byte* Source;
    public nuint SourceLength;
    public byte* DocumentName;
    public nuint DocumentNameLength;
    public uint Flags;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropInvokeRequest
{
    public uint StructSize;
    public uint Version;
    public JavaScriptBinaryOperation Operation;
    public JavaScriptBinaryCallFlags Flags;
    public ulong TargetHandle;
    public byte* GlobalName;
    public nuint GlobalNameLength;
    public byte* MemberName;
    public nuint MemberNameLength;
    public JavaScriptBinaryValueData* Values;
    public nuint ValueCount;
    public JavaScriptBinaryEdgeData* Edges;
    public nuint EdgeCount;
    public byte* Utf8Bytes;
    public nuint Utf8ByteCount;
    public uint ArgumentsRoot;
    public JavaScriptBinaryResultMode ResultMode;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropResultView
{
    public uint StructSize;
    public uint Version;
    public NativeInteropResultStatus Status;
    public uint Flags;
    public ulong OperationId;
    public NativeInteropValueData* Values;
    public NativeInteropEdgeData* Edges;
    public byte* Utf8Bytes;
    public byte* ErrorBytes;
    public ulong LeaseId;
    public uint ValueCount;
    public uint EdgeCount;
    public uint Utf8ByteCount;
    public uint ErrorByteCount;
    public uint RootValueIndex;
    public uint PooledCapacity;
    public uint Reserved0;
    public uint Reserved1;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropCallbackView
{
    public uint StructSize;
    public uint Version;
    public ulong CallId;
    public ulong TargetId;
    public uint MethodId;
    public JavaScriptCallbackReturnKind ReturnKind;
    public JavaScriptBinaryValueData* Values;
    public JavaScriptBinaryEdgeData* Edges;
    public byte* Utf8Bytes;
    public ulong LeaseId;
    public uint ValueCount;
    public uint EdgeCount;
    public uint Utf8ByteCount;
    public uint ArgumentsRoot;
    public uint PooledCapacity;
    public uint Reserved0;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropCallbackCompletion
{
    public uint StructSize;
    public uint Version;
    public ulong CallId;
    public uint Succeeded;
    public uint Reserved;
    public JavaScriptBinaryValueData* Values;
    public nuint ValueCount;
    public JavaScriptBinaryEdgeData* Edges;
    public nuint EdgeCount;
    public byte* Utf8Bytes;
    public nuint Utf8ByteCount;
    public byte* ErrorBytes;
    public nuint ErrorByteCount;
    public uint RootValueIndex;
    public uint Reserved1;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeInteropPoolMetrics
{
    public uint StructSize;
    public uint Version;
    public ulong OutstandingResults;
    public ulong PooledBytes;
    public ulong PoolHits;
    public ulong PoolMisses;
    public ulong OversizeAllocations;
    public ulong HighWaterOutstandingResults;
    public ulong PooledRequestRecords;
    public ulong RequestPoolHits;
    public ulong RequestPoolMisses;
    public ulong RequestOversizeAllocations;
    public ulong ActiveOperationSlots;
    public ulong AvailableOperationSlots;
    public ulong OperationSlotHighWater;
    public ulong PooledResultBytes4K;
    public ulong PooledResultBytes16K;
    public ulong PooledResultBytes64K;
    public ulong PooledResultBytes256K;
    public ulong PooledResultBytes1M;
    public ulong TakenResultLeases;
    public ulong OperationResultLeases;
    public ulong QueuedCallbacks;
    public ulong TakenCallbackLeases;
    public ulong PendingCallbackPromises;
    public ulong CallbackQueueHighWater;
}

[StructLayout(LayoutKind.Sequential)]
public struct EngineMetrics
{
    public ulong EnqueuedInputs;
    public ulong DroppedInputs;
    public ulong ConsumedInputs;
    public ulong PublishedScenes;
    public ulong AcquiredScenes;
    public ulong ExecutedScripts;
    public ulong ScriptErrors;
    public ulong DomNodes;
    public ulong LayoutPasses;
    public ulong IframeNodes;
    public ulong IframeHtmlBytes;
    public ulong FrameScriptsExecuted;
    public ulong FrameScriptErrors;
    public ulong CanvasNodes;
    public ulong ComponentReady;
    public ulong CompilationRequests;
    public ulong CompilationMemoryHits;
    public ulong CompilationPersistentHits;
    public ulong CompilationPersistentMisses;
    public ulong CompilationCacheRejections;
    public ulong CompilationCacheBytesRead;
    public ulong CompilationCacheBytesWritten;
    public ulong CompilationTimeNanoseconds;
    public ulong InputEventsDispatched;
    public ulong InputCallbacksInvoked;
    public ulong BusiestCanvasWidthMilli;
    public ulong BusiestCanvasHeightMilli;
    public ulong CoalescedResizeInputs;
    public ulong AppliedResizeInputs;
    public ulong LastResizeDispatchNanoseconds;
    public ulong LastScenePublicationNanoseconds;
    public ulong LastResizeOuterListenersNanoseconds;
    public ulong LastResizeFrameListenersNanoseconds;
    public ulong LastResizeLayoutNanoseconds;
    public ulong LastResizeObserversNanoseconds;
    public ulong CoalescedPointerMoveInputs;
    public ulong CoalescedWheelInputs;
    public ulong AppliedPointerMoveInputs;
    public ulong AppliedWheelInputs;
    public ulong AppliedAnimationFrames;
    public ulong CoalescedAnimationFrames;
    public ulong LastAnimationAdvanceNanoseconds;
    public ulong LastLayoutNanoseconds;
    public ulong LastSceneBuildNanoseconds;
    public ulong MaximumScenePublicationNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct InputDispatchMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong LastDispatchSequence;
    public ulong DispatchedInputs;
    public ulong TotalDispatchNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct AnimationFrameMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong DispatchedFrames;
    public ulong TotalDispatchNanoseconds;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong LastTimestampMicroseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct SceneFlowMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong PublicationAttempts;
    public ulong BlockedPublications;
    public ulong AcknowledgedScenes;
    public ulong TotalAcknowledgementNanoseconds;
    public ulong LastAcknowledgementNanoseconds;
    public ulong MaximumAcknowledgementNanoseconds;
    public ulong AcknowledgedRevision;
}

[StructLayout(LayoutKind.Sequential)]
public struct ResizeFrameMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong SubmittedPairs;
    public ulong AppliedPairs;
    public ulong PublishedPairs;
    public ulong TotalQueueNanoseconds;
    public ulong LastQueueNanoseconds;
    public ulong MaximumQueueNanoseconds;
    public ulong TotalDispatchNanoseconds;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong AnimationFrameCallbacks;
    public ulong TotalAnimationFrameBatchNanoseconds;
    public ulong LastAnimationFrameBatchNanoseconds;
    public ulong MaximumAnimationFrameBatchNanoseconds;
    public ulong TotalToPublicationNanoseconds;
    public ulong LastToPublicationNanoseconds;
    public ulong MaximumToPublicationNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct ResourceCacheMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong Requests;
    public ulong Hits;
    public ulong Misses;
    public ulong Rejections;
    public ulong BytesRead;
    public ulong BytesWritten;
}

[StructLayout(LayoutKind.Sequential)]
public struct RuntimeWorkMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong TimersScheduled;
    public ulong TimersFired;
    public ulong TimersCancelled;
    public ulong LateTimers;
    public ulong TotalTimerLatenessNanoseconds;
    public ulong AnimationFramesRequested;
    public ulong AnimationFramesInvoked;
    public ulong AnimationFramesCancelled;
    public ulong MicrotaskCheckpoints;
    public ulong WorkerWaits;
    public ulong WorkerSignalledWakes;
    public ulong WorkerTimeoutWakes;
    public ulong SceneBuilds;
    public ulong NoDamageSceneBuilds;
    public ulong FullCheckpointSceneBuilds;
    public ulong ArbitraryEvaluationCalls;
    public ulong GeneratedInvokeCalls;
    public ulong GeneratedCallbackCalls;
    public ulong ArbitraryEvaluationSourceBytes;
    public ulong GeneratedRequestBytes;
}

[StructLayout(LayoutKind.Sequential)]
public struct ProcessCacheMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong CompilationMemoryHits;
    public ulong CompilationLeaders;
    public ulong CompilationWaiters;
    public ulong CompilationSharedBytes;
    public ulong ResourceMemoryHits;
    public ulong ResourceLoadLeaders;
    public ulong ResourceLoadWaiters;
    public ulong ResourceSharedBytes;
    public ulong ScriptSourceMemoryHits;
    public ulong ScriptSourceSharedBytes;
    public ulong SharedIsolateSlot;
    public ulong SharedIsolateActiveContexts;
    public ulong SharedIsolatePeakContexts;
}

[StructLayout(LayoutKind.Sequential)]
public struct EngineMemoryMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong V8TotalHeapBytes;
    public ulong V8UsedHeapBytes;
    public ulong V8ExecutableHeapBytes;
    public ulong V8PhysicalHeapBytes;
    public ulong V8ExternalBytes;
    public ulong V8MallocedBytes;
    public ulong V8PeakMallocedBytes;
    public ulong LatestSceneBytes;
    public ulong ProcessCompilationCacheBytes;
    public ulong ProcessResourceCacheBytes;
    public ulong V8CodeAndMetadataBytes;
    public ulong V8BytecodeAndMetadataBytes;
    public ulong V8ExternalScriptSourceBytes;
    public ulong NativeDomNodeCount;
    public ulong NativeDomNodeSizeBytes;
    public ulong NativeDomInlineBytes;
    public ulong NativeDomPseudoStorageBytes;
    public ulong NativeDomCanvasNodeCount;
    public ulong NativeDomCanvasStorageBytes;
    public ulong NativeDomAnimationCount;
    public ulong NativeDomAnimationStorageBytes;
    public ulong NativeDomCustomPropertyNodeCount;
    public ulong NativeDomCustomPropertyEntryCount;
    public ulong NativeDomCustomPropertyStorageBytes;
    public ulong NativeDomBackgroundImageCount;
    public ulong NativeDomBackgroundImageStorageBytes;
    public ulong NativeDomGridCount;
    public ulong NativeDomGridStorageBytes;
    public ulong NativeDomAuthoredStyleNodeCount;
    public ulong NativeDomAuthoredStyleEntryCount;
    public ulong NativeDomAuthoredStyleStorageBytes;
    public ulong NativeCssRuleCount;
    public ulong NativeCssRuleStorageBytes;
    public ulong NativeCssIndexStorageBytes;
    public ulong ProcessSharedCssRuleCount;
    public ulong ProcessSharedCssRuleStorageBytes;
    public ulong LowMemoryNotifications;
    public ulong NativeDomAttributeNodeCount;
    public ulong NativeDomAttributeEntryCount;
    public ulong NativeDomAttributeStorageBytes;
    public ulong NativeWrapperHandleCount;
    public ulong NativeWrapperStorageBytes;
    public ulong NativeTextMeasurementCacheEntryCount;
    public ulong NativeTextMeasurementCacheStorageBytes;
    public ulong ProcessCompilationMappedCacheBytes;
    public ulong ProcessResourceMappedCacheBytes;
    public ulong NativeDomTextualStyleCount;
    public ulong NativeDomTextualStyleStorageBytes;
    public ulong NativeDomNodePoolReservedBytes;
    public ulong NativeDomNodePoolPeakBytes;
    public ulong NativeDomTableLayoutCount;
    public ulong NativeDomTableLayoutStorageBytes;
    public ulong NativeDomFormControlCount;
    public ulong NativeDomFormControlStorageBytes;
    public ulong HiddenLowMemoryNotifications;
    public ulong NativeEventListenerCount;
    public ulong NativeEventListenerStorageBytes;
    public ulong V8YoungSpaceUsedBytes;
    public ulong V8YoungSpacePhysicalBytes;
    public ulong V8OldSpaceUsedBytes;
    public ulong V8OldSpacePhysicalBytes;
    public ulong V8CodeSpaceUsedBytes;
    public ulong V8CodeSpacePhysicalBytes;
    public ulong V8MapSpaceUsedBytes;
    public ulong V8MapSpacePhysicalBytes;
    public ulong V8LargeObjectSpaceUsedBytes;
    public ulong V8LargeObjectSpacePhysicalBytes;
    public ulong V8ReadOnlySpaceUsedBytes;
    public ulong V8ReadOnlySpacePhysicalBytes;
    public ulong V8SharedSpaceUsedBytes;
    public ulong V8SharedSpacePhysicalBytes;
    public ulong V8TrustedSpaceUsedBytes;
    public ulong V8TrustedSpacePhysicalBytes;
    public ulong PendingSceneCount;
    public ulong PendingSceneBytes;
}

[StructLayout(LayoutKind.Sequential)]
internal struct EngineOptions
{
    public uint StructSize;
    public uint SimulatedChartCommandCount;
    public IntPtr CompilationCacheDirectory;
    public nuint CompilationCacheDirectoryLength;
    public IntPtr ResourceLoadCallback;
    public IntPtr ResourceLoadUserData;
    public IntPtr ScenePublishedCallback;
    public IntPtr ScenePublishedUserData;
    public IntPtr TextMeasureCallback;
    public IntPtr TextMeasureUserData;
    public IntPtr HostRequestAvailableCallback;
    public IntPtr HostRequestAvailableUserData;
    public IntPtr InteropCallbackAvailableCallback;
    public IntPtr InteropCallbackAvailableUserData;
    public IntPtr AnimationFrameRequestedCallback;
    public IntPtr AnimationFrameRequestedUserData;
}

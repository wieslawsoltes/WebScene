using System.Buffers;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks.Sources;
using Microsoft.Win32.SafeHandles;
using WebScene.JavaScript.Interop;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed unsafe class NativeInteropResultSafeHandle
    : SafeHandleZeroOrMinusOneIsInvalid
{
    private readonly ulong _leaseId;

    internal NativeInteropResultSafeHandle(IntPtr value)
        : base(ownsHandle: true)
    {
        _leaseId = ((NativeInteropResultView*)value)->LeaseId;
        SetHandle(value);
    }

    protected override bool ReleaseHandle()
    {
        NativeWebSceneApi.InteropResultReleaseV3(handle, _leaseId);
        return true;
    }

    internal static void ReleaseRaw(IntPtr result)
    {
        if (result == IntPtr.Zero) return;
        var view = (NativeInteropResultView*)result;
        NativeWebSceneApi.InteropResultReleaseV3(result, view->LeaseId);
    }
}

internal sealed unsafe class NativeInteropCallbackSafeHandle
    : SafeHandleZeroOrMinusOneIsInvalid
{
    private readonly ulong _leaseId;

    internal NativeInteropCallbackSafeHandle(IntPtr value)
        : base(ownsHandle: true)
    {
        _leaseId = ((NativeInteropCallbackView*)value)->LeaseId;
        SetHandle(value);
    }

    protected override bool ReleaseHandle()
    {
        NativeWebSceneApi.InteropCallbackReleaseV3(handle, _leaseId);
        return true;
    }
}

internal sealed class NativeCallbackCompletionOwner : IDisposable
{
    private readonly object _gate = new();
    private IntPtr _engine;

    internal NativeCallbackCompletionOwner(IntPtr engine)
    {
        if (engine == IntPtr.Zero)
        {
            throw new ArgumentException(
                "A native engine is required.",
                nameof(engine));
        }
        _engine = engine;
    }

    internal bool TryEnter(out IntPtr engine)
    {
        Monitor.Enter(_gate);
        engine = _engine;
        if (engine != IntPtr.Zero)
        {
            return true;
        }
        Monitor.Exit(_gate);
        return false;
    }

    internal void Exit() => Monitor.Exit(_gate);

    public void Dispose()
    {
        lock (_gate)
        {
            _engine = IntPtr.Zero;
        }
    }
}

internal sealed unsafe class NativeJavaScriptBinaryCallbackLease
    : JavaScriptBinaryCallbackLease
{
    private NativeInteropCallbackSafeHandle? _handle;
    private readonly NativeCallbackCompletionOwner _completionOwner;
    private readonly ulong _callId;
    private readonly ulong _targetId;
    private readonly uint _methodId;
    private readonly JavaScriptCallbackReturnKind _returnKind;
    private int _disposed;

    internal NativeJavaScriptBinaryCallbackLease(
        NativeCallbackCompletionOwner completionOwner,
        IntPtr callback)
    {
        ArgumentNullException.ThrowIfNull(completionOwner);
        if (callback == IntPtr.Zero)
        {
            throw new ArgumentException(
                "A native callback lease is required.",
                nameof(callback));
        }
        var view = (NativeInteropCallbackView*)callback;
        ValidateHeader(view);
        _completionOwner = completionOwner;
        _callId = view->CallId;
        _targetId = view->TargetId;
        _methodId = view->MethodId;
        _returnKind = view->ReturnKind;
        _handle = new NativeInteropCallbackSafeHandle(callback);
    }

    ~NativeJavaScriptBinaryCallbackLease()
    {
        if (Volatile.Read(ref _disposed) == 0)
        {
            Interlocked.Increment(ref s_finalizerRecoveredLeaseCount);
        }
        DisposeCore();
    }

    private static long s_finalizerRecoveredLeaseCount;

    internal static long FinalizerRecoveredLeaseCount
        => Interlocked.Read(ref s_finalizerRecoveredLeaseCount);

    public override ulong CallId => _callId;

    public override ulong TargetId => _targetId;

    public override uint MethodId => _methodId;

    public override JavaScriptCallbackReturnKind ReturnKind => _returnKind;

    public override JavaScriptBinaryCallbackCompletion CreateCompletion()
    {
        ObjectDisposedException.ThrowIf(
            Volatile.Read(ref _disposed) != 0,
            this);
        return new NativeJavaScriptBinaryCallbackCompletion(
            _completionOwner,
            _callId,
            _returnKind);
    }

    protected override JavaScriptBinaryValue AcquireBorrow(
        out object? borrowToken)
    {
        borrowToken = null;
        ObjectDisposedException.ThrowIf(
            Volatile.Read(ref _disposed) != 0,
            this);
        var handle = Volatile.Read(ref _handle);
        ObjectDisposedException.ThrowIf(handle is null, this);
        var added = false;
        try
        {
            handle.DangerousAddRef(ref added);
            var view = (NativeInteropCallbackView*)
                handle.DangerousGetHandle();
            ValidateHeader(view);
            borrowToken = handle;
            return new JavaScriptBinaryValue(
                view->Values,
                view->ValueCount,
                view->Edges,
                view->EdgeCount,
                view->Utf8Bytes,
                view->Utf8ByteCount,
                view->ArgumentsRoot);
        }
        catch
        {
            if (added)
            {
                handle.DangerousRelease();
            }
            throw;
        }
    }

    protected override void ReleaseBorrow(object? borrowToken)
    {
        if (borrowToken is not NativeInteropCallbackSafeHandle handle)
        {
            throw new InvalidOperationException(
                "The native callback borrow token is invalid.");
        }
        handle.DangerousRelease();
    }

    public override void Dispose()
    {
        DisposeCore();
        GC.SuppressFinalize(this);
    }

    private void DisposeCore()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        Interlocked.Exchange(ref _handle, null)?.Dispose();
    }

    private static void ValidateHeader(NativeInteropCallbackView* view)
    {
        if (view == null
            || view->StructSize < (uint)sizeof(NativeInteropCallbackView)
            || view->Version != 3
            || view->CallId == 0
            || view->TargetId == 0
            || view->ReturnKind
                > JavaScriptCallbackReturnKind.Synchronous
            || view->ArgumentsRoot >= view->ValueCount
            || (view->ValueCount != 0 && view->Values == null)
            || (view->EdgeCount != 0 && view->Edges == null)
            || (view->Utf8ByteCount != 0 && view->Utf8Bytes == null))
        {
            throw new InvalidDataException(
                "The native callback lease header is invalid.");
        }
    }
}

internal sealed unsafe class NativeJavaScriptBinaryCallbackCompletion(
    NativeCallbackCompletionOwner completionOwner,
    ulong callId,
    JavaScriptCallbackReturnKind returnKind)
    : JavaScriptBinaryCallbackCompletion(returnKind)
{
    protected override void CompleteSuccess(
        ReadOnlySpan<JavaScriptBinaryValueData> values,
        ReadOnlySpan<JavaScriptBinaryEdgeData> edges,
        ReadOnlySpan<byte> utf8,
        uint rootValueIndex)
    {
        if (!completionOwner.TryEnter(out var engine))
        {
            return;
        }
        try
        {
            fixed (JavaScriptBinaryValueData* valuePointer = values)
            fixed (JavaScriptBinaryEdgeData* edgePointer = edges)
            fixed (byte* utf8Pointer = utf8)
            {
                var completion = new NativeInteropCallbackCompletion
                {
                    StructSize =
                        (uint)sizeof(NativeInteropCallbackCompletion),
                    Version = 3,
                    CallId = callId,
                    Succeeded = 1,
                    Values = valuePointer,
                    ValueCount = (nuint)values.Length,
                    Edges = edgePointer,
                    EdgeCount = (nuint)edges.Length,
                    Utf8Bytes = utf8Pointer,
                    Utf8ByteCount = (nuint)utf8.Length,
                    RootValueIndex = rootValueIndex
                };
                if (NativeWebSceneApi.EngineCompleteCallbackV3(
                        engine,
                        in completion) == 0)
                {
                    throw new InvalidOperationException(
                        "Native callback completion was rejected: "
                        + NativeWebSceneApi.GetLastError(engine));
                }
            }
        }
        finally
        {
            completionOwner.Exit();
        }
    }

    protected override void CompleteFailure(string error)
    {
        if (!completionOwner.TryEnter(out var engine))
        {
            return;
        }
        try
        {
            var byteCount = Encoding.UTF8.GetByteCount(error);
            var bytes = ArrayPool<byte>.Shared.Rent(Math.Max(1, byteCount));
            try
            {
                byteCount = Encoding.UTF8.GetBytes(error, bytes);
                fixed (byte* errorPointer = bytes)
                {
                    var completion = new NativeInteropCallbackCompletion
                    {
                        StructSize =
                            (uint)sizeof(NativeInteropCallbackCompletion),
                        Version = 3,
                        CallId = callId,
                        ErrorBytes = errorPointer,
                        ErrorByteCount = (nuint)byteCount
                    };
                    if (NativeWebSceneApi.EngineCompleteCallbackV3(
                            engine,
                            in completion) == 0)
                    {
                        throw new InvalidOperationException(
                            "Native callback rejection was rejected: "
                            + NativeWebSceneApi.GetLastError(engine));
                    }
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(bytes);
            }
        }
        finally
        {
            completionOwner.Exit();
        }
    }
}

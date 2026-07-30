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

/// <summary>
/// Implemented by generated codecs that materialize a normal managed result
/// directly from a tagged native value.
/// </summary>
public interface INativeInteropValueDecoder<T>
{
    T Decode(NativeInteropValue value);
}

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

internal sealed unsafe class NativeJavaScriptBinaryResultLease
    : JavaScriptBinaryResultLease
{
    private NativeInteropResultSafeHandle? _handle;
    private int _disposed;

    internal NativeJavaScriptBinaryResultLease(IntPtr result)
    {
        if (result == IntPtr.Zero)
        {
            throw new ArgumentException(
                "A native interop result is required.",
                nameof(result));
        }
        _handle = new NativeInteropResultSafeHandle(result);
    }

    ~NativeJavaScriptBinaryResultLease()
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
            var root = NativeInteropBorrowScope.GetBinaryRoot(
                (NativeInteropResultView*)handle.DangerousGetHandle());
            borrowToken = handle;
            return root;
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
        if (borrowToken is not NativeInteropResultSafeHandle handle)
        {
            throw new InvalidOperationException(
                "The native binary borrow token is invalid.");
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
}

/// <summary>
/// Owns one immutable native JavaScript result until it is disposed. Borrowed
/// readers and all spans obtained from them are valid only within a
/// <see cref="Borrow"/> scope.
/// </summary>
public sealed unsafe class NativeInteropResultLease : IDisposable
{
    private NativeInteropResultSafeHandle? _handle;
    private int _disposed;

    internal NativeInteropResultLease(IntPtr result)
    {
        if (result == IntPtr.Zero)
        {
            throw new ArgumentException("A native interop result is required.", nameof(result));
        }
        _handle = new NativeInteropResultSafeHandle(result);
    }

    ~NativeInteropResultLease()
    {
        if (Volatile.Read(ref _disposed) == 0)
        {
            Interlocked.Increment(ref s_finalizerRecoveredLeaseCount);
        }
        Dispose(disposing: false);
    }

    private static long s_finalizerRecoveredLeaseCount;

    public static long FinalizerRecoveredLeaseCount
        => Interlocked.Read(ref s_finalizerRecoveredLeaseCount)
           + NativeJavaScriptBinaryResultLease.FinalizerRecoveredLeaseCount;

    public ulong OperationId
    {
        get
        {
            using var borrowed = Borrow();
            return borrowed.OperationId;
        }
    }

    public uint PooledCapacity
    {
        get
        {
            using var borrowed = Borrow();
            return borrowed.PooledCapacity;
        }
    }

    public NativeInteropBorrowScope Borrow()
    {
        ObjectDisposedException.ThrowIf(
            Volatile.Read(ref _disposed) != 0,
            this);
        var handle = Volatile.Read(ref _handle);
        ObjectDisposedException.ThrowIf(handle is null, this);
        var added = false;
        try
        {
            handle.DangerousAddRef(ref added);
            var view = (NativeInteropResultView*)handle.DangerousGetHandle();
            NativeInteropBorrowScope.ValidateHeader(view);
            return new NativeInteropBorrowScope(handle, view, added);
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

    /// <summary>
    /// Materializes this tagged result as JSON-compatible text for diagnostics
    /// and compatibility tooling. Normal generated APIs decode the tagged
    /// value directly and do not call this method.
    /// </summary>
    public string ToJsonText()
    {
        using var scope = Borrow();
        return NativeInteropJsonText.Serialize(scope.Root);
    }

    internal string GetError()
    {
        using var scope = Borrow();
        return scope.GetError();
    }

    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        Interlocked.Exchange(ref _handle, null)?.Dispose();
    }
}

/// <summary>
/// Scoped projection of a leased native result. Disposing the scope releases
/// its SafeHandle reference; it does not release the owning result lease.
/// </summary>
public unsafe ref struct NativeInteropBorrowScope
{
    private NativeInteropResultSafeHandle? _handle;
    private readonly NativeInteropResultView* _view;
    private bool _addedReference;

    internal NativeInteropBorrowScope(
        NativeInteropResultSafeHandle handle,
        NativeInteropResultView* view,
        bool addedReference)
    {
        _handle = handle;
        _view = view;
        _addedReference = addedReference;
    }

    public NativeInteropValue Root
    {
        get
        {
            ValidateHeader(_view);
            if (_view->Status != NativeInteropResultStatus.Succeeded)
            {
                throw new InvalidOperationException(GetError());
            }
            return new NativeInteropValue(_view, _view->RootValueIndex);
        }
    }

    internal JavaScriptBinaryValue BinaryRoot
    {
        get
        {
            return GetBinaryRoot(_view);
        }
    }

    internal static JavaScriptBinaryValue GetBinaryRoot(
        NativeInteropResultView* view)
    {
        ValidateHeader(view);
        if (view->Status != NativeInteropResultStatus.Succeeded)
        {
            throw new InvalidOperationException(
                "The native interop operation did not succeed.");
        }
        return new JavaScriptBinaryValue(
            (JavaScriptBinaryValueData*)view->Values,
            view->ValueCount,
            (JavaScriptBinaryEdgeData*)view->Edges,
            view->EdgeCount,
            view->Utf8Bytes,
            view->Utf8ByteCount,
            view->RootValueIndex);
    }

    public ulong OperationId
    {
        get
        {
            ValidateHeader(_view);
            return _view->OperationId;
        }
    }

    public uint PooledCapacity
    {
        get
        {
            ValidateHeader(_view);
            return _view->PooledCapacity;
        }
    }

    public string GetError()
    {
        ValidateHeader(_view);
        if (_view->ErrorByteCount == 0)
        {
            return _view->Status.ToString();
        }
        if (_view->ErrorBytes == null)
        {
            throw new InvalidDataException(
                "The native interop result has an invalid error buffer.");
        }
        return System.Text.Encoding.UTF8.GetString(
            new ReadOnlySpan<byte>(
                _view->ErrorBytes,
                checked((int)_view->ErrorByteCount)));
    }

    public void Dispose()
    {
        if (!_addedReference) return;
        _addedReference = false;
        _handle!.DangerousRelease();
        _handle = null;
    }

    internal static void ValidateHeader(NativeInteropResultView* view)
    {
        if (view == null
            || view->StructSize < (uint)sizeof(NativeInteropResultView)
            || view->Version != 3
            || (view->Status == NativeInteropResultStatus.Succeeded
                && view->RootValueIndex >= view->ValueCount)
            || (view->ValueCount != 0 && view->Values == null)
            || (view->EdgeCount != 0 && view->Edges == null)
            || (view->Utf8ByteCount != 0 && view->Utf8Bytes == null))
        {
            throw new InvalidDataException(
                "The native interop result header is invalid.");
        }
    }
}

/// <summary>
/// Stack-only view of one tagged JavaScript value in a borrowed native result.
/// </summary>
public readonly unsafe ref struct NativeInteropValue
{
    private readonly NativeInteropResultView* _view;
    private readonly uint _index;

    internal NativeInteropValue(NativeInteropResultView* view, uint index)
    {
        _view = view;
        _index = index;
        _ = Data;
    }

    private ref readonly NativeInteropValueData Data
    {
        get
        {
            if (_view == null
                || _index >= _view->ValueCount
                || _view->Values == null)
            {
                throw new InvalidDataException(
                    "The native interop value index is invalid.");
            }
            return ref _view->Values[_index];
        }
    }

    public NativeInteropValueKind Kind => Data.Kind;

    public int Count
    {
        get
        {
            ref readonly var data = ref Data;
            if (data.Kind is not (
                    NativeInteropValueKind.Array
                    or NativeInteropValueKind.Object))
            {
                throw new InvalidOperationException(
                    "Only arrays and objects have child values.");
            }
            ValidateEdges(data);
            return checked((int)data.Length);
        }
    }

    public bool GetBoolean()
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Boolean);
        return data.Payload != 0;
    }

    public double GetNumber()
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Number);
        return BitConverter.Int64BitsToDouble(unchecked((long)data.Payload));
    }

    public long GetHandle()
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Handle);
        return unchecked((long)data.Payload);
    }

    public ReadOnlySpan<byte> Utf8
    {
        get
        {
            ref readonly var data = ref Require(NativeInteropValueKind.String);
            ValidateUtf8(data.Offset, data.Length);
            return new ReadOnlySpan<byte>(
                _view->Utf8Bytes + data.Offset,
                checked((int)data.Length));
        }
    }

    public string GetString() => System.Text.Encoding.UTF8.GetString(Utf8);

    public NativeInteropValue GetArrayItem(int index)
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Array);
        var edge = GetEdge(data, index);
        return new NativeInteropValue(_view, edge.ValueIndex);
    }

    public ReadOnlySpan<byte> GetObjectPropertyNameUtf8(int index)
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Object);
        var edge = GetEdge(data, index);
        ValidateUtf8(edge.NameOffset, edge.NameLength);
        return new ReadOnlySpan<byte>(
            _view->Utf8Bytes + edge.NameOffset,
            checked((int)edge.NameLength));
    }

    public string GetObjectPropertyName(int index)
        => System.Text.Encoding.UTF8.GetString(GetObjectPropertyNameUtf8(index));

    public NativeInteropValue GetObjectPropertyValue(int index)
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Object);
        var edge = GetEdge(data, index);
        return new NativeInteropValue(_view, edge.ValueIndex);
    }

    public bool TryGetProperty(
        ReadOnlySpan<byte> utf8Name,
        out NativeInteropValue value)
    {
        ref readonly var data = ref Require(NativeInteropValueKind.Object);
        for (var index = 0; index < data.Length; index++)
        {
            var edge = GetEdge(data, checked((int)index));
            ValidateUtf8(edge.NameOffset, edge.NameLength);
            var name = new ReadOnlySpan<byte>(
                _view->Utf8Bytes + edge.NameOffset,
                checked((int)edge.NameLength));
            if (!name.SequenceEqual(utf8Name)) continue;
            value = new NativeInteropValue(_view, edge.ValueIndex);
            return true;
        }
        value = default;
        return false;
    }

    private ref readonly NativeInteropValueData Require(
        NativeInteropValueKind kind)
    {
        ref readonly var data = ref Data;
        if (data.Kind != kind)
        {
            throw new InvalidOperationException(
                $"Expected a native {kind} value but received {data.Kind}.");
        }
        return ref data;
    }

    private NativeInteropEdgeData GetEdge(
        in NativeInteropValueData data,
        int index)
    {
        ValidateEdges(data);
        if ((uint)index >= data.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        var edge = _view->Edges[data.Offset + (uint)index];
        if (edge.ValueIndex >= _view->ValueCount)
        {
            throw new InvalidDataException(
                "A native interop edge has an invalid value index.");
        }
        return edge;
    }

    private void ValidateEdges(in NativeInteropValueData data)
    {
        if (data.Offset > _view->EdgeCount
            || data.Length > _view->EdgeCount - data.Offset
            || (data.Length != 0 && _view->Edges == null))
        {
            throw new InvalidDataException(
                "The native interop edge range is invalid.");
        }
    }

    private void ValidateUtf8(uint offset, uint length)
    {
        if (offset > _view->Utf8ByteCount
            || length > _view->Utf8ByteCount - offset
            || (length != 0 && _view->Utf8Bytes == null))
        {
            throw new InvalidDataException(
                "The native interop UTF-8 range is invalid.");
        }
    }
}

internal static class NativeInteropJsonText
{
    internal static string Serialize(NativeInteropValue value)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            WriteValue(writer, value);
        }
        return Encoding.UTF8.GetString(buffer.WrittenSpan);
    }

    private static void WriteValue(
        Utf8JsonWriter writer,
        NativeInteropValue value)
    {
        switch (value.Kind)
        {
            case NativeInteropValueKind.Undefined:
            case NativeInteropValueKind.Null:
                writer.WriteNullValue();
                break;
            case NativeInteropValueKind.Boolean:
                writer.WriteBooleanValue(value.GetBoolean());
                break;
            case NativeInteropValueKind.Number:
                var number = value.GetNumber();
                if (double.IsFinite(number))
                {
                    writer.WriteNumberValue(number);
                }
                else
                {
                    writer.WriteNullValue();
                }
                break;
            case NativeInteropValueKind.String:
                writer.WriteStringValue(value.Utf8);
                break;
            case NativeInteropValueKind.Handle:
                writer.WriteStartObject();
                writer.WriteNumber("__webSceneHandle", value.GetHandle());
                writer.WriteEndObject();
                break;
            case NativeInteropValueKind.Array:
                writer.WriteStartArray();
                for (var index = 0; index < value.Count; index++)
                {
                    WriteValue(writer, value.GetArrayItem(index));
                }
                writer.WriteEndArray();
                break;
            case NativeInteropValueKind.Object:
                writer.WriteStartObject();
                for (var index = 0; index < value.Count; index++)
                {
                    writer.WritePropertyName(
                        value.GetObjectPropertyNameUtf8(index));
                    WriteValue(writer, value.GetObjectPropertyValue(index));
                }
                writer.WriteEndObject();
                break;
            default:
                throw new InvalidDataException(
                    $"Unsupported native interop value kind {value.Kind}.");
        }
    }
}

/// <summary>
/// Direct generated-call transport for an existing native WebScene engine.
/// Callers must dispose it before destroying the engine.
/// </summary>
public sealed class NativeJavaScriptBinaryTransport
    : IJavaScriptBinaryTransport
{
    private readonly NativeInteropInvoker _operations;

    public NativeJavaScriptBinaryTransport(IntPtr engine)
    {
        _operations = new NativeInteropInvoker(engine);
    }

    public NativeInteropPoolMetrics PoolMetrics => _operations.PoolMetrics;

    public ValueTask<TResult> InvokeAsync<TArguments, TResult, TCodec>(
        IJavaScriptInvoker invoker,
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>
        => BinaryDecodeSource<TArguments, TResult, TCodec>.Start(
            BeginInvoke<TArguments, TResult, TCodec>(
                callSite,
                target,
                arguments,
                cancellationToken),
            invoker);

    public ValueTask InvokeVoidAsync<TArguments, TCodec>(
        IJavaScriptInvoker invoker,
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        => BinaryVoidDecodeSource<TArguments, TCodec>.Start(
            BeginInvoke<
                TArguments,
                JavaScriptBinaryVoid,
                TCodec>(
                callSite,
                target,
                arguments,
                cancellationToken),
            invoker);

    public ValueTask<JavaScriptBinaryResultLease> InvokeBorrowedAsync<
        TArguments,
        TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments>
        => NativeInteropBorrowedLeaseAdapter.WrapAsync(
            BeginInvoke<TArguments, JavaScriptBinaryVoid, TCodec>(
                callSite,
                target,
                arguments,
                cancellationToken));

    private unsafe ValueTask<IntPtr> BeginInvoke<
        TArguments,
        TResult,
        TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken)
        where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments>
    {
        ArgumentNullException.ThrowIfNull(callSite);
        var writer = new JavaScriptBinaryWriter();
        try
        {
            var root = TCodec.EncodeArguments(ref writer, in arguments);
            var values = writer.Values;
            if (root >= values.Length
                || values[checked((int)root)].Kind
                    != JavaScriptBinaryValueKind.Array)
            {
                throw new InvalidDataException(
                    "A generated binary codec must return an argument-array root.");
            }
            var edges = writer.Edges;
            var utf8 = writer.Utf8;
            var globalName = callSite.GlobalNameUtf8.AsSpan();
            var memberName = callSite.MemberNameUtf8.AsSpan();
            ValueTask<IntPtr> pending;
            fixed (JavaScriptBinaryValueData* valuePointer = values)
            fixed (JavaScriptBinaryEdgeData* edgePointer = edges)
            fixed (byte* utf8Pointer = utf8)
            fixed (byte* globalNamePointer = globalName)
            fixed (byte* memberNamePointer = memberName)
            {
                var request = new NativeInteropInvokeRequest
                {
                    StructSize =
                        (uint)sizeof(NativeInteropInvokeRequest),
                    Version = 3,
                    Operation = callSite.Operation,
                    Flags = callSite.Flags,
                    TargetHandle = target.IsEmpty
                        ? 0U
                        : unchecked((ulong)target.Id),
                    GlobalName = globalNamePointer,
                    GlobalNameLength = (nuint)globalName.Length,
                    MemberName = memberNamePointer,
                    MemberNameLength = (nuint)memberName.Length,
                    Values = valuePointer,
                    ValueCount = (nuint)values.Length,
                    Edges = edgePointer,
                    EdgeCount = (nuint)edges.Length,
                    Utf8Bytes = utf8Pointer,
                    Utf8ByteCount = (nuint)utf8.Length,
                    ArgumentsRoot = root,
                    ResultMode = callSite.ResultMode
                };
                pending = _operations.InvokeGeneratedAsync(
                    in request,
                    cancellationToken);
            }
            return pending;
        }
        finally
        {
            writer.Dispose();
        }
    }

    public void Dispose() => _operations.Dispose();

    private sealed class BinaryDecodeSource<TArguments, TResult, TCodec>
        : IValueTaskSource<TResult>
        where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>
    {
        private static readonly System.Collections.Concurrent.ConcurrentStack<
            BinaryDecodeSource<TArguments, TResult, TCodec>> s_pool = new();

        private ManualResetValueTaskSourceCore<TResult> _core;
        private readonly Action _complete;
        private ValueTask<IntPtr> _pending;
        private IJavaScriptInvoker? _invoker;

        private BinaryDecodeSource()
        {
            _core.RunContinuationsAsynchronously = true;
            _complete = Complete;
        }

        internal static ValueTask<TResult> Start(
            ValueTask<IntPtr> pending,
            IJavaScriptInvoker invoker)
        {
            ArgumentNullException.ThrowIfNull(invoker);
            if (pending.IsCompletedSuccessfully)
            {
                var result = pending.Result;
                try
                {
                    unsafe
                    {
                        var root = NativeInteropBorrowScope.GetBinaryRoot(
                            (NativeInteropResultView*)result);
                        return new ValueTask<TResult>(
                            TCodec.DecodeResult(root, invoker));
                    }
                }
                finally
                {
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                }
            }

            if (!s_pool.TryPop(out var source))
            {
                source = new BinaryDecodeSource<
                    TArguments,
                    TResult,
                    TCodec>();
            }
            source._core.Reset();
            source._pending = pending;
            source._invoker = invoker;
            pending.GetAwaiter().UnsafeOnCompleted(source._complete);
            return new ValueTask<TResult>(source, source._core.Version);
        }

        public TResult GetResult(short token)
        {
            try
            {
                return _core.GetResult(token);
            }
            finally
            {
                _pending = default;
                _invoker = null;
                s_pool.Push(this);
            }
        }

        public ValueTaskSourceStatus GetStatus(short token)
            => _core.GetStatus(token);

        public void OnCompleted(
            Action<object?> continuation,
            object? state,
            short token,
            ValueTaskSourceOnCompletedFlags flags)
            => _core.OnCompleted(continuation, state, token, flags);

        private void Complete()
        {
            try
            {
                var result = _pending.GetAwaiter().GetResult();
                try
                {
                    unsafe
                    {
                        var root = NativeInteropBorrowScope.GetBinaryRoot(
                            (NativeInteropResultView*)result);
                        _core.SetResult(TCodec.DecodeResult(
                            root,
                            _invoker!));
                    }
                }
                finally
                {
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                }
            }
            catch (Exception error)
            {
                _core.SetException(error);
            }
        }
    }

    private sealed class BinaryVoidDecodeSource<TArguments, TCodec>
        : IValueTaskSource
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
    {
        private static readonly System.Collections.Concurrent.ConcurrentStack<
            BinaryVoidDecodeSource<TArguments, TCodec>> s_pool = new();

        private ManualResetValueTaskSourceCore<bool> _core;
        private readonly Action _complete;
        private ValueTask<IntPtr> _pending;
        private IJavaScriptInvoker? _invoker;

        private BinaryVoidDecodeSource()
        {
            _core.RunContinuationsAsynchronously = true;
            _complete = Complete;
        }

        internal static ValueTask Start(
            ValueTask<IntPtr> pending,
            IJavaScriptInvoker invoker)
        {
            ArgumentNullException.ThrowIfNull(invoker);
            if (pending.IsCompletedSuccessfully)
            {
                var result = pending.Result;
                try
                {
                    unsafe
                    {
                        var root = NativeInteropBorrowScope.GetBinaryRoot(
                            (NativeInteropResultView*)result);
                        _ = TCodec.DecodeResult(root, invoker);
                    }
                }
                finally
                {
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                }
                return ValueTask.CompletedTask;
            }

            if (!s_pool.TryPop(out var source))
            {
                source = new BinaryVoidDecodeSource<TArguments, TCodec>();
            }
            source._core.Reset();
            source._pending = pending;
            source._invoker = invoker;
            pending.GetAwaiter().UnsafeOnCompleted(source._complete);
            return new ValueTask(source, source._core.Version);
        }

        public void GetResult(short token)
        {
            try
            {
                _core.GetResult(token);
            }
            finally
            {
                _pending = default;
                _invoker = null;
                s_pool.Push(this);
            }
        }

        public ValueTaskSourceStatus GetStatus(short token)
            => _core.GetStatus(token);

        public void OnCompleted(
            Action<object?> continuation,
            object? state,
            short token,
            ValueTaskSourceOnCompletedFlags flags)
            => _core.OnCompleted(continuation, state, token, flags);

        private void Complete()
        {
            try
            {
                var result = _pending.GetAwaiter().GetResult();
                try
                {
                    unsafe
                    {
                        var root = NativeInteropBorrowScope.GetBinaryRoot(
                            (NativeInteropResultView*)result);
                        _ = TCodec.DecodeResult(root, _invoker!);
                    }
                }
                finally
                {
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                }
                _core.SetResult(true);
            }
            catch (Exception error)
            {
                _core.SetException(error);
            }
        }
    }
}

/// <summary>
/// Owns the pooled asynchronous-operation bridge for an existing native
/// WebScene engine. The engine owner must dispose this invoker before
/// destroying the engine; result leases already returned to callers may
/// outlive both.
/// </summary>
internal static class NativeInteropLeaseAdapter
{
    internal static async ValueTask<NativeInteropResultLease> WrapAsync(
        ValueTask<IntPtr> pending)
        => new(await pending.ConfigureAwait(false));
}

internal static class NativeInteropBorrowedLeaseAdapter
{
    internal static ValueTask<JavaScriptBinaryResultLease> WrapAsync(
        ValueTask<IntPtr> pending)
    {
        if (pending.IsCompletedSuccessfully)
        {
            return new ValueTask<JavaScriptBinaryResultLease>(
                new NativeJavaScriptBinaryResultLease(pending.Result));
        }
        return Awaited(pending);
    }

    private static async ValueTask<JavaScriptBinaryResultLease> Awaited(
        ValueTask<IntPtr> pending)
        => new NativeJavaScriptBinaryResultLease(
            await pending.ConfigureAwait(false));
}

public sealed unsafe class NativeInteropInvoker : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void CompletedCallback(IntPtr userData, ulong operationId);

    private static readonly CompletedCallback s_completed = Complete;
    private static readonly IntPtr s_completedAddress =
        Marshal.GetFunctionPointerForDelegate(s_completed);
    private static readonly ConcurrentDictionary<IntPtr, NativeInteropInvoker>
        s_completionBridges = new();
    private static long s_nextCompletionBridge;

    private readonly object _gate = new();
    private readonly Stack<OperationSlot> _available = [];
    private readonly HashSet<OperationSlot> _active = [];
    private readonly List<OperationSlot> _all = [];
    private readonly Dictionary<ulong, OperationSlot> _operations = [];
    private readonly HashSet<ulong> _earlyCompletions = [];
    private readonly IntPtr _engine;
    private readonly IntPtr _callbackData;
    private int _beginsInFlight;
    private bool _disposed;

    public NativeInteropInvoker(IntPtr engine)
    {
        if (engine == IntPtr.Zero)
        {
            throw new ArgumentException("A native engine is required.", nameof(engine));
        }
        _engine = engine;
        var bridge = Interlocked.Increment(ref s_nextCompletionBridge);
        if (bridge == 0)
        {
            bridge = Interlocked.Increment(ref s_nextCompletionBridge);
        }
        _callbackData = new IntPtr(bridge);
        if (!s_completionBridges.TryAdd(_callbackData, this))
        {
            throw new InvalidOperationException(
                "The native interop completion bridge could not be registered.");
        }
    }

    public NativeInteropPoolMetrics PoolMetrics
        => NativeWebSceneApi.GetInteropPoolMetrics(_engine);

    public ValueTask<NativeInteropResultLease> InvokeAsync(
        string source,
        string documentName,
        CancellationToken cancellationToken)
        => NativeInteropLeaseAdapter.WrapAsync(InvokeRawAsync(
                source,
                documentName,
                cancellationToken));

    private ValueTask<IntPtr> InvokeRawAsync(
        string source,
        string documentName,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(documentName);

        var slot = RentSlot(cancellationToken);
        BeginBindingWindow();
        var sourceLength = System.Text.Encoding.UTF8.GetByteCount(source);
        var nameLength = System.Text.Encoding.UTF8.GetByteCount(documentName);
        var sourceBytes = ArrayPool<byte>.Shared.Rent(Math.Max(1, sourceLength));
        var nameBytes = ArrayPool<byte>.Shared.Rent(Math.Max(1, nameLength));
        try
        {
            sourceLength = System.Text.Encoding.UTF8.GetBytes(
                source,
                sourceBytes);
            nameLength = System.Text.Encoding.UTF8.GetBytes(
                documentName,
                nameBytes);
            fixed (byte* sourcePointer = sourceBytes)
            fixed (byte* namePointer = nameBytes)
            {
                var request = new NativeInteropEvaluateRequest
                {
                    StructSize = (uint)sizeof(NativeInteropEvaluateRequest),
                    Version = 3,
                    Source = sourcePointer,
                    SourceLength = (nuint)sourceLength,
                    DocumentName = namePointer,
                    DocumentNameLength = (nuint)nameLength
                };
                var operationId = NativeWebSceneApi.EngineBeginEvaluateV3(
                    _engine,
                    in request,
                    s_completedAddress,
                    _callbackData);
                if (operationId == 0)
                {
                    slot.FailToBegin(
                        new InvalidOperationException(
                            "Native interop invocation was rejected: "
                            + NativeWebSceneApi.GetLastError(_engine)));
                }
                else
                {
                    slot.SetOperationId(operationId);
                }
            }
        }
        catch (Exception error)
        {
            slot.FailToBegin(error);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(sourceBytes);
            ArrayPool<byte>.Shared.Return(nameBytes);
            EndBindingWindow();
        }
        return slot.AsValueTask();
    }

    internal ValueTask<IntPtr> InvokeGeneratedAsync(
        in NativeInteropInvokeRequest request,
        CancellationToken cancellationToken)
    {
        var slot = RentSlot(cancellationToken);
        BeginBindingWindow();
        try
        {
            var operationId = NativeWebSceneApi.EngineBeginInvokeV3(
                _engine,
                in request,
                s_completedAddress,
                _callbackData);
            if (operationId == 0)
            {
                slot.FailToBegin(
                    new InvalidOperationException(
                        "Generated native interop invocation was rejected: "
                        + NativeWebSceneApi.GetLastError(_engine)));
            }
            else
            {
                slot.SetOperationId(operationId);
            }
        }
        catch (Exception error)
        {
            slot.FailToBegin(error);
        }
        finally
        {
            EndBindingWindow();
        }
        return slot.AsValueTask();
    }

    public void CancelAll()
    {
        OperationSlot[] active;
        lock (_gate)
        {
            active = [.. _active];
        }
        foreach (var slot in active)
        {
            slot.Cancel();
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            _disposed = true;
        }
        CancelAll();
        s_completionBridges.TryRemove(_callbackData, out _);
        lock (_gate)
        {
            foreach (var slot in _available)
            {
                slot.Dispose();
                _all.Remove(slot);
            }
            _available.Clear();
            _operations.Clear();
            _earlyCompletions.Clear();
        }
    }

    private void Return(OperationSlot slot, ulong operationId)
    {
        lock (_gate)
        {
            _active.Remove(slot);
            if (operationId != 0
                && _operations.TryGetValue(operationId, out var registered)
                && ReferenceEquals(registered, slot))
            {
                _operations.Remove(operationId);
            }
            if (!_disposed)
            {
                _available.Push(slot);
            }
            else
            {
                slot.Dispose();
                _all.Remove(slot);
            }
        }
    }

    private void Bind(OperationSlot slot, ulong operationId)
    {
        var complete = false;
        var cancel = false;
        lock (_gate)
        {
            if (_disposed)
            {
                cancel = true;
            }
            else
            {
                _operations.Add(operationId, slot);
                complete = _earlyCompletions.Remove(operationId);
            }
        }
        if (cancel)
        {
            slot.Cancel();
        }
        else if (complete)
        {
            slot.Complete(operationId);
        }
    }

    private void CompleteOperation(ulong operationId)
    {
        OperationSlot? slot;
        lock (_gate)
        {
            if (_disposed) return;
            if (!_operations.TryGetValue(operationId, out slot))
            {
                if (_beginsInFlight != 0)
                {
                    _earlyCompletions.Add(operationId);
                }
                return;
            }
        }
        slot.Complete(operationId);
    }

    private void BeginBindingWindow()
    {
        lock (_gate)
        {
            _beginsInFlight++;
        }
    }

    private void EndBindingWindow()
    {
        lock (_gate)
        {
            _beginsInFlight--;
            if (_beginsInFlight == 0)
            {
                // Every operation ID returned by an overlapping begin call has
                // now been bound. Any unmatched callback belongs to an
                // operation that was already cancelled/returned and must not
                // become an unbounded stale-ID set.
                _earlyCompletions.Clear();
            }
        }
    }

    private OperationSlot RentSlot(CancellationToken cancellationToken)
    {
        OperationSlot slot;
        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (!_available.TryPop(out slot!))
            {
                slot = new OperationSlot(this);
                _all.Add(slot);
            }
            _active.Add(slot);
        }
        slot.Prepare(_engine, cancellationToken);
        return slot;
    }

    private static void Complete(IntPtr userData, ulong operationId)
    {
        try
        {
            if (userData == IntPtr.Zero) return;
            if (s_completionBridges.TryGetValue(userData, out var owner))
            {
                owner.CompleteOperation(operationId);
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native interop completion] {error}");
        }
    }

    private sealed class OperationSlot
        : IValueTaskSource<IntPtr>, IDisposable
    {
        private readonly object _gate = new();
        private readonly NativeInteropInvoker _owner;
        private ManualResetValueTaskSourceCore<IntPtr> _source;
        private CancellationTokenRegistration _cancellation;
        private IntPtr _engine;
        private ulong _operationId;
        private bool _active;
        private bool _cancelRequested;
        private bool _completionSet;

        public OperationSlot(NativeInteropInvoker owner)
        {
            _owner = owner;
            _source.RunContinuationsAsynchronously = true;
        }

        public void Prepare(IntPtr engine, CancellationToken cancellationToken)
        {
            lock (_gate)
            {
                _source.Reset();
                _engine = engine;
                _operationId = 0;
                _active = true;
                _cancelRequested = false;
                _completionSet = false;
            }
            if (cancellationToken.CanBeCanceled)
            {
                _cancellation = cancellationToken.Register(
                    static state => ((OperationSlot)state!).Cancel(),
                    this);
            }
        }

        public ValueTask<IntPtr> AsValueTask()
            => new(this, _source.Version);

        public void SetOperationId(ulong operationId)
        {
            var cancel = false;
            lock (_gate)
            {
                if (!_active) return;
                _operationId = operationId;
                cancel = _cancelRequested;
            }
            _owner.Bind(this, operationId);
            if (cancel)
            {
                Cancel();
            }
        }

        public void Complete(ulong operationId)
        {
            IntPtr engine;
            lock (_gate)
            {
                if (!_active
                    || _completionSet
                    || (_operationId != 0 && _operationId != operationId)) {
                    return;
                }
                _operationId = operationId;
                _completionSet = true;
                engine = _engine;
            }

            var result = NativeWebSceneApi.EngineTakeInvokeResultV3(
                engine,
                operationId);
            if (result == IntPtr.Zero)
            {
                _source.SetException(
                    new InvalidOperationException(
                        "The native interop result was not available."));
                return;
            }

            try
            {
                var view = (NativeInteropResultView*)result;
                NativeInteropBorrowScope.ValidateHeader(view);
                if (view->Status != NativeInteropResultStatus.Succeeded)
                {
                    if (view->ErrorByteCount != 0
                        && view->ErrorBytes == null)
                    {
                        throw new InvalidDataException(
                            "The native interop result has an invalid error buffer.");
                    }
                    var error = view->ErrorByteCount == 0
                        ? view->Status.ToString()
                        : System.Text.Encoding.UTF8.GetString(
                            new ReadOnlySpan<byte>(
                                view->ErrorBytes,
                                checked((int)view->ErrorByteCount)));
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                    _source.SetException(
                        new InvalidOperationException(error));
                    return;
                }
                _source.SetResult(result);
            }
            catch (Exception error)
            {
                NativeInteropResultSafeHandle.ReleaseRaw(result);
                _source.SetException(error);
            }
        }

        public void FailToBegin(Exception error)
        {
            lock (_gate)
            {
                if (!_active || _completionSet) return;
                _completionSet = true;
            }
            _source.SetException(error);
        }

        public void Cancel()
        {
            ulong operationId;
            IntPtr engine;
            lock (_gate)
            {
                if (!_active || _completionSet) return;
                _cancelRequested = true;
                operationId = _operationId;
                engine = _engine;
                if (operationId == 0) return;
                _completionSet = true;
            }
            NativeWebSceneApi.EngineCancelInvokeV3(engine, operationId);
            _source.SetException(new OperationCanceledException());
        }

        public IntPtr GetResult(short token)
        {
            try
            {
                return _source.GetResult(token);
            }
            finally
            {
                _cancellation.Dispose();
                ulong operationId;
                lock (_gate)
                {
                    _active = false;
                    _engine = IntPtr.Zero;
                    operationId = _operationId;
                    _operationId = 0;
                }
                _owner.Return(this, operationId);
            }
        }

        public ValueTaskSourceStatus GetStatus(short token)
            => _source.GetStatus(token);

        public void OnCompleted(
            Action<object?> continuation,
            object? state,
            short token,
            ValueTaskSourceOnCompletedFlags flags)
            => _source.OnCompleted(continuation, state, token, flags);

        public void Dispose()
        {
        }
    }

}

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

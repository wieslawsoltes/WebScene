using System.Buffers;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace WebScene.JavaScript.Interop;

/// <summary>
/// Operation performed by a generated binary JavaScript call site.
/// </summary>
public enum JavaScriptBinaryOperation : uint
{
    GetGlobal = 1,
    InvokeGlobal = 2,
    Construct = 3,
    GetProperty = 4,
    SetProperty = 5,
    InvokeMember = 6,
    ReleaseHandle = 7
}

/// <summary>
/// How the native runtime projects a successful JavaScript return value.
/// </summary>
public enum JavaScriptBinaryResultMode : uint
{
    Value = 0,
    RetainedHandle = 1,
    Void = 2
}

[Flags]
public enum JavaScriptBinaryCallFlags : uint
{
    None = 0,
    AwaitPromise = 1
}

/// <summary>
/// Immutable metadata emitted once per generated member. User values are
/// carried by the tagged request arena rather than embedded in this metadata.
/// </summary>
public sealed class JavaScriptBinaryCallSite
{
    public JavaScriptBinaryCallSite(
        JavaScriptBinaryOperation operation,
        string? globalName,
        string? memberName,
        JavaScriptBinaryResultMode resultMode,
        JavaScriptBinaryCallFlags flags = JavaScriptBinaryCallFlags.None)
    {
        Operation = operation;
        ResultMode = resultMode;
        Flags = flags;
        GlobalNameUtf8 = globalName is null
            ? null
            : Encoding.UTF8.GetBytes(globalName);
        MemberNameUtf8 = memberName is null
            ? null
            : Encoding.UTF8.GetBytes(memberName);
    }

    public JavaScriptBinaryOperation Operation { get; }

    public JavaScriptBinaryResultMode ResultMode { get; }

    public JavaScriptBinaryCallFlags Flags { get; }

    [EditorBrowsable(EditorBrowsableState.Never)]
    public byte[]? GlobalNameUtf8 { get; }

    [EditorBrowsable(EditorBrowsableState.Never)]
    public byte[]? MemberNameUtf8 { get; }
}

/// <summary>
/// Direct tagged-value path implemented by native invokers. Generated methods
/// use this interface when the selected runtime provides the ABI 3 transport.
/// </summary>
public interface IJavaScriptBinaryInvoker : IJavaScriptInvoker
{
    bool IsBinaryInteropAvailable => true;

    ValueTask<TResult> InvokeBinaryAsync<TArguments, TResult, TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>;

    ValueTask InvokeBinaryVoidAsync<TArguments, TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>;

    ValueTask<JavaScriptBinaryResultLease> InvokeBinaryBorrowedAsync<
        TArguments,
        TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments>;
}

/// <summary>
/// Engine-specific transport injected into <see cref="NativeJavaScriptInvoker"/>.
/// It owns request copying, operation completion, native result decoding, and
/// arena release while the invoker owns generated dispatch and transport
/// lifetime.
/// </summary>
public interface IJavaScriptBinaryTransport : IDisposable
{
    ValueTask<TResult> InvokeAsync<TArguments, TResult, TCodec>(
        IJavaScriptInvoker invoker,
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>;

    ValueTask InvokeVoidAsync<TArguments, TCodec>(
        IJavaScriptInvoker invoker,
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>;

    ValueTask<JavaScriptBinaryResultLease> InvokeBorrowedAsync<
        TArguments,
        TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments>;
}

/// <summary>
/// Implemented by generated, reflection-free request codecs.
/// </summary>
public interface IJavaScriptBinaryArgumentsCodec<TArguments>
{
    static abstract uint EncodeArguments(
        ref JavaScriptBinaryWriter writer,
        in TArguments arguments);
}

/// <summary>
/// Implemented by generated, reflection-free request and materialized-result
/// codecs.
/// </summary>
public interface IJavaScriptBinaryCodec<TArguments, TResult>
    : IJavaScriptBinaryArgumentsCodec<TArguments>
{
    static abstract TResult DecodeResult(
        JavaScriptBinaryValue value,
        IJavaScriptInvoker invoker);
}

/// <summary>
/// Placeholder result used by generated void call codecs.
/// </summary>
public readonly record struct JavaScriptBinaryVoid;

/// <summary>
/// Owns an immutable native tagged-result arena. A borrow scope pins the arena
/// while its stack-only values and UTF-8 spans are being inspected.
/// </summary>
public abstract class JavaScriptBinaryResultLease : IDisposable
{
    public JavaScriptBinaryBorrowScope Borrow()
    {
        var root = AcquireBorrow(out var token);
        return new JavaScriptBinaryBorrowScope(this, root, token);
    }

    protected abstract JavaScriptBinaryValue AcquireBorrow(
        out object? borrowToken);

    protected abstract void ReleaseBorrow(object? borrowToken);

    internal void ReleaseBorrowCore(object? borrowToken)
        => ReleaseBorrow(borrowToken);

    public abstract void Dispose();
}

/// <summary>
/// Stack-only reader for one borrowed result arena. Every value and span
/// obtained from this scope expires when the scope is disposed.
/// </summary>
public ref struct JavaScriptBinaryBorrowScope
{
    private JavaScriptBinaryResultLease? _lease;
    private object? _borrowToken;
    private readonly JavaScriptBinaryValue _root;

    internal JavaScriptBinaryBorrowScope(
        JavaScriptBinaryResultLease lease,
        JavaScriptBinaryValue root,
        object? borrowToken)
    {
        _lease = lease;
        _root = root;
        _borrowToken = borrowToken;
    }

    public readonly JavaScriptBinaryValue Root
        => _lease is null
            ? throw new ObjectDisposedException(
                nameof(JavaScriptBinaryBorrowScope))
            : _root;

    public void Dispose()
    {
        var lease = _lease;
        if (lease is null) return;
        _lease = null;
        var token = _borrowToken;
        _borrowToken = null;
        lease.ReleaseBorrowCore(token);
    }
}

public enum JavaScriptBinaryValueKind : uint
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

/// <summary>
/// Fixed-size tagged node shared by generated request codecs and the native
/// invocation ABI.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
[EditorBrowsable(EditorBrowsableState.Never)]
public struct JavaScriptBinaryValueData
{
    public JavaScriptBinaryValueKind Kind;
    public uint Flags;
    public uint Offset;
    public uint Length;
    public ulong Payload;
}

/// <summary>
/// Fixed-size array/object edge shared by generated request codecs and the
/// native invocation ABI.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
[EditorBrowsable(EditorBrowsableState.Never)]
public struct JavaScriptBinaryEdgeData
{
    public uint NameOffset;
    public uint NameLength;
    public uint ValueIndex;
    public uint Reserved;
}

/// <summary>
/// Stack-only builder for a pooled tagged request arena. The native invoker
/// copies the completed spans before this writer is disposed.
/// </summary>
public ref struct JavaScriptBinaryWriter
{
    private JavaScriptBinaryValueData[]? _values;
    private JavaScriptBinaryEdgeData[]? _edges;
    private byte[]? _utf8;
    private int _valueCount;
    private int _edgeCount;
    private int _utf8Count;

    public JavaScriptBinaryWriter()
        : this(16, 16, 256)
    {
    }

    public JavaScriptBinaryWriter(
        int initialValueCapacity,
        int initialEdgeCapacity,
        int initialUtf8Capacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(initialValueCapacity);
        ArgumentOutOfRangeException.ThrowIfNegative(initialEdgeCapacity);
        ArgumentOutOfRangeException.ThrowIfNegative(initialUtf8Capacity);
        _values = ArrayPool<JavaScriptBinaryValueData>.Shared.Rent(
            Math.Max(1, initialValueCapacity));
        _edges = ArrayPool<JavaScriptBinaryEdgeData>.Shared.Rent(
            Math.Max(1, initialEdgeCapacity));
        _utf8 = ArrayPool<byte>.Shared.Rent(Math.Max(1, initialUtf8Capacity));
        _valueCount = 0;
        _edgeCount = 0;
        _utf8Count = 0;
    }

    public readonly ReadOnlySpan<JavaScriptBinaryValueData> Values
        => RequireValues().AsSpan(0, _valueCount);

    public readonly ReadOnlySpan<JavaScriptBinaryEdgeData> Edges
        => RequireEdges().AsSpan(0, _edgeCount);

    public readonly ReadOnlySpan<byte> Utf8
        => RequireUtf8().AsSpan(0, _utf8Count);

    public uint WriteUndefined()
        => AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Undefined
        });

    public uint WriteNull()
        => AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Null
        });

    public uint WriteBoolean(bool value)
        => AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Boolean,
            Payload = value ? 1U : 0U
        });

    public uint WriteNumber(double value)
        => AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Number,
            Payload = unchecked((ulong)BitConverter.DoubleToInt64Bits(value))
        });

    public uint WriteHandle(JavaScriptObjectReference value)
    {
        if (value.IsEmpty)
        {
            throw new ArgumentException(
                "A non-empty JavaScript object reference is required.",
                nameof(value));
        }
        return AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Handle,
            Payload = unchecked((ulong)value.Id)
        });
    }

    public uint WriteString(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var byteCount = Encoding.UTF8.GetByteCount(value);
        var offset = ReserveUtf8(byteCount);
        var written = Encoding.UTF8.GetBytes(
            value.AsSpan(),
            RequireUtf8().AsSpan(offset, byteCount));
        return AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.String,
            Offset = checked((uint)offset),
            Length = checked((uint)written)
        });
    }

    public uint BeginArray(int itemCount)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(itemCount);
        var edgeOffset = ReserveEdges(itemCount);
        return AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Array,
            Offset = checked((uint)edgeOffset),
            Length = checked((uint)itemCount)
        });
    }

    public void SetArrayItem(uint arrayIndex, int itemIndex, uint valueIndex)
    {
        ref var array = ref GetContainer(
            arrayIndex,
            JavaScriptBinaryValueKind.Array,
            itemIndex);
        ValidateValueIndex(valueIndex);
        RequireEdges()[checked((int)array.Offset + itemIndex)] =
            new JavaScriptBinaryEdgeData { ValueIndex = valueIndex };
    }

    public uint BeginObject(int propertyCount)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(propertyCount);
        var edgeOffset = ReserveEdges(propertyCount);
        return AppendValue(new JavaScriptBinaryValueData
        {
            Kind = JavaScriptBinaryValueKind.Object,
            Offset = checked((uint)edgeOffset),
            Length = checked((uint)propertyCount)
        });
    }

    public void SetObjectProperty(
        uint objectIndex,
        int propertyIndex,
        ReadOnlySpan<byte> utf8Name,
        uint valueIndex)
    {
        if (utf8Name.IsEmpty)
        {
            throw new ArgumentException(
                "A property name is required.",
                nameof(utf8Name));
        }
        ref var value = ref GetContainer(
            objectIndex,
            JavaScriptBinaryValueKind.Object,
            propertyIndex);
        ValidateValueIndex(valueIndex);
        var nameOffset = ReserveUtf8(utf8Name.Length);
        utf8Name.CopyTo(RequireUtf8().AsSpan(nameOffset, utf8Name.Length));
        RequireEdges()[checked((int)value.Offset + propertyIndex)] =
            new JavaScriptBinaryEdgeData
            {
                NameOffset = checked((uint)nameOffset),
                NameLength = checked((uint)utf8Name.Length),
                ValueIndex = valueIndex
            };
    }

    public void Dispose()
    {
        var values = _values;
        var edges = _edges;
        var utf8 = _utf8;
        this = default;
        if (values is not null)
        {
            ArrayPool<JavaScriptBinaryValueData>.Shared.Return(values);
        }
        if (edges is not null)
        {
            ArrayPool<JavaScriptBinaryEdgeData>.Shared.Return(edges);
        }
        if (utf8 is not null)
        {
            ArrayPool<byte>.Shared.Return(utf8);
        }
    }

    private uint AppendValue(JavaScriptBinaryValueData value)
    {
        EnsureValueCapacity(1);
        var index = _valueCount++;
        RequireValues()[index] = value;
        return checked((uint)index);
    }

    private int ReserveEdges(int count)
    {
        EnsureEdgeCapacity(count);
        var offset = _edgeCount;
        _edgeCount = checked(_edgeCount + count);
        RequireEdges().AsSpan(offset, count).Clear();
        return offset;
    }

    private int ReserveUtf8(int count)
    {
        EnsureUtf8Capacity(count);
        var offset = _utf8Count;
        _utf8Count = checked(_utf8Count + count);
        return offset;
    }

    private ref JavaScriptBinaryValueData GetContainer(
        uint valueIndex,
        JavaScriptBinaryValueKind expectedKind,
        int childIndex)
    {
        ValidateValueIndex(valueIndex);
        ref var value = ref RequireValues()[checked((int)valueIndex)];
        if (value.Kind != expectedKind)
        {
            throw new InvalidOperationException(
                $"Expected {expectedKind}, received {value.Kind}.");
        }
        if ((uint)childIndex >= value.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(childIndex));
        }
        return ref value;
    }

    private readonly void ValidateValueIndex(uint valueIndex)
    {
        if (valueIndex >= _valueCount)
        {
            throw new ArgumentOutOfRangeException(nameof(valueIndex));
        }
    }

    private void EnsureValueCapacity(int additional)
    {
        var required = checked(_valueCount + additional);
        if (required <= RequireValues().Length) return;
        var replacement =
            ArrayPool<JavaScriptBinaryValueData>.Shared.Rent(
                Math.Max(required, checked(RequireValues().Length * 2)));
        RequireValues().AsSpan(0, _valueCount).CopyTo(replacement);
        ArrayPool<JavaScriptBinaryValueData>.Shared.Return(RequireValues());
        _values = replacement;
    }

    private void EnsureEdgeCapacity(int additional)
    {
        var required = checked(_edgeCount + additional);
        if (required <= RequireEdges().Length) return;
        var replacement =
            ArrayPool<JavaScriptBinaryEdgeData>.Shared.Rent(
                Math.Max(required, checked(RequireEdges().Length * 2)));
        RequireEdges().AsSpan(0, _edgeCount).CopyTo(replacement);
        ArrayPool<JavaScriptBinaryEdgeData>.Shared.Return(RequireEdges());
        _edges = replacement;
    }

    private void EnsureUtf8Capacity(int additional)
    {
        var required = checked(_utf8Count + additional);
        if (required <= RequireUtf8().Length) return;
        var replacement = ArrayPool<byte>.Shared.Rent(
            Math.Max(required, checked(RequireUtf8().Length * 2)));
        RequireUtf8().AsSpan(0, _utf8Count).CopyTo(replacement);
        ArrayPool<byte>.Shared.Return(RequireUtf8());
        _utf8 = replacement;
    }

    private readonly JavaScriptBinaryValueData[] RequireValues()
        => _values
           ?? throw new ObjectDisposedException(nameof(JavaScriptBinaryWriter));

    private readonly JavaScriptBinaryEdgeData[] RequireEdges()
        => _edges
           ?? throw new ObjectDisposedException(nameof(JavaScriptBinaryWriter));

    private readonly byte[] RequireUtf8()
        => _utf8
           ?? throw new ObjectDisposedException(nameof(JavaScriptBinaryWriter));
}

/// <summary>
/// Stack-only view used by generated result codecs. Offsets address the
/// supplied UTF-8 and edge arenas and are validated before spans are created.
/// </summary>
public readonly unsafe ref struct JavaScriptBinaryValue
{
    private readonly JavaScriptBinaryValueData* _values;
    private readonly JavaScriptBinaryEdgeData* _edges;
    private readonly byte* _utf8;
    private readonly uint _valueCount;
    private readonly uint _edgeCount;
    private readonly uint _utf8Count;
    private readonly uint _index;

    [EditorBrowsable(EditorBrowsableState.Never)]
    public JavaScriptBinaryValue(
        JavaScriptBinaryValueData* values,
        uint valueCount,
        JavaScriptBinaryEdgeData* edges,
        uint edgeCount,
        byte* utf8,
        uint utf8Count,
        uint index)
    {
        _values = values;
        _valueCount = valueCount;
        _edges = edges;
        _edgeCount = edgeCount;
        _utf8 = utf8;
        _utf8Count = utf8Count;
        _index = index;
        _ = Data;
    }

    public JavaScriptBinaryValueKind Kind => Data.Kind;

    public int Count
    {
        get
        {
            ref readonly var data = ref Data;
            if (data.Kind is not (
                    JavaScriptBinaryValueKind.Array
                    or JavaScriptBinaryValueKind.Object))
            {
                throw new InvalidOperationException(
                    "Only arrays and objects have child values.");
            }
            ValidateEdges(data);
            return checked((int)data.Length);
        }
    }

    public bool GetBoolean()
        => Require(JavaScriptBinaryValueKind.Boolean).Payload != 0;

    public double GetNumber()
        => BitConverter.Int64BitsToDouble(
            unchecked((long)Require(JavaScriptBinaryValueKind.Number).Payload));

    public JavaScriptObjectReference GetHandle()
        => new(unchecked((long)Require(
            JavaScriptBinaryValueKind.Handle).Payload));

    public ReadOnlySpan<byte> Utf8
    {
        get
        {
            ref readonly var data = ref Require(
                JavaScriptBinaryValueKind.String);
            ValidateUtf8(data.Offset, data.Length);
            return new ReadOnlySpan<byte>(
                _utf8 + data.Offset,
                checked((int)data.Length));
        }
    }

    public string GetString() => Encoding.UTF8.GetString(Utf8);

    public JavaScriptBinaryValue GetArrayItem(int index)
    {
        ref readonly var data = ref Require(JavaScriptBinaryValueKind.Array);
        var edge = GetEdge(data, index);
        return Child(edge.ValueIndex);
    }

    public bool TryGetProperty(
        ReadOnlySpan<byte> utf8Name,
        out JavaScriptBinaryValue value)
    {
        ref readonly var data = ref Require(JavaScriptBinaryValueKind.Object);
        for (var index = 0; index < data.Length; index++)
        {
            var edge = GetEdge(data, checked((int)index));
            ValidateUtf8(edge.NameOffset, edge.NameLength);
            var name = new ReadOnlySpan<byte>(
                _utf8 + edge.NameOffset,
                checked((int)edge.NameLength));
            if (!name.SequenceEqual(utf8Name)) continue;
            value = Child(edge.ValueIndex);
            return true;
        }
        value = default;
        return false;
    }

    public JavaScriptBinaryValue GetRequiredProperty(
        ReadOnlySpan<byte> utf8Name)
        => TryGetProperty(utf8Name, out var value)
            ? value
            : throw new InvalidDataException(
                $"The required binary property '{Encoding.UTF8.GetString(utf8Name)}' is missing.");

    private ref readonly JavaScriptBinaryValueData Data
    {
        get
        {
            if (_values == null || _index >= _valueCount)
            {
                throw new InvalidDataException(
                    "The binary value index is invalid.");
            }
            return ref _values[_index];
        }
    }

    private JavaScriptBinaryValue Child(uint index)
        => new(
            _values,
            _valueCount,
            _edges,
            _edgeCount,
            _utf8,
            _utf8Count,
            index);

    private ref readonly JavaScriptBinaryValueData Require(
        JavaScriptBinaryValueKind kind)
    {
        ref readonly var data = ref Data;
        if (data.Kind != kind)
        {
            throw new InvalidOperationException(
                $"Expected {kind}, received {data.Kind}.");
        }
        return ref data;
    }

    private JavaScriptBinaryEdgeData GetEdge(
        in JavaScriptBinaryValueData data,
        int index)
    {
        ValidateEdges(data);
        if ((uint)index >= data.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        var edge = _edges[data.Offset + checked((uint)index)];
        if (edge.ValueIndex >= _valueCount)
        {
            throw new InvalidDataException(
                "A binary edge references an invalid value.");
        }
        return edge;
    }

    private void ValidateEdges(in JavaScriptBinaryValueData data)
    {
        if (_edges == null && data.Length != 0
            || data.Offset > _edgeCount
            || data.Length > _edgeCount - data.Offset)
        {
            throw new InvalidDataException(
                "A binary value has an invalid edge range.");
        }
    }

    private void ValidateUtf8(uint offset, uint length)
    {
        if (_utf8 == null && length != 0
            || offset > _utf8Count
            || length > _utf8Count - offset)
        {
            throw new InvalidDataException(
                "A binary value has an invalid UTF-8 range.");
        }
    }
}

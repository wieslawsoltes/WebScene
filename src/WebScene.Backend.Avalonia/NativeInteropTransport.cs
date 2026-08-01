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
    : IJavaScriptBinaryCallbackTransport
{
    private readonly NativeInteropInvoker _operations;
    private readonly NativeCallbackCompletionOwner _callbackCompletions;

    public NativeJavaScriptBinaryTransport(IntPtr engine)
    {
        if (engine == IntPtr.Zero)
        {
            throw new ArgumentException(
                "A native engine is required.",
                nameof(engine));
        }
        _operations = new NativeInteropInvoker(engine);
        _callbackCompletions = new NativeCallbackCompletionOwner(engine);
    }

    public NativeInteropPoolMetrics PoolMetrics => _operations.PoolMetrics;

    public JavaScriptBinaryCallbackLease? TryTakeCallback()
    {
        if (!_callbackCompletions.TryEnter(out var engine))
        {
            return null;
        }
        try
        {
            var callback = NativeWebSceneApi.EngineTakeCallbackV3(engine);
            return callback == IntPtr.Zero
                ? null
                : new NativeJavaScriptBinaryCallbackLease(
                    _callbackCompletions,
                    callback);
        }
        finally
        {
            _callbackCompletions.Exit();
        }
    }

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

    public void Dispose()
    {
        _callbackCompletions.Dispose();
        _operations.Dispose();
    }

    private sealed class BinaryDecodeSource<TArguments, TResult, TCodec>
        : IValueTaskSource<TResult>
        where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>
    {
        private static readonly object s_poolGate = new();
        private static readonly Stack<
            BinaryDecodeSource<TArguments, TResult, TCodec>> s_pool = new();

        private ManualResetValueTaskSourceCore<TResult> _core;
        private readonly Action _complete;
        private readonly object _lifetimeGate = new();
        private ValueTask<IntPtr> _pending;
        private IJavaScriptInvoker? _invoker;
        private bool _publisherCompleted;
        private bool _consumerCompleted;

        private BinaryDecodeSource()
        {
            _core.RunContinuationsAsynchronously = false;
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

            BinaryDecodeSource<TArguments, TResult, TCodec>? source;
            lock (s_poolGate)
            {
                s_pool.TryPop(out source);
            }
            source ??= new BinaryDecodeSource<
                TArguments,
                TResult,
                TCodec>();
            source._core.Reset();
            lock (source._lifetimeGate)
            {
                source._publisherCompleted = false;
                source._consumerCompleted = false;
            }
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
                FinishLifetime(consumer: true);
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
                TResult decoded;
                try
                {
                    unsafe
                    {
                        var root = NativeInteropBorrowScope.GetBinaryRoot(
                            (NativeInteropResultView*)result);
                        decoded = TCodec.DecodeResult(
                            root,
                            _invoker!);
                    }
                }
                finally
                {
                    NativeInteropResultSafeHandle.ReleaseRaw(result);
                }
                _core.SetResult(decoded);
            }
            catch (Exception error)
            {
                _core.SetException(error);
            }
            finally
            {
                FinishLifetime(consumer: false);
            }
        }

        private void FinishLifetime(bool consumer)
        {
            var returnToPool = false;
            lock (_lifetimeGate)
            {
                if (consumer)
                {
                    _consumerCompleted = true;
                    returnToPool = _publisherCompleted;
                }
                else
                {
                    _publisherCompleted = true;
                    returnToPool = _consumerCompleted;
                }
            }
            if (returnToPool)
            {
                lock (s_poolGate)
                {
                    s_pool.Push(this);
                }
            }
        }
    }

    private sealed class BinaryVoidDecodeSource<TArguments, TCodec>
        : IValueTaskSource
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
    {
        private static readonly object s_poolGate = new();
        private static readonly Stack<
            BinaryVoidDecodeSource<TArguments, TCodec>> s_pool = new();

        private ManualResetValueTaskSourceCore<bool> _core;
        private readonly Action _complete;
        private readonly object _lifetimeGate = new();
        private ValueTask<IntPtr> _pending;
        private IJavaScriptInvoker? _invoker;
        private bool _publisherCompleted;
        private bool _consumerCompleted;

        private BinaryVoidDecodeSource()
        {
            _core.RunContinuationsAsynchronously = false;
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

            BinaryVoidDecodeSource<TArguments, TCodec>? source;
            lock (s_poolGate)
            {
                s_pool.TryPop(out source);
            }
            source ??= new BinaryVoidDecodeSource<TArguments, TCodec>();
            source._core.Reset();
            lock (source._lifetimeGate)
            {
                source._publisherCompleted = false;
                source._consumerCompleted = false;
            }
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
                FinishLifetime(consumer: true);
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
            finally
            {
                FinishLifetime(consumer: false);
            }
        }

        private void FinishLifetime(bool consumer)
        {
            var returnToPool = false;
            lock (_lifetimeGate)
            {
                if (consumer)
                {
                    _consumerCompleted = true;
                    returnToPool = _publisherCompleted;
                }
                else
                {
                    _publisherCompleted = true;
                    returnToPool = _consumerCompleted;
                }
            }
            if (returnToPool)
            {
                lock (s_poolGate)
                {
                    s_pool.Push(this);
                }
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

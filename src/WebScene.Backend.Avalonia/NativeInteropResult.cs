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
        : IValueTaskSource<IntPtr>, IThreadPoolWorkItem, IDisposable
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
        private ulong _completedOperationId;
        private bool _publisherCompleted;
        private bool _consumerCompleted;

        public OperationSlot(NativeInteropInvoker owner)
        {
            _owner = owner;
            _source.RunContinuationsAsynchronously = false;
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
                _completedOperationId = 0;
                _publisherCompleted = false;
                _consumerCompleted = false;
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
            lock (_gate)
            {
                if (!_active
                    || _completionSet
                    || (_operationId != 0 && _operationId != operationId)) {
                    return;
                }
                _operationId = operationId;
                _completionSet = true;
                _completedOperationId = operationId;
            }
            ThreadPool.UnsafeQueueUserWorkItem(this, preferLocal: false);
        }

        void IThreadPoolWorkItem.Execute()
        {
            IntPtr engine;
            ulong operationId;
            lock (_gate)
            {
                if (!_active || _completedOperationId == 0) return;
                engine = _engine;
                operationId = _completedOperationId;
                _completedOperationId = 0;
            }
            try
            {
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
                var view = (NativeInteropResultView*)result;
                try
                {
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
            finally
            {
                FinishLifetime(consumer: false);
            }
        }

        public void FailToBegin(Exception error)
        {
            lock (_gate)
            {
                if (!_active || _completionSet) return;
                _completionSet = true;
            }
            try
            {
                _source.SetException(error);
            }
            finally
            {
                FinishLifetime(consumer: false);
            }
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
            try
            {
                _source.SetException(new OperationCanceledException());
            }
            finally
            {
                FinishLifetime(consumer: false);
            }
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
                FinishLifetime(consumer: true);
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

        private void FinishLifetime(bool consumer)
        {
            var returnToPool = false;
            ulong operationId = 0;
            lock (_gate)
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
                if (returnToPool)
                {
                    _active = false;
                    _engine = IntPtr.Zero;
                    operationId = _operationId;
                    _operationId = 0;
                    _completedOperationId = 0;
                }
            }
            if (returnToPool)
            {
                _owner.Return(this, operationId);
            }
        }
    }

}

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Channels;
using WebScene.Backends;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

internal sealed class NativeEngineLifetime
{
    private readonly object _gate = new();
    private int _activeCalls;
    private bool _closing;

    public bool IsClosing
    {
        get
        {
            lock (_gate) return _closing;
        }
    }

    public bool TryEnter()
    {
        lock (_gate)
        {
            if (_closing) return false;
            _activeCalls++;
            return true;
        }
    }

    public void Exit()
    {
        lock (_gate)
        {
            if (--_activeCalls == 0) Monitor.PulseAll(_gate);
        }
    }

    public void BeginClosingAndWait()
    {
        lock (_gate)
        {
            _closing = true;
            while (_activeCalls != 0) Monitor.Wait(_gate);
        }
    }
}

internal sealed class NativeInspectorOutputByteBudget
{
    public const long MaximumBytes = 16L * 1024 * 1024;

    private long _queuedBytes;

    public long QueuedBytes => Volatile.Read(ref _queuedBytes);

    public bool TryReserve(int messageBytes)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(messageBytes);
        while (true)
        {
            var queuedBytes = Volatile.Read(ref _queuedBytes);
            if (messageBytes > MaximumBytes - queuedBytes) return false;
            if (Interlocked.CompareExchange(
                    ref _queuedBytes,
                    queuedBytes + messageBytes,
                    queuedBytes) == queuedBytes)
            {
                return true;
            }
        }
    }

    public void Release(int messageBytes)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(messageBytes);
        var remaining = Interlocked.Add(ref _queuedBytes, -messageBytes);
        if (remaining < 0)
        {
            Interlocked.Add(ref _queuedBytes, messageBytes);
            throw new InvalidOperationException(
                "The Inspector output byte budget was released more than it was reserved.");
        }
    }
}

public sealed class NativeV8InspectorSession : INativeV8InspectorSession
{
    private readonly IntPtr _engine;
    private readonly NativeEngineLifetime _engineLifetime;
    private readonly Channel<ReadOnlyMemory<byte>> _messages;
    private readonly NativeInspectorOutputByteBudget _outputByteBudget = new();
    private readonly object _drainGate = new();
    private Task? _drainTask;
    private int _disposed;
    private int _nativeClosed;

    internal NativeV8InspectorSession(
        IntPtr engine,
        ulong sessionId,
        NativeEngineLifetime engineLifetime)
    {
        _engine = engine;
        _engineLifetime = engineLifetime;
        SessionId = sessionId;
        _messages = Channel.CreateBounded<ReadOnlyMemory<byte>>(
            new BoundedChannelOptions(1024)
            {
                FullMode = BoundedChannelFullMode.Wait,
                SingleReader = true,
                SingleWriter = true
            });
    }

    public ulong SessionId { get; }

    public ValueTask SendAsync(
        ReadOnlyMemory<byte> message,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(
            Volatile.Read(ref _disposed) != 0,
            this);
        cancellationToken.ThrowIfCancellationRequested();
        if (message.IsEmpty)
        {
            throw new ArgumentException(
                "An Inspector request cannot be empty.",
                nameof(message));
        }
        if (!_engineLifetime.TryEnter())
        {
            throw new ObjectDisposedException(nameof(NativeV8InspectorSession));
        }
        try
        {
            ObjectDisposedException.ThrowIf(
                Volatile.Read(ref _disposed) != 0,
                this);
            if (!NativeWebSceneApi.TryDispatchInspectorMessage(
                    _engine,
                    SessionId,
                    message.Span))
            {
                throw new InvalidOperationException(
                    "The WebScene V8 Inspector session rejected the request.");
            }
        }
        finally
        {
            _engineLifetime.Exit();
        }
        return ValueTask.CompletedTask;
    }

    public async IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await foreach (var message in _messages.Reader.ReadAllAsync(cancellationToken)
                           .ConfigureAwait(false))
        {
            _outputByteBudget.Release(message.Length);
            yield return message;
        }
    }

    internal void NotifyMessagesAvailable()
    {
        lock (_drainGate)
        {
            if (Volatile.Read(ref _disposed) != 0
                || _drainTask is { IsCompleted: false }) return;
            _drainTask = Task.Run(DrainAvailableMessages);
        }
    }

    private void DrainAvailableMessages()
    {
        while (Volatile.Read(ref _disposed) == 0)
        {
            while (Volatile.Read(ref _disposed) == 0)
            {
                if (!_engineLifetime.TryEnter()) return;
                nuint required;
                try
                {
                    if (Volatile.Read(ref _disposed) != 0) return;
                    required = NativeWebSceneApi.GetInspectorMessageSize(
                        _engine,
                        SessionId);
                }
                finally
                {
                    _engineLifetime.Exit();
                }
                if (required == 0) break;
                if (required == nuint.MaxValue)
                {
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector native output queue exceeded its 1,024-message or 16 MiB limit."));
                    return;
                }
                if (required > int.MaxValue)
                {
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector produced a message too large for the managed host."));
                    return;
                }
                var message = new byte[(int)required];
                if (!_engineLifetime.TryEnter()) return;
                nuint copied;
                try
                {
                    if (Volatile.Read(ref _disposed) != 0) return;
                    copied = NativeWebSceneApi.TakeInspectorMessage(
                        _engine,
                        SessionId,
                        message);
                }
                finally
                {
                    _engineLifetime.Exit();
                }
                if (copied == nuint.MaxValue)
                {
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector native output queue overflowed while draining."));
                    return;
                }
                if (copied == 0) break;
                if (copied != required)
                {
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector output message changed while it was being copied."));
                    return;
                }
                if (!_outputByteBudget.TryReserve(message.Length))
                {
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector managed output queue exceeded its 16 MiB aggregate byte limit."));
                    return;
                }
                if (!_messages.Writer.TryWrite(message))
                {
                    _outputByteBudget.Release(message.Length);
                    Fail(new InvalidOperationException(
                        "The WebScene V8 Inspector managed output queue exceeded 1,024 messages."));
                    return;
                }
            }

            lock (_drainGate)
            {
                if (Volatile.Read(ref _disposed) != 0)
                {
                    _drainTask = null;
                    return;
                }
                if (!_engineLifetime.TryEnter())
                {
                    _drainTask = null;
                    return;
                }
                bool hasMessages;
                try
                {
                    hasMessages = Volatile.Read(ref _disposed) == 0
                        && NativeWebSceneApi.GetInspectorMessageSize(_engine, SessionId) != 0;
                }
                finally
                {
                    _engineLifetime.Exit();
                }
                if (!hasMessages)
                {
                    _drainTask = null;
                    return;
                }
            }
        }
        lock (_drainGate) _drainTask = null;
    }

    private void Fail(Exception error)
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
        {
            _messages.Writer.TryComplete(error);
        }
    }

    internal void CompleteFromEngine()
    {
        var completeWriter = Interlocked.Exchange(ref _disposed, 1) == 0;
        Task? drainTask;
        lock (_drainGate) drainTask = _drainTask;
        if (drainTask is not null)
        {
            try
            {
                drainTask.GetAwaiter().GetResult();
            }
            catch
            {
            }
        }
        if (completeWriter)
        {
            _messages.Writer.TryComplete();
        }
        Interlocked.Exchange(ref _nativeClosed, 1);
    }

    public async ValueTask DisposeAsync()
    {
        var completeWriter = Interlocked.Exchange(ref _disposed, 1) == 0;
        Task? drainTask;
        lock (_drainGate) drainTask = _drainTask;
        if (drainTask is not null)
        {
            try
            {
                await drainTask.ConfigureAwait(false);
            }
            catch
            {
            }
        }
        if (Interlocked.Exchange(ref _nativeClosed, 1) == 0)
        {
            NativeWebSceneApi.CloseInspectorSession(
                _engine,
                SessionId,
                _engineLifetime);
        }
        if (completeWriter) _messages.Writer.TryComplete();
    }
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_connect_v3")]
    private static extern ulong EngineInspectorConnectV3(
        IntPtr engine,
        IntPtr messageAvailableCallback,
        IntPtr userData,
        byte waitForDebugger);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_take_message")]
    private static extern nuint EngineInspectorTakeMessage(
        IntPtr engine,
        ulong sessionId,
        byte* destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_dispatch")]
    private static extern byte EngineInspectorDispatch(
        IntPtr engine,
        ulong sessionId,
        byte* message,
        nuint messageLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_disconnect")]
    private static extern byte EngineInspectorDisconnect(
        IntPtr engine,
        ulong sessionId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_is_available")]
    private static extern byte EngineInspectorIsAvailable(IntPtr engine);

    public static bool IsInspectorAvailable(IntPtr engine)
    {
        if (engine == IntPtr.Zero
            || !EngineResourceBridges.TryGetValue(engine, out var bridgeHandle)
            || !bridgeHandle.IsAllocated
            || bridgeHandle.Target is not ResourceBridge bridge
            || !bridge.InspectorLifetime.TryEnter()) return false;
        try
        {
            return EngineInspectorIsAvailable(engine) != 0;
        }
        finally
        {
            bridge.InspectorLifetime.Exit();
        }
    }

    public static NativeV8InspectorSession OpenInspectorSession(
        IntPtr engine,
        bool waitForDebugger = false)
    {
        if (engine == IntPtr.Zero)
        {
            throw new ArgumentException("The native engine is not loaded.", nameof(engine));
        }
        if (!EngineResourceBridges.TryGetValue(engine, out var bridgeHandle)
            || !bridgeHandle.IsAllocated
            || bridgeHandle.Target is not ResourceBridge bridge)
        {
            throw new InvalidOperationException(
                "The WebScene native engine callback bridge is unavailable.");
        }
        if (!bridge.InspectorLifetime.TryEnter())
        {
            throw new ObjectDisposedException("The WebScene native engine is shutting down.");
        }
        try
        {
            var sessionId = EngineInspectorConnectV3(
                engine,
                InspectorMessageAvailableAddress,
                GCHandle.ToIntPtr(bridgeHandle),
                waitForDebugger ? (byte)1 : (byte)0);
            if (sessionId == 0)
            {
                throw new InvalidOperationException(
                    "V8 Inspector is unavailable. Shared-isolate mode does not support per-view inspector sessions.");
            }
            var session = new NativeV8InspectorSession(
                engine,
                sessionId,
                bridge.InspectorLifetime);
            bridge.RegisterInspectorSession(session);
            return session;
        }
        finally
        {
            bridge.InspectorLifetime.Exit();
        }
    }

    internal static bool TryDispatchInspectorMessage(
        IntPtr engine,
        ulong sessionId,
        ReadOnlySpan<byte> message)
    {
        fixed (byte* pointer = message)
        {
            return EngineInspectorDispatch(
                engine,
                sessionId,
                pointer,
                (nuint)message.Length) != 0;
        }
    }

    internal static nuint GetInspectorMessageSize(IntPtr engine, ulong sessionId)
        => EngineInspectorTakeMessage(engine, sessionId, null, 0);

    internal static nuint TakeInspectorMessage(
        IntPtr engine,
        ulong sessionId,
        Span<byte> destination)
    {
        fixed (byte* pointer = destination)
        {
            return EngineInspectorTakeMessage(
                engine,
                sessionId,
                pointer,
                (nuint)destination.Length);
        }
    }

    internal static void CloseInspectorSession(
        IntPtr engine,
        ulong sessionId,
        NativeEngineLifetime engineLifetime)
    {
        if (!engineLifetime.TryEnter()) return;
        try
        {
            if (EngineResourceBridges.TryGetValue(engine, out var bridgeHandle)
                && bridgeHandle.IsAllocated
                && bridgeHandle.Target is ResourceBridge bridge)
            {
                bridge.UnregisterInspectorSession(sessionId);
            }
            if (engine != IntPtr.Zero)
            {
                EngineInspectorDisconnect(engine, sessionId);
            }
        }
        finally
        {
            engineLifetime.Exit();
        }
    }
}

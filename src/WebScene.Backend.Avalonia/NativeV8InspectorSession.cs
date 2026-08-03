using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Channels;
using WebScene.Backends;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

public sealed class NativeV8InspectorSession : INativeV8InspectorSession
{
    private readonly IntPtr _engine;
    private readonly Channel<ReadOnlyMemory<byte>> _messages;
    private readonly object _drainGate = new();
    private Task? _drainTask;
    private int _disposed;
    private int _nativeClosed;

    internal NativeV8InspectorSession(IntPtr engine, ulong sessionId)
    {
        _engine = engine;
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
        if (!NativeWebSceneApi.TryDispatchInspectorMessage(
                _engine,
                SessionId,
                message.Span))
        {
            throw new InvalidOperationException(
                "The WebScene V8 Inspector session rejected the request.");
        }
        return ValueTask.CompletedTask;
    }

    public async IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        await foreach (var message in _messages.Reader.ReadAllAsync(cancellationToken)
                           .ConfigureAwait(false))
        {
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
                var required = NativeWebSceneApi.GetInspectorMessageSize(
                    _engine,
                    SessionId);
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
                var copied = NativeWebSceneApi.TakeInspectorMessage(
                    _engine,
                    SessionId,
                    message);
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
                if (!_messages.Writer.TryWrite(message))
                {
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
                if (NativeWebSceneApi.GetInspectorMessageSize(_engine, SessionId) == 0)
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
            NativeWebSceneApi.CloseInspectorSession(_engine, SessionId);
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
        => engine != IntPtr.Zero && EngineInspectorIsAvailable(engine) != 0;

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
        var session = new NativeV8InspectorSession(engine, sessionId);
        bridge.RegisterInspectorSession(session);
        return session;
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

    internal static void CloseInspectorSession(IntPtr engine, ulong sessionId)
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
}

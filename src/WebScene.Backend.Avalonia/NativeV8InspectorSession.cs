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
    private int _disposed;

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

    internal void Publish(ReadOnlySpan<byte> message)
    {
        if (Volatile.Read(ref _disposed) != 0) return;
        if (!_messages.Writer.TryWrite(message.ToArray()))
        {
            _messages.Writer.TryComplete(new InvalidOperationException(
                "The WebScene V8 Inspector output queue exceeded 1024 messages."));
        }
    }

    internal void CompleteFromEngine()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
        {
            _messages.Writer.TryComplete();
        }
    }

    public ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return ValueTask.CompletedTask;
        }
        NativeWebSceneApi.CloseInspectorSession(_engine, SessionId);
        _messages.Writer.TryComplete();
        return ValueTask.CompletedTask;
    }
}

public static unsafe partial class NativeWebSceneApi
{
    [DllImport(LibraryName, EntryPoint = "webscene_engine_inspector_connect")]
    private static extern ulong EngineInspectorConnect(
        IntPtr engine,
        IntPtr messageCallback,
        IntPtr userData,
        byte waitForDebugger);

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
        var sessionId = EngineInspectorConnect(
            engine,
            InspectorMessageAddress,
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

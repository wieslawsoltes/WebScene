using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;
using WebScene.Backends.Native;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

// Shared by both .NET presenters. Native callbacks only release a semaphore;
// a separate bounded dispatcher isolates application handlers from draining and UI state.
internal sealed class NativeRuntimeDiagnostics : IDisposable
{
    private readonly object _gate = new();
    private readonly Channel<(long Generation, Action Action)> _delivery = Channel.CreateBounded<(long, Action)>(
        new BoundedChannelOptions(256) { SingleReader = true, FullMode = BoundedChannelFullMode.Wait });
    private readonly Task _dispatcher;
    private NativeDiagnosticSession? _session;
    private Action<WebSceneJavaScriptException>? _exception;
    private Action<WebSceneConsoleMessage>? _console;
    private Action<WebSceneRuntimeFailure>? _failed;
    private bool _captureConsole, _legacyConsole, _fallback;
    private volatile bool _acceptRecords;
    private long _generation, _dropped;
    private WebSceneRuntimeState _state;
    private WebSceneRuntimeFailure? _lastFailure;
    private (long Generation, Action Action)? _reservedFailure;
    private CancellationTokenSource _failureCancellation = new();
    internal CancellationToken FailureToken { get { lock (_gate) return _failureCancellation.Token; } }

    internal NativeRuntimeDiagnostics() => _dispatcher = Task.Run(DispatchAsync);
    internal event Action<WebSceneRuntimeFailure>? FailureStateChanged;
    internal long Generation => Interlocked.Read(ref _generation);
    internal WebSceneRuntimeState State { get { lock (_gate) return _state; } }
    internal WebSceneRuntimeFailure? LastFailure { get { lock (_gate) return _lastFailure; } }
    internal long DroppedCount => Interlocked.Read(ref _dropped);
    internal bool HasNativeDiagnostics => _session?.IsSupported == true;
    internal void CheckForNativeFailure()
    {
        if (_session?.CopyFailure() is { } json) Receive(Generation, json);
    }
    internal bool CaptureConsole { get => _captureConsole; set { lock (_gate) { _captureConsole = value; Configure(); } } }
    internal bool LegacyConsole { get => _legacyConsole; set { lock (_gate) { _legacyConsole = value; Configure(); } } }
    internal bool Fallback { get => _fallback; set { lock (_gate) { _fallback = value; Configure(); } } }
    internal event Action<WebSceneJavaScriptException> JavaScriptException
    {
        add { lock (_gate) { _exception += value; Configure(); } }
        remove { lock (_gate) { _exception -= value; Configure(); } }
    }
    internal event Action<WebSceneConsoleMessage> ConsoleMessage
    {
        add { lock (_gate) { _console += value; Configure(); } }
        remove { lock (_gate) { _console -= value; Configure(); } }
    }
    internal event Action<WebSceneRuntimeFailure> RuntimeFailed
    {
        add { lock (_gate) { _failed += value; Configure(); } }
        remove { lock (_gate) { _failed -= value; Configure(); } }
    }
    private uint Flags => (_exception is null ? 0u : 1u) | (_captureConsole || _console is not null ? 2u : 0u) | (_legacyConsole ? 4u : 0u);
    private void Configure() => _session?.Configure(Flags, _fallback || _failed is not null);

    internal void Begin()
    {
        lock (_gate) {
            ObjectDisposedException.ThrowIf(_state == WebSceneRuntimeState.Disposed, this);
            ++_generation;
            _failureCancellation.Dispose();
            _failureCancellation = new();
            _lastFailure = null;
            _reservedFailure = null;
            _acceptRecords = true;
            _state = WebSceneRuntimeState.Loading;
        }
    }
    internal void Attach(IntPtr engine)
    {
        lock (_gate) {
            ObjectDisposedException.ThrowIf(_state == WebSceneRuntimeState.Disposed, this);
            _session = new NativeDiagnosticSession(engine, Generation, Receive);
            Configure();
        }
    }
    internal void Ready() { lock (_gate) if (_state == WebSceneRuntimeState.Loading) _state = WebSceneRuntimeState.Ready; }
    internal void Detach()
    {
        NativeDiagnosticSession? session;
        lock (_gate) {
            session = _session; _session = null;
            _acceptRecords = false;
            if (_state != WebSceneRuntimeState.Failed && _state != WebSceneRuntimeState.Disposed) _state = WebSceneRuntimeState.Unloaded;
        }
        session?.Dispose();
    }
    internal void Fail(string message, string? stack, string stage, string? source)
        => Failure(new(message, stack, stage, new(Generation, 0, DateTimeOffset.UtcNow, source, 0, source, 0, 0)));

    private void Failure(WebSceneRuntimeFailure failure)
    {
        CancellationTokenSource cancellation;
        lock (_gate) {
            if (failure.Context.Generation != _generation || _state is WebSceneRuntimeState.Failed or WebSceneRuntimeState.Disposed) return;
            _lastFailure = failure;
            _state = WebSceneRuntimeState.Failed;
            cancellation = _failureCancellation;
        }
        // Unblock startup/interop barriers before waiting for UI lifecycle cleanup.
        // Otherwise a dead worker and a load holding the lifecycle gate deadlock.
        try { cancellation.Cancel(); }
        catch (ObjectDisposedException) { }
        catch (Exception error) { SafeTrace(error); }
        // Internal only: posts UI work, never executes application subscribers.
        try { FailureStateChanged?.Invoke(failure); }
        catch (Exception error) { SafeTrace(error); }
        lock (_gate) {
            // One terminal record per generation has reserved delivery capacity.
            var item = (failure.Context.Generation, (Action)(() => Invoke(_failed, failure)));
            if (!_delivery.Writer.TryWrite(item)) _reservedFailure = item;
        }
    }
    internal void Receive(long generation, string json)
    {
        if (!_acceptRecords || generation != Generation || State == WebSceneRuntimeState.Disposed) return;
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        string Text(string name) => root.TryGetProperty(name, out var value) ? value.GetString() ?? "" : "";
        long Number(string name) => root.TryGetProperty(name, out var value) ? value.GetInt64() : 0;
        bool Flag(string name) => root.TryGetProperty(name, out var value) && value.GetBoolean();
        if (Text("kind") == "dropped") { Interlocked.Add(ref _dropped, Number("droppedCount")); return; }
        var context = new WebSceneDiagnosticContext(generation, Number("sequence"),
            DateTimeOffset.FromUnixTimeMilliseconds(Number("timestamp")), Text("documentUrl"),
            (uint)Number("frameId"), Text("source"), (int)Number("line"), (int)Number("column"), Flag("truncated"));
        switch (Text("kind")) {
            case "failure": Failure(new(Text("message"), Text("stack"), Text("stage"), context)); break;
            case "exception":
                var error = new WebSceneJavaScriptException(Text("message"), Text("stack"), Flag("promiseRejection"), context);
                Enqueue(generation, () => Invoke(_exception, error));
                break;
            case "console":
                var args = root.GetProperty("arguments").EnumerateArray().Select(value =>
                    new WebSceneConsoleArgument(value.GetProperty("type").GetString()!, value.GetProperty("value").GetString()!)).ToArray();
                var console = new WebSceneConsoleMessage(Text("level"), Text("message"), Text("stack"), Array.AsReadOnly(args), context);
                Enqueue(generation, () => Invoke(_console, console));
                break;
        }
    }
    private void Enqueue(long generation, Action action)
    {
        lock (_gate)
            if (_reservedFailure is not null || !_delivery.Writer.TryWrite((generation, () => { if (_acceptRecords) action(); }))) Interlocked.Increment(ref _dropped);
    }
    private async Task DispatchAsync()
    {
        await foreach (var item in _delivery.Reader.ReadAllAsync().ConfigureAwait(false)) {
            if (item.Generation == Generation && State != WebSceneRuntimeState.Disposed) item.Action();
            if (!_delivery.Reader.TryPeek(out _)) {
                (long Generation, Action Action)? failure;
                lock (_gate) { failure = _reservedFailure; _reservedFailure = null; }
                if (failure is { } pending && pending.Generation == Generation && State != WebSceneRuntimeState.Disposed) pending.Action();
            }
        }
    }
    private static void Invoke<T>(Action<T>? handlers, T record)
    {
        if (handlers is null) return;
        foreach (Action<T> handler in handlers.GetInvocationList()) {
            try { handler(record); }
            catch (Exception error) { SafeTrace(error); }
        }
    }
    private static void SafeTrace(Exception error)
    {
        try { Trace.TraceError("WebScene diagnostic delivery failed: {0}", error); }
        catch { /* A throwing application TraceListener must not kill delivery. */ }
    }
    public void Dispose()
    {
        lock (_gate) { _state = WebSceneRuntimeState.Disposed; ++_generation; }
        Detach();
        _delivery.Writer.TryComplete();
        _failureCancellation.Dispose();
        // Never join an application callback: disposal is valid from a subscriber.
    }
}

internal sealed class NativeDiagnosticSession : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void Signal(IntPtr data);
    private readonly Signal _callback;
    private readonly SemaphoreSlim _signal = new(0, 1);
    private readonly CancellationTokenSource _stop = new();
    private readonly object _nativeGate = new();
    private readonly IntPtr _engine;
    private readonly long _generation;
    private readonly Action<long, string> _receive;
    private bool _disposed, _supported = true;
    internal bool IsSupported => _supported;

    [DllImport("webscene_native_engine", EntryPoint = "webscene_engine_configure_diagnostics", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ConfigureNative(IntPtr engine, uint flags, Signal? callback, IntPtr data);
    [DllImport("webscene_native_engine", EntryPoint = "webscene_engine_take_diagnostic", CallingConvention = CallingConvention.Cdecl)]
    private static extern nuint TakeNative(IntPtr engine, [Out] byte[]? buffer, nuint capacity);
    [DllImport("webscene_native_engine", EntryPoint = "webscene_engine_copy_runtime_failure", CallingConvention = CallingConvention.Cdecl)]
    private static extern nuint CopyFailureNative(IntPtr engine, [Out] byte[]? buffer, nuint capacity);

    internal string? CopyFailure()
    {
        lock (_nativeGate) {
            if (_disposed || !_supported) return null;
            var size = CopyFailureNative(_engine, null, 0);
            if (size == 0) return null;
            var buffer = new byte[checked((int)size)];
            var written = CopyFailureNative(_engine, buffer, size);
            return written == 0 || written > size ? null : Encoding.UTF8.GetString(buffer, 0, (int)written - 1);
        }
    }

    internal NativeDiagnosticSession(IntPtr engine, long generation, Action<long, string> receive)
    {
        _engine = engine; _generation = generation; _receive = receive;
        _callback = _ => { try { _signal.Release(); } catch (SemaphoreFullException) { } };
        _ = Task.Run(DrainAsync);
    }
    internal void Configure(uint flags, bool required)
    {
        lock (_nativeGate) {
            if (_disposed) return;
            if (_supported) {
                try { ConfigureNative(_engine, flags, _callback, IntPtr.Zero); }
                catch (EntryPointNotFoundException) { _supported = false; }
            }
            if (!_supported && (required || flags != 0)) throw new NotSupportedException(
                "This native WebScene library does not support runtime diagnostics. Upgrade the native runtime package.");
        }
    }
    private async Task DrainAsync()
    {
        try {
            while (true) {
                await _signal.WaitAsync(_stop.Token).ConfigureAwait(false);
                while (true) {
                    string? json = null;
                    lock (_nativeGate) {
                        if (_disposed) return;
                        var size = TakeNative(_engine, null, 0);
                        if (size != 0) {
                            var buffer = new byte[checked((int)size)];
                            var written = TakeNative(_engine, buffer, size);
                            // Producers can evict the probed record between calls. Retry a short copy.
                            if (written > size) continue;
                            if (written > 0) json = Encoding.UTF8.GetString(buffer, 0, (int)written - 1);
                        }
                    }
                    if (json is null) break;
                    try { _receive(_generation, json); }
                    catch (Exception error) { Trace.TraceError("WebScene diagnostic decode failed: {0}", error); }
                }
            }
        }
        catch (OperationCanceledException) { }
    }
    public void Dispose()
    {
        lock (_nativeGate) {
            if (_disposed) return;
            _disposed = true;
            if (_supported) ConfigureNative(_engine, 0, null, IntPtr.Zero);
        }
        _stop.Cancel();
        GC.KeepAlive(_callback);
    }
}

using System.Collections.Concurrent;
using WebScene.JavaScript.Interop;

namespace WebScene.Sdk.Avalonia;

internal sealed class NativeComponentBridgeSession : IAsyncDisposable
{
    private static readonly JavaScriptBinaryCallSite s_getGlobal = new(
        JavaScriptBinaryOperation.GetGlobal,
        "globalThis",
        memberName: null,
        JavaScriptBinaryResultMode.RetainedHandle);
    private static readonly JavaScriptBinaryCallSite s_setBridge = new(
        JavaScriptBinaryOperation.SetProperty,
        globalName: null,
        "__webSceneNativeHostBridge",
        JavaScriptBinaryResultMode.Void);

    private readonly NativeJavaScriptInvoker _invoker;
    private readonly NativeComponentHostBridge _target;
    private readonly CancellationTokenSource _pumpCancellation = new();
    private readonly ConcurrentDictionary<Task, byte> _pendingCallbacks = new();
    private JavaScriptObjectReference _global;
    private JavaScriptObjectReference _bridge;
    private Task? _pumpTask;

    private NativeComponentBridgeSession(
        NativeJavaScriptInvoker invoker,
        NativeComponentHostBridge target)
    {
        _invoker = invoker;
        _target = target;
    }

    public static async ValueTask<NativeComponentBridgeSession> CreateAsync(
        NativeJavaScriptInvoker invoker,
        WebSceneHostBridge bridge,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(invoker);
        var session = new NativeComponentBridgeSession(
            invoker,
            new NativeComponentHostBridge(bridge));
        try
        {
            session._global = await invoker.InvokeBinaryAsync<
                JavaScriptBinaryVoid,
                JavaScriptObjectReference,
                EmptyToHandleCodec>(
                    s_getGlobal,
                    default,
                    default,
                    cancellationToken).ConfigureAwait(false);
            session._bridge = await invoker.RegisterBinaryCallbackTargetAsync(
                    session._target,
                    NativeComponentHostBridge.Methods,
                    cancellationToken)
                .ConfigureAwait(false);
            await invoker.InvokeBinaryVoidAsync<
                    JavaScriptObjectReference,
                    HandleToVoidCodec>(
                    s_setBridge,
                    session._global,
                    session._bridge,
                    cancellationToken)
                .ConfigureAwait(false);
            session._pumpTask = Task.Run(
                () => session.PumpAsync(session._pumpCancellation.Token),
                CancellationToken.None);
            return session;
        }
        catch
        {
            await session.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        _target.CancelAll();
        _pumpCancellation.Cancel();
        if (_pumpTask is not null)
        {
            try
            {
                await _pumpTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }
        var pendingCallbacks = _pendingCallbacks.Keys.ToArray();
        if (pendingCallbacks.Length != 0)
        {
            await Task.WhenAll(pendingCallbacks).ConfigureAwait(false);
        }
        try
        {
            if (!_bridge.IsEmpty)
            {
                await _invoker.ReleaseAsync(_bridge).ConfigureAwait(false);
            }
            if (!_global.IsEmpty)
            {
                await _invoker.ReleaseAsync(_global).ConfigureAwait(false);
            }
        }
        catch (ObjectDisposedException)
        {
        }
        _invoker.Dispose();
        _pumpCancellation.Dispose();
    }

    private async Task PumpAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            await _invoker.WaitForCallbackAsync(cancellationToken)
                .ConfigureAwait(false);
            while (true)
            {
                var pending = _invoker.PumpCallbackAsync(cancellationToken);
                if (pending.IsCompletedSuccessfully)
                {
                    if (!pending.Result)
                    {
                        break;
                    }
                }
                else
                {
                    Track(pending.AsTask());
                }
            }
        }
    }

    private void Track(Task<bool> pending)
    {
        _pendingCallbacks.TryAdd(pending, 0);
        _ = ObserveAsync(pending);
    }

    private async Task ObserveAsync(Task<bool> pending)
    {
        try
        {
            await pending.ConfigureAwait(false);
        }
        catch
        {
            // Callback failures are returned to the JavaScript promise by the
            // invoker. There is no second failure channel to observe here.
        }
        finally
        {
            _pendingCallbacks.TryRemove(pending, out _);
        }
    }

    private readonly struct EmptyToHandleCodec
        : IJavaScriptBinaryCodec<JavaScriptBinaryVoid, JavaScriptObjectReference>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static JavaScriptObjectReference DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetHandle();
    }

    private readonly struct HandleToVoidCodec
        : IJavaScriptBinaryCodec<JavaScriptObjectReference, JavaScriptBinaryVoid>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptObjectReference arguments)
        {
            var root = writer.BeginArray(1);
            writer.SetArrayItem(root, 0, writer.WriteHandle(arguments));
            return root;
        }

        public static JavaScriptBinaryVoid DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => default;
    }
}

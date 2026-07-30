namespace WebScene.JavaScript.Interop;

/// <summary>
/// Bidirectional native JavaScript invoker backed by the ABI 3 tagged binary
/// transport. Generated calls and reverse callbacks whose shapes have binary
/// codecs avoid source generation and JSON.
/// </summary>
public sealed class NativeJavaScriptInvoker :
    IJavaScriptBinaryBidirectionalInvoker,
    IDisposable
{
    private static readonly JavaScriptBinaryCallSite s_releaseCallSite = new(
        JavaScriptBinaryOperation.ReleaseHandle,
        globalName: null,
        memberName: null,
        JavaScriptBinaryResultMode.Void);
    private static readonly JavaScriptBinaryCallSite s_createCallbackTargetCallSite =
        new(
            JavaScriptBinaryOperation.CreateCallbackTarget,
            globalName: null,
            memberName: null,
            JavaScriptBinaryResultMode.RetainedHandle);
    private static readonly JavaScriptBinaryCallSite s_invokeFunctionCallSite =
        new(
            JavaScriptBinaryOperation.InvokeFunction,
            globalName: null,
            memberName: null,
            JavaScriptBinaryResultMode.Void);
    private static readonly JavaScriptBinaryCallSite s_createCallbackFunctionCallSite =
        new(
            JavaScriptBinaryOperation.CreateCallbackFunction,
            globalName: null,
            memberName: null,
            JavaScriptBinaryResultMode.RetainedHandle);
    private static readonly JavaScriptBinaryCallSite s_createSynchronousFactoryCallSite =
        new(
            JavaScriptBinaryOperation.CreateSynchronousFactory,
            globalName: null,
            memberName: null,
            JavaScriptBinaryResultMode.RetainedHandle);

    private IJavaScriptBinaryTransport? _transport;
    private readonly Func<CancellationToken, ValueTask>? _waitForCallbackAsync;
    private readonly object _callbackGate = new();
    private readonly Dictionary<ulong, IJavaScriptBinaryCallbackTarget>
        _callbackTargets = [];
    private readonly Dictionary<long, ulong> _callbackTargetHandles = [];
    private long _nextCallbackTarget;

    public NativeJavaScriptInvoker(
        IJavaScriptBinaryTransport transport,
        Func<CancellationToken, ValueTask>? waitForCallbackAsync = null)
    {
        _transport = transport
            ?? throw new ArgumentNullException(nameof(transport));
        _waitForCallbackAsync = waitForCallbackAsync;
    }

    public bool IsBinaryInteropAvailable
        => Volatile.Read(ref _transport) is not null;

    public bool SupportsCallbackNotifications
        => _waitForCallbackAsync is not null;

    public ValueTask WaitForCallbackAsync(
        CancellationToken cancellationToken = default)
        => _waitForCallbackAsync?.Invoke(cancellationToken)
           ?? ValueTask.FromException(
               new NotSupportedException(
                   "This native invoker was not configured with a callback signal."));

    public ValueTask<TResult> InvokeBinaryAsync<
        TArguments,
        TResult,
        TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, TResult>
        => RequireTransport().InvokeAsync<
            TArguments,
            TResult,
            TCodec>(
            this,
            callSite,
            target,
            arguments,
            cancellationToken);

    public ValueTask InvokeBinaryVoidAsync<TArguments, TCodec>(
        JavaScriptBinaryCallSite callSite,
        JavaScriptObjectReference target,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        => RequireTransport().InvokeVoidAsync<TArguments, TCodec>(
            this,
            callSite,
            target,
            arguments,
            cancellationToken);

    public ValueTask<JavaScriptBinaryResultLease>
        InvokeBinaryBorrowedAsync<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryArgumentsCodec<TArguments>
        => RequireTransport().InvokeBorrowedAsync<TArguments, TCodec>(
            callSite,
            target,
            arguments,
            cancellationToken);

    public ValueTask<JavaScriptObjectReference> ConstructAsync(
        string globalName,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<JavaScriptObjectReference>();

    public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<JavaScriptObjectReference>();

    public ValueTask<T?> InvokeAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<T?>();

    public ValueTask<T?> InvokePromiseAsync<T>(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<T?>();

    public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<JavaScriptObjectReference>();

    public ValueTask InvokeVoidAsync(
        JavaScriptObjectReference target,
        string method,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw Unsupported();

    public ValueTask ReleaseAsync(
        JavaScriptObjectReference reference,
        CancellationToken cancellationToken = default)
    {
        if (reference.IsEmpty)
        {
            return ValueTask.CompletedTask;
        }
        lock (_callbackGate)
        {
            if (_callbackTargetHandles.Remove(
                    reference.Id,
                    out var targetId))
            {
                _callbackTargets.Remove(targetId);
            }
        }
        return RequireTransport().InvokeVoidAsync<
            JavaScriptBinaryVoid,
            ReleaseCodec>(
            this,
            s_releaseCallSite,
            reference,
            new JavaScriptBinaryVoid(),
            cancellationToken);
    }

    public ValueTask<JavaScriptObjectReference> RegisterCallbackTargetAsync(
        IJavaScriptCallbackTarget target,
        IReadOnlyList<JavaScriptCallbackMethod> methods,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(methods);
        if (target is not IJavaScriptBinaryCallbackTarget binaryTarget)
        {
            return ValueTask.FromException<JavaScriptObjectReference>(
                new NotSupportedException(
                    "Native callback targets require a generated binary dispatcher."));
        }
        var binaryMethods = new JavaScriptBinaryCallbackMethod[methods.Count];
        for (var index = 0; index < methods.Count; index++)
        {
            binaryMethods[index] = new JavaScriptBinaryCallbackMethod(
                methods[index].Name,
                checked((uint)index),
                methods[index].ReturnKind,
                methods[index].ReturnKind
                    == JavaScriptCallbackReturnKind.Synchronous
                && !string.IsNullOrEmpty(
                    methods[index].SynchronousResult.Value));
        }
        return RegisterBinaryCallbackTargetAsync(
            binaryTarget,
            binaryMethods,
            cancellationToken);
    }

    public async ValueTask<JavaScriptObjectReference>
        RegisterBinaryCallbackTargetAsync(
            IJavaScriptBinaryCallbackTarget target,
            IReadOnlyList<JavaScriptBinaryCallbackMethod> methods,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(methods);
        var signedTargetId = Interlocked.Increment(
            ref _nextCallbackTarget);
        if (signedTargetId <= 0)
        {
            throw new InvalidOperationException(
                "The native callback target ID space was exhausted.");
        }
        var targetId = checked((ulong)signedTargetId);
        lock (_callbackGate)
        {
            _callbackTargets.Add(targetId, target);
        }
        try
        {
            var reference = await InvokeBinaryAsync<
                CallbackRegistration,
                JavaScriptObjectReference,
                CallbackRegistrationCodec>(
                s_createCallbackTargetCallSite,
                new JavaScriptObjectReference(signedTargetId),
                new CallbackRegistration(
                    target,
                    methods,
                    cancellationToken),
                cancellationToken).ConfigureAwait(false);
            lock (_callbackGate)
            {
                _callbackTargetHandles.Add(reference.Id, targetId);
            }
            return reference;
        }
        catch
        {
            lock (_callbackGate)
            {
                _callbackTargets.Remove(targetId);
            }
            throw;
        }
    }

    public async ValueTask<JavaScriptFunctionReference>
        RegisterBinaryFunctionAsync(
            IJavaScriptBinaryCallbackTarget target,
            JavaScriptCallbackReturnKind returnKind =
                JavaScriptCallbackReturnKind.Void,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        var targetId = RegisterCallbackTarget(target);
        try
        {
            var reference = await InvokeBinaryAsync<
                JavaScriptCallbackReturnKind,
                JavaScriptObjectReference,
                CallbackFunctionRegistrationCodec>(
                s_createCallbackFunctionCallSite,
                new JavaScriptObjectReference(checked((long)targetId)),
                returnKind,
                cancellationToken).ConfigureAwait(false);
            RegisterCallbackTargetHandle(reference, targetId);
            return new JavaScriptFunctionReference(this, reference);
        }
        catch
        {
            RemoveCallbackTarget(targetId);
            throw;
        }
    }

    public async ValueTask<JavaScriptFunctionReference>
        RegisterBinarySynchronousFactoryAsync(
            JavaScriptObjectReference result,
            IJavaScriptBinaryCallbackTarget target,
            CancellationToken cancellationToken = default)
    {
        if (result.IsEmpty)
        {
            throw new ArgumentException(
                "A non-empty synchronous factory result is required.",
                nameof(result));
        }
        ArgumentNullException.ThrowIfNull(target);
        var targetId = RegisterCallbackTarget(target);
        try
        {
            var reference = await InvokeBinaryAsync<
                JavaScriptObjectReference,
                JavaScriptObjectReference,
                SynchronousFactoryRegistrationCodec>(
                s_createSynchronousFactoryCallSite,
                new JavaScriptObjectReference(checked((long)targetId)),
                result,
                cancellationToken).ConfigureAwait(false);
            RegisterCallbackTargetHandle(reference, targetId);
            return new JavaScriptFunctionReference(this, reference);
        }
        catch
        {
            RemoveCallbackTarget(targetId);
            throw;
        }
    }

    public ValueTask<bool> PumpCallbackAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var transport = RequireTransport()
            as IJavaScriptBinaryCallbackTransport
            ?? throw new NotSupportedException(
                "This native binary transport does not implement the ABI 3 "
                + "callback operations.");
        var lease = transport.TryTakeCallback();
        if (lease is null)
        {
            return new ValueTask<bool>(false);
        }
        return DispatchCallback(lease, cancellationToken);
    }

    public ValueTask InvokeBinaryFunctionVoidAsync<TArguments, TCodec>(
        JavaScriptObjectReference function,
        TArguments arguments,
        CancellationToken cancellationToken = default)
        where TCodec : struct,
        IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
    {
        if (function.IsEmpty)
        {
            return ValueTask.FromException(
                new ArgumentException(
                    "A non-empty JavaScript function reference is required.",
                    nameof(function)));
        }
        return InvokeBinaryVoidAsync<TArguments, TCodec>(
            s_invokeFunctionCallSite,
            function,
            arguments,
            cancellationToken);
    }

    public ValueTask<JavaScriptFunctionReference> RegisterFunctionAsync(
        JavaScriptCallbackHandler callback,
        JavaScriptCallbackReturnKind returnKind =
            JavaScriptCallbackReturnKind.Void,
        CancellationToken cancellationToken = default)
        => ValueTask.FromException<JavaScriptFunctionReference>(
            new NotSupportedException(
                "Native managed functions require a generated binary callback codec."));

    public ValueTask<JavaScriptFunctionReference>
        RegisterSynchronousFactoryAsync(
            JavaScriptObjectReference result,
            JavaScriptCallbackHandler callback,
            CancellationToken cancellationToken = default)
        => ValueTask.FromException<JavaScriptFunctionReference>(
            new NotSupportedException(
                "Native synchronous factories require a generated binary callback codec."));

    public ValueTask<T?> InvokeFunctionAsync<T>(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => Unsupported<T?>();

    public ValueTask InvokeFunctionVoidAsync(
        JavaScriptObjectReference function,
        IReadOnlyList<JavaScriptArgument> arguments,
        CancellationToken cancellationToken = default)
        => throw Unsupported();

    public void Dispose()
    {
        lock (_callbackGate)
        {
            _callbackTargets.Clear();
            _callbackTargetHandles.Clear();
        }
        Interlocked.Exchange(ref _transport, null)?.Dispose();
    }

    private IJavaScriptBinaryTransport RequireTransport()
        => Volatile.Read(ref _transport)
           ?? throw new ObjectDisposedException(nameof(NativeJavaScriptInvoker));

    private ulong RegisterCallbackTarget(
        IJavaScriptBinaryCallbackTarget target)
    {
        var signedTargetId = Interlocked.Increment(
            ref _nextCallbackTarget);
        if (signedTargetId <= 0)
        {
            throw new InvalidOperationException(
                "The native callback target ID space was exhausted.");
        }
        var targetId = checked((ulong)signedTargetId);
        lock (_callbackGate)
        {
            _callbackTargets.Add(targetId, target);
        }
        return targetId;
    }

    private void RegisterCallbackTargetHandle(
        JavaScriptObjectReference reference,
        ulong targetId)
    {
        lock (_callbackGate)
        {
            _callbackTargetHandles.Add(reference.Id, targetId);
        }
    }

    private void RemoveCallbackTarget(ulong targetId)
    {
        lock (_callbackGate)
        {
            _callbackTargets.Remove(targetId);
        }
    }

    private static ValueTask<T> Unsupported<T>()
        => ValueTask.FromException<T>(Unsupported());

    private static NotSupportedException Unsupported()
        => new(
            "Native JavaScript interop requires a generated ABI 3 binary codec; "
            + "the JSON compatibility path has been removed.");

    private ValueTask<bool> DispatchCallback(
        JavaScriptBinaryCallbackLease lease,
        CancellationToken cancellationToken)
    {
        IJavaScriptBinaryCallbackTarget? target;
        lock (_callbackGate)
        {
            _callbackTargets.TryGetValue(lease.TargetId, out target);
        }
        var completion = lease.CreateCompletion();
        ValueTask pending;
        try
        {
            if (target is null)
            {
                throw new InvalidOperationException(
                    $"Unknown native callback target {lease.TargetId}.");
            }
            using var borrowed = lease.Borrow();
            pending = target.DispatchBinaryAsync(
                lease.MethodId,
                borrowed.Arguments,
                completion,
                cancellationToken);
        }
        catch (Exception error)
        {
            try
            {
                if (!completion.IsCompleted)
                {
                    completion.SetException(error);
                }
            }
            finally
            {
                completion.Dispose();
                lease.Dispose();
            }
            return new ValueTask<bool>(true);
        }
        lease.Dispose();
        if (pending.IsCompletedSuccessfully)
        {
            try
            {
                pending.GetAwaiter().GetResult();
                if (!completion.IsCompleted)
                {
                    completion.SetVoid();
                }
            }
            catch (Exception error)
            {
                if (!completion.IsCompleted)
                {
                    completion.SetException(error);
                }
            }
            finally
            {
                completion.Dispose();
            }
            return new ValueTask<bool>(true);
        }
        return AwaitCallback(pending, completion);
    }

    private static async ValueTask<bool> AwaitCallback(
        ValueTask pending,
        JavaScriptBinaryCallbackCompletion completion)
    {
        try
        {
            await pending.ConfigureAwait(false);
            if (!completion.IsCompleted)
            {
                completion.SetVoid();
            }
        }
        catch (Exception error)
        {
            if (!completion.IsCompleted)
            {
                completion.SetException(error);
            }
        }
        finally
        {
            completion.Dispose();
        }
        return true;
    }

    private readonly record struct CallbackRegistration(
        IJavaScriptBinaryCallbackTarget Target,
        IReadOnlyList<JavaScriptBinaryCallbackMethod> Methods,
        CancellationToken CancellationToken);

    private readonly struct CallbackRegistrationCodec :
        IJavaScriptBinaryCodec<
            CallbackRegistration,
            JavaScriptObjectReference>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in CallbackRegistration arguments)
        {
            var root = writer.BeginArray(1);
            var methods = writer.BeginArray(arguments.Methods.Count);
            writer.SetArrayItem(root, 0, methods);
            for (var index = 0; index < arguments.Methods.Count; index++)
            {
                var method = arguments.Methods[index];
                var descriptor = writer.BeginArray(
                    method.HasSynchronousResult ? 4 : 3);
                writer.SetArrayItem(methods, index, descriptor);
                writer.SetArrayItem(
                    descriptor,
                    0,
                    writer.WriteString(method.Name));
                writer.SetArrayItem(
                    descriptor,
                    1,
                    writer.WriteNumber(method.MethodId));
                writer.SetArrayItem(
                    descriptor,
                    2,
                    writer.WriteNumber((uint)method.ReturnKind));
                if (method.HasSynchronousResult)
                {
                    writer.SetArrayItem(
                        descriptor,
                        3,
                        arguments.Target.EncodeSynchronousResult(
                            method.MethodId,
                            ref writer,
                            arguments.CancellationToken));
                }
            }
            return root;
        }

        public static JavaScriptObjectReference DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetHandle();
    }

    private readonly struct ReleaseCodec :
        IJavaScriptBinaryCodec<
            JavaScriptBinaryVoid,
            JavaScriptBinaryVoid>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static JavaScriptBinaryVoid DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => new();
    }

    private readonly struct CallbackFunctionRegistrationCodec :
        IJavaScriptBinaryCodec<
            JavaScriptCallbackReturnKind,
            JavaScriptObjectReference>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptCallbackReturnKind arguments)
        {
            var root = writer.BeginArray(1);
            writer.SetArrayItem(
                root,
                0,
                writer.WriteNumber((uint)arguments));
            return root;
        }

        public static JavaScriptObjectReference DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetHandle();
    }

    private readonly struct SynchronousFactoryRegistrationCodec :
        IJavaScriptBinaryCodec<
            JavaScriptObjectReference,
            JavaScriptObjectReference>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptObjectReference arguments)
        {
            var root = writer.BeginArray(1);
            writer.SetArrayItem(
                root,
                0,
                writer.WriteHandle(arguments));
            return root;
        }

        public static JavaScriptObjectReference DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetHandle();
    }
}

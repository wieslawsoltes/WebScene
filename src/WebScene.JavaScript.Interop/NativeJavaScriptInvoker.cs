namespace WebScene.JavaScript.Interop;

/// <summary>
/// Forward-only native JavaScript invoker backed by the ABI 3 tagged binary
/// transport. Generated members whose shapes do not have binary codecs are
/// unsupported instead of falling back to source generation or JSON.
/// </summary>
public sealed class NativeJavaScriptInvoker :
    IJavaScriptBinaryInvoker,
    IDisposable
{
    private static readonly JavaScriptBinaryCallSite s_releaseCallSite = new(
        JavaScriptBinaryOperation.ReleaseHandle,
        globalName: null,
        memberName: null,
        JavaScriptBinaryResultMode.Void);

    private IJavaScriptBinaryTransport? _transport;

    public NativeJavaScriptInvoker(IJavaScriptBinaryTransport transport)
    {
        _transport = transport
            ?? throw new ArgumentNullException(nameof(transport));
    }

    public bool IsBinaryInteropAvailable
        => Volatile.Read(ref _transport) is not null;

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
        return RequireTransport().InvokeVoidAsync<
            JavaScriptBinaryVoid,
            ReleaseCodec>(
            this,
            s_releaseCallSite,
            reference,
            new JavaScriptBinaryVoid(),
            cancellationToken);
    }

    public void Dispose()
        => Interlocked.Exchange(ref _transport, null)?.Dispose();

    private IJavaScriptBinaryTransport RequireTransport()
        => Volatile.Read(ref _transport)
           ?? throw new ObjectDisposedException(nameof(NativeJavaScriptInvoker));

    private static ValueTask<T> Unsupported<T>()
        => ValueTask.FromException<T>(Unsupported());

    private static NotSupportedException Unsupported()
        => new(
            "Native JavaScript interop requires a generated ABI 3 binary codec; "
            + "the JSON compatibility path has been removed.");

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
}

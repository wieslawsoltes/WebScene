using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class NativeJavaScriptInvokerTests
{
    private static readonly JavaScriptBinaryCallSite s_valueCallSite = new(
        JavaScriptBinaryOperation.InvokeGlobal,
        "sample.read",
        memberName: null,
        JavaScriptBinaryResultMode.Value);

    [Fact]
    public async Task NativeInvokerForwardsOnlyTaggedBinaryCalls()
    {
        var transport = new RecordingTransport();
        using var invoker = new NativeJavaScriptInvoker(transport);

        var result = await invoker.InvokeBinaryAsync<
            JavaScriptBinaryVoid,
            double,
            NumberCodec>(
            s_valueCallSite,
            default,
            new JavaScriptBinaryVoid());

        Assert.Equal(42, result);
        Assert.Same(s_valueCallSite, transport.LastCallSite);
        Assert.True(invoker.IsBinaryInteropAvailable);
    }

    [Fact]
    public async Task NativeInvokerRejectsTheRemovedJsonCompatibilitySurface()
    {
        using var invoker = new NativeJavaScriptInvoker(
            new RecordingTransport());

        var error = await Assert.ThrowsAsync<NotSupportedException>(
            async () => await invoker.InvokeAsync<double>(
                new JavaScriptObjectReference(1),
                "legacy",
                []));

        Assert.Contains("JSON compatibility path has been removed", error.Message);
    }

    [Fact]
    public async Task NativeInvokerReleasesHandlesThroughAbi3AndOwnsTransport()
    {
        var transport = new RecordingTransport();
        var invoker = new NativeJavaScriptInvoker(transport);

        await invoker.ReleaseAsync(default);
        Assert.Equal(0, transport.VoidCalls);

        await invoker.ReleaseAsync(new JavaScriptObjectReference(71));
        Assert.Equal(1, transport.VoidCalls);
        Assert.Equal(
            JavaScriptBinaryOperation.ReleaseHandle,
            transport.LastCallSite?.Operation);
        Assert.Equal(71, transport.LastTarget.Id);

        invoker.Dispose();
        Assert.True(transport.Disposed);
        Assert.False(invoker.IsBinaryInteropAvailable);
        Assert.Throws<ObjectDisposedException>(() =>
            invoker.InvokeBinaryAsync<
                JavaScriptBinaryVoid,
                double,
                NumberCodec>(
                s_valueCallSite,
                default,
                new JavaScriptBinaryVoid()));
    }

    private readonly struct NumberCodec :
        IJavaScriptBinaryCodec<JavaScriptBinaryVoid, double>
    {
        public static uint EncodeArguments(
            ref JavaScriptBinaryWriter writer,
            in JavaScriptBinaryVoid arguments)
            => writer.BeginArray(0);

        public static double DecodeResult(
            JavaScriptBinaryValue value,
            IJavaScriptInvoker invoker)
            => value.GetNumber();
    }

    private sealed class RecordingTransport : IJavaScriptBinaryTransport
    {
        public JavaScriptBinaryCallSite? LastCallSite { get; private set; }

        public JavaScriptObjectReference LastTarget { get; private set; }

        public int VoidCalls { get; private set; }

        public bool Disposed { get; private set; }

        public ValueTask<TResult> InvokeAsync<
            TArguments,
            TResult,
            TCodec>(
            IJavaScriptInvoker invoker,
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, TResult>
        {
            LastCallSite = callSite;
            LastTarget = target;
            return ValueTask.FromResult((TResult)(object)42d);
        }

        public ValueTask InvokeVoidAsync<TArguments, TCodec>(
            IJavaScriptInvoker invoker,
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            LastCallSite = callSite;
            LastTarget = target;
            VoidCalls++;
            return ValueTask.CompletedTask;
        }

        public ValueTask<JavaScriptBinaryResultLease>
            InvokeBorrowedAsync<TArguments, TCodec>(
                JavaScriptBinaryCallSite callSite,
                JavaScriptObjectReference target,
                TArguments arguments,
                CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryArgumentsCodec<TArguments>
            => throw new NotSupportedException();

        public void Dispose() => Disposed = true;
    }
}

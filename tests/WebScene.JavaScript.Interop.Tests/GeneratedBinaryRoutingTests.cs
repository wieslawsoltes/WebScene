using TradingViewInterop.Generated;
using WebScene.JavaScript.Interop;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class GeneratedBinaryRoutingTests
{
    [Fact]
    public async Task BinarySupportedMembersNeverFallBackToJsonInvokerMethods()
    {
        var invoker = new BinaryRecordingInvoker();
        var widget = TradingViewWidget.FromReference(
            invoker,
            new JavaScriptObjectReference(41));

        var chart = await widget.ActiveChartAsync();

        Assert.Equal(JavaScriptBinaryOperation.InvokeMember, invoker.Operation);
        Assert.Equal("activeChart", invoker.MemberName);
        Assert.Equal(41, invoker.Target.Id);
        Assert.Empty(invoker.ArgumentKinds);

        await chart.SetZoomEnabledAsync(false);

        Assert.Equal(JavaScriptBinaryOperation.InvokeMember, invoker.Operation);
        Assert.Equal("setZoomEnabled", invoker.MemberName);
        Assert.Equal(42, invoker.Target.Id);
        Assert.Equal([JavaScriptBinaryValueKind.Boolean], invoker.ArgumentKinds);
        Assert.False(invoker.FirstBooleanArgument);
        Assert.Equal(2, invoker.BinaryCalls);
        Assert.Equal(0, invoker.JsonCalls);
    }

    [Fact]
    public async Task BinarySupportedMemberRejectsAnInvokerWithoutAbi3Transport()
    {
        var widget = TradingViewWidget.FromReference(
            new JsonOnlyInvoker(),
            new JavaScriptObjectReference(41));

        var error = await Assert.ThrowsAsync<NotSupportedException>(
            async () => await widget.ActiveChartAsync());

        Assert.Contains("binary ABI 3", error.Message);
    }

    private sealed class BinaryRecordingInvoker : IJavaScriptBinaryInvoker
    {
        public JavaScriptBinaryOperation Operation { get; private set; }

        public string? MemberName { get; private set; }

        public JavaScriptObjectReference Target { get; private set; }

        public JavaScriptBinaryValueKind[] ArgumentKinds { get; private set; } = [];

        public bool FirstBooleanArgument { get; private set; }

        public int BinaryCalls { get; private set; }

        public int JsonCalls { get; private set; }

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
        {
            Record<TArguments, TCodec>(callSite, target, in arguments);
            object result = typeof(TResult) == typeof(TradingViewChart)
                ? TradingViewChart.FromReference(
                    this,
                    new JavaScriptObjectReference(42))
                : throw new NotSupportedException(
                    $"Unexpected binary test result {typeof(TResult)}.");
            return ValueTask.FromResult((TResult)result);
        }

        public ValueTask InvokeBinaryVoidAsync<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            Record<TArguments, TCodec>(callSite, target, in arguments);
            return ValueTask.CompletedTask;
        }

        public ValueTask<JavaScriptBinaryResultLease>
            InvokeBinaryBorrowedAsync<TArguments, TCodec>(
                JavaScriptBinaryCallSite callSite,
                JavaScriptObjectReference target,
                TArguments arguments,
                CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryArgumentsCodec<TArguments>
            => throw new NotSupportedException();

        private void Record<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            in TArguments arguments)
            where TCodec : struct,
            IJavaScriptBinaryArgumentsCodec<TArguments>
        {
            BinaryCalls++;
            Operation = callSite.Operation;
            MemberName = callSite.MemberNameUtf8 is null
                ? null
                : System.Text.Encoding.UTF8.GetString(
                    callSite.MemberNameUtf8);
            Target = target;

            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = TCodec.EncodeArguments(ref writer, in arguments);
                var rootValue = writer.Values[checked((int)root)];
                var kinds = new JavaScriptBinaryValueKind[
                    checked((int)rootValue.Length)];
                for (var index = 0U; index < rootValue.Length; index++)
                {
                    var edge = writer.Edges[
                        checked((int)(rootValue.Offset + index))];
                    var value = writer.Values[checked((int)edge.ValueIndex)];
                    kinds[index] = value.Kind;
                    if (index == 0
                        && value.Kind == JavaScriptBinaryValueKind.Boolean)
                    {
                        FirstBooleanArgument = value.Payload != 0;
                    }
                }
                ArgumentKinds = kinds;
            }
            finally
            {
                writer.Dispose();
            }
        }

        public ValueTask<JavaScriptObjectReference> ConstructAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => JsonCall<JavaScriptObjectReference>();

        public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => JsonCall<JavaScriptObjectReference>();

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => JsonCall<T?>();

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => JsonCall<T?>();

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => JsonCall<JavaScriptObjectReference>();

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            JsonCalls++;
            throw new InvalidOperationException("JSON fallback was invoked.");
        }

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        private ValueTask<T> JsonCall<T>()
        {
            JsonCalls++;
            throw new InvalidOperationException("JSON fallback was invoked.");
        }
    }

    private sealed class JsonOnlyInvoker : IJavaScriptInvoker
    {
        public ValueTask<JavaScriptObjectReference> ConstructAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("JSON fallback was invoked.");

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("JSON fallback was invoked.");

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("JSON fallback was invoked.");

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("JSON fallback was invoked.");

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new InvalidOperationException("JSON fallback was invoked.");

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;
    }
}

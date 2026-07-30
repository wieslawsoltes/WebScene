using WebScene.JavaScript.Interop;
using TradingViewInterop.Generated;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class GeneratedTradingViewBindingTests
{
    [Fact]
    public async Task GeneratedFacadeMapsNativeAsyncCallsAndPromiseResults()
    {
        var invoker = new RecordingInvoker();
        await using var widget = await TradingViewWidget.CreateAsync(
            invoker,
            new TradingViewWidgetOptions
            {
                Container = "chart",
                Symbol = "NASDAQ:AAPL",
                Interval = "1D",
                Locale = "en",
                Autosize = true
            });

        Assert.Equal("TradingView.widget", invoker.LastGlobalName);
        Assert.Contains("\"symbol\":\"NASDAQ:AAPL\"", invoker.LastArguments.Single().Json);
        Assert.Contains("\"autosize\":true", invoker.LastArguments.Single().Json);

        await using var chart = await widget.ActiveChartAsync();
        Assert.Equal("activeChart", invoker.LastMethod);

        invoker.NextValue = "NASDAQ:AAPL";
        Assert.Equal("NASDAQ:AAPL", await chart.SymbolAsync());
        Assert.Equal("symbol", invoker.LastMethod);

        invoker.NextValue = "1D";
        Assert.Equal("1D", await chart.ResolutionAsync());
        Assert.Equal("resolution", invoker.LastMethod);

        await chart.SetZoomEnabledAsync(false);
        Assert.Equal("setZoomEnabled", invoker.LastMethod);
        Assert.Equal("false", invoker.LastArguments.Single().Json);

        await chart.SetScrollEnabledAsync(true);
        Assert.Equal("setScrollEnabled", invoker.LastMethod);
        Assert.Equal("true", invoker.LastArguments.Single().Json);

        invoker.NextValue = true;
        Assert.True(await chart.SetSymbolAsync("NYSE:IBM"));
        Assert.True(invoker.LastInvocationWasPromise);
        Assert.Equal("setSymbol", invoker.LastMethod);
        Assert.Equal(2, invoker.LastArguments.Count);
        Assert.Equal("""{"__webSceneUndefined":true}""", invoker.LastArguments[1].Json);

        await using var orderLine = await chart.CreateOrderLineAsync();
        Assert.True(invoker.LastInvocationWasPromise);
        Assert.Equal("createOrderLine", invoker.LastMethod);

        await using var configuredOrderLine = await orderLine.SetPriceAsync(185.25);
        Assert.Equal("setPrice", invoker.LastMethod);
        Assert.Equal("185.25", invoker.LastArguments.Single().Json);

        invoker.NextValue = "line-1";
        Assert.Equal("line-1", await orderLine.GetIdAsync());
        Assert.Equal("id", invoker.LastProperty);

        await orderLine.SetPricePropertyAsync(186.5);
        Assert.Equal("price", invoker.LastProperty);
        Assert.Equal("186.5", invoker.LastArguments.Single().Json);

        await using var watchedPrice = await chart.CrosshairPriceAsync();
        invoker.NextValue = 187.75;
        Assert.Equal(187.75, await watchedPrice.ValueAsync());
        await watchedPrice.SetValueAsync(188.0);
        Assert.Equal("setValue", invoker.LastMethod);

        await chart.SetVisibleStudiesAsync(["Volume", "MACD"]);
        Assert.Contains(
            "\"__webSceneRest\":[\"Volume\",\"MACD\"]",
            invoker.LastArguments.Single().Json);

        await widget.RemoveAsync();
        Assert.Equal("remove", invoker.LastMethod);
    }

    private sealed class RecordingInvoker : IJavaScriptBinaryInvoker
    {
        private long _nextHandle;

        public string? LastGlobalName { get; private set; }
        public string? LastMethod { get; private set; }
        public string? LastProperty { get; private set; }
        public IReadOnlyList<JavaScriptArgument> LastArguments { get; private set; } = [];
        public object? NextValue { get; set; }
        public bool LastInvocationWasPromise { get; private set; }

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
            RecordBinary<TArguments, TCodec>(callSite, in arguments);
            object? result = typeof(TResult) switch
            {
                var type when type == typeof(TradingViewChart) =>
                    TradingViewChart.FromReference(
                        this,
                        new JavaScriptObjectReference(++_nextHandle)),
                var type when type == typeof(TradingViewOrderLine) =>
                    TradingViewOrderLine.FromReference(
                        this,
                        new JavaScriptObjectReference(++_nextHandle)),
                var type when type == typeof(TradingViewWatchedValue<double>) =>
                    TradingViewWatchedValue<double>.FromReference(
                        this,
                        new JavaScriptObjectReference(++_nextHandle)),
                _ => NextValue
            };
            return ValueTask.FromResult((TResult)result!);
        }

        public ValueTask InvokeBinaryVoidAsync<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            RecordBinary<TArguments, TCodec>(callSite, in arguments);
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

        private unsafe void RecordBinary<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            in TArguments arguments)
            where TCodec : struct,
            IJavaScriptBinaryArgumentsCodec<TArguments>
        {
            LastMethod = null;
            LastProperty = null;
            LastInvocationWasPromise =
                (callSite.Flags & JavaScriptBinaryCallFlags.AwaitPromise) != 0;
            var name = callSite.MemberNameUtf8 is null
                ? null
                : System.Text.Encoding.UTF8.GetString(
                    callSite.MemberNameUtf8);
            if (callSite.Operation
                is JavaScriptBinaryOperation.GetProperty
                    or JavaScriptBinaryOperation.SetProperty)
            {
                LastProperty = name;
            }
            else
            {
                LastMethod = name;
            }

            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = TCodec.EncodeArguments(ref writer, in arguments);
                fixed (JavaScriptBinaryValueData* values = writer.Values)
                fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                fixed (byte* utf8 = writer.Utf8)
                {
                    var rootValue = new JavaScriptBinaryValue(
                        values,
                        checked((uint)writer.Values.Length),
                        edges,
                        checked((uint)writer.Edges.Length),
                        utf8,
                        checked((uint)writer.Utf8.Length),
                        root);
                    var converted =
                        new JavaScriptArgument[rootValue.Count];
                    for (var index = 0; index < rootValue.Count; index++)
                    {
                        converted[index] = ToArgument(
                            rootValue.GetArrayItem(index));
                    }
                    LastArguments = converted;
                }
            }
            finally
            {
                writer.Dispose();
            }
        }

        private static JavaScriptArgument ToArgument(
            JavaScriptBinaryValue value)
            => value.Kind switch
            {
                JavaScriptBinaryValueKind.Undefined =>
                    JavaScriptArgument.Undefined,
                JavaScriptBinaryValueKind.Null =>
                    JavaScriptArgument.From<object?>(null),
                JavaScriptBinaryValueKind.Boolean =>
                    JavaScriptArgument.From(value.GetBoolean()),
                JavaScriptBinaryValueKind.Number =>
                    JavaScriptArgument.From(value.GetNumber()),
                JavaScriptBinaryValueKind.String =>
                    JavaScriptArgument.From(value.GetString()),
                JavaScriptBinaryValueKind.Handle =>
                    JavaScriptArgument.From(value.GetHandle()),
                _ => throw new NotSupportedException(
                    $"The test does not materialize {value.Kind} arguments.")
            };

        public ValueTask<JavaScriptObjectReference> ConstructAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            LastGlobalName = globalName;
            LastArguments = arguments;
            return ValueTask.FromResult(new JavaScriptObjectReference(++_nextHandle));
        }

        public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Record(method, arguments);
            return ValueTask.FromResult(new JavaScriptObjectReference(++_nextHandle));
        }

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Record(method, arguments);
            return ValueTask.FromResult((T?)NextValue);
        }

        public ValueTask<JavaScriptObjectReference> GetObjectPropertyAsync(
            JavaScriptObjectReference target,
            string property,
            CancellationToken cancellationToken = default)
        {
            LastProperty = property;
            return ValueTask.FromResult(new JavaScriptObjectReference(++_nextHandle));
        }

        public ValueTask<T?> GetPropertyAsync<T>(
            JavaScriptObjectReference target,
            string property,
            CancellationToken cancellationToken = default)
        {
            LastProperty = property;
            return ValueTask.FromResult((T?)NextValue);
        }

        public ValueTask SetPropertyAsync(
            JavaScriptObjectReference target,
            string property,
            JavaScriptArgument value,
            CancellationToken cancellationToken = default)
        {
            LastProperty = property;
            LastArguments = [value];
            return ValueTask.CompletedTask;
        }

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Record(method, arguments);
            LastInvocationWasPromise = true;
            return ValueTask.FromResult((T?)NextValue);
        }

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Record(method, arguments);
            LastInvocationWasPromise = true;
            return ValueTask.FromResult(new JavaScriptObjectReference(++_nextHandle));
        }

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Record(method, arguments);
            return ValueTask.CompletedTask;
        }

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.CompletedTask;
        }

        private void Record(string method, IReadOnlyList<JavaScriptArgument> arguments)
        {
            LastMethod = method;
            LastArguments = arguments;
            LastInvocationWasPromise = false;
        }
    }
}

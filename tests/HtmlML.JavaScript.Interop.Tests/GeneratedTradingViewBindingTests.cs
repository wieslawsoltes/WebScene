using HtmlML.JavaScript.Interop;
using TradingViewInterop.Generated;
using Xunit;

namespace HtmlML.JavaScript.Interop.Tests;

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
        Assert.Equal("""{"__htmlMlUndefined":true}""", invoker.LastArguments[1].Json);

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
            "\"__htmlMlRest\":[\"Volume\",\"MACD\"]",
            invoker.LastArguments.Single().Json);

        await widget.RemoveAsync();
        Assert.Equal("remove", invoker.LastMethod);
    }

    private sealed class RecordingInvoker : IJavaScriptInvoker
    {
        private long _nextHandle;

        public string? LastGlobalName { get; private set; }
        public string? LastMethod { get; private set; }
        public string? LastProperty { get; private set; }
        public IReadOnlyList<JavaScriptArgument> LastArguments { get; private set; } = [];
        public object? NextValue { get; set; }
        public bool LastInvocationWasPromise { get; private set; }

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

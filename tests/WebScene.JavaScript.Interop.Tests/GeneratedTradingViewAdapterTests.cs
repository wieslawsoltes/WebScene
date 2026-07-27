using System.Text.Json;
using WebScene.JavaScript.Interop;
using TradingViewInterop.Generated;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class GeneratedTradingViewAdapterTests
{
    [Fact]
    public void OptionalCallbackArgumentsPreserveUndefinedAndNull()
    {
        var invoker = new RecordingBidirectionalInvoker();
        using var arguments = JsonDocument.Parse(
            """[7,null,{"__webSceneUndefined":true},{"__webSceneHandle":42}]""");

        var value = JavaScriptCallbackArguments.GetOptional<double>(
            arguments.RootElement,
            0,
            invoker);
        var explicitNull = JavaScriptCallbackArguments.GetOptional<string?>(
            arguments.RootElement,
            1,
            invoker);
        var undefined = JavaScriptCallbackArguments.GetOptional<double>(
            arguments.RootElement,
            2,
            invoker);
        var missing = JavaScriptCallbackArguments.GetOptional<double>(
            arguments.RootElement,
            5,
            invoker);
        var function = JavaScriptCallbackArguments
            .GetOptional<JavaScriptFunctionReference>(
                arguments.RootElement,
                3,
                invoker);

        Assert.True(value.HasValue);
        Assert.Equal(7, value.Value);
        Assert.True(explicitNull.HasValue);
        Assert.Null(explicitNull.Value);
        Assert.False(undefined.HasValue);
        Assert.False(missing.HasValue);
        Assert.True(function.HasValue);
        Assert.Equal(42, function.Value!.Reference.Id);
    }

    [Fact]
    public async Task TupleActionPreservesEveryWideCallbackArgument()
    {
        var invoker = new RecordingBidirectionalInvoker();
        await using var action =
            new JavaScriptTupleAction<(string, double, bool, string, double)>(
                invoker,
                new JavaScriptFunctionReference(
                    invoker,
                    new JavaScriptObjectReference(43)));

        await action.InvokeAsync(("one", 2, true, "four", 5));

        Assert.Equal(43, invoker.LastFunction.Id);
        Assert.Equal(
            ["\"one\"", "2", "true", "\"four\"", "5"],
            invoker.LastArguments.Select(argument => argument.Json));
    }

    [Fact]
    public async Task DatafeedAdapterDispatchesTradingViewCallbacksAndSerializesAsHandle()
    {
        var invoker = new RecordingBidirectionalInvoker();
        await using var datafeed = new TestDatafeed();

        var reference = await datafeed.RegisterAsync(invoker);
        Assert.Equal(99, reference.Id);
        Assert.Contains(invoker.Methods, method => method.Name == "getBars");
        Assert.Contains(invoker.Methods, method => method.Name == "subscribeQuotes");

        using var arguments = JsonDocument.Parse("""[{"__webSceneHandle":42,"__webSceneKind":"function"}]""");
        await invoker.Target!.DispatchAsync(
            "onReady",
            arguments.RootElement,
            CancellationToken.None);

        Assert.Equal(42, invoker.LastFunction.Id);
        Assert.Contains(
            "\"supported_resolutions\":[\"1D\"]",
            invoker.LastArguments.Single().Json);

        var options = new TradingViewWidgetOptions
        {
            Container = "chart",
            Symbol = "NASDAQ:AAPL",
            Interval = "1D",
            Locale = "en",
            Datafeed = datafeed
        };
        Assert.Contains(
            "\"datafeed\":{\"__webSceneHandle\":99}",
            JavaScriptArgument.From(options).Json);
    }

    [Fact]
    public async Task BrokerAdapterPublishesPromiseAndSynchronousMethodMetadata()
    {
        var invoker = new RecordingBidirectionalInvoker();
        await using var broker = new TestBroker();
        await broker.RegisterAsync(invoker);

        Assert.Contains(
            invoker.Methods,
            method => method is
            {
                Name: "orders",
                ReturnKind: JavaScriptCallbackReturnKind.Promise
            });
        Assert.Contains(
            invoker.Methods,
            method => method is
            {
                Name: "accountManagerInfo",
                ReturnKind: JavaScriptCallbackReturnKind.Synchronous
            });
        var accountInfo = Assert.Single(
            invoker.Methods,
            method => method.Name == "accountManagerInfo");
        Assert.Contains("\"accountTitle\":\"Primary\"", accountInfo.SynchronousResult.Value);

        using var noArguments = JsonDocument.Parse("[]");
        var result = await invoker.Target!.DispatchAsync(
            "orders",
            noArguments.RootElement,
            CancellationToken.None);
        var orders = Assert.IsAssignableFrom<IReadOnlyList<Order>>(result);
        Assert.Single(orders);
        Assert.Equal("order-1", orders[0].Id);

        TradingViewBrokerHost? host = null;
        await using var factory = await invoker.RegisterSynchronousFactoryAsync(
            broker.JavaScriptReference,
            (arguments, _) =>
            {
                var reference = JavaScriptCallbackArguments.Get<JavaScriptObjectReference>(
                    arguments,
                    0,
                    invoker);
                host = TradingViewBrokerHost.FromReference(invoker, reference);
                return ValueTask.FromResult<object?>(null);
            });
        using var hostArguments = JsonDocument.Parse("""[{"__webSceneHandle":77}]""");
        await invoker.FactoryCallback!(
            hostArguments.RootElement,
            CancellationToken.None);

        Assert.Equal(99, invoker.FactoryResult.Id);
        Assert.Equal(101, factory.Reference.Id);
        Assert.Equal(77, host!.JavaScriptReference.Id);
    }

    private sealed class TestDatafeed : TradingViewDatafeed
    {
        public override async ValueTask OnReadyAsync(
            JavaScriptAction<DatafeedConfiguration> callback,
            CancellationToken cancellationToken = default)
        {
            await callback.InvokeAsync(
                new DatafeedConfiguration
                {
                    SupportedResolutions = ["1D"]
                },
                cancellationToken);
        }

        public override ValueTask GetBarsAsync(
            LibrarySymbolInfo symbolInfo,
            string resolution,
            PeriodParams periodParams,
            JavaScriptAction<IReadOnlyList<Bar>, HistoryMetadata?> onResult,
            JavaScriptAction<string> onError,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask GetQuotesAsync(
            IReadOnlyList<string> symbols,
            JavaScriptAction<IReadOnlyList<QuoteData>> onDataCallback,
            JavaScriptAction<string> onErrorCallback,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask ResolveSymbolAsync(
            string symbolName,
            JavaScriptAction<LibrarySymbolInfo> onResolve,
            JavaScriptAction<string> onError,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask SearchSymbolsAsync(
            string userInput,
            string exchange,
            string symbolType,
            JavaScriptAction<IReadOnlyList<SearchSymbolResultItem>> onResult,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask SubscribeBarsAsync(
            LibrarySymbolInfo symbolInfo,
            string resolution,
            JavaScriptAction<Bar> onRealtimeCallback,
            string subscriberUID,
            JavaScriptAction onResetCacheNeededCallback,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask SubscribeQuotesAsync(
            IReadOnlyList<string> symbols,
            IReadOnlyList<string> fastSymbols,
            JavaScriptAction<IReadOnlyList<QuoteData>> onRealtimeCallback,
            string listenerGuid,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask UnsubscribeBarsAsync(
            string subscriberUID,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask UnsubscribeQuotesAsync(
            string listenerGuid,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;
    }

    private sealed class TestBroker : TradingViewBroker
    {
        public override AccountManagerInfo AccountManagerInfo(
            CancellationToken cancellationToken = default)
            => new()
            {
                AccountTitle = "Primary",
                Summary = new()
                {
                    AccountBalance = 10_000,
                    Equity = 10_250
                }
            };

        public override ValueTask CancelOrderAsync(
            string orderId,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask ClosePositionAsync(
            string positionId,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask<IReadOnlyList<Execution>> ExecutionsAsync(
            string symbol,
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult<IReadOnlyList<Execution>>([]);

        public override ValueTask ModifyOrderAsync(
            Order order,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        public override ValueTask<IReadOnlyList<Order>> OrdersAsync(
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult<IReadOnlyList<Order>>(
            [
                new()
                {
                    Id = "order-1",
                    Symbol = "NASDAQ:AAPL",
                    Qty = 10,
                    Side = 1,
                    Status = "working"
                }
            ]);

        public override ValueTask<OrderResult> PlaceOrderAsync(
            PreOrder order,
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult(new OrderResult { OrderId = "order-2" });

        public override ValueTask<IReadOnlyList<Position>> PositionsAsync(
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult<IReadOnlyList<Position>>([]);

        public override ValueTask ReversePositionAsync(
            string positionId,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;
    }

    private sealed class RecordingBidirectionalInvoker : IJavaScriptBidirectionalInvoker
    {
        public IJavaScriptCallbackTarget? Target { get; private set; }
        public IReadOnlyList<JavaScriptCallbackMethod> Methods { get; private set; } = [];
        public JavaScriptObjectReference LastFunction { get; private set; }
        public IReadOnlyList<JavaScriptArgument> LastArguments { get; private set; } = [];
        public JavaScriptObjectReference FactoryResult { get; private set; }
        public JavaScriptCallbackHandler? FactoryCallback { get; private set; }

        public ValueTask<JavaScriptObjectReference> RegisterCallbackTargetAsync(
            IJavaScriptCallbackTarget target,
            IReadOnlyList<JavaScriptCallbackMethod> methods,
            CancellationToken cancellationToken = default)
        {
            Target = target;
            Methods = methods;
            return ValueTask.FromResult(new JavaScriptObjectReference(99));
        }

        public ValueTask InvokeFunctionVoidAsync(
            JavaScriptObjectReference function,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            LastFunction = function;
            LastArguments = arguments;
            return ValueTask.CompletedTask;
        }

        public ValueTask<T?> InvokeFunctionAsync<T>(
            JavaScriptObjectReference function,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult(default(T));

        public ValueTask<JavaScriptFunctionReference> RegisterFunctionAsync(
            JavaScriptCallbackHandler callback,
            JavaScriptCallbackReturnKind returnKind = JavaScriptCallbackReturnKind.Void,
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult(new JavaScriptFunctionReference(
                this,
                new JavaScriptObjectReference(100)));

        public ValueTask<JavaScriptFunctionReference> RegisterSynchronousFactoryAsync(
            JavaScriptObjectReference result,
            JavaScriptCallbackHandler callback,
            CancellationToken cancellationToken = default)
        {
            FactoryResult = result;
            FactoryCallback = callback;
            return ValueTask.FromResult(new JavaScriptFunctionReference(
                this,
                new JavaScriptObjectReference(101)));
        }

        public ValueTask<bool> PumpCallbackAsync(
            CancellationToken cancellationToken = default)
            => ValueTask.FromResult(false);

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
            => throw new NotSupportedException();

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
            => throw new NotSupportedException();

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;
    }
}

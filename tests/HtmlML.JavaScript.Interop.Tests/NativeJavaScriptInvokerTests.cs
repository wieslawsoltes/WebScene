using System.Text.Json;
using HtmlML.JavaScript.Interop;
using Xunit;

namespace HtmlML.JavaScript.Interop.Tests;

public sealed class NativeJavaScriptInvokerTests
{
    [Fact]
    public async Task UsesNativeEvaluateJsonContractAndPollsPromiseResults()
    {
        var evaluations = new List<(string Source, string Document)>();
        var promisePolls = 0;
        var invoker = new NativeJavaScriptInvoker(
            (source, document, _) =>
            {
                evaluations.Add((source, document));
                var result = document switch
                {
                    "htmlml-native-dotnet-interop.js" => "true",
                    "htmlml-interop-construct.js" => "11",
                    "htmlml-interop-object.js" => "12",
                    "htmlml-interop-value.js" => "\"NASDAQ:AAPL\"",
                    "htmlml-interop-void.js" => "true",
                    "htmlml-interop-promise.js" => "17",
                    "htmlml-interop-promise-result.js" when promisePolls++ == 0
                        => """{"status":"pending"}""",
                    "htmlml-interop-promise-result.js"
                        => """{"status":"fulfilled","value":true}""",
                    "htmlml-interop-release.js" => "true",
                    _ => throw new InvalidOperationException(document)
                };
                return Task.FromResult(result);
            },
            promisePollInterval: TimeSpan.Zero);

        var widget = await invoker.ConstructAsync(
            "TradingView.widget",
            [JavaScriptArgument.From(new { symbol = "NASDAQ:AAPL" })]);
        var chart = await invoker.InvokeObjectAsync(widget, "activeChart", []);
        var symbol = await invoker.InvokeAsync<string>(chart, "symbol", []);
        await invoker.InvokeVoidAsync(
            chart,
            "setZoomEnabled",
            [JavaScriptArgument.From(false)]);
        var changed = await invoker.InvokePromiseAsync<bool>(
            chart,
            "setSymbol",
            [JavaScriptArgument.From("NYSE:IBM")]);
        await invoker.ReleaseAsync(chart);

        Assert.Equal(11, widget.Id);
        Assert.Equal(12, chart.Id);
        Assert.Equal("NASDAQ:AAPL", symbol);
        Assert.True(changed);
        Assert.Single(evaluations.Where(item =>
            item.Document == "htmlml-native-dotnet-interop.js"));
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-promise.js"
            && item.Source.Contains("\"setSymbol\"", StringComparison.Ordinal));
        Assert.Equal(2, promisePolls);
    }

    [Fact]
    public async Task RegistersAndPumpsReverseCallbackTargets()
    {
        var completed = false;
        var target = new RecordingTarget();
        var invoker = new NativeJavaScriptInvoker((source, document, _) =>
        {
            var result = document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-register-callback.js" => "51",
                "htmlml-interop-take-callback.js" =>
                    """{"call":7,"target":1,"method":"getBars","arguments":["AAPL"]}""",
                "htmlml-interop-complete-callback.js" => Complete(),
                _ => throw new InvalidOperationException(document)
            };
            return Task.FromResult(result);

            string Complete()
            {
                completed = true;
                Assert.Contains("completeCallback(7, true", source);
                return "true";
            }
        });

        var reference = await invoker.RegisterCallbackTargetAsync(
            target,
            [new("getBars", JavaScriptCallbackReturnKind.Void)]);
        Assert.Equal(51, reference.Id);

        Assert.True(await invoker.PumpCallbackAsync());
        Assert.Equal("getBars", target.Method);
        Assert.Equal("AAPL", target.Arguments[0].GetString());
        Assert.True(completed);
    }

    [Fact]
    public async Task RetainsObjectsResolvedByJavaScriptPromises()
    {
        var invoker = new NativeJavaScriptInvoker(
            (_, document, _) => Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-promise.js" => "18",
                "htmlml-interop-promise-result.js" =>
                    """{"status":"fulfilled","objectHandle":73}""",
                _ => throw new InvalidOperationException(document)
            }),
            promisePollInterval: TimeSpan.Zero);

        var result = await invoker.InvokePromiseObjectAsync(
            new JavaScriptObjectReference(10),
            "createOrderLine",
            []);

        Assert.Equal(73, result.Id);
    }

    [Fact]
    public async Task DeserializesPlainObjectsAndArraysResolvedByPromises()
    {
        var invoker = new NativeJavaScriptInvoker(
            (_, document, _) => Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-promise.js" => "19",
                "htmlml-interop-promise-result.js" =>
                    """{"status":"fulfilled","value":[{"name":"AAPL","price":187.5}]}""",
                _ => throw new InvalidOperationException(document)
            }),
            promisePollInterval: TimeSpan.Zero);

        var result = await invoker.InvokePromiseAsync<Quote[]>(
            new JavaScriptObjectReference(10),
            "quotes",
            []);

        var quote = Assert.Single(result!);
        Assert.Equal("AAPL", quote.Name);
        Assert.Equal(187.5, quote.Price);
    }

    [Fact]
    public async Task ReturnsNullableRawReferencesFromPromiseHandles()
    {
        var invoker = new NativeJavaScriptInvoker(
            (_, document, _) => Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-promise.js" => "20",
                "htmlml-interop-promise-result.js" =>
                    """{"status":"fulfilled","objectHandle":88}""",
                _ => throw new InvalidOperationException(document)
            }),
            promisePollInterval: TimeSpan.Zero);

        var result = await invoker.InvokePromiseAsync<JavaScriptObjectReference?>(
            new JavaScriptObjectReference(10),
            "maybeElement",
            []);

        Assert.Equal(88, result?.Id);
    }

    [Fact]
    public async Task InvokesDottedGlobalFunctionsThroughTheNativeBoundary()
    {
        var evaluations = new List<(string Source, string Document)>();
        var invoker = new NativeJavaScriptInvoker(
            (source, document, _) =>
            {
                evaluations.Add((source, document));
                return Task.FromResult(document switch
                {
                    "htmlml-native-dotnet-interop.js" => "true",
                    "htmlml-interop-get-global-object.js" => "20",
                    "htmlml-interop-get-global-value.js" => "\"1.2.3\"",
                    "htmlml-interop-global-object.js" => "21",
                    "htmlml-interop-global-value.js" => "\"ABC\"",
                    "htmlml-interop-global-void.js" => "true",
                    "htmlml-interop-global-promise.js" => "31",
                    "htmlml-interop-promise-result.js" =>
                        """{"status":"fulfilled","objectHandle":32}""",
                    _ => throw new InvalidOperationException(document)
                });
            },
            promisePollInterval: TimeSpan.Zero);

        var current = await invoker.GetGlobalObjectAsync("Library.current");
        var version = await invoker.GetGlobalAsync<string>("Library.version");
        var widget = await invoker.InvokeGlobalObjectAsync(
            "Library.createWidget",
            [JavaScriptArgument.From("chart")]);
        var normalized = await invoker.InvokeGlobalAsync<string>(
            "Library.normalize",
            [JavaScriptArgument.From("abc")]);
        await invoker.InvokeGlobalVoidAsync("Library.initialize", []);
        var loaded = await invoker.InvokeGlobalPromiseObjectAsync(
            "Library.loadWidget",
            []);

        Assert.Equal(20, current.Id);
        Assert.Equal("1.2.3", version);
        Assert.Equal(21, widget.Id);
        Assert.Equal("ABC", normalized);
        Assert.Equal(32, loaded.Id);
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-get-global-object.js"
            && item.Source.Contains("\"Library.current\"", StringComparison.Ordinal));
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-global-object.js"
            && item.Source.Contains("\"Library.createWidget\"", StringComparison.Ordinal)
            && item.Source.Contains("chart", StringComparison.Ordinal));
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-global-promise.js"
            && item.Source.Contains("\"Library.loadWidget\"", StringComparison.Ordinal));
    }

    [Fact]
    public async Task ReturnedFunctionReferencesCanBeInvokedAndReleased()
    {
        var evaluations = new List<(string Source, string Document)>();
        var invoker = new NativeJavaScriptInvoker((source, document, _) =>
        {
            evaluations.Add((source, document));
            return Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-object.js" => "61",
                "htmlml-interop-invoke-function-void.js" => "true",
                "htmlml-interop-release.js" => "true",
                _ => throw new InvalidOperationException(document)
            });
        });

        var reference = await invoker.InvokeObjectAsync(
            new JavaScriptObjectReference(10),
            "createDisposer",
            []);
        await using var function = new JavaScriptFunctionReference(
            invoker,
            reference);

        await function.InvokeVoidAsync(
            arguments: [JavaScriptArgument.From("done")]);

        Assert.Equal(61, function.Reference.Id);
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-invoke-function-void.js"
            && item.Source.Contains("invokeFunctionVoid(61", StringComparison.Ordinal)
            && item.Source.Contains("done", StringComparison.Ordinal));
    }

    [Fact]
    public async Task PreservesJavaScriptNullAndUndefinedAsDistinctValues()
    {
        var invocation = 0;
        var invoker = new NativeJavaScriptInvoker((_, document, _) =>
            Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-value.js" when invocation++ == 0
                    => "null",
                "htmlml-interop-value.js"
                    => """{"__htmlMlUndefined":true}""",
                _ => throw new InvalidOperationException(document)
            }));

        var nullValue = await invoker.InvokeAsync<JavaScriptNullish>(
            new JavaScriptObjectReference(1),
            "nullValue",
            []);
        var undefinedValue = await invoker.InvokeAsync<JavaScriptNullish>(
            new JavaScriptObjectReference(1),
            "undefinedValue",
            []);

        Assert.False(nullValue.IsUndefined);
        Assert.True(undefinedValue.IsUndefined);
        Assert.Equal("null", JavaScriptArgument.From(JavaScriptNullish.Null).Json);
        Assert.Equal(
            """{"__htmlMlUndefined":true}""",
            JavaScriptArgument.From(JavaScriptNullish.Undefined).Json);
    }

    [Fact]
    public async Task DeserializesRetainedHandlesInsideArraysAndUnions()
    {
        var invocation = 0;
        var invoker = new NativeJavaScriptInvoker((_, document, _) =>
            Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-value.js" when invocation++ == 0
                    => """[{"__htmlMlHandle":401},{"__htmlMlHandle":402}]""",
                "htmlml-interop-value.js"
                    => """{"__htmlMlHandle":403}""",
                _ => throw new InvalidOperationException(document)
            }));

        var references = await invoker.InvokeAsync<
            IReadOnlyList<JavaScriptObjectReference>>(
            new JavaScriptObjectReference(1),
            "widgets",
            []);
        var union = await invoker.InvokeAsync<
            JavaScriptUnion<string, JavaScriptObjectReference>>(
            new JavaScriptObjectReference(1),
            "widgetOrLabel",
            []);

        Assert.Equal([401L, 402L], references!.Select(item => item.Id));
        Assert.True(union.TryGet<JavaScriptObjectReference>(
            out var reference));
        Assert.Equal(403, reference.Id);
    }

    [Fact]
    public async Task EncodesAndDecodesTypeScriptTuplesAsJavaScriptArrays()
    {
        var evaluations = new List<(string Source, string Document)>();
        var invoker = new NativeJavaScriptInvoker((source, document, _) =>
        {
            evaluations.Add((source, document));
            return Task.FromResult(document switch
            {
                "htmlml-native-dotnet-interop.js" => "true",
                "htmlml-interop-value.js" =>
                    """["AAPL",{"__htmlMlHandle":501}]""",
                "htmlml-interop-void.js" => "true",
                _ => throw new InvalidOperationException(document)
            });
        });

        var result = await invoker.InvokeAsync<
            (string Symbol, JavaScriptObjectReference Widget)>(
            new JavaScriptObjectReference(1),
            "tuple",
            []);
        var argument = JavaScriptArgument.From((
            "MSFT",
            new JavaScriptObjectReference(502)));
        await invoker.InvokeVoidAsync(
            new JavaScriptObjectReference(1),
            "acceptTuple",
            [argument]);

        Assert.Equal("AAPL", result.Symbol);
        Assert.Equal(501, result.Widget.Id);
        Assert.Equal(
            """["MSFT",{"__htmlMlHandle":502}]""",
            argument.Json);
        Assert.Contains(evaluations, item =>
            item.Document == "htmlml-interop-void.js"
            && item.Source.Contains("acceptTuple", StringComparison.Ordinal));

        var longTuple = JavaScriptArgument.From((
            "a",
            1,
            true,
            "b",
            2,
            false,
            "c",
            3));
        using var document = JsonDocument.Parse(longTuple.Json);
        Assert.Equal(8, document.RootElement.GetArrayLength());
    }

    private sealed class RecordingTarget : IJavaScriptCallbackTarget
    {
        public string? Method { get; private set; }
        public JsonElement Arguments { get; private set; }

        public ValueTask<object?> DispatchAsync(
            string method,
            JsonElement arguments,
            CancellationToken cancellationToken = default)
        {
            Method = method;
            Arguments = arguments.Clone();
            return ValueTask.FromResult<object?>(new { accepted = true });
        }
    }

    private sealed record Quote(string Name, double Price);
}

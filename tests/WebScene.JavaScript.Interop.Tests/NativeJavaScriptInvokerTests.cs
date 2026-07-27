using System.Text.Json;
using WebScene.JavaScript.Interop;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

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
                    "webscene-native-dotnet-interop.js" => "true",
                    "webscene-interop-construct.js" => "11",
                    "webscene-interop-object.js" => "12",
                    "webscene-interop-value.js" => "\"NASDAQ:AAPL\"",
                    "webscene-interop-void.js" => "true",
                    "webscene-interop-promise.js" => "17",
                    "webscene-interop-promise-result.js" when promisePolls++ == 0
                        => """{"status":"pending"}""",
                    "webscene-interop-promise-result.js"
                        => """{"status":"fulfilled","value":true}""",
                    "webscene-interop-release.js" => "true",
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
            item.Document == "webscene-native-dotnet-interop.js"));
        Assert.Contains(evaluations, item =>
            item.Document == "webscene-interop-promise.js"
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-register-callback.js" => "51",
                "webscene-interop-take-callback.js" =>
                    """{"call":7,"target":1,"method":"getBars","arguments":["AAPL"]}""",
                "webscene-interop-complete-callback.js" => Complete(),
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-promise.js" => "18",
                "webscene-interop-promise-result.js" =>
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-promise.js" => "19",
                "webscene-interop-promise-result.js" =>
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-promise.js" => "20",
                "webscene-interop-promise-result.js" =>
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
                    "webscene-native-dotnet-interop.js" => "true",
                    "webscene-interop-get-global-object.js" => "20",
                    "webscene-interop-get-global-value.js" => "\"1.2.3\"",
                    "webscene-interop-global-object.js" => "21",
                    "webscene-interop-global-value.js" => "\"ABC\"",
                    "webscene-interop-global-void.js" => "true",
                    "webscene-interop-global-promise.js" => "31",
                    "webscene-interop-promise-result.js" =>
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
            item.Document == "webscene-interop-get-global-object.js"
            && item.Source.Contains("\"Library.current\"", StringComparison.Ordinal));
        Assert.Contains(evaluations, item =>
            item.Document == "webscene-interop-global-object.js"
            && item.Source.Contains("\"Library.createWidget\"", StringComparison.Ordinal)
            && item.Source.Contains("chart", StringComparison.Ordinal));
        Assert.Contains(evaluations, item =>
            item.Document == "webscene-interop-global-promise.js"
            && item.Source.Contains("\"Library.loadWidget\"", StringComparison.Ordinal));
    }

    [Fact]
    public async Task RetainsNullableGlobalObjectsWithoutJsonShaping()
    {
        await VerifyAsync("null", null);
        await VerifyAsync("42", 42L);

        static async Task VerifyAsync(string response, long? expectedHandle)
        {
        var invoker = new NativeJavaScriptInvoker((source, document, _) =>
            Task.FromResult(document switch
            {
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-get-optional-global-object.js" => response,
                _ => throw new InvalidOperationException(document)
            }));

        var result = await invoker.GetGlobalAsync<JavaScriptObjectReference?>(
            "Library.optionalWidget");

        Assert.Equal(expectedHandle, result?.Id);
        }
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-object.js" => "61",
                "webscene-interop-invoke-function-void.js" => "true",
                "webscene-interop-release.js" => "true",
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
            item.Document == "webscene-interop-invoke-function-void.js"
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-value.js" when invocation++ == 0
                    => "null",
                "webscene-interop-value.js"
                    => """{"__webSceneUndefined":true}""",
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
            """{"__webSceneUndefined":true}""",
            JavaScriptArgument.From(JavaScriptNullish.Undefined).Json);
    }

    [Fact]
    public async Task DeserializesRetainedHandlesInsideArraysAndUnions()
    {
        var invocation = 0;
        var invoker = new NativeJavaScriptInvoker((_, document, _) =>
            Task.FromResult(document switch
            {
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-value.js" when invocation++ == 0
                    => """[{"__webSceneHandle":401},{"__webSceneHandle":402}]""",
                "webscene-interop-value.js"
                    => """{"__webSceneHandle":403}""",
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
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-value.js" =>
                    """["AAPL",{"__webSceneHandle":501}]""",
                "webscene-interop-void.js" => "true",
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
            """["MSFT",{"__webSceneHandle":502}]""",
            argument.Json);
        Assert.Contains(evaluations, item =>
            item.Document == "webscene-interop-void.js"
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

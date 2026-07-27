using System.Text.Json;
using GeneratorCapabilities.Generated;
using WebScene.JavaScript.Interop;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class GeneratedHandleReturnTests
{
    [Fact]
    public async Task GeneratedBindingsMaterializeDistinguishableRetainedUnions()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        var widgetOrLabel = await widget.WidgetOrLabelAsync();
        var widgetsOrLabel = await widget.WidgetsOrLabelAsync();
        var disposerOrLabel = await widget.DisposerOrLabelAsync();
        var globalWidgetOrLabel =
            await JavaScriptGlobals.WidgetOrLabelAsync(invoker);

        Assert.True(widgetOrLabel.TryGet<WidgetProxy>(out var returnedWidget));
        Assert.Equal(301, returnedWidget!.JavaScriptReference.Id);
        Assert.True(widgetsOrLabel.TryGet<IReadOnlyList<WidgetProxy>>(
            out var returnedWidgets));
        Assert.Equal(
            [302L, 303L],
            returnedWidgets!.Select(item => item.JavaScriptReference.Id));
        Assert.True(disposerOrLabel.TryGet<JavaScriptFunctionReference>(
            out var returnedDisposer));
        Assert.Equal(304, returnedDisposer!.Reference.Id);
        Assert.True(globalWidgetOrLabel.TryGet<WidgetProxy>(
            out var globalWidget));
        Assert.Equal(305, globalWidget!.JavaScriptReference.Id);
        Assert.Equal(
            [
                "value:widgetOrLabel",
                "value:widgetsOrLabel",
                "value:disposerOrLabel",
                "global-value:GeneratorCapabilities.widgetOrLabel"
            ],
            invoker.Calls);

        await returnedWidget.DisposeAsync();
        foreach (var item in returnedWidgets!)
        {
            await item.DisposeAsync();
        }
        await returnedDisposer.DisposeAsync();
        await globalWidget.DisposeAsync();
    }

    [Fact]
    public async Task GeneratedBindingsMaterializeNestedHandlesWithoutReflection()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        var widgets = await widget.WidgetsAsync();
        var aliasedWidgets = await widget.AliasedWidgetsAsync();
        var promisedWidgets = await widget.WidgetsAsyncMethod2();
        var maybeWidgets = await widget.MaybeWidgetsAsync();
        var disposers = await widget.DisposersAsync();
        var widgetRecord = await widget.WidgetRecordAsync();
        var children = await widget.GetChildrenAsync();
        var readyChildren = await widget.GetReadyChildrenAsync();
        var globalWidgets = await JavaScriptGlobals.ListWidgetsAsync(invoker);
        var loadedWidgets = await JavaScriptGlobals.LoadWidgetsAsync(invoker);
        var exportedWidgets = await JavaScriptGlobals.GetWidgetsAsync(invoker);

        Assert.Equal([201L, 202L], widgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([212L], aliasedWidgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([203L], promisedWidgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([204L], maybeWidgets!.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([205L, 206L], disposers.Select(item =>
            item.Reference.Id));
        Assert.Equal(216, widgetRecord["primary"].JavaScriptReference.Id);
        Assert.Equal([207L], children.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([208L], readyChildren.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([209L], globalWidgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([210L], loadedWidgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal([211L], exportedWidgets.Select(item =>
            item.JavaScriptReference.Id));
        Assert.Equal(
            [
                "value:widgets",
                "value:aliasedWidgets",
                "promise-value:widgetsAsync",
                "value:maybeWidgets",
                "value:disposers",
                "value:widgetRecord",
                "property:children",
                "promise-property-value:readyChildren",
                "global-value:GeneratorCapabilities.listWidgets",
                "global-promise-value:GeneratorCapabilities.loadWidgets",
                "global-property-value:GeneratorCapabilities.widgets"
            ],
            invoker.Calls);

        foreach (var item in widgets
                     .Concat(aliasedWidgets)
                     .Concat(promisedWidgets)
                     .Concat(maybeWidgets!)
                     .Concat(widgetRecord.Values)
                     .Concat(children)
                     .Concat(readyChildren)
                     .Concat(globalWidgets)
                     .Concat(loadedWidgets)
                     .Concat(exportedWidgets))
        {
            await item.DisposeAsync();
        }
        foreach (var disposer in disposers)
        {
            await disposer.DisposeAsync();
        }
    }

    [Fact]
    public async Task GeneratedIndexedModelsPreserveTypedKeysAndRetainedValues()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        var dictionary = await widget.WidgetDictionaryAsync();
        var generic = await widget.GenericWidgetDictionaryAsync();
        var numeric = await widget.NumericWidgetDictionaryAsync();
        var mixed = await widget.MixedWidgetDictionaryAsync();
        await widget.AcceptWidgetDictionaryAsync(new WidgetDictionary
        {
            AdditionalProperties = new Dictionary<string, WidgetProxy>
            {
                ["sent"] = widget
            }
        });
        await widget.AcceptGenericWidgetDictionaryAsync(
            new DictionaryEnvelope<WidgetProxy>
            {
                AdditionalProperties = new Dictionary<string, WidgetProxy>
                {
                    ["genericSent"] = widget
                }
            });

        Assert.Equal(
            217,
            dictionary.AdditionalProperties!["primary"].JavaScriptReference.Id);
        Assert.Equal(
            221,
            generic.AdditionalProperties!["generic"].JavaScriptReference.Id);
        Assert.Equal(
            218,
            numeric.AdditionalProperties![7].JavaScriptReference.Id);
        Assert.Equal(219, mixed.Primary.JavaScriptReference.Id);
        Assert.Equal(
            220,
            mixed.AdditionalProperties!["secondary"].JavaScriptReference.Id);
        Assert.Contains(
            """void:acceptWidgetDictionary:{"sent":{"__webSceneHandle":10}}""",
            invoker.Calls);
        Assert.Contains(
            """void:acceptGenericWidgetDictionary:{"genericSent":{"__webSceneHandle":10}}""",
            invoker.Calls);

        await dictionary.AdditionalProperties["primary"].DisposeAsync();
        await generic.AdditionalProperties["generic"].DisposeAsync();
        await numeric.AdditionalProperties[7].DisposeAsync();
        await mixed.Primary.DisposeAsync();
        await mixed.AdditionalProperties["secondary"].DisposeAsync();
    }

    [Fact]
    public async Task GeneratedBindingsEncodeAndMaterializeTypeScriptTuples()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        var tuple = await widget.TupleAsync();
        var widgetTuple = await widget.WidgetTupleAsync();
        var single = await widget.SingleWidgetTupleAsync();
        var longTuple = await widget.LongTupleAsync();
        await widget.AcceptWidgetTupleAsync(("sent", widget));

        Assert.Equal(("plain", 12.5), tuple);
        Assert.Equal("retained", widgetTuple.Item1);
        Assert.Equal(213, widgetTuple.Item2.JavaScriptReference.Id);
        Assert.Equal(214, single.Item1.JavaScriptReference.Id);
        Assert.Equal(215, longTuple.Item8.JavaScriptReference.Id);
        Assert.Contains(
            """void:acceptWidgetTuple:["sent",{"__webSceneHandle":10}]""",
            invoker.Calls);

        await widgetTuple.Item2.DisposeAsync();
        await single.Item1.DisposeAsync();
        await longTuple.Item8.DisposeAsync();
    }

    [Fact]
    public async Task GeneratedModelsMaterializeNestedRetainedValues()
    {
        var invoker = new NativeJavaScriptInvoker(
            (source, document, _) => Task.FromResult(document switch
            {
                "webscene-native-dotnet-interop.js" => "true",
                "webscene-interop-value.js" when source.Contains(
                    "\"snapshot\"",
                    StringComparison.Ordinal) =>
                    """
                    {
                      "dispose":{"__webSceneHandle":601},
                      "maybeWidget":{"__webSceneHandle":602},
                      "title":"state",
                      "tuple":["paired",{"__webSceneHandle":603}],
                      "widget":{"__webSceneHandle":604},
                      "widgets":[{"__webSceneHandle":605}]
                    }
                    """,
                "webscene-interop-value.js" when source.Contains(
                    "\"anonymousSnapshot\"",
                    StringComparison.Ordinal) =>
                    """
                    {
                      "title":"anonymous",
                      "widget":{"__webSceneHandle":606}
                    }
                    """,
                "webscene-interop-value.js" =>
                    """
                    {
                      "value":{"__webSceneHandle":607},
                      "values":[
                        {"__webSceneHandle":608},
                        {"__webSceneHandle":609}
                      ]
                    }
                    """,
                "webscene-interop-release.js" => "true",
                _ => throw new InvalidOperationException(document)
            }));
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        var snapshot = await widget.SnapshotAsync();
        var anonymous = await widget.AnonymousSnapshotAsync();
        var envelope = await widget.WidgetEnvelopeAsync();

        Assert.Equal("state", snapshot.Title);
        Assert.Equal(601, snapshot.Dispose.Reference.Id);
        Assert.True(snapshot.MaybeWidget.HasValue);
        Assert.Equal(
            602,
            snapshot.MaybeWidget.Value?.JavaScriptReference.Id);
        Assert.Equal(603, snapshot.Tuple.Item2.JavaScriptReference.Id);
        Assert.Equal(604, snapshot.Widget.JavaScriptReference.Id);
        Assert.Equal(605, Assert.Single(snapshot.Widgets).JavaScriptReference.Id);
        Assert.Equal("anonymous", anonymous.Title);
        Assert.Equal(606, anonymous.Widget.JavaScriptReference.Id);
        Assert.Equal(607, envelope.Value.JavaScriptReference.Id);
        Assert.Equal(
            [608L, 609L],
            envelope.Values.Select(value => value.JavaScriptReference.Id));

        await snapshot.Dispose.DisposeAsync();
        await snapshot.MaybeWidget.Value!.DisposeAsync();
        await snapshot.Tuple.Item2.DisposeAsync();
        await snapshot.Widget.DisposeAsync();
        await snapshot.Widgets[0].DisposeAsync();
        await anonymous.Widget.DisposeAsync();
        await envelope.Value.DisposeAsync();
        foreach (var value in envelope.Values)
        {
            await value.DisposeAsync();
        }
    }

    [Fact]
    public void GeneratedOptionalPropertiesPreserveAbsentAndExplicitNull()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));
        var snapshot = new WidgetSnapshot
        {
            Dispose = new JavaScriptFunctionReference(
                invoker,
                new JavaScriptObjectReference(11)),
            Title = "state",
            Tuple = ("paired", widget),
            Widget = widget,
            Widgets = [widget]
        };

        using var absent = JsonDocument.Parse(
            JavaScriptArgument.From(snapshot).Json);
        Assert.False(absent.RootElement.TryGetProperty(
            "maybeWidget",
            out _));

        var withNull = snapshot with
        {
            MaybeWidget = new JavaScriptOptional<WidgetProxy>(null)
        };
        using var explicitNull = JsonDocument.Parse(
            JavaScriptArgument.From(withNull).Json);
        Assert.Equal(
            JsonValueKind.Null,
            explicitNull.RootElement.GetProperty("maybeWidget").ValueKind);
    }

    [Fact]
    public async Task GeneratedBindingsExposeEveryDiscoveredConstructorOverload()
    {
        var invoker = new RecordingInvoker();

        await using var byName = await Controller.CreateAsync(invoker, "primary");
        await using var byId = await Controller.CreateOverload2Async(invoker, 42);
        await using var fromId = await JavaScriptGlobals.FromIdAsync(invoker, 43);

        Assert.Equal(111, byName.JavaScriptReference.Id);
        Assert.Equal(112, byId.JavaScriptReference.Id);
        Assert.Equal(113, fromId.JavaScriptReference.Id);
        Assert.Equal(
            [
                "construct:GeneratorCapabilities.Controller:\"primary\"",
                "construct:GeneratorCapabilities.Controller:42",
                "global-object:GeneratorCapabilities.Controller.fromId"
            ],
            invoker.Calls);
    }

    [Fact]
    public async Task GeneratedGlobalsExposeExportedAndStaticValues()
    {
        var invoker = new RecordingInvoker();

        await using var current = await JavaScriptGlobals.GetCurrentAsync(invoker);
        await using var ready = await JavaScriptGlobals.GetReadyAsync(invoker);
        var version = await JavaScriptGlobals.GetVersionAsync(invoker);
        await using var exported =
            await JavaScriptGlobals.GetCurrentControllerAsync(invoker);
        await using var exportedReady =
            await JavaScriptGlobals.GetReadyControllerAsync(invoker);
        var libraryVersion =
            await JavaScriptGlobals.GetLibraryVersionAsync(invoker);

        Assert.Equal(114, current.JavaScriptReference.Id);
        Assert.Equal(116, ready.JavaScriptReference.Id);
        Assert.Equal("controller-1.0", version);
        Assert.Equal(115, exported.JavaScriptReference.Id);
        Assert.Equal(117, exportedReady.JavaScriptReference.Id);
        Assert.Equal("library-2.0", libraryVersion);
        Assert.Equal(
            [
                "global-property-object:GeneratorCapabilities.Controller.current",
                "global-property-promise-object:GeneratorCapabilities.Controller.ready",
                "global-property-value:GeneratorCapabilities.Controller.version",
                "global-property-object:GeneratorCapabilities.currentController",
                "global-property-promise-object:GeneratorCapabilities.readyController",
                "global-property-value:GeneratorCapabilities.libraryVersion"
            ],
            invoker.Calls);
    }

    [Fact]
    public async Task GeneratedBindingsUseHandlePathsForFunctionsAndAliasedProxies()
    {
        var invoker = new RecordingInvoker();
        var widget = WidgetProxy.FromReference(
            invoker,
            new JavaScriptObjectReference(10));

        await using var direct = await widget.CreateDisposerAsync();
        await using var promised = await widget.CreateDisposerAsyncMethod2();
        await using var maybe = await widget.MaybeDisposerAsync();
        await using var maybePromised = await widget.MaybeDisposerAsyncMethod2();
        await using var property = await widget.GetDisposerAsync();
        await using var ready = await widget.GetReadyAsync();
        await using var readyDisposer = await widget.GetReadyDisposerAsync();
        await using var alias = await widget.AliasedWidgetAsync();
        await using var maybeAlias = await widget.MaybeAliasedWidgetAsync();
        await using var global = await JavaScriptGlobals.CreateDisposerAsync(invoker);
        await using var globalPromised =
            await JavaScriptGlobals.LoadDisposerAsync(invoker);
        await using var globalMaybe =
            await JavaScriptGlobals.MaybeDisposerAsync(invoker);

        Assert.Equal(101, direct.Reference.Id);
        Assert.Equal(102, promised.Reference.Id);
        Assert.Equal(103, maybe?.Reference.Id);
        Assert.Equal(104, maybePromised?.Reference.Id);
        Assert.Equal(105, property?.Reference.Id);
        Assert.Equal(118, ready.JavaScriptReference.Id);
        Assert.Equal(119, readyDisposer.Reference.Id);
        Assert.Equal(106, alias.JavaScriptReference.Id);
        Assert.Equal(107, maybeAlias?.JavaScriptReference.Id);
        Assert.Equal(108, global.Reference.Id);
        Assert.Equal(109, globalPromised.Reference.Id);
        Assert.Equal(110, globalMaybe?.Reference.Id);
        Assert.Equal(
            [
                "object:createDisposer",
                "promise-object:createDisposerAsync",
                "value:maybeDisposer",
                "promise-value:maybeDisposerAsync",
                "property:disposer",
                "promise-property-object:ready",
                "promise-property-object:readyDisposer",
                "object:aliasedWidget",
                "value:maybeAliasedWidget",
                "global-object:GeneratorCapabilities.createDisposer",
                "global-promise-object:GeneratorCapabilities.loadDisposer",
                "global-value:GeneratorCapabilities.maybeDisposer"
            ],
            invoker.Calls);
    }

    private sealed class RecordingInvoker : IJavaScriptInvoker
    {
        public List<string> Calls { get; } = [];

        public ValueTask<JavaScriptObjectReference> GetGlobalObjectAsync(
            string globalName,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-property-object:{globalName}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                globalName.EndsWith(
                    ".currentController",
                    StringComparison.Ordinal)
                    ? 115
                    : 114));
        }

        public ValueTask<T?> GetGlobalAsync<T>(
            string globalName,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-property-value:{globalName}");
            if (globalName.EndsWith(".widgets", StringComparison.Ordinal))
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(211) });
            }
            object value = globalName.EndsWith(
                ".libraryVersion",
                StringComparison.Ordinal)
                ? "library-2.0"
                : "controller-1.0";
            return Result<T>(value);
        }

        public ValueTask<T?> GetGlobalPromiseAsync<T>(
            string globalName,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-property-promise-value:{globalName}");
            throw new NotSupportedException();
        }

        public ValueTask<JavaScriptObjectReference> GetGlobalPromiseObjectAsync(
            string globalName,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-property-promise-object:{globalName}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                globalName.EndsWith(
                    ".readyController",
                    StringComparison.Ordinal)
                    ? 117
                    : 116));
        }

        public ValueTask<JavaScriptObjectReference> InvokeGlobalObjectAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-object:{globalName}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                globalName.EndsWith(".fromId", StringComparison.Ordinal)
                    ? 113
                    : 108));
        }

        public ValueTask<T?> InvokeGlobalAsync<T>(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-value:{globalName}");
            if (globalName.EndsWith(
                    ".widgetOrLabel",
                    StringComparison.Ordinal))
            {
                object union =
                    new JavaScriptUnion<
                        string,
                        JavaScriptObjectReference>(
                        new JavaScriptObjectReference(305));
                return Result<T>(union);
            }
            if (globalName.EndsWith(".listWidgets", StringComparison.Ordinal))
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(209) });
            }
            return Result<T>(new JavaScriptObjectReference(110));
        }

        public ValueTask<T?> InvokeGlobalPromiseAsync<T>(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-promise-value:{globalName}");
            return Result<T>(
                new[] { new JavaScriptObjectReference(210) });
        }

        public ValueTask<JavaScriptObjectReference> InvokeGlobalPromiseObjectAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"global-promise-object:{globalName}");
            return ValueTask.FromResult(new JavaScriptObjectReference(109));
        }

        public ValueTask<JavaScriptObjectReference> ConstructAsync(
            string globalName,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"construct:{globalName}:{arguments[0].Json}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                arguments[0].Json.StartsWith("\"", StringComparison.Ordinal)
                    ? 111
                    : 112));
        }

        public ValueTask<JavaScriptObjectReference> InvokeObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"object:{method}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                method == "aliasedWidget" ? 106 : 101));
        }

        public ValueTask<T?> GetPropertyAsync<T>(
            JavaScriptObjectReference target,
            string property,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"property:{property}");
            if (property == "children")
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(207) });
            }
            return Result<T>(new JavaScriptObjectReference(105));
        }

        public ValueTask<T?> GetPromisePropertyAsync<T>(
            JavaScriptObjectReference target,
            string property,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"promise-property-value:{property}");
            return Result<T>(
                new[] { new JavaScriptObjectReference(208) });
        }

        public ValueTask<JavaScriptObjectReference> GetPromiseObjectPropertyAsync(
            JavaScriptObjectReference target,
            string property,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"promise-property-object:{property}");
            return ValueTask.FromResult(new JavaScriptObjectReference(
                property == "readyDisposer" ? 119 : 118));
        }

        public ValueTask<T?> InvokeAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"value:{method}");
            if (method == "widgetOrLabel")
            {
                object union =
                    new JavaScriptUnion<
                        string,
                        JavaScriptObjectReference>(
                        new JavaScriptObjectReference(301));
                return Result<T>(union);
            }
            if (method == "widgetsOrLabel")
            {
                object union =
                    new JavaScriptUnion<
                        string,
                        IReadOnlyList<JavaScriptObjectReference>>(
                        new[]
                        {
                            new JavaScriptObjectReference(302),
                            new JavaScriptObjectReference(303)
                        });
                return Result<T>(union);
            }
            if (method == "disposerOrLabel")
            {
                object union =
                    new JavaScriptUnion<
                        string,
                        JavaScriptObjectReference>(
                        new JavaScriptObjectReference(304));
                return Result<T>(union);
            }
            if (method == "widgets")
            {
                return Result<T>(
                    new[]
                    {
                        new JavaScriptObjectReference(201),
                        new JavaScriptObjectReference(202)
                    });
            }
            if (method == "aliasedWidgets")
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(212) });
            }
            if (method == "maybeWidgets")
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(204) });
            }
            if (method == "disposers")
            {
                return Result<T>(
                    new[]
                    {
                        new JavaScriptObjectReference(205),
                        new JavaScriptObjectReference(206)
                    });
            }
            if (method == "widgetRecord")
            {
                return Result<T>(
                    new Dictionary<string, JavaScriptObjectReference>
                    {
                        ["primary"] = new(216)
                    });
            }
            if (method == "widgetDictionary")
            {
                return Result<T>(JsonSerializer.Deserialize(
                    """{"primary":{"__webSceneHandle":217}}""",
                    typeof(T))!);
            }
            if (method == "genericWidgetDictionary")
            {
                return Result<T>(JsonSerializer.Deserialize(
                    """{"generic":{"__webSceneHandle":221}}""",
                    typeof(T))!);
            }
            if (method == "numericWidgetDictionary")
            {
                return Result<T>(JsonSerializer.Deserialize(
                    """{"7":{"__webSceneHandle":218}}""",
                    typeof(T))!);
            }
            if (method == "mixedWidgetDictionary")
            {
                return Result<T>(JsonSerializer.Deserialize(
                    """
                    {
                      "primary":{"__webSceneHandle":219},
                      "secondary":{"__webSceneHandle":220}
                    }
                    """,
                    typeof(T))!);
            }
            if (method == "tuple")
            {
                return Result<T>(("plain", 12.5));
            }
            if (method == "widgetTuple")
            {
                return Result<T>((
                    "retained",
                    new JavaScriptObjectReference(213)));
            }
            if (method == "singleWidgetTuple")
            {
                return Result<T>(
                    ValueTuple.Create(new JavaScriptObjectReference(214)));
            }
            if (method == "longTuple")
            {
                return Result<T>((
                    "a",
                    1d,
                    true,
                    "b",
                    2d,
                    false,
                    "c",
                    new JavaScriptObjectReference(215)));
            }
            return Result<T>(new JavaScriptObjectReference(
                method == "maybeAliasedWidget" ? 107 : 103));
        }

        public ValueTask<T?> InvokePromiseAsync<T>(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"promise-value:{method}");
            if (method == "widgetsAsync")
            {
                return Result<T>(
                    new[] { new JavaScriptObjectReference(203) });
            }
            return Result<T>(new JavaScriptObjectReference(104));
        }

        public ValueTask<JavaScriptObjectReference> InvokePromiseObjectAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add($"promise-object:{method}");
            return ValueTask.FromResult(new JavaScriptObjectReference(102));
        }

        public ValueTask InvokeVoidAsync(
            JavaScriptObjectReference target,
            string method,
            IReadOnlyList<JavaScriptArgument> arguments,
            CancellationToken cancellationToken = default)
        {
            Calls.Add(
                $"void:{method}:{string.Join(",", arguments.Select(argument => argument.Json))}");
            return ValueTask.CompletedTask;
        }

        public ValueTask ReleaseAsync(
            JavaScriptObjectReference reference,
            CancellationToken cancellationToken = default)
            => ValueTask.CompletedTask;

        private static ValueTask<T?> Result<T>(object value)
            => ValueTask.FromResult((T?)(object)value);
    }
}

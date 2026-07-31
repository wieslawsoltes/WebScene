using System.Buffers;
using System.Text;
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
                "object:maybeDisposer",
                "promise-object:maybeDisposerAsync",
                "property:disposer",
                "promise-property-object:ready",
                "promise-property-object:readyDisposer",
                "object:aliasedWidget",
                "object:maybeAliasedWidget",
                "global-object:GeneratorCapabilities.createDisposer",
                "global-promise-object:GeneratorCapabilities.loadDisposer",
                "global-object:GeneratorCapabilities.maybeDisposer"
            ],
            invoker.Calls);
    }

    private sealed class RecordingInvoker : IJavaScriptBinaryInvoker
    {
        public List<string> Calls { get; } = [];

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
            var argumentJson = RecordBinary<TArguments, TCodec>(
                callSite,
                in arguments);
            var resultJson = BinaryResultJson(callSite, argumentJson);
            return ValueTask.FromResult(
                DecodeBinaryResult<TArguments, TResult, TCodec>(resultJson));
        }

        public ValueTask InvokeBinaryVoidAsync<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target,
            TArguments arguments,
            CancellationToken cancellationToken = default)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            _ = RecordBinary<TArguments, TCodec>(callSite, in arguments);
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

        private string RecordBinary<TArguments, TCodec>(
            JavaScriptBinaryCallSite callSite,
            in TArguments arguments)
            where TCodec : struct,
            IJavaScriptBinaryArgumentsCodec<TArguments>
        {
            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = TCodec.EncodeArguments(ref writer, in arguments);
                var argumentJson = ReadArgumentJson(ref writer, root);
                var globalName = DecodeName(callSite.GlobalNameUtf8);
                var memberName = DecodeName(callSite.MemberNameUtf8);
                var promise =
                    (callSite.Flags & JavaScriptBinaryCallFlags.AwaitPromise) != 0;
                var call = callSite.Operation switch
                {
                    JavaScriptBinaryOperation.Construct =>
                        $"construct:{globalName}:{FirstArgument(argumentJson)}",
                    JavaScriptBinaryOperation.GetGlobal =>
                        $"global-property-{ResultKind(callSite, promise)}:{globalName}",
                    JavaScriptBinaryOperation.InvokeGlobal =>
                        $"global-{ResultKind(callSite, promise)}:{globalName}",
                    JavaScriptBinaryOperation.GetProperty =>
                        promise
                            ? $"promise-property-{ResultKind(callSite, false)}:{memberName}"
                            : $"property:{memberName}",
                    JavaScriptBinaryOperation.InvokeMember
                        when callSite.ResultMode == JavaScriptBinaryResultMode.Void =>
                        $"void:{memberName}:{JoinArguments(argumentJson)}",
                    JavaScriptBinaryOperation.InvokeMember =>
                        $"{(promise ? "promise-" : string.Empty)}{ResultKind(callSite, false)}:{memberName}",
                    _ => throw new NotSupportedException(
                        $"Unexpected binary test operation {callSite.Operation}.")
                };
                Calls.Add(call);
                return argumentJson;
            }
            finally
            {
                writer.Dispose();
            }
        }

        private static string ResultKind(
            JavaScriptBinaryCallSite callSite,
            bool promise)
        {
            var kind = callSite.ResultMode
                == JavaScriptBinaryResultMode.RetainedHandle
                ? "object"
                : "value";
            return promise ? "promise-" + kind : kind;
        }

        private static string DecodeName(byte[]? value)
            => value is null ? string.Empty : Encoding.UTF8.GetString(value);

        private static string FirstArgument(string arguments)
        {
            using var document = JsonDocument.Parse(arguments);
            return document.RootElement.GetArrayLength() == 0
                ? string.Empty
                : document.RootElement[0].GetRawText();
        }

        private static string JoinArguments(string arguments)
        {
            using var document = JsonDocument.Parse(arguments);
            return string.Join(
                ",",
                document.RootElement.EnumerateArray()
                    .Select(static item => item.GetRawText()));
        }

        private static string BinaryResultJson(
            JavaScriptBinaryCallSite callSite,
            string arguments)
        {
            var name = DecodeName(
                callSite.MemberNameUtf8 ?? callSite.GlobalNameUtf8);
            return name switch
            {
                "GeneratorCapabilities.Controller" =>
                    Handle(FirstArgument(arguments).StartsWith(
                        "\"",
                        StringComparison.Ordinal) ? 111 : 112),
                "GeneratorCapabilities.Controller.fromId" => Handle(113),
                "GeneratorCapabilities.Controller.current" => Handle(114),
                "GeneratorCapabilities.currentController" => Handle(115),
                "GeneratorCapabilities.Controller.ready" => Handle(116),
                "GeneratorCapabilities.readyController" => Handle(117),
                "GeneratorCapabilities.Controller.version" => "\"controller-1.0\"",
                "GeneratorCapabilities.libraryVersion" => "\"library-2.0\"",
                "GeneratorCapabilities.createDisposer" => Handle(108),
                "GeneratorCapabilities.loadDisposer" => Handle(109),
                "GeneratorCapabilities.maybeDisposer" => Handle(110),
                "GeneratorCapabilities.widgetOrLabel" => Handle(305),
                "GeneratorCapabilities.listWidgets" => Handles(209),
                "GeneratorCapabilities.loadWidgets" => Handles(210),
                "GeneratorCapabilities.widgets" => Handles(211),
                "createDisposer" => Handle(101),
                "createDisposerAsync" => Handle(102),
                "maybeDisposer" => Handle(103),
                "maybeDisposerAsync" => Handle(104),
                "disposer" => Handle(105),
                "aliasedWidget" => Handle(106),
                "maybeAliasedWidget" => Handle(107),
                "ready" => Handle(118),
                "readyDisposer" => Handle(119),
                "widgetOrLabel" => Handle(301),
                "widgetsOrLabel" => Handles(302, 303),
                "disposerOrLabel" => Handle(304),
                "widgets" => Handles(201, 202),
                "aliasedWidgets" => Handles(212),
                "widgetsAsync" => Handles(203),
                "maybeWidgets" => Handles(204),
                "disposers" => Handles(205, 206),
                "widgetRecord" => $$"""{"primary":{{Handle(216)}}}""",
                "children" => Handles(207),
                "readyChildren" => Handles(208),
                "widgetDictionary" => $$"""{"primary":{{Handle(217)}}}""",
                "genericWidgetDictionary" =>
                    $$"""{"generic":{{Handle(221)}}}""",
                "numericWidgetDictionary" => $$"""{"7":{{Handle(218)}}}""",
                "mixedWidgetDictionary" =>
                    $$"""{"primary":{{Handle(219)}},"secondary":{{Handle(220)}}}""",
                "tuple" => """["plain",12.5]""",
                "widgetTuple" => $$"""["retained",{{Handle(213)}}]""",
                "singleWidgetTuple" => $$"""[{{Handle(214)}}]""",
                "longTuple" =>
                    $$"""["a",1,true,"b",2,false,"c",{{Handle(215)}}]""",
                _ => throw new NotSupportedException(
                    $"No binary test result is registered for '{name}'.")
            };
        }

        private static string Handle(long value)
            => $$"""{"__webSceneHandle":{{value}}}""";

        private static string Handles(params long[] values)
            => "[" + string.Join(",", values.Select(Handle)) + "]";

        private unsafe TResult DecodeBinaryResult<
            TArguments,
            TResult,
            TCodec>(string json)
            where TCodec : struct,
            IJavaScriptBinaryCodec<TArguments, TResult>
        {
            using var document = JsonDocument.Parse(json);
            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = WriteJson(ref writer, document.RootElement);
                fixed (JavaScriptBinaryValueData* values = writer.Values)
                fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                fixed (byte* utf8 = writer.Utf8)
                {
                    var value = new JavaScriptBinaryValue(
                        values,
                        checked((uint)writer.Values.Length),
                        edges,
                        checked((uint)writer.Edges.Length),
                        utf8,
                        checked((uint)writer.Utf8.Length),
                        root);
                    return TCodec.DecodeResult(value, this);
                }
            }
            finally
            {
                writer.Dispose();
            }
        }

        private static uint WriteJson(
            ref JavaScriptBinaryWriter writer,
            JsonElement value)
        {
            if (value.ValueKind == JsonValueKind.Object
                && value.TryGetProperty("__webSceneHandle", out var handle)
                && handle.TryGetInt64(out var handleId))
            {
                return writer.WriteHandle(new JavaScriptObjectReference(handleId));
            }
            return value.ValueKind switch
            {
                JsonValueKind.Null => writer.WriteNull(),
                JsonValueKind.False => writer.WriteBoolean(false),
                JsonValueKind.True => writer.WriteBoolean(true),
                JsonValueKind.Number => writer.WriteNumber(value.GetDouble()),
                JsonValueKind.String => writer.WriteString(value.GetString()!),
                JsonValueKind.Array => WriteArray(ref writer, value),
                JsonValueKind.Object => WriteObject(ref writer, value),
                _ => writer.WriteUndefined()
            };
        }

        private static uint WriteArray(
            ref JavaScriptBinaryWriter writer,
            JsonElement value)
        {
            var result = writer.BeginArray(value.GetArrayLength());
            var index = 0;
            foreach (var item in value.EnumerateArray())
            {
                writer.SetArrayItem(
                    result,
                    index++,
                    WriteJson(ref writer, item));
            }
            return result;
        }

        private static uint WriteObject(
            ref JavaScriptBinaryWriter writer,
            JsonElement value)
        {
            var properties = value.EnumerateObject().ToArray();
            var result = writer.BeginObject(properties.Length);
            for (var index = 0; index < properties.Length; index++)
            {
                var property = properties[index];
                writer.SetObjectProperty(
                    result,
                    index,
                    Encoding.UTF8.GetBytes(property.Name),
                    WriteJson(ref writer, property.Value));
            }
            return result;
        }

        private static string ReadArgumentJson(
            ref JavaScriptBinaryWriter writer,
            uint root)
        {
            var buffer = new ArrayBufferWriter<byte>();
            using var json = new Utf8JsonWriter(buffer);
            WriteBinaryJson(
                json,
                writer.Values,
                writer.Edges,
                writer.Utf8,
                root);
            json.Flush();
            return Encoding.UTF8.GetString(buffer.WrittenSpan);
        }

        private static void WriteBinaryJson(
            Utf8JsonWriter writer,
            ReadOnlySpan<JavaScriptBinaryValueData> values,
            ReadOnlySpan<JavaScriptBinaryEdgeData> edges,
            ReadOnlySpan<byte> utf8,
            uint valueIndex)
        {
            var value = values[checked((int)valueIndex)];
            switch (value.Kind)
            {
                case JavaScriptBinaryValueKind.Undefined:
                    writer.WriteStartObject();
                    writer.WriteBoolean("__webSceneUndefined", true);
                    writer.WriteEndObject();
                    break;
                case JavaScriptBinaryValueKind.Null:
                    writer.WriteNullValue();
                    break;
                case JavaScriptBinaryValueKind.Boolean:
                    writer.WriteBooleanValue(value.Payload != 0);
                    break;
                case JavaScriptBinaryValueKind.Number:
                    writer.WriteNumberValue(BitConverter.Int64BitsToDouble(
                        unchecked((long)value.Payload)));
                    break;
                case JavaScriptBinaryValueKind.String:
                    writer.WriteStringValue(utf8.Slice(
                        checked((int)value.Offset),
                        checked((int)value.Length)));
                    break;
                case JavaScriptBinaryValueKind.Handle:
                    writer.WriteStartObject();
                    writer.WriteNumber(
                        "__webSceneHandle",
                        unchecked((long)value.Payload));
                    writer.WriteEndObject();
                    break;
                case JavaScriptBinaryValueKind.Array:
                    writer.WriteStartArray();
                    for (var index = 0U; index < value.Length; index++)
                    {
                        var edge = edges[
                            checked((int)(value.Offset + index))];
                        WriteBinaryJson(
                            writer,
                            values,
                            edges,
                            utf8,
                            edge.ValueIndex);
                    }
                    writer.WriteEndArray();
                    break;
                case JavaScriptBinaryValueKind.Object:
                    writer.WriteStartObject();
                    for (var index = 0U; index < value.Length; index++)
                    {
                        var edge = edges[
                            checked((int)(value.Offset + index))];
                        writer.WritePropertyName(utf8.Slice(
                            checked((int)edge.NameOffset),
                            checked((int)edge.NameLength)));
                        WriteBinaryJson(
                            writer,
                            values,
                            edges,
                            utf8,
                            edge.ValueIndex);
                    }
                    writer.WriteEndObject();
                    break;
                default:
                    throw new InvalidDataException(
                        $"Unexpected binary value kind {value.Kind}.");
            }
        }

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

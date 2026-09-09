using System.Runtime.Loader;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using WebScene.JavaScript.Interop.Generator;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed partial class ExternalCodecGenerationTests
{
    private static readonly MetadataReference[] References = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!)
        .Split(Path.PathSeparator).Select(path => MetadataReference.CreateFromFile(path))
        .Append(MetadataReference.CreateFromFile(typeof(NativeJavaScriptInvoker).Assembly.Location)).ToArray();

    [Theory]
    [InlineData("direct", false)]
    [InlineData("array", false)]
    [InlineData("nullable", false)]
    [InlineData("nullableArray", false)]
    [InlineData("direct", true)]
    [InlineData("array", true)]
    [InlineData("direct", true, true, true)]
    [InlineData("direct", false, true, true)]
    [InlineData("array", false, true, true)]
    [InlineData("nullable", false, true, true)]
    [InlineData("nullableArray", false, true, true)]
    [InlineData("direct", false, false, true)]
    [InlineData("array", false, true, false)]
    public void ExternalAssemblyContractsRoundTripThroughNativeInvoker(string shape, bool initializer, bool valueType = false, bool int64 = false)
    {
        var contract = initializer
            ? "public sealed record Sample { public required double Timestamp { get; init; } public double? Price { get; init; } }"
            : "public sealed record Sample(double? Price, double Timestamp);";
        if (valueType) contract = contract.Replace("sealed record", "readonly record struct");
        if (int64) contract = contract.Replace("double Timestamp", "long Timestamp");
        var construction = initializer ? "new Sample { Timestamp = 7, Price = null }" : "new Sample(null, 7)";
        var input = shape switch
        {
            "array" or "nullableArray" => "new Sample?[] { sample, null }",
            _ => "sample"
        };
        var equivalent = shape is "array" or "nullableArray"
            ? "result.Count == 2 && result[0] == sample && result[1] is null" : "result == sample";
        var bridge = $$"""
            using System;
            using System.Linq;
            using System.Runtime.InteropServices;
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    var sample = {{construction}};
                    var result = JavaScriptGlobals.ExchangeAsync(invoker, {{input}}).GetAwaiter().GetResult();
                    if (!({{equivalent}})) return false;
                    {{(shape is "nullable" or "nullableArray" ? "if (JavaScriptGlobals.ExchangeAsync(invoker, null).GetAwaiter().GetResult() is not null) return false;" : "")}}
                    return Compare(sample, invoker) && Compare(sample with { Price = 12.5 }, invoker);
                }
                private static unsafe bool Compare(Sample sample, IJavaScriptInvoker invoker)
                {
                    var external = new JavaScriptBinaryWriter();
                    var generated = new JavaScriptBinaryWriter();
                    try
                    {
                        var root = __WebSceneExternalCodec0.__WebSceneWriteBinary(ref external, sample);
                        var other = GeneratedSample.__WebSceneWriteBinary(ref generated,
                            new GeneratedSample { Time = sample.Timestamp, Value = sample.Price });
                        if (root != other || !external.Utf8.SequenceEqual(generated.Utf8)
                            || !MemoryMarshal.AsBytes(external.Values).SequenceEqual(MemoryMarshal.AsBytes(generated.Values))
                            || !MemoryMarshal.AsBytes(external.Edges).SequenceEqual(MemoryMarshal.AsBytes(generated.Edges))) return false;
                        fixed (JavaScriptBinaryValueData* values = external.Values)
                        fixed (JavaScriptBinaryEdgeData* edges = external.Edges)
                        fixed (byte* utf8 = external.Utf8)
                        {
                            var value = new JavaScriptBinaryValue(values, (uint)external.Values.Length,
                                edges, (uint)external.Edges.Length, utf8, (uint)external.Utf8.Length, root);
                            return __WebSceneExternalCodec0.__WebSceneReadBinary(value, invoker) == sample;
                        }
                    }
                    finally { external.Dispose(); generated.Dispose(); }
                }
            }
            """;
        var (result, compilation, contractImage) = Generate(contract, shape, bridge);
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contractImage));
    }

    [Theory]
    [InlineData("public sealed record Sample(string Timestamp, double? Price);", "expected 'double'")]
    [InlineData("public sealed record Sample(double Missing, double? Price);", "Timestamp")]
    [InlineData("public sealed class Sample { public double Timestamp { get; } public double? Price { get; } }", "constructor")]
    [InlineData("public abstract record Sample(double Timestamp, double? Price);", "non-abstract")]
    public void UnsupportedContractsHaveActionableDiagnostics(string contract, string reason)
    {
        var (result, compilation, _) = Generate(contract, "direct", "");
        var diagnostic = Assert.Single(result.Diagnostics);
        Assert.Equal("WEBSCENEJS004", diagnostic.Id);
        Assert.Contains(reason, diagnostic.GetMessage());
        Assert.DoesNotContain(compilation.GetDiagnostics(), diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NestedExternalContractsAndArrayPropertiesRoundTrip(bool childArray)
    {
        var propertyType = childArray ? "Child[]" : "Child";
        var expression = childArray ? "new Child[] { new Child(9) }" : "new Child(9)";
        var access = childArray ? "result.Timestamp[0].X" : "result.Timestamp.X";
        var contract = $"public sealed record Sample({propertyType} Timestamp, double? Price); public sealed record Child(double X);";
        var bridge = $$"""
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    var result = JavaScriptGlobals.ExchangeAsync(invoker, new Sample({{expression}}, null)).GetAwaiter().GetResult();
                    return {{access}} == 9 && result.Price is null;
                }
            }
            """;
        var (result, compilation, contracts) = Generate(contract, "direct", bridge, (api, policy) =>
        {
            var childReference = JsonNode.Parse("""{"kind":"reference","name":"Child","qualifiedName":"Child","typeArguments":[]} """)!;
            api["types"]![0]!["properties"]![0]!["type"] = childArray
                ? new JsonObject { ["kind"] = "array", ["element"] = childReference } : childReference;
            api["types"]!.AsArray().Add(JsonNode.Parse("""
                {"name":"Child","qualifiedName":"Child","kind":"interface","properties":[
                    {"name":"x","optional":false,"type":{"kind":"number"}}]}
                """));
            policy["typeMappings"]!["Child"] = "global::Contracts.Child";
        });
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Fact]
    public void ObjectAliasesUseExternalCodecs()
    {
        var (result, compilation, _) = Generate("public sealed record Sample(double Timestamp, double? Price);", "direct", "", (api, _) =>
        {
            var schema = api["types"]![0]!;
            schema["kind"] = "typeAlias";
            schema["aliasTarget"] = new JsonObject { ["kind"] = "inlineObject", ["properties"] = schema["properties"]!.DeepClone() };
            schema["properties"] = new JsonArray();
        });
        Assert.Empty(result.Diagnostics);
        Emit(compilation);
    }

    [Fact]
    public void RecursiveContractsAreDiagnosedWithoutRecursingIndefinitely()
    {
        var (result, compilation, _) = Generate("public sealed record Sample(Sample Timestamp, double? Price);", "direct", "", (api, _) =>
        {
            api["types"]![0]!["properties"]![0]!["type"] = JsonNode.Parse(
                """{"kind":"reference","name":"Sample","qualifiedName":"Sample","typeArguments":[]} """);
        });
        var diagnostic = Assert.Single(result.Diagnostics);
        Assert.Equal("WEBSCENEJS004", diagnostic.Id);
        Assert.Contains("recursive", diagnostic.GetMessage());
        Emit(compilation);
    }

    [Theory]
    [InlineData("typo", "Timestamp", "absent")]
    [InlineData("value", "Timestamp", "distinct")]
    public void InvalidPropertyMappingsAreDiagnosed(string jsName, string clrName, string reason)
    {
        var (result, _, _) = Generate("public sealed record Sample(double Timestamp, double? Price);", "direct", "", (_, policy) =>
        {
            policy["models"]![0]!["propertyMappings"]![jsName] = clrName;
        });
        Assert.Contains(reason, Assert.Single(result.Diagnostics).GetMessage());
    }

    [Fact]
    public void DefaultPropertyNamesNeedNoModelPolicy()
    {
        var (result, compilation, _) = Generate("public sealed record Sample(double Time, double? Value);", "direct", "", (_, policy) =>
        {
            policy["models"]!.AsArray().RemoveAt(0);
        });
        Assert.Empty(result.Diagnostics);
        Emit(compilation);
    }

    [Fact]
    public void ChartHostUsesExternalCodecsForVoidAndReturningMembers()
    {
        const string bridge = """
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    var host = ChartHost.FromReference(invoker, new JavaScriptObjectReference(47));
                    var samples = new Sample[] { new Sample(null, 7), new Sample(12.5, 8) };
                    host.SetSamplesAsync(samples).GetAwaiter().GetResult();
                    var result = host.ExchangeAsync(samples).GetAwaiter().GetResult();
                    return result.Count == 2 && result[0] == samples[0] && result[1] == samples[1];
                }
            }
            """;
        var (result, compilation, contracts) = Generate("public sealed record Sample(double? Price, double Timestamp);", "array", bridge, (api, policy) =>
        {
            var method = api["functions"]![0]!.DeepClone();
            var setter = method.DeepClone();
            setter["name"] = "setSamples";
            setter["returns"] = new JsonObject { ["kind"] = "void" };
            var root = new JsonObject
            {
                ["name"] = "ChartHost", ["qualifiedName"] = "ChartHost", ["kind"] = "interface",
                ["typeParameters"] = new JsonArray(), ["methods"] = new JsonArray(method, setter), ["properties"] = new JsonArray()
            };
            api["roots"]!.AsArray().Add(root.DeepClone());
            api["types"]!.AsArray().Add(root);
            policy["bindings"]!.AsArray().Add(JsonNode.Parse("""
                {"source":"ChartHost","name":"ChartHost","methods":[
                    {"source":"exchange","name":"ExchangeAsync","overload":0},
                    {"source":"setSamples","name":"SetSamplesAsync","overload":0}]}
                """));
        });
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Fact]
    public void Int64ConversionsPreserveSafeIntegersAndRejectLossyValues()
    {
        const string bridge = """
            using System;
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    foreach (long number in new long[] { -9007199254740991L, -1700000000123L, -1, 0, 1, 1700000000123L, 9007199254740991L })
                    {
                        var sample = new Sample(number, number);
                        if (JavaScriptGlobals.ExchangeAsync(invoker, sample).GetAwaiter().GetResult() != sample) return false;
                        if (Read(number, null, invoker) != new Sample(number, null)) return false;
                    }
                    foreach (double number in new double[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity,
                        0.5, -0.5, 9007199254740992d, -9007199254740992d, (double)long.MaxValue, (double)long.MinValue })
                    {
                        try { Read(number, null, invoker); return false; } catch (OverflowException) { }
                        try { Read(0, number, invoker); return false; } catch (OverflowException) { }
                    }
                    foreach (long number in new long[] { long.MinValue, long.MaxValue, -9007199254740992L, 9007199254740992L })
                    {
                        try { Write(new Sample(number, null)); return false; } catch (OverflowException) { }
                        try { Write(new Sample(0, number)); return false; } catch (OverflowException) { }
                    }
                    return true;
                }
                private static void Write(Sample sample)
                {
                    var writer = new JavaScriptBinaryWriter();
                    try { __WebSceneExternalCodec0.__WebSceneWriteBinary(ref writer, sample); }
                    finally { writer.Dispose(); }
                }
                private static unsafe Sample Read(double time, double? price, IJavaScriptInvoker invoker)
                {
                    var writer = new JavaScriptBinaryWriter();
                    try
                    {
                        var root = GeneratedSample.__WebSceneWriteBinary(ref writer, new GeneratedSample { Time = time, Value = price });
                        fixed (JavaScriptBinaryValueData* values = writer.Values)
                        fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                        fixed (byte* utf8 = writer.Utf8)
                        {
                            return __WebSceneExternalCodec0.__WebSceneReadBinary(new JavaScriptBinaryValue(values, (uint)writer.Values.Length,
                                edges, (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root), invoker);
                        }
                    }
                    finally { writer.Dispose(); }
                }
            }
            """;
        var (result, compilation, contracts) = Generate("public readonly record struct Sample(long Timestamp, long? Price);", "direct", bridge);
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void TradingViewBarStructArraysUseEquivalentWireFormatWithoutBoxing(bool optionalVolume)
    {
        const string contract = "public readonly record struct TradingViewBar(long TimeMilliseconds, double Open, double High, double Low, double Close, double Volume);";
        const string bridge = """
            using System;
            using System.Linq;
            using System.Runtime.InteropServices;
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static unsafe bool Run(IJavaScriptInvoker invoker)
                {
                    var bars = new TradingViewBar[32];
                    for (int i = 0; i < bars.Length; i++) bars[i] = new TradingViewBar(1700000000123L + i, 1.5, 3, 1, 2.5, 100);
                    var result = JavaScriptGlobals.ExchangeAsync(invoker, bars).GetAwaiter().GetResult();
                    if (!bars.SequenceEqual(result)) return false;
                    var external = new JavaScriptBinaryWriter();
                    var generated = new JavaScriptBinaryWriter();
                    try
                    {
                        var root = external.BeginArray(bars.Length);
                        var other = generated.BeginArray(bars.Length);
                        for (int i = 0; i < bars.Length; i++)
                        {
                            var bar = bars[i];
                            external.SetArrayItem(root, i, __WebSceneExternalCodec0.__WebSceneWriteBinary(ref external, bar));
                            generated.SetArrayItem(other, i, GeneratedSample.__WebSceneWriteBinary(ref generated,
                                new GeneratedSample { Time = bar.TimeMilliseconds, Open = bar.Open, High = bar.High,
                                    Low = bar.Low, Close = bar.Close, Volume = bar.Volume }));
                        }
                        return external.Utf8.SequenceEqual(generated.Utf8)
                            && MemoryMarshal.AsBytes(external.Values).SequenceEqual(MemoryMarshal.AsBytes(generated.Values))
                            && MemoryMarshal.AsBytes(external.Edges).SequenceEqual(MemoryMarshal.AsBytes(generated.Edges));
                    }
                    finally { external.Dispose(); generated.Dispose(); }
                }
            }
            """;
        var (result, compilation, contracts) = Generate(contract, "array", bridge, (api, policy) =>
        {
            foreach (var schema in api["types"]!.AsArray())
            {
                var properties = new JsonArray();
                foreach (var name in new[] { "time", "open", "high", "low", "close", "volume" })
                    properties.Add(new JsonObject { ["name"] = name, ["optional"] = false, ["type"] = new JsonObject { ["kind"] = "number" } });
                schema!["properties"] = properties;
            }
            foreach (var type in new[] { api["functions"]![0]!["parameters"]![0]!["type"]!, api["functions"]![0]!["returns"]! })
                type["element"] = JsonNode.Parse("""{"kind":"reference","name":"Sample","qualifiedName":"Sample","typeArguments":[]} """);
            policy["typeMappings"]!["Sample"] = "global::Contracts.TradingViewBar";
            policy["models"]![0]!["propertyMappings"] = new JsonObject { ["time"] = "TimeMilliseconds" };
            if (optionalVolume)
            {
                api["types"]![0]!["properties"]![5]!["optional"] = true;
                policy["models"]![0]!["optionalProperties"] = JsonNode.Parse(
                    """{"volume":{"write":"always","read":"default","default":0}} """);
            }
        });
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Fact]
    public void NullableStructMappingsAcceptClrNamesWithoutGlobalPrefix()
    {
        var (result, compilation, _) = Generate("public readonly record struct Sample(long Timestamp, double? Price);", "nullable", "", (_, policy) =>
        {
            policy["typeMappings"]!["Sample"] = "Contracts.Sample";
        });
        Assert.Empty(result.Diagnostics);
        Emit(compilation);
    }

    private static bool Run(Compilation compilation, byte[] contracts, IJavaScriptBinaryTransport? transport = null)
    {
        AssemblyLoadContext.Default.LoadFromStream(new MemoryStream(contracts));
        var assembly = AssemblyLoadContext.Default.LoadFromStream(new MemoryStream(Emit(compilation)));
        using var invoker = new NativeJavaScriptInvoker(transport ?? new EchoTransport());
        return (bool)assembly.GetType("Generated.Bridge")!.GetMethod("Run")!.Invoke(null, [invoker])!;
    }

    private static (GeneratorDriverRunResult Result, Compilation Compilation, byte[] ContractImage) Generate(
        string contract, string shape, string bridge, Action<JsonNode, JsonNode>? configure = null)
    {
        var contractCompilation = CSharpCompilation.Create("Contracts" + Guid.NewGuid().ToString("N"),
            [CSharpSyntaxTree.ParseText("#nullable enable\nnamespace Contracts; " + contract)], References,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        var contractImage = Emit(contractCompilation);
        var sample = JsonNode.Parse("""
            {"kind":"reference","name":"Sample","qualifiedName":"Sample","typeArguments":[]}
            """)!;
        var nullableSample = new JsonObject { ["kind"] = "union", ["types"] = new JsonArray(sample.DeepClone(), new JsonObject { ["kind"] = "null" }) };
        JsonNode type = shape switch
        {
            "nullable" => nullableSample,
            "array" or "nullableArray" => new JsonObject { ["kind"] = "array", ["element"] = nullableSample },
            _ => sample
        };
        if (shape == "nullableArray")
            type = new JsonObject { ["kind"] = "union", ["types"] = new JsonArray(type, new JsonObject { ["kind"] = "null" }) };
        var schema = JsonNode.Parse("""
            {"name":"Sample","qualifiedName":"Sample","kind":"interface","typeParameters":[],"indexSignatures":[],
             "properties":[{"name":"time","optional":false,"type":{"kind":"number"}},
                           {"name":"value","optional":false,"type":{"kind":"union","types":[{"kind":"number"},{"kind":"null"}]}}]}
            """)!;
        var generatedSchema = schema.DeepClone();
        generatedSchema["name"] = "GeneratedSample";
        generatedSchema["qualifiedName"] = "GeneratedSample";
        var api = $$"""
            {"schemaVersion":"1.0","roots":[],"types":[{{schema}},{{generatedSchema}}],
             "functions":[{"name":"exchange","qualifiedName":"exchange","overload":0,"typeParameters":[],
                "parameters":[{"name":"sample","optional":false,"rest":false,"type":{{type}}}],"returns":{{type}}}]}
            """;
        const string policy = """
            {"schemaVersion":"1.0","api":"test.webscene-interop-api.json","namespace":"Generated","bindings":[],
             "models":[{"source":"Sample","include":false,"propertyMappings":{"time":"Timestamp","value":"Price"}},
                       {"source":"GeneratedSample","name":"GeneratedSample"}],
             "typeMappings":{"Sample":"global::Contracts.Sample"},
             "functions":[{"source":"exchange","globalName":"exchange","name":"ExchangeAsync","overload":0}]}
            """;
        var apiNode = JsonNode.Parse(api)!;
        var policyNode = JsonNode.Parse(policy)!;
        configure?.Invoke(apiNode, policyNode);
        var compilation = CSharpCompilation.Create("Integration" + Guid.NewGuid().ToString("N"),
            [CSharpSyntaxTree.ParseText(bridge)], References.Append(MetadataReference.CreateFromImage(contractImage)),
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true));
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            [new ManifestJavaScriptBindingGenerator().AsSourceGenerator()],
            [new Input("test.webscene-interop-api.json", apiNode.ToJsonString()), new Input("test.webscene-interop-policy.json", policyNode.ToJsonString())]);
        driver = driver.RunGeneratorsAndUpdateCompilation(compilation, out var updated, out _);
        return (driver.GetRunResult(), updated, contractImage);
    }

    private static byte[] Emit(Compilation compilation)
    {
        using var stream = new MemoryStream();
        var result = compilation.Emit(stream);
        Assert.True(result.Success, string.Join("\n", result.Diagnostics));
        return stream.ToArray();
    }

    private sealed class Input(string path, string content) : AdditionalText
    {
        public override string Path => path;
        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(content);
    }

    private sealed class EchoTransport : IJavaScriptBinaryTransport
    {
        public unsafe ValueTask<TResult> InvokeAsync<TArguments, TResult, TCodec>(IJavaScriptInvoker invoker,
            JavaScriptBinaryCallSite callSite, JavaScriptObjectReference target, TArguments arguments,
            CancellationToken cancellationToken = default) where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>
        {
            VerifyPooledEncoding<TArguments, TCodec>(in arguments);
            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = TCodec.EncodeArguments(ref writer, in arguments);
                fixed (JavaScriptBinaryValueData* values = writer.Values)
                fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                fixed (byte* utf8 = writer.Utf8)
                {
                    var value = new JavaScriptBinaryValue(values, (uint)writer.Values.Length, edges,
                        (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root);
                    return ValueTask.FromResult(TCodec.DecodeResult(value.GetArrayItem(0), invoker));
                }
            }
            finally { writer.Dispose(); }
        }
        public ValueTask InvokeVoidAsync<TArguments, TCodec>(IJavaScriptInvoker invoker, JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target, TArguments arguments, CancellationToken cancellationToken = default)
            where TCodec : struct, IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            VerifyPooledEncoding<TArguments, TCodec>(in arguments);
            return ValueTask.CompletedTask;
        }
        public ValueTask<JavaScriptBinaryResultLease> InvokeBorrowedAsync<TArguments, TCodec>(JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target, TArguments arguments, CancellationToken cancellationToken = default)
            where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments> => throw new NotSupportedException();
        private static void VerifyPooledEncoding<TArguments, TCodec>(in TArguments arguments)
            where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments>
        {
            static void Encode(in TArguments value)
            {
                var writer = new JavaScriptBinaryWriter();
                try { TCodec.EncodeArguments(ref writer, in value); }
                finally { writer.Dispose(); }
            }
            Encode(in arguments);
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var index = 0; index < 100; index++) Encode(in arguments);
            Assert.InRange(GC.GetAllocatedBytesForCurrentThread() - before, 0, 1024);
        }

        public void Dispose() { }
    }
}

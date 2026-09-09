using System.Text.Json.Nodes;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed partial class ExternalCodecGenerationTests
{
    [Theory]
    [InlineData("string?", "default", false)]
    [InlineData("bool?", "default", true)]
    [InlineData("double?", "null", false)]
    [InlineData("double?", "reject", false)]
    [InlineData("double?", "default", true)]
    [InlineData("double", "reject", false)]
    [InlineData("double", "default", true)]
    [InlineData("long?", "null", true)]
    [InlineData("long", "default", true)]
    [InlineData("Child?", "null", false)]
    [InlineData("Child?", "reject", true)]
    public void OrdinaryOptionalPropertiesHaveExplicitWireSemantics(string clrType, string read, bool valueType)
    {
        var nullable = clrType.EndsWith('?');
        var child = clrType == "Child?";
        var supplied = child ? "new Child(8)" : clrType == "string?" ? "\"ready\"" : clrType == "bool?" ? "true" : "8";
        var defaultValue = read == "null" ? "null" : clrType == "string?" ? "\"fallback\"" : clrType == "bool?" ? "false" : "-4";
        var wireKind = child ? "Object" : clrType == "string?" ? "String" : clrType == "bool?" ? "Boolean" : "Number";
        var writeValue = child ? "__WebSceneExternalCodec0.__WebSceneWriteBinary(ref writer, new Child(8))"
            : clrType == "string?" ? "writer.WriteString(\"ready\")" : clrType == "bool?" ? "writer.WriteBoolean(true)" : "writer.WriteNumber(8)";
        var sampleCodec = child ? "__WebSceneExternalCodec1" : "__WebSceneExternalCodec0";
        var contract = $"public {(valueType ? "readonly record struct" : "sealed record")} Sample(double Timestamp, {clrType} Price);"
            + (child ? "public sealed record Child(double X);" : "");
        var bridge = $$"""
            using System;
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    var sample = new Sample(7, {{supplied}});
                    var array = new Sample?[] { sample, null };
                    var result = JavaScriptGlobals.ExchangeAsync(invoker, array).GetAwaiter().GetResult();
                    if (result.Count != 2 || result[0] != sample || result[1] is not null) return false;
                    for (var state = 0; state <= 1; state++)
                    {
                        {{(read == "reject" ? "try { Read(state, invoker); return false; } catch (InvalidOperationException) { }" : "if (Read(state, invoker).Price != " + defaultValue + ") return false;")}}
                    }
                    {{(nullable ? "if (Read(2, invoker).Price is not null) return false;" : "try { Read(2, invoker); return false; } catch (InvalidOperationException) { }")}}
                    if (Read(3, invoker) != sample) return false;
                    return CheckWrite(sample) {{(nullable ? "&& CheckWrite(new Sample(7, null))" : "")}};
                }
                private static unsafe bool CheckWrite(Sample sample)
                {
                    var writer = new JavaScriptBinaryWriter();
                    try
                    {
                        var root = {{sampleCodec}}.__WebSceneWriteBinary(ref writer, sample);
                        fixed (JavaScriptBinaryValueData* values = writer.Values)
                        fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                        fixed (byte* utf8 = writer.Utf8)
                        {
                            var value = new JavaScriptBinaryValue(values, (uint)writer.Values.Length, edges,
                                (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root);
                            if (value.Count != 2) return false;
                            var price = value.GetRequiredProperty("value"u8);
                            return price.Kind == {{(nullable ? "(sample.Price is null ? JavaScriptBinaryValueKind.Null : " : "")}}
                                JavaScriptBinaryValueKind.{{wireKind}}{{(nullable ? ")" : "")}};
                        }
                    }
                    finally { writer.Dispose(); }
                }
                private static unsafe Sample Read(int state, IJavaScriptInvoker invoker)
                {
                    var writer = new JavaScriptBinaryWriter();
                    try
                    {
                        var root = writer.BeginObject(state == 0 ? 1 : 2);
                        writer.SetObjectProperty(root, 0, "time"u8, writer.WriteNumber(7));
                        if (state != 0)
                        {
                            uint price;
                            if (state == 1) price = writer.WriteUndefined();
                            else if (state == 2) price = writer.WriteNull();
                            else price = {{writeValue}};
                            writer.SetObjectProperty(root, 1, "value"u8, price);
                        }
                        fixed (JavaScriptBinaryValueData* values = writer.Values)
                        fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                        fixed (byte* utf8 = writer.Utf8)
                        {
                            return {{sampleCodec}}.__WebSceneReadBinary(new JavaScriptBinaryValue(values, (uint)writer.Values.Length,
                                edges, (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root), invoker);
                        }
                    }
                    finally { writer.Dispose(); }
                }
            }
            """;
        var (result, compilation, contracts) = Generate(contract, "array", bridge, (api, policy) =>
        {
            var property = api["types"]![0]!["properties"]![1]!;
            property["optional"] = true;
            var payload = child ? JsonNode.Parse("""{"kind":"reference","name":"Child","qualifiedName":"Child","typeArguments":[]} """)!
                : new JsonObject { ["kind"] = clrType == "string?" ? "string" : clrType == "bool?" ? "boolean" : "number" };
            var types = new JsonArray(payload, new JsonObject { ["kind"] = "undefined" });
            if (nullable) types.Add(new JsonObject { ["kind"] = "null" });
            property["type"] = new JsonObject { ["kind"] = "union", ["types"] = types };
            var settings = new JsonObject { ["write"] = "always", ["read"] = read };
            if (read == "default") settings["default"] = JsonNode.Parse(defaultValue);
            policy["models"]![0]!["optionalProperties"] = new JsonObject { ["value"] = settings };
            if (child)
            {
                policy["typeMappings"]!["Child"] = "global::Contracts.Child";
                api["types"]!.AsArray().Add(JsonNode.Parse("""
                    {"name":"Child","qualifiedName":"Child","kind":"interface","properties":[
                        {"name":"x","optional":false,"type":{"kind":"number"}}]}
                    """));
            }
        });
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Theory]
    [InlineData("{}")]
    [InlineData("{\"write\":\"omitNull\",\"read\":\"null\"}")]
    [InlineData("{\"write\":\"always\",\"read\":\"default\"}")]
    [InlineData("{\"write\":\"always\",\"read\":\"default\",\"default\":\"bad\"}")]
    [InlineData("{\"write\":\"always\",\"read\":\"reject\",\"default\":0}")]
    [InlineData("{\"write\":\"always\",\"read\":\"null\",\"typo\":true}")]
    public void InvalidOptionalPoliciesAreDiagnosed(string settings)
    {
        var (result, _, _) = Generate("public sealed record Sample(double Timestamp, double? Price);", "direct", "", (api, policy) =>
        {
            api["types"]![0]!["properties"]![1]!["optional"] = true;
            policy["models"]![0]!["optionalProperties"] = new JsonObject { ["value"] = JsonNode.Parse(settings) };
        });
        Assert.Equal("WEBSCENEJS004", Assert.Single(result.Diagnostics).Id);
    }
    [Fact]
    public void OptionalWrappersStillPreserveAbsenceWithoutPolicy()
    {
        const string bridge = """
            using System;
            using Contracts;
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static unsafe bool Run(IJavaScriptInvoker invoker)
                {
                    for (int state = 0; state < 4; state++)
                    {
                        var writer = new JavaScriptBinaryWriter();
                        try
                        {
                            var root = writer.BeginObject(state == 0 ? 1 : 2);
                            writer.SetObjectProperty(root, 0, "time"u8, writer.WriteNumber(7));
                            if (state > 0) writer.SetObjectProperty(root, 1, "value"u8,
                                state == 1 ? writer.WriteUndefined() : state == 2 ? writer.WriteNull() : writer.WriteNumber(8));
                            Sample sample;
                            fixed (JavaScriptBinaryValueData* values = writer.Values)
                            fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                            fixed (byte* utf8 = writer.Utf8)
                                sample = __WebSceneExternalCodec0.__WebSceneReadBinary(new JavaScriptBinaryValue(values,
                                    (uint)writer.Values.Length, edges, (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root), invoker);
                            if (sample.Price.HasValue != (state >= 2)) return false;
                            if (state == 2 && sample.Price.Value is not null) return false;
                            if (state == 3 && sample.Price.Value != 8) return false;
                            var output = new JavaScriptBinaryWriter();
                            try
                            {
                                var outputRoot = __WebSceneExternalCodec0.__WebSceneWriteBinary(ref output, sample);
                                if (output.Values[(int)outputRoot].Length != (state >= 2 ? 2 : 1)) return false;
                                var result = JavaScriptGlobals.ExchangeAsync(invoker, sample).GetAwaiter().GetResult();
                                if (result != sample) return false;
                            }
                            finally { output.Dispose(); }
                        }
                        finally { writer.Dispose(); }
                    }
                    return true;
                }
            }
            """;
        var (result, compilation, contracts) = Generate(
            "public sealed record Sample(double Timestamp, global::WebScene.JavaScript.Interop.JavaScriptOptional<double?> Price);",
            "direct", bridge, (api, _) =>
            {
                var property = api["types"]![0]!["properties"]![1]!;
                property["optional"] = true;
                property["type"]!["types"]!.AsArray().Add(new JsonObject { ["kind"] = "undefined" });
            });
        Assert.Empty(result.Diagnostics);
        Assert.True(Run(compilation, contracts));
    }

    [Theory]
    [InlineData("double", "value", "null", "null", false)]
    [InlineData("double?", "typo", "reject", "null", false)]
    [InlineData("double?", "value", "reject", "null", true)]
    [InlineData("long?", "value", "default", "9007199254740992", false)]
    [InlineData("long?", "value", "default", "1.5", false)]
    public void OptionalPolicyMustMatchSchemaAndClrType(string clrType, string key, string read, string fallback, bool required)
    {
        var (result, _, _) = Generate($"public sealed record Sample(double Timestamp, {clrType} Price);", "direct", "", (api, policy) =>
        {
            var property = api["types"]![0]!["properties"]![1]!;
            property["optional"] = !required;
            if (clrType == "double") property["type"] = new JsonObject { ["kind"] = "number" };
            var settings = new JsonObject { ["write"] = "always", ["read"] = read };
            if (read == "default") settings["default"] = JsonNode.Parse(fallback);
            policy["models"]![0]!["optionalProperties"] = new JsonObject { [key] = settings };
        });
        Assert.Equal("WEBSCENEJS004", Assert.Single(result.Diagnostics).Id);
    }

}

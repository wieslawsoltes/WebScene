using System.Text.Json.Nodes;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed partial class ExternalCodecGenerationTests
{
    [Theory]
    [InlineData(false, false, false, 0)]
    [InlineData(true, false, false, 0)]
    [InlineData(true, false, false, 1)]
    [InlineData(true, false, false, 2)]
    [InlineData(false, true, false, 1)]
    [InlineData(false, true, true, 0)]
    [InlineData(false, true, true, 2)]
    public void AdapterPropertiesReturnOwnedNativeHandles(bool optional, bool nullable, bool promise, int state)
    {
        var bridge = $$"""
            using WebScene.JavaScript.Interop;
            namespace Generated;
            public static class Bridge
            {
                public static bool Run(IJavaScriptInvoker invoker)
                {
                    var host = Host.FromReference(invoker, new JavaScriptObjectReference(47));
                    JavaScriptObjectReference? local = host.GetBridgeAsync().GetAwaiter().GetResult();
                    JavaScriptObjectReference? global = JavaScriptGlobals.GetBridgeAsync(invoker).GetAwaiter().GetResult();
                    {{(state == 0 ? "if (local?.Id != 501 || global?.Id != 502) return false; invoker.ReleaseAsync(local.Value).GetAwaiter().GetResult(); invoker.ReleaseAsync(global.Value).GetAwaiter().GetResult();" : "if (local is not null || global is not null) return false;")}}
                    return true;
                }
            }
            """;
        var (result, compilation, contracts) = Generate("public sealed record Sample(double Timestamp, double? Price);", "direct", bridge, (api, policy) =>
        {
            var adapter = JsonNode.Parse("""
                {"name":"CallbackBridge","qualifiedName":"CallbackBridge","kind":"interface","typeParameters":[],"methods":[],"properties":[]}
                """)!;
            api["types"]!.AsArray().Add(adapter);
            policy["adapters"] = JsonNode.Parse("""[{"source":"CallbackBridge","name":"CallbackBridge"}]""");
            JsonNode type = JsonNode.Parse("""{"kind":"reference","name":"CallbackBridge","qualifiedName":"CallbackBridge","typeArguments":[]}""")!;
            if (nullable) type = new JsonObject { ["kind"] = "union", ["types"] = new JsonArray(type, new JsonObject { ["kind"] = "null" }) };
            if (promise) type = new JsonObject { ["kind"] = "promise", ["result"] = type };
            var property = new JsonObject { ["name"] = "bridge", ["optional"] = optional, ["readonly"] = true, ["type"] = type };
            var root = new JsonObject { ["name"] = "Host", ["qualifiedName"] = "Host", ["kind"] = "interface",
                ["typeParameters"] = new JsonArray(), ["methods"] = new JsonArray(), ["properties"] = new JsonArray(property) };
            api["roots"]!.AsArray().Add(root.DeepClone());
            api["types"]!.AsArray().Add(root);
            policy["bindings"] = JsonNode.Parse("""[{"source":"Host","name":"Host","properties":[{"source":"bridge","getterName":"GetBridgeAsync"}]}]""");
            var global = property.DeepClone();
            global["qualifiedName"] = "bridge";
            api["globals"] = new JsonArray(global);
            policy["globalProperties"] = JsonNode.Parse("""[{"source":"bridge","globalName":"bridge","getterName":"GetBridgeAsync"}]""");
        });
        Assert.Empty(result.Diagnostics);
        var transport = new PropertyTransport(state, promise);
        Assert.True(Run(compilation, contracts, transport));
        Assert.Equal(2, transport.Calls);
        Assert.Equal(state == 0 ? new long[] { 501, 502 } : [], transport.Released);
    }

    private sealed class PropertyTransport(int state, bool promise) : IJavaScriptBinaryTransport
    {
        public int Calls { get; private set; }
        public List<long> Released { get; } = [];

        public unsafe ValueTask<TResult> InvokeAsync<TArguments, TResult, TCodec>(IJavaScriptInvoker invoker,
            JavaScriptBinaryCallSite callSite, JavaScriptObjectReference target, TArguments arguments,
            CancellationToken cancellationToken = default) where TCodec : struct, IJavaScriptBinaryCodec<TArguments, TResult>
        {
            Assert.Equal(JavaScriptBinaryResultMode.RetainedHandle, callSite.ResultMode);
            Assert.Equal(promise ? JavaScriptBinaryCallFlags.AwaitPromise : JavaScriptBinaryCallFlags.None, callSite.Flags);
            Assert.Equal(Calls == 0 ? JavaScriptBinaryOperation.GetProperty : JavaScriptBinaryOperation.GetGlobal, callSite.Operation);
            Assert.Equal(Calls == 0 ? 47 : 0, target.Id);
            Calls++;
            var writer = new JavaScriptBinaryWriter();
            try
            {
                var root = state == 1 ? writer.WriteNull() : state == 2 ? writer.WriteUndefined()
                    : writer.WriteHandle(new JavaScriptObjectReference(500 + Calls));
                fixed (JavaScriptBinaryValueData* values = writer.Values)
                fixed (JavaScriptBinaryEdgeData* edges = writer.Edges)
                fixed (byte* utf8 = writer.Utf8)
                    return ValueTask.FromResult(TCodec.DecodeResult(new JavaScriptBinaryValue(values, (uint)writer.Values.Length,
                        edges, (uint)writer.Edges.Length, utf8, (uint)writer.Utf8.Length, root), invoker));
            }
            finally { writer.Dispose(); }
        }

        public ValueTask InvokeVoidAsync<TArguments, TCodec>(IJavaScriptInvoker invoker, JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target, TArguments arguments, CancellationToken cancellationToken = default)
            where TCodec : struct, IJavaScriptBinaryCodec<TArguments, JavaScriptBinaryVoid>
        {
            Assert.Equal(JavaScriptBinaryOperation.ReleaseHandle, callSite.Operation);
            Released.Add(target.Id);
            return ValueTask.CompletedTask;
        }
        public ValueTask<JavaScriptBinaryResultLease> InvokeBorrowedAsync<TArguments, TCodec>(JavaScriptBinaryCallSite callSite,
            JavaScriptObjectReference target, TArguments arguments, CancellationToken cancellationToken = default)
            where TCodec : struct, IJavaScriptBinaryArgumentsCodec<TArguments> => throw new NotSupportedException();
        public void Dispose() { }
    }
}

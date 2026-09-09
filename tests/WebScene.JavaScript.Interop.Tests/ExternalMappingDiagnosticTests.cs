using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using WebScene.JavaScript.Interop.Generator;
using Xunit;

namespace WebScene.JavaScript.Interop.Tests;

public sealed class ExternalMappingDiagnosticTests
{
    [Theory]
    [InlineData("direct", false)]
    [InlineData("array", false)]
    [InlineData("nullable", false)]
    [InlineData("direct", true)]
    [InlineData("array", true)]
    [InlineData("nullable", true)]
    public void ExternalContractsReportMissingNativeCodec(string shape, bool returnsValue)
    {
        var result = Generate(shape, returnsValue, "global::Application.Contracts.Sample");
        var diagnostic = Assert.Single(result.Diagnostics);
        Assert.Equal("WEBSCENEJS004", diagnostic.Id);
        Assert.Equal(DiagnosticSeverity.Warning, diagnostic.Severity);
        Assert.Contains("Example.Sample", diagnostic.GetMessage());
        Assert.Contains("global::Application.Contracts.Sample", diagnostic.GetMessage());
        Assert.Contains("NativeJavaScriptInvoker", diagnostic.GetMessage());
        Assert.NotEmpty(result.GeneratedTrees);
    }

    [Theory]
    [InlineData("global::WebScene.JavaScript.Interop.JavaScriptObjectReference")]
    [InlineData("global::WebScene.JavaScript.Interop.JavaScriptFunctionReference")]
    public void NativeHandleMappingsDoNotReportMissingCodec(string mapping)
    {
        Assert.Empty(Generate("direct", false, mapping).Diagnostics);
    }

    [Theory]
    [InlineData("Sample")]
    [InlineData("Example.Sample")]
    [InlineData("DisplayedSample")]
    public void ResolvesAllPolicyMappingKeys(string key)
    {
        Assert.Equal("WEBSCENEJS004", Assert.Single(
            Generate("direct", false, "global::Application.Contracts.Sample", key).Diagnostics).Id);
    }

    [Fact]
    public void UnusedMappingsDoNotWarn()
    {
        Assert.DoesNotContain(
            Generate("direct", false, "global::Application.Contracts.Sample", "Unused").Diagnostics,
            diagnostic => diagnostic.Id == "WEBSCENEJS004");
    }

    private static GeneratorDriverRunResult Generate(string shape, bool returnsValue, string mapping, string key = "Example.Sample")
    {
        const string reference = """
            {"kind":"reference","name":"Sample","qualifiedName":"Example.Sample","display":"DisplayedSample","typeArguments":[]}
            """;
        var type = shape switch
        {
            "array" => "{\"kind\":\"array\",\"element\":" + reference + "}",
            "nullable" => "{\"kind\":\"union\",\"types\":[" + reference + ",{\"kind\":\"null\"}]}",
            _ => reference
        };
        var parameters = returnsValue ? "[]" : "[{\"name\":\"sample\",\"optional\":false,\"rest\":false,\"type\":" + type + "}]";
        var returns = returnsValue ? type : "{\"kind\":\"void\"}";
        var api = $$"""
            {
              "schemaVersion":"1.0", "roots":[], "types":[],
              "functions":[{"name":"exchange","qualifiedName":"exchange","overload":0,
                "typeParameters":[],"parameters":{{parameters}},"returns":{{returns}}}]
            }
            """;
        var policy = $$"""
            {
              "schemaVersion":"1.0", "api":"test.webscene-interop-api.json", "namespace":"Generated",
              "bindings":[], "models":[{"source":"Example.Sample","name":"Sample","include":false}],
              "typeMappings":{"{{key}}":"{{mapping}}"},
              "functions":[{"source":"exchange","globalName":"exchange","name":"ExchangeAsync","overload":0}]
            }
            """;
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            generators: [new ManifestJavaScriptBindingGenerator().AsSourceGenerator()],
            additionalTexts: [new Input("test.webscene-interop-api.json", api), new Input("test.webscene-interop-policy.json", policy)]);
        return driver.RunGenerators(CSharpCompilation.Create("ExternalMappingTests")).GetRunResult();
    }

    private sealed class Input(string path, string content) : AdditionalText
    {
        public override string Path => path;
        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(content);
    }
}

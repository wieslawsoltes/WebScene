using WebScene.Testing;
using Xunit;

namespace WebScene.Architecture.Tests;

public sealed class NativeAbiExportTests
{
    [Fact]
    public void MacOsExportListMatchesEveryPublicAbiFunction()
    {
        var declarations = ReadPublicFunctions();
        var exports = ReadMacOsExports();

        Assert.NotEmpty(declarations);
        Assert.Empty(declarations.Except(exports, StringComparer.Ordinal));
        Assert.Empty(exports.Except(declarations, StringComparer.Ordinal));
        Assert.Equal(exports.Length, exports.Distinct(StringComparer.Ordinal).Count());
    }

    [Theory]
    [InlineData("webscene_engine_configure_diagnostics")]
    [InlineData("webscene_engine_take_diagnostic")]
    [InlineData("webscene_engine_copy_runtime_failure")]
    public void MissingDiagnosticExportIsDetected(string omittedFunction)
    {
        var declarations = ReadPublicFunctions();
        var exports = ReadMacOsExports().Where(name => name != omittedFunction);

        Assert.Contains(omittedFunction, declarations);
        Assert.Contains(omittedFunction, declarations.Except(exports, StringComparer.Ordinal));
    }

    [Fact]
    public void PublicDeclarationParserHandlesPointersAndMultilineSignatures()
    {
        const string header = """
            #define WEBSCENE_API __attribute__((visibility("default")))
            /*
            WEBSCENE_API void webscene_not_an_export();
            */
            WEBSCENE_API const webscene_scene_view* webscene_pointer(webscene_engine* engine);
            WEBSCENE_API size_t
                webscene_multiline(
                    webscene_engine* engine, char* destination);
            #if defined(WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_BENCHMARK_COUNTERS)
            WEBSCENE_API void webscene_benchmark_only(void);
            #endif
            WEBSCENE_API void webscene_after_benchmark(void);
            """;
        Assert.Equal(new[] { "webscene_pointer", "webscene_multiline", "webscene_after_benchmark" },
            NativeAbiContract.PublicFunctions(header));
    }

    private static string[] ReadPublicFunctions() =>
        NativeAbiContract.PublicFunctions(File.ReadAllText(Path.Combine(NativeDirectory(), "webscene_native_engine.h")));

    private static string[] ReadMacOsExports() =>
        File.ReadAllLines(Path.Combine(NativeDirectory(), "webscene_native_engine.exports"))
            .Select(line => line.Trim()).Where(line => line.Length > 0)
            .Select(line => line.TrimStart('_')).ToArray();

    private static string NativeDirectory()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln")))
                return Path.Combine(directory.FullName, "experiments", "WebScene.NativeEngine.Probe", "native");
            directory = directory.Parent;
        }
        throw new DirectoryNotFoundException("Could not locate the WebScene repository root.");
    }
}

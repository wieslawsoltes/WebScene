using System.Text.RegularExpressions;

namespace WebScene.Testing;

internal static class NativeAbiContract
{
    internal static string[] PublicFunctions(string header)
    {
        header = Regex.Replace(header, @"/\*.*?\*/|//[^\r\n]*", "", RegexOptions.Singleline);
        // These instrumentation-only declarations are absent from release builds.
        // Keep all other declarations, including the Inspector ABI stubs.
        header = Regex.Replace(header,
            @"^\s*#if defined\(WEBSCENE_NATIVE_ENGINE_\w+_BENCHMARK_COUNTERS\)[^\r\n]*\r?\n.*?^\s*#endif[^\r\n]*",
            "", RegexOptions.Multiline | RegexOptions.Singleline);
        return Regex.Matches(header,
                @"^\s*WEBSCENE_API\s+[^;]*?\b(webscene_\w+)\s*\(", RegexOptions.Multiline)
            .Select(match => match.Groups[1].Value).ToArray();
    }
}

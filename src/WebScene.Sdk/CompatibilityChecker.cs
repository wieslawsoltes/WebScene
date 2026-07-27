using System.Text.RegularExpressions;

namespace WebScene.Sdk;

public enum WebSceneCompatibilitySeverity
{
    Warning,
    Error
}

public sealed record WebSceneCompatibilityDiagnostic(
    string Code,
    WebSceneCompatibilitySeverity Severity,
    string Message,
    string Source,
    int Line,
    int Column,
    string? RequiredCapability = null);

public sealed record WebSceneCompatibilityReport(IReadOnlyList<WebSceneCompatibilityDiagnostic> Diagnostics)
{
    public bool IsCompatible => Diagnostics.All(static diagnostic => diagnostic.Severity != WebSceneCompatibilitySeverity.Error);
}

/// <summary>Static preflight for the deliberately bounded WebScene Component Profile 1 surface.</summary>
public static partial class WebSceneCompatibilityChecker
{
    private sealed record Rule(
        Regex Pattern,
        string Code,
        WebSceneCompatibilitySeverity Severity,
        string Message,
        string? Capability = null);

    private static readonly Rule[] s_rules =
    [
        Unsupported(@"\bnavigator\s*\.\s*serviceWorker\b", "WEBSCENE1001", "Service workers are not supported."),
        Unsupported(@"\b(?:localStorage|sessionStorage|indexedDB)\b", "WEBSCENE1002", "Browser storage is not supported; request host.settings instead."),
        Unsupported(@"\b(?:Worker|SharedWorker|Worklet)\s*\(", "WEBSCENE1003", "Web workers and worklets are not supported."),
        Unsupported(@"\b(?:RTCPeerConnection|MediaRecorder|AudioContext|webkitAudioContext)\b", "WEBSCENE1004", "WebRTC, recording, and Web Audio are not supported."),
        Unsupported(@"\bnavigator\s*\.\s*(?:mediaDevices|geolocation)\b", "WEBSCENE1005", "Media devices and geolocation are not supported."),
        Unsupported(@"\bwindow\s*\.\s*open\s*\(", "WEBSCENE1006", "Arbitrary browser windows and navigation are not supported."),
        Requires(@"\bnavigator\s*\.\s*clipboard\b", "WEBSCENE2001", WebSceneComponentCapabilities.Clipboard, "Clipboard access must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*commands\b", "WEBSCENE2002", WebSceneComponentCapabilities.Commands, "Host commands must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*settings\b", "WEBSCENE2003", WebSceneComponentCapabilities.Settings, "Host settings must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*notifications\b", "WEBSCENE2004", WebSceneComponentCapabilities.Notifications, "Host notifications must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*network\b", "WEBSCENE2005", WebSceneComponentCapabilities.Networking, "Host networking must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*clipboard\b", "WEBSCENE2006", WebSceneComponentCapabilities.HostClipboard, "Host clipboard access must be declared."),
        Requires(@"\bwebscene\s*\.\s*host\s*\.\s*files\b", "WEBSCENE2007", WebSceneComponentCapabilities.FileSelection, "Host file selection must be declared."),
        new Rule(GeneratedNetworkPattern(), "WEBSCENE3001", WebSceneCompatibilitySeverity.Warning, "Direct networking bypasses host policy; prefer webscene.host.network.")
    ];

    public static WebSceneCompatibilityReport Check(
        string source,
        WebSceneComponentManifest manifest,
        string sourceName = "<source>")
    {
        ArgumentNullException.ThrowIfNull(source);
        WebSceneComponentManifestSerializer.Validate(manifest).ThrowIfInvalid();
        var diagnostics = new List<WebSceneCompatibilityDiagnostic>();
        var searchable = MaskCommentsAndStrings(source);
        foreach (var rule in s_rules)
        {
            foreach (Match match in rule.Pattern.Matches(searchable))
            {
                if (rule.Capability is not null
                    && manifest.Capabilities.Contains(rule.Capability, StringComparer.Ordinal))
                {
                    continue;
                }
                var (line, column) = GetLocation(source, match.Index);
                diagnostics.Add(new WebSceneCompatibilityDiagnostic(
                    rule.Code,
                    rule.Severity,
                    rule.Capability is null ? rule.Message : $"{rule.Message} Missing capability '{rule.Capability}'.",
                    sourceName,
                    line,
                    column,
                    rule.Capability));
            }
        }
        return new WebSceneCompatibilityReport(diagnostics);
    }

    public static WebSceneCompatibilityReport CheckFiles(
        IEnumerable<string> sourceFiles,
        WebSceneComponentManifest manifest)
    {
        ArgumentNullException.ThrowIfNull(sourceFiles);
        var diagnostics = sourceFiles
            .SelectMany(path => Check(File.ReadAllText(path), manifest, path).Diagnostics)
            .ToArray();
        return new WebSceneCompatibilityReport(diagnostics);
    }

    private static Rule Unsupported(string pattern, string code, string message)
        => new(new Regex(pattern, RegexOptions.CultureInvariant), code, WebSceneCompatibilitySeverity.Error, message);

    private static Rule Requires(string pattern, string code, string capability, string message)
        => new(new Regex(pattern, RegexOptions.CultureInvariant), code, WebSceneCompatibilitySeverity.Error, message, capability);

    private static string MaskCommentsAndStrings(string source)
    {
        var chars = source.ToCharArray();
        foreach (Match match in CommentsAndStringsPattern().Matches(source))
        {
            for (var index = match.Index; index < match.Index + match.Length; index++)
            {
                if (chars[index] is not ('\r' or '\n'))
                {
                    chars[index] = ' ';
                }
            }
        }
        return new string(chars);
    }

    private static (int Line, int Column) GetLocation(string source, int index)
    {
        var line = 1;
        var column = 1;
        for (var offset = 0; offset < index; offset++)
        {
            if (source[offset] == '\n')
            {
                line++;
                column = 1;
            }
            else
            {
                column++;
            }
        }
        return (line, column);
    }

    [GeneratedRegex(@"\b(?:fetch|WebSocket|XMLHttpRequest)\b", RegexOptions.CultureInvariant)]
    private static partial Regex GeneratedNetworkPattern();

    [GeneratedRegex("""//[^\r\n]*|/\*[\s\S]*?\*/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`""", RegexOptions.CultureInvariant)]
    private static partial Regex CommentsAndStringsPattern();
}

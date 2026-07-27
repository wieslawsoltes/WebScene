using System.Text.RegularExpressions;

namespace WebScene.WebPlatformSubset.Runner;

internal static class HtmlScriptSemantics
{
    private static readonly Regex TypeAttributeRegex = new(
        "\\btype\\s*=\\s*(?:\"(?<double>[^\"]*)\"|'(?<single>[^']*)'|(?<bare>[^\\s>]+))",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    private static readonly HashSet<string> JavaScriptMimeTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        "application/ecmascript",
        "application/javascript",
        "application/x-ecmascript",
        "application/x-javascript",
        "text/ecmascript",
        "text/javascript",
        "text/javascript1.0",
        "text/javascript1.1",
        "text/javascript1.2",
        "text/javascript1.3",
        "text/javascript1.4",
        "text/javascript1.5",
        "text/jscript",
        "text/livescript",
        "text/x-ecmascript",
        "text/x-javascript"
    };

    internal static bool IsInertScript(string attributes)
    {
        var match = TypeAttributeRegex.Match(attributes);
        if (!match.Success)
        {
            return false;
        }

        var type = match.Groups["double"].Success
            ? match.Groups["double"].Value
            : match.Groups["single"].Success
                ? match.Groups["single"].Value
                : match.Groups["bare"].Value;
        type = type.Trim();
        return type.Length > 0
               && !string.Equals(type, "module", StringComparison.OrdinalIgnoreCase)
               && !JavaScriptMimeTypes.Contains(type);
    }

    internal static string RemoveAllScripts(string markup, Regex scriptRegex)
        => scriptRegex.Replace(markup, string.Empty);

    internal static string RemoveExecutableScriptsAndStyles(
        string markup,
        Regex scriptRegex,
        Regex styleRegex)
    {
        var inertScripts = new List<(string Placeholder, string Markup)>();
        var placeholderPrefix = "__WEBSCENE_INERT_SCRIPT_PLACEHOLDER_";
        while (markup.Contains(placeholderPrefix, StringComparison.Ordinal))
        {
            placeholderPrefix = "_" + placeholderPrefix;
        }

        var withoutExecutableScripts = scriptRegex.Replace(markup, match =>
        {
            if (!IsInertScript(match.Groups["attributes"].Value))
            {
                return string.Empty;
            }

            var placeholder = $"{placeholderPrefix}{inertScripts.Count}__";
            inertScripts.Add((placeholder, match.Value));
            return placeholder;
        });
        var withoutStyles = styleRegex.Replace(withoutExecutableScripts, string.Empty);
        foreach (var inertScript in inertScripts)
        {
            withoutStyles = withoutStyles.Replace(
                inertScript.Placeholder,
                inertScript.Markup,
                StringComparison.Ordinal);
        }
        return withoutStyles;
    }
}

namespace WebScene.Css;

/// <summary>Resolves nested CSS custom-property references and fallbacks.</summary>
public static class CssVariableResolver
{
    public static bool TryResolve(
        string value,
        IReadOnlyDictionary<string, string> customProperties,
        out string resolved)
    {
        ArgumentNullException.ThrowIfNull(value);
        ArgumentNullException.ThrowIfNull(customProperties);
        return TryResolveCore(
            value,
            customProperties,
            new HashSet<string>(StringComparer.Ordinal),
            out resolved);
    }

    private static bool TryResolveCore(
        string value,
        IReadOnlyDictionary<string, string> customProperties,
        HashSet<string> resolving,
        out string resolved)
    {
        var start = value.IndexOf("var(", StringComparison.OrdinalIgnoreCase);
        while (start >= 0)
        {
            var end = FindClosingParenthesis(value, start + 3);
            if (end < 0)
            {
                resolved = string.Empty;
                return false;
            }

            var content = value.Substring(start + 4, end - start - 4);
            var comma = FindTopLevelComma(content);
            var name = (comma >= 0 ? content[..comma] : content).Trim();
            var fallback = comma >= 0 ? content[(comma + 1)..].Trim() : string.Empty;
            string replacement;
            if (name.StartsWith("--", StringComparison.Ordinal)
                && resolving.Add(name)
                && customProperties.TryGetValue(name, out var customValue))
            {
                var valid = TryResolveCore(customValue, customProperties, resolving, out replacement);
                resolving.Remove(name);
                if (!valid
                    && (string.IsNullOrWhiteSpace(fallback)
                        || !TryResolveCore(fallback, customProperties, resolving, out replacement)))
                {
                    resolved = string.Empty;
                    return false;
                }
            }
            else if (string.IsNullOrWhiteSpace(fallback)
                     || !TryResolveCore(fallback, customProperties, resolving, out replacement))
            {
                resolved = string.Empty;
                return false;
            }

            value = value[..start] + replacement + value[(end + 1)..];
            start = value.IndexOf("var(", StringComparison.OrdinalIgnoreCase);
        }

        resolved = value;
        return true;
    }

    private static int FindClosingParenthesis(string value, int openIndex)
    {
        var depth = 0;
        for (var index = openIndex; index < value.Length; index++)
        {
            if (value[index] == '(')
            {
                depth++;
            }
            else if (value[index] == ')' && --depth == 0)
            {
                return index;
            }
        }

        return -1;
    }

    private static int FindTopLevelComma(string value)
    {
        var depth = 0;
        for (var index = 0; index < value.Length; index++)
        {
            if (value[index] == '(')
            {
                depth++;
            }
            else if (value[index] == ')')
            {
                depth--;
            }
            else if (value[index] == ',' && depth == 0)
            {
                return index;
            }
        }

        return -1;
    }
}

namespace JavaScript.Avalonia;

/// <summary>
/// Framework-neutral DOMTokenList parsing and state-decision semantics. Backend
/// adapters keep their concrete collection operations and caches so membership
/// reads do not cross an abstraction boundary.
/// </summary>
public static class DomTokenListSemantics
{
    public static IEnumerable<string> SplitTokens(IEnumerable<string> tokens)
    {
        foreach (var token in tokens)
        {
            if (string.IsNullOrWhiteSpace(token))
            {
                continue;
            }

            foreach (var part in token.Split(
                         new[] { ' ', '\t', '\r', '\n' },
                         StringSplitOptions.RemoveEmptyEntries))
            {
                yield return part;
            }
        }
    }

    public static bool ShouldAdd(bool contains, bool? force)
        => force ?? !contains;

    public static bool IsBackendVisibleToken(string token)
        => !token.StartsWith(':');
}

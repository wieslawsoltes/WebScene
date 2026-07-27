using System.Globalization;

namespace WebScene.Css;

[Flags]
public enum CssSelectorState
{
    None = 0,
    Hover = 1 << 0,
    Active = 1 << 1,
    Focus = 1 << 2,
    FocusVisible = 1 << 3,
    Disabled = 1 << 4,
    Checked = 1 << 5
}

public interface ICssSelectorNode
{
    string TagName { get; }

    string Id { get; }

    string TextContent { get; }

    int ChildElementCount { get; }

    bool IsDocumentElement { get; }

    ICssSelectorNode? ParentElement { get; }

    ICssSelectorNode? PreviousElementSibling { get; }

    ICssSelectorNode? NextElementSibling { get; }

    bool HasClass(string className);

    string? GetAttribute(string name);

    bool HasState(CssSelectorState state);
}

public readonly record struct CssSelectorMatchOptions(
    bool IgnorePseudoElements = false,
    bool IgnoreChildListPseudos = false,
    bool IgnoreDynamicPseudos = false,
    string? PseudoElementName = null,
    ICssSelectorNode? ScopeNode = null);

public static class CssSelectorMatcher
{
    public static bool Matches(
        CssSelectorSyntax selector,
        ICssSelectorNode node,
        CssSelectorMatchOptions options = default)
    {
        ArgumentNullException.ThrowIfNull(selector);
        ArgumentNullException.ThrowIfNull(node);
        if (selector.Parts.Count == 0)
        {
            return false;
        }

        var declaredPseudoElement = selector.Parts[^1].Simple.Pseudos
            .FirstOrDefault(static pseudo => pseudo.IsElement)?.Name;
        if (options.PseudoElementName is { } requested)
        {
            if (!string.Equals(declaredPseudoElement, requested, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
            options = options with { IgnorePseudoElements = true };
        }
        else if (declaredPseudoElement is not null && !options.IgnorePseudoElements)
        {
            return false;
        }

        return MatchPart(selector, node, selector.Parts.Count - 1, options);
    }

    private static bool MatchPart(
        CssSelectorSyntax selector,
        ICssSelectorNode node,
        int index,
        CssSelectorMatchOptions options)
    {
        if (index < 0 || !MatchesSimple(selector.Parts[index].Simple, node, options))
        {
            return false;
        }
        if (index == 0)
        {
            return true;
        }

        return selector.Parts[index].CombinatorToPrevious switch
        {
            CssSelectorCombinator.Child => node.ParentElement is { } parent
                                           && MatchPart(selector, parent, index - 1, options),
            CssSelectorCombinator.AdjacentSibling => node.PreviousElementSibling is { } sibling
                                                    && MatchPart(selector, sibling, index - 1, options),
            CssSelectorCombinator.GeneralSibling => MatchPreviousSibling(
                selector, node.PreviousElementSibling, index - 1, options),
            _ => MatchAncestor(selector, node.ParentElement, index - 1, options)
        };
    }

    private static bool MatchAncestor(
        CssSelectorSyntax selector,
        ICssSelectorNode? node,
        int index,
        CssSelectorMatchOptions options)
    {
        while (node is { } current)
        {
            if (MatchPart(selector, current, index, options)) return true;
            node = current.ParentElement;
        }
        return false;
    }

    private static bool MatchPreviousSibling(
        CssSelectorSyntax selector,
        ICssSelectorNode? node,
        int index,
        CssSelectorMatchOptions options)
    {
        while (node is { } current)
        {
            if (MatchPart(selector, current, index, options)) return true;
            node = current.PreviousElementSibling;
        }
        return false;
    }

    private static bool MatchesSimple(
        CssSimpleSelectorSyntax selector,
        ICssSelectorNode node,
        CssSelectorMatchOptions options)
    {
        if (selector.Tag is { Length: > 0 } tag
            && tag != "*"
            && !string.Equals(tag, node.TagName, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        if (selector.Id is { Length: > 0 } id && !string.Equals(id, node.Id, StringComparison.Ordinal))
        {
            return false;
        }
        foreach (var className in selector.Classes)
        {
            if (!node.HasClass(className)) return false;
        }
        foreach (var attribute in selector.Attributes)
        {
            if (!MatchesAttribute(attribute, node.GetAttribute(attribute.Name))) return false;
        }
        foreach (var pseudo in selector.Pseudos)
        {
            if (pseudo.IsElement)
            {
                if (!options.IgnorePseudoElements) return false;
                continue;
            }
            if (options.IgnoreChildListPseudos && DependsOnChildList(pseudo.Name)) continue;
            if (options.IgnoreDynamicPseudos && IsDynamic(pseudo.Name)) continue;
            if (!MatchesPseudo(pseudo, node, options)) return false;
        }
        return true;
    }

    private static bool MatchesAttribute(CssAttributeSelectorSyntax selector, string? actual)
    {
        if (actual is null) return false;
        if (selector.Operator is null) return true;
        var comparison = selector.CaseInsensitive ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        var expected = selector.Value ?? string.Empty;
        return selector.Operator switch
        {
            "=" => string.Equals(actual, expected, comparison),
            "~=" => actual.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
                .Any(token => string.Equals(token, expected, comparison)),
            "|=" => string.Equals(actual, expected, comparison) || actual.StartsWith(expected + "-", comparison),
            "^=" => actual.StartsWith(expected, comparison),
            "$=" => actual.EndsWith(expected, comparison),
            "*=" => actual.IndexOf(expected, comparison) >= 0,
            _ => false
        };
    }

    private static bool MatchesPseudo(
        CssPseudoSelectorSyntax pseudo,
        ICssSelectorNode node,
        CssSelectorMatchOptions options)
        => pseudo.Name switch
        {
            "root" => node.IsDocumentElement,
            "scope" => options.ScopeNode is null
                ? node.IsDocumentElement
                : ReferenceEquals(node, options.ScopeNode),
            "empty" => node.ChildElementCount == 0 && string.IsNullOrEmpty(node.TextContent),
            "first-child" => node.PreviousElementSibling is null,
            "last-child" => node.NextElementSibling is null,
            "only-child" => node.PreviousElementSibling is null && node.NextElementSibling is null,
            "first-of-type" => !EnumeratePrevious(node).Any(sibling => SameTag(sibling, node)),
            "last-of-type" => !EnumerateNext(node).Any(sibling => SameTag(sibling, node)),
            "only-of-type" => !EnumeratePrevious(node).Concat(EnumerateNext(node)).Any(sibling => SameTag(sibling, node)),
            "nth-child" => MatchesNth(GetChildIndex(node, ofType: false), pseudo.Argument),
            "nth-last-child" => MatchesNth(GetReverseChildIndex(node, ofType: false), pseudo.Argument),
            "nth-of-type" => MatchesNth(GetChildIndex(node, ofType: true), pseudo.Argument),
            "nth-last-of-type" => MatchesNth(GetReverseChildIndex(node, ofType: true), pseudo.Argument),
            "not" => !MatchesSelectorArgument(pseudo, node, options),
            "is" or "where" => MatchesSelectorArgument(pseudo, node, options),
            "hover" => node.HasState(CssSelectorState.Hover),
            "active" => node.HasState(CssSelectorState.Active),
            "focus" => node.HasState(CssSelectorState.Focus),
            "focus-visible" => node.HasState(CssSelectorState.FocusVisible),
            "disabled" => SupportsDisabledState(node) && node.HasState(CssSelectorState.Disabled),
            "enabled" => SupportsDisabledState(node) && !node.HasState(CssSelectorState.Disabled),
            "checked" => node.HasState(CssSelectorState.Checked),
            "link" => string.Equals(node.TagName, "a", StringComparison.OrdinalIgnoreCase)
                      && node.GetAttribute("href") is not null,
            "lang" => true,
            _ => false
        };

    private static bool SupportsDisabledState(ICssSelectorNode node)
        => node.TagName.Equals("button", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("fieldset", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("input", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("optgroup", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("option", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("select", StringComparison.OrdinalIgnoreCase)
           || node.TagName.Equals("textarea", StringComparison.OrdinalIgnoreCase);

    private static bool MatchesSelectorArgument(
        CssPseudoSelectorSyntax pseudo,
        ICssSelectorNode node,
        CssSelectorMatchOptions options)
    {
        foreach (var selector in pseudo.GetArgumentSelectors())
        {
            if (Matches(selector, node, options with { PseudoElementName = null }))
            {
                return true;
            }
        }
        return false;
    }

    private static IEnumerable<ICssSelectorNode> EnumeratePrevious(ICssSelectorNode node)
    {
        var current = node.PreviousElementSibling;
        while (current is not null)
        {
            yield return current;
            current = current.PreviousElementSibling;
        }
    }

    private static IEnumerable<ICssSelectorNode> EnumerateNext(ICssSelectorNode node)
    {
        var current = node.NextElementSibling;
        while (current is not null)
        {
            yield return current;
            current = current.NextElementSibling;
        }
    }

    private static int GetChildIndex(ICssSelectorNode node, bool ofType)
        => 1 + EnumeratePrevious(node).Count(sibling => !ofType || SameTag(sibling, node));

    private static int GetReverseChildIndex(ICssSelectorNode node, bool ofType)
        => 1 + EnumerateNext(node).Count(sibling => !ofType || SameTag(sibling, node));

    private static bool SameTag(ICssSelectorNode left, ICssSelectorNode right)
        => string.Equals(left.TagName, right.TagName, StringComparison.OrdinalIgnoreCase);

    private static bool MatchesNth(int index, string? expression)
    {
        var text = expression?.Replace(" ", string.Empty, StringComparison.Ordinal).ToLowerInvariant();
        if (string.IsNullOrEmpty(text)) return false;
        if (text == "odd") return index % 2 == 1;
        if (text == "even") return index % 2 == 0;
        if (int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var exact)) return index == exact;
        var separator = text.IndexOf('n');
        if (separator < 0) return false;
        var coefficientText = text[..separator];
        var offsetText = text[(separator + 1)..];
        var coefficient = coefficientText switch
        {
            "" or "+" => 1,
            "-" => -1,
            _ => int.TryParse(coefficientText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedCoefficient)
                ? parsedCoefficient
                : 0
        };
        var offset = string.IsNullOrEmpty(offsetText)
            ? 0
            : int.TryParse(offsetText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsedOffset)
                ? parsedOffset
                : 0;
        if (coefficient == 0) return index == offset;
        var delta = index - offset;
        return delta / coefficient >= 0 && delta % coefficient == 0;
    }

    private static bool DependsOnChildList(string name)
        => name is "empty" or "first-child" or "last-child" or "only-child"
            or "first-of-type" or "last-of-type" or "only-of-type"
            or "nth-child" or "nth-last-child" or "nth-of-type" or "nth-last-of-type";

    private static bool IsDynamic(string name)
        => name is "hover" or "active" or "focus" or "focus-visible" or "disabled" or "enabled" or "checked";
}

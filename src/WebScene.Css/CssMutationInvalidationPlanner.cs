namespace WebScene.Css;

[Flags]
public enum CssMutationInvalidationScope
{
    None = 0,
    Target = 1 << 0,
    Descendants = 1 << 1,
    PreviousSiblings = 1 << 2,
    FollowingSiblings = 1 << 3,
    Layout = 1 << 4
}

public readonly record struct CssMutationInvalidationPlan(CssMutationInvalidationScope Scope)
{
    public bool Affects(CssMutationInvalidationScope scope) => (Scope & scope) != 0;
}

/// <summary>
/// Computes backend-neutral invalidation scope from authored mutations and
/// selector dependency profiles. Adapters map these scopes to retained nodes.
/// </summary>
public static class CssMutationInvalidationPlanner
{
    private static readonly HashSet<string> s_inheritedProperties = new(StringComparer.OrdinalIgnoreCase)
    {
        "color", "cursor", "direction", "fill", "fill-opacity", "fill-rule", "font", "font-family",
        "font-size", "font-style", "font-variant", "font-weight", "letter-spacing", "line-height",
        "list-style", "list-style-position", "list-style-type",
        "pointer-events", "stroke", "stroke-linecap", "stroke-linejoin", "stroke-opacity", "stroke-width",
        "text-align", "text-indent", "text-transform", "visibility", "white-space", "word-spacing"
    };

    private static readonly HashSet<string> s_layoutProperties = new(StringComparer.OrdinalIgnoreCase)
    {
        "display", "position", "top", "right", "bottom", "left", "inset", "width", "height",
        "min-width", "min-height", "max-width", "max-height", "margin", "padding", "overflow",
        "box-sizing", "flex", "flex-basis", "flex-direction", "flex-flow", "flex-grow", "flex-shrink", "flex-wrap", "grid",
        "grid-template-columns", "grid-template-rows", "grid-column", "grid-column-start", "grid-column-end",
        "align-content", "align-items", "align-self", "justify-content",
        "gap", "list-style", "list-style-position", "list-style-type", "order", "row-gap", "column-gap", "z-index", "white-space"
    };

    internal static IReadOnlySet<string> InheritedProperties => s_inheritedProperties;

    public static CssMutationInvalidationPlan PlanInlineStyle(string? oldStyle, string? newStyle)
    {
        return new CssMutationInvalidationPlan(
            CssMutationInvalidationScope.Target | AnalyzeInlineStyle(oldStyle) | AnalyzeInlineStyle(newStyle));
    }

    private static CssMutationInvalidationScope AnalyzeInlineStyle(string? style)
    {
        var scope = CssMutationInvalidationScope.None;
        var remaining = style.AsSpan();
        while (!remaining.IsEmpty)
        {
            var terminator = remaining.IndexOf(';');
            var declaration = terminator >= 0 ? remaining[..terminator] : remaining;
            remaining = terminator >= 0 ? remaining[(terminator + 1)..] : ReadOnlySpan<char>.Empty;
            var separator = declaration.IndexOf(':');
            if (separator <= 0) continue;
            var property = declaration[..separator].Trim();
            if (property.StartsWith("--", StringComparison.Ordinal)
                || property.Equals("all", StringComparison.OrdinalIgnoreCase)
                || Contains(s_inheritedProperties, property))
            {
                scope |= CssMutationInvalidationScope.Descendants;
            }

            if (!property.StartsWith("--", StringComparison.Ordinal)
                && Contains(s_layoutProperties, property))
            {
                scope |= CssMutationInvalidationScope.Layout;
            }
        }

        return scope;
    }

    public static CssMutationInvalidationPlan PlanClassChange(
        string? oldClassName,
        string? newClassName,
        IEnumerable<CssSelectorDependencyProfile> selectorDependencies)
    {
        ArgumentNullException.ThrowIfNull(selectorDependencies);
        var changed = new HashSet<string>(SplitClassNames(oldClassName), StringComparer.Ordinal);
        changed.SymmetricExceptWith(SplitClassNames(newClassName));
        var scope = CssMutationInvalidationScope.Target;
        foreach (var profile in selectorDependencies)
        {
            if (changed.Overlaps(profile.ClassNames)
                && (profile.Ancestors & CssSelectorDependency.Class) != 0)
            {
                scope |= CssMutationInvalidationScope.Descendants;
            }

            if (profile.HasSiblingCombinator
                && (changed.Overlaps(profile.ClassNames)
                    || profile.AttributeNames.Contains("class")))
            {
                scope |= CssMutationInvalidationScope.FollowingSiblings;
            }
        }

        return new CssMutationInvalidationPlan(scope);
    }

    public static CssMutationInvalidationPlan PlanChildList(
        IEnumerable<CssSelectorDependencyProfile> selectorDependencies,
        bool appendAtEnd)
    {
        ArgumentNullException.ThrowIfNull(selectorDependencies);
        var scope = CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Layout;
        foreach (var profile in selectorDependencies)
        {
            if ((profile.Rightmost & CssSelectorDependency.Empty) != 0)
            {
                scope |= CssMutationInvalidationScope.Target;
            }
            if ((profile.Rightmost & CssSelectorDependency.PositionFromStart) != 0)
            {
                scope |= CssMutationInvalidationScope.FollowingSiblings;
            }
            if ((profile.Rightmost & CssSelectorDependency.AppendAtEnd) != 0)
            {
                scope |= CssMutationInvalidationScope.PreviousSiblings;
            }
            if (profile.HasSiblingCombinator)
            {
                scope |= CssMutationInvalidationScope.FollowingSiblings;
            }
            if ((profile.Ancestors & (CssSelectorDependency.Empty
                                      | CssSelectorDependency.PositionFromStart
                                      | CssSelectorDependency.AppendAtEnd)) != 0)
            {
                scope |= CssMutationInvalidationScope.Descendants;
            }
        }

        if (appendAtEnd)
        {
            scope &= ~CssMutationInvalidationScope.FollowingSiblings;
        }
        return new CssMutationInvalidationPlan(scope);
    }

    private static bool Contains(IEnumerable<string> values, ReadOnlySpan<char> candidate)
    {
        foreach (var value in values)
        {
            if (candidate.Equals(value, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    private static IEnumerable<string> SplitClassNames(string? className)
        => string.IsNullOrWhiteSpace(className)
            ? Array.Empty<string>()
            : className.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
}

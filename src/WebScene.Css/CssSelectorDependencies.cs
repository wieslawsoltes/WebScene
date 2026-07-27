namespace WebScene.Css;

[Flags]
public enum CssSelectorDependency
{
    None = 0,
    DynamicState = 1 << 0,
    Empty = 1 << 1,
    PositionFromStart = 1 << 2,
    AppendAtEnd = 1 << 3,
    Attribute = 1 << 4,
    Class = 1 << 5
}

/// <summary>Mutation dependencies precomputed from a parsed selector.</summary>
public sealed record CssSelectorDependencyProfile(
    CssSelectorDependency All,
    CssSelectorDependency Rightmost,
    CssSelectorDependency Ancestors,
    bool HasSiblingCombinator,
    IReadOnlySet<string> AttributeNames,
    IReadOnlySet<string> ClassNames)
{
    public bool DependsOnAttribute(string name) => AttributeNames.Contains(name);
}

public static class CssSelectorDependencyAnalyzer
{
    public static CssSelectorDependencyProfile Analyze(CssSelectorSyntax selector)
    {
        ArgumentNullException.ThrowIfNull(selector);
        var attributes = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var classes = new HashSet<string>(StringComparer.Ordinal);
        var all = CssSelectorDependency.None;
        var ancestors = CssSelectorDependency.None;
        var rightmost = CssSelectorDependency.None;
        for (var index = 0; index < selector.Parts.Count; index++)
        {
            var current = AnalyzeSimple(selector.Parts[index].Simple, attributes, classes);
            all |= current;
            if (index == selector.Parts.Count - 1)
            {
                rightmost = current;
            }
            else
            {
                ancestors |= current;
            }
        }

        return new CssSelectorDependencyProfile(
            all,
            rightmost,
            ancestors,
            selector.Parts.Any(static part => part.CombinatorToPrevious is
                CssSelectorCombinator.AdjacentSibling or CssSelectorCombinator.GeneralSibling),
            attributes,
            classes);
    }

    private static CssSelectorDependency AnalyzeSimple(
        CssSimpleSelectorSyntax simple,
        ISet<string> attributes,
        ISet<string> classes)
    {
        var dependencies = CssSelectorDependency.None;
        if (simple.Classes.Count > 0)
        {
            dependencies |= CssSelectorDependency.Class;
            classes.UnionWith(simple.Classes);
        }

        foreach (var attribute in simple.Attributes)
        {
            dependencies |= CssSelectorDependency.Attribute;
            attributes.Add(attribute.Name);
        }

        foreach (var pseudo in simple.Pseudos)
        {
            dependencies |= AnalyzePseudo(pseudo, attributes, classes);
        }

        return dependencies;
    }

    private static CssSelectorDependency AnalyzePseudo(
        CssPseudoSelectorSyntax pseudo,
        ISet<string> attributes,
        ISet<string> classes)
    {
        var dependency = pseudo.Name switch
        {
            "hover" or "active" or "focus" or "focus-visible" or "disabled" or "enabled" or "checked"
                => CssSelectorDependency.DynamicState,
            "empty" => CssSelectorDependency.Empty,
            "first-child" or "first-of-type" or "nth-child" or "nth-of-type"
                => CssSelectorDependency.PositionFromStart,
            "last-child" or "last-of-type" or "nth-last-child" or "nth-last-of-type"
                => CssSelectorDependency.AppendAtEnd,
            "only-child" or "only-of-type"
                => CssSelectorDependency.PositionFromStart | CssSelectorDependency.AppendAtEnd,
            _ => CssSelectorDependency.None
        };

        if (pseudo.Name is not ("is" or "not" or "where"))
        {
            return dependency;
        }

        foreach (var selector in pseudo.GetArgumentSelectors())
        {
            foreach (var part in selector.Parts)
            {
                dependency |= AnalyzeSimple(part.Simple, attributes, classes);
            }
        }

        return dependency;
    }
}

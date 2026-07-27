using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssSelectorDependencyAnalyzerTests
{
    [Fact]
    public void SeparatesAncestorAndRightmostMutationDependencies()
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(
            ".toolbar:hover:first-child[data-mode] ~ button:is(.active, [aria-pressed]):last-child",
            out var selector));

        var profile = CssSelectorDependencyAnalyzer.Analyze(selector);

        Assert.True(profile.HasSiblingCombinator);
        Assert.True(profile.Ancestors.HasFlag(CssSelectorDependency.DynamicState));
        Assert.True(profile.Ancestors.HasFlag(CssSelectorDependency.PositionFromStart));
        Assert.True(profile.Rightmost.HasFlag(CssSelectorDependency.AppendAtEnd));
        Assert.True(profile.Rightmost.HasFlag(CssSelectorDependency.Class));
        Assert.True(profile.DependsOnAttribute("DATA-MODE"));
        Assert.True(profile.DependsOnAttribute("aria-pressed"));
        Assert.Contains("toolbar", profile.ClassNames);
        Assert.Contains("active", profile.ClassNames);
    }

    [Theory]
    [InlineData(":empty", CssSelectorDependency.Empty)]
    [InlineData(":only-child", CssSelectorDependency.PositionFromStart | CssSelectorDependency.AppendAtEnd)]
    [InlineData(":nth-of-type(2n)", CssSelectorDependency.PositionFromStart)]
    [InlineData(":nth-last-of-type(2n)", CssSelectorDependency.AppendAtEnd)]
    [InlineData(":not(:focus-visible)", CssSelectorDependency.DynamicState)]
    public void ClassifiesPseudoDependencies(string text, CssSelectorDependency expected)
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(text, out var selector));
        var profile = CssSelectorDependencyAnalyzer.Analyze(selector);
        Assert.Equal(expected, profile.All & expected);
    }

    [Fact]
    public void RejectsNullSelector()
        => Assert.Throws<ArgumentNullException>(() => CssSelectorDependencyAnalyzer.Analyze(null!));
}

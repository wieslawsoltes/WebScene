using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssVariableResolverTests
{
    [Fact]
    public void ResolvesNestedValuesCaseSensitivelyAndPreservesOtherTokens()
    {
        var values = new Dictionary<string, string>
        {
            ["--accent"] = "var(--palette)",
            ["--palette"] = "rgb(1, 2, 3)",
            ["--Accent"] = "green"
        };

        Assert.True(CssVariableResolver.TryResolve("1px solid var(--accent)", values, out var resolved));
        Assert.Equal("1px solid rgb(1, 2, 3)", resolved);
        Assert.True(CssVariableResolver.TryResolve("var(--Accent)", values, out resolved));
        Assert.Equal("green", resolved);
    }

    [Theory]
    [InlineData("var(--missing, red)", "red")]
    [InlineData("var(--missing, rgb(1, 2, 3))", "rgb(1, 2, 3)")]
    [InlineData("calc(var(--missing, var(--gap, 4px)) * 2)", "calc(4px * 2)")]
    [InlineData("literal", "literal")]
    public void ResolvesFallbacksAndNestedFunctions(string input, string expected)
    {
        Assert.True(CssVariableResolver.TryResolve(input, new Dictionary<string, string>(), out var resolved));
        Assert.Equal(expected, resolved);
    }

    [Theory]
    [InlineData("var(--missing)")]
    [InlineData("var(--missing, )")]
    [InlineData("var(--broken")]
    public void RejectsInvalidOrUnresolvedReferences(string input)
        => Assert.False(CssVariableResolver.TryResolve(input, new Dictionary<string, string>(), out _));

    [Fact]
    public void InvalidNameCanStillUseTheDeclaredFallback()
    {
        Assert.True(CssVariableResolver.TryResolve("var(color, red)", new Dictionary<string, string>(), out var resolved));
        Assert.Equal("red", resolved);
    }

    [Fact]
    public void CyclesUseFallbackWhenPresentAndOtherwiseInvalidate()
    {
        var values = new Dictionary<string, string>
        {
            ["--a"] = "var(--b)",
            ["--b"] = "var(--a)"
        };

        Assert.False(CssVariableResolver.TryResolve("var(--a)", values, out _));
        Assert.True(CssVariableResolver.TryResolve("var(--a, blue)", values, out var resolved));
        Assert.Equal("blue", resolved);
    }

    [Fact]
    public void RejectsNullInputs()
    {
        Assert.Throws<ArgumentNullException>(() => CssVariableResolver.TryResolve(null!, new Dictionary<string, string>(), out _));
        Assert.Throws<ArgumentNullException>(() => CssVariableResolver.TryResolve("x", null!, out _));
    }
}

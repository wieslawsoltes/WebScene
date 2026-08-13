using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssSelectorSyntaxParserTests
{
    [Theory]
    [InlineData(".scroller::-webkit-scrollbar")]
    [InlineData(".scroller.no-scrollbar::-webkit-scrollbar-thumb")]
    [InlineData("#panel::-webkit-scrollbar-track")]
    public void AcceptsSupportedScrollbarPseudoElements(string selectorText)
        => Assert.True(CssSelectorSyntaxParser.IsSupportedDomSelectorList(selectorText));

    [Fact]
    public void ParsesComplexSelectorAndSpecificity()
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(
            "section#chart > .toolbar button[data-mode='draw' i]:hover::before",
            out var selector));

        Assert.Equal(3, selector.Parts.Count);
        Assert.Equal(133, selector.Specificity);
        Assert.Equal(CssSelectorCombinator.Child, selector.Parts[1].CombinatorToPrevious);
        Assert.Equal(CssSelectorCombinator.Descendant, selector.Parts[2].CombinatorToPrevious);
        Assert.True(selector.Parts[2].Simple.Attributes[0].CaseInsensitive);
        Assert.True(selector.Parts[2].Simple.Pseudos[^1].IsElement);
    }

    [Fact]
    public void SplitSelectorListIgnoresNestedCommas()
    {
        var selectors = CssSelectorSyntaxParser.SplitSelectorList(
            ".a:is(.b, .c), [data-value='x,y'], div").ToArray();

        Assert.Equal(new[] { ".a:is(.b, .c)", "[data-value='x,y']", "div" }, selectors);
    }

    [Theory]
    [InlineData(".card:where(#ignored)", 10)]
    [InlineData("article:is(.note, #winner)", 101)]
    [InlineData("div:not(.a, #b)", 101)]
    [InlineData("section:has(.item > #target)", 111)]
    public void FunctionalSelectorsUseTheirMostSpecificArgument(
        string selectorText,
        int expectedSpecificity)
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(selectorText, out var selector));

        Assert.Equal(expectedSpecificity, selector.Specificity);
    }

    [Fact]
    public void ComplexIsSpecificityMatchesSelectorsLevelFourCascadeExample()
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(
            ":is(.a, .b.c + .d, .q) + :is(* + .p, .q.r + .s, * + .t) + #target",
            out var functional));
        Assert.True(CssSelectorSyntaxParser.TryParse(
            ".b.c + .d + .q.r + .s + #target",
            out var expanded));

        Assert.Equal(160, functional.Specificity);
        Assert.Equal(expanded.Specificity, functional.Specificity);
    }

    [Fact]
    public void ParsesCssEscapedIdentifiersWithoutTreatingEscapeTerminatorsAsCombinators()
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(
            "#\\30 \\/my\\/id",
            out var leadingDigit));
        Assert.Equal("0/my/id", leadingDigit.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#space\\ id", out var space));
        Assert.Equal("space id", space.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#\\.\\,\\:\\!", out var punctuation));
        Assert.Equal(".,:!", punctuation.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#spac\\65\r\ns", out var continuation));
        Assert.Equal("spaces", continuation.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#eof\\", out var endOfFile));
        Assert.Equal("eof\ufffd", endOfFile.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#\ud834\udf06", out var astral));
        Assert.Equal("\ud834\udf06", astral.Parts.Single().Simple.Id);

        Assert.True(CssSelectorSyntaxParser.TryParse("#null\0", out var nullCodeUnit));
        Assert.Equal("null\ufffd", nullCodeUnit.Parts.Single().Simple.Id);

        Assert.Equal(
            new[] { "#comma\\,id", ".other" },
            CssSelectorSyntaxParser.SplitSelectorList("#comma\\,id, .other").ToArray());
    }

    [Theory]
    [InlineData("")]
    [InlineData("div|")]
    [InlineData("::before::after")]
    public void RejectsUnsupportedOrInvalidSelectors(string value)
        => Assert.False(CssSelectorSyntaxParser.TryParse(value, out _));

    [Theory]
    [InlineData("#target")]
    [InlineData("section > .item:first-child")]
    [InlineData("button:is(:hover, :focus-visible)")]
    [InlineData("li:nth-child(2n+1)")]
    [InlineData("div::before")]
    public void AcceptsSupportedDomSelectorLists(string value)
        => Assert.True(CssSelectorSyntaxParser.IsSupportedDomSelectorList(value));

    [Theory]
    [InlineData("")]
    [InlineData("[")]
    [InlineData(":visible")]
    [InlineData("div, :example")]
    [InlineData("div,")]
    [InlineData("div::example")]
    [InlineData("li:nth-child(today)")]
    public void RejectsMalformedOrUnsupportedDomSelectorLists(string value)
        => Assert.False(CssSelectorSyntaxParser.IsSupportedDomSelectorList(value));
}

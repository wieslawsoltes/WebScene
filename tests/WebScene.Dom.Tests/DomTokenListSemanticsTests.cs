using JavaScript.Avalonia;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomTokenListSemanticsTests
{
    [Fact]
    public void SplitTokensPreservesOrderDuplicatesAndHtmlWhitespaceRules()
    {
        var tokens = DomTokenListSemantics.SplitTokens(
            new[] { " active\tchart ", "", "active\r\nalternate" });

        Assert.Equal(new[] { "active", "chart", "active", "alternate" }, tokens);
    }

    [Theory]
    [InlineData(false, null, true)]
    [InlineData(true, null, false)]
    [InlineData(false, true, true)]
    [InlineData(true, true, true)]
    [InlineData(false, false, false)]
    [InlineData(true, false, false)]
    public void ToggleDecisionMatchesDomTokenListState(bool contains, bool? force, bool expected)
    {
        Assert.Equal(expected, DomTokenListSemantics.ShouldAdd(contains, force));
    }

    [Theory]
    [InlineData("active", true)]
    [InlineData(":pointerover", false)]
    public void BackendPseudoClassesRemainHiddenFromDomTokenList(string token, bool expected)
    {
        Assert.Equal(expected, DomTokenListSemantics.IsBackendVisibleToken(token));
    }
}

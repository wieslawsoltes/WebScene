using JavaScript.Avalonia;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomTextNodeSemanticsTests
{
    [Theory]
    [InlineData(null, "none")]
    [InlineData("  UPPERCASE  ", "uppercase")]
    [InlineData("Capitalize", "capitalize")]
    public void TextTransformNamesAreNormalized(string? value, string expected)
    {
        Assert.Equal(expected, DomTextNodeSemantics.NormalizeTextTransform(value));
    }

    [Theory]
    [InlineData("Market Price", "uppercase", "MARKET PRICE")]
    [InlineData("Market Price", "lowercase", "market price")]
    [InlineData("market-price 42day", "capitalize", "Market-Price 42day")]
    [InlineData("Market Price", "none", "Market Price")]
    [InlineData("Market Price", "unsupported", "Market Price")]
    public void TextTransformIsIndependentOfThePresentationBackend(
        string value,
        string transform,
        string expected)
    {
        Assert.Equal(expected, DomTextNodeSemantics.ApplyTextTransform(value, transform));
    }
}

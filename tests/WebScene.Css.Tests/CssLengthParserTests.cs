using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssLengthParserTests
{
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("  ")]
    public void MissingLengthIsValidAndUnspecified(string? text)
    {
        Assert.True(CssLengthParser.TryParse(text, out var length));
        Assert.Null(length);
    }

    [Theory]
    [InlineData("96px", 96)]
    [InlineData("1in", 96)]
    [InlineData("2.54cm", 96)]
    [InlineData("25.4mm", 96)]
    [InlineData("72pt", 96)]
    [InlineData("6pc", 96)]
    [InlineData("101.6q", 96)]
    public void ConvertsAbsoluteUnitsToCssPixels(string text, double expected)
    {
        Assert.True(CssLengthParser.TryParse(text, out var length));
        Assert.Equal(CssLayoutLength.Pixels(expected), length!.Value);
    }

    [Theory]
    [InlineData("auto", CssLayoutLengthUnit.Auto, 0, 0)]
    [InlineData("50%", CssLayoutLengthUnit.Percent, 50, 0)]
    [InlineData("calc(50% - 14px)", CssLayoutLengthUnit.Percent, 50, -14)]
    [InlineData("calc(20px - 3px * 2)", CssLayoutLengthUnit.Pixel, 14, 0)]
    [InlineData("max(0, 10px)", CssLayoutLengthUnit.Pixel, 10, 0)]
    [InlineData("min(12px, 10px)", CssLayoutLengthUnit.Pixel, 10, 0)]
    [InlineData("-(2px + 3px)", CssLayoutLengthUnit.Pixel, -5, 0)]
    public void ParsesDeferredMath(string text, CssLayoutLengthUnit unit, double value, double offset)
    {
        Assert.True(CssLengthParser.TryParse(text, out var length));
        Assert.Equal(unit, length!.Value.Unit);
        Assert.Equal(value, length.Value.Value, precision: 8);
        Assert.Equal(offset, length.Value.PixelOffset, precision: 8);
    }

    [Theory]
    [InlineData("calc(1px / 0)")]
    [InlineData("calc(1px * 2px)")]
    [InlineData("min(10%, 2px)")]
    [InlineData("unknown(1px)")]
    [InlineData("calc(1px")]
    [InlineData("nope")]
    [InlineData("NaNpx")]
    public void RejectsUnrepresentableOrMalformedLengths(string text)
        => Assert.False(CssLengthParser.TryParse(text, out _));

    [Fact]
    public void ResolvesDeferredPercentageAgainstContainingBlock()
    {
        Assert.True(CssLengthParser.TryParse("calc(25% + 5px)", out var length));
        Assert.Equal(55, length!.Value.Resolve(200));
    }
}

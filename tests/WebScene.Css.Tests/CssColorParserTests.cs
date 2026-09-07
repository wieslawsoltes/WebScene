using WebScene.Core;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssColorParserTests
{
    [Theory]
    [InlineData("rgb(10, 20, 30)", 255, 10, 20, 30)]
    [InlineData("rgba(100%, 0%, 50%, 25%)", 64, 255, 0, 127)]
    [InlineData("rgba(300, -10, 12, 1.5)", 255, 255, 0, 12)]
    public void ParsesFunctionalColors(string css, byte a, byte r, byte g, byte b)
    {
        Assert.True(CssColorParser.TryParseFunctionalColor(css, out var color));
        Assert.Equal(new WebSceneColor(a, r, g, b), color);
    }

    [Theory]
    [InlineData("hsl(0, 100%, 50%)", 255, 255, 0, 0)]
    [InlineData("hsl(120 100% 25%)", 255, 0, 128, 0)]
    [InlineData("hsl(0.5turn 100% 50% / 25%)", 64, 0, 255, 255)]
    [InlineData("hsla(-90deg, 100%, 50%, .5)", 128, 128, 0, 255)]
    public void ParsesHslFunctionalColors(string css, byte a, byte r, byte g, byte b)
    {
        Assert.True(CssColorParser.TryParseFunctionalColor(css, out var color));
        Assert.Equal(new WebSceneColor(a, r, g, b), color);
    }

    [Theory]
    [InlineData("#b8b8b833", 0x33, 0xb8, 0xb8, 0xb8)]
    [InlineData("#63636326", 0x26, 0x63, 0x63, 0x63)]
    [InlineData("#0f08", 0x88, 0x00, 0xff, 0x00)]
    [InlineData("#abc", 0xff, 0xaa, 0xbb, 0xcc)]
    [InlineData("#123456", 0xff, 0x12, 0x34, 0x56)]
    public void ParsesCssHexInRgbaOrder(string css, byte a, byte r, byte g, byte b)
    {
        Assert.True(CssColorParser.TryParseColor(css, out var color));
        Assert.Equal(new WebSceneColor(a, r, g, b), color);
    }

    [Theory]
    [InlineData("#12")]
    [InlineData("#12345")]
    [InlineData("#gggg")]
    [InlineData("#123456789")]
    public void RejectsMalformedHexColors(string css)
        => Assert.False(CssColorParser.TryParseColor(css, out _));

    [Theory]
    [InlineData(null)]
    [InlineData("hsl(0, 0, 0)")]
    [InlineData("hsl(nope, 0%, 0%)")]
    [InlineData("rgb(1, 2)")]
    [InlineData("rgba(1, 2, 3, nope)")]
    public void RejectsUnsupportedOrMalformedColors(string? css)
        => Assert.False(CssColorParser.TryParseFunctionalColor(css, out _));

    [Theory]
    [InlineData("#FFFFFF", "rgb(255, 255, 255)")]
    [InlineData("#0f08", "rgba(0, 255, 0, 0.533)")]
    [InlineData("rgb(1,2,3)", "rgb(1, 2, 3)")]
    [InlineData("rgba(1,2,3,.5)", "rgba(1, 2, 3, 0.5)")]
    [InlineData("hsl(0, 100%, 50%)", "rgb(255, 0, 0)")]
    [InlineData("hsl(180 100% 50% / 25%)", "rgba(0, 255, 255, 0.25)")]
    public void SerializesAuthoredColorsUsingBrowserCssomForm(string css, string expected)
    {
        Assert.True(CssColorParser.TrySerializeSpecifiedColor(css, out var serialized));
        Assert.Equal(expected, serialized);
    }

    [Theory]
    [InlineData("white")]
    [InlineData("transparent")]
    [InlineData("var(--accent)")]
    [InlineData("not-a-color")]
    public void PreservesColorKeywordsAndUnresolvedValues(string css)
        => Assert.False(CssColorParser.TrySerializeSpecifiedColor(css, out _));
}

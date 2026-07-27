using WebScene.Core;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssNamedColorParserTests
{
    [Theory]
    [InlineData("gray", 0x80, 0x80, 0x80)]
    [InlineData("grey", 0x80, 0x80, 0x80)]
    [InlineData("darkgrey", 0xa9, 0xa9, 0xa9)]
    [InlineData("dimgrey", 0x69, 0x69, 0x69)]
    [InlineData("lightgrey", 0xd3, 0xd3, 0xd3)]
    [InlineData("lightslategrey", 0x77, 0x88, 0x99)]
    [InlineData("slategrey", 0x70, 0x80, 0x90)]
    [InlineData("darkslategrey", 0x2f, 0x4f, 0x4f)]
    public void ParsesCssGrayAndGreyNamedColorAliases(string css, byte r, byte g, byte b)
    {
        Assert.True(CssColorParser.TryParseColor(css, out var color));
        Assert.Equal(new WebSceneColor(0xff, r, g, b), color);
    }
}

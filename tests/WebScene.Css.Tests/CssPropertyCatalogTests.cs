using WebScene.Css;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssPropertyCatalogTests
{
    [Theory]
    [InlineData("color")]
    [InlineData("backgroundColor")]
    [InlineData("border-left-color")]
    [InlineData("fill")]
    [InlineData("stroke")]
    public void IdentifiesCssColorValueProperties(string propertyName)
        => Assert.True(CssPropertyCatalog.IsColorValueProperty(propertyName));

    [Theory]
    [InlineData("--theme-color")]
    [InlineData("color-scheme")]
    [InlineData("print-color-adjust")]
    [InlineData("background")]
    public void DoesNotMisclassifyNonColorValueProperties(string propertyName)
        => Assert.False(CssPropertyCatalog.IsColorValueProperty(propertyName));

    [Theory]
    [InlineData("position")]
    [InlineData("backgroundAttachment")]
    [InlineData("grid-area")]
    [InlineData("fillOpacity")]
    [InlineData("order")]
    [InlineData("-moz-transform")]
    [InlineData("WebkitTransform")]
    [InlineData("grid-gap")]
    [InlineData("insetInlineStart")]
    [InlineData("padding-inline-end")]
    [InlineData("borderStartEndRadius")]
    [InlineData("transitionTimingFunction")]
    [InlineData("transform-origin")]
    public void ExposesSupportedCssomProperties(string name)
        => Assert.True(CssPropertyCatalog.IsSupported(name));

    [Theory]
    [InlineData("fakeProperty")]
    [InlineData("WebkitFakeProperty")]
    [InlineData("--custom-token")]
    public void DoesNotExposeUnknownOrCustomPropertiesAsIdlAttributes(string name)
        => Assert.False(CssPropertyCatalog.IsSupported(name));

    [Theory]
    [InlineData("position", "absolute", true)]
    [InlineData("position", "ABSOLUTE", true)]
    [InlineData("position", "fake value", false)]
    [InlineData("position", " ", false)]
    [InlineData("font-size", "27px", true)]
    [InlineData("font-size", "2", false)]
    [InlineData("font-size", "0", true)]
    [InlineData("letter-spacing", "normal", true)]
    [InlineData("letter-spacing", "3", false)]
    public void ValidatesCssomValuesWithoutFrameworkKnowledge(string name, string value, bool expected)
        => Assert.Equal(expected, CssPropertyCatalog.IsValidCssomValue(name, value));
}

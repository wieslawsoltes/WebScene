using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssComputedValueNormalizerTests
{
    [Fact]
    public void ExpandsPortableBoxBorderBackgroundInsetGapAndOverflowShorthands()
    {
        var values = new CssPropertyValueStore
        {
            ["margin"] = "1px 2px 3px 4px",
            ["padding"] = "5px 6px",
            ["border"] = "2px solid rgb(1, 2, 3)",
            ["background"] = "#abcdef",
            ["inset"] = "7px 8px 9px",
            ["gap"] = "10px 11px",
            ["overflow"] = "hidden visible"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal(("1px", "2px", "3px", "4px"),
            (values["margin-top"], values["margin-right"], values["margin-bottom"], values["margin-left"]));
        Assert.Equal(("5px", "6px", "5px", "6px"),
            (values["padding-top"], values["padding-right"], values["padding-bottom"], values["padding-left"]));
        Assert.Equal("2px", values["border-top-width"]);
        Assert.Equal("solid", values["border-right-style"]);
        Assert.Equal("rgb(1, 2, 3)", values["border-bottom-color"]);
        Assert.Equal("#abcdef", values["background-color"]);
        Assert.Equal(("7px", "8px", "9px", "8px"),
            (values["top"], values["right"], values["bottom"], values["left"]));
        Assert.Equal(("10px", "11px"), (values["row-gap"], values["column-gap"]));
        Assert.Equal(("hidden", "visible"), (values["overflow-x"], values["overflow-y"]));
    }

    [Fact]
    public void ExpandsLegacyGridGapAliasIntoModernLonghands()
    {
        var values = new CssPropertyValueStore { ["grid-gap"] = "8px 6px" };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("8px", values["row-gap"]);
        Assert.Equal("6px", values["column-gap"]);
    }

    [Theory]
    [InlineData("2", "2", "auto", "auto", "auto")]
    [InlineData("2 / 3", "2", "3", "auto", "auto")]
    [InlineData("2 / 3 / 4 / 5", "2", "3", "4", "5")]
    public void ExpandsGridAreaPlacementComponents(
        string shorthand,
        string rowStart,
        string columnStart,
        string rowEnd,
        string columnEnd)
    {
        Assert.True(CssComputedValueNormalizer.TryExpandGridPlacementShorthand(
            "grid-area",
            shorthand,
            out var actualRowStart,
            out var actualColumnStart,
            out var actualRowEnd,
            out var actualColumnEnd));
        Assert.Equal(
            (rowStart, columnStart, rowEnd, columnEnd),
            (actualRowStart, actualColumnStart, actualRowEnd, actualColumnEnd));
    }

    [Theory]
    [InlineData("grid-row", "3", "3", "auto", "auto", "auto")]
    [InlineData("grid-row", "3 / 4", "3", "auto", "4", "auto")]
    [InlineData("grid-column", "2", "auto", "2", "auto", "auto")]
    [InlineData("grid-column", "2 / 5", "auto", "2", "auto", "5")]
    public void ExpandsGridAxisPlacementComponents(
        string property,
        string shorthand,
        string rowStart,
        string columnStart,
        string rowEnd,
        string columnEnd)
    {
        Assert.True(CssComputedValueNormalizer.TryExpandGridPlacementShorthand(
            property,
            shorthand,
            out var actualRowStart,
            out var actualColumnStart,
            out var actualRowEnd,
            out var actualColumnEnd));
        Assert.Equal(
            (rowStart, columnStart, rowEnd, columnEnd),
            (actualRowStart, actualColumnStart, actualRowEnd, actualColumnEnd));
    }

    [Theory]
    [InlineData("green solid 5px", "green", "solid", "5px")]
    [InlineData("5px green", "green", "none", "5px")]
    [InlineData("dashed", "currentcolor", "dashed", "medium")]
    [InlineData("auto", "currentcolor", "auto", "medium")]
    [InlineData("0", "currentcolor", "none", "0")]
    public void ExpandsOutlineShorthandIntoComputedLonghandInputs(
        string shorthand,
        string color,
        string style,
        string width)
    {
        var values = new CssPropertyValueStore { ["outline"] = shorthand };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal(color, values["outline-color"]);
        Assert.Equal(style, values["outline-style"]);
        Assert.Equal(width, values["outline-width"]);
    }

    [Fact]
    public void OutlineShorthandDoesNotOverwriteCascadedLonghandsOrOffset()
    {
        var values = new CssPropertyValueStore
        {
            ["outline"] = "red solid 5px",
            ["outline-color"] = "blue",
            ["outline-offset"] = "3px"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("blue", values["outline-color"]);
        Assert.Equal("solid", values["outline-style"]);
        Assert.Equal("5px", values["outline-width"]);
        Assert.Equal("3px", values["outline-offset"]);
    }

    [Theory]
    [InlineData("10% solid red")]
    [InlineData("-1px solid red")]
    [InlineData("solid dashed red")]
    [InlineData("initial")]
    public void InvalidOrCascadeDependentOutlineShorthandsDoNotProduceLonghands(string shorthand)
    {
        var values = new CssPropertyValueStore { ["outline"] = shorthand };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.False(values.ContainsKey("outline-color"));
        Assert.False(values.ContainsKey("outline-style"));
        Assert.False(values.ContainsKey("outline-width"));
    }

    [Theory]
    [InlineData("1px", "1px", "1px", "1px", "1px")]
    [InlineData("1px 2px", "1px", "2px", "1px", "2px")]
    [InlineData("1px 2px 3px", "1px", "2px", "3px", "2px")]
    [InlineData("1px 2px 3px 4px", "1px", "2px", "3px", "4px")]
    public void ExpandsBorderRadiusHorizontalCornerValues(
        string shorthand,
        string topLeft,
        string topRight,
        string bottomRight,
        string bottomLeft)
    {
        var values = new CssPropertyValueStore { ["border-radius"] = shorthand };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal(
            (topLeft, topRight, bottomRight, bottomLeft),
            (values["border-top-left-radius"],
                values["border-top-right-radius"],
                values["border-bottom-right-radius"],
                values["border-bottom-left-radius"]));
    }

    [Fact]
    public void ExpandsEllipticalBorderRadiusWithoutFlatteningFunctionalComponents()
    {
        var values = new CssPropertyValueStore
        {
            ["border-radius"] = "calc(1px + 2px) 4px / 5px calc(6px + 1px)"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("calc(1px + 2px) 5px", values["border-top-left-radius"]);
        Assert.Equal("4px calc(6px + 1px)", values["border-top-right-radius"]);
        Assert.Equal("calc(1px + 2px) 5px", values["border-bottom-right-radius"]);
        Assert.Equal("4px calc(6px + 1px)", values["border-bottom-left-radius"]);
    }

    [Fact]
    public void BorderRadiusFallbackExpansionDoesNotOverwriteACascadedCornerLonghand()
    {
        var values = new CssPropertyValueStore
        {
            ["border-radius"] = "6px",
            ["border-top-left-radius"] = "11px"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("11px", values["border-top-left-radius"]);
        Assert.Equal("6px", values["border-top-right-radius"]);
        Assert.Equal("6px", values["border-bottom-right-radius"]);
        Assert.Equal("6px", values["border-bottom-left-radius"]);
    }

    [Theory]
    [InlineData("row wrap", "row", "wrap")]
    [InlineData("column-reverse nowrap", "column-reverse", "nowrap")]
    public void ExpandsFlexFlow(string value, string direction, string wrap)
    {
        var values = new CssPropertyValueStore { ["flex-flow"] = value };
        CssComputedValueNormalizer.ExpandShorthands(values);
        Assert.Equal(direction, values["flex-direction"]);
        Assert.Equal(wrap, values["flex-wrap"]);
    }

    [Theory]
    [InlineData("none", "0", "0", "auto")]
    [InlineData("auto", "1", "1", "auto")]
    [InlineData("2 3 10px", "2", "3", "10px")]
    public void ExpandsFlex(string value, string grow, string shrink, string basis)
    {
        var values = new CssPropertyValueStore { ["flex"] = value };
        CssComputedValueNormalizer.ExpandShorthands(values);
        Assert.Equal((grow, shrink, basis),
            (values["flex-grow"], values["flex-shrink"], values["flex-basis"]));
    }

    [Fact]
    public void ExpandsAbsoluteFontAndNormalizesUnitlessLineHeight()
    {
        var values = new CssPropertyValueStore
        {
            ["font"] = "italic bold 12pt/1.5 Test Sans"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("italic", values["font-style"]);
        Assert.Equal("bold", values["font-weight"]);
        Assert.Equal("12pt", values["font-size"]);
        Assert.Equal("24px", values["line-height"]);
        Assert.Equal("Test Sans", values["font-family"]);
    }

    [Fact]
    public void ExpandsPixelFontAndNormalizesUnitlessLineHeight()
    {
        var values = new CssPropertyValueStore
        {
            ["font"] = "italic 600 16px/1.5 \"Test Sans\", sans-serif"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("italic", values["font-style"]);
        Assert.Equal("normal", values["font-variant"]);
        Assert.Equal("600", values["font-weight"]);
        Assert.Equal("16px", values["font-size"]);
        Assert.Equal("24px", values["line-height"]);
        Assert.Equal("\"Test Sans\", sans-serif", values["font-family"]);
    }

    [Fact]
    public void ExpandsPixelFontWithSeparatedSlashLineHeight()
    {
        var values = new CssPropertyValueStore
        {
            ["font"] = "500 14px / 20px -apple-system, sans-serif"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal("500", values["font-weight"]);
        Assert.Equal("14px", values["font-size"]);
        Assert.Equal("20px", values["line-height"]);
        Assert.Equal("-apple-system, sans-serif", values["font-family"]);
    }

    [Fact]
    public void NormalizesSimpleEmBoxLengthsAfterShorthandExpansion()
    {
        var values = new CssPropertyValueStore
        {
            ["font-size"] = "4px",
            ["inset"] = "1em 2em 3em 4em",
            ["width"] = "25em",
            ["min-height"] = "2.5em",
            ["margin"] = "5em",
            ["padding"] = "6em",
            ["gap"] = "7em 8em",
            ["flex-basis"] = "9em"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.Equal(("4px", "8px", "12px", "16px"),
            (values["top"], values["right"], values["bottom"], values["left"]));
        Assert.Equal("100px", values["width"]);
        Assert.Equal("10px", values["min-height"]);
        Assert.Equal("20px", values["margin-right"]);
        Assert.Equal("24px", values["padding-left"]);
        Assert.Equal(("28px", "32px"), (values["row-gap"], values["column-gap"]));
        Assert.Equal("36px", values["flex-basis"]);
    }

    [Theory]
    [InlineData("visible", "hidden", "auto", "hidden", "auto hidden")]
    [InlineData("clip", "scroll", "hidden", "scroll", "hidden scroll")]
    [InlineData("hidden", "visible", "hidden", "auto", "hidden auto")]
    [InlineData("visible", "visible", "visible", "visible", "visible")]
    public void NormalizesComputedOverflowAxes(
        string inputX,
        string inputY,
        string outputX,
        string outputY,
        string shorthand)
    {
        var values = new CssPropertyValueStore
        {
            ["overflow-x"] = inputX,
            ["overflow-y"] = inputY
        };

        CssComputedValueNormalizer.NormalizeOverflow(values);

        Assert.Equal((outputX, outputY, shorthand),
            (values["overflow-x"], values["overflow-y"], values["overflow"]));
    }

    [Fact]
    public void InvalidOrUnsupportedShorthandsRemainNonDestructive()
    {
        var values = new CssPropertyValueStore
        {
            ["margin"] = "1 2 3 4 5",
            ["font"] = "caption",
            ["background"] = "url(image.png) no-repeat"
        };

        CssComputedValueNormalizer.ExpandShorthands(values);

        Assert.False(values.ContainsKey("margin-top"));
        Assert.False(values.ContainsKey("font-size"));
        Assert.False(values.ContainsKey("background-color"));
        Assert.Throws<ArgumentNullException>(() => CssComputedValueNormalizer.ExpandShorthands(null!));
        Assert.Throws<ArgumentNullException>(() => CssComputedValueNormalizer.NormalizeOverflow(null!));
    }
}

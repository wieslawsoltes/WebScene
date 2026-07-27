using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssMediaQueryEvaluatorTests
{
    private static readonly CssMediaEnvironment Desktop = new(1280, 720, 2);

    [Theory]
    [InlineData("screen", true)]
    [InlineData("print", false)]
    [InlineData("not print", true)]
    [InlineData("only screen and (min-width: 1280px)", true)]
    [InlineData("(max-width: 1279px), (orientation: landscape)", true)]
    [InlineData("screen and (width: 1280px) and (height: 720px)", true)]
    [InlineData("(min-height: 721px)", false)]
    [InlineData("(max-height: 720px)", true)]
    [InlineData("(resolution: 192dpi)", true)]
    [InlineData("(min-resolution: 2dppx) and (max-resolution: 2)", true)]
    [InlineData("(-webkit-min-device-pixel-ratio: 2)", true)]
    [InlineData("(-webkit-max-device-pixel-ratio: 1)", false)]
    [InlineData("(hover) and (pointer: fine)", true)]
    [InlineData("(prefers-color-scheme: light) and (prefers-reduced-motion: no-preference)", true)]
    [InlineData("(unsupported: yes)", false)]
    [InlineData("(width: nope)", false)]
    [InlineData("width: 1280px", false)]
    public void EvaluatesSupportedDesktopProfile(string query, bool expected)
        => Assert.Equal(expected, CssMediaQueryEvaluator.Matches(query, Desktop));

    [Fact]
    public void EnvironmentCapabilitiesAreInputsRatherThanBackendConstants()
    {
        var environment = new CssMediaEnvironment(
            390,
            844,
            3,
            Pointer: CssMediaPointer.Coarse,
            Hover: CssMediaHover.None,
            ColorScheme: CssPreferredColorScheme.Dark,
            Motion: CssPreferredMotion.Reduce);

        Assert.True(CssMediaQueryEvaluator.Matches(
            "screen and (orientation: portrait) and (pointer: coarse) and (prefers-color-scheme: dark) and (prefers-reduced-motion: reduce)",
            environment));
        Assert.False(CssMediaQueryEvaluator.Matches("(hover: hover)", environment));
        Assert.True(CssMediaQueryEvaluator.Matches("(pointer: coarse)", environment));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void RejectsMissingQueries(string? query)
        => Assert.False(CssMediaQueryEvaluator.Matches(query, Desktop));

    [Theory]
    [InlineData(double.NaN, 10, 1)]
    [InlineData(10, double.PositiveInfinity, 1)]
    [InlineData(-1, 10, 1)]
    [InlineData(10, -1, 1)]
    [InlineData(10, 10, 0)]
    public void RejectsInvalidEnvironments(double width, double height, double ratio)
        => Assert.False(CssMediaQueryEvaluator.Matches(
            "screen",
            new CssMediaEnvironment(width, height, ratio)));
}

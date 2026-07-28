using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssStylesheetCompilerTests
{
    [Fact]
    public void PreservesScrollbarPseudoElementRules()
    {
        var result = CssStylesheetCompiler.Compile(
            ".scroller.no-scrollbar::-webkit-scrollbar { display: none; width: 0; height: 0; }");

        var rule = Assert.Single(result.Rules);
        Assert.Equal("-webkit-scrollbar", Assert.Single(
            rule.Selector.Parts[^1].Simple.Pseudos.Where(static pseudo => pseudo.IsElement)).Name);
        Assert.Contains(rule.Declarations, static declaration =>
            declaration.Name == "display" && declaration.Value == "none");
    }

    [Fact]
    public void CompilerProducesPortableSelectorsDeclarationsAndNestedMedia()
    {
        var result = CssStylesheetCompiler.Compile("""
            .alpha, #beta:hover {
              margin: var(--space);
              color: red !important;
            }
            @media (min-width: 10px) {
              .gamma { padding-block: 1px 2px; }
            }
            """);

        Assert.Equal(3, result.Rules.Count);
        Assert.All(result.Rules, rule => Assert.NotEmpty(rule.Selector.Parts));
        Assert.Contains(result.Rules, rule => rule.Selector.Parts[^1].Simple.Classes.Contains("alpha"));
        Assert.Contains(result.Rules, rule => rule.Selector.Parts[^1].Simple.Id == "beta");
        Assert.Contains(result.Rules.SelectMany(rule => rule.Declarations),
            declaration => declaration.Name == "margin" && declaration.Value.Contains("var(--space)", StringComparison.Ordinal));
        Assert.Contains(result.Rules.SelectMany(rule => rule.Declarations),
            declaration => declaration.Name == "color" && declaration.Important);
        var mediaRule = Assert.Single(result.Rules.Where(rule => rule.MediaQueries.Count > 0));
        Assert.Contains("min-width", Assert.Single(mediaRule.MediaQueries), StringComparison.OrdinalIgnoreCase);
        Assert.Contains(mediaRule.Declarations, declaration => declaration.Name == "padding-top" && declaration.Value == "1px");
        Assert.Contains(mediaRule.Declarations, declaration => declaration.Name == "padding-bottom" && declaration.Value == "2px");
    }

    [Fact]
    public void NormalizerPreservesVariableShorthandsAndMapsLogicalProperties()
    {
        var normalized = CssStylesheetCompiler.Normalize("""
            .x {
              background: var(--background);
              inset-inline-start: 3px;
              margin-inline: calc(1px + var(--x)) 4px !important;
            }
            """);

        Assert.Contains(CssStylesheetCompiler.ProtectedVariableShorthandPrefix + "background", normalized);
        Assert.Contains("left: 3px", normalized);
        Assert.Contains("margin-left:calc(1px + var(--x)) !important", normalized);
        Assert.Contains("margin-right:4px !important", normalized);
    }

    [Fact]
    public void CompilerPreservesVariableBorderRadiusForComputedValueResolution()
    {
        var result = CssStylesheetCompiler.Compile(".tool::before { border-radius: var(--hover-radius); }");

        var rule = Assert.Single(result.Rules);
        var declaration = Assert.Single(rule.Declarations);
        Assert.Equal("border-radius", declaration.Name);
        Assert.Equal("var(--hover-radius)", declaration.Value);
    }

    [Fact]
    public void CompilerPreservesCaseSensitiveCustomPropertyNamesAndCssTokenWhitespace()
    {
        var result = CssStylesheetCompiler.Compile(
            ".probe { --Token: upper; --token: lower; --vertical:\vkept\v; color: var(--Token); }");

        var declarations = Assert.Single(result.Rules).Declarations;
        Assert.Contains(declarations, declaration =>
            declaration.Name == "--Token" && declaration.Value == "upper");
        Assert.Contains(declarations, declaration =>
            declaration.Name == "--token" && declaration.Value == "lower");
        Assert.Contains(declarations, declaration =>
            declaration.Name == "--vertical" && declaration.Value == "\vkept\v");
        Assert.Contains(declarations, declaration =>
            declaration.Name == "color" && declaration.Value == "var(--Token)");
    }

    [Fact]
    public void CompilerAcceptsUnitlessZeroRotateWithoutAcceptingNonZeroUnitlessAngles()
    {
        var result = CssStylesheetCompiler.Compile("""
            .closed .toggler .icon { transform: rotate(0); }
            .invalid { transform: rotate(1); }
            """);

        var rule = Assert.Single(result.Rules);
        Assert.Equal(3, rule.Selector.Parts.Count);
        var declaration = Assert.Single(rule.Declarations);
        Assert.Equal("transform", declaration.Name);
        Assert.Equal("rotate(0deg)", declaration.Value);
    }

    [Fact]
    public void CompilerPreservesTransformTransitionShorthandForRuntimePresentation()
    {
        var result = CssStylesheetCompiler.Compile(
            ".icon { transition: transform .1s cubic-bezier(.06,.52,1,.54); }");

        var declaration = Assert.Single(Assert.Single(result.Rules).Declarations);
        Assert.Equal("transition", declaration.Name);
        Assert.Contains("transform", declaration.Value, StringComparison.Ordinal);
        Assert.Contains("cubic-bezier", declaration.Value, StringComparison.Ordinal);
    }

    [Fact]
    public void CompilerPreservesBackgroundCurrentColorForWebSceneCascadeExpansion()
    {
        var result = CssStylesheetCompiler.Compile(".swatch { background: red; background: currentColor; }");

        var declarations = Assert.Single(result.Rules).Declarations;
        Assert.Contains(declarations, declaration =>
            declaration.Name == "background" && declaration.Value == "currentColor");
    }

    [Fact]
    public void CompilerPreservesAllUnsetForPerPropertyCascadeExpansion()
    {
        var result = CssStylesheetCompiler.Compile(
            ".control { padding: 11px; } .control { all: unset; display: flex; }");

        Assert.Equal(2, result.Rules.Count);
        Assert.All(result.Rules[0].Declarations, declaration =>
            Assert.StartsWith("padding-", declaration.Name, StringComparison.Ordinal));
        var declarations = result.Rules[1].Declarations;
        var allIndex = declarations.ToList().FindIndex(declaration => declaration.Name == "all");
        var displayIndex = declarations.ToList().FindIndex(declaration => declaration.Name == "display");
        Assert.Equal(0, allIndex);
        Assert.True(displayIndex > allIndex);
        Assert.Equal("unset", declarations[allIndex].Value);
    }

    [Fact]
    public void CompilerProjectsFontFaceMatchingAndSourceDescriptors()
    {
        var result = CssStylesheetCompiler.Compile("""
            @font-face {
              font-family: "Chart Face";
              src: local("Chart Face"), url("fonts/chart.woff2") format("woff2"), url(fonts/chart.ttf) format("truetype");
              font-style: italic;
              font-weight: 600;
              font-stretch: condensed;
            }
            .chart { font-family: "Chart Face", sans-serif; }
            """);

        var face = Assert.Single(result.FontFaces);
        Assert.Equal("\"Chart Face\"", face.Family);
        Assert.Contains("fonts/chart.woff2", face.Source, StringComparison.Ordinal);
        Assert.Contains("fonts/chart.ttf", face.Source, StringComparison.Ordinal);
        Assert.Equal("italic", face.Style);
        Assert.Equal("600", face.Weight);
        Assert.Equal("condensed", face.Stretch);
        Assert.Single(result.Rules);
    }

    [Fact]
    public void CompilerEvaluatesSupportsSelectorConditionsAgainstWebSceneSelectorSupport()
    {
        var result = CssStylesheetCompiler.Compile("""
            .baseline { color: black; }
            @supports selector(:focus-visible) {
              .supported { color: green; }
            }
            @supports not selector(:focus-visible) {
              .fallback { color: red; }
            }
            @supports selector(:webscene-unknown-pseudo) {
              .unknown { color: red; }
            }
            @supports selector(:focus:not(:focus-visible)) and (not selector(:webscene-unknown-pseudo)) {
              .combined { color: green; }
            }
            @supports (display: grid) {
              .declaration-query { display: grid; }
            }
            """);

        var classes = result.Rules
            .SelectMany(rule => rule.Selector.Parts[^1].Simple.Classes)
            .ToArray();
        Assert.Contains("baseline", classes);
        Assert.Contains("supported", classes);
        Assert.Contains("combined", classes);
        Assert.Contains("declaration-query", classes);
        Assert.DoesNotContain("fallback", classes);
        Assert.DoesNotContain("unknown", classes);
    }

    [Fact]
    public void CompilerPreservesOutlineShorthandAndLonghands()
    {
        var result = CssStylesheetCompiler.Compile("""
            .shorthand { outline: green solid 5px; }
            .longhands {
              outline-color: rgb(1, 2, 3);
              outline-style: dashed;
              outline-width: 2px;
              outline-offset: 4px;
            }
            .variable { outline: var(--focus-ring); }
            """);

        var shorthand = Assert.Single(result.Rules.Where(rule =>
            rule.Selector.Parts[^1].Simple.Classes.Contains("shorthand")));
        Assert.Contains(shorthand.Declarations, declaration =>
            declaration.Name == "outline-color" && declaration.Value.Contains("0, 128, 0", StringComparison.Ordinal));
        Assert.Contains(shorthand.Declarations, declaration =>
            declaration.Name == "outline-style" && declaration.Value == "solid");
        Assert.Contains(shorthand.Declarations, declaration =>
            declaration.Name == "outline-width" && declaration.Value == "5px");

        var longhands = Assert.Single(result.Rules.Where(rule =>
            rule.Selector.Parts[^1].Simple.Classes.Contains("longhands")));
        Assert.Contains(longhands.Declarations, declaration =>
            declaration.Name == "outline-color" && declaration.Value.Contains("1, 2, 3", StringComparison.Ordinal));
        Assert.Contains(longhands.Declarations, declaration =>
            declaration.Name == "outline-style" && declaration.Value == "dashed");
        Assert.Contains(longhands.Declarations, declaration =>
            declaration.Name == "outline-width" && declaration.Value == "2px");
        Assert.Contains(longhands.Declarations, declaration =>
            declaration.Name == "outline-offset" && declaration.Value == "4px");

        var variable = Assert.Single(result.Rules.Where(rule =>
            rule.Selector.Parts[^1].Simple.Classes.Contains("variable")));
        Assert.Contains(variable.Declarations, declaration =>
            declaration.Name == "outline" && declaration.Value == "var(--focus-ring)");
    }

    [Fact]
    public void InvalidLogicalAxisArityIsLeftForTheSyntaxParser()
    {
        const string css = ".x { padding-block: 1px 2px 3px; }";

        Assert.Equal(css, CssStylesheetCompiler.Normalize(css));
    }

    [Fact]
    public void OptionalMetricsAndGuardDisabledPathRemainDeterministic()
    {
        var result = CssStylesheetCompiler.Compile(
            ".x { color: blue; }",
            disableNormalizationGuards: true,
            collectPerformanceMetrics: true);

        Assert.Single(result.Rules);
        Assert.True(result.NormalizationTicks >= 0);
        Assert.True(result.ParserTicks >= 0);
        Assert.True(result.RuleCompilationTicks >= 0);
        Assert.True(result.NormalizationAllocatedBytes >= 0);
        Assert.True(result.ParserAllocatedBytes >= 0);
        Assert.True(result.RuleCompilationAllocatedBytes >= 0);
    }

    [Fact]
    public void NullStylesheetInputsAreRejected()
    {
        Assert.Throws<ArgumentNullException>(() => CssStylesheetCompiler.Normalize(null!));
        Assert.Throws<ArgumentNullException>(() => CssStylesheetCompiler.Compile(null!));
    }
}

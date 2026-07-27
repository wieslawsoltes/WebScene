using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssVariableSideBorderCompilerRegressionTests
{
    [Fact]
    public void CompilerPreservesVariableSideBorderShorthandForComputedValueResolution()
    {
        var result = CssStylesheetCompiler.Compile(
            ".footer { border-top: 1px solid var(--divider); }");

        var declaration = Assert.Single(Assert.Single(result.Rules).Declarations);
        Assert.Equal("border-top", declaration.Name);
        Assert.Equal("1px solid var(--divider)", declaration.Value);
    }

    [Fact]
    public void CompilerMapsVariableInlineEndBorderShorthandToPhysicalSide()
    {
        var result = CssStylesheetCompiler.Compile(
            ".sidebar { border-inline-end: 1px solid var(--divider); }");

        var declaration = Assert.Single(Assert.Single(result.Rules).Declarations);
        Assert.Equal("border-right", declaration.Name);
        Assert.Equal("1px solid var(--divider)", declaration.Value);
    }
}

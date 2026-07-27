using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssLogicalCornerRadiusCompilerRegressionTests
{
    [Fact]
    public void CompilerMapsLogicalCornerRadiiToLtrPhysicalCorners()
    {
        // WPT: css/css-logical/logical-box-border-radius.html. WebScene's current
        // projection is horizontal-tb/ltr, so this spike covers that WPT mapping.
        var result = CssStylesheetCompiler.Compile("""
            .toolbar {
                border-start-start-radius: 1px;
                border-start-end-radius: 2px;
                border-end-start-radius: 3px;
                border-end-end-radius: 4px;
            }
            """);

        var declarations = Assert.Single(result.Rules).Declarations;
        Assert.Contains(declarations, item => item.Name == "border-top-left-radius" && item.Value == "1px");
        Assert.Contains(declarations, item => item.Name == "border-top-right-radius" && item.Value == "2px");
        Assert.Contains(declarations, item => item.Name == "border-bottom-left-radius" && item.Value == "3px");
        Assert.Contains(declarations, item => item.Name == "border-bottom-right-radius" && item.Value == "4px");
    }
}

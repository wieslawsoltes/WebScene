using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssRepeatedFlexShorthandRegressionTests
{
    [Fact]
    public void LaterFlexNoneWinsOverEarlierFlexibleShorthand()
    {
        var result = CssStylesheetCompiler.Compile(
            ".sidebar { display: flex; flex: 1 1 auto; flex: none; width: 200px; }");

        var declarations = Assert.Single(result.Rules).Declarations;
        Assert.Equal(
            "display=flex|flex=none|width=200px",
            string.Join('|', declarations.Select(declaration => $"{declaration.Name}={declaration.Value}")));
    }
}

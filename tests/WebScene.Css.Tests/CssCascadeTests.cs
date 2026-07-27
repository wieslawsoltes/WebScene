using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssCascadeTests
{
    [Fact]
    public void ImportantThenSpecificityThenSourceOrderDetermineWinner()
    {
        var winners = new Dictionary<string, CssCascadeWinner>(StringComparer.OrdinalIgnoreCase);
        CssCascade.ApplyWinner(winners, "color", "red", false, 100, 1);
        CssCascade.ApplyWinner(winners, "color", "blue", false, 10, 2);
        Assert.Equal("red", winners["color"].Value);

        CssCascade.ApplyWinner(winners, "color", "green", true, 1, 0);
        CssCascade.ApplyWinner(winners, "color", "purple", true, 1, 3);
        Assert.Equal("purple", winners["color"].Value);
        Assert.Equal(0, winners["color"].Sequence);
    }

    [Fact]
    public void PropertyEnumerationSequenceRemainsStableWhenWinnerChanges()
    {
        var winners = new Dictionary<string, CssCascadeWinner>();
        CssCascade.ApplyWinner(winners, "width", "10px", false, 1, 0);
        CssCascade.ApplyWinner(winners, "height", "20px", false, 1, 1);
        CssCascade.ApplyWinner(winners, "width", "30px", false, 1, 2);

        Assert.Equal(0, winners["width"].Sequence);
        Assert.Equal(1, winners["height"].Sequence);
    }
}

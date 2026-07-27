using WebScene.Core;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssFlexEmptyItemMinimumTests
{
    [Fact]
    public void EmptyFlexItemUsesZeroContentMinimumWhenMarginCreatesNegativeFreeSpace()
    {
        var root = new CssLayoutNode(1, new CssLayoutStyle
        {
            Display = CssLayoutDisplay.Flex,
            FlexWrap = CssLayoutFlexWrap.Wrap
        });
        root.Add(new CssLayoutNode(2, new CssLayoutStyle
        {
            Width = CssLayoutLength.Pixels(48),
            Border = new CssLayoutEdges(
                CssLayoutLength.Pixels(1),
                CssLayoutLength.Pixels(1),
                CssLayoutLength.Pixels(1),
                CssLayoutLength.Pixels(1)),
            Margin = new CssLayoutEdges(
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(0),
                CssLayoutLength.Pixels(80))
        })
        {
            // A backend may report the previously measured specified width as
            // DesiredSize. With no content children, it is not a min-content
            // contribution and must not prevent flex shrink.
            IntrinsicSize = new WebSceneSize(50, 2)
        });

        var snapshot = new CssArrangementEngine().Arrange(root, new WebSceneSize(100, 12));

        Assert.Equal(new WebSceneRect(80, 0, 20, 12), snapshot[2].BorderBox);
    }
}

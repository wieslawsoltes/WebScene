using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssMutationInvalidationPlannerTests
{
    [Theory]
    [InlineData("color:red", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Descendants)]
    [InlineData("fill:currentColor", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Descendants)]
    [InlineData("stroke-width:2", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Descendants)]
    [InlineData("--theme:red", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Descendants)]
    [InlineData("width:10px", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Layout)]
    [InlineData("flex-wrap:wrap", CssMutationInvalidationScope.Target | CssMutationInvalidationScope.Layout)]
    [InlineData("background:red", CssMutationInvalidationScope.Target)]
    [InlineData("broken", CssMutationInvalidationScope.Target)]
    public void InlineStylePlanClassifiesPortableScope(string style, CssMutationInvalidationScope expected)
    {
        var plan = CssMutationInvalidationPlanner.PlanInlineStyle(null, style);
        Assert.Equal(expected, plan.Scope);
    }

    [Fact]
    public void InlineStylePlanUnionsOldAndNewDeclarationsWithoutAllocatingParsedRules()
    {
        var plan = CssMutationInvalidationPlanner.PlanInlineStyle(
            "width:10px; color: red",
            "background:blue");

        Assert.True(plan.Affects(CssMutationInvalidationScope.Target));
        Assert.True(plan.Affects(CssMutationInvalidationScope.Descendants));
        Assert.True(plan.Affects(CssMutationInvalidationScope.Layout));
    }

    [Fact]
    public void ClassPlanDistinguishesTargetDescendantAndSiblingSubjects()
    {
        var targetOnly = CssSelectorDependencyAnalyzer.Analyze(
            Parse(".changed"));
        var descendant = CssSelectorDependencyAnalyzer.Analyze(
            Parse(".changed .child"));
        var sibling = CssSelectorDependencyAnalyzer.Analyze(
            Parse(".changed + .peer"));

        var targetPlan = CssMutationInvalidationPlanner.PlanClassChange(
            string.Empty, "changed", new[] { targetOnly });
        var descendantPlan = CssMutationInvalidationPlanner.PlanClassChange(
            string.Empty, "changed", new[] { descendant });
        var siblingPlan = CssMutationInvalidationPlanner.PlanClassChange(
            string.Empty, "changed", new[] { sibling });

        Assert.Equal(CssMutationInvalidationScope.Target, targetPlan.Scope);
        Assert.True(descendantPlan.Affects(CssMutationInvalidationScope.Descendants));
        Assert.True(siblingPlan.Affects(CssMutationInvalidationScope.FollowingSiblings));
        Assert.Throws<ArgumentNullException>(() =>
            CssMutationInvalidationPlanner.PlanClassChange(null, null, null!));
    }

    [Fact]
    public void ChildListPlanUsesPortablePositionEmptyAncestorAndSiblingDependencies()
    {
        var profiles = new[]
        {
            CssSelectorDependencyAnalyzer.Analyze(Parse(".row:first-child")),
            CssSelectorDependencyAnalyzer.Analyze(Parse(".row:last-child")),
            CssSelectorDependencyAnalyzer.Analyze(Parse(".list:empty .placeholder")),
            CssSelectorDependencyAnalyzer.Analyze(Parse(".row + .row"))
        };

        var removal = CssMutationInvalidationPlanner.PlanChildList(profiles, appendAtEnd: false);
        var append = CssMutationInvalidationPlanner.PlanChildList(profiles, appendAtEnd: true);

        Assert.True(removal.Affects(CssMutationInvalidationScope.Target));
        Assert.True(removal.Affects(CssMutationInvalidationScope.Layout));
        Assert.True(removal.Affects(CssMutationInvalidationScope.PreviousSiblings));
        Assert.True(removal.Affects(CssMutationInvalidationScope.FollowingSiblings));
        Assert.True(removal.Affects(CssMutationInvalidationScope.Descendants));
        Assert.False(append.Affects(CssMutationInvalidationScope.FollowingSiblings));
        Assert.Throws<ArgumentNullException>(() =>
            CssMutationInvalidationPlanner.PlanChildList(null!, appendAtEnd: false));
    }

    private static CssSelectorSyntax Parse(string text)
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(text, out var selector));
        return selector;
    }
}

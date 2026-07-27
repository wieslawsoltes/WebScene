using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssSelectorMatcherTests
{
    [Fact]
    public void MatchesCombinatorsAttributesPositionAndDynamicState()
    {
        var root = new Node("html") { IsRoot = true };
        var body = root.Add(new Node("body"));
        var toolbar = body.Add(new Node("div").WithClass("toolbar"));
        toolbar.Add(new Node("button").WithAttribute("data-mode", "select"));
        var target = toolbar.Add(new Node("button")
            .WithAttribute("data-mode", "DRAW")
            .WithState(CssSelectorState.Hover));
        Assert.True(CssSelectorSyntaxParser.TryParse(
            "html body .toolbar > button:nth-child(2)[data-mode='draw' i]:hover",
            out var selector));

        Assert.True(CssSelectorMatcher.Matches(selector, target));
    }

    [Fact]
    public void MatchesSelectorListArgumentsAndPseudoElements()
    {
        var node = new Node("div").WithClass("chart");
        Assert.True(CssSelectorSyntaxParser.TryParse("div:is(.chart, .table)::before", out var selector));

        Assert.False(CssSelectorMatcher.Matches(selector, node));
        Assert.True(CssSelectorMatcher.Matches(
            selector,
            node,
            new CssSelectorMatchOptions(PseudoElementName: "before")));
    }

    [Theory]
    [InlineData("[data-list~='two']", true)]
    [InlineData("[lang|='en']", true)]
    [InlineData("[data-prefix^='abc']", true)]
    [InlineData("[data-suffix$='xyz']", true)]
    [InlineData("[data-middle*='MID']", true)]
    [InlineData("[present]", true)]
    [InlineData("[missing]", false)]
    [InlineData("#wrong", false)]
    [InlineData("span", false)]
    public void MatchesAttributeOperatorsAndIdentity(string text, bool expected)
    {
        var node = new Node("div")
            .WithAttribute("id", "target")
            .WithAttribute("data-list", "one two three")
            .WithAttribute("lang", "en-US")
            .WithAttribute("data-prefix", "abcdef")
            .WithAttribute("data-suffix", "abcxyz")
            .WithAttribute("data-middle", "abcMIDxyz")
            .WithAttribute("present", string.Empty);
        Assert.True(CssSelectorSyntaxParser.TryParse(text, out var selector));
        Assert.Equal(expected, CssSelectorMatcher.Matches(selector, node));
    }

    [Theory]
    [InlineData(":first-child", false)]
    [InlineData(":last-child", false)]
    [InlineData(":only-child", false)]
    [InlineData(":nth-child(even)", true)]
    [InlineData(":nth-last-child(2)", true)]
    [InlineData(":nth-of-type(2n)", true)]
    [InlineData(":nth-last-of-type(odd)", false)]
    [InlineData(":first-of-type", false)]
    [InlineData(":last-of-type", false)]
    [InlineData(":only-of-type", false)]
    [InlineData(":not(.missing)", true)]
    [InlineData(":is(.selected)", true)]
    public void MatchesStructuralPseudos(string text, bool expected)
    {
        var parent = new Node("section");
        parent.Add(new Node("span"));
        var target = parent.Add(new Node("span").WithClass("selected"));
        parent.Add(new Node("span"));
        Assert.True(CssSelectorSyntaxParser.TryParse(text, out var selector));
        Assert.Equal(expected, CssSelectorMatcher.Matches(selector, target));
    }

    [Fact]
    public void GeneralAndAdjacentSiblingCombinatorsArePortable()
    {
        var parent = new Node("section");
        parent.Add(new Node("h1"));
        parent.Add(new Node("p").WithClass("lead"));
        var target = parent.Add(new Node("p").WithClass("body"));

        Assert.True(CssSelectorSyntaxParser.TryParse("h1 ~ p.body", out var general));
        Assert.True(CssSelectorMatcher.Matches(general, target));
        Assert.True(CssSelectorSyntaxParser.TryParse("p.lead + p.body", out var adjacent));
        Assert.True(CssSelectorMatcher.Matches(adjacent, target));
    }

    [Fact]
    public void MatchOptionsCanIgnoreDynamicAndChildListDependencies()
    {
        var parent = new Node("div");
        var target = parent.Add(new Node("button"));
        parent.Add(new Node("button"));
        Assert.True(CssSelectorSyntaxParser.TryParse("button:last-child:hover", out var selector));

        Assert.False(CssSelectorMatcher.Matches(selector, target));
        Assert.True(CssSelectorMatcher.Matches(
            selector,
            target,
            new CssSelectorMatchOptions(IgnoreChildListPseudos: true, IgnoreDynamicPseudos: true)));
    }

    [Theory]
    [InlineData("button", CssSelectorState.None, ":enabled", true)]
    [InlineData("button", CssSelectorState.Disabled, ":disabled", true)]
    [InlineData("button", CssSelectorState.Disabled, ":enabled", false)]
    [InlineData("span", CssSelectorState.None, ":enabled", false)]
    [InlineData("span", CssSelectorState.Disabled, ":disabled", false)]
    public void EnabledAndDisabledOnlyMatchDisableCapableElements(
        string tagName,
        CssSelectorState state,
        string selectorText,
        bool expected)
    {
        var node = new Node(tagName).WithState(state);
        Assert.True(CssSelectorSyntaxParser.TryParse(selectorText, out var selector));

        Assert.Equal(expected, CssSelectorMatcher.Matches(selector, node));
    }

    [Fact]
    public void GenericFocusDoesNotImplyFocusVisible()
    {
        Assert.True(CssSelectorSyntaxParser.TryParse(":focus", out var focusSelector));
        Assert.True(CssSelectorSyntaxParser.TryParse(":focus-visible", out var focusVisibleSelector));
        var pointerFocused = new Node("button").WithState(CssSelectorState.Focus);
        var keyboardFocused = new Node("button")
            .WithState(CssSelectorState.Focus)
            .WithState(CssSelectorState.FocusVisible);

        Assert.True(CssSelectorMatcher.Matches(focusSelector, pointerFocused));
        Assert.False(CssSelectorMatcher.Matches(focusVisibleSelector, pointerFocused));
        Assert.True(CssSelectorMatcher.Matches(focusSelector, keyboardFocused));
        Assert.True(CssSelectorMatcher.Matches(focusVisibleSelector, keyboardFocused));
    }

    [Fact]
    public void ScopePseudoUsesTheExplicitElementQueryRoot()
    {
        var root = new Node("div");
        var outer = root.Add(new Node("div").WithClass("collapse"));
        var wrapper = outer.Add(new Node("div"));
        var nested = wrapper.Add(new Node("div").WithClass("collapse"));
        Assert.True(CssSelectorSyntaxParser.TryParse(
            ":scope .collapse .collapse",
            out var selector));
        var options = new CssSelectorMatchOptions(ScopeNode: root);

        Assert.True(CssSelectorMatcher.Matches(selector, nested, options));
        Assert.False(CssSelectorMatcher.Matches(selector, outer, options));
    }

    private sealed class Node(string tagName) : ICssSelectorNode
    {
        private readonly Dictionary<string, string> _attributes = new(StringComparer.OrdinalIgnoreCase);
        private readonly HashSet<string> _classes = new(StringComparer.Ordinal);
        private readonly List<Node> _children = [];
        private CssSelectorState _state;

        public string TagName { get; } = tagName;
        public string Id => GetAttribute("id") ?? string.Empty;
        public string TextContent { get; init; } = string.Empty;
        public int ChildElementCount => _children.Count;
        public bool IsRoot { get; init; }
        public bool IsDocumentElement => IsRoot;
        public ICssSelectorNode? ParentElement { get; private set; }
        public ICssSelectorNode? PreviousElementSibling => Sibling(-1);
        public ICssSelectorNode? NextElementSibling => Sibling(1);

        public Node Add(Node child)
        {
            child.ParentElement = this;
            _children.Add(child);
            return child;
        }

        public Node WithClass(string value) { _classes.Add(value); return this; }
        public Node WithAttribute(string name, string value) { _attributes[name] = value; return this; }
        public Node WithState(CssSelectorState value) { _state |= value; return this; }
        public bool HasClass(string className) => _classes.Contains(className);
        public string? GetAttribute(string name) => _attributes.TryGetValue(name, out var value) ? value : null;
        public bool HasState(CssSelectorState state) => (_state & state) == state;

        private Node? Sibling(int offset)
        {
            if (ParentElement is not Node parent) return null;
            var index = parent._children.IndexOf(this) + offset;
            return index >= 0 && index < parent._children.Count ? parent._children[index] : null;
        }
    }
}

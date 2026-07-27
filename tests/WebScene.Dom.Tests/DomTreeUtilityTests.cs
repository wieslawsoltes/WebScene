using JavaScript.Avalonia;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomTreeUtilityTests
{
    [Fact]
    public void FragmentUsesTheTypedContainerWithoutChangingNodeIdentity()
    {
        var container = new RecordingElement("div", "container");
        var child = new RecordingElement("span", "child");
        var fragment = new RecordingFragment(container);

        var appended = fragment.appendChild(child);

        Assert.Same(child, appended);
        Assert.Same(child, fragment.firstChild);
        Assert.Same(child, fragment.firstElementChild);
        Assert.Same(child, fragment.childNodes[0]);
        Assert.Equal(11, fragment.nodeType);
        Assert.Equal("#document-fragment", fragment.nodeName);

        Assert.Same(child, fragment.removeChild(child));
        Assert.Empty(fragment.childNodes);
    }

    [Fact]
    public void ParsedDocumentIncludesMatchingRootBeforeDescendants()
    {
        var root = new RecordingElement("html", "root");
        var descendant = new RecordingElement("html", "nested");
        root.AppendChild(descendant);
        var document = new DomParsedDocumentCore<RecordingElement>(root);

        Assert.Same(root, document.querySelector("#root"));
        Assert.Equal(new object[] { root, descendant }, document.querySelectorAll("html"));
        Assert.Equal(new object[] { root, descendant }, document.getElementsByTagName("html"));
        Assert.Same(root, document.firstElementChild);
    }

    [Fact]
    public void EmptyParsedDocumentReturnsSharedEmptyShapes()
    {
        var document = new DomParsedDocumentCore<RecordingElement>(null);

        Assert.Null(document.querySelector("*"));
        Assert.Empty(document.querySelectorAll("*"));
        Assert.Empty(document.getElementsByTagName("div"));
        Assert.Empty(document.childNodes);
    }

    [Fact]
    public void RangeAlgorithmCreatesAndPopulatesOneBackendContainer()
    {
        var range = new RecordingRange();

        var fragment = range.createContextualFragment("<b>value</b>");

        Assert.Same(range.Container, fragment);
        Assert.Equal("<b>value</b>", range.Markup);
        Assert.Equal(1, range.ContainerCreations);
        Assert.Equal(1, range.FragmentCreations);
    }

    private sealed class RecordingRange : DomRangeCore
    {
        public object Container { get; } = new();
        public string? Markup { get; private set; }
        public int ContainerCreations { get; private set; }
        public int FragmentCreations { get; private set; }

        protected override object CreateContextualContainer()
        {
            ContainerCreations++;
            return Container;
        }

        protected override void SetInnerHtml(object container, string html)
        {
            Assert.Same(Container, container);
            Markup = html;
        }

        protected override object CreateFragment(object container)
        {
            Assert.Same(Container, container);
            FragmentCreations++;
            return container;
        }
    }

    private sealed class RecordingFragment : DomDocumentFragmentCore<RecordingElement>
    {
        public RecordingFragment(RecordingElement container)
            : base(container)
        {
        }
    }

    private sealed class RecordingElement : IDomContainerElement<RecordingElement>
    {
        private readonly List<RecordingElement> _children = [];

        public RecordingElement(string tagName, string id)
        {
            TagName = tagName;
            Id = id;
        }

        public RecordingElement? FirstElementChild => _children.FirstOrDefault();
        public RecordingElement? FirstChild => _children.FirstOrDefault();
        public object[] ChildNodes => _children.Cast<object>().ToArray();
        public object[] Children => ChildNodes;
        public int ChildElementCount => _children.Count;
        public string Id { get; }
        public string TagName { get; }

        public object? QuerySelector(string selector)
            => QuerySelectorAll(selector).FirstOrDefault();

        public object[] QuerySelectorAll(string selector)
            => _children
                .Where(child => Matches(child, selector))
                .Cast<object>()
                .ToArray();

        public object[] GetElementsByTagName(string tagName)
            => _children
                .Where(child => tagName == "*" || string.Equals(child.TagName, tagName, StringComparison.OrdinalIgnoreCase))
                .Cast<object>()
                .ToArray();

        public RecordingElement? AppendChild(RecordingElement child)
        {
            _children.Add(child);
            return child;
        }

        public RecordingElement? RemoveChild(RecordingElement child)
            => _children.Remove(child) ? child : null;

        private static bool Matches(RecordingElement element, string selector)
            => selector == "*"
               || (selector.StartsWith('#') && string.Equals(element.Id, selector[1..], StringComparison.Ordinal))
               || string.Equals(element.TagName, selector, StringComparison.OrdinalIgnoreCase);
    }
}

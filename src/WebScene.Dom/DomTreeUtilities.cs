namespace JavaScript.Avalonia;

/// <summary>
/// Typed tree operations used by framework-neutral fragment and parsed-document
/// algorithms. Generic specialization preserves the backend element type and avoids
/// allocating bridge wrappers for fragment operations.
/// </summary>
public interface IDomContainerElement<TElement>
    where TElement : class
{
    TElement? FirstElementChild { get; }

    TElement? FirstChild { get; }

    object[] ChildNodes { get; }

    object[] Children { get; }

    int ChildElementCount { get; }

    string Id { get; }

    string TagName { get; }

    object? QuerySelector(string selector);

    object[] QuerySelectorAll(string selector);

    object[] GetElementsByTagName(string tagName);

    TElement? AppendChild(TElement child);

    TElement? RemoveChild(TElement child);
}

public abstract class DomRangeCore
{
    public virtual void selectNodeContents(object node)
    {
    }

    public object? createContextualFragment(string html)
    {
        var container = CreateContextualContainer();
        if (container is null)
        {
            return null;
        }

        SetInnerHtml(container, html);
        return CreateFragment(container);
    }

    protected abstract object? CreateContextualContainer();

    protected abstract void SetInnerHtml(object container, string html);

    protected abstract object CreateFragment(object container);
}

public class DomDocumentFragmentCore<TElement> : DomNodeCore
    where TElement : class, IDomContainerElement<TElement>
{
    private readonly TElement _container;

    protected DomDocumentFragmentCore(TElement container)
    {
        _container = container ?? throw new ArgumentNullException(nameof(container));
    }

    public object? firstElementChild => _container.FirstElementChild;

    public object? firstChild => _container.FirstChild;

    public object[] childNodes => _container.ChildNodes;

    public object[] children => _container.Children;

    public int childElementCount => _container.ChildElementCount;

    public int nodeType => 11;

    public string nodeName => "#document-fragment";

    public object? querySelector(string selector) => _container.QuerySelector(selector);

    public object[] querySelectorAll(string selector) => _container.QuerySelectorAll(selector);

    public object? appendChild(object child)
        => child is TElement element ? _container.AppendChild(element) : null;

    public object? removeChild(object child)
        => child is TElement element ? _container.RemoveChild(element) : null;
}

public class DomParsedDocumentCore<TElement>
    where TElement : class, IDomContainerElement<TElement>
{
    public DomParsedDocumentCore(TElement? documentElement, TElement? body = null)
    {
        this.documentElement = documentElement;
        this.body = body;
    }

    public TElement? documentElement { get; }

    public TElement? body { get; }

    public TElement? firstElementChild => documentElement;

    public TElement? firstChild => documentElement;

    public object[] childNodes => documentElement is null ? [] : [documentElement];

    public object[] children => childNodes;

    public object? querySelector(string selector)
    {
        if (documentElement is null)
        {
            return null;
        }
        return RootMatches(selector) ? documentElement : documentElement.QuerySelector(selector);
    }

    public object[] querySelectorAll(string selector)
    {
        if (documentElement is null)
        {
            return [];
        }

        var descendants = documentElement.QuerySelectorAll(selector);
        if (!RootMatches(selector))
        {
            return descendants;
        }

        var result = new object[descendants.Length + 1];
        result[0] = documentElement;
        Array.Copy(descendants, 0, result, 1, descendants.Length);
        return result;
    }

    public object[] getElementsByTagName(string tagName)
    {
        if (documentElement is null)
        {
            return [];
        }

        var descendants = documentElement.GetElementsByTagName(tagName);
        if (tagName != "*" && !string.Equals(documentElement.TagName, tagName, StringComparison.OrdinalIgnoreCase))
        {
            return descendants;
        }

        var result = new object[descendants.Length + 1];
        result[0] = documentElement;
        Array.Copy(descendants, 0, result, 1, descendants.Length);
        return result;
    }

    private bool RootMatches(string selector)
    {
        if (documentElement is null || string.IsNullOrWhiteSpace(selector))
        {
            return false;
        }

        var normalized = selector.Trim();
        if (normalized == "*")
        {
            return true;
        }
        if (normalized.StartsWith('#'))
        {
            return string.Equals(documentElement.Id, normalized[1..], StringComparison.Ordinal);
        }
        return string.Equals(documentElement.TagName, normalized, StringComparison.OrdinalIgnoreCase);
    }
}

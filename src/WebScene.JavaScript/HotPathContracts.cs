using WebScene.Core;

namespace WebScene.JavaScript;

/// <summary>
/// Allocation-free geometry read used by engine adapters instead of constructing a
/// public DOMRect wrapper for every property access.
/// </summary>
public interface IWebSceneDomRectTarget
{
    WebSceneRect ReadBoundingClientRect();
}

/// <summary>
/// Allocation-free single-box client-rect read. WebScene elements currently have
/// either one layout box or none, so engine adapters can avoid materializing a
/// managed DOMRect array for JavaScript's getClientRects().
/// </summary>
public interface IWebSceneDomClientRectsTarget
{
    bool TryReadClientRect(out WebSceneRect rect);
}

/// <summary>
/// Primitive numeric DOM reads used by engine adapters to avoid reflection and
/// boxing in layout-sensitive JavaScript loops.
/// </summary>
public interface IWebSceneDomNumericTarget
{
    double ReadDomNumericProperty(WebSceneDomNumericProperty property);
}

/// <summary>
/// Stable integer token used by typed native engine adapters to resolve a DOM node
/// without carrying a managed object through a generic JavaScript host boundary.
/// </summary>
public interface IWebSceneDomIdentityTarget
{
    long WebSceneDomIdentity { get; }
}

public enum WebSceneDomNumericProperty
{
    Width,
    Height,
    ClientWidth,
    ClientHeight,
    OffsetWidth,
    OffsetHeight,
    OffsetTop,
    OffsetLeft
}

/// <summary>
/// Primitive Canvas text measurement used by engine facades that only need width.
/// </summary>
public interface IWebSceneCanvasTextTarget
{
    double MeasureTextWidth(string text);
}

/// <summary>
/// Replays a complete Canvas packet in one backend call. Implementations must retain
/// packet validation and return the number of replayed operations.
/// </summary>
public interface IWebSceneCanvasBatchTarget
{
    int ReplayCanvasBatch(
        ReadOnlySpan<double> values,
        IReadOnlyList<string> strings);
}

public interface IWebSceneComputedStyleTarget
{
    int Length { get; }

    string GetPropertyValue(string propertyName);

    string Item(int index);
}

/// <summary>
/// Optional browser-shaped named-property catalog for a computed style target.
/// Kept separate so existing computed-style providers remain source compatible.
/// </summary>
public interface IWebSceneComputedStylePropertySupportTarget
{
    bool SupportsPropertyName(string propertyName);
}

/// <summary>
/// Optional extension for resolved values that change with presentation time.
/// Engine bridges must not memoize a property while this returns true.
/// </summary>
public interface IWebSceneLiveComputedStyleTarget
{
    bool IsPropertyLive(string propertyName);
}

/// <summary>
/// Typed inline-style write used by engine adapters to avoid dynamic member
/// dispatch while retaining the DOM implementation's normal mutation and
/// invalidation path.
/// </summary>
public interface IWebSceneCssStyleDeclarationTarget
{
    void SetProperty(string propertyName, string? value);
}

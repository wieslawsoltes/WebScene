using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Dynamic;
using System.Globalization;
using System.IO;
using System.Diagnostics;
using System.Linq;
using System.Linq.Expressions;
using System.Net.Http;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Xml.Linq;
using Avalonia;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Layout;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.Composition.Animations;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using HtmlML.Core;
using HtmlML.JavaScript;
using SkiaSharp;
using Svg.Skia;

namespace JavaScript.Avalonia;

public class AvaloniaDomDocument : DomDocumentCore<AvaloniaDomElement>, IHtmlMlJavaScriptDocument
{
    private static readonly ConditionalWeakTable<RoutedEventArgs, PointerDispatchState> s_pointerDispatchStates = new();
    private static readonly ConditionalWeakTable<RoutedEventArgs, KeyboardDispatchState> s_keyboardDispatchStates = new();
    private static readonly ConditionalWeakTable<RoutedEventArgs, TextInputDispatchState> s_textInputDispatchStates = new();
    private static readonly object s_activePointerDocumentsLock = new();
    private static readonly Dictionary<int, WeakReference<AvaloniaDomDocument>> s_activePointerDocuments = new();
    private static readonly bool s_traceCssInvalidations =
        string.Equals(
            Environment.GetEnvironmentVariable("HTMLML_TRACE_CSS_INVALIDATIONS"),
            "1",
            StringComparison.Ordinal);
    private static readonly bool s_disableIterativeDocumentTraversal =
        string.Equals(
            Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_ITERATIVE_DOCUMENT_TRAVERSAL"),
            "1",
            StringComparison.Ordinal);
    protected static readonly bool DisableScopedPositionedLayoutReapply =
        string.Equals(
            Environment.GetEnvironmentVariable("HTMLML_DISABLE_SCOPED_POSITIONED_LAYOUT_REAPPLY"),
            "1",
            StringComparison.Ordinal);
    private readonly Func<string, Control?>? _elementFactory;
    private readonly ConditionalWeakTable<Control, AvaloniaDomElement> _elementWrappers = new();
    private readonly Dictionary<string, List<DomEventRegistration>> _documentEventListeners = new(StringComparer.OrdinalIgnoreCase);
    private bool _readyStateScheduled;
    private readonly List<Action> _readyStateCompletionCallbacks = new();
    private string _readyState = "loading";
    private StringBuilder? _documentWriteBuffer;
    private readonly DomHeadElement _head;
    private readonly DomDocumentElement _documentElement;
    private DomDocumentImplementation? _implementation;
    private readonly Dictionary<string, string> _cookies = new(StringComparer.Ordinal);
    private readonly CssStyleEngine _styleEngine;
    private readonly CssFontFaceRegistry _fontFaces;
    private readonly List<DomMutationObserver> _mutationObservers = new();
    private bool _mutationDeliveryScheduled;
    private bool _stylesheetUpdateScheduled;
    private bool _layoutUpdateScheduled;
    private long _computedStyleSnapshotGeneration;
    private long _computedStyleSnapshotHitCount;
    private long _computedStyleSnapshotBuildCount;
    private long _computedStyleSnapshotBuildTicks;
    private long _computedStyleSnapshotBuildAllocatedBytes;
    private long _computedStyleSnapshotStateReuseCount;
    private readonly HashSet<IExternalJavaScriptCallback> _pendingResizeObserverCallbacks =
        new(ReferenceEqualityComparer.Instance);
    private bool _resizeObserverBatchDeliveryScheduled;
    private readonly HashSet<AvaloniaDomElement> _pendingResizeObserverTargets =
        new(ReferenceEqualityComparer.Instance);
    private bool _resizeObserverTargetDeliveryScheduled;
    private bool _topLevelInputHandlersAttached;
    private bool _topLevelPointerHandlersAttached;
    private AvaloniaDomElement? _domActiveElement;
    private long _actualFocusNotificationVersion;
    private long _focusTransitionVersion;
    private bool _focusTransitionInProgress;
    private readonly Dictionary<int, IPointer> _activePointers = new();
    private readonly Dictionary<int, AvaloniaDomElement> _activePointerTargets = new();
    private readonly Dictionary<int, AvaloniaDomElement> _pointerCaptures = new();
    private readonly Dictionary<int, int> _pointerClickCounts = new();
    private readonly HashSet<AvaloniaDomElement> _programmaticClickTargets =
        new(ReferenceEqualityComparer.Instance);
    private readonly Dictionary<int, AvaloniaDomElement> _pointerHoverTargets = new();
    private readonly Dictionary<int, Point> _lastPointerPositions = new();
    private readonly Dictionary<int, Vector> _lastPointerMovements = new();
    private readonly HashSet<AvaloniaDomElement> _scheduledScriptElements = new();
    private readonly List<HtmlMlResourceTimelineEntry> _resourceTimeline = new();
    private readonly List<string> _selectorMissDiagnostics = new();
    private readonly List<string> _eventDispatchDiagnostics = new();
    private long _layoutFlushRequestCount;
    private long _layoutFlushFastPathCount;
    private long _layoutFlushCount;
    private long _layoutFlushTicks;
    private long _layoutFlushAllocatedBytes;
    private long _layoutPassCount;
    private long _layoutPassTicks;
    private long _layoutPassAllocatedBytes;
    private long _domNodeCreationAllocatedBytes;
    private long _domNodeInsertionCount;
    private long _domNodeInsertionTicks;
    private long _domNodeInsertionAllocatedBytes;
    private long _childListMutationAllocatedBytes;
    private Dictionary<string, List<AvaloniaDomElement>>? _elementsById;
    private long _elementIdIndexBuildCount;
    private bool _forceElementPresentationApply;
    private bool _listMarkerCountersDirty;
    private AvaloniaBrowserHost.LocationJs? _browsingContextLocation;
    /// <summary>
    /// Optional owner-window object supplied by an external JavaScript engine.
    /// DOM relationships return this external runtime object when set.
    /// </summary>
    public object? ExternalWindowContext { get; set; }

    public IExternalWindowEventDispatcher? ExternalWindowEventDispatcher { get; set; }
    /// <summary>
    /// Adapter for opaque engine-owned function objects. Ordinary DOM event methods
    /// preserve listener identity without leaking engine values into the DOM layer.
    /// </summary>
    public IExternalDomEventListenerAdapter? ExternalEventListenerAdapter { get; set; }

    object IHtmlMlJavaScriptDocument.JavaScriptObject => this;

    object IHtmlMlJavaScriptDocument.Location => location;

    void IHtmlMlJavaScriptDocument.DetachExternalBrowsingContext(
        object frameElement,
        IExternalVirtualBrowsingContext context)
    {
        if (this is VirtualIframeDomDocument frameDocument)
        {
            frameDocument.DetachExternalRuntime(context);
        }

        if (frameElement is not AvaloniaDomElement element)
        {
            throw new ArgumentException(
                "The virtual browsing-context owner is not an Avalonia DOM element.",
                nameof(frameElement));
        }
        element.DetachExternalContentWindow(context);
    }

    protected AvaloniaBrowserHost Host { get; }

    internal TopLevel HostTopLevel => Host.TopLevel;
    internal HtmlMlViewportMetrics HostViewportMetrics => Host.Services.Viewport.HostMetrics;
    internal bool DiagnosticLoggingEnabled => Host.EnableDiagnosticLogging;
    internal bool TargetOnlyInlineStylesEnabled =>
        Host.EnableTargetOnlyInlineStyles && Host.TargetOnlyInlineStylesArmed;
    internal bool CollectPerformanceMetrics => Host.CollectPerformanceMetrics;

    internal bool HasUiThreadWorkBudget => Host.UiThreadWorkBudget > TimeSpan.Zero;

    internal void RecordUiThreadWork(UiThreadWorkKind kind, long elapsedTicks)
        => Host.RecordUiThreadWork(kind, elapsedTicks);

    internal void EnqueueResizeObserverCallbacks(IEnumerable<IExternalJavaScriptCallback> callbacks)
    {
        foreach (var callback in callbacks)
        {
            _pendingResizeObserverCallbacks.Add(callback);
        }

        if (_pendingResizeObserverCallbacks.Count == 0 || _resizeObserverBatchDeliveryScheduled)
        {
            return;
        }

        _resizeObserverBatchDeliveryScheduled = true;
        Host.Services.Dispatcher.Post(DeliverResizeObserverCallbackBatch, HtmlMlDispatchPriority.Background);
    }

    internal void ScheduleResizeObserverDelivery(AvaloniaDomElement target)
    {
        _pendingResizeObserverTargets.Add(target);
        if (_resizeObserverTargetDeliveryScheduled)
        {
            return;
        }

        _resizeObserverTargetDeliveryScheduled = true;
        Host.Services.Dispatcher.Post(DeliverResizeObserverTargets, HtmlMlDispatchPriority.Render);
    }

    private void DeliverResizeObserverTargets()
    {
        if (!_resizeObserverTargetDeliveryScheduled)
        {
            return;
        }

        // Size observation is a document-level phase. Waiting until JavaScript
        // unwinds prevents re-entrancy, while one queued pass avoids allocating
        // a dispatcher work item for every observed element in a dense chart.
        if (Host.IsExecutingJavaScript)
        {
            Host.Services.Dispatcher.Post(DeliverResizeObserverTargets, HtmlMlDispatchPriority.Background);
            return;
        }

        _resizeObserverTargetDeliveryScheduled = false;
        if (_pendingResizeObserverTargets.Count == 0)
        {
            return;
        }

        var targets = _pendingResizeObserverTargets.ToArray();
        _pendingResizeObserverTargets.Clear();
        foreach (var target in targets)
        {
            target.DeliverResizeObserverTargetNow();
        }
    }

    private void DeliverResizeObserverCallbackBatch()
    {
        if (!_resizeObserverBatchDeliveryScheduled)
        {
            return;
        }

        if (Host.IsExecutingJavaScript)
        {
            Host.Services.Dispatcher.Post(DeliverResizeObserverCallbackBatch, HtmlMlDispatchPriority.Background);
            return;
        }

        _resizeObserverBatchDeliveryScheduled = false;
        if (_pendingResizeObserverCallbacks.Count == 0)
        {
            return;
        }

        var callbacks = _pendingResizeObserverCallbacks.ToArray();
        _pendingResizeObserverCallbacks.Clear();
        foreach (var callback in callbacks)
        {
            try
            {
                using var measurement = Host.MeasureResizeCallback(ResizeCallbackKind.Observer);
                using (Host.EnterExternalJavaScriptCall())
                {
                    callback.Invoke(ExternalWindowContext);
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"External ResizeObserver callback failed: {ex.Message}");
            }
        }
    }
    internal long ElementIdIndexBuildCount => _elementIdIndexBuildCount;

    internal IReadOnlyList<string> SelectorMissDiagnostics => _selectorMissDiagnostics;
    internal IReadOnlyList<string> EventDispatchDiagnostics => _eventDispatchDiagnostics;
    internal void ClearSelectorMissDiagnostics() => _selectorMissDiagnostics.Clear();
    internal void ClearEventDispatchDiagnostics() => _eventDispatchDiagnostics.Clear();

    internal void RecordSelectorMiss(string scope, string? selector)
    {
        if (!Host.EnableDiagnosticLogging || string.IsNullOrWhiteSpace(selector) || _selectorMissDiagnostics.Count >= 200)
        {
            return;
        }
        _selectorMissDiagnostics.Add($"{scope}:{selector}");
    }

    public AvaloniaDomDocument(AvaloniaBrowserHost host, Func<string, Control?>? elementFactory = null)
        : this(host, elementFactory, attachTopLevelHandlers: true)
    {
    }

    protected AvaloniaDomDocument(
        AvaloniaBrowserHost host,
        Func<string, Control?>? elementFactory,
        bool attachTopLevelHandlers)
    {
        Host = host ?? throw new ArgumentNullException(nameof(host));
        _elementFactory = elementFactory;
        _head = new DomHeadElement(this);
        _documentElement = new DomDocumentElement(this, _head);
        _fontFaces = new CssFontFaceRegistry(this);
        _styleEngine = new CssStyleEngine(this);
        if (attachTopLevelHandlers)
        {
            EnsureTopLevelInputHandlers();
            EnsureTopLevelPointerHandlers();
        }
    }

    protected virtual Control? GetDocumentRoot()
    {
        var content = Host.TopLevel.Content as Control;
        if (content is Border border && border.Child is Control child)
        {
            return child;
        }
        return content;
    }

    private Control? GetDocumentBodyRoot() => GetDocumentRoot();

    internal void EnableNativeLayoutHotPath()
    {
        if (GetDocumentRoot() is { } root)
        {
            CssLayout.SetNativeLayoutHotPath(root, true);
        }
    }

    public virtual string readyState => _readyState;

    // React feature-detects modern text input support with
    // `"oninput" in document` before it installs its delegated `input`
    // listener. Expose the standard event-handler slot even though event
    // delivery itself continues through addEventListener below.
    public object? oninput { get; set; }

    internal Control? FindControlByName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return null;
        }

        var root = GetDocumentRoot();
        if (root is null)
        {
            return null;
        }

        foreach (var control in TraverseDocument(root))
        {
            if (string.Equals((control as StyledElement)?.Name, name, StringComparison.Ordinal))
            {
                return control;
            }
        }

        return null;
    }

    public virtual object? getElementById(string id)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            return null;
        }

        EnsureElementIdIndex();
        if (_elementsById is null || !_elementsById.TryGetValue(id, out var candidates))
        {
            RecordSelectorMiss("document-id", id);
            return null;
        }

        for (var index = candidates.Count - 1; index >= 0; index--)
        {
            var candidate = candidates[index];
            if (!string.Equals(candidate.id, id, StringComparison.Ordinal)
                || !IsInDocumentTree(candidate.Control))
            {
                candidates.RemoveAt(index);
            }
        }

        if (candidates.Count == 1)
        {
            return candidates[0];
        }

        if (candidates.Count > 1 && GetDocumentRoot() is { } root)
        {
            var candidateSet = new HashSet<AvaloniaDomElement>(candidates, ReferenceEqualityComparer.Instance);
            foreach (var control in TraverseDocument(root))
            {
                var wrapped = WrapControl(control);
                if (candidateSet.Contains(wrapped))
                {
                    return wrapped;
                }
            }
        }

        if (candidates.Count == 0)
        {
            _elementsById.Remove(id);
        }
        RecordSelectorMiss("document-id", id);
        return null;
    }

    public virtual object? resolveNamedProperty(string name)
    {
        if (string.IsNullOrEmpty(name)) return null;
        foreach (var element in EnumerateDocumentElements())
        {
            if ((string.Equals(element.id, name, StringComparison.Ordinal)
                 || string.Equals(element.name, name, StringComparison.Ordinal))
                && element.localName is "embed" or "form" or "iframe" or "img" or "object")
            {
                return element;
            }
        }
        return null;
    }

    private void EnsureElementIdIndex()
    {
        if (_elementsById is not null)
        {
            return;
        }

        _elementsById = new Dictionary<string, List<AvaloniaDomElement>>(StringComparer.Ordinal);
        _elementIdIndexBuildCount++;
        if (GetDocumentRoot() is not { } root)
        {
            return;
        }

        foreach (var control in TraverseDocument(root))
        {
            AddElementToIdIndex(WrapControl(control));
        }
    }

    private void AddElementToIdIndex(AvaloniaDomElement element)
    {
        if (_elementsById is null || string.IsNullOrEmpty(element.id))
        {
            return;
        }

        if (!_elementsById.TryGetValue(element.id, out var candidates))
        {
            candidates = new List<AvaloniaDomElement>();
            _elementsById[element.id] = candidates;
        }
        if (!candidates.Contains(element, ReferenceEqualityComparer.Instance))
        {
            candidates.Add(element);
        }
    }

    private void RemoveElementFromIdIndex(AvaloniaDomElement element, string? id = null)
    {
        if (_elementsById is null || string.IsNullOrEmpty(id ?? element.id)
            || !_elementsById.TryGetValue(id ?? element.id, out var candidates))
        {
            return;
        }

        candidates.RemoveAll(candidate => ReferenceEquals(candidate, element));
        if (candidates.Count == 0)
        {
            _elementsById.Remove(id ?? element.id);
        }
    }

    private void AddSubtreeToIdIndex(AvaloniaDomElement element)
    {
        foreach (var control in TraverseDocument(element.Control))
        {
            AddElementToIdIndex(WrapControl(control));
        }
    }

    private void RemoveSubtreeFromIdIndex(AvaloniaDomElement element)
    {
        foreach (var control in TraverseDocument(element.Control))
        {
            RemoveElementFromIdIndex(WrapControl(control));
        }
    }

    private bool IsInDocumentTree(Control control)
    {
        var root = GetDocumentRoot();
        for (Control? current = control; current is not null; current = current.Parent as Control)
        {
            if (ReferenceEquals(current, root))
            {
                return true;
            }
        }
        return false;
    }

    public virtual object? querySelector(string selector)
    {
        if (string.IsNullOrWhiteSpace(selector))
        {
            return null;
        }

        var special = QuerySpecialSelector(selector);
        if (special is not null)
        {
            return special;
        }

        var root = GetDocumentRoot();
        if (root is not null)
        {
            foreach (var control in TraverseDocument(root))
            {
                if (MatchesSelector(control, selector))
                {
                    return WrapControl(control);
                }
            }
        }

        RecordSelectorMiss("document", selector);
        return null;
    }

    public bool __htmlMlIsValidSelector(string selector)
        => CssSelectorSyntaxParser.IsSupportedDomSelectorList(selector);

    public virtual object[] querySelectorAll(string selector)
    {
        if (string.IsNullOrWhiteSpace(selector))
        {
            return Array.Empty<object>();
        }

        var special = QuerySpecialSelector(selector);
        if (special is not null)
        {
            return new[] { special };
        }

        var root = GetDocumentRoot();
        if (root is null)
        {
            return Array.Empty<object>();
        }

        var list = new List<object>();
        foreach (var control in TraverseDocument(root))
        {
            if (MatchesSelector(control, selector))
            {
                list.Add(WrapControl(control));
            }
        }

        return list.ToArray();
    }

    public virtual object? elementFromPoint(double x, double y)
        => elementsFromPoint(x, y).FirstOrDefault();

    public virtual object[] elementsFromPoint(double x, double y)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y))
        {
            return Array.Empty<object>();
        }

        EnsureStylesCurrent();
        var point = new Point(x, y);
        return EnumerateDocumentElements()
            .Select((element, index) => new
            {
                Element = element,
                Index = index,
                ZIndexPath = GetDocumentZIndexPath(element),
                Rect = element.getBoundingClientRect(),
                ContainsPoint = element.ContainsViewportPoint(point)
            })
            .Where(candidate => candidate.Element.isConnected
                                && candidate.Element.Control.IsVisible
                                && candidate.Element.Control.IsHitTestVisible
                                && !candidate.Element.HasHiddenComputedVisibility()
                                && !CssLayout.GetPointerEventsNone(candidate.Element.Control)
                                && candidate.Rect.width > 0
                                && candidate.Rect.height > 0
                                && candidate.ContainsPoint)
            .OrderByDescending(
                candidate => candidate.ZIndexPath,
                Comparer<int[]>.Create(CompareZIndexPaths))
            .ThenByDescending(candidate => candidate.Index)
            .Select(candidate => (object)candidate.Element)
            .ToArray();
    }

    private static int[] GetDocumentZIndexPath(AvaloniaDomElement element)
    {
        var path = new List<int>();
        for (var current = element; current is not null; current = current.parentElement)
        {
            if (int.TryParse(
                current.ComputedStyleValues.GetValueOrDefault("z-index"),
                NumberStyles.Integer,
                CultureInfo.InvariantCulture,
                out var computed))
                path.Add(computed);
        }
        path.Reverse();
        return path.ToArray();
    }

    private static int CompareZIndexPaths(int[]? left, int[]? right)
    {
        left ??= [];
        right ??= [];
        var length = Math.Max(left.Length, right.Length);
        for (var index = 0; index < length; index++)
        {
            var leftValue = index < left.Length ? left[index] : 0;
            var rightValue = index < right.Length ? right[index] : 0;
            var comparison = leftValue.CompareTo(rightValue);
            if (comparison != 0) return comparison;
        }
        return 0;
    }

    public virtual bool hasFocus() => true;

    public virtual bool hidden => false;

    public virtual string visibilityState => "visible";

    public virtual object? createElement(string tag)
    {
        var allocationStart = Host.CollectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var control = CreateControl(tag);
        if (control is null)
        {
            return null;
        }

        var wrapper = WrapControl(control);
        if (!string.IsNullOrWhiteSpace(tag))
        {
            wrapper.SetNodeNameOverride(tag.Trim());
        }

        if (Host.CollectPerformanceMetrics)
        {
            _domNodeCreationAllocatedBytes += GC.GetAllocatedBytesForCurrentThread() - allocationStart;
        }

        return wrapper;
    }

    public virtual object? createElementNS(string? namespaceUri, string qualifiedName)
    {
        var element = createElement(qualifiedName) as AvaloniaDomElement;
        element?.SetNamespaceUri(namespaceUri);
        return element;
    }

    public virtual object? createTextNode(string data)
    {
        var allocationStart = Host.CollectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var textBlock = CreateTextNodeControl(data ?? string.Empty);
        if (_elementWrappers.TryGetValue(textBlock, out var existing))
        {
            return existing;
        }

        var node = new AvaloniaDomTextNode(Host, this, textBlock);
        _elementWrappers.Add(textBlock, node);
        if (Host.CollectPerformanceMetrics)
        {
            _domNodeCreationAllocatedBytes += GC.GetAllocatedBytesForCurrentThread() - allocationStart;
        }
        return node;
    }

    public virtual DomAttribute createAttribute(string name)
        => new(name ?? string.Empty, string.Empty, this);

    public virtual DomComment createComment(string data)
        => new(data ?? string.Empty, this);

    public virtual object? createDocumentFragment()
    {
        var container = createElement("div") as AvaloniaDomElement;
        return container is null ? null : new DomDocumentFragment(container);
    }

    public virtual object createRange()
    {
        return new DomRange(this);
    }

    /// <summary>
    /// Starts the bounded HTML document replacement lifecycle used by a
    /// same-origin iframe's initial about:blank document.
    /// </summary>
    public virtual AvaloniaDomDocument open(params object?[] arguments)
    {
        _documentWriteBuffer = new StringBuilder();
        _readyStateScheduled = false;
        _readyStateCompletionCallbacks.Clear();
        SetReadyState("loading");

        foreach (var child in _head.childNodes)
        {
            _head.removeChild(child);
        }

        if (body is AvaloniaDomElement bodyElement)
        {
            bodyElement.innerHTML = string.Empty;
        }

        return this;
    }

    public virtual void write(params object?[] text)
    {
        _documentWriteBuffer ??= new StringBuilder();
        foreach (var value in text)
        {
            _documentWriteBuffer.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
        }
    }

    public virtual void writeln(params object?[] text)
    {
        write(text);
        _documentWriteBuffer!.Append('\n');
    }

    public virtual void close()
    {
        if (_documentWriteBuffer is null)
        {
            return;
        }

        var markup = _documentWriteBuffer.ToString();
        _documentWriteBuffer = null;
        var parser = new AngleSharp.Html.Parser.HtmlParser();
        var parsed = parser.ParseDocument(markup);
        if (body is AvaloniaDomElement bodyElement)
        {
            foreach (var attribute in bodyElement.attributes)
            {
                bodyElement.removeAttribute(attribute.name);
            }
            if (parsed.Body is not null)
            {
                foreach (var attribute in parsed.Body.Attributes)
                {
                    bodyElement.setAttribute(attribute.Name, attribute.Value);
                }
                bodyElement.innerHTML = parsed.Body.InnerHtml;
            }
        }

        if (parsed.Head is not null && body is AvaloniaDomElement nodeFactory)
        {
            foreach (var source in parsed.Head.ChildNodes)
            {
                var child = nodeFactory.CreateDomNodeFromAngleSharp(source);
                if (child is not null)
                {
                    _head.appendChild(child);
                }
            }
        }

        RestoreViewportLayoutContract();

        // Document content is observable immediately after close(); lifecycle
        // events complete on the next host task, after the caller unwinds.
        ScheduleReadyStateCompletion();
    }

    /// <summary>
    /// Parses markup for an external engine's DOMParser facade.
    /// </summary>
    public object ParseMarkupDocument(string markup, string mimeType)
    {
        if (string.Equals(mimeType, "text/html", StringComparison.OrdinalIgnoreCase))
        {
            var html = createElement("html") as AvaloniaDomElement
                       ?? throw new InvalidOperationException("Unable to create a detached parser root.");
            var body = createElement("body") as AvaloniaDomElement
                       ?? throw new InvalidOperationException("Unable to create a detached parser body.");
            html.appendChild(body);
            body.innerHTML = markup ?? string.Empty;
            return new DomParsedDocument(this, html, body);
        }
        var parsed = XDocument.Parse(markup ?? string.Empty, LoadOptions.PreserveWhitespace);
        var documentElement = parsed.Root is null ? null : CreateXmlElement(parsed.Root);
        return new DomParsedDocument(this, documentElement, xmlMode: true);
    }

    internal AvaloniaDomElement CreateXmlElement(string qualifiedName, string? namespaceUri = null)
    {
        var element = createElement(qualifiedName) as AvaloniaDomElement
                      ?? throw new InvalidOperationException($"Unable to create XML element '{qualifiedName}'.");
        element.SetXmlMode(namespaceUri);
        return element;
    }

    private AvaloniaDomElement CreateXmlElement(XElement source)
    {
        var element = CreateXmlElement(source.Name.LocalName, source.Name.NamespaceName);
        foreach (var attribute in source.Attributes())
        {
            element.setAttribute(attribute.Name.LocalName, attribute.Value);
        }
        foreach (var child in source.Nodes())
        {
            switch (child)
            {
                case XElement childElement:
                    element.appendChild(CreateXmlElement(childElement));
                    break;
                case XText text:
                    element.appendChild((AvaloniaDomElement)createTextNode(text.Value)!);
                    break;
            }
        }
        return element;
    }

    public virtual object? body
    {
        get
        {
            var root = GetDocumentBodyRoot();
            if (root is null)
            {
                return null;
            }

            // Traverse down past single-child decorators/content controls to find the actual layout container
            bool progressed = true;
            while (progressed)
            {
                progressed = false;
                if (root is Decorator decorator && decorator.Child is not null)
                {
                    root = decorator.Child;
                    progressed = true;
                }
                else if (root is ContentControl cc && cc.Content is Control childControl)
                {
                    root = childControl;
                    progressed = true;
                }
            }

            var wrapper = WrapControl(root);
            wrapper.SetNodeNameOverride("BODY");
            wrapper.SetDomParent(_documentElement);
            return wrapper;
        }
    }

    public virtual int nodeType => 9;

    public virtual string nodeName => "#document";

    public virtual object[] childNodes => new object[] { documentElement };

    public virtual object[] children => new object[] { documentElement };

    public virtual object? firstChild => documentElement;

    public virtual object? lastChild => documentElement;

    public virtual bool hasChildNodes => true;

    public virtual object? activeElement
    {
        get
        {
            // Avalonia installs the new native focus before GotFocus is raised.
            // While the old target's blur/focusout handlers run, HTML's focus
            // update steps still expose the viewport/body as active. Keep the
            // DOM transition authoritative for that synchronous event window.
            if (_focusTransitionInProgress)
            {
                return _domActiveElement ?? body;
            }

            if (_virtualActiveElement is not null)
            {
                return _virtualActiveElement;
            }

            var focused = GetActualActiveElement()?.Control;
            if (focused is null)
            {
                return _domActiveElement is { isConnected: true }
                    ? _domActiveElement
                    : body;
            }

            return WrapControl(focused);
        }
    }

    public DomHeadElement head => _head;

    public DomDocumentElement documentElement => _documentElement;

    public DomDocumentElement scrollingElement => _documentElement;

    public object? currentScript => Host.CurrentScript;

    public object? defaultView => ExternalWindowContext;

    public DomDocumentImplementation implementation => _implementation ??= new(this);

    public virtual string? title
    {
        get => Host.TopLevel is Window window ? window.Title : null;
        set
        {
            if (Host.TopLevel is Window window)
            {
                window.Title = value;
            }
        }
    }

    public virtual string baseURI
    {
        get
        {
            var baseHref = _styleEngine.BaseHref;
            if (string.IsNullOrWhiteSpace(baseHref))
            {
                return location.href;
            }

            if (Uri.TryCreate(baseHref, UriKind.Absolute, out var absolute))
            {
                return absolute.ToString();
            }

            return Uri.TryCreate(location.href, UriKind.Absolute, out var documentUri)
                ? new Uri(documentUri, baseHref).ToString()
                : baseHref;
        }
    }

    public AvaloniaBrowserHost.LocationJs location => _browsingContextLocation ?? Host.Location;

    internal void SetBrowsingContextLocation(AvaloniaBrowserHost.LocationJs location)
        => _browsingContextLocation = location;

    public string cookie
    {
        get => string.Join("; ", _cookies.Select(static pair => $"{pair.Key}={pair.Value}"));
        set
        {
            var assignment = value?.Split(';', 2)[0] ?? string.Empty;
            var separator = assignment.IndexOf('=');
            if (separator <= 0)
            {
                return;
            }

            var name = assignment[..separator].Trim();
            if (name.Length == 0)
            {
                return;
            }

            _cookies[name] = assignment[(separator + 1)..].Trim();
        }
    }

    public virtual CssComputedStyle getComputedStyle(object? element)
    {
        if (ReferenceEquals(element, documentElement))
        {
            EnsureStylesCurrent();
            FlushPendingLayout();
            return _styleEngine.GetDocumentElementComputedStyle();
        }

        if (element is AvaloniaDomElement domElement)
        {
            EnsureStylesCurrent();
            // Resolved width/height and other used values are layout-dependent.
            // CSSOM reads are synchronous layout boundaries just like
            // getBoundingClientRect/offset*; otherwise same-task stylesheet and
            // DOM mutations expose stale zero-sized computed boxes.
            FlushPendingLayout();
            return domElement.ComputeComputedStyle(_computedStyleSnapshotGeneration);
        }

        return CssComputedStyle.Empty;
    }

    internal IEnumerable<object> HeadChildren => _head.childNodes;

    internal IEnumerable<AvaloniaDomElement> StylesheetNodes
    {
        get
        {
            foreach (var node in HeadChildren.OfType<AvaloniaDomElement>())
            {
                yield return node;
            }

            if (body is not AvaloniaDomElement bodyElement)
            {
                yield break;
            }

            foreach (var control in TraverseDocument(bodyElement.Control))
            {
                var element = WrapControl(control);
                if (IsStylesheetElement(element))
                {
                    yield return element;
                }
            }
        }
    }

    internal IReadOnlyList<HtmlMlResourceTimelineEntry> ResourceTimeline => _resourceTimeline;

    internal void RecordResourceTimeline(string kind, string resourceType, string? source, string outcome = "")
    {
        const int maximumEntries = 2048;
        if (_resourceTimeline.Count >= maximumEntries)
        {
            return;
        }

        _resourceTimeline.Add(new HtmlMlResourceTimelineEntry
        {
            TimeMilliseconds = Host.GetTimestamp(),
            Kind = kind,
            ResourceType = resourceType,
            Source = source ?? string.Empty,
            Outcome = outcome
        });
    }

    internal string LoadTextResource(string href, string? baseHref)
        => Host.LoadTextResource(href, baseHref);

    internal HtmlML.Core.HtmlMlTextResource LoadTextResourceDetails(string href, string? baseHref)
        => Host.LoadTextResourceDetails(href, baseHref);

    internal Task<AvaloniaBinaryResource> LoadBinaryResourceAsync(
        string href,
        string? baseHref,
        CancellationToken cancellationToken)
        => Host.LoadBinaryResourceAsync(href, baseHref, cancellationToken);

    internal CssFontFaceRegistry FontFaces => _fontFaces;

    internal void PostFontFaceCompletion(Action completion)
        => Host.Services.Dispatcher.Post(completion, HtmlMlDispatchPriority.Send);

    internal void NotifyFontFacesChanged()
    {
        _forceElementPresentationApply = true;
        _styleEngine.Invalidate();
        // Avalonia may not resolve a newly registered collection while an
        // invisible top-level is being measured from the same dispatcher
        // drain that installed it. Keep the document dirty and let the next
        // explicit geometry/style boundary flush it. Visible hosts retain the
        // normal automatic reflow.
        ScheduleLayoutUpdate();
        InvalidateLayoutFromStyleMutation();
    }

    internal void DisposeFontFaces() => _fontFaces.Dispose();

    internal void EnsureStylesCurrent()
    {
        _styleEngine.EnsureCurrent();
        RefreshDirtyListMarkerCounters();
    }

    private void RefreshDirtyListMarkerCounters()
    {
        if (!_listMarkerCountersDirty) return;
        _listMarkerCountersDirty = false;
        foreach (var element in EnumerateStyleElements())
        {
            element.RefreshListMarkerPresentation();
        }
    }

    internal int StyleRuleCount => _styleEngine.RuleCount;

    internal int StylesheetParseCount => _styleEngine.StylesheetParseCount;

    internal long CompiledStylesheetCacheHitCount => _styleEngine.CompiledStylesheetCacheHitCount;

    internal int CompiledStylesheetCacheEntryCount => CssStyleEngine.CompiledStylesheetCacheEntryCount;

    internal long MediaQueryOutcomeCacheHitCount => _styleEngine.MediaQueryOutcomeCacheHitCount;

    internal long ViewportPresentationReapplyElementCount =>
        _styleEngine.ViewportPresentationReapplyElementCount;

    internal int StyleRecomputeCount => _styleEngine.StyleRecomputeCount;

    internal long ElementStyleComputeCount => _styleEngine.ElementStyleComputeCount;

    internal long ElementStyleApplyCount => _styleEngine.ElementStyleApplyCount;

    internal long SharedOrdinaryStyleHitCount => _styleEngine.SharedOrdinaryStyleHitCount;

    internal int SharedOrdinaryStyleEntryCount => _styleEngine.SharedOrdinaryStyleEntryCount;

    internal long CascadeTemplateHitCount => _styleEngine.CascadeTemplateHitCount;

    internal int CascadeTemplateEntryCount => _styleEngine.CascadeTemplateEntryCount;

    internal long ElementPresentationApplyCount { get; private set; }

    internal long InlinePresentationApplyCount { get; private set; }

    internal long SelectorMatchEvaluationCount => _styleEngine.SelectorMatchEvaluationCount;

    internal long MatchedRuleCacheHitCount => _styleEngine.MatchedRuleCacheHitCount;

    internal long ScopedClassInvalidationCount => _styleEngine.ScopedClassInvalidationCount;

    internal long ClassInvalidationFallbackCount => _styleEngine.ClassInvalidationFallbackCount;

    internal long ClassInvalidationPropagationCount => _styleEngine.ClassInvalidationPropagationCount;

    internal long InheritedCursorRebaseElementCount => _styleEngine.InheritedCursorRebaseElementCount;

    internal long InheritedPropagationPrunedElementCount => _styleEngine.InheritedPropagationPrunedElementCount;

    internal long AppendStylesheetCandidateEvaluationCount =>
        _styleEngine.AppendStylesheetCandidateEvaluationCount;

    internal bool InheritedCursorRebaseEnabled => Host.EnableInheritedCursorRebase;

    internal bool IndexedAppendStylesheetMatchingEnabled =>
        Host.EnableIndexedAppendStylesheetMatching;

    internal TimeSpan StyleEnsureDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.EnsureCurrentTicks / Stopwatch.Frequency);

    internal long StyleEnsureAllocatedBytes => _styleEngine.EnsureCurrentAllocatedBytes;

    internal TimeSpan StylesheetNormalizationDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.StylesheetNormalizationTicks / Stopwatch.Frequency);

    internal long StylesheetNormalizationAllocatedBytes => _styleEngine.StylesheetNormalizationAllocatedBytes;

    internal TimeSpan StylesheetParserDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.StylesheetParserTicks / Stopwatch.Frequency);

    internal long StylesheetParserAllocatedBytes => _styleEngine.StylesheetParserAllocatedBytes;

    internal TimeSpan StylesheetRuleCompilationDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.StylesheetRuleCompilationTicks / Stopwatch.Frequency);

    internal long StylesheetRuleCompilationAllocatedBytes => _styleEngine.StylesheetRuleCompilationAllocatedBytes;

    internal TimeSpan StylesheetIndexingDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.StylesheetIndexingTicks / Stopwatch.Frequency);

    internal long StylesheetIndexingAllocatedBytes => _styleEngine.StylesheetIndexingAllocatedBytes;

    internal TimeSpan ElementStyleCascadeDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.ElementStyleCascadeTicks / Stopwatch.Frequency);

    internal long ElementStyleCascadeAllocatedBytes => _styleEngine.ElementStyleCascadeAllocatedBytes;

    internal TimeSpan ElementStyleRuleMatchDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.ElementStyleRuleMatchTicks / Stopwatch.Frequency);

    internal long ElementStyleRuleMatchAllocatedBytes => _styleEngine.ElementStyleRuleMatchAllocatedBytes;

    internal TimeSpan ElementStyleValueInitializationDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.ElementStyleValueInitializationTicks / Stopwatch.Frequency);

    internal long ElementStyleValueInitializationAllocatedBytes =>
        _styleEngine.ElementStyleValueInitializationAllocatedBytes;

    internal TimeSpan ElementStyleResolutionDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.ElementStyleResolutionTicks / Stopwatch.Frequency);

    internal long ElementStyleResolutionAllocatedBytes => _styleEngine.ElementStyleResolutionAllocatedBytes;

    internal TimeSpan ElementStyleCommitDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.ElementStyleCommitTicks / Stopwatch.Frequency);

    internal long ElementStyleCommitAllocatedBytes => _styleEngine.ElementStyleCommitAllocatedBytes;

    internal TimeSpan PseudoElementDuration =>
        TimeSpan.FromSeconds((double)_styleEngine.PseudoElementTicks / Stopwatch.Frequency);

    internal long PseudoElementAllocatedBytes => _styleEngine.PseudoElementAllocatedBytes;

    internal void RecordElementPresentationApply() => ElementPresentationApplyCount++;

    internal void RecordInlinePresentationApply() => InlinePresentationApplyCount++;

    internal long LayoutFlushRequestCount => _layoutFlushRequestCount;

    internal long LayoutFlushFastPathCount => _layoutFlushFastPathCount;

    internal long LayoutFlushCount => _layoutFlushCount;

    internal TimeSpan LayoutFlushDuration =>
        TimeSpan.FromSeconds((double)_layoutFlushTicks / Stopwatch.Frequency);

    internal long LayoutFlushAllocatedBytes => _layoutFlushAllocatedBytes;

    internal long LayoutPassCount => _layoutPassCount;

    internal TimeSpan LayoutPassDuration =>
        TimeSpan.FromSeconds((double)_layoutPassTicks / Stopwatch.Frequency);

    internal long LayoutPassAllocatedBytes => _layoutPassAllocatedBytes;

    internal long ComputedStyleSnapshotHitCount => _computedStyleSnapshotHitCount;

    internal long ComputedStyleSnapshotBuildCount => _computedStyleSnapshotBuildCount;

    internal TimeSpan ComputedStyleSnapshotBuildDuration =>
        TimeSpan.FromSeconds((double)_computedStyleSnapshotBuildTicks / Stopwatch.Frequency);

    internal long ComputedStyleSnapshotBuildAllocatedBytes => _computedStyleSnapshotBuildAllocatedBytes;

    internal long ComputedStyleSnapshotStateReuseCount => _computedStyleSnapshotStateReuseCount;

    internal bool ComputedStyleSnapshotStateReuseEnabled =>
        Host.EnableComputedStyleSnapshotStateReuse;

    internal void RecordComputedStyleSnapshot(bool cacheHit)
    {
        if (cacheHit) _computedStyleSnapshotHitCount++;
        else _computedStyleSnapshotBuildCount++;
    }

    internal void RecordComputedStyleSnapshotStateReuse()
        => _computedStyleSnapshotStateReuseCount++;

    internal void InvalidateComputedStyleSnapshots()
    {
        unchecked
        {
            _computedStyleSnapshotGeneration++;
        }
    }

    internal void RecordComputedStyleSnapshotBuild(long started, long allocationStarted)
    {
        _computedStyleSnapshotBuildTicks += Stopwatch.GetTimestamp() - started;
        _computedStyleSnapshotBuildAllocatedBytes +=
            GC.GetAllocatedBytesForCurrentThread() - allocationStarted;
    }

    internal long DomNodeCreationAllocatedBytes => _domNodeCreationAllocatedBytes;

    internal long DomNodeInsertionCount => _domNodeInsertionCount;

    internal TimeSpan DomNodeInsertionDuration =>
        TimeSpan.FromSeconds((double)_domNodeInsertionTicks / Stopwatch.Frequency);

    internal long DomNodeInsertionAllocatedBytes => _domNodeInsertionAllocatedBytes;

    internal void RecordDomNodeInsertion(long started, long allocationStarted)
    {
        _domNodeInsertionCount++;
        _domNodeInsertionTicks += Stopwatch.GetTimestamp() - started;
        _domNodeInsertionAllocatedBytes +=
            GC.GetAllocatedBytesForCurrentThread() - allocationStarted;
    }

    internal long ChildListMutationAllocatedBytes => _childListMutationAllocatedBytes;

    internal int StylesheetNodeCount => StylesheetNodes
        .Count(element => string.Equals(element.tagName, "STYLE", StringComparison.OrdinalIgnoreCase)
                          || (string.Equals(element.tagName, "LINK", StringComparison.OrdinalIgnoreCase)
                              && string.Equals(element.getAttribute("rel"), "stylesheet", StringComparison.OrdinalIgnoreCase)));

    internal string[] StylesheetSources => StylesheetNodes
        .Where(element => string.Equals(element.tagName, "STYLE", StringComparison.OrdinalIgnoreCase)
                          || (string.Equals(element.tagName, "LINK", StringComparison.OrdinalIgnoreCase)
                              && string.Equals(element.getAttribute("rel"), "stylesheet", StringComparison.OrdinalIgnoreCase)))
        .Select(element => string.Equals(element.tagName, "STYLE", StringComparison.OrdinalIgnoreCase)
            ? "inline-style"
            : Path.GetFileName(element.getAttribute("href") ?? string.Empty))
        .ToArray();

    [ThreadStatic]
    private static bool s_isFlushingLayout;
    private static readonly bool s_disableLayoutFlushFastPath =
        string.Equals(
            Environment.GetEnvironmentVariable("HTMLML_DISABLE_LAYOUT_FLUSH_FAST_PATH"),
            "1",
            StringComparison.Ordinal);

    internal void FlushPendingLayout()
    {
        if (Host.IsDisposed)
        {
            return;
        }
        if (s_isFlushingLayout)
        {
            return;
        }

        var collectPerformance = CollectPerformanceMetrics;
        if (collectPerformance)
        {
            _layoutFlushRequestCount++;
        }

        // Geometry APIs are commonly read in width/height pairs and tight chart
        // layout loops. Once styles and Avalonia layout are current, repeating
        // the complete flush protocol cannot change the answer. Keep this check
        // ahead of profiling clocks as well so diagnostics do not turn thousands
        // of no-op browser layout checks into a synthetic hotspot.
        var documentRoot = GetDocumentRoot();
        var documentLayoutValid = documentRoot is null
                                  || (documentRoot.IsMeasureValid && documentRoot.IsArrangeValid);
        if (!s_disableLayoutFlushFastPath
            && !_styleEngine.HasPendingWork
            && Host.TopLevel.Content is Control currentContent
            && currentContent.IsMeasureValid
            && currentContent.IsArrangeValid
            && documentLayoutValid)
        {
            if (collectPerformance)
            {
                _layoutFlushFastPathCount++;
            }
            return;
        }

        // A native layout invalidation can occur without a DOM mutation (for
        // example when an ancestor viewport is arranged). Any cached CSSOM
        // snapshot must be rebuilt after this synchronous layout boundary.
        unchecked
        {
            _computedStyleSnapshotGeneration++;
        }

        var flushStarted = collectPerformance || HasUiThreadWorkBudget
            ? Stopwatch.GetTimestamp()
            : 0;
        var flushAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        if (collectPerformance)
        {
            _layoutFlushCount++;
        }
        s_isFlushingLayout = true;
        try
        {
            EnsureStylesCurrent();
            if (Host.TopLevel.Content is not Control content)
            {
                return;
            }

            documentLayoutValid = documentRoot is null
                                  || (documentRoot.IsMeasureValid && documentRoot.IsArrangeValid);
            if (content.IsMeasureValid && content.IsArrangeValid && documentLayoutValid)
            {
                return;
            }

            if (!documentLayoutValid)
            {
                // A nested browsing context can invalidate its own retained
                // layout without Avalonia propagating invalidity through an
                // otherwise size-stable outer document. Invalidating only the
                // top-level content is insufficient: its intermediate Canvas,
                // preview host, and iframe controls can remain valid and skip
                // the dirty frame subtree. Mark the complete visual ancestor
                // chain so synchronous DOM geometry reads observe the new box.
                for (Control? current = documentRoot; current is not null; current = current.GetVisualParent() as Control)
                {
                    current.InvalidateMeasure();
                    current.InvalidateArrange();
                    if (ReferenceEquals(current, content))
                    {
                        break;
                    }
                }
                content.InvalidateMeasure();
                content.InvalidateArrange();
            }

            var size = content.Bounds.Size;
            if (size.Width <= 0 || size.Height <= 0)
            {
                var width = double.IsFinite(content.Width) && content.Width > 0
                    ? content.Width
                    : Host.Services.Viewport.HostMetrics.ClientSize.Width;
                var height = double.IsFinite(content.Height) && content.Height > 0
                    ? content.Height
                    : Host.Services.Viewport.HostMetrics.ClientSize.Height;
                size = new Size(Math.Max(0, width), Math.Max(0, height));
            }

            if (size.Width > 0 && size.Height > 0)
            {
                var passStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
                var passAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
                if (collectPerformance)
                {
                    _layoutPassCount++;
                }
                try
                {
                    content.Measure(size);
                    content.Arrange(new Rect(size));
                }
                finally
                {
                    if (collectPerformance)
                    {
                        _layoutPassTicks += Stopwatch.GetTimestamp() - passStarted;
                        _layoutPassAllocatedBytes +=
                            GC.GetAllocatedBytesForCurrentThread() - passAllocationStarted;
                    }
                }
            }
        }
        finally
        {
            s_isFlushingLayout = false;
            var elapsedTicks = flushStarted == 0
                ? 0
                : Stopwatch.GetTimestamp() - flushStarted;
            if (collectPerformance)
            {
                _layoutFlushTicks += elapsedTicks;
                _layoutFlushAllocatedBytes +=
                    GC.GetAllocatedBytesForCurrentThread() - flushAllocationStarted;
            }
            if (elapsedTicks > 0)
            {
                RecordUiThreadWork(UiThreadWorkKind.Layout, elapsedTicks);
            }
        }
    }

    internal IEnumerable<AvaloniaDomElement> EnumerateStyleElements()
    {
        if (body is not AvaloniaDomElement bodyElement)
        {
            yield break;
        }

        foreach (var control in TraverseDocument(bodyElement.Control))
        {
            yield return WrapControl(control);
        }
    }

    internal IEnumerable<AvaloniaDomElement> EnumerateStyleElements(AvaloniaDomElement root)
    {
        foreach (var control in TraverseDocument(root.Control))
        {
            yield return WrapControl(control);
        }
    }

    internal bool IsConnectedStyleElement(AvaloniaDomElement element)
    {
        if (body is not AvaloniaDomElement bodyElement)
        {
            return false;
        }

        for (var current = element; current is not null; current = current.parentElement)
        {
            if (ReferenceEquals(current, bodyElement))
            {
                return true;
            }
        }

        return false;
    }

    internal IEnumerable<AvaloniaDomElement> EnumerateDocumentElements()
    {
        if (body is not AvaloniaDomElement bodyElement)
        {
            yield break;
        }

        foreach (var control in TraverseDocument(bodyElement.Control))
        {
            yield return WrapControl(control);
        }
    }

    public HtmlMlVisualSnapshot captureVisualSnapshot()
        => HtmlMlDiagnostics.Capture(this);

    public HtmlMlScreenshot captureScreenshot()
        => HtmlMlDiagnostics.CaptureScreenshot(this);

    internal void NotifyHeadChanged()
    {
        _styleEngine.Invalidate(stylesheetsChanged: true);
        // Stylesheet fetch/parse and load/error delivery are asynchronous in a
        // browser. Deferring also prevents link.onload from re-entering React
        // while it is still rendering the element that attached the link.
        ScheduleStylesheetUpdate();
    }

    internal void NotifyHeadNodeAttached(object node)
    {
        if (node is AvaloniaDomElement element
            && string.Equals(element.nodeName, "SCRIPT", StringComparison.OrdinalIgnoreCase))
        {
            RecordResourceTimeline("attached", "script", element.src);
            ScheduleScriptElement(element);
            return;
        }

        if (node is AvaloniaDomElement resourceElement)
        {
            RecordResourceTimeline("attached", resourceElement.localName, resourceElement.href ?? "inline-style");
        }

        NotifyHeadChanged();
    }

    private void ScheduleScriptElement(AvaloniaDomElement script)
    {
        if (!_scheduledScriptElements.Add(script))
        {
            return;
        }

        Host.Services.Dispatcher.Post(() =>
        {
            try
            {
                RecordResourceTimeline("execute", "script", script.src, "started");
                ExecuteInBrowsingContext(() =>
                {
                    ExecuteScriptElement(script);
                    if (Host.EnableDiagnosticLogging)
                    {
                        Console.WriteLine($"Script resource loaded: {script.src}; onload={script.onload is not null}");
                    }
                    RecordResourceTimeline("event", "script", script.src, "dispatched:load");
                    script.DispatchResourceEvent("load");
                });
                RecordResourceTimeline("execute", "script", script.src, "completed");
            }
            catch (Exception exception)
            {
                RecordResourceTimeline("execute", "script", script.src, "failed:" + exception.GetType().Name);
                if (Host.EnableDiagnosticLogging)
                {
                    Console.Error.WriteLine($"Script resource failed: {exception.Message}");
                }
                ExecuteInBrowsingContext(() =>
                {
                    RecordResourceTimeline("event", "script", script.src, "dispatched:error");
                    script.DispatchResourceEvent("error");
                });
            }
        }, HtmlMlDispatchPriority.Send);
    }

    internal void ScheduleResourceEvent(AvaloniaDomElement element, string eventName)
    {
        RecordResourceTimeline("event", element.localName, element.src ?? element.href, "scheduled:" + eventName);
        Host.Services.Dispatcher.Post(() =>
        {
            ExecuteInBrowsingContext(() => element.DispatchResourceEvent(eventName));
            RecordResourceTimeline("event", element.localName, element.src ?? element.href, "dispatched:" + eventName);
            if (Host.EnableDiagnosticLogging)
            {
                Console.WriteLine($"Resource event completed: {eventName} {element.src ?? element.href}; onload={element.onload is not null}");
            }
        }, HtmlMlDispatchPriority.Send);
    }

    private void ExecuteScriptElement(AvaloniaDomElement script)
    {
        var frameDocument = this as VirtualIframeDomDocument;
        var externalRuntime = frameDocument?.ExternalRuntime;
        var source = script.getAttribute("src");
        if (!string.IsNullOrWhiteSpace(source))
        {
            externalRuntime?.ExecuteClassicScript(ResolveIframeScriptSpecifier(source));
            return;
        }

        var code = script.textContent;
        if (!string.IsNullOrWhiteSpace(code))
        {
            externalRuntime?.ExecuteInlineClassicScript(code, script);
        }
    }

    private void ExecuteInBrowsingContext(Action action)
    {
        // This method is the task-entry boundary for asynchronously loaded
        // scripts and resource events in a virtual browsing context. Keep one
        // outer scope alive through context restoration so nested DOM calls do
        // not flush promise jobs while their caller is still running.
        using var jsScope = new AvaloniaBrowserHost.JsCallScope(Host);
        if (this is VirtualIframeDomDocument { ExternalRuntime: { } externalRuntime })
        {
            action();
            externalRuntime.ProcessPendingTasks();
            Host.ProcessPendingTasks();
            return;
        }
        action();
        Host.ProcessPendingTasks();
    }

    private string ResolveIframeScriptSpecifier(string source)
    {
        var normalized = source.Trim();
        if (AvaloniaBrowserHost.UrlJs.TryGetObjectUrlRelativePath(normalized, out var objectRelativePath))
        {
            normalized = objectRelativePath;
        }
        var baseHref = _styleEngine.BaseHref;
        if (!string.IsNullOrWhiteSpace(baseHref)
            && !Uri.TryCreate(normalized, UriKind.Absolute, out _)
            && !normalized.StartsWith("/", StringComparison.Ordinal))
        {
            if (Uri.TryCreate(baseHref, UriKind.Absolute, out var absoluteBase))
            {
                normalized = new Uri(absoluteBase, normalized).ToString();
            }
            else
            {
                normalized = baseHref.TrimEnd('/') + "/" + normalized.TrimStart('.', '/');
            }
        }

        if (Uri.TryCreate(normalized, UriKind.Absolute, out var absolute))
        {
            normalized = Uri.UnescapeDataString(absolute.AbsolutePath);
        }

        normalized = normalized.Split('?', '#')[0].Replace('\\', '/');
        var scriptsIndex = normalized.IndexOf("/Scripts/", StringComparison.OrdinalIgnoreCase);
        if (scriptsIndex >= 0)
        {
            return "." + normalized[scriptsIndex..];
        }

        return normalized;
    }

    internal void NotifyDocumentStyleChanged()
        => _styleEngine.Invalidate();

    internal void ReconcileStylesAfterViewportResize()
    {
        _forceElementPresentationApply = true;
        // Active @media rules are viewport-dependent even when stylesheet text
        // itself is unchanged. The stylesheet engine compares their evaluated
        // outcomes at the final viewport size before deciding whether the
        // cascade graph needs to be rebuilt.
        _styleEngine.InvalidateViewportStyles();
        ScheduleLayoutUpdate();
        InvalidateLayoutFromStyleMutation();

        // Each iframe owns an independent cascade. A live viewport resize may
        // use scoped inline-style invalidation in every same-origin context;
        // reconcile all of those contexts once the viewport becomes quiet.
        foreach (var iframe in querySelectorAll("iframe").OfType<AvaloniaDomElement>())
        {
            iframe.GetContentDocument()?.ReconcileStylesAfterViewportResize();
        }
    }

    internal bool ForceElementPresentationApply => _forceElementPresentationApply;

    internal void CompleteViewportPresentationReconciliation()
        => _forceElementPresentationApply = false;

    internal void InvalidateLayoutFromStyleMutation()
    {
        if (Host.TopLevel.Content is Control content)
        {
            content.InvalidateMeasure();
            content.InvalidateArrange();
        }

    }

    internal void NotifyTextChanged(AvaloniaDomElement target)
    {
        var inHead = _head.Contains(target);
        var stylesheetsChanged = inHead || FindStylesheetAncestor(target) is not null;
        _styleEngine.Invalidate(target.parentElement ?? target, stylesheetsChanged: stylesheetsChanged);
        if (stylesheetsChanged) ScheduleStylesheetUpdate();
        else ScheduleLayoutUpdate();
    }

    private AvaloniaDomElement? FindStylesheetAncestor(AvaloniaDomElement target)
    {
        for (var current = target; current is not null; current = current.parentElement)
        {
            if (IsStylesheetElement(current))
            {
                return IsConnectedStyleElement(current) ? current : null;
            }
        }
        return null;
    }

    private void ScheduleStylesheetUpdate()
    {
        unchecked
        {
            _computedStyleSnapshotGeneration++;
        }
        if (_stylesheetUpdateScheduled)
        {
            return;
        }

        _stylesheetUpdateScheduled = true;
        Host.Services.Dispatcher.Post(() =>
        {
            _stylesheetUpdateScheduled = false;
            EnsureStylesCurrent();
            FlushPendingLayout();
        }, HtmlMlDispatchPriority.Send);
    }

    private void ScheduleLayoutUpdate()
    {
        unchecked
        {
            _computedStyleSnapshotGeneration++;
        }
        if (_layoutUpdateScheduled)
        {
            return;
        }

        _layoutUpdateScheduled = true;
        Host.Services.Dispatcher.Post(() =>
        {
            _layoutUpdateScheduled = false;
            FlushPendingLayout();
        }, HtmlMlDispatchPriority.Render);
    }

    public virtual bool contains(object? node)
    {
        node = UnwrapDomNode(node);
        if (ReferenceEquals(node, this)
            || ReferenceEquals(node, documentElement)
            || ReferenceEquals(node, head))
        {
            return true;
        }

        return node is AvaloniaDomElement element
               && ReferenceEquals(element.ownerDocument, this)
               && (IsConnectedStyleElement(element) || _head.Contains(element));
    }

    public virtual int compareDocumentPosition(object? other)
        => CompareDocumentPosition(this, other);

    internal static int CompareDocumentPosition(object referenceNode, object? otherNode)
    {
        otherNode = UnwrapDomNode(otherNode);
        if (ReferenceEquals(referenceNode, otherNode))
        {
            return 0;
        }

        if (otherNode is null)
        {
            return 1 | 32;
        }

        var referencePath = GetDocumentPositionPath(referenceNode);
        var otherPath = GetDocumentPositionPath(otherNode);
        if (referencePath.Count == 0
            || otherPath.Count == 0
            || !ReferenceEquals(referencePath[0], otherPath[0]))
        {
            var referenceOrder = RuntimeHelpers.GetHashCode(referenceNode);
            var otherOrder = RuntimeHelpers.GetHashCode(otherNode);
            return 1 | 32 | (otherOrder < referenceOrder ? 2 : 4);
        }

        var sharedLength = 0;
        var maximumSharedLength = Math.Min(referencePath.Count, otherPath.Count);
        while (sharedLength < maximumSharedLength
               && ReferenceEquals(referencePath[sharedLength], otherPath[sharedLength]))
        {
            sharedLength++;
        }

        if (sharedLength == otherPath.Count)
        {
            // The other node contains and precedes the reference node.
            return 2 | 8;
        }

        if (sharedLength == referencePath.Count)
        {
            // The other node is contained by and follows the reference node.
            return 4 | 16;
        }

        var parent = referencePath[sharedLength - 1];
        var referenceChild = referencePath[sharedLength];
        var otherChild = otherPath[sharedLength];
        foreach (var child in GetDocumentPositionChildren(parent))
        {
            if (ReferenceEquals(child, otherChild))
            {
                return 2;
            }

            if (ReferenceEquals(child, referenceChild))
            {
                return 4;
            }
        }

        return 1 | 32;
    }

    private static List<object> GetDocumentPositionPath(object node)
    {
        var path = new List<object>();
        var visited = new HashSet<object>(ReferenceEqualityComparer.Instance);
        object? current = node;
        while (current is not null && visited.Add(current))
        {
            path.Add(current);
            current = GetDocumentPositionParent(current);
        }

        path.Reverse();
        return path;
    }

    private static object? GetDocumentPositionParent(object node)
    {
        return node switch
        {
            AvaloniaDomDocument => null,
            DomDocumentElement documentElement => documentElement.ownerDocument,
            DomHeadElement head => head.parentElement,
            AvaloniaDomElement element when ReferenceEquals(element.ownerDocument.body, element)
                => element.ownerDocument.documentElement,
            AvaloniaDomElement element when element.parentNode is not null => element.parentNode,
            _ => null
        };
    }

    private static IEnumerable<object> GetDocumentPositionChildren(object node)
    {
        return node switch
        {
            AvaloniaDomDocument document => document.childNodes,
            DomDocumentElement documentElement => documentElement.childNodes,
            DomHeadElement head => head.childNodes,
            AvaloniaDomElement element => element.childNodes,
            _ => Array.Empty<object>()
        };
    }

    internal static object? UnwrapDomNode(object? node)
    {
        return node;
    }

    public DomMutationObserver __htmlMlCreateExternalMutationObserver(object callback)
    {
        var externalCallback = callback as IExternalJavaScriptCallback
                               ?? Host.ExternalCallbackAdapter?.GetCallback(callback, create: true)
                               ?? throw new ArgumentException(
                                   "The configured external JavaScript engine did not recognize the callback.",
                                   nameof(callback));
        return new DomMutationObserver(this, externalCallback);
    }

    internal void RegisterMutationObserver(DomMutationObserver observer)
    {
        if (!_mutationObservers.Contains(observer))
        {
            _mutationObservers.Add(observer);
        }
    }

    internal void UnregisterMutationObserver(DomMutationObserver observer)
    {
        _mutationObservers.Remove(observer);
    }

    public virtual object[] forms
        => GetElementCollection(static element => element.localName == "form");

    public virtual object[] images
        => GetElementCollection(static element => element.localName == "img");

    public virtual object[] links
        => GetElementCollection(static element =>
            element.localName is "a" or "area" && element.hasAttribute("href"));

    private static string DocumentNormalizeEventName(string? type)
        => string.IsNullOrWhiteSpace(type) ? string.Empty : type.Trim().ToLowerInvariant();

    public virtual void addEventListener(string type, object handler)
        => addEventListener(type, handler, options: null);

    public virtual void addEventListener(string type, object handler, object? options)
    {
        var adapter = ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: true);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        __htmlMlAddExternalEventListener(
            type,
            listener,
            parsed.Capture,
            parsed.Once,
            parsed.Passive);
    }


    public virtual void removeEventListener(string type, object handler)
        => removeEventListener(type, handler, options: null);

    public virtual void removeEventListener(string type, object handler, object? options)
    {
        var adapter = ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: false);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        __htmlMlRemoveExternalEventListener(type, listener, parsed.Capture);
    }


    public void __htmlMlAddExternalEventListener(
        string type,
        IExternalDomEventListener listener,
        bool capture,
        bool once,
        bool passive)
    {
        var normalized = DocumentNormalizeEventName(type);
        if (string.IsNullOrEmpty(normalized) || listener is null)
        {
            return;
        }

        var listeners = GetDocumentListeners(normalized, create: true)!;
        if (listeners.Any(item => item.Matches(listener, capture)))
        {
            return;
        }

        listeners.Add(new DomEventRegistration(
            listener,
            new EventListenerOptions(capture, once, passive)));
    }

    public void __htmlMlRemoveExternalEventListener(
        string type,
        IExternalDomEventListener listener,
        bool capture)
    {
        var normalized = DocumentNormalizeEventName(type);
        if (string.IsNullOrEmpty(normalized)
            || listener is null
            || !_documentEventListeners.TryGetValue(normalized, out var listeners))
        {
            return;
        }

        for (var index = 0; index < listeners.Count; index++)
        {
            if (listeners[index].Matches(listener, capture))
            {
                listeners.RemoveAt(index);
                break;
            }
        }

        if (listeners.Count == 0)
        {
            _documentEventListeners.Remove(normalized);
        }
    }

    private List<DomEventRegistration>? GetDocumentListeners(string type, bool create)
    {
        if (_documentEventListeners.TryGetValue(type, out var list))
        {
            return list;
        }

        if (!create)
        {
            return null;
        }

        list = new List<DomEventRegistration>();
        _documentEventListeners[type] = list;
        return list;
    }

    public virtual object[] getElementsByClassName(string className)
    {
        if (string.IsNullOrWhiteSpace(className))
        {
            return Array.Empty<object>();
        }

        var classes = className.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
        return GetElementCollection(element => classes.All(element.classList.contains));
    }

    public virtual object[] getElementsByTagName(string tagName)
    {
        if (string.IsNullOrWhiteSpace(tagName))
        {
            return Array.Empty<object>();
        }

        var special = QuerySpecialSelector(tagName);
        if (special is not null)
        {
            return new[] { special };
        }

        var normalized = tagName.Trim();
        return GetElementCollection(element =>
            normalized == "*"
            || string.Equals(element.localName, normalized, StringComparison.OrdinalIgnoreCase));
    }

    protected internal virtual AvaloniaDomElement WrapControl(Control control)
    {
        if (_elementWrappers.TryGetValue(control, out var existing))
        {
            return existing;
        }

        var created = control is Image image
            ? new AvaloniaDomImageElement(Host, this, image)
            : new AvaloniaDomElement(Host, this, control);

        _elementWrappers.Add(control, created);
        return created;
    }

    internal AvaloniaDomElement ResolvePointerEventTarget(
        Control? source,
        AvaloniaDomElement fallback,
        PointerEventArgs? pointerEvent = null)
    {
        var nativeTarget = fallback;
        for (var current = source; current is not null; current = current.Parent as Control)
        {
            if (_elementWrappers.TryGetValue(current, out var existing) && existing.HasExplicitDomTag)
            {
                nativeTarget = existing;
                break;
            }

            if (ReferenceEquals(current, fallback.Control))
                break;
        }

        var viewport = GetDocumentViewport();
        if (pointerEvent is not null && viewport is not null)
        {
            var point = pointerEvent.GetPosition(viewport);
            if (elementsFromPoint(point.X, point.Y).FirstOrDefault() is AvaloniaDomElement cssTarget)
            {
                // Correct native hierarchy trapping only when CSS places the
                // candidate in a strictly higher nested stacking level. For
                // equal levels the native hit is more precise (for example an
                // SVG path inside a toolbar button) and must remain the target.
                if (CompareZIndexPaths(
                        GetDocumentZIndexPath(cssTarget),
                        GetDocumentZIndexPath(nativeTarget)) > 0)
                    return cssTarget;
            }
        }

        return nativeTarget;
    }

    /// <summary>
    /// Phase 2 helper for dynamic resizing.
    /// Re-walks the tree and re-applies absolute positioning (including re-resolving % against current parent sizes)
    /// when the overall window or preview area size changes. This lets component autosize and internal
    /// absolute layout adapt.
    /// </summary>
    public void ReapplyAllPositionedLayout()
    {
        var started = Host.CollectPerformanceMetrics ? Stopwatch.GetTimestamp() : 0;
        try
        {
            // A TopLevel can host several isolated DOM documents (for example,
            // the Playground's 2x2 chart grid). Starting at the window would
            // make every host walk and mutate every sibling document on each
            // resize, turning four documents into sixteen full traversals.
            var documentViewport = GetDocumentViewport();
            var root = documentViewport is null ? null : WrapControl(documentViewport);
            if (root != null)
            {
                ReapplyPositionedRecursive(root);
            }

            foreach (var iframe in querySelectorAll("iframe").OfType<AvaloniaDomElement>())
            {
                if (iframe.GetContentDocument() is { } frameDocument)
                {
                    Host.Services.Dispatcher.Post(
                        frameDocument.RestoreViewportLayoutContract,
                        HtmlMlDispatchPriority.Render);
                }
            }
        }
        catch { }
        finally
        {
            if (started != 0)
            {
                Host.RecordPositionedLayoutReapply(Stopwatch.GetTimestamp() - started);
            }
        }
    }

    protected void ReapplyPositionedRecursive(AvaloniaDomElement element)
    {
        // CssLayout keeps percentages as percentages and resolves them against
        // the current containing block during every arrange pass. Reapplying
        // those unchanged declarations on viewport resize only invalidates the
        // tree again. Legacy Avalonia Canvas children are different: their CSS
        // percentages are materialized as pixel-valued attached properties and
        // must be resolved again when the parent size changes.
        if (DisableScopedPositionedLayoutReapply || element.Control.Parent is Canvas)
        {
            element.ApplyCanvasPositioning();
        }

        // A nested browsing context owns its visual document subtree. The
        // owner document must not walk through the iframe panel into those
        // controls; RestoreViewportLayoutContract handles that document after
        // its viewport has been arranged.
        if (!DisableScopedPositionedLayoutReapply
            && string.Equals(element.nodeName, "IFRAME", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (element.Control is Panel panel)
        {
            foreach (var childCtrl in panel.Children.OfType<Control>())
            {
                if (IsDomInfrastructureControl(childCtrl)) continue;
                var child = WrapControl(childCtrl);
                ReapplyPositionedRecursive(child);
            }
        }
        else if (element.Control is ContentControl cc && cc.Content is Control content)
        {
            ReapplyPositionedRecursive(WrapControl(content));
        }
        else if (element.Control is Decorator dec && dec.Child is Control child)
        {
            ReapplyPositionedRecursive(WrapControl(child));
        }
    }

    internal virtual void RestoreViewportLayoutContract()
    {
    }


    internal AvaloniaDomElement? GetActualActiveElement()
    {
        var focused = Host.TopLevel.FocusManager?.GetFocusedElement() as Control;
        return focused is null ? null : WrapControl(focused);
    }

    internal bool ActivateElement(AvaloniaDomElement element, bool dispatchFocusEvent, bool clearActualFocus)
    {
        if (clearActualFocus)
        {
            Host.TopLevel.FocusManager?.ClearFocus();
        }

        // Native GotFocus normally performed this transition synchronously.
        // A retained/virtual element has no native focus event, so keep it as
        // the keyboard target and run the same DOM transition here. Calling
        // TransitionFocus for both paths also covers backends whose Focus()
        // succeeds without raising GotFocus; an already-completed transition
        // is idempotent.
        _ = dispatchFocusEvent;
        _actualFocusNotificationVersion++;
        _virtualActiveElement = clearActualFocus ? element : null;
        TransitionFocus(element);

        return true;
    }

    internal void ReleaseElement(AvaloniaDomElement element, bool dispatchBlurEvent, bool clearActualFocus)
    {
        var wasActive = clearActualFocus
                        || ReferenceEquals(_virtualActiveElement, element)
                        || ReferenceEquals(_domActiveElement, element);

        if (clearActualFocus)
        {
            Host.TopLevel.FocusManager?.ClearFocus();
        }

        _ = dispatchBlurEvent;
        _actualFocusNotificationVersion++;
        if (ReferenceEquals(_virtualActiveElement, element))
        {
            _virtualActiveElement = null;
        }
        if (wasActive)
        {
            TransitionFocus(null);
        }
    }

    internal void NotifyActualFocus(AvaloniaDomElement element)
    {
        _actualFocusNotificationVersion++;
        _virtualActiveElement = null;
        TransitionFocus(element);
    }

    internal void NotifyActualBlur(AvaloniaDomElement element)
    {
        if (!ReferenceEquals(_domActiveElement, element))
        {
            return;
        }

        // Avalonia raises LostFocus before the sibling's GotFocus, so the new
        // relatedTarget is not available yet. Defer only the terminal
        // focus-to-nowhere case; a following GotFocus cancels this callback
        // and performs the complete A -> B transition synchronously.
        var notificationVersion = ++_actualFocusNotificationVersion;
        Host.Services.Dispatcher.Post(
            () => CompleteActualBlur(element, notificationVersion),
            HtmlMlDispatchPriority.Input);
    }

    private void CompleteActualBlur(AvaloniaDomElement element, long notificationVersion)
    {
        if (notificationVersion != _actualFocusNotificationVersion
            || !ReferenceEquals(_domActiveElement, element))
        {
            return;
        }

        var actual = GetActualActiveElement();
        if (actual is not null && !ReferenceEquals(actual, element))
        {
            NotifyActualFocus(actual);
            return;
        }

        TransitionFocus(null);
    }

    private void TransitionFocus(AvaloniaDomElement? next)
    {
        var previous = _domActiveElement;
        if (ReferenceEquals(previous, next))
        {
            return;
        }

        // Avalonia updates :focus and :focus-visible before GotFocus/LostFocus.
        // Queue the affected selector subjects before dispatching DOM focus
        // events so synchronous computed-style reads in those handlers observe
        // the new input modality, just as they do in a browser.
        if (previous is not null)
        {
            NotifyDynamicStateChanged(previous);
        }
        if (next is not null)
        {
            NotifyDynamicStateChanged(next);
        }

        var transitionVersion = ++_focusTransitionVersion;
        var sharesDocument = previous is null
                             || next is null
                             || SharesFocusDocument(previous, next);
        var relatedForPrevious = sharesDocument ? next : null;
        var relatedForNext = sharesDocument ? previous : null;
        _focusTransitionInProgress = true;
        try
        {
            if (previous is not null)
            {
                _domActiveElement = null;
                DispatchFocusStateEvent(previous, "blur", bubbles: false, relatedTarget: relatedForPrevious);
                if (transitionVersion != _focusTransitionVersion) return;

                DispatchFocusStateEvent(previous, "focusout", bubbles: true, relatedTarget: relatedForPrevious);
                if (transitionVersion != _focusTransitionVersion) return;
            }

            if (next is null)
            {
                return;
            }

            _domActiveElement = next;
            DispatchFocusStateEvent(next, "focus", bubbles: false, relatedTarget: relatedForNext);
            if (transitionVersion != _focusTransitionVersion) return;

            DispatchFocusStateEvent(next, "focusin", bubbles: true, relatedTarget: relatedForNext);
        }
        finally
        {
            if (transitionVersion == _focusTransitionVersion)
            {
                _focusTransitionInProgress = false;
            }
        }
    }

    private static bool SharesFocusDocument(AvaloniaDomElement left, AvaloniaDomElement right)
        => ReferenceEquals(
            GetFocusDocumentViewportRoot(left.Control),
            GetFocusDocumentViewportRoot(right.Control));

    private static Control GetFocusDocumentViewportRoot(Control control)
    {
        var outermost = control;
        for (var current = control; current is not null; current = current.Parent as Control)
        {
            outermost = current;
            if (CssLayout.GetDocumentViewportRoot(current))
            {
                return current;
            }
        }

        return outermost;
    }

    private void DispatchFocusStateEvent(
        AvaloniaDomElement target,
        string type,
        bool bubbles,
        AvaloniaDomElement? relatedTarget)
    {
        var evt = new DomFocusEvent(type, bubbles, Host.GetTimestamp(), relatedTarget);
        DispatchDomEventInternal(target, evt);
    }

    private void EnsureTopLevelInputHandlers()
    {
        if (_topLevelInputHandlersAttached)
        {
            return;
        }

        _topLevelInputHandlersAttached = true;
        Host.Services.Input.Keyboard += OnHostKeyboardInput;
        Host.Services.Input.TextInput += OnHostTextInput;
    }

    private void EnsureTopLevelPointerHandlers()
    {
        if (_topLevelPointerHandlersAttached)
        {
            return;
        }

        _topLevelPointerHandlersAttached = true;
        Host.Services.Input.Pointer += OnHostPointerInput;
    }

    private void OnHostPointerInput(object? sender, HtmlMlPointerInputEventArgs input)
    {
        if (!input.NativeEventHandle.TryGet<PointerEventArgs>(out var args) || args is null)
        {
            return;
        }

        switch (input.Kind)
        {
            case HtmlMlPointerEventKind.Pressed when args is PointerPressedEventArgs pressed:
                OnTopLevelPointerPressed(sender, pressed);
                break;
            case HtmlMlPointerEventKind.Moved:
                OnTopLevelPointerMoved(sender, args);
                break;
            case HtmlMlPointerEventKind.Released when args is PointerReleasedEventArgs released:
                OnTopLevelPointerReleased(sender, released);
                break;
            case HtmlMlPointerEventKind.Wheel when args is PointerWheelEventArgs wheel:
                OnTopLevelPointerWheelChanged(sender, wheel);
                break;
        }
    }

    private void OnHostKeyboardInput(object? sender, HtmlMlKeyboardInputEventArgs input)
    {
        if (!input.NativeEventHandle.TryGet<KeyEventArgs>(out var args) || args is null)
        {
            return;
        }

        if (string.Equals(input.Type, "keydown", StringComparison.Ordinal))
        {
            OnTopLevelKeyDown(sender, args);
        }
        else if (string.Equals(input.Type, "keyup", StringComparison.Ordinal))
        {
            OnTopLevelKeyUp(sender, args);
        }
    }

    private void OnHostTextInput(object? sender, HtmlMlTextInputEventArgs input)
    {
        if (input.NativeEventHandle.TryGet<TextInputEventArgs>(out var args) && args is not null)
        {
            OnTopLevelTextInput(sender, args);
        }
    }

    private void OnTopLevelPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (TryBeginPointerDispatch(e))
        {
            DispatchDocumentPointerEvent("pointerdown", e, bubbles: true, cancelable: true);
            DispatchDocumentPointerEvent("mousedown", e, bubbles: true, cancelable: true);
            if (IsSecondaryButtonPress(e, GetDocumentViewport()))
            {
                DispatchDocumentPointerEvent("contextmenu", e, bubbles: true, cancelable: true);
            }
        }
    }

    private void OnTopLevelPointerMoved(object? sender, PointerEventArgs e)
    {
        if (TryBeginPointerDispatch(e))
        {
            var document = GetActivePointerDocument(e.Pointer?.Id ?? 0) ?? this;
            document.DispatchActivePointerEvent("pointermove", e, bubbles: true, cancelable: false);
            document.DispatchActivePointerEvent("mousemove", e, bubbles: true, cancelable: false);
        }
    }

    private void OnTopLevelPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (TryBeginPointerDispatch(e))
        {
            var pointerId = e.Pointer?.Id ?? 0;
            var document = GetActivePointerDocument(pointerId) ?? this;
            document.DispatchActivePointerEvent("pointerup", e, bubbles: true, cancelable: true);
            document.DispatchActivePointerEvent("mouseup", e, bubbles: true, cancelable: true);
            if (IsAuxiliaryButton(e.InitialPressMouseButton))
            {
                document.DispatchActivePointerEvent("auxclick", e, bubbles: true, cancelable: true);
            }
            document.CompletePointer(pointerId);
        }
    }

    internal static bool IsSecondaryButtonPress(PointerPressedEventArgs args, Control? relativeTo)
    {
        if (relativeTo is null)
        {
            return false;
        }

        try
        {
            return args.GetCurrentPoint(relativeTo).Properties.PointerUpdateKind
                   == PointerUpdateKind.RightButtonPressed;
        }
        catch
        {
            return false;
        }
    }

    internal static bool IsAuxiliaryButton(MouseButton button)
        => button is MouseButton.Right
            or MouseButton.Middle
            or MouseButton.XButton1
            or MouseButton.XButton2;

    private void OnTopLevelPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (TryBeginPointerDispatch(e))
        {
            DispatchDocumentPointerEvent("wheel", e, bubbles: true, cancelable: true);
        }
    }

    internal static bool TryBeginPointerDispatch(RoutedEventArgs args)
    {
        var state = s_pointerDispatchStates.GetValue(args, static _ => new PointerDispatchState());
        if (state.Dispatched)
        {
            return false;
        }

        state.Dispatched = true;
        return true;
    }

    internal static bool TryBeginKeyboardDispatch(RoutedEventArgs args)
    {
        var state = s_keyboardDispatchStates.GetValue(args, static _ => new KeyboardDispatchState());
        if (state.Dispatched)
        {
            return false;
        }

        state.Dispatched = true;
        return true;
    }

    internal static bool TryBeginTextInputDispatch(RoutedEventArgs args)
    {
        var state = s_textInputDispatchStates.GetValue(args, static _ => new TextInputDispatchState());
        if (state.Dispatched)
        {
            return false;
        }

        state.Dispatched = true;
        return true;
    }

    private void DispatchDocumentPointerEvent(string type, PointerEventArgs args, bool bubbles, bool cancelable)
    {
        var relativeTo = GetDocumentViewport();
        if (relativeTo is null)
        {
            return;
        }

        var view = ExternalWindowContext;
        var evt = new DomPointerEvent(type, args, relativeTo, relativeTo, view, Host.GetTimestamp(), bubbles, cancelable)
        {
            target = this
        };
        DispatchDocumentEvent(evt);
        if (evt.bubbles && !evt.PropagationStopped)
        {
            DispatchWindowDomEvent(type, evt);
        }
    }

    private sealed class PointerDispatchState
    {
        public bool Dispatched { get; set; }
    }

    private sealed class KeyboardDispatchState
    {
        public bool Dispatched { get; set; }
    }

    private sealed class TextInputDispatchState
    {
        public bool Dispatched { get; set; }
    }

    private void OnTopLevelKeyDown(object? sender, KeyEventArgs e)
    {
        var target = GetVirtualInputTarget(e.Source);
        if (target is null || !TryBeginKeyboardDispatch(e))
        {
            return;
        }

        DispatchKeyboardEvent(target, "keydown", e, bubbles: true, cancelable: true);
    }

    private void OnTopLevelKeyUp(object? sender, KeyEventArgs e)
    {
        var target = GetVirtualInputTarget(e.Source);
        if (target is null || !TryBeginKeyboardDispatch(e))
        {
            return;
        }

        DispatchKeyboardEvent(target, "keyup", e, bubbles: true, cancelable: false);
    }

    private void OnTopLevelTextInput(object? sender, TextInputEventArgs e)
    {
        var target = GetVirtualInputTarget(e.Source);
        if (target is null || !TryBeginTextInputDispatch(e))
        {
            return;
        }

        DispatchTextInputEvent(target, "textinput", e, bubbles: true, cancelable: false);
        DispatchTextInputEvent(target, "input", e, bubbles: true, cancelable: false);
    }

    private AvaloniaDomElement? GetVirtualInputTarget(object? source)
    {
        if (_virtualActiveElement is null)
        {
            return null;
        }

        if (source is Control sourceControl && ReferenceEquals(sourceControl, _virtualActiveElement.Control))
        {
            return null;
        }

        return _virtualActiveElement;
    }

    private object[] GetCollection(Func<Control, bool> predicate) => QueryAll(predicate);

    private object[] GetElementCollection(Func<AvaloniaDomElement, bool> predicate)
    {
        var root = GetDocumentRoot();
        if (root is null)
        {
            return Array.Empty<object>();
        }

        var list = new List<object>();
        foreach (var control in TraverseDocument(root))
        {
            var element = WrapControl(control);
            if (predicate(element))
            {
                list.Add(element);
            }
        }

        return list.ToArray();
    }

    private object[] QueryAll(Func<Control, bool> predicate)
    {
        var root = GetDocumentRoot();
        if (root is null)
        {
            return Array.Empty<object>();
        }

        var list = new List<object>();
        foreach (var control in TraverseDocument(root))
        {
            if (predicate(control))
            {
                list.Add(WrapControl(control));
            }
        }

        return list.ToArray();
    }

    protected virtual bool IsFormControl(Control control)
    {
        var name = control.GetType().Name;
        return string.Equals(name, "Form", StringComparison.OrdinalIgnoreCase)
               || name.EndsWith("Form", StringComparison.OrdinalIgnoreCase);
    }

    protected virtual bool IsLinkControl(Control control)
    {
        if (control is HyperlinkButton)
        {
            return true;
        }

        var name = control.GetType().Name;
        return string.Equals(name, "Hyperlink", StringComparison.OrdinalIgnoreCase)
               || name.EndsWith("Link", StringComparison.OrdinalIgnoreCase);
    }

    protected virtual object? QuerySpecialSelector(string? selector)
    {
        var normalized = selector?.Trim().ToLowerInvariant();
        return normalized switch
        {
            "body" => body,
            "html" => documentElement,
            "head" => head,
            _ => null
        };
    }

    protected virtual Control? CreateControl(string tag)
    {
        if (_elementFactory is not null)
        {
            return _elementFactory(tag);
        }

        if (string.IsNullOrWhiteSpace(tag))
        {
            return null;
        }

        var typeName = ToTypeName(tag.Trim());
        if (typeName is null)
        {
            return null;
        }

        switch (tag.Trim().ToLowerInvariant())
        {
            case "canvas":
                // A DOM canvas owns its drawing surface. Rendering it as the
                // actual child preserves the same clipping, transforms, and
                // stacking context as the logical DOM element.
                return new CanvasDrawingSurface
                {
                    IsHitTestVisible = true,
                    Focusable = true
                };
            case "svg":
            case "g":
                return new SvgLayoutPanel { ClipToBounds = false };
            case "path":
                return new SvgPathControl { IsHitTestVisible = false, Focusable = false };
            case "circle":
                return new SvgCircleControl { IsHitTestVisible = false, Focusable = false };
            case "div":
            case "section":
            case "article":
            case "main":
            case "header":
            case "footer":
            case "nav":
            case "table":
            case "tbody":
            case "thead":
            case "tfoot":
            case "tr":
            case "td":
            case "th":
            case "col":
            case "colgroup":
            case "style":
            case "iframe":
                // An iframe establishes a clipped viewport for its independent
                // document tree. Its virtual body is mounted below on navigation.
                return new CssLayoutPanel { Background = Brushes.Transparent, ClipToBounds = true };
            case "span":
            case "label":
            case "p":
            case "h1":
            case "h2":
            case "h3":
            case "h4":
            case "h5":
            case "h6":
                // Inline DOM nodes frequently own nested SVG icons. A retained
                // panel preserves child elements while text nodes remain real
                // TextBlocks inside it. Paragraphs are containers too; mapping
                // an empty <p> directly to TextBlock creates a spurious line box.
                return new CssLayoutPanel { Background = Brushes.Transparent, ClipToBounds = false };
            case "br":
                return new DomLineBreakControl { Background = Brushes.Transparent, ClipToBounds = false };
            case "img":
                return new Image();
            case "input":
            case "textarea":
                return new DomTextInputControl();
            case "button":
                return new DomButtonControl { Background = Brushes.Transparent, ClipToBounds = false };
        }

        var assembly = typeof(Control).Assembly;
        var qualified = typeName.Contains('.') ? typeName : $"Avalonia.Controls.{typeName}";
        var type = assembly.GetType(qualified, throwOnError: false, ignoreCase: true)
                   ?? Type.GetType(qualified, throwOnError: false, ignoreCase: true);
        if (type is null)
        {
            // Unknown HTML/custom-element names are generic DOM containers,
            // not absolute-positioning canvases. Keeping them on the CSS
            // layout path is required when author styles assign internal
            // table roles such as table, table-row, or table-cell.
            return new CssLayoutPanel { Background = Brushes.Transparent, ClipToBounds = false };
        }

        if (!typeof(Control).IsAssignableFrom(type))
        {
            return new CssLayoutPanel { Background = Brushes.Transparent, ClipToBounds = false };
        }

        try
        {
            return (Control)Activator.CreateInstance(type)!;
        }
        catch
        {
            return new CssLayoutPanel { Background = Brushes.Transparent, ClipToBounds = false };
        }
    }

    private static TextBlock CreateTextNodeControl(string text)
    {
        return new DomTextBlockControl
        {
            Text = text,
            // CSS white-space defaults to normal, so an anonymous text box
            // must accept a finite containing-line width and wrap at spaces.
            // Explicit nowrap/pre presentation overrides this during cascade.
            TextWrapping = TextWrapping.Wrap,
            // Whitespace between block/flex markup nodes participates in the
            // DOM but does not create its own line box. Realizing it as a
            // standalone Avalonia TextBlock introduced a 12px row before the
            // widget and between every parsed element.
            IsVisible = !string.IsNullOrWhiteSpace(text),
            IsHitTestVisible = false,
            Focusable = false
        };
    }

    public static IEnumerable<Control> Traverse(Control root)
    {
        yield return root;

        switch (root)
        {
            case Panel panel:
                foreach (var child in panel.Children.OfType<Control>())
                {
                    if (IsDomInfrastructureControl(child)) continue;
                    foreach (var x in Traverse(child))
                    {
                        yield return x;
                    }
                }
                break;
            case ContentControl cc when cc.Content is Control content:
                foreach (var x in Traverse(content))
                {
                    yield return x;
                }
                break;
            case Decorator decorator when decorator.Child is Control child:
                foreach (var x in Traverse(child))
                {
                    yield return x;
                }
                break;
        }
    }

    internal static bool IsDomInfrastructureControl(Control control)
        => control is SvgSkiaSurface or IDomInfrastructureControl;

    private IEnumerable<Control> TraverseDocument(Control root)
        => s_disableIterativeDocumentTraversal
            ? TraverseDocumentRecursive(root)
            : TraverseDocumentIterative(root);

    private IEnumerable<Control> TraverseDocumentIterative(Control root)
    {
        var pending = new Stack<Control>();
        pending.Push(root);
        while (pending.Count > 0)
        {
            var current = pending.Pop();
            yield return current;

            // An iframe is a document boundary even though its native rendering
            // subtree is hosted below the iframe control in Avalonia's visual tree.
            if (!ReferenceEquals(current, GetDocumentRoot())
                && string.Equals(WrapControl(current).tagName, "IFRAME", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            switch (current)
            {
                case Panel panel:
                    for (var index = panel.Children.Count - 1; index >= 0; index--)
                    {
                        var child = panel.Children[index];
                        if (!IsDomInfrastructureControl(child))
                        {
                            pending.Push(child);
                        }
                    }
                    break;
                case ContentControl contentControl when contentControl.Content is Control content:
                    pending.Push(content);
                    break;
                case Decorator decorator when decorator.Child is Control child:
                    pending.Push(child);
                    break;
            }
        }
    }

    private IEnumerable<Control> TraverseDocumentRecursive(Control root)
    {
        yield return root;

        // An iframe is a document boundary even though its native rendering
        // subtree is hosted below the iframe control in Avalonia's visual tree.
        if (!ReferenceEquals(root, GetDocumentRoot())
            && string.Equals(WrapControl(root).tagName, "IFRAME", StringComparison.OrdinalIgnoreCase))
        {
            yield break;
        }

        IEnumerable<Control> children = root switch
        {
            Panel panel => panel.Children.OfType<Control>().Where(child => !IsDomInfrastructureControl(child)),
            ContentControl contentControl when contentControl.Content is Control content => new[] { content },
            Decorator decorator when decorator.Child is Control child => new[] { child },
            _ => Array.Empty<Control>()
        };

        foreach (var child in children)
        {
            foreach (var descendant in TraverseDocumentRecursive(child))
            {
                yield return descendant;
            }
        }
    }

    protected virtual bool MatchesSelector(Control control, string selector)
    {
        selector = selector.Trim();
        var wrapped = WrapControl(control);
        return wrapped.matches(selector)
               || (!wrapped.HasExplicitDomTag
                   && selector.IndexOfAny(['#', '.', '[', ':', ' ', '>', '+', '~', ',']) < 0
                   && string.Equals(control.GetType().Name, selector, StringComparison.OrdinalIgnoreCase));
    }

    internal void NotifyAttributeChanged(AvaloniaDomElement target, string attributeName, string? oldValue, string? newValue)
    {
        if (string.Equals(oldValue, newValue, StringComparison.Ordinal))
        {
            return;
        }

        if (attributeName.Equals("start", StringComparison.OrdinalIgnoreCase)
            || attributeName.Equals("value", StringComparison.OrdinalIgnoreCase)
            || attributeName.Equals("reversed", StringComparison.OrdinalIgnoreCase))
        {
            _listMarkerCountersDirty = true;
        }

        if (_elementsById is not null && string.Equals(attributeName, "id", StringComparison.OrdinalIgnoreCase))
        {
            RemoveElementFromIdIndex(target, oldValue);
            if (IsInDocumentTree(target.Control))
            {
                AddElementToIdIndex(target);
            }
        }

        var inHead = _head.Contains(target);
        var stylesheetsChanged = inHead
                                 || (IsStylesheetElement(target)
                                     && IsConnectedStyleElement(target)
                                     && attributeName is "media" or "type" or "disabled" or "href" or "rel");
        if (s_traceCssInvalidations)
        {
            Console.WriteLine(
                $"[CSS INVALIDATE] attribute={attributeName} target={target.localName}#{target.id}.{target.className.Replace(' ', '.')} " +
                $"old={oldValue} new={newValue} inHead={inHead}");
        }
        if (string.Equals(attributeName, "style", StringComparison.OrdinalIgnoreCase) && !inHead)
        {
            _styleEngine.InvalidateInlineStyle(target, oldValue, newValue);
        }
        else if (string.Equals(attributeName, "class", StringComparison.OrdinalIgnoreCase) && !inHead)
        {
            _styleEngine.InvalidateClass(target, oldValue, newValue);
        }
        else
        {
            _styleEngine.Invalidate(target, stylesheetsChanged: stylesheetsChanged);
        }
        if (stylesheetsChanged) ScheduleStylesheetUpdate();
        else ScheduleLayoutUpdate();

        if (!HasActiveMutationObservers())
        {
            return;
        }

        var record = DomMutationRecord.CreateForAttribute(target, attributeName, oldValue);
        QueueMutationRecord(record);
    }

    internal void NotifyDynamicStateChanged(AvaloniaDomElement target)
    {
        if (_styleEngine.InvalidateDynamicState(EnumerateElementAncestors(target)))
        {
            ScheduleLayoutUpdate();
        }
    }

    internal void NotifyStylePropertyChanged(
        AvaloniaDomElement target,
        string property,
        string? oldValue,
        string? newValue,
        bool notifyMutationObservers,
        string? oldStyleAttributeValue)
    {
        if (string.Equals(oldValue, newValue, StringComparison.Ordinal))
        {
            return;
        }

        if (_head.Contains(target))
        {
            _styleEngine.Invalidate(target, stylesheetsChanged: true);
            ScheduleStylesheetUpdate();
        }
        else
        {
            _styleEngine.InvalidateInlineStyleProperty(target, property, oldValue, newValue);
            ScheduleLayoutUpdate();
        }

        if (notifyMutationObservers)
        {
            QueueMutationRecord(DomMutationRecord.CreateForAttribute(target, "style", oldStyleAttributeValue));
        }
    }

    internal void NotifyChildListMutation(
        AvaloniaDomElement target,
        IReadOnlyList<AvaloniaDomElement>? addedNodes,
        IReadOnlyList<AvaloniaDomElement>? removedNodes,
        AvaloniaDomElement? previousSibling,
        AvaloniaDomElement? nextSibling)
    {
        var allocationStart = Host.CollectPerformanceMetrics ? GC.GetAllocatedBytesForCurrentThread() : 0;
        try
        {
        var hasAdded = addedNodes is { Count: > 0 };
        var hasRemoved = removedNodes is { Count: > 0 };
        if (!hasAdded && !hasRemoved)
        {
            return;
        }
        _listMarkerCountersDirty = true;
        if (addedNodes?.Any(SubtreeContainsViewportFixedElement) == true
            || removedNodes?.Any(SubtreeContainsViewportFixedElement) == true)
        {
            target.RefreshOverflowClipChainAfterChildMutation();
        }

        if (_elementsById is not null)
        {
            if (removedNodes is not null)
            {
                foreach (var removed in removedNodes)
                {
                    RemoveSubtreeFromIdIndex(removed);
                }
            }
            if (addedNodes is not null && IsInDocumentTree(target.Control))
            {
                foreach (var added in addedNodes)
                {
                    AddSubtreeToIdIndex(added);
                }
            }
        }

        var inHead = _head.Contains(target);
        var stylesheetsChanged = inHead
                                 || addedNodes?.Any(SubtreeContainsStylesheetElement) == true
                                 || removedNodes?.Any(SubtreeContainsStylesheetElement) == true;
        if (s_traceCssInvalidations)
        {
            Console.WriteLine(
                $"[CSS INVALIDATE] childList target={target.localName}#{target.id}.{target.className.Replace(' ', '.')} " +
                $"added={addedNodes?.Count ?? 0} removed={removedNodes?.Count ?? 0} " +
                $"addedNode={(addedNodes is { Count: > 0 } ? addedNodes[0].localName + ":" + addedNodes[0].nodeType : "none")} " +
                $"previous={(previousSibling is null ? "none" : previousSibling.localName)} " +
                $"next={(nextSibling is null ? "none" : nextSibling.localName)} inHead={inHead}");
        }
        if (stylesheetsChanged)
        {
            _styleEngine.Invalidate(target, stylesheetsChanged: true);
        }
        else
        {
            _styleEngine.InvalidateChildList(
                target,
                addedNodes,
                removedNodes,
                previousSibling,
                nextSibling);
        }
        if (stylesheetsChanged) ScheduleStylesheetUpdate();
        else ScheduleLayoutUpdate();

        if (!HasActiveMutationObservers())
        {
            return;
        }

        var record = DomMutationRecord.CreateForChildList(target, addedNodes, removedNodes, previousSibling, nextSibling);
        QueueMutationRecord(record);
        }
        finally
        {
            if (Host.CollectPerformanceMetrics)
            {
                _childListMutationAllocatedBytes += GC.GetAllocatedBytesForCurrentThread() - allocationStart;
            }
        }
    }

    private bool HasActiveMutationObservers() => _mutationObservers.Count > 0;

    internal bool TryGetStyleMutationObservation(
        AvaloniaDomElement target,
        out bool requiresOldValue)
    {
        requiresOldValue = false;
        var observed = false;
        foreach (var observer in _mutationObservers)
        {
            if (observer.ObservesAttributeMutation(target, out var observerRequiresOldValue))
            {
                observed = true;
                requiresOldValue |= observerRequiresOldValue;
                if (requiresOldValue)
                {
                    return true;
                }
            }
        }

        return observed;
    }

    internal bool RequiresChildListMutationNotification(
        AvaloniaDomElement target,
        AvaloniaDomElement addedChild)
    {
        if (HasActiveMutationObservers() || _head.Contains(target))
        {
            return true;
        }
        if (!IsConnectedStyleElement(target))
        {
            return false;
        }
        if (SubtreeContainsStylesheetElement(addedChild))
        {
            return true;
        }
        if (!_styleEngine.PendingDirtyRootCovers(target))
        {
            return true;
        }
        // A lazily-built id index predating this batch cannot discover an id
        // that was absent when the new ancestor was first attached.
        return _elementsById is not null && SubtreeContainsId(addedChild);
    }

    private static bool SubtreeContainsId(AvaloniaDomElement element)
    {
        if (!string.IsNullOrEmpty(element.id))
        {
            return true;
        }
        foreach (var child in element.GetChildElements())
        {
            if (SubtreeContainsId(child))
            {
                return true;
            }
        }
        return false;
    }

    private static bool IsStylesheetElement(AvaloniaDomElement element)
        => string.Equals(element.tagName, "STYLE", StringComparison.OrdinalIgnoreCase)
           || (string.Equals(element.tagName, "LINK", StringComparison.OrdinalIgnoreCase)
               && string.Equals(element.getAttribute("rel"), "stylesheet", StringComparison.OrdinalIgnoreCase));

    private static bool SubtreeContainsStylesheetElement(AvaloniaDomElement element)
    {
        if (IsStylesheetElement(element))
        {
            return true;
        }
        foreach (var child in element.GetChildElements())
        {
            if (SubtreeContainsStylesheetElement(child))
            {
                return true;
            }
        }
        return false;
    }

    private static bool SubtreeContainsViewportFixedElement(AvaloniaDomElement element)
    {
        if (CssLayout.GetPosition(element.Control) == CssPosition.Fixed)
        {
            return true;
        }
        foreach (var child in element.GetChildElements())
        {
            if (SubtreeContainsViewportFixedElement(child))
            {
                return true;
            }
        }
        return false;
    }

    private void QueueMutationRecord(DomMutationRecord record)
    {
        if (_mutationObservers.Count == 0)
        {
            return;
        }

        var anyQueued = false;
        var observers = _mutationObservers.ToArray();
        foreach (var observer in observers)
        {
            anyQueued |= observer.TryQueue(record);
        }

        if (!anyQueued)
        {
            return;
        }

        if (_mutationDeliveryScheduled)
        {
            return;
        }

        _mutationDeliveryScheduled = true;
        // MutationObserver delivery is a microtask checkpoint. Queue it ahead
        // of iframe navigation/resource tasks posted by the same JavaScript
        // turn so observers can attach load handlers before navigation runs.
        Host.Services.Dispatcher.Post(DeliverMutationRecords, HtmlMlDispatchPriority.Send);
    }

    private void DeliverMutationRecords()
    {
        // Mutation observer callbacks are microtasks in a browser. Avalonia's
        // dispatcher may be pumped by layout while JavaScript is still on the
        // stack, so explicitly preserve task run-to-completion here.
        if (Host.IsExecutingJavaScript)
        {
            Host.Services.Dispatcher.Post(DeliverMutationRecords, HtmlMlDispatchPriority.Background);
            return;
        }

        _mutationDeliveryScheduled = false;
        if (_mutationObservers.Count == 0)
        {
            return;
        }

        var observers = _mutationObservers.ToArray();
        using (new AvaloniaBrowserHost.JsCallScope(Host))
        {
            foreach (var observer in observers)
            {
                observer.Deliver();
            }
        }
        Host.RestoreOwnerRealm();
    }

    private static string? ToTypeName(string tag)
    {
        if (string.IsNullOrWhiteSpace(tag))
        {
            return null;
        }

        if (tag.IndexOfAny(new[] { '-', '_' }) >= 0)
        {
            var parts = tag.Split(new[] { '-', '_' }, StringSplitOptions.RemoveEmptyEntries);
            return string.Concat(parts.Select(Capitalize));
        }

        if (char.IsLower(tag[0]))
        {
            return char.ToUpper(tag[0], CultureInfo.InvariantCulture) + tag.Substring(1);
        }

        return tag;
    }

    private static string Capitalize(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return string.Empty;
        }

        if (value.Length == 1)
        {
            return value.ToUpper(CultureInfo.InvariantCulture);
        }

        return char.ToUpper(value[0], CultureInfo.InvariantCulture) + value.Substring(1);
    }

    internal void ScheduleReadyStateCompletion(Action? completed = null)
    {
        if (completed is not null)
        {
            if (string.Equals(_readyState, "complete", StringComparison.Ordinal))
            {
                Host.Services.Dispatcher.Post(completed, HtmlMlDispatchPriority.Background);
                return;
            }
            _readyStateCompletionCallbacks.Add(completed);
        }

        if (_readyStateScheduled)
        {
            return;
        }

        _readyStateScheduled = true;
        Host.Services.Dispatcher.Post(CompleteReadyState, HtmlMlDispatchPriority.Background);
    }

    internal void MarkParserComplete()
    {
        if (!string.Equals(_readyState, "loading", StringComparison.Ordinal))
        {
            return;
        }

        SetReadyState("interactive");
        DispatchDocumentLifecycleEvent("readystatechange", bubbles: false, cancelable: false);
    }

    private void CompleteReadyState()
    {
        if (Host.IsExecutingJavaScript)
        {
            Host.Services.Dispatcher.Post(CompleteReadyState, HtmlMlDispatchPriority.Background);
            return;
        }

        CompleteReadyStateCore();
    }

    internal void CompleteReadyStateAfterParser(Action? completed = null)
    {
        if (completed is not null)
        {
            _readyStateCompletionCallbacks.Add(completed);
        }

        _readyStateScheduled = true;
        CompleteReadyStateCore();
    }

    private void CompleteReadyStateCore()
    {
        if (string.Equals(_readyState, "complete", StringComparison.Ordinal))
        {
            foreach (var completed in _readyStateCompletionCallbacks.ToArray())
            {
                completed();
            }
            _readyStateCompletionCallbacks.Clear();
            return;
        }

        ExecuteInBrowsingContext(() =>
        {
            MarkParserComplete();
            DispatchDocumentLifecycleEvent("DOMContentLoaded", bubbles: true, cancelable: false);
            // DOMContentLoaded targets Document and reaches Window through the
            // document's event path in browsers. Window is a realm-local JS
            // event target here, so deliver that final path entry explicitly.
            DispatchWindowLifecycleEvent("DOMContentLoaded");
            SetReadyState("complete");
            DispatchDocumentLifecycleEvent("readystatechange", bubbles: false, cancelable: false);
            // Preserve the existing document-level compatibility event while
            // also delivering the browser-facing Window load below.
            DispatchDocumentLifecycleEvent("load", bubbles: false, cancelable: false);
            DispatchWindowLifecycleEvent("load");
        });

        foreach (var callback in _readyStateCompletionCallbacks.ToArray())
        {
            callback();
        }
        _readyStateCompletionCallbacks.Clear();
    }

    private void SetReadyState(string state)
    {
        _readyState = state;
    }

    private void DispatchDocumentLifecycleEvent(string type, bool bubbles, bool cancelable)
    {
        var evt = new DomEvent(type, bubbles, cancelable, initiallyHandled: false, Host.GetTimestamp(), isTrusted: true);
        evt.target = this;
        DispatchDocumentEvent(evt);
    }

    internal void DispatchWindowLifecycleEvent(string type)
    {
        var evt = new DomEvent(
            type,
            bubbles: false,
            cancelable: false,
            initiallyHandled: false,
            Host.GetTimestamp(),
            isTrusted: true)
        {
            // Window listeners receive the lifecycle event through dispatchEvent;
            // the document remains the stable host object exposed as its target.
            target = this
        };
        DispatchWindowDomEvent(type, evt);
    }

    private void DispatchWindowDomEvent(string type, DomEvent evt)
    {
        if (this is VirtualIframeDomDocument { ExternalRuntime: { } externalRuntime })
        {
            using var measurement = string.Equals(type, "resize", StringComparison.Ordinal)
                ? Host.MeasureResizeCallback(ResizeCallbackKind.FrameWindow)
                : default;
            externalRuntime.DispatchWindowEvent(type, evt);
            return;
        }

        if (ExternalWindowEventDispatcher is { } externalWindowEventDispatcher)
        {
            using var measurement = string.Equals(type, "resize", StringComparison.Ordinal)
                ? Host.MeasureResizeCallback(ResizeCallbackKind.OwnerWindow)
                : default;
            externalWindowEventDispatcher.DispatchWindowEvent(type, evt);
            return;
        }

    }

    internal void DispatchWindowEventToChildBrowsingContexts(string type)
    {
        foreach (var iframe in querySelectorAll("iframe").OfType<AvaloniaDomElement>())
        {
            if (iframe.GetExternalContentWindowRuntime() is { } externalRuntime)
            {
                externalRuntime.DispatchWindowEvent(
                    type,
                    new DomSyntheticEvent(
                        type, bubbles: false, cancelable: false, Host.GetTimestamp(), detail: null, accessor: null));
                continue;
            }

        }
    }

    internal void DisposeExternalBrowsingContexts()
    {
        _pendingResizeObserverTargets.Clear();
        _resizeObserverTargetDeliveryScheduled = false;
        _pendingResizeObserverCallbacks.Clear();
        _resizeObserverBatchDeliveryScheduled = false;
        foreach (var iframe in querySelectorAll("iframe").OfType<AvaloniaDomElement>())
        {
            if (iframe.GetContentDocument() is { } frameDocument)
            {
                frameDocument.DisposeExternalBrowsingContexts();
            }
            iframe.GetExternalContentWindowRuntime()?.Dispose();
        }
    }

    internal void RaiseDocumentEvent(string type, bool bubbles, bool cancelable)
        => DispatchDocumentLifecycleEvent(type, bubbles, cancelable);

    private void DispatchDocumentEvent(DomEvent domEvent)
    {
        var normalizedType = DocumentNormalizeEventName(domEvent.type);
        if (string.IsNullOrEmpty(normalizedType))
        {
            return;
        }

        var entry = new DomEventPathEntry(this);
        var path = new List<DomEventPathEntry> { entry };
        if (!HasListeners(normalizedType, path))
        {
            return;
        }

        InvokeListeners(entry, normalizedType, domEvent, capture: true, DomEventPhase.AtTarget);
        if (!domEvent.ImmediatePropagationStopped)
        {
            InvokeListeners(entry, normalizedType, domEvent, capture: false, DomEventPhase.AtTarget);
        }

        domEvent.ResetCurrentTarget();
    }

    public virtual bool dispatchEvent(object eventValue)
    {
        var synthetic = CreateSyntheticEvent(eventValue);
        if (synthetic is null)
        {
            return true;
        }

        try
        {
            synthetic.target = this;
            DispatchDocumentEvent(synthetic);
            synthetic.SyncDefaultPrevented();
        }
        finally
        {
            CompleteExternalSyntheticEventDispatch(eventValue);
        }
        return !synthetic.defaultPrevented;
    }

    internal void CompleteExternalSyntheticEventDispatch(object eventValue)
    {
        if (ExternalEventListenerAdapter is IExternalSyntheticEventAdapter adapter)
        {
            adapter.CompleteDispatch(eventValue);
        }
    }

    internal void DispatchPointerEvent(AvaloniaDomElement target, string type, PointerEventArgs args, bool bubbles, bool cancelable)
    {
        if (args.Pointer is { } pointer
            && string.Equals(type, "pointerdown", StringComparison.Ordinal))
        {
            _activePointers[pointer.Id] = pointer;
            _activePointerTargets[pointer.Id] = target;
            SetActivePointerDocument(pointer.Id, this);
        }
        if (string.Equals(type, "pointermove", StringComparison.Ordinal))
        {
            UpdatePointerHover(target, args);
        }
        // A trusted user activation must not click a disabled form control.
        // Synthetic dispatchEvent("click") follows its separate DOM path and
        // remains observable, as required by HTML.
        if (string.Equals(type, "click", StringComparison.Ordinal)
            && target.IsInDisabledFormControl())
        {
            return;
        }
        var activation = string.Equals(type, "click", StringComparison.Ordinal)
            ? BeginCheckableInputActivation(target)
            : null;
        var evt = DispatchPointerEventCore(target, type, args, bubbles, cancelable);
        if (activation is null)
        {
            if (string.Equals(type, "click", StringComparison.Ordinal)
                && !evt.defaultPrevented
                && FindLabelActivationControl(target) is { } labelControl)
            {
                // Clicking non-interactive content inside a label performs a
                // second, trusted click activation on the associated control.
                // Component libraries may deliberately give their visual checkbox face the
                // hit area while keeping the native checkbox transparent.
                DispatchPointerEvent(labelControl, type, args, bubbles, cancelable);
                return;
            }
            if (string.Equals(type, "click", StringComparison.Ordinal)
                && !evt.defaultPrevented)
            {
                PerformClickDefaultAction(target);
            }
            return;
        }

        CompleteCheckableInputActivation(target, activation, evt);
    }

    internal void DispatchProgrammaticClick(AvaloniaDomElement target)
    {
        if (target.IsDisabledFormControl || !_programmaticClickTargets.Add(target))
        {
            return;
        }

        try
        {
            var activation = BeginCheckableInputActivation(target);
            var evt = new DomEvent(
                "click",
                bubbles: true,
                cancelable: true,
                initiallyHandled: false,
                Host.GetTimestamp(),
                isTrusted: false);
            ExecuteInBrowsingContext(() =>
            {
                DispatchDomEventInternal(target, evt);
                if (evt.bubbles && !evt.PropagationStopped)
                {
                    DispatchWindowDomEvent("click", evt);
                }
            });

            if (activation is null
                && !evt.defaultPrevented
                && FindLabelActivationControl(target) is { } labelControl)
            {
                DispatchProgrammaticClick(labelControl);
                return;
            }

            CompleteCheckableInputActivation(target, activation, evt);
            if (!evt.defaultPrevented)
            {
                PerformClickDefaultAction(target);
            }
        }
        finally
        {
            _programmaticClickTargets.Remove(target);
        }
    }

    internal void DispatchProgrammaticClick(DomDocumentElement target)
    {
        var evt = new DomEvent(
            "click",
            bubbles: true,
            cancelable: true,
            initiallyHandled: false,
            Host.GetTimestamp(),
            isTrusted: false)
        {
            target = target
        };

        ExecuteInBrowsingContext(() =>
        {
            var composedPath = new List<object> { target, this };
            if (ExternalWindowContext is not null)
            {
                composedPath.Add(ExternalWindowContext);
            }
            evt.SetComposedPath(composedPath);

            const string type = "click";
            var documentListeners = GetDocumentListeners(type, create: false);
            var rootListeners = target.GetListeners(type);

            if (documentListeners is { Count: > 0 })
            {
                InvokeListenersCore(
                    this,
                    type,
                    evt,
                    capture: true,
                    DomEventPhase.CapturingPhase,
                    documentListeners,
                    listener => RemoveDocumentListener(type, listener));
            }

            if (!evt.PropagationStopped && rootListeners is { Count: > 0 })
            {
                InvokeListenersCore(
                    target,
                    type,
                    evt,
                    capture: true,
                    DomEventPhase.AtTarget,
                    rootListeners,
                    listener => target.RemoveListener(type, listener));
                if (!evt.ImmediatePropagationStopped)
                {
                    InvokeListenersCore(
                        target,
                        type,
                        evt,
                        capture: false,
                        DomEventPhase.AtTarget,
                        rootListeners,
                        listener => target.RemoveListener(type, listener));
                }
            }

            if (!evt.PropagationStopped && evt.bubbles && documentListeners is { Count: > 0 })
            {
                InvokeListenersCore(
                    this,
                    type,
                    evt,
                    capture: false,
                    DomEventPhase.BubblingPhase,
                    documentListeners,
                    listener => RemoveDocumentListener(type, listener));
            }

            evt.ResetCurrentTarget();
            if (evt.bubbles && !evt.PropagationStopped)
            {
                DispatchWindowDomEvent(type, evt);
            }
        });
    }

    private void PerformClickDefaultAction(AvaloniaDomElement target)
    {
        AvaloniaDomElement? anchor = target;
        while (anchor is not null
               && !string.Equals(anchor.localName, "a", StringComparison.OrdinalIgnoreCase))
        {
            anchor = anchor.parentElement;
        }

        if (anchor is null
            || !anchor.hasAttribute("download")
            || string.IsNullOrWhiteSpace(anchor.href))
        {
            return;
        }

        var fileName = anchor.getAttribute("download") ?? string.Empty;
        _ = Host.RequestDownloadAsync(fileName, anchor.href);
    }

    private void CompleteCheckableInputActivation(
        AvaloniaDomElement target,
        CheckableInputActivation? activation,
        DomEvent clickEvent)
    {
        if (activation is null)
        {
            return;
        }
        if (clickEvent.defaultPrevented)
        {
            RestoreCheckableInputActivation(activation);
            return;
        }
        if (!activation.Changed)
        {
            return;
        }

        ExecuteInBrowsingContext(() =>
        {
            DispatchDomEventInternal(
                target,
                new DomEvent("input", bubbles: true, cancelable: false, initiallyHandled: false, Host.GetTimestamp(), isTrusted: true));
            DispatchDomEventInternal(
                target,
                new DomEvent("change", bubbles: true, cancelable: false, initiallyHandled: false, Host.GetTimestamp(), isTrusted: true));
        });
    }

    private AvaloniaDomElement? FindLabelActivationControl(AvaloniaDomElement target)
    {
        AvaloniaDomElement? label = null;
        for (var current = target; current is not null; current = current.parentElement)
        {
            if (string.Equals(current.localName, "label", StringComparison.OrdinalIgnoreCase))
            {
                label = current;
                break;
            }

            // A click on interactive content nested in a label retains that
            // control's own activation behavior and does not proxy to another
            // associated control.
            if (string.Equals(current.localName, "input", StringComparison.OrdinalIgnoreCase)
                || string.Equals(current.localName, "button", StringComparison.OrdinalIgnoreCase)
                || string.Equals(current.localName, "select", StringComparison.OrdinalIgnoreCase)
                || string.Equals(current.localName, "textarea", StringComparison.OrdinalIgnoreCase)
                || string.Equals(current.localName, "a", StringComparison.OrdinalIgnoreCase)
                   && current.hasAttribute("href"))
            {
                return null;
            }
        }

        if (label is null)
        {
            return null;
        }

        var forId = label.getAttribute("for")?.Trim();
        var control = !string.IsNullOrEmpty(forId)
            ? getElementById(forId) as AvaloniaDomElement
            : label.querySelector("input,button,select,textarea") as AvaloniaDomElement;
        if (control is null
            || string.Equals(control.localName, "input", StringComparison.OrdinalIgnoreCase)
               && string.Equals(control.type, "hidden", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        return control;
    }

    private CheckableInputActivation? BeginCheckableInputActivation(AvaloniaDomElement target)
    {
        if (!string.Equals(target.localName, "input", StringComparison.OrdinalIgnoreCase)
            || target.hasAttribute("disabled"))
        {
            return null;
        }

        var inputType = target.type?.Trim();
        if (string.Equals(inputType, "checkbox", StringComparison.OrdinalIgnoreCase))
        {
            var activation = new CheckableInputActivation([(target, target.@checked)]);
            target.@checked = !target.@checked;
            return activation;
        }

        if (!string.Equals(inputType, "radio", StringComparison.OrdinalIgnoreCase)
            || target.@checked)
        {
            return null;
        }

        var name = target.name;
        var group = querySelectorAll("input")
            .OfType<AvaloniaDomElement>()
            .Where(candidate =>
                string.Equals(candidate.type, "radio", StringComparison.OrdinalIgnoreCase)
                && string.Equals(candidate.name, name, StringComparison.Ordinal))
            .DistinctBy(candidate => candidate.Control)
            .ToArray();
        var radioActivation = new CheckableInputActivation(
            group.Select(candidate => (candidate, candidate.@checked)).ToArray());
        foreach (var candidate in group)
        {
            candidate.@checked = ReferenceEquals(candidate, target);
        }
        return radioActivation;
    }

    private static void RestoreCheckableInputActivation(CheckableInputActivation activation)
    {
        foreach (var (element, wasChecked) in activation.PreviousStates)
        {
            element.@checked = wasChecked;
        }
    }

    private sealed class CheckableInputActivation(
        IReadOnlyList<(AvaloniaDomElement Element, bool WasChecked)> previousStates)
    {
        public IReadOnlyList<(AvaloniaDomElement Element, bool WasChecked)> PreviousStates { get; } = previousStates;

        public bool Changed => PreviousStates.Any(state => state.Element.@checked != state.WasChecked);
    }

    internal bool ApplyDefaultWheelScroll(AvaloniaDomElement target, PointerWheelEventArgs args)
    {
        var delta = new Vector(
            args.Delta.X * DomPointerEvent.WheelPixelScale,
            -args.Delta.Y * DomPointerEvent.WheelPixelScale);
        for (var current = target; current is not null; current = current.parentElement)
        {
            if (current.Control is CssLayoutPanel panel && panel.ScrollBy(delta))
            {
                return true;
            }
        }

        return false;
    }

    internal void DispatchActivePointerEvent(
        string type,
        PointerEventArgs args,
        bool bubbles,
        bool cancelable)
    {
        var pointerId = args.Pointer?.Id ?? 0;
        if (_pointerCaptures.TryGetValue(pointerId, out var captured))
        {
            DispatchPointerEventCore(captured, type, args, bubbles, cancelable);
            return;
        }

        if (_activePointerTargets.TryGetValue(pointerId, out var pressedTarget))
        {
            DispatchPointerEventCore(pressedTarget, type, args, bubbles, cancelable);
            return;
        }

        DispatchDocumentPointerEvent(type, args, bubbles, cancelable);
    }

    internal static AvaloniaDomDocument? GetActivePointerDocument(int pointerId)
    {
        if (pointerId == 0)
        {
            return null;
        }

        lock (s_activePointerDocumentsLock)
        {
            if (!s_activePointerDocuments.TryGetValue(pointerId, out var weakDocument))
            {
                return null;
            }

            if (weakDocument.TryGetTarget(out var document))
            {
                return document;
            }

            s_activePointerDocuments.Remove(pointerId);
            return null;
        }
    }

    private static void SetActivePointerDocument(int pointerId, AvaloniaDomDocument document)
    {
        lock (s_activePointerDocumentsLock)
        {
            s_activePointerDocuments[pointerId] = new WeakReference<AvaloniaDomDocument>(document);
        }
    }

    private void ClearActivePointerDocument(int pointerId)
    {
        lock (s_activePointerDocumentsLock)
        {
            if (s_activePointerDocuments.TryGetValue(pointerId, out var weakDocument)
                && (!weakDocument.TryGetTarget(out var document) || ReferenceEquals(document, this)))
            {
                s_activePointerDocuments.Remove(pointerId);
            }
        }
    }

    private DomPointerEvent DispatchPointerEventCore(
        AvaloniaDomElement target,
        string type,
        PointerEventArgs args,
        bool bubbles,
        bool cancelable,
        AvaloniaDomElement? relatedTarget = null)
    {
        var viewport = GetDocumentViewport() ?? target.Control;
        var view = ExternalWindowContext;
        var movement = ResolvePointerMovement(type, args, viewport);
        var pointerId = args.Pointer?.Id ?? 0;
        if (args is PointerPressedEventArgs pressed
            && string.Equals(type, "pointerdown", StringComparison.Ordinal))
        {
            _pointerClickCounts[pointerId] = Math.Max(1, pressed.ClickCount);
        }
        var detail = args is PointerPressedEventArgs pointerPressed
            ? pointerPressed.ClickCount
            : string.Equals(type, "click", StringComparison.Ordinal)
                ? _pointerClickCounts.GetValueOrDefault(pointerId, 1)
                : 0;
        var evt = new DomPointerEvent(
            type,
            args,
            target.Control,
            viewport,
            view,
            Host.GetTimestamp(),
            bubbles,
            cancelable,
            movement,
            detail,
            relatedTarget);
        ExecuteInBrowsingContext(() =>
        {
            DispatchDomEventInternal(target, evt);
            // Window is the final entry in a browser event path. Gesture
            // libraries commonly listen there for pointerup/mouseup so a drag
            // still terminates after its hit target changes or leaves a frame.
            if (evt.bubbles && !evt.PropagationStopped)
            {
                DispatchWindowDomEvent(type, evt);
            }
        });
        return evt;
    }

    private Vector ResolvePointerMovement(string type, PointerEventArgs args, Control viewport)
    {
        var pointerId = args.Pointer?.Id ?? 0;
        if (string.Equals(type, "mousemove", StringComparison.Ordinal))
        {
            return _lastPointerMovements.TryGetValue(pointerId, out var mouseMovement) ? mouseMovement : default;
        }

        if (!string.Equals(type, "pointermove", StringComparison.Ordinal)
            && !string.Equals(type, "pointerdown", StringComparison.Ordinal))
        {
            return default;
        }

        Point current;
        try
        {
            current = args.GetPosition(viewport);
        }
        catch (ArgumentException) when (args.Source is Visual source)
        {
            // Headless/unit-hosted controls may not have a visual root. Their
            // event coordinates are still valid relative to the original source.
            current = args.GetPosition(source);
        }
        var movement = string.Equals(type, "pointermove", StringComparison.Ordinal)
                       && _lastPointerPositions.TryGetValue(pointerId, out var previous)
            ? current - previous
            : default;
        _lastPointerPositions[pointerId] = current;
        _lastPointerMovements[pointerId] = movement;
        return movement;
    }

    internal void UpdatePointerHover(AvaloniaDomElement target, PointerEventArgs args)
    {
        var pointerId = GetPointerHoverKey(args);
        if (_pointerHoverTargets.TryGetValue(pointerId, out var previous) && ReferenceEquals(previous, target))
        {
            return;
        }

        var newPath = EnumerateElementAncestors(target).ToList();
        var previousPath = previous is null
            ? new List<AvaloniaDomElement>()
            : EnumerateElementAncestors(previous).ToList();
        var newSet = new HashSet<AvaloniaDomElement>(newPath);
        var previousSet = new HashSet<AvaloniaDomElement>(previousPath);

        _pointerHoverTargets[pointerId] = target;
        var changedHoverRoots = previousPath
            .Where(element => !newSet.Contains(element))
            .Concat(newPath.Where(element => !previousSet.Contains(element)));
        if (_styleEngine.InvalidateDynamicState(changedHoverRoots))
        {
            ScheduleLayoutUpdate();
        }

        if (previous is not null)
        {
            DispatchPointerEventCore(previous, "pointerout", args, bubbles: true, cancelable: false, relatedTarget: target);
            DispatchPointerEventCore(previous, "mouseout", args, bubbles: true, cancelable: false, relatedTarget: target);
            foreach (var exited in previousPath.Where(element => !newSet.Contains(element)))
            {
                DispatchPointerEventCore(exited, "pointerleave", args, bubbles: false, cancelable: false, relatedTarget: target);
                DispatchPointerEventCore(exited, "mouseleave", args, bubbles: false, cancelable: false, relatedTarget: target);
            }
        }

        DispatchPointerEventCore(target, "pointerover", args, bubbles: true, cancelable: false, relatedTarget: previous);
        DispatchPointerEventCore(target, "mouseover", args, bubbles: true, cancelable: false, relatedTarget: previous);
        foreach (var entered in newPath.Where(element => !previousSet.Contains(element)).Reverse())
        {
            DispatchPointerEventCore(entered, "pointerenter", args, bubbles: false, cancelable: false, relatedTarget: previous);
            DispatchPointerEventCore(entered, "mouseenter", args, bubbles: false, cancelable: false, relatedTarget: previous);
        }
    }

    private static IEnumerable<AvaloniaDomElement> EnumerateElementAncestors(AvaloniaDomElement element)
    {
        for (var current = element; current is not null; current = current.parentElement)
        {
            yield return current;
        }
    }

    internal bool IsPointerHovered(AvaloniaDomElement element)
    {
        foreach (var target in _pointerHoverTargets.Values)
        {
            for (var current = target; current is not null; current = current.parentElement)
            {
                if (ReferenceEquals(current, element))
                {
                    return true;
                }
            }
        }

        return false;
    }

    internal void ClearPointerHover(AvaloniaDomElement target, PointerEventArgs args)
    {
        var pointerId = GetPointerHoverKey(args);
        if (!_pointerHoverTargets.TryGetValue(pointerId, out var current)
            || !EnumerateElementAncestors(current).Any(element => ReferenceEquals(element, target)))
        {
            return;
        }

        // PointerExited and PointerEntered are separate Avalonia routed events.
        // A posted clear can therefore run between them even though the mouse
        // only crossed two boxes in the same document. Browsers update the
        // complete hover path atomically for that boundary. Re-hit-test the
        // exit coordinates before clearing so sibling/descendant hand-offs do
        // not expose an intermediate unhovered frame.
        var viewport = GetDocumentViewport();
        if (ReferenceEquals(args.RoutedEvent, InputElement.PointerExitedEvent)
            && viewport is not null)
        {
            var point = args.GetPosition(viewport);
            var replacement = elementsFromPoint(point.X, point.Y)
                .OfType<AvaloniaDomElement>()
                .FirstOrDefault(IsConnectedStyleElement);
            if (replacement is not null)
            {
                if (!ReferenceEquals(replacement, current))
                {
                    UpdatePointerHover(replacement, args);
                }

                // A native child/control boundary can still map to the same
                // DOM element. In that case there is no browser hover
                // transition to dispatch and, crucially, nothing to clear.
                return;
            }
        }

        _pointerHoverTargets.Remove(pointerId);
        if (_styleEngine.InvalidateDynamicState(EnumerateElementAncestors(current)))
        {
            ScheduleLayoutUpdate();
        }
        DispatchPointerEventCore(current, "pointerout", args, bubbles: true, cancelable: false);
        DispatchPointerEventCore(current, "mouseout", args, bubbles: true, cancelable: false);
        foreach (var exited in EnumerateElementAncestors(current))
        {
            DispatchPointerEventCore(exited, "pointerleave", args, bubbles: false, cancelable: false);
            DispatchPointerEventCore(exited, "mouseleave", args, bubbles: false, cancelable: false);
        }
    }

    private static int GetPointerHoverKey(PointerEventArgs args)
    {
        // The browser has one mouse hover chain. Avalonia may replace the native
        // Pointer instance (and therefore its numeric ID) while the mouse crosses
        // controls or popup/native-host boundaries. Keying mouse hover by that ID
        // leaves the previous chain permanently active. Touch and pen pointers can
        // coexist, so retain their real identities.
        return args.Pointer?.Type == PointerType.Mouse
            ? 0
            : args.Pointer?.Id ?? 0;
    }

    internal void SetPointerCapture(AvaloniaDomElement element, int pointerId)
    {
        if (!_activePointers.TryGetValue(pointerId, out var pointer))
        {
            throw new InvalidOperationException($"Pointer {pointerId} is not active.");
        }

        if (_pointerCaptures.TryGetValue(pointerId, out var previous) && !ReferenceEquals(previous, element))
        {
            ReleasePointerCapture(previous, pointerId);
        }

        pointer.Capture(element.Control);
        _pointerCaptures[pointerId] = element;
        DispatchSyntheticEvent(element, new DomSyntheticEvent(
            "gotpointercapture", bubbles: true, cancelable: false, Host.GetTimestamp(), pointerId, accessor: null));
    }

    internal void ReleasePointerCapture(AvaloniaDomElement element, int pointerId)
    {
        if (!_pointerCaptures.TryGetValue(pointerId, out var captured) || !ReferenceEquals(captured, element))
        {
            return;
        }

        if (_activePointers.TryGetValue(pointerId, out var pointer) && ReferenceEquals(pointer.Captured, element.Control))
        {
            pointer.Capture(null);
        }

        _pointerCaptures.Remove(pointerId);
        DispatchSyntheticEvent(element, new DomSyntheticEvent(
            "lostpointercapture", bubbles: true, cancelable: false, Host.GetTimestamp(), pointerId, accessor: null));
    }

    internal bool HasPointerCapture(AvaloniaDomElement element, int pointerId)
        => _pointerCaptures.TryGetValue(pointerId, out var captured) && ReferenceEquals(captured, element);

    internal void CompletePointer(int pointerId)
    {
        if (_pointerCaptures.TryGetValue(pointerId, out var captured))
        {
            ReleasePointerCapture(captured, pointerId);
        }
        _activePointers.Remove(pointerId);
        _activePointerTargets.Remove(pointerId);
        _pointerClickCounts.Remove(pointerId);
        ClearActivePointerDocument(pointerId);
    }

    internal Control? GetDocumentViewport()
    {
        if (body is AvaloniaDomElement bodyEl && bodyEl.Control is { } bodyCtrl && bodyCtrl.GetVisualRoot() is not null)
        {
            return bodyCtrl;
        }

        var content = Host.TopLevel.Content as Control;
        if (content?.GetVisualRoot() is not null)
        {
            return content;
        }

        return Host.TopLevel as Control ?? content ?? GetDocumentRoot();
    }

    internal Size GetDocumentViewportClientSize()
    {
        var viewport = GetDocumentViewport();
        if (viewport is not null)
        {
            var size = viewport.Bounds.Size;
            if (double.IsFinite(size.Width) && double.IsFinite(size.Height)
                && size.Width > 0 && size.Height > 0)
            {
                return size;
            }
        }

        var clientSize = Host.Services.Viewport.HostMetrics.ClientSize;
        return new Size(
            double.IsFinite(clientSize.Width) ? Math.Max(0, clientSize.Width) : 0,
            double.IsFinite(clientSize.Height) ? Math.Max(0, clientSize.Height) : 0);
    }

    internal double GetDocumentDevicePixelRatio()
        => Host.Services.Viewport.HostMetrics.DeviceScaleFactor;

    internal void DispatchKeyboardEvent(AvaloniaDomElement target, string type, KeyEventArgs args, bool bubbles, bool cancelable)
    {
        var evt = new DomKeyboardEvent(type, args, Host.GetTimestamp(), bubbles, cancelable);
        DispatchDomEventInternal(target, evt);
        if (bubbles && !evt.PropagationStopped)
        {
            DispatchWindowDomEvent(type, evt);
        }
    }

    internal void DispatchTextInputEvent(AvaloniaDomElement target, string type, TextInputEventArgs args, bool bubbles, bool cancelable)
    {
        var evt = new DomTextInputEvent(type, args, Host.GetTimestamp(), bubbles, cancelable);
        DispatchDomEventInternal(target, evt);
    }

    internal void DispatchRoutedEvent(AvaloniaDomElement target, string type, RoutedEventArgs args, bool bubbles, bool cancelable)
    {
        var evt = new AvaloniaDomRoutedEvent(
            type,
            bubbles,
            cancelable,
            args,
            Host.GetTimestamp(),
            isTrusted: true);
        DispatchDomEventInternal(target, evt);
    }

    internal void DispatchSyntheticEvent(AvaloniaDomElement target, DomSyntheticEvent evt)
        => DispatchDomEventInternal(target, evt);

    internal void DispatchTransitionEvent(
        AvaloniaDomElement target,
        string type,
        string propertyName,
        double elapsedTime)
        => DispatchDomEventInternal(
            target,
            new DomTransitionEvent(type, propertyName, elapsedTime, Host.GetTimestamp()));

    internal DomSyntheticEvent? CreateSyntheticEvent(object value)
    {
        if (value is DomSyntheticEvent syntheticEvent)
        {
            return syntheticEvent;
        }

        if (value is string eventType)
        {
            var trimmedType = eventType.Trim();
            return string.IsNullOrEmpty(DocumentNormalizeEventName(trimmedType))
                ? null
                : new DomSyntheticEvent(
                    trimmedType,
                    bubbles: false,
                    cancelable: false,
                    Host.GetTimestamp(),
                    detail: null,
                    accessor: null);
        }

        if (ExternalEventListenerAdapter is not IExternalSyntheticEventAdapter adapter
            || !adapter.TryReadSyntheticEvent(value, out var data))
        {
            return null;
        }

        var type = data.Type?.Trim() ?? string.Empty;
        if (string.IsNullOrEmpty(DocumentNormalizeEventName(type)))
        {
            return null;
        }

        return new DomSyntheticEvent(
            type,
            data.Bubbles,
            data.Cancelable,
            Host.GetTimestamp(),
            data.Detail,
            new DefaultPreventedAccessor(defaultPrevented => adapter.SetDefaultPrevented(value, defaultPrevented)),
            data.SourceEvent);
    }

    private void DispatchDomEventInternal(AvaloniaDomElement target, DomEvent domEvent)
    {
        domEvent.target = target;
        var normalizedType = DocumentNormalizeEventName(domEvent.type);
        if (string.IsNullOrEmpty(normalizedType))
        {
            return;
        }

        var path = BuildEventPath(target);
        var composedPath = path.AsEnumerable().Reverse().Select(entry =>
            entry.IsDocument ? (object)entry.Document! : entry.Element!).ToList();
        if (ExternalWindowContext is not null)
        {
            composedPath.Add(ExternalWindowContext);
        }
        domEvent.SetComposedPath(composedPath);
        if (!HasListeners(normalizedType, path))
        {
            return;
        }

        for (var i = 0; i < path.Count - 1; i++)
        {
            var entry = path[i];
            InvokeListeners(entry, normalizedType, domEvent, capture: true, DomEventPhase.CapturingPhase);
            if (domEvent.PropagationStopped)
            {
                domEvent.ResetCurrentTarget();
                return;
            }
        }

        var targetEntry = path[^1];
        InvokeListeners(targetEntry, normalizedType, domEvent, capture: true, DomEventPhase.AtTarget);
        if (!domEvent.ImmediatePropagationStopped)
        {
            InvokeListeners(targetEntry, normalizedType, domEvent, capture: false, DomEventPhase.AtTarget);
        }

        if (domEvent.PropagationStopped || !domEvent.bubbles)
        {
            domEvent.ResetCurrentTarget();
            return;
        }

        for (var i = path.Count - 2; i >= 0; i--)
        {
            var entry = path[i];
            InvokeListeners(entry, normalizedType, domEvent, capture: false, DomEventPhase.BubblingPhase);
            if (domEvent.PropagationStopped)
            {
                domEvent.ResetCurrentTarget();
                return;
            }
        }

        domEvent.ResetCurrentTarget();
    }

    private List<DomEventPathEntry> BuildEventPath(AvaloniaDomElement target)
    {
        var path = new List<DomEventPathEntry> { new(this) };
        var stack = new Stack<AvaloniaDomElement>();
        for (var current = target; current is not null; current = current.parentElement)
        {
            stack.Push(current);
        }

        while (stack.Count > 0)
        {
            path.Add(new DomEventPathEntry(stack.Pop()));
        }

        return path;
    }

    private bool HasListeners(string type, List<DomEventPathEntry> path)
    {
        if (_documentEventListeners.TryGetValue(type, out var documentList) && documentList.Count > 0)
        {
            return true;
        }

        foreach (var entry in path)
        {
            if (!entry.IsDocument && entry.Element!.HasListeners(type))
            {
                return true;
            }
        }

        return false;
    }

    private void InvokeListeners(DomEventPathEntry entry, string type, DomEvent domEvent, bool capture, DomEventPhase phase)
    {
        if (entry.IsDocument)
        {
            if (!_documentEventListeners.TryGetValue(type, out var list) || list.Count == 0)
            {
                return;
            }

            InvokeListenersCore(this, type, domEvent, capture, phase, list, listener => RemoveDocumentListener(type, listener));
            return;
        }

        var element = entry.Element!;
        var listeners = element.GetListeners(type);
        if (listeners is null || listeners.Count == 0)
        {
            return;
        }

        InvokeListenersCore(element, type, domEvent, capture, phase, listeners, listener => element.RemoveListener(type, listener));
    }

    private void InvokeListenersCore(object currentTarget, string type, DomEvent domEvent, bool capture, DomEventPhase phase, IReadOnlyList<DomEventRegistration> listeners, Action<DomEventRegistration> remove)
    {
        if (listeners.Count == 0)
        {
            return;
        }

        var snapshot = listeners.ToArray();
        if (ExternalEventListenerAdapter is IExternalDomEventListenerBatchInvoker batchInvoker)
        {
            var externalBatch = new List<(DomEventRegistration Listener, int Index)>();
            for (var listenerIndex = 0; listenerIndex < snapshot.Length; listenerIndex++)
            {
                var listener = snapshot[listenerIndex];
                if (listener.Options.Capture != capture)
                {
                    continue;
                }
                if (listener.ExternalCallback is null)
                {
                    externalBatch.Clear();
                    break;
                }
                externalBatch.Add((listener, listenerIndex));
            }

            if (externalBatch.Count > 1)
            {
                var batch = externalBatch.ToArray();
                var callbacks = new IExternalDomEventListener[batch.Length];
                for (var index = 0; index < batch.Length; index++)
                {
                    callbacks[index] = batch[index].Listener.ExternalCallback!;
                }
                var batchControl = new ExternalDomEventBatchControl(
                    this,
                    currentTarget,
                    type,
                    domEvent,
                    phase,
                    batch,
                    remove);
                try
                {
                    batchInvoker.InvokeBatch(currentTarget, domEvent, callbacks, batchControl);
                }
                catch (Exception exception)
                {
                    batchControl.ReportBatchError(exception);
                }
                return;
            }
        }

        for (var listenerIndex = 0; listenerIndex < snapshot.Length; listenerIndex++)
        {
            var listener = snapshot[listenerIndex];
            if (listener.Options.Capture != capture)
            {
                continue;
            }

            domEvent.SetCurrentTarget(currentTarget, phase, listener.Options.Passive);
            var traceListener = Host.TraceEventListeners;
            var listenerStarted = traceListener ? Stopwatch.GetTimestamp() : 0;
            var listenerAllocated = traceListener ? GC.GetAllocatedBytesForCurrentThread() : 0;
            try
            {
                listener.ExternalCallback!.Invoke(currentTarget, domEvent);
                if (Host.EnableDiagnosticLogging && type is "click" or "focus" && _eventDispatchDiagnostics.Count < 200)
                {
                    _eventDispatchDiagnostics.Add($"{type}:{phase}:{(capture ? "capture" : "bubble")}:{listenerIndex}:ok:" +
                                                  $"stop={domEvent.PropagationStopped}:immediate={domEvent.ImmediatePropagationStopped}:current={currentTarget.GetType().Name}");
                }
            }
            catch (Exception exception)
            {
                if (Host.EnableDiagnosticLogging && type is "click" or "focus" && _eventDispatchDiagnostics.Count < 200)
                {
                    _eventDispatchDiagnostics.Add($"{type}:{phase}:{(capture ? "capture" : "bubble")}:{listenerIndex}:error:{exception.Message}");
                }
                if (Host.JavaScriptExceptionDiagnostics.Count < 2000)
                {
                    Host.JavaScriptExceptionDiagnostics.Add(
                        $"DOM event listener failed: type={type}, phase={phase}, target={currentTarget}: {exception}");
                }
                if (Host.EnableDiagnosticLogging)
                {
                    Console.Error.WriteLine(
                        $"DOM event listener failed: type={type}, phase={phase}, target={currentTarget}: {exception}");
                }
            }
            finally
            {
                if (traceListener)
                {
                    Host.RecordEventListener(
                        $"{type}:{phase}:{(capture ? "capture" : "bubble")}:{currentTarget.GetType().Name}:listener-{listenerIndex}",
                        Stopwatch.GetTimestamp() - listenerStarted,
                        GC.GetAllocatedBytesForCurrentThread() - listenerAllocated);
                }
            }

            if (listener.Options.Once)
            {
                remove(listener);
            }

            if (domEvent.ImmediatePropagationStopped)
            {
                break;
            }
        }
    }

    public sealed class ExternalDomEventBatchControl : IExternalDomEventBatchControl
    {
        private readonly AvaloniaDomDocument _document;
        private readonly object _currentTarget;
        private readonly string _type;
        private readonly DomEvent _domEvent;
        private readonly DomEventPhase _phase;
        private readonly (DomEventRegistration Listener, int Index)[] _listeners;
        private readonly Action<DomEventRegistration> _remove;
        private readonly long[]? _started;
        private readonly long[]? _allocated;
        private readonly bool[]? _failed;

        internal ExternalDomEventBatchControl(
            AvaloniaDomDocument document,
            object currentTarget,
            string type,
            DomEvent domEvent,
            DomEventPhase phase,
            (DomEventRegistration Listener, int Index)[] listeners,
            Action<DomEventRegistration> remove)
        {
            _document = document;
            _currentTarget = currentTarget;
            _type = type;
            _domEvent = domEvent;
            _phase = phase;
            _listeners = listeners;
            _remove = remove;
            if (document.Host.TraceEventListeners)
            {
                _started = new long[listeners.Length];
                _allocated = new long[listeners.Length];
            }
            if (document.Host.EnableDiagnosticLogging)
            {
                _failed = new bool[listeners.Length];
            }
        }

        public bool ShouldStop => _domEvent.ImmediatePropagationStopped;

        public void BeforeInvoke(int index)
        {
            var listener = _listeners[index].Listener;
            _domEvent.SetCurrentTarget(_currentTarget, _phase, listener.Options.Passive);
            if (_document.Host.TraceEventListeners)
            {
                _started![index] = Stopwatch.GetTimestamp();
                _allocated![index] = GC.GetAllocatedBytesForCurrentThread();
            }
        }

        public void ReportError(int index, string error)
        {
            if (_failed is not null)
            {
                _failed[index] = true;
            }
            var listenerIndex = _listeners[index].Index;
            if (_document.Host.EnableDiagnosticLogging
                && _type is "click" or "focus"
                && _document._eventDispatchDiagnostics.Count < 200)
            {
                _document._eventDispatchDiagnostics.Add(
                    $"{_type}:{_phase}:{(_listeners[index].Listener.Options.Capture ? "capture" : "bubble")}:" +
                    $"{listenerIndex}:error:{error}");
            }
            if (_document.Host.JavaScriptExceptionDiagnostics.Count < 2000)
            {
                _document.Host.JavaScriptExceptionDiagnostics.Add(
                    $"DOM event listener failed: type={_type}, phase={_phase}, target={_currentTarget}: {error}");
            }
            if (_document.Host.EnableDiagnosticLogging)
            {
                Console.Error.WriteLine(
                    $"DOM event listener failed: type={_type}, phase={_phase}, target={_currentTarget}: {error}");
            }
        }

        public void AfterInvoke(int index)
        {
            var (listener, listenerIndex) = _listeners[index];
            if (_failed is not null
                && !_failed[index]
                && _document.Host.EnableDiagnosticLogging
                && _type is "click" or "focus"
                && _document._eventDispatchDiagnostics.Count < 200)
            {
                _document._eventDispatchDiagnostics.Add(
                    $"{_type}:{_phase}:{(listener.Options.Capture ? "capture" : "bubble")}:" +
                    $"{listenerIndex}:ok:stop={_domEvent.PropagationStopped}:" +
                    $"immediate={_domEvent.ImmediatePropagationStopped}:current={_currentTarget.GetType().Name}");
            }
            if (_document.Host.TraceEventListeners)
            {
                _document.Host.RecordEventListener(
                    $"{_type}:{_phase}:{(listener.Options.Capture ? "capture" : "bubble")}:" +
                    $"{_currentTarget.GetType().Name}:listener-{listenerIndex}",
                    Stopwatch.GetTimestamp() - _started![index],
                    GC.GetAllocatedBytesForCurrentThread() - _allocated![index]);
            }
            if (listener.Options.Once)
            {
                _remove(listener);
            }
        }

        public void ReportBatchError(Exception exception)
        {
            if (_document.Host.JavaScriptExceptionDiagnostics.Count < 2000)
            {
                _document.Host.JavaScriptExceptionDiagnostics.Add(
                    $"DOM event listener batch failed: type={_type}, phase={_phase}, target={_currentTarget}: {exception}");
            }
            if (_document.Host.EnableDiagnosticLogging)
            {
                Console.Error.WriteLine(
                    $"DOM event listener batch failed: type={_type}, phase={_phase}, target={_currentTarget}: {exception}");
            }
        }
    }

    private void RemoveDocumentListener(string type, DomEventRegistration listener)
    {
        if (_documentEventListeners.TryGetValue(type, out var list) && list.Remove(listener) && list.Count == 0)
        {
            _documentEventListeners.Remove(type);
        }
    }

    internal int GetDocumentEventListenerCount(string type)
    {
        var normalized = DocumentNormalizeEventName(type);
        return _documentEventListeners.TryGetValue(normalized, out var listeners) ? listeners.Count : 0;
    }

    private readonly struct DomEventPathEntry
    {
        public DomEventPathEntry(AvaloniaDomDocument document)
        {
            Document = document;
            Element = null;
        }

        public DomEventPathEntry(AvaloniaDomElement element)
        {
            Document = null;
            Element = element;
        }

        public AvaloniaDomDocument? Document { get; }

        public AvaloniaDomElement? Element { get; }

        public bool IsDocument => Document is not null;
    }

    internal void TriggerIframeVirtualization(AvaloniaDomElement element, string src)
    {
        if (string.IsNullOrWhiteSpace(src))
        {
            return;
        }

        var frameDocument = MountVirtualIframeBody(element);
        if (frameDocument is null)
        {
            return;
        }
        // Mounting an external browsing context executes adapter setup code and
        // can re-enter DOM APIs. The content document is already observable at
        // that point, but its window is not attached until the factory returns.
        // Do not let a re-entrant src mutation queue navigation against that
        // partially initialized document; the outer call will queue it once the
        // browsing context is complete.
        if (frameDocument.ExternalRuntime is null || element.GetExternalContentWindow() is null)
        {
            return;
        }
        if (!element.TryBeginContentInitialization())
        {
            return;
        }

        Action? initializeFrame = null;
        initializeFrame = () =>
        {
            if (element.parentElement is null
                || !ReferenceEquals(element.GetContentDocument(), frameDocument))
            {
                element.EndContentInitialization(succeeded: false, frameDocument);
                return;
            }
            // Navigation is a later task. A nested Avalonia dispatcher pump
            // must not initialize an iframe while the script that appended it
            // is still running; MutationObserver microtasks and load-listener
            // registration must get their browser-defined checkpoint first.
            if (Host.IsExecutingJavaScript)
            {
                Host.Services.Dispatcher.Post(initializeFrame!, HtmlMlDispatchPriority.Background);
                return;
            }

            using var jsScope = new AvaloniaBrowserHost.JsCallScope(Host);
            var externalRuntime = frameDocument.ExternalRuntime;
            var initialized = false;
            try
            {
                if (externalRuntime is null || element.GetExternalContentWindow() is null)
                {
                    throw new InvalidOperationException("The iframe window context was not initialized.");
                }
                // Attribute mutations made before the queued navigation task
                // replace the pending URL in browsers. Read src at execution
                // time so the latest standard URL wins.
                var navigationSource = element.getAttribute("src") ?? src;
                frameDocument.location.href = navigationSource;
                var initialScripts = HydrateIframeHeadFromObjectUrl(frameDocument, navigationSource);
                // Parser-inserted styles precede the iframe's blocking scripts.
                // Resolve that initial batch once; later DOM-inserted links use
                // the asynchronous scheduler above.
                frameDocument.EnsureStylesCurrent();
                // Execute the script sequence declared by the runtime-created
                // iframe document. No product filenames or bundle contents are
                // inspected here; normal script ordering is preserved.
                var orderedScripts = initialScripts
                    .Select((script, index) => (Script: script, Index: index))
                    .OrderBy(item => item.Script.Defer ? 1 : 0)
                    .ThenBy(item => item.Index)
                    .ToArray();
                void ExecuteScripts(bool defer)
                {
                    foreach (var item in orderedScripts.Where(item => item.Script.Defer == defer))
                    {
                        var script = item.Script;
                        var scriptIndex = item.Index;
                        frameDocument.RecordResourceTimeline(
                            "parser-execute",
                            "script",
                            string.IsNullOrWhiteSpace(script.Source) ? $"inline:{scriptIndex}" : script.Source,
                            "started");
                        if (!string.IsNullOrWhiteSpace(script.Source))
                        {
                            var specifier = frameDocument.ResolveIframeScriptSpecifier(script.Source);
                            externalRuntime.ExecuteClassicScript(specifier);
                        }
                        else if (!string.IsNullOrWhiteSpace(script.Code))
                        {
                            externalRuntime.ExecuteInlineClassicScript(
                                script.Code,
                                new AvaloniaBrowserHost.ScriptElementJs(
                                    string.Empty,
                                    script.Code,
                                    frameDocument,
                                    script.Attributes),
                                $"inline-frame-script-{scriptIndex}.js");
                        }
                        frameDocument.RecordResourceTimeline(
                            "parser-execute",
                            "script",
                            string.IsNullOrWhiteSpace(script.Source) ? $"inline:{scriptIndex}" : script.Source,
                            "completed");
                    }
                }

                ExecuteScripts(defer: false);
                frameDocument.MarkParserComplete();
                ExecuteScripts(defer: true);

                // Parser completion, frame lifecycle events, and the iframe
                // element's load event precede timer tasks queued by deferred
                // scripts in browsers. Complete them in this parser task.
                frameDocument.CompleteReadyStateAfterParser(() =>
                {
                    element.DispatchResourceEvent("load");
                });
                externalRuntime?.ProcessPendingTasks();
                initialized = true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IFRAME ERROR] Failed to initialize virtual iframe: {ex}");
            }
            finally
            {
                element.EndContentInitialization(initialized, frameDocument);
            }
        };
        Host.Services.Dispatcher.Post(initializeFrame, HtmlMlDispatchPriority.Default);
    }

    internal void EnsureInitialIframeBrowsingContext(AvaloniaDomElement element)
    {
        // A connected iframe owns an initial about:blank Document even without
        // a src attribute. Pure layout hosts may intentionally omit a
        // JavaScript browsing-context factory; retain their lightweight iframe
        // panel in that case rather than manufacturing an unusable document.
        if (Host.ExternalVirtualBrowsingContextFactory is null)
        {
            return;
        }

        MountVirtualIframeBody(element);
    }

    private sealed record IframeScriptDescriptor(
        string? Source,
        string Code,
        bool Defer,
        IReadOnlyDictionary<string, string> Attributes);

    private static IReadOnlyList<IframeScriptDescriptor> HydrateIframeHeadFromObjectUrl(AvaloniaDomDocument frameDocument, string src)
    {
        var scripts = new List<IframeScriptDescriptor>();
        if (!AvaloniaBrowserHost.UrlJs.TryGetObjectUrlText(src, out var html)
            || string.IsNullOrWhiteSpace(html))
        {
            return scripts;
        }

        var parser = new AngleSharp.Html.Parser.HtmlParser();
        var parsed = parser.ParseDocument(html);
        if (parsed.Head is null)
        {
            return scripts;
        }

        foreach (var source in parsed.Head.Children)
        {
            var tag = source.TagName.ToLowerInvariant();
            if (tag != "base" && tag != "link" && tag != "style")
            {
                continue;
            }

            var target = frameDocument.createElement(tag) as AvaloniaDomElement;
            if (target is null)
            {
                continue;
            }

            foreach (var attribute in source.Attributes)
            {
                target.setAttribute(attribute.Name, attribute.Value);
            }

            if (tag == "style")
            {
                target.textContent = source.TextContent;
            }

            frameDocument.head.appendChild(target);
        }

        if (parsed.Body is not null && frameDocument.body is AvaloniaDomElement frameBody)
        {
            foreach (var attribute in parsed.Body.Attributes)
            {
                frameBody.setAttribute(attribute.Name, attribute.Value);
            }

            foreach (var sourceNode in parsed.Body.ChildNodes)
            {
                if (sourceNode is AngleSharp.Dom.IElement element
                    && string.Equals(element.TagName, "SCRIPT", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                var targetNode = frameBody.CreateDomNodeFromAngleSharp(sourceNode);
                if (targetNode is not null)
                {
                    frameBody.appendChild(targetNode);
                }
            }
        }

        foreach (var script in parsed.QuerySelectorAll("script"))
        {
            scripts.Add(new IframeScriptDescriptor(
                script.GetAttribute("src"),
                script.TextContent ?? string.Empty,
                script.HasAttribute("defer"),
                script.Attributes.ToDictionary(
                    attribute => attribute.Name,
                    attribute => attribute.Value,
                    StringComparer.OrdinalIgnoreCase)));
        }

        if (frameDocument.Host.EnableDiagnosticLogging)
        {
            Console.WriteLine("Virtual iframe scripts: " + string.Join(", ", scripts.Select((script, index) =>
                $"#{index}:{(string.IsNullOrWhiteSpace(script.Source) ? $"inline({script.Code.Length})" : script.Source)}")));
        }

        return scripts;
    }

    private VirtualIframeDomDocument? MountVirtualIframeBody(AvaloniaDomElement iframe)
    {
        if (iframe.Control is not Panel iframePanel)
        {
            return null;
        }

        if (iframe.GetContentDocument() is VirtualIframeDomDocument existing)
        {
            return existing;
        }

        var body = new CssLayoutPanel
        {
            Background = Brushes.Transparent,
            ClipToBounds = true
        };
        CssLayout.SetWidth(body, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetHeight(body, new CssLength(100, CssLengthUnit.Percent));
        CssLayout.SetDocumentViewportRoot(body, true);

        iframePanel.Children.Add(body);
        var externalFactory = Host.ExternalVirtualBrowsingContextFactory;
        if (externalFactory is null)
        {
            iframePanel.Children.Remove(body);
            throw new InvalidOperationException("A JavaScript runtime must provide virtual iframe browsing contexts.");
        }

        var frameDocument = new VirtualIframeDomDocument(Host, body);
        if (frameDocument.body is AvaloniaDomElement frameBody)
        {
            // A browsing-context document viewport fills its iframe. Keep this
            // contract in CSS state so a later cascade pass cannot replace the
            // attached 100% lengths with the generic body `auto` defaults.
            frameBody.SetStyleProperty("width", "100%");
            frameBody.SetStyleProperty("height", "100%");
            frameBody.SetStyleProperty("overflow", "hidden");
        }
        iframe.SetContentDocument(frameDocument);
        var frameLocation = new AvaloniaBrowserHost.LocationJs(Host) { href = "about:blank" };
        frameDocument.SetBrowsingContextLocation(frameLocation);
        var externalRuntime = externalFactory.Create(Host, frameDocument, iframe);
        frameDocument.SetExternalRuntime(externalRuntime);
        iframe.SetExternalContentWindow(externalRuntime.Window, externalRuntime);
        iframePanel.InvalidateMeasure();
        iframePanel.InvalidateArrange();
        return frameDocument;
    }
}

internal sealed class VirtualIframeDomDocument : AvaloniaDomDocument
{
    private static readonly bool s_enableFrameResizeCoalescing = string.Equals(
        Environment.GetEnvironmentVariable("HTMLML_ENABLE_VIRTUAL_FRAME_RESIZE_COALESCING"),
        "1",
        StringComparison.Ordinal);
    private static readonly TimeSpan s_frameResizeQuietPeriod = TimeSpan.FromMilliseconds(80);
    private readonly Control _root;
    private IHtmlMlScheduledWork? _pendingFrameResizeEvent;
    private bool _viewportResizeFramePending;
    public VirtualIframeDomDocument(AvaloniaBrowserHost host, Control root)
        : base(host, elementFactory: null, attachTopLevelHandlers: false)
    {
        _root = root;
    }

    internal IExternalVirtualBrowsingContext? ExternalRuntime { get; private set; }

    internal void SetExternalRuntime(IExternalVirtualBrowsingContext runtime)
    {
        ExternalRuntime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        ExternalWindowContext = runtime.Window;
    }

    internal void DetachExternalRuntime(IExternalVirtualBrowsingContext runtime)
    {
        if (ReferenceEquals(ExternalRuntime, runtime))
        {
            _pendingFrameResizeEvent?.Dispose();
            _pendingFrameResizeEvent = null;
            ExternalRuntime = null;
            ExternalWindowContext = null;
        }
    }


    protected override Control? GetDocumentRoot() => _root;

    internal override void RestoreViewportLayoutContract()
    {
        if (EnsureViewportLayoutContract())
        {
            InvalidateViewportLayout();
        }

        if (DisableScopedPositionedLayoutReapply)
        {
            ReapplyPositionedRecursive(WrapControl(_root));
        }

        // Native window sizing can notify more frequently than a frame can be
        // presented. Keep one live redraw queued per presentation frame and let
        // it consume the newest arranged size, matching browser rAF cadence
        // without waiting for the resize gesture to end.
        if (_viewportResizeFramePending)
        {
            return;
        }
        _viewportResizeFramePending = true;
        var weakDocument = new WeakReference<VirtualIframeDomDocument>(this);
        HostTopLevel.RequestAnimationFrame(_ =>
        {
            if (!weakDocument.TryGetTarget(out var document))
            {
                return;
            }
            document.DeliverViewportResizeFrame();
        });
    }

    private void DeliverViewportResizeFrame()
    {
        _viewportResizeFramePending = false;
        if (Host.IsDisposed)
        {
            return;
        }

        if (EnsureViewportLayoutContract())
        {
            InvalidateViewportLayout();
        }
        if (DisableScopedPositionedLayoutReapply)
        {
            ReapplyPositionedRecursive(WrapControl(_root));
        }
        FlushPendingLayout();
        ScheduleFrameResizeEvent();
    }

    private void ScheduleFrameResizeEvent()
    {
        if (!s_enableFrameResizeCoalescing)
        {
            DispatchWindowLifecycleEvent("resize");
            return;
        }

        _pendingFrameResizeEvent?.Dispose();
        _pendingFrameResizeEvent = Host.Services.Dispatcher.Schedule(
            s_frameResizeQuietPeriod,
            DeliverFrameResizeEvent,
            HtmlMlDispatchPriority.Background);
    }

    private void DeliverFrameResizeEvent()
    {
        _pendingFrameResizeEvent = null;
        if (Host.IsDisposed)
        {
            return;
        }
        if (Host.IsExecutingJavaScript)
        {
            ScheduleFrameResizeEvent();
            return;
        }

        DispatchWindowLifecycleEvent("resize");
    }

    internal bool FlushPendingFrameResizeEvent()
    {
        if (_pendingFrameResizeEvent is null || Host.IsDisposed || Host.IsExecutingJavaScript)
        {
            return false;
        }

        _pendingFrameResizeEvent.Dispose();
        _pendingFrameResizeEvent = null;
        DispatchWindowLifecycleEvent("resize");
        return true;
    }

    private bool EnsureViewportLayoutContract()
    {
        var changed = false;
        var full = new CssLength(100, CssLengthUnit.Percent);
        if (CssLayout.GetWidth(_root) != full)
        {
            CssLayout.SetWidth(_root, full);
            changed = true;
        }
        if (CssLayout.GetHeight(_root) != full)
        {
            CssLayout.SetHeight(_root, full);
            changed = true;
        }
        if (_root.IsSet(Control.WidthProperty))
        {
            _root.ClearValue(Control.WidthProperty);
            changed = true;
        }
        if (_root.IsSet(Control.HeightProperty))
        {
            _root.ClearValue(Control.HeightProperty);
            changed = true;
        }
        return changed;
    }

    private void InvalidateViewportLayout()
    {
        _root.InvalidateMeasure();
        _root.InvalidateArrange();
        if (_root.Parent is Control parent)
        {
            parent.InvalidateMeasure();
            parent.InvalidateArrange();
        }
    }

}

internal readonly struct EventListenerOptions
{
    public EventListenerOptions(bool capture, bool once, bool passive)
    {
        Capture = capture;
        Once = once;
        Passive = passive;
    }

    public bool Capture { get; }

    public bool Once { get; }

    public bool Passive { get; }

}

internal sealed class DomEventRegistration
{
    public DomEventRegistration(IExternalDomEventListener callback, EventListenerOptions options)
    {
        ExternalCallback = callback;
        Options = options;
    }

    public IExternalDomEventListener ExternalCallback { get; }

    public EventListenerOptions Options { get; }

    public bool Matches(IExternalDomEventListener callback, bool capture)
        => ReferenceEquals(ExternalCallback, callback) && Options.Capture == capture;
}


public class AvaloniaDomElement :
    DomElementCore,
    IHtmlMlDomRectTarget,
    IHtmlMlDomClientRectsTarget,
    IHtmlMlDomNumericTarget,
    IHtmlMlDomIdentityTarget,
    IDomContainerElement<AvaloniaDomElement>,
    ICssSelectorNode
{
    private const int MaximumNativePropertyCacheEntries = 512;
    private static readonly AttachedProperty<CssLength?>[] s_cssLayoutLengthProperties =
    [
        CssLayout.LeftProperty, CssLayout.TopProperty, CssLayout.RightProperty, CssLayout.BottomProperty,
        CssLayout.WidthProperty, CssLayout.HeightProperty,
        CssLayout.MinWidthProperty, CssLayout.MinHeightProperty,
        CssLayout.MaxWidthProperty, CssLayout.MaxHeightProperty,
        CssLayout.FlexBasisProperty, CssLayout.RowGapProperty, CssLayout.ColumnGapProperty,
        CssLayout.MarginTopProperty, CssLayout.MarginRightProperty,
        CssLayout.MarginBottomProperty, CssLayout.MarginLeftProperty,
        CssLayout.PaddingTopProperty, CssLayout.PaddingRightProperty,
        CssLayout.PaddingBottomProperty, CssLayout.PaddingLeftProperty
    ];
    private static readonly string[] s_cssLayoutLengthPropertyNames =
    [
        "left", "top", "right", "bottom", "width", "height",
        "min-width", "min-height", "max-width", "max-height",
        "flex-basis", "row-gap", "column-gap",
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left"
    ];
    private static readonly bool s_disableStyleReadScope =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_STYLE_READ_SCOPE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableLayoutReadFastPaths =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_LAYOUT_READ_FAST_PATHS"), "1", StringComparison.Ordinal);
    private static readonly bool s_disablePresentationChangeSet =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_PRESENTATION_CHANGE_SET"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableViewportLayoutInvalidationCoalescing =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_VIEWPORT_LAYOUT_INVALIDATION_COALESCING"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableViewportLayoutChangeSet =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_VIEWPORT_LAYOUT_CHANGE_SET"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableComputedLayoutChangeSet =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_COMPUTED_LAYOUT_CHANGE_SET"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableInlineStyleSpanParser =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_INLINE_STYLE_SPAN_PARSER"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableStyleSerializationBuilder =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_STYLE_SERIALIZATION_BUILDER"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableInlinePresentationBatching =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_INLINE_PRESENTATION_BATCHING"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableRedundantInlineStyleWriteSuppression =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_REDUNDANT_INLINE_STYLE_WRITE_SUPPRESSION"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableNativePropertyCache =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_CSS_NATIVE_PROPERTY_CACHE"), "1", StringComparison.Ordinal);
    private static readonly object s_nativePropertyCacheLock = new();
    private static readonly Dictionary<(Type ControlType, string PropertyName), AvaloniaProperty?> s_nativePropertyCache = new();
    private static readonly Dictionary<(Type ControlType, string PropertyName), PropertyInfo?> s_clrPropertyCache = new();

    private readonly Dictionary<string, List<DomEventRegistration>> _eventListeners = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, ClrEventBridge> _clrEventBridges = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> s_builtinEventNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "click",
        "auxclick",
        "contextmenu",
        "focus",
        "blur",
        "mousedown",
        "mousemove",
        "mouseup",
        "wheel",
        "mouseover",
        "mouseout",
        "mouseenter",
        "mouseleave",
        "pointerdown",
        "pointermove",
        "pointerup",
        "pointerenter",
        "pointerleave",
        "keydown",
        "keyup",
        "textinput",
        "input"
    };

    private sealed record ClrEventBridge(EventInfo EventInfo, Delegate Handler, string EventName);

    private readonly Dictionary<string, string?> _styleValues = new(CssPropertyNameComparer.Instance);
    private readonly HashSet<string> _importantStyleProperties = new(CssPropertyNameComparer.Instance);
    private long _svgSceneRevision;
    private readonly Dictionary<string, NativePresentationValue> _nativePresentationValues = new(StringComparer.OrdinalIgnoreCase);
    private CssComputedValues _computedStyleValues = CssComputedValues.Empty;
    private CssDeclaredPropertySet _declaredStyleProperties = CssDeclaredPropertySet.Empty;
    private CssComputedStyle? _computedStyleSnapshot;
    private long _computedStyleSnapshotGeneration = long.MinValue;
    private ComputedStyleSnapshotState? _computedStyleSnapshotState;
    private DomStringMap? _dataset;
    private CssStyleDeclaration? _style;
    private DomTokenList? _classList;
    private bool _suppressStyleMutation;
    private System.Threading.Timer? _searchInputDebounceTimer;
    private int _searchInputDebounceVersion;
    private string _pendingSearchInputText = string.Empty;
    private int _styleReadScopeDepth;
    private WeakReference<Control>? _cachedScrollOwner;
    private WeakReference<IScrollable>? _cachedScrollable;
    private double _scrollLeft;
    private double _scrollTop;
    private bool _pointerHandlersAttached;
    private bool _pointerOverHandlersAttached;
    private bool _wheelHandlersAttached;
    private bool _keyboardHandlersAttached;
    private bool _textInputHandlersAttached;
    private bool _clickHandlersAttached;
    private bool _suppressNextNativeButtonClick;
    private bool _focusHandlersAttached;
    private List<IExternalJavaScriptCallback>? _externalResizeObserverCallbacks;
    private bool _resizeObserverSubscriptionAttached;
    private bool _resizeObserverDeliveryPending;
    private Size? _lastResizeObserverSize;
    private bool _percentageTransformResizeAttached;
    private string? _cssTransformValue;
    private CssRotateTransition? _cssRotateTransition;
    private CssMatrixTransition? _cssMatrixTransition;
    private HtmlMlFrameRequest _cssTransformFrameRequest;
    private CssScalarTransition? _cssOpacityTransition;
    private CssColorTransition? _cssColorTransition;
    private HtmlMlFrameRequest _cssPaintFrameRequest;
    private double? _cssPresentedOpacity;
    private Color? _cssPresentedColor;
    private bool _hasComputedPresentation;
    private bool _paintSuppressedUntilStyleCommit;
    private string? _nodeNameOverride;
    private string? _tagNameCache;
    private string? _namespaceUri;
    private bool _xmlMode;
    private readonly Dictionary<string, string?> _xmlAttributes = new(StringComparer.Ordinal);
    private string _selectionDirection = "none";
    private bool _checked;
    private bool _selected;
    private bool _selectionExplicitlyEmpty;
    private bool _hasExplicitValueState;
    private string _explicitValueState = string.Empty;
    private readonly record struct NativePresentationValue(AvaloniaProperty Property, bool WasSet, object? Value);
    private AvaloniaDomDocument? _contentDocument;
    private object? _externalContentWindow;
    private IExternalVirtualBrowsingContext? _externalContentWindowRuntime;
    private bool _contentInitializationScheduled;
    private bool _contentInitializationCompleted;
    private object? _domParentOverride;

    private enum LengthAxis
    {
        Horizontal,
        Vertical,
        Both
    }

    private enum BoxSide
    {
        Top,
        Right,
        Bottom,
        Left
    }

    protected AvaloniaBrowserHost Host { get; }


    protected AvaloniaDomDocument OwnerDocument { get; }

    AvaloniaDomElement? IDomContainerElement<AvaloniaDomElement>.FirstElementChild => firstElementChild;
    AvaloniaDomElement? IDomContainerElement<AvaloniaDomElement>.FirstChild => firstChild;
    object[] IDomContainerElement<AvaloniaDomElement>.ChildNodes => childNodes;
    object[] IDomContainerElement<AvaloniaDomElement>.Children => children;
    int IDomContainerElement<AvaloniaDomElement>.ChildElementCount => childElementCount;
    string IDomContainerElement<AvaloniaDomElement>.Id => id;
    string IDomContainerElement<AvaloniaDomElement>.TagName => tagName;
    object? IDomContainerElement<AvaloniaDomElement>.QuerySelector(string selector) => querySelector(selector);
    object[] IDomContainerElement<AvaloniaDomElement>.QuerySelectorAll(string selector) => querySelectorAll(selector);
    object[] IDomContainerElement<AvaloniaDomElement>.GetElementsByTagName(string tagName) => getElementsByTagName(tagName);
    AvaloniaDomElement? IDomContainerElement<AvaloniaDomElement>.AppendChild(AvaloniaDomElement child) => appendChild(child);
    AvaloniaDomElement? IDomContainerElement<AvaloniaDomElement>.RemoveChild(AvaloniaDomElement child) => removeChild(child);

    string ICssSelectorNode.TagName => tagName;
    string ICssSelectorNode.Id => id;
    string ICssSelectorNode.TextContent => textContent ?? string.Empty;
    int ICssSelectorNode.ChildElementCount => childElementCount;
    bool ICssSelectorNode.IsDocumentElement => false;
    ICssSelectorNode? ICssSelectorNode.ParentElement
        => string.Equals(tagName, "BODY", StringComparison.OrdinalIgnoreCase)
            ? OwnerDocument.documentElement
            : parentElement;
    ICssSelectorNode? ICssSelectorNode.PreviousElementSibling => previousElementSibling;
    ICssSelectorNode? ICssSelectorNode.NextElementSibling => nextElementSibling;
    bool ICssSelectorNode.HasClass(string className) => Control.Classes.Contains(className);
    string? ICssSelectorNode.GetAttribute(string name) => getAttribute(name);
    bool ICssSelectorNode.HasState(CssSelectorState state)
        => state switch
        {
            CssSelectorState.Hover => OwnerDocument.IsPointerHovered(this),
            CssSelectorState.Active => Control.Classes.Contains(":pressed"),
            CssSelectorState.Focus => Control.IsFocused,
            CssSelectorState.FocusVisible => Control.Classes.Contains(":focus-visible"),
            CssSelectorState.Disabled => SupportsDisabledState && !Control.IsEnabled,
            CssSelectorState.Checked => MatchesCheckedPseudoClass,
            CssSelectorState.None => true,
            _ => false
        };

    public object? onload { get; set; }

    public object? onerror { get; set; }

    public Control Control { get; }

    /// <summary>
    /// Framework-neutral identity used by portable runtime contracts. The Avalonia
    /// control remains available for source compatibility but is carried across the
    /// backend seam only through this opaque handle.
    /// </summary>
    public AvaloniaDomElement(AvaloniaBrowserHost host, Control control)
        : this(host, host.Document, control)
    {
    }

    public AvaloniaDomElement(AvaloniaBrowserHost host, AvaloniaDomDocument ownerDocument, Control control)
        : base(HtmlMlBackendHandle.Create(control))
    {
        Host = host ?? throw new ArgumentNullException(nameof(host));
        OwnerDocument = ownerDocument ?? throw new ArgumentNullException(nameof(ownerDocument));
        Control = control ?? throw new ArgumentNullException(nameof(control));
        // Native controls supplied by an application historically exposed
        // Avalonia's x:Name as their initial DOM id. Capture that compatibility
        // value once, then keep browser-authored id/name mutations in DOM
        // storage: StyledElement.Name becomes immutable after styling and is
        // not the HTML name attribute in any case.
        if (Control is StyledElement { Name.Length: > 0 } styled)
        {
            SetGenericAttribute("id", styled.Name);
            SetAttributePresence("id", present: true);
        }
        if (Control is DomTextInputControl textInput)
        {
            textInput.BeforeNativeKeyDown = DispatchTextInputKeyDown;
            textInput.NativeKeyEditCompleted = DispatchNativeKeyInput;
        }
        EnsureEventBridges();
    }

    public virtual int nodeType => 1;

    public virtual string nodeName
    {
        get
        {
            var name = _nodeNameOverride ?? Control.GetType().Name;
            if (string.IsNullOrEmpty(name)) return string.Empty;
            return _xmlMode ? name : IsSvgNamespace ? name.ToLowerInvariant() : name.ToUpperInvariant();
        }
    }

    public string localName
        => _xmlMode
            ? _nodeNameOverride ?? Control.GetType().Name
            : (_nodeNameOverride ?? Control.GetType().Name).ToLowerInvariant();

    public string? namespaceURI => _namespaceUri;

    private bool IsSvgNamespace
        => string.Equals(_namespaceUri, "http://www.w3.org/2000/svg", StringComparison.OrdinalIgnoreCase);

    public virtual string? nodeValue
    {
        get => null;
        set { }
    }

    public AvaloniaDomDocument ownerDocument => OwnerDocument;

    public object? parentNode => _domParentOverride ?? parentElement;

    public virtual bool isConnected => _domParentOverride is not null || Control.GetVisualRoot() is not null;

    internal void SetDomParent(object? parent) => _domParentOverride = parent;

    public DomTokenList classList => _classList ??= new DomTokenList(this);

    public CssStyleDeclaration style => _style ??= new CssStyleDeclaration(this);

    public virtual string className
    {
        get => getAttribute("class") ?? string.Empty;
        set => setAttribute("class", value);
    }

    public virtual string? href
    {
        get => _attributes.TryGetValue("href", out var value) ? value : null;
        set => setAttribute("href", value);
    }

    public virtual string hash
    {
        get
        {
            var value = getAttribute("href") ?? string.Empty;
            var fragment = value.IndexOf('#');
            return fragment >= 0 ? value[fragment..] : string.Empty;
        }
        set
        {
            var hrefValue = getAttribute("href") ?? string.Empty;
            var fragment = hrefValue.IndexOf('#');
            var prefix = fragment >= 0 ? hrefValue[..fragment] : hrefValue;
            var next = value ?? string.Empty;
            if (next.Length > 0 && next[0] != '#')
            {
                next = "#" + next;
            }
            setAttribute("href", prefix + next);
        }
    }

    public virtual string download
    {
        get => getAttribute("download") ?? string.Empty;
        set => setAttribute("download", value ?? string.Empty);
    }

    public virtual string src
    {
        get => _attributes.TryGetValue("src", out var value) ? value ?? string.Empty : string.Empty;
        set => setAttribute("src", value);
    }

    public bool async { get; set; }

    public string charset { get; set; } = string.Empty;

    public string nonce
    {
        get => getAttribute("nonce") ?? string.Empty;
        set => setAttribute("nonce", value);
    }

    public virtual string? rel
    {
        get => _attributes.TryGetValue("rel", out var value) ? value : null;
        set => setAttribute("rel", value);
    }

    public virtual string? media
    {
        get => _attributes.TryGetValue("media", out var value) ? value : null;
        set => setAttribute("media", value);
    }

    public virtual string? type
    {
        get
        {
            if (_attributes.TryGetValue("type", out var value)) return value;
            if (string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase))
            {
                return HasAttributePresence("multiple") ? "select-multiple" : "select-one";
            }
            return string.Equals(localName, "input", StringComparison.OrdinalIgnoreCase)
                ? "text"
                : null;
        }
        set => setAttribute("type", value);
    }

    /// <summary>
    /// HTML boolean disabled state. The attribute's presence, not its text,
    /// controls the reflected property.
    /// </summary>
    public virtual bool disabled
    {
        get => HasAttributePresence("disabled");
        set => setAttribute("disabled", value ? string.Empty : null);
    }

    internal bool SupportsDisabledState
        => localName.Equals("button", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("fieldset", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("input", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("optgroup", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("option", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("select", StringComparison.OrdinalIgnoreCase)
           || localName.Equals("textarea", StringComparison.OrdinalIgnoreCase);

    internal bool IsDisabledFormControl => SupportsDisabledState && disabled;

    internal bool IsInDisabledFormControl()
    {
        for (AvaloniaDomElement? current = this; current is not null; current = current.parentElement)
        {
            if (current.IsDisabledFormControl)
            {
                return true;
            }
        }
        return false;
    }

    public virtual bool @checked
    {
        get => _checked;
        set
        {
            if (_checked == value)
            {
                return;
            }
            _checked = value;
            OwnerDocument.NotifyDynamicStateChanged(this);
        }
    }

    internal bool MatchesCheckedPseudoClass
    {
        get
        {
            if (string.Equals(localName, "input", StringComparison.OrdinalIgnoreCase))
            {
                var inputType = type?.Trim();
                return _checked
                       && (string.Equals(inputType, "checkbox", StringComparison.OrdinalIgnoreCase)
                           || string.Equals(inputType, "radio", StringComparison.OrdinalIgnoreCase));
            }
            return string.Equals(localName, "option", StringComparison.OrdinalIgnoreCase) && selected;
        }
    }

    public virtual bool selected
    {
        get
        {
            if (_selected || !string.Equals(localName, "option", StringComparison.OrdinalIgnoreCase))
            {
                return _selected;
            }

            var select = parentElement;
            while (select is not null
                   && !string.Equals(select.localName, "select", StringComparison.OrdinalIgnoreCase))
            {
                select = select.parentElement;
            }
            if (select is null)
            {
                return false;
            }

            if (select._selectionExplicitlyEmpty)
            {
                return false;
            }

            var options = select.options;
            return options.Length > 0
                   && ReferenceEquals(options[0], this)
                   && !options.Any(option => option._selected);
        }
        set
        {
            if (_selected == value)
            {
                return;
            }

            _selected = value;
            if (value && string.Equals(localName, "option", StringComparison.OrdinalIgnoreCase))
            {
                var select = parentElement;
                while (select is not null
                       && !string.Equals(select.localName, "select", StringComparison.OrdinalIgnoreCase))
                {
                    select = select.parentElement;
                }
                if (select is not null && !select.multiple)
                {
                    select._selectionExplicitlyEmpty = false;
                    foreach (var option in select.querySelectorAll("option").OfType<AvaloniaDomElement>())
                    {
                        if (!ReferenceEquals(option, this) && option._selected)
                        {
                            option._selected = false;
                            OwnerDocument.NotifyDynamicStateChanged(option);
                        }
                    }
                }
            }
            OwnerDocument.NotifyDynamicStateChanged(this);
        }
    }

    public virtual bool multiple
    {
        get => HasAttributePresence("multiple");
        set => setAttribute("multiple", value ? string.Empty : null);
    }

    public virtual void click() => OwnerDocument.DispatchProgrammaticClick(this);

    public virtual string id
    {
        get => getAttribute("id") ?? string.Empty;
        set => setAttribute("id", value);
    }

    public virtual string dir
    {
        get => getAttribute("dir") ?? string.Empty;
        set => setAttribute("dir", value ?? string.Empty);
    }

    public virtual string contentEditable
    {
        get => getAttribute("contenteditable") ?? "inherit";
        set => setAttribute("contenteditable", value ?? "inherit");
    }

    public virtual string name
    {
        get => getAttribute("name") ?? string.Empty;
        set => setAttribute("name", value);
    }

    public virtual string htmlFor
    {
        get => getAttribute("for") ?? string.Empty;
        set => setAttribute("for", value);
    }

    public virtual string unselectable { get; set; } = string.Empty;

    public object currentStyle => style;

    public virtual object? getContext(string type)
    {
        ApplyCanvasPositioning();

        var context = CanvasContextBridge.GetContext(Control, type);
        if (context is CanvasRenderingContext2D canvasContext)
        {
            canvasContext.canvas = this;
        }
        else if (context is CanvasWebGlRenderingContext webGlContext)
        {
            webGlContext.canvas = this;
        }

        return context;
    }

    public virtual object? getContext(string type, object? options) => getContext(type);

    public string __htmlMlCanvasToDataURL(string? type = null, double? quality = null)
    {
        if (!IsExplicitHtmlCanvasElement())
        {
            throw new InvalidOperationException("toDataURL is only available on canvas elements.");
        }

        _ = quality;
        if (!string.IsNullOrWhiteSpace(type)
            && !string.Equals(type, "image/png", StringComparison.OrdinalIgnoreCase))
        {
            // PNG is the required browser fallback when the requested encoder
            // is unavailable.
            type = "image/png";
        }

        _ = CanvasContextBridge.GetContext(Control, "2d");
        var surface = CanvasContextBridge.TryGetSurface(Control, out var canvasSurface)
            ? canvasSurface
            : null;
        if (surface is null)
        {
            return "data:image/png;base64,";
        }

        return "data:image/png;base64," + Convert.ToBase64String(surface.CapturePng());
    }

    // Internal bridge used by the ResizeObserver shim. The callback is invoked
    // after Avalonia has processed a bounds/size mutation, instead of relying
    // solely on a compositor frame being scheduled.
    public void __htmlMlObserveResize(object callback)
    {
        var externalCallback = callback as IExternalJavaScriptCallback
                               ?? Host.ExternalCallbackAdapter?.GetCallback(callback, create: true);
        if (externalCallback is null)
        {
            return;
        }

        _externalResizeObserverCallbacks ??= new List<IExternalJavaScriptCallback>();
        if (!_externalResizeObserverCallbacks.Contains(externalCallback))
        {
            _externalResizeObserverCallbacks.Add(externalCallback);
            _lastResizeObserverSize = null;
            EnsureResizeObserverSubscription();
        }
    }

    public void __htmlMlUnobserveResize(object callback)
    {
        var externalCallback = callback as IExternalJavaScriptCallback
                               ?? Host.ExternalCallbackAdapter?.GetCallback(callback, create: false);
        if (externalCallback is null || _externalResizeObserverCallbacks is null)
        {
            return;
        }

        _externalResizeObserverCallbacks.Remove(externalCallback);
        DetachResizeObserverSubscriptionIfUnused();
    }

    private void EnsureResizeObserverSubscription()
    {
        if (!_resizeObserverSubscriptionAttached)
        {
            _resizeObserverSubscriptionAttached = true;
            Control.PropertyChanged += OnResizeObservedPropertyChanged;
        }

        ScheduleResizeObserverDelivery();
    }

    private void ScheduleResizeObserverDelivery()
    {
        if (!_resizeObserverDeliveryPending)
        {
            _resizeObserverDeliveryPending = true;
            OwnerDocument.ScheduleResizeObserverDelivery(this);
        }
    }

    private void DetachResizeObserverSubscriptionIfUnused()
    {
        if (_externalResizeObserverCallbacks is { Count: > 0 }
            || !_resizeObserverSubscriptionAttached)
        {
            return;
        }

        Control.PropertyChanged -= OnResizeObservedPropertyChanged;
        _resizeObserverSubscriptionAttached = false;
        _resizeObserverDeliveryPending = false;
        _lastResizeObserverSize = null;
    }

    private void OnResizeObservedPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property != Visual.BoundsProperty &&
            e.Property != Control.WidthProperty &&
            e.Property != Control.HeightProperty)
        {
            return;
        }

        ScheduleResizeObserverDelivery();
    }

    internal void DeliverResizeObserverTargetNow()
    {
        if (!_resizeObserverDeliveryPending)
        {
            return;
        }

        _resizeObserverDeliveryPending = false;
        if (_externalResizeObserverCallbacks is not { Count: > 0 })
        {
            return;
        }

        // ResizeObserver reports content-box changes, not every intermediate
        // Width/Height/Bounds property notification produced by a layout pass.
        // Suppressing equal-size deliveries prevents observer-driven resize
        // feedback loops while preserving the required initial notification.
        var currentSize = GetClientSize();
        if (_lastResizeObserverSize is { } previousSize
            && Math.Abs(previousSize.Width - currentSize.Width) < 0.01
            && Math.Abs(previousSize.Height - currentSize.Height) < 0.01)
        {
            return;
        }
        _lastResizeObserverSize = currentSize;

        if (_externalResizeObserverCallbacks is not { Count: > 0 } externalCallbacks)
        {
            return;
        }

        OwnerDocument.EnqueueResizeObserverCallbacks(externalCallbacks);
    }

    public DomStringMap dataset => _dataset ??= new DomStringMap(this);

    internal IReadOnlyDictionary<string, string?> DataAttributes => _dataAttributes;

    internal IReadOnlyDictionary<string, string?> StyleValues => _styleValues;

    internal bool IsInlineStyleImportant(string propertyName)
        => _importantStyleProperties.Contains(propertyName);

    internal bool TryGetSvgPresentationAttribute(string name, out string value)
    {
        if (IsSvgNamespace
            && _attributes.TryGetValue(name, out var authored)
            && !string.IsNullOrWhiteSpace(authored))
        {
            value = authored;
            return true;
        }

        value = string.Empty;
        return false;
    }

    internal IReadOnlyDictionary<string, string> ComputedStyleValues => _computedStyleValues;

    // Captured while resolving the cascade so inherited-property fast paths can
    // distinguish a genuinely inherited cursor from an authored declaration
    // whose computed value merely equals its parent's value.
    internal bool HasOwnCursorDeclaration { get; set; }

    internal IReadOnlySet<string> DeclaredStyleProperties => _declaredStyleProperties;

    internal CssPropertyValueStore ComputedOrdinaryStyleStorage => _computedStyleValues.OrdinaryValues;

    internal CssPropertyNameSet DeclaredOrdinaryStyleStorage => _declaredStyleProperties.OrdinaryProperties;

    internal CssCustomPropertyMap ComputedCustomProperties => _computedStyleValues.CustomProperties;

    internal bool TryPlanCustomPropertyRebase(
        CssCustomPropertyMap oldInherited,
        CssCustomPropertyMap newInherited,
        out CssCustomPropertyMap computed,
        out CssCustomPropertyMap declared)
    {
        if (!_computedStyleValues.CustomProperties.TryRebaseOnto(oldInherited, newInherited, out computed)
            || !_declaredStyleProperties.CustomProperties.TryRebaseOnto(oldInherited, newInherited, out declared))
        {
            computed = _computedStyleValues.CustomProperties;
            declared = _declaredStyleProperties.CustomProperties;
            return false;
        }

        return true;
    }

    internal void ApplyCustomPropertyRebase(
        CssCustomPropertyMap computed,
        CssCustomPropertyMap declared)
    {
        _computedStyleValues = new CssComputedValues(
            _computedStyleValues.OrdinaryValues,
            computed);
        _declaredStyleProperties = new CssDeclaredPropertySet(
            _declaredStyleProperties.OrdinaryProperties,
            declared);
    }

    internal void ApplyInheritedCursorRebase(string cursor)
    {
        var ordinaryValues = _computedStyleValues.OrdinaryValues.Clone();
        ordinaryValues["cursor"] = cursor;
        _computedStyleValues = new CssComputedValues(
            ordinaryValues,
            _computedStyleValues.CustomProperties);
        ApplyCursor(cursor);
    }

    [Flags]
    private enum CssPresentationChanges
    {
        None = 0,
        Canvas = 1 << 0,
        Cursor = 1 << 1,
        Opacity = 1 << 2,
        Background = 1 << 3,
        Color = 1 << 4,
        FontFamily = 1 << 5,
        FontSize = 1 << 6,
        FontStyle = 1 << 7,
        FontWeight = 1 << 8,
        TextAlignment = 1 << 9,
        Padding = 1 << 10,
        WhiteSpace = 1 << 11,
        Display = 1 << 12,
        Border = 1 << 13,
        TextTransform = 1 << 14,
        LetterSpacing = 1 << 15,
        LineHeight = 1 << 16,
        SvgPaint = 1 << 17,
        Outline = 1 << 18,
        WordSpacing = 1 << 19,
        FontSizeOrDisplay = FontSize | Display,
        ComputedAll = Opacity | Background | Color | FontFamily | FontSize | FontStyle |
                      FontWeight | TextAlignment | Padding | WhiteSpace | Display | Border |
                      TextTransform | LetterSpacing | WordSpacing | LineHeight | SvgPaint | Outline,
        All = Canvas | Cursor | ComputedAll
    }

    private static readonly string[] s_paddingPresentationProperties =
        ["padding", "padding-top", "padding-right", "padding-bottom", "padding-left"];

    private static readonly string[] s_borderPresentationProperties =
    [
        "border", "border-width", "border-color", "border-style",
        "border-top", "border-right", "border-bottom", "border-left",
        "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
        "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
        "border-radius", "border-top-left-radius", "border-top-right-radius",
        "border-bottom-right-radius", "border-bottom-left-radius"
    ];

    private static readonly string[] s_outlinePresentationProperties =
        ["outline", "outline-color", "outline-style", "outline-width", "outline-offset"];

    private static readonly string[] s_svgPaintPresentationProperties =
    [
        "clip-rule", "fill", "fill-opacity", "fill-rule", "stroke", "stroke-linecap",
        "stroke-linejoin", "stroke-opacity", "stroke-width"
    ];

    private static CssPresentationChanges GetPresentationChanges(
        CssComputedValues previousValues,
        CssComputedValues currentValues,
        CssDeclaredPropertySet previousDeclarations,
        CssDeclaredPropertySet currentDeclarations)
    {
        // CssLayoutPanel consumes more properties than the native presentation
        // subset below, including properties added over time. Preserve the
        // existing conservative layout invalidation for every ordinary change;
        // the fine-grained mask is used to avoid unrelated brush/font/padding/
        // cursor work without risking a stale layout property.
        var changes = previousValues.OrdinaryContentEquals(currentValues)
                      && previousDeclarations.OrdinaryContentEquals(currentDeclarations)
            ? CssPresentationChanges.None
            : CssPresentationChanges.Canvas;

        if (PropertyStateChanged("cursor", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.Cursor;
        if (PropertyStateChanged("visibility", previousValues, currentValues, previousDeclarations, currentDeclarations)
            || PropertyStateChanged("opacity", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.Opacity;
        if (PropertyStateChanged("background-color", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.Background;
        if (PropertyStateChanged("color", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.Color;
        if (PropertyStateChanged("font-family", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.FontFamily;
        if (PropertyStateChanged("font-size", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.FontSize;
        if (PropertyStateChanged("font-style", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.FontStyle;
        if (PropertyStateChanged("font-weight", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.FontWeight;
        if (PropertyStateChanged("text-align", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.TextAlignment;
        if (AnyPropertyStateChanged(
                s_paddingPresentationProperties,
                previousValues,
                currentValues,
                previousDeclarations,
                currentDeclarations))
            changes |= CssPresentationChanges.Padding;
        if (PropertyStateChanged("white-space", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.WhiteSpace;
        if (PropertyStateChanged("text-transform", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.TextTransform;
        if (PropertyStateChanged("letter-spacing", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.LetterSpacing;
        if (PropertyStateChanged("word-spacing", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.WordSpacing;
        if (PropertyStateChanged("line-height", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.LineHeight;
        if (PropertyStateChanged("display", previousValues, currentValues, previousDeclarations, currentDeclarations))
            changes |= CssPresentationChanges.Display;
        if (AnyPropertyStateChanged(
                s_borderPresentationProperties,
                previousValues,
                currentValues,
                previousDeclarations,
                currentDeclarations))
            changes |= CssPresentationChanges.Border;
        if (AnyPropertyStateChanged(
                s_outlinePresentationProperties,
                previousValues,
                currentValues,
                previousDeclarations,
                currentDeclarations))
            changes |= CssPresentationChanges.Outline;
        if (AnyPropertyStateChanged(
                s_svgPaintPresentationProperties,
                previousValues,
                currentValues,
                previousDeclarations,
                currentDeclarations))
            changes |= CssPresentationChanges.SvgPaint;
        return changes;
    }

    private static bool AnyPropertyStateChanged(
        string[] properties,
        CssComputedValues previousValues,
        CssComputedValues currentValues,
        CssDeclaredPropertySet previousDeclarations,
        CssDeclaredPropertySet currentDeclarations)
    {
        foreach (var property in properties)
        {
            if (PropertyStateChanged(
                    property,
                    previousValues,
                    currentValues,
                    previousDeclarations,
                    currentDeclarations))
            {
                return true;
            }
        }

        return false;
    }

    private static bool PropertyStateChanged(
        string property,
        CssComputedValues previousValues,
        CssComputedValues currentValues,
        CssDeclaredPropertySet previousDeclarations,
        CssDeclaredPropertySet currentDeclarations)
    {
        var hadPrevious = previousValues.TryGetValue(property, out var previousValue);
        var hasCurrent = currentValues.TryGetValue(property, out var currentValue);
        return hadPrevious != hasCurrent
               || (hadPrevious && !string.Equals(previousValue, currentValue, StringComparison.Ordinal))
               || previousDeclarations.Contains(property) != currentDeclarations.Contains(property);
    }

    // The cascade produces fresh collections for one element. This method takes
    // ownership when the result changed; copying these collections would
    // duplicate every inherited custom property for no semantic benefit.
    internal bool SetComputedStyleValues(CssComputedValues values, CssDeclaredPropertySet declaredProperties)
        => SetComputedStyleValues(
            values,
            declaredProperties,
            valuesFromScratch: false,
            declaredPropertiesFromScratch: false,
            usePresentationChangeSet: false,
            out _,
            out _);

    // Reusable candidate storage never becomes element-owned. An unchanged
    // candidate, or one whose only change is its immutable custom-property
    // maps, can return directly to the cascade; a changed ordinary candidate
    // is copied into fresh, capacity-equivalent element storage first.
    internal bool SetComputedStyleValues(
        CssComputedValues values,
        CssDeclaredPropertySet declaredProperties,
        bool valuesFromScratch,
        bool declaredPropertiesFromScratch,
        bool usePresentationChangeSet,
        out CssPropertyValueStore? recyclableValues,
        out CssPropertyNameSet? recyclableDeclaredProperties)
    {
        var ordinaryValuesEqual = _computedStyleValues.OrdinaryContentEquals(values);
        var ordinaryDeclarationsEqual = _declaredStyleProperties.OrdinaryContentEquals(declaredProperties);
        var allValuesEqual = ordinaryValuesEqual
                             && ordinaryDeclarationsEqual
                             && _computedStyleValues.CustomProperties.ContentEquals(values.CustomProperties)
                             && _declaredStyleProperties.CustomProperties.ContentEquals(declaredProperties.CustomProperties);
        var forcePresentation = OwnerDocument.ForceElementPresentationApply;
        if (allValuesEqual && !forcePresentation)
        {
            RestorePaintAfterFirstStyleCommit(values);
            recyclableValues = values.OrdinaryValues.IsFrozen ? null : values.OrdinaryValues;
            recyclableDeclaredProperties = declaredProperties.OrdinaryProperties.IsFrozen
                ? null
                : declaredProperties.OrdinaryProperties;
            return false;
        }

        if (allValuesEqual)
        {
            ReapplyComputedPresentation();
            RestorePaintAfterFirstStyleCommit(values);
            recyclableValues = values.OrdinaryValues.IsFrozen ? null : values.OrdinaryValues;
            recyclableDeclaredProperties = declaredProperties.OrdinaryProperties.IsFrozen
                ? null
                : declaredProperties.OrdinaryProperties;
            return true;
        }

        var presentationChanges = !usePresentationChangeSet || s_disablePresentationChangeSet
            ? ordinaryValuesEqual && ordinaryDeclarationsEqual
                ? CssPresentationChanges.None
                : CssPresentationChanges.All
            : GetPresentationChanges(
                _computedStyleValues,
                values,
                _declaredStyleProperties,
                declaredProperties);
        var candidateValues = values;
        var candidateDeclaredProperties = declaredProperties;
        if (ordinaryValuesEqual)
        {
            values = new CssComputedValues(
                _computedStyleValues.OrdinaryValues,
                values.CustomProperties);
        }
        else if (valuesFromScratch)
        {
            var ownedValues = values.OrdinaryValues.Clone(frozen: true);
            values = new CssComputedValues(ownedValues, values.CustomProperties);
        }
        else
        {
            values.OrdinaryValues.Freeze();
        }
        if (ordinaryDeclarationsEqual)
        {
            declaredProperties = new CssDeclaredPropertySet(
                _declaredStyleProperties.OrdinaryProperties,
                declaredProperties.CustomProperties);
        }
        else if (declaredPropertiesFromScratch)
        {
            var ownedDeclaredProperties = declaredProperties.OrdinaryProperties.Clone(frozen: true);
            declaredProperties = new CssDeclaredPropertySet(
                ownedDeclaredProperties,
                declaredProperties.CustomProperties);
        }
        else
        {
            declaredProperties.OrdinaryProperties.Freeze();
        }

        _computedStyleValues = values;
        _declaredStyleProperties = declaredProperties;
        if (forcePresentation)
        {
            ReapplyComputedPresentation();
        }
        else if (presentationChanges != CssPresentationChanges.None)
        {
            OwnerDocument.RecordElementPresentationApply();
            if ((presentationChanges & CssPresentationChanges.Canvas) != 0)
            {
                ApplyCanvasPositioning(useLayoutChangeSet: !s_disableComputedLayoutChangeSet);
            }
            ApplyComputedPresentation(values, presentationChanges);
            if ((presentationChanges & CssPresentationChanges.Cursor) != 0)
            {
                ApplyCursor(GetStyleValue("cursor"));
            }
        }
        RestorePaintAfterFirstStyleCommit(values);
        recyclableValues = ReferenceEquals(candidateValues.OrdinaryValues, values.OrdinaryValues)
            ? null
            : candidateValues.OrdinaryValues;
        recyclableDeclaredProperties = ReferenceEquals(
            candidateDeclaredProperties.OrdinaryProperties,
            declaredProperties.OrdinaryProperties)
                ? null
                : candidateDeclaredProperties.OrdinaryProperties;
        return true;
    }

    private void SuppressPaintUntilFirstStyleCommit()
    {
        if (IsSvgNamespace
            || _hasComputedPresentation
            || _paintSuppressedUntilStyleCommit)
        {
            return;
        }

        _paintSuppressedUntilStyleCommit = true;
        Control.Opacity = 0;
    }

    private void RestorePaintAfterFirstStyleCommit(IReadOnlyDictionary<string, string> values)
    {
        _hasComputedPresentation = true;
        if (!_paintSuppressedUntilStyleCommit)
        {
            return;
        }

        _paintSuppressedUntilStyleCommit = false;
        var visibilityHidden = values.TryGetValue("visibility", out var visibility)
                               && string.Equals(visibility, "hidden", StringComparison.OrdinalIgnoreCase);
        Control.Opacity = visibilityHidden
            ? 0
            : values.TryGetValue("opacity", out var opacityValue)
              && double.TryParse(opacityValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var opacity)
                ? Math.Clamp(opacity, 0, 1)
                : 1;
    }

    internal void ReapplyComputedPresentation()
    {
        OwnerDocument.RecordElementPresentationApply();
        ApplyCanvasPositioning(useLayoutChangeSet: !s_disableComputedLayoutChangeSet);
        ApplyComputedPresentation(_computedStyleValues, CssPresentationChanges.ComputedAll);
        ApplyCursor(GetStyleValue("cursor"));
    }

    internal void ApplyInheritedTextPresentationFrom(AvaloniaDomElement parent)
    {
        if (this is not AvaloniaDomTextNode)
        {
            return;
        }

        // CSS selectors never target text nodes. Newly attached React text
        // fragments can therefore consume their parent's inherited native
        // presentation immediately without running a selector/cascade pass per
        // character. A later inherited-style invalidation still traverses and
        // refreshes this shared parent snapshot before applying presentation.
        _computedStyleValues = parent._computedStyleValues;
        _declaredStyleProperties = parent._declaredStyleProperties;
        ApplyComputedPresentation(
            parent._computedStyleValues,
            CssPresentationChanges.Color
            | CssPresentationChanges.FontFamily
            | CssPresentationChanges.FontSize
            | CssPresentationChanges.FontStyle
            | CssPresentationChanges.FontWeight
            | CssPresentationChanges.TextAlignment
            | CssPresentationChanges.WhiteSpace
            | CssPresentationChanges.TextTransform
            | CssPresentationChanges.LetterSpacing
            | CssPresentationChanges.WordSpacing
            | CssPresentationChanges.LineHeight);
        var visibilityHidden = parent._computedStyleValues.TryGetValue("visibility", out var visibility)
                               && string.Equals(visibility, "hidden", StringComparison.OrdinalIgnoreCase);
        Control.Opacity = visibilityHidden ? 0 : 1;
    }

    internal void ReapplyViewportPresentation()
    {
        // A stable media-query outcome means the cascade and every computed
        // value are unchanged. Only presentation consumed against the current
        // viewport/layout tree can become stale: display/visibility/opacity,
        // transforms, overflow, positioning, and CssLayout attached values.
        // Rebuilding brushes, fonts, cursor, and native control padding here is
        // both viewport-invariant and allocation-heavy. TextBlock visibility
        // is the exception: ApplyCanvasPositioning restores display visibility,
        // then this correction preserves CSS zero-font-size/empty-text collapse.
        OwnerDocument.RecordElementPresentationApply();
        ApplyCanvasPositioning(
            invalidateLayout: s_disableViewportLayoutInvalidationCoalescing,
            useLayoutChangeSet: !s_disableViewportLayoutChangeSet);
        ApplyComputedPresentation(
            _computedStyleValues,
            CssPresentationChanges.Display);
    }

    private void ApplyComputedPresentation(
        IReadOnlyDictionary<string, string> values,
        CssPresentationChanges changes)
    {
        if ((changes & CssPresentationChanges.Opacity) != 0)
        {
            var visibilityHidden = values.TryGetValue("visibility", out var visibilityValue)
                                   && string.Equals(visibilityValue, "hidden", StringComparison.OrdinalIgnoreCase);
            if (visibilityHidden)
            {
                // CSS visibility keeps the element in layout but suppresses its
                // entire painted subtree, regardless of the declared opacity.
                CancelCssOpacityTransition(dispatchCancel: true);
                Control.Opacity = 0;
            }
            else if (values.TryGetValue("opacity", out var opacityValue)
                     && double.TryParse(opacityValue, NumberStyles.Float, CultureInfo.InvariantCulture, out var opacity))
            {
                var targetOpacity = Math.Clamp(opacity, 0, 1);
                if (!TryStartCssOpacityTransition(targetOpacity, values))
                {
                    Control.Opacity = targetOpacity;
                    _cssPresentedOpacity = targetOpacity;
                }
            }
        }

        if ((changes & CssPresentationChanges.Background) != 0)
        {
            ApplyComputedControlProperty(values, "background-color", "Background");
        }
        if ((changes & CssPresentationChanges.Color) != 0)
        {
            if (!TryStartCssColorTransition(values.GetValueOrDefault("color"), values))
            {
                ApplyComputedControlProperty(values, "color", "Foreground");
                _cssPresentedColor = TryParseCssColor(values.GetValueOrDefault("color"), out var color)
                    ? color
                    : null;
            }
        }
        if ((changes & CssPresentationChanges.FontFamily) != 0)
        {
            ApplyComputedControlProperty(values, "font-family", "FontFamily");
        }
        if ((changes & CssPresentationChanges.FontSize) != 0)
        {
            ApplyComputedControlProperty(values, "font-size", "FontSize");
        }
        if ((changes & CssPresentationChanges.FontStyle) != 0)
        {
            ApplyComputedControlProperty(values, "font-style", "FontStyle");
        }
        if ((changes & CssPresentationChanges.FontWeight) != 0)
        {
            ApplyComputedControlProperty(values, "font-weight", "FontWeight");
        }
        if ((changes & (CssPresentationChanges.FontFamily
                        | CssPresentationChanges.FontSize
                        | CssPresentationChanges.FontWeight)) != 0
            && Control is DomTextBlockControl domText)
        {
            var resolution = ResolveFont(
                values.GetValueOrDefault("font-family", "sans-serif"),
                values);
            var metricSize = values.TryGetValue("font-size", out var metricSizeValue)
                             && TryParseCssPixels(metricSizeValue, out var parsedMetricSize)
                ? parsedMetricSize
                : domText.FontSize;
            domText.FontWidthScale = resolution.ResolveWidthScale(metricSize, domText.FontWeight);
        }
        if ((changes & CssPresentationChanges.TextAlignment) != 0)
        {
            ApplyComputedControlProperty(values, "text-align", "TextAlignment");
        }
        if ((changes & CssPresentationChanges.Padding) != 0)
        {
            var hasDeclaredPadding = _declaredStyleProperties.Contains("padding")
                                     || _declaredStyleProperties.Contains("padding-top")
                                     || _declaredStyleProperties.Contains("padding-right")
                                     || _declaredStyleProperties.Contains("padding-bottom")
                                     || _declaredStyleProperties.Contains("padding-left");
            if (hasDeclaredPadding && values.TryGetValue("padding", out var paddingValue))
            {
                SetControlProperty("Padding", paddingValue);
            }
            else if (hasDeclaredPadding
                     && values.TryGetValue("padding-top", out var paddingTop)
                     && values.TryGetValue("padding-right", out var paddingRight)
                     && values.TryGetValue("padding-bottom", out var paddingBottom)
                     && values.TryGetValue("padding-left", out var paddingLeft))
            {
                SetControlProperty("Padding", $"{paddingTop} {paddingRight} {paddingBottom} {paddingLeft}");
            }
        }

        if ((changes & CssPresentationChanges.Border) != 0)
        {
            ApplyComputedBorder(values);
        }
        if ((changes & CssPresentationChanges.Outline) != 0)
        {
            ApplyComputedOutline(values);
        }

        if ((changes & CssPresentationChanges.Color) != 0
            && Control is SvgLayoutPanel svg
            && values.TryGetValue("color", out var svgColor)
            && TryParseBrush(svgColor, out var currentColor))
        {
            svg.CurrentColor = currentColor;
            svg.InvalidateVisual();
            svg.InvalidateSkiaVisual();
            svg.InvalidateSceneVisual();
        }

        if ((changes & CssPresentationChanges.SvgPaint) != 0 && IsSvgNamespace)
        {
            InvalidateSvgRenderer();
        }

        if ((changes & CssPresentationChanges.WhiteSpace) != 0
            && Control is TextBlock textBlock
            && values.TryGetValue("white-space", out var whiteSpace))
        {
            textBlock.TextWrapping = whiteSpace.Trim().ToLowerInvariant() switch
            {
                "normal" or "pre-wrap" or "pre-line" => TextWrapping.Wrap,
                _ => TextWrapping.NoWrap
            };
            if (this is AvaloniaDomTextNode whitespaceTextNode)
            {
                whitespaceTextNode.ApplyWhiteSpace(whiteSpace);
            }
        }

        if ((changes & CssPresentationChanges.TextTransform) != 0
            && this is AvaloniaDomTextNode textNode)
        {
            textNode.ApplyTextTransform(values.TryGetValue("text-transform", out var transform)
                ? transform
                : "none");
        }

        if ((changes & CssPresentationChanges.LetterSpacing) != 0
            && Control is TextBlock trackedText)
        {
            trackedText.LetterSpacing = values.TryGetValue("letter-spacing", out var spacing)
                                        && !string.Equals(spacing.Trim(), "normal", StringComparison.OrdinalIgnoreCase)
                                        && TryParseCssPixels(spacing, out var parsedSpacing)
                ? parsedSpacing
                : 0;
        }

        if ((changes & CssPresentationChanges.WordSpacing) != 0
            && Control is DomTextBlockControl spacedText)
        {
            spacedText.WordSpacing = values.TryGetValue("word-spacing", out var spacing)
                                     && !string.Equals(spacing.Trim(), "normal", StringComparison.OrdinalIgnoreCase)
                                     && TryParseCssPixels(spacing, out var parsedSpacing)
                ? parsedSpacing
                : 0;
        }

        if ((changes & CssPresentationChanges.LineHeight) != 0)
        {
            if (values.TryGetValue("line-height", out var lineHeight)
                && !string.Equals(lineHeight.Trim(), "normal", StringComparison.OrdinalIgnoreCase)
                && TryParseCssPixels(lineHeight, out var parsedLineHeight))
            {
                if (Control is TextBlock lineText)
                {
                    // CSS permits a zero line box and still paints glyphs
                    // outside it. Avalonia rejects a literal zero
                    // TextBlock.LineHeight, so retain automatic glyph metrics
                    // while CssLayoutPanel keeps the authored zero-height line
                    // box.
                    if (parsedLineHeight > 0)
                    {
                        lineText.LineHeight = parsedLineHeight;
                    }
                    else
                    {
                        lineText.ClearValue(TextBlock.LineHeightProperty);
                    }
                }
                CssLayout.SetLineHeight(Control, Math.Max(0, parsedLineHeight));
            }
            else
            {
                if (Control is TextBlock lineText)
                {
                    lineText.ClearValue(TextBlock.LineHeightProperty);
                }
                CssLayout.SetLineHeight(Control, null);
            }
        }

        if ((changes & CssPresentationChanges.FontSizeOrDisplay) != 0
            && Control is TextBlock fontTextBlock
            && values.TryGetValue("font-size", out var fontSizeValue)
            && TryParseCssPixels(fontSizeValue, out var fontSize))
        {
            // Avalonia rejects a literal zero FontSize, while CSS uses it to
            // collapse accessibility-only text. Model the resulting zero line
            // box by removing that text visual from layout.
            var allowsLayout = fontSize > 0
                               && (!values.TryGetValue("display", out var textDisplay)
                                   || !string.Equals(textDisplay, "none", StringComparison.OrdinalIgnoreCase));
            if (fontTextBlock is DomTextBlockControl domTextBlock)
            {
                // Boundary whitespace refreshes happen during every measure.
                // Keep CSS display/font-size suppression as independent state
                // so normal-space trimming cannot resurrect accessibility-only
                // zero-font-size text into layout.
                domTextBlock.SetCssAllowsLayout(allowsLayout);
            }
            else
            {
                fontTextBlock.IsVisible = allowsLayout
                                          && !string.IsNullOrWhiteSpace(fontTextBlock.Text);
            }
        }
    }

    private void ApplyComputedBorder(IReadOnlyDictionary<string, string> values)
    {
        static string ValueOr(IReadOnlyDictionary<string, string> source, string name, string fallback)
            => source.TryGetValue(name, out var value) ? value : fallback;

        double WidthFor(string side, LengthAxis axis)
        {
            var style = ValueOr(values, $"border-{side}-style", "none").Trim().ToLowerInvariant();
            if (style is "none" or "hidden") return 0;
            var width = ValueOr(values, $"border-{side}-width", "0px");
            if (TryParseLength(width, axis, allowNegative: false, allowAuto: false, out var parsed))
                return Math.Max(0, parsed);
            return width.Trim().ToLowerInvariant() switch
            {
                "thin" => 1,
                "medium" => 3,
                "thick" => 5,
                _ => 0
            };
        }

        var thickness = new Thickness(
            WidthFor("left", LengthAxis.Horizontal),
            WidthFor("top", LengthAxis.Vertical),
            WidthFor("right", LengthAxis.Horizontal),
            WidthFor("bottom", LengthAxis.Vertical));
        (string Color, IBrush? Brush) BrushFor(string side)
        {
            var color = ValueOr(values, $"border-{side}-color", "currentcolor");
            if (string.Equals(color, "currentcolor", StringComparison.OrdinalIgnoreCase))
                color = ValueOr(values, "color", "transparent");
            return (color, TryParseBrush(color, out var brush) ? brush : null);
        }

        var top = BrushFor("top");
        var right = BrushFor("right");
        var bottom = BrushFor("bottom");
        var left = BrushFor("left");

        if (Control is CssLayoutPanel panel)
        {
            panel.BorderThickness = thickness;
            // Keep the aggregate brush for compatibility with consumers that
            // only understand uniform borders, while the DOM panel paints the
            // four CSS side colors independently.
            panel.BorderBrush = top.Brush;
            panel.SetBorderBrushes(top.Brush, right.Brush, bottom.Brush, left.Brush);
            panel.CornerRadius = new CornerRadius(
                RadiusFor("border-top-left-radius"),
                RadiusFor("border-top-right-radius"),
                RadiusFor("border-bottom-right-radius"),
                RadiusFor("border-bottom-left-radius"));
            return;
        }

        SetControlProperty("BorderThickness", $"{thickness.Top}px {thickness.Right}px {thickness.Bottom}px {thickness.Left}px");
        SetControlProperty("BorderBrush", top.Color);

        double RadiusFor(string name)
        {
            var value = ValueOr(values, name, "0px");
            var first = FirstRadiusComponent(value);
            if (CssLayout.TryParseAbsoluteLength(first, out var parsed))
            {
                return Math.Max(0, parsed);
            }

            // CSS math functions remain functional at computed-value time. Resolve
            // their used length here just as we do a percentage radius; otherwise
            // valid values such as calc(max(...) * 6px) silently become square.
            // CssLayoutPanel currently projects one scalar radius per corner, so
            // elliptical vertical radii remain a separate capability boundary.
            if (CssLayout.TryParseLength(first, out var length)
                && length.HasValue
                && !length.Value.IsAuto)
            {
                var reference = Control.Bounds.Width > 0
                    ? Control.Bounds.Width
                    : CssLayout.Resolve(CssLayout.GetWidth(Control), 0)
                      ?? Control.DesiredSize.Width;
                return Math.Max(0, CssLayout.Resolve(length, reference) ?? 0);
            }
            return 0;

            static string FirstRadiusComponent(string radius)
            {
                var source = radius.AsSpan().Trim();
                var depth = 0;
                for (var index = 0; index < source.Length; index++)
                {
                    var current = source[index];
                    if (current == '(')
                    {
                        depth++;
                    }
                    else if (current == ')')
                    {
                        depth = Math.Max(0, depth - 1);
                    }
                    else if (depth == 0 && (current == '/' || char.IsWhiteSpace(current)))
                    {
                        return source[..index].ToString();
                    }
                }

                return source.Length == 0 ? "0px" : source.ToString();
            }
        }
    }

    private void ApplyComputedOutline(IReadOnlyDictionary<string, string> values)
    {
        if (Control is not CssLayoutPanel panel) return;

        var style = values.GetValueOrDefault("outline-style", "none").Trim().ToLowerInvariant();
        var widthValue = values.GetValueOrDefault("outline-width", "medium");
        var width = style is "none" or "hidden"
            ? 0
            : TryParseLength(
                widthValue,
                LengthAxis.Horizontal,
                allowNegative: false,
                allowAuto: false,
                out var parsedWidth)
                ? Math.Max(0, parsedWidth)
                : widthValue.Trim().ToLowerInvariant() switch
                {
                    "thin" => 1,
                    "medium" => 3,
                    "thick" => 5,
                    _ => 0
                };

        var offset = values.TryGetValue("outline-offset", out var offsetValue)
                     && TryParseLength(
                         offsetValue,
                         LengthAxis.Horizontal,
                         allowNegative: true,
                         allowAuto: false,
                         out var parsedOffset)
            ? parsedOffset
            : 0;
        var color = values.GetValueOrDefault("outline-color", "currentcolor");
        if (string.Equals(color.Trim(), "currentcolor", StringComparison.OrdinalIgnoreCase))
        {
            color = values.GetValueOrDefault("color", "black");
        }
        var brush = TryParseBrush(color, out var parsedBrush) ? parsedBrush : null;
        panel.SetOutline(brush, width, offset, style);
    }

    private static bool TryParseCssPixels(string value, out double result)
    {
        if (CssLayout.TryParseAbsoluteLength(value, out result))
        {
            return true;
        }

        var normalized = value.Trim();
        if (normalized.EndsWith("px", StringComparison.OrdinalIgnoreCase))
        {
            normalized = normalized[..^2];
        }

        return double.TryParse(normalized, NumberStyles.Float, CultureInfo.InvariantCulture, out result)
               && double.IsFinite(result);
    }

    private void ApplyComputedControlProperty(
        IReadOnlyDictionary<string, string> values,
        string cssName,
        string avaloniaName)
    {
        // CSS initial values are exposed through computed style, but they must
        // not overwrite an explicit native Avalonia property when the CSS
        // property was never declared. A native Background, Foreground, or
        // font setting remains the presentation source until CSS declares it.
        // A DomTextBlockControl is created solely to paint an HTML text node;
        // it has no authored native presentation to preserve. Its parent CSS
        // values are therefore authoritative even when they are initial or
        // inherited rather than explicitly declared. Without this exception,
        // CSS medium (16px) falls back to Avalonia TextBlock's 12px default,
        // while generated content such as a list marker correctly uses 16px.
        var domTextConsumesComputedValue = Control is DomTextBlockControl
                                           && cssName is "color"
                                               or "font-family"
                                               or "font-size"
                                               or "font-style"
                                               or "font-weight";
        if ((!_declaredStyleProperties.Contains(cssName) && !domTextConsumesComputedValue)
            || !values.TryGetValue(cssName, out var value)
            || string.IsNullOrWhiteSpace(value))
        {
            if (_nativePresentationValues.Remove(avaloniaName, out var original))
            {
                if (original.WasSet)
                {
                    Control.SetValue(original.Property, original.Value);
                }
                else
                {
                    Control.ClearValue(original.Property);
                }
            }
            return;
        }

        if (!_nativePresentationValues.ContainsKey(avaloniaName)
            && FindAvaloniaProperty(Control.GetType(), avaloniaName) is { } property)
        {
            _nativePresentationValues[avaloniaName] = new NativePresentationValue(
                property,
                Control.IsSet(property),
                Control.GetValue(property));
        }
        SetControlProperty(avaloniaName, value);
    }

    public AvaloniaDomElement? parentElement => Control.Parent is Control parent ? OwnerDocument.WrapControl(parent) : null;

    public void setPointerCapture(int pointerId) => OwnerDocument.SetPointerCapture(this, pointerId);

    public void releasePointerCapture(int pointerId) => OwnerDocument.ReleasePointerCapture(this, pointerId);

    public bool hasPointerCapture(int pointerId) => OwnerDocument.HasPointerCapture(this, pointerId);

    public virtual bool contains(object? node)
    {
        node = AvaloniaDomDocument.UnwrapDomNode(node);
        if (ReferenceEquals(node, this))
        {
            return true;
        }

        if (node is not AvaloniaDomElement element
            || !ReferenceEquals(element.ownerDocument, OwnerDocument))
        {
            return false;
        }

        for (var current = element.parentElement; current is not null; current = current.parentElement)
        {
            if (ReferenceEquals(current, this))
            {
                return true;
            }
        }

        return false;
    }

    public virtual int compareDocumentPosition(object? other)
        => AvaloniaDomDocument.CompareDocumentPosition(this, other);

    public virtual bool isSameNode(object? other)
        => ReferenceEquals(this, AvaloniaDomDocument.UnwrapDomNode(other));

    public virtual bool isEqualNode(object? other)
    {
        other = AvaloniaDomDocument.UnwrapDomNode(other);
        if (ReferenceEquals(this, other)) return true;
        if (other is not AvaloniaDomElement element
            || nodeType != element.nodeType
            || !string.Equals(nodeName, element.nodeName, StringComparison.Ordinal)
            || !string.Equals(namespaceURI, element.namespaceURI, StringComparison.Ordinal)
            || !string.Equals(textContent, element.textContent, StringComparison.Ordinal))
        {
            return false;
        }

        var ownAttributes = attributes;
        var otherAttributes = element.attributes;
        if (ownAttributes.Length != otherAttributes.Length) return false;
        foreach (var attribute in ownAttributes)
        {
            if (!element.hasAttribute(attribute.name)
                || !string.Equals(
                    attribute.value,
                    element.getAttribute(attribute.name),
                    StringComparison.Ordinal))
            {
                return false;
            }
        }

        var ownChildren = GetChildElements().ToArray();
        var otherChildren = element.GetChildElements().ToArray();
        if (ownChildren.Length != otherChildren.Length) return false;
        for (var index = 0; index < ownChildren.Length; index++)
        {
            if (!ownChildren[index].isEqualNode(otherChildren[index])) return false;
        }
        return true;
    }

    public object? contentWindow
        => string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase)
           && parentElement is not null
            ? _externalContentWindow
            : null;

    internal void SetContentDocument(AvaloniaDomDocument document)
        => _contentDocument = document;

    internal AvaloniaDomDocument? GetContentDocument() => _contentDocument;

    internal object? GetExternalContentWindow() => _externalContentWindow;

    internal void SetExternalContentWindow(object window, IExternalVirtualBrowsingContext runtime)
    {
        _externalContentWindow = window ?? throw new ArgumentNullException(nameof(window));
        _externalContentWindowRuntime = runtime ?? throw new ArgumentNullException(nameof(runtime));
    }

    internal IExternalVirtualBrowsingContext? GetExternalContentWindowRuntime()
        => _externalContentWindowRuntime;

    public object? __htmlMlExternalContentDocument
        => _externalContentWindowRuntime is IExternalVirtualBrowsingContextDocumentView view
            ? view.Document
            : null;

    internal void DetachExternalContentWindow(IExternalVirtualBrowsingContext runtime)
    {
        if (!ReferenceEquals(_externalContentWindowRuntime, runtime))
        {
            return;
        }
        _externalContentWindowRuntime = null;
        _externalContentWindow = null;
    }

    private void ReleaseBrowsingContextsForRemoval()
    {
        if (string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase))
        {
            ReleaseOwnBrowsingContextForRemoval();
            return;
        }

        foreach (var child in GetChildElements().ToArray())
        {
            child.ReleaseBrowsingContextsForRemoval();
        }
    }

    private void ReleaseOwnBrowsingContextForRemoval()
    {
        var frameDocument = _contentDocument as VirtualIframeDomDocument;
        frameDocument?.DisposeExternalBrowsingContexts();
        var externalRuntime = _externalContentWindowRuntime ?? frameDocument?.ExternalRuntime;
        externalRuntime?.Dispose();
        if (frameDocument is not null && externalRuntime is not null)
        {
            ExternalVirtualBrowsingContextLifecycle.Detach(frameDocument, this, externalRuntime);
        }
        if (frameDocument is not null)
        {
            frameDocument.ExternalEventListenerAdapter = null;
            frameDocument.ExternalWindowContext = null;
        }

        _contentDocument = null;
        _externalContentWindow = null;
        _externalContentWindowRuntime = null;
        _contentInitializationScheduled = false;
        _contentInitializationCompleted = false;
        if (Control is Panel panel)
        {
            foreach (var child in panel.Children
                         .OfType<Control>()
                         .Where(child => !AvaloniaDomDocument.IsDomInfrastructureControl(child))
                         .ToArray())
            {
                panel.Children.Remove(child);
            }
        }
    }

    internal bool TryBeginContentInitialization()
    {
        if (_contentInitializationScheduled || _contentInitializationCompleted)
        {
            return false;
        }

        _contentInitializationScheduled = true;
        return true;
    }

    internal void EndContentInitialization(
        bool succeeded,
        AvaloniaDomDocument? expectedDocument = null)
    {
        if (expectedDocument is not null && !ReferenceEquals(_contentDocument, expectedDocument))
        {
            return;
        }
        _contentInitializationScheduled = false;
        _contentInitializationCompleted = succeeded;
    }


    private bool WouldCreateParentCycle(Control desiredParent)
    {
        if (ReferenceEquals(desiredParent, Control))
        {
            return true;
        }

        var current = desiredParent.Parent as Control;
        while (current is not null)
        {
            if (ReferenceEquals(current, Control))
            {
                return true;
            }

            current = current.Parent as Control;
        }

        return false;
    }

    private void EnsureChartElementInTree()
    {
        // DOM mutation is responsible for real visual-tree ownership. Geometry
        // reads and context creation must be observational: moving a control
        // here changes parentElement, clipping, and paint order mid-frame.
    }
    public object? contentDocument
        => string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase)
           && parentElement is not null
            ? _contentDocument
            : null;

    public AvaloniaDomElement? firstChild => GetChildElements().FirstOrDefault();

    public AvaloniaDomElement? lastChild => GetChildElements().LastOrDefault();

    public AvaloniaDomElement? previousSibling => GetSibling(-1);

    public AvaloniaDomElement? nextSibling => GetSibling(1);

    public AvaloniaDomElement? firstElementChild
        => GetChildElements().FirstOrDefault(child => child.nodeType == 1);

    public AvaloniaDomElement? lastElementChild
        => GetChildElements().LastOrDefault(child => child.nodeType == 1);

    public AvaloniaDomElement? previousElementSibling => GetElementSibling(-1);

    public AvaloniaDomElement? nextElementSibling => GetElementSibling(1);

    public object[] childNodes => GetChildElements().Cast<object>().ToArray();

    public object[] children
        => GetChildElements().Where(child => child.nodeType == 1).Cast<object>().ToArray();

    public DomAttribute[] attributes
        => (_xmlMode ? _xmlAttributes.Keys.AsEnumerable() : _attributeNames.AsEnumerable())
            .Select(name => new DomAttribute(name, getAttribute(name) ?? string.Empty, OwnerDocument))
            .ToArray();

    public int childElementCount => GetChildElements().Count(child => child.nodeType == 1);

    public bool hasChildNodes => GetChildElements().Any();

    public virtual AvaloniaDomElement cloneNode(bool deep = false)
    {
        if (Control is TextBlock)
        {
            return AssertCreatedNode(OwnerDocument.createTextNode(textContent ?? string.Empty));
        }

        var clone = AssertCreatedNode(OwnerDocument.createElementNS(namespaceURI, localName));
        foreach (var attributeName in _attributeNames)
        {
            if (!string.Equals(attributeName, "style", StringComparison.OrdinalIgnoreCase))
            {
                clone.setAttribute(attributeName, getAttribute(attributeName));
            }
        }
        foreach (var dataAttribute in _dataAttributes)
        {
            clone.SetDataAttribute(dataAttribute.Key, dataAttribute.Value);
        }
        foreach (var styleValue in _styleValues)
        {
            clone.style.setProperty(styleValue.Key, styleValue.Value);
        }
        if (deep)
        {
            foreach (var child in GetChildElements())
            {
                clone.appendChild(child.cloneNode(deep: true));
            }
        }
        return clone;
    }

    private static AvaloniaDomElement AssertCreatedNode(object? node)
        => node as AvaloniaDomElement
           ?? throw new InvalidOperationException("The owner document could not clone this DOM node.");

    public string tagName => _tagNameCache ??= nodeName;

    public virtual object? querySelector(string selector)
    {
        if (string.IsNullOrWhiteSpace(selector))
        {
            return null;
        }

        foreach (var control in AvaloniaDomDocument.Traverse(Control).Skip(1))
        {
            if (MatchesElementSelector(control, selector, this))
            {
                return OwnerDocument.WrapControl(control);
            }
        }

        OwnerDocument.RecordSelectorMiss($"element:{localName}.{className}", selector);
        return null;
    }

    public virtual object[] querySelectorAll(string selector)
    {
        if (string.IsNullOrWhiteSpace(selector))
        {
            return Array.Empty<object>();
        }

        var list = new List<object>();
        foreach (var control in AvaloniaDomDocument.Traverse(Control).Skip(1))
        {
            if (MatchesElementSelector(control, selector, this))
            {
                list.Add(OwnerDocument.WrapControl(control));
            }
        }

        return list.ToArray();
    }

    public virtual bool matches(string selector)
        => MatchesSelector(selector, this);

    public bool __htmlMlIsValidSelector(string selector)
        => CssSelectorSyntaxParser.IsSupportedDomSelectorList(selector);

    internal bool MatchesSelector(string selector, AvaloniaDomElement scopeElement)
    {
        if (string.IsNullOrWhiteSpace(selector))
        {
            return false;
        }

        foreach (var selectorPart in CssSelectorParser.SplitSelectorList(selector))
        {
            if (CssSelectorParser.TryParse(selectorPart, out var parsed) &&
                parsed.Matches(this, OwnerDocument, scopeElement))
            {
                return true;
            }
        }

        return false;
    }

    public bool webkitMatchesSelector(string selector) => matches(selector);

    public bool msMatchesSelector(string selector) => matches(selector);

    public virtual AvaloniaDomElement? closest(string selector)
    {
        for (AvaloniaDomElement? current = this; current is not null; current = current.parentElement)
        {
            if (current.matches(selector))
            {
                return current;
            }
        }

        return null;
    }

    public virtual object[] getElementsByTagName(string tagName)
    {
        if (string.IsNullOrWhiteSpace(tagName))
        {
            return Array.Empty<object>();
        }

        var normalized = tagName.Trim();
        var matchesAll = normalized == "*";
        var list = new List<object>();
        foreach (var child in GetChildElements())
        {
            foreach (var control in AvaloniaDomDocument.Traverse(child.Control))
            {
                var wrapped = OwnerDocument.WrapControl(control);
                if (matchesAll || string.Equals(wrapped.tagName, normalized, StringComparison.OrdinalIgnoreCase))
                {
                    list.Add(wrapped);
                }
            }
        }

        return list.ToArray();
    }

    public virtual object[] getElementsByClassName(string className)
    {
        if (string.IsNullOrWhiteSpace(className))
        {
            return Array.Empty<object>();
        }

        var classes = className.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
        var list = new List<object>();
        foreach (var control in AvaloniaDomDocument.Traverse(Control).Skip(1))
        {
            if (control is StyledElement styled && classes.All(c => styled.Classes.Contains(c)))
            {
                list.Add(OwnerDocument.WrapControl(control));
            }
        }

        return list.ToArray();
    }

    public virtual DomRect getBoundingClientRect()
        => new(GetBoundingClientRectValue());

    internal Rect GetBoundingClientRectValue()
    {
        if (s_disableLayoutReadFastPaths)
        {
            OwnerDocument.FlushPendingLayout();
        }
        return HasLayoutBox() ? GetElementBounds() : default;
    }

    HtmlMlRect IHtmlMlDomRectTarget.ReadBoundingClientRect()
    {
        var rect = GetBoundingClientRectValue();
        return new HtmlMlRect(rect.X, rect.Y, rect.Width, rect.Height);
    }

    bool IHtmlMlDomClientRectsTarget.TryReadClientRect(out HtmlMlRect rect)
    {
        if (s_disableLayoutReadFastPaths)
        {
            OwnerDocument.FlushPendingLayout();
        }
        if (!HasLayoutBox())
        {
            rect = default;
            return false;
        }

        var bounds = GetElementBounds();
        rect = new HtmlMlRect(bounds.X, bounds.Y, bounds.Width, bounds.Height);
        return true;
    }

    double IHtmlMlDomNumericTarget.ReadDomNumericProperty(HtmlMlDomNumericProperty property)
        => property switch
        {
            HtmlMlDomNumericProperty.Width => width,
            HtmlMlDomNumericProperty.Height => height,
            HtmlMlDomNumericProperty.ClientWidth => clientWidth,
            HtmlMlDomNumericProperty.ClientHeight => clientHeight,
            HtmlMlDomNumericProperty.OffsetWidth => offsetWidth,
            HtmlMlDomNumericProperty.OffsetHeight => offsetHeight,
            HtmlMlDomNumericProperty.OffsetTop => offsetTop,
            HtmlMlDomNumericProperty.OffsetLeft => offsetLeft,
            _ => double.NaN
        };

    long IHtmlMlDomIdentityTarget.HtmlMlDomIdentity => __htmlMlDomIdentity;

    public virtual DomRect[] getClientRects()
    {
        if (s_disableLayoutReadFastPaths)
        {
            OwnerDocument.FlushPendingLayout();
            return HasLayoutBox() ? new[] { getBoundingClientRect() } : [];
        }

        var bounds = GetElementBounds();
        return HasLayoutBox() ? new[] { new DomRect(bounds) } : [];
    }

    public virtual double clientWidth => GetClientSize().Width;

    public virtual double clientHeight => GetClientSize().Height;

    public virtual double clientTop => 0;

    public virtual double clientLeft => 0;

    public virtual double offsetWidth
    {
        get
        {
            var val = s_disableLayoutReadFastPaths
                ? GetElementBounds().Width
                : GetLayoutSize().Width;
            if (val <= 0 && IsCanvasControl() && HasLayoutBox())
            {
                return 300;
            }
            return val;
        }
    }

    public virtual double offsetHeight
    {
        get
        {
            var val = s_disableLayoutReadFastPaths
                ? GetElementBounds().Height
                : GetLayoutSize().Height;
            if (val <= 0 && IsCanvasControl() && HasLayoutBox())
            {
                return 150;
            }
            return val;
        }
    }

    public virtual double offsetTop => GetOffsetRelativeToParent().Y;

    public virtual double offsetLeft => GetOffsetRelativeToParent().X;

    public virtual double width
    {
        get
        {
            OwnerDocument.FlushPendingLayout();
            if (IsExplicitHtmlCanvasElement())
            {
                return CanvasContextBridge.TryGetVirtualSize(Control, out var virtualWidth, out _)
                    ? virtualWidth
                    : 300;
            }

            double w = 0;
            if (Control is CanvasOpenGlDrawingSurface openGlSurface)
            {
                w = openGlSurface.DrawingBufferWidth;
            }
            else if (Control is CanvasDrawingSurface surface)
            {
                w = surface.VirtualWidth;
            }
            else
            {
                w = GetExplicitOrArrangedSize(Control.Width, Control.Bounds.Width);
            }

            if (w <= 0 && IsCanvasControl())
            {
                return 300;
            }
            return w;
        }
        set
        {
            if (double.IsFinite(value) && value >= 0)
            {
                if (IsExplicitHtmlCanvasElement())
                {
                    CanvasContextBridge.SetVirtualSize(Control, width: value);
                    // HTML canvas attributes define the intrinsic CSS size when
                    // no stylesheet width overrides them. Keep the backing store
                    // separate, but expose that intrinsic size to layout.
                    if (!_styleValues.ContainsKey("width"))
                    {
                        Control.Width = value;
                    }
                    return;
                }

                if (Control is CanvasOpenGlDrawingSurface openGlSurface)
                {
                    openGlSurface.SetDrawingBufferWidth(value);
                    return;
                }

                if (Control is CanvasDrawingSurface surface)
                {
                    surface.VirtualWidth = value;
                    if (!_styleValues.ContainsKey("width"))
                    {
                        Control.Width = value;
                    }
                    CanvasContextBridge.Reset2D(Control);
                    return;
                }

                Control.Width = value;
                CanvasContextBridge.Reset2D(Control);
            }
        }
    }

    public virtual double height
    {
        get
        {
            OwnerDocument.FlushPendingLayout();
            if (IsExplicitHtmlCanvasElement())
            {
                return CanvasContextBridge.TryGetVirtualSize(Control, out _, out var virtualHeight)
                    ? virtualHeight
                    : 150;
            }

            double h = 0;
            if (Control is CanvasOpenGlDrawingSurface openGlSurface)
            {
                h = openGlSurface.DrawingBufferHeight;
            }
            else if (Control is CanvasDrawingSurface surface)
            {
                h = surface.VirtualHeight;
            }
            else
            {
                h = GetExplicitOrArrangedSize(Control.Height, Control.Bounds.Height);
            }

            if (h <= 0 && IsCanvasControl())
            {
                return 150;
            }
            return h;
        }
        set
        {
            if (double.IsFinite(value) && value >= 0)
            {
                if (IsExplicitHtmlCanvasElement())
                {
                    CanvasContextBridge.SetVirtualSize(Control, height: value);
                    // See width: the CSS box follows the intrinsic attribute
                    // only while no CSS height is in effect.
                    if (!_styleValues.ContainsKey("height"))
                    {
                        Control.Height = value;
                    }
                    return;
                }

                if (Control is CanvasOpenGlDrawingSurface openGlSurface)
                {
                    openGlSurface.SetDrawingBufferHeight(value);
                    return;
                }

                if (Control is CanvasDrawingSurface surface)
                {
                    surface.VirtualHeight = value;
                    if (!_styleValues.ContainsKey("height"))
                    {
                        Control.Height = value;
                    }
                    CanvasContextBridge.Reset2D(Control);
                    return;
                }

                Control.Height = value;
                CanvasContextBridge.Reset2D(Control);
            }
        }
    }

    public virtual AvaloniaDomElement? offsetParent => FindOffsetParentElement();

    public virtual double scrollWidth => GetScrollSize().Width;

    public virtual double scrollHeight => GetScrollSize().Height;

    public virtual double scrollTop
    {
        get => GetScrollOffset().Y;
        set
        {
            var current = GetScrollOffset();
            SetScrollOffset(current.X, value);
            var updated = GetScrollOffset();
            if (!AreClose(current.Y, updated.Y))
            {
                OwnerDocument.DispatchSyntheticEvent(
                    this,
                    new DomSyntheticEvent(
                        "scroll",
                        bubbles: false,
                        cancelable: false,
                        Host.GetTimestamp(),
                        detail: null,
                        accessor: null));
            }
        }
    }

    public virtual double scrollLeft
    {
        get => GetScrollOffset().X;
        set
        {
            var current = GetScrollOffset();
            SetScrollOffset(value, current.Y);
            var updated = GetScrollOffset();
            if (!AreClose(current.X, updated.X))
            {
                OwnerDocument.DispatchSyntheticEvent(
                    this,
                    new DomSyntheticEvent(
                        "scroll",
                        bubbles: false,
                        cancelable: false,
                        Host.GetTimestamp(),
                        detail: null,
                        accessor: null));
            }
        }
    }

    public virtual string? value
    {
        get
        {
            if (localName is "progress" or "meter")
            {
                return getAttribute("value") ?? "0";
            }
            if (string.Equals(localName, "option", StringComparison.OrdinalIgnoreCase))
            {
                return getAttribute("value") ?? NormalizeOptionText(textContent);
            }
            if (string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase))
            {
                var index = selectedIndex;
                return index >= 0 && index < options.Length ? options[index].value : string.Empty;
            }
            if (string.Equals(localName, "input", StringComparison.OrdinalIgnoreCase)
                && (string.Equals(type, "checkbox", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(type, "radio", StringComparison.OrdinalIgnoreCase)))
            {
                if (_hasExplicitValueState) return _explicitValueState;
                return getAttribute("value") ?? "on";
            }
            return Control switch
            {
                TextBox textBox => textBox.Text ?? string.Empty,
                TextBlock textBlock => textBlock.Text ?? string.Empty,
                ContentControl { Content: string text } => text,
                _ => _hasExplicitValueState ? _explicitValueState : getAttribute("value")
            };
        }
        set
        {
            var stringValue = value ?? string.Empty;
            if (localName is "progress" or "meter")
            {
                setAttribute("value", stringValue);
                return;
            }
            if (string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase))
            {
                var expected = stringValue;
                var matched = false;
                foreach (var option in options)
                {
                    var selected = string.Equals(option.value ?? string.Empty, expected, StringComparison.Ordinal);
                    option.selected = selected;
                    matched |= selected;
                }
                if (!matched) selectedIndex = -1;
                return;
            }
            if (string.Equals(localName, "option", StringComparison.OrdinalIgnoreCase))
            {
                setAttribute("value", stringValue);
                return;
            }
            _hasExplicitValueState = true;
            _explicitValueState = stringValue;
            switch (Control)
            {
                case TextBox textBox:
                    var nextValue = stringValue;
                    if (string.Equals(textBox.Text ?? string.Empty, nextValue, StringComparison.Ordinal))
                    {
                        break;
                    }

                    textBox.Text = nextValue;
                    // The HTML value IDL setter treats a changed value as a new
                    // editing value. Chromium collapses the selection at the
                    // end immediately (before focus as well as after it). A
                    // same-value assignment is deliberately a no-op so an
                    // existing author selection is preserved.
                    var end = nextValue.Length;
                    textBox.CaretIndex = end;
                    textBox.SelectionStart = end;
                    textBox.SelectionEnd = end;
                    _selectionDirection = "none";
                    break;
                case TextBlock textBlock:
                    textBlock.Text = stringValue;
                    break;
                case ContentControl contentControl when contentControl.Content is null || contentControl.Content is string:
                    contentControl.Content = stringValue;
                    break;
            }
        }
    }

    public virtual AvaloniaDomElement[] options
        => string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase)
            ? querySelectorAll("option").OfType<AvaloniaDomElement>().ToArray()
            : [];

    public virtual AvaloniaDomElement[] elements
        => string.Equals(localName, "form", StringComparison.OrdinalIgnoreCase)
            ? querySelectorAll("*")
                .OfType<AvaloniaDomElement>()
                .Where(element => element.localName is
                    "button" or "fieldset" or "input" or "object" or
                    "output" or "select" or "textarea")
                .ToArray()
            : [];

    public virtual int selectedIndex
    {
        get
        {
            if (!string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase)) return -1;
            if (_selectionExplicitlyEmpty) return -1;
            var values = options;
            for (var index = 0; index < values.Length; index++)
            {
                if (values[index].selected) return index;
            }
            return values.Length > 0 ? 0 : -1;
        }
        set
        {
            if (!string.Equals(localName, "select", StringComparison.OrdinalIgnoreCase)) return;
            var values = options;
            _selectionExplicitlyEmpty = value < 0 || value >= values.Length;
            for (var index = 0; index < values.Length; index++)
            {
                values[index].selected = index == value;
            }
        }
    }

    public virtual void reset()
    {
        if (!string.Equals(localName, "form", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        foreach (var element in querySelectorAll("*").OfType<AvaloniaDomElement>())
        {
            if (string.Equals(element.localName, "select", StringComparison.OrdinalIgnoreCase))
            {
                element._selectionExplicitlyEmpty = false;
                foreach (var option in element.options)
                {
                    option._selected = option.HasAttributePresence("selected");
                    OwnerDocument.NotifyDynamicStateChanged(option);
                }
                continue;
            }

            if (string.Equals(element.localName, "input", StringComparison.OrdinalIgnoreCase))
            {
                element.value = element.defaultValue;
                var inputType = element.type?.Trim();
                if (string.Equals(inputType, "checkbox", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(inputType, "radio", StringComparison.OrdinalIgnoreCase))
                {
                    element.@checked = element.HasAttributePresence("checked");
                }
            }
            else if (string.Equals(element.localName, "textarea", StringComparison.OrdinalIgnoreCase))
            {
                element.value = element.defaultValue;
            }
        }
    }

    private static string NormalizeOptionText(string? value)
        => Regex.Replace(value ?? string.Empty, "[\\u0009\\u000A\\u000C\\u000D\\u0020]+", " ").Trim(' ');

    public virtual string? placeholder
    {
        get => Control switch
        {
            DomTextInputControl input => input.PlaceholderText,
            TextBox textBox => textBox.Watermark?.ToString() ?? string.Empty,
            _ => null
        };
        set
        {
            if (Control is DomTextInputControl input)
            {
                input.PlaceholderText = value ?? string.Empty;
                input.Watermark = null;
            }
            else if (Control is TextBox textBox)
            {
                textBox.Watermark = value ?? string.Empty;
            }

            if (value is null)
            {
                _attributeNames.Remove("placeholder");
            }
            else
            {
                _attributeNames.Add("placeholder");
            }
            SetGenericAttribute("placeholder", value);
        }
    }

    public virtual int? selectionStart
    {
        get => Control is TextBox textBox ? textBox.SelectionStart : null;
        set
        {
            if (Control is TextBox textBox && value is not null)
            {
                textBox.SelectionStart = Math.Clamp(value.Value, 0, (textBox.Text ?? string.Empty).Length);
            }
        }
    }

    public virtual int? selectionEnd
    {
        get => Control is TextBox textBox ? textBox.SelectionEnd : null;
        set
        {
            if (Control is TextBox textBox && value is not null)
            {
                textBox.SelectionEnd = Math.Clamp(value.Value, 0, (textBox.Text ?? string.Empty).Length);
            }
        }
    }

    public virtual string? selectionDirection
    {
        get => Control is TextBox ? _selectionDirection : null;
        set => _selectionDirection = NormalizeSelectionDirection(value);
    }

    public virtual void setSelectionRange(int start, int end, string? direction = null)
    {
        if (Control is not TextBox textBox)
        {
            return;
        }

        var length = (textBox.Text ?? string.Empty).Length;
        var normalizedEnd = Math.Clamp(end, 0, length);
        var normalizedStart = Math.Clamp(start, 0, normalizedEnd);
        textBox.SelectionStart = normalizedStart;
        textBox.SelectionEnd = normalizedEnd;
        _selectionDirection = NormalizeSelectionDirection(direction);
    }

    public virtual void select()
    {
        if (Control is TextBox textBox)
        {
            setSelectionRange(0, (textBox.Text ?? string.Empty).Length);
        }
    }

    private static string NormalizeSelectionDirection(string? direction)
        => direction?.ToLowerInvariant() switch
        {
            "forward" => "forward",
            "backward" => "backward",
            _ => "none"
        };

    public virtual int tabIndex
    {
        get => HasAttributePresence("tabindex")
            ? TryParseTabIndexAttribute(getAttribute("tabindex"), out var explicitValue) ? explicitValue : -1
            : HasDefaultZeroTabIndex() ? 0 : -1;
        set => setAttribute("tabindex", value.ToString(CultureInfo.InvariantCulture));
    }

    public virtual int maxLength
    {
        get => int.TryParse(
            getAttribute("maxlength"),
            NumberStyles.Integer,
            CultureInfo.InvariantCulture,
            out var length) ? length : -1;
        set => setAttribute("maxlength", value.ToString(CultureInfo.InvariantCulture));
    }

    public virtual string defaultValue
    {
        get => getAttribute("value") ?? string.Empty;
        set => setAttribute("value", value ?? string.Empty);
    }

    public virtual bool readOnly
    {
        get => HasAttributePresence("readonly");
        set => setAttribute("readonly", value ? string.Empty : null);
    }

    public virtual int rowSpan
    {
        get => ParsePositiveIntegerAttribute("rowspan", 1);
        set => setAttribute("rowspan", value.ToString(CultureInfo.InvariantCulture));
    }

    public virtual int colSpan
    {
        get => ParsePositiveIntegerAttribute("colspan", 1);
        set => setAttribute("colspan", value.ToString(CultureInfo.InvariantCulture));
    }

    public virtual string cellSpacing
    {
        get => getAttribute("cellspacing") ?? string.Empty;
        set => setAttribute("cellspacing", value ?? string.Empty);
    }

    public virtual string cellPadding
    {
        get => getAttribute("cellpadding") ?? string.Empty;
        set => setAttribute("cellpadding", value ?? string.Empty);
    }

    public virtual string enctype
    {
        get => getAttribute("enctype") ?? "application/x-www-form-urlencoded";
        set => setAttribute("enctype", value ?? string.Empty);
    }

    private int ParsePositiveIntegerAttribute(string name, int fallback)
        => int.TryParse(getAttribute(name), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed)
           && parsed > 0
            ? parsed
            : fallback;

    public virtual bool isFocused => Control.IsFocused;

    public virtual bool focus()
    {
        if (HasHiddenComputedVisibility())
        {
            return false;
        }

        if (!Control.Focusable)
        {
            Control.Focusable = true;
        }

        var actualFocus = TryFocusControl(NavigationMethod.Unspecified, KeyModifiers.None);
        return OwnerDocument.ActivateElement(this, dispatchFocusEvent: !actualFocus, clearActualFocus: !actualFocus);
    }

    internal bool HasHiddenComputedVisibility()
    {
        var visibility = GetStyleValue("visibility");
        return string.Equals(visibility, "hidden", StringComparison.OrdinalIgnoreCase)
               || string.Equals(visibility, "collapse", StringComparison.OrdinalIgnoreCase);
    }

    private bool IsNaturallyFocusableElement()
    {
        if (localName is "button" or "select" or "textarea")
        {
            return true;
        }

        if (localName == "input")
        {
            return !string.Equals(type?.Trim(), "hidden", StringComparison.OrdinalIgnoreCase);
        }

        return localName is "a" or "area"
               && !string.IsNullOrEmpty(getAttribute("href"));
    }

    private bool HasDefaultZeroTabIndex()
        => localName == "input" || IsNaturallyFocusableElement();

    private static bool TryParseTabIndexAttribute(string? value, out int tabIndex)
    {
        tabIndex = 0;
        if (string.IsNullOrEmpty(value))
        {
            return false;
        }

        var index = 0;
        while (index < value.Length && value[index] is ' ' or '\t' or '\n' or '\f' or '\r')
        {
            index++;
        }

        var negative = false;
        if (index < value.Length && value[index] is '+' or '-')
        {
            negative = value[index] == '-';
            index++;
        }

        if (index >= value.Length || value[index] is < '0' or > '9')
        {
            return false;
        }

        var limit = negative ? 2147483648L : int.MaxValue;
        long parsed = 0;
        while (index < value.Length && value[index] is >= '0' and <= '9')
        {
            var digit = value[index] - '0';
            if (parsed > (limit - digit) / 10)
            {
                return false;
            }
            parsed = parsed * 10 + digit;
            index++;
        }

        tabIndex = negative
            ? parsed == 2147483648L ? int.MinValue : -(int)parsed
            : (int)parsed;
        return true;
    }

    private void ApplyTabIndexState()
    {
        var hasExplicitValue = HasAttributePresence("tabindex");
        var parsed = TryParseTabIndexAttribute(getAttribute("tabindex"), out var explicitValue);
        var naturallyFocusable = IsNaturallyFocusableElement();
        var effectiveValue = hasExplicitValue
            ? parsed ? explicitValue : -1
            : naturallyFocusable ? 0 : -1;

        KeyboardNavigation.SetTabIndex(Control, effectiveValue);
        var canReceiveFocus = localName != "input"
                              || !string.Equals(type?.Trim(), "hidden", StringComparison.OrdinalIgnoreCase);
        Control.Focusable = canReceiveFocus && (hasExplicitValue || naturallyFocusable);
        Control.IsTabStop = Control.Focusable && effectiveValue >= 0;
    }

    public virtual bool focus(object? options)
    {
        // Avalonia focus does not scroll the retained DOM viewport, so the
        // browser's preventScroll option is already the effective behavior.
        return focus();
    }

    public virtual void blur()
    {
        var hasActualFocus = ReferenceEquals(Host.TopLevel.FocusManager?.GetFocusedElement(), Control);
        OwnerDocument.ReleaseElement(this, dispatchBlurEvent: !hasActualFocus, clearActualFocus: hasActualFocus);
    }

    private bool TryFocusControl(NavigationMethod navigationMethod, KeyModifiers keyModifiers)
    {
        if (Control.Focus(navigationMethod, keyModifiers))
        {
            return true;
        }

        var focusManager = Host.TopLevel.FocusManager;
        var focusMethod = focusManager?.GetType().GetMethod(
            "Focus",
            new[] { typeof(IInputElement), typeof(NavigationMethod), typeof(KeyModifiers) });

        if (focusMethod?.Invoke(focusManager, new object?[] { Control, navigationMethod, keyModifiers }) is bool result)
        {
            return result;
        }

        return Control.IsFocused;
    }

    [ThreadStatic]
    private static bool _inGetClientSize;

    private Size GetClientSize()
    {
        if (_inGetClientSize)
        {
            return Control.Bounds.Size;
        }

        _inGetClientSize = true;
        try
        {
            OwnerDocument.FlushPendingLayout();
            if (!HasLayoutBox())
            {
                return default;
            }
            if (Control is ScrollViewer viewer)
            {
                var viewport = viewer.Viewport;
                if (IsFiniteSize(viewport))
                {
                    return viewport;
                }
            }

            if (TryGetScrollInfo(out _, out var scrollable))
            {
                var viewport = scrollable.Viewport;
                if (IsFiniteSize(viewport))
                {
                    return viewport;
                }
            }

            return s_disableLayoutReadFastPaths
                ? GetElementBounds().Size
                : GetGenericLayoutSize(Control);
        }
        finally
        {
            _inGetClientSize = false;
        }
    }

    private Size GetScrollSize()
    {
        OwnerDocument.FlushPendingLayout();
        if (Control is CssLayoutPanel panel)
        {
            var extent = panel.ScrollExtent;
            var viewport = panel.ScrollViewport;
            return new Size(
                Math.Max(extent.Width, viewport.Width),
                Math.Max(extent.Height, viewport.Height));
        }

        if (Control is ScrollViewer viewer)
        {
            var width = Math.Max(viewer.Extent.Width, viewer.Viewport.Width);
            var height = Math.Max(viewer.Extent.Height, viewer.Viewport.Height);
            return new Size(double.IsFinite(width) ? width : 0, double.IsFinite(height) ? height : 0);
        }

        if (TryGetScrollInfo(out _, out var scrollable))
        {
            var viewport = scrollable.Viewport;
            var extent = scrollable.Extent;
            var width = Math.Max(extent.Width, viewport.Width);
            var height = Math.Max(extent.Height, viewport.Height);
            return new Size(double.IsFinite(width) ? width : 0, double.IsFinite(height) ? height : 0);
        }

        return GetElementBounds().Size;
    }

    [ThreadStatic]
    private static bool _inGetElementBounds;

    private Rect GetElementBounds()
    {
        if (_inGetElementBounds)
        {
            return new Rect(Control.Bounds.Position, GetGenericLayoutSize(Control));
        }

        _inGetElementBounds = true;
        try
        {
            OwnerDocument.FlushPendingLayout();
            if (!HasLayoutBox())
            {
                return default;
            }
            var size = GetGenericLayoutSize(Control);
            var viewport = OwnerDocument.GetDocumentViewport();
            if (viewport is not null)
            {
                var topLeft = Control.TranslatePoint(new Point(0, 0), viewport);
                var topRight = Control.TranslatePoint(new Point(size.Width, 0), viewport);
                var bottomRight = Control.TranslatePoint(new Point(size.Width, size.Height), viewport);
                var bottomLeft = Control.TranslatePoint(new Point(0, size.Height), viewport);
                if (topLeft is { } first
                    && topRight is { } second
                    && bottomRight is { } third
                    && bottomLeft is { } fourth)
                {
                    var left = Math.Min(Math.Min(first.X, second.X), Math.Min(third.X, fourth.X));
                    var top = Math.Min(Math.Min(first.Y, second.Y), Math.Min(third.Y, fourth.Y));
                    var right = Math.Max(Math.Max(first.X, second.X), Math.Max(third.X, fourth.X));
                    var bottom = Math.Max(Math.Max(first.Y, second.Y), Math.Max(third.Y, fourth.Y));
                    return new Rect(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top));
                }
            }

            return new Rect(ComputeGenericPositionInViewport(), size);
        }
        finally
        {
            _inGetElementBounds = false;
        }
    }

    internal bool ContainsViewportPoint(Point point)
    {
        var size = GetGenericLayoutSize(Control);
        var viewport = OwnerDocument.GetDocumentViewport();
        var local = viewport?.TranslatePoint(point, Control);
        var containsOwnBorderBox = false;
        if (local is { } transformed)
        {
            // TranslatePoint applies the inverse of the complete visual
            // transform chain. Testing the untransformed local border box is
            // therefore precise for scale, rotation, transform-origin, and
            // transformed ancestors rather than accepting the empty corners
            // of an axis-aligned transformed bounding rectangle.
            containsOwnBorderBox = transformed.X >= 0
                                   && transformed.X < size.Width
                                   && transformed.Y >= 0
                                   && transformed.Y < size.Height;
        }
        else
        {
            var bounds = GetElementBounds();
            containsOwnBorderBox = point.X >= bounds.Left
                                   && point.X < bounds.Right
                                   && point.Y >= bounds.Top
                                   && point.Y < bounds.Bottom;
        }
        return containsOwnBorderBox && IsInsideAncestorOverflowClips(point, viewport);
    }

    private bool IsInsideAncestorOverflowClips(Point point, Control? viewport)
    {
        var escapesIntermediateClips =
            CssLayout.GetPosition(Control) == CssPosition.Fixed;
        for (var ancestor = parentElement; ancestor is not null; ancestor = ancestor.parentElement)
        {
            if (ancestor.Control is CssLayoutPanel panel
                && (!escapesIntermediateClips || CssLayout.GetDocumentViewportRoot(panel))
                && (panel.OverflowX != "visible" || panel.OverflowY != "visible"))
            {
                var size = GetGenericLayoutSize(ancestor.Control);
                var local = viewport?.TranslatePoint(point, ancestor.Control);
                var x = local?.X;
                var y = local?.Y;
                if (x is null || y is null)
                {
                    var bounds = ancestor.GetElementBounds();
                    x = point.X - bounds.Left;
                    y = point.Y - bounds.Top;
                }
                if (panel.OverflowX != "visible"
                    && (x < 0 || x >= size.Width))
                {
                    return false;
                }
                if (panel.OverflowY != "visible"
                    && (y < 0 || y >= size.Height))
                {
                    return false;
                }
            }

            if (CssLayout.GetPosition(ancestor.Control) == CssPosition.Fixed)
            {
                escapesIntermediateClips = true;
            }
        }
        return true;
    }

    private Size GetLayoutSize()
    {
        OwnerDocument.FlushPendingLayout();
        return HasLayoutBox() ? GetGenericLayoutSize(Control) : default;
    }

    private bool HasLayoutBox()
    {
        for (var current = this; current is not null; current = current.parentElement)
        {
            if (!current.Control.IsVisible)
            {
                return false;
            }

            var display = CssLayout.GetDisplay(current.Control);
            if (display is CssDisplay.TableColumn or CssDisplay.TableColumnGroup)
            {
                return false;
            }
            if (current.parentElement is { } parent
                && CssLayout.GetDisplay(parent.Control) == CssDisplay.TableColumnGroup
                && display != CssDisplay.TableColumn)
            {
                return false;
            }

            // The DOM layout tree ends at body. Native TopLevel visibility is
            // independent (and is commonly false in headless pre-show tests).
            if (string.Equals(current.tagName, "BODY", StringComparison.OrdinalIgnoreCase))
            {
                break;
            }
        }

        return true;
    }

    private Size GetGenericLayoutSize(Control control)
    {
        var bounds = control.Bounds;
        // getBoundingClientRect reports the final arranged border box. In
        // particular, a percentage-sized popup can live below an intentionally
        // zero-sized portal while min/max constraints still give the popup a
        // real arranged size. Resolving the percentage against that portal a
        // second time would incorrectly replace the arranged size with zero.
        var width = double.IsFinite(bounds.Width) && bounds.Width > 0
            ? bounds.Width
            : GetExplicitOrArrangedSize(control.Width, bounds.Width);
        var height = double.IsFinite(bounds.Height) && bounds.Height > 0
            ? bounds.Height
            : GetExplicitOrArrangedSize(control.Height, bounds.Height);

        if (control.Parent is CssLayoutPanel cssParent)
        {
            var parentSize = GetGenericLayoutSize(cssParent);
            if (width <= 0)
            {
                width = CssLayout.Resolve(CssLayout.GetWidth(control), parentSize.Width) ?? width;
            }
            if (height <= 0)
            {
                height = CssLayout.Resolve(CssLayout.GetHeight(control), parentSize.Height) ?? height;
            }
        }

        if (width <= 0 && (control is Canvas || control is CanvasDrawingSurface || control is CanvasOpenGlDrawingSurface))
        {
            width = 300;
        }
        if (height <= 0 && (control is Canvas || control is CanvasDrawingSurface || control is CanvasOpenGlDrawingSurface))
        {
            height = 150;
        }

        return new Size(Math.Max(0, width), Math.Max(0, height));
    }

    private Point ComputeGenericPositionInViewport()
    {
        var x = 0d;
        var y = 0d;
        Control? current = Control;

        while (current is not null)
        {
            var parent = current.Parent as Control;
            if (parent is null)
            {
                break;
            }

            var local = current.Bounds.Position;
            if (parent is CssLayoutPanel)
            {
                var position = CssLayout.GetPosition(current);
                if (position is CssPosition.Absolute or CssPosition.Fixed)
                {
                    var parentSize = GetGenericLayoutSize(parent);
                    var childSize = GetGenericLayoutSize(current);
                    var left = CssLayout.Resolve(CssLayout.GetLeft(current), parentSize.Width);
                    var top = CssLayout.Resolve(CssLayout.GetTop(current), parentSize.Height);
                    local = new Point(
                        left ?? (CssLayout.Resolve(CssLayout.GetRight(current), parentSize.Width) is { } right
                            ? parentSize.Width - childSize.Width - right
                            : 0d),
                        top ?? (CssLayout.Resolve(CssLayout.GetBottom(current), parentSize.Height) is { } bottom
                            ? parentSize.Height - childSize.Height - bottom
                            : 0d));
                }
            }
            else if (parent is Canvas)
            {
                var left = Canvas.GetLeft(current);
                var top = Canvas.GetTop(current);
                local = new Point(double.IsNaN(left) ? local.X : left, double.IsNaN(top) ? local.Y : top);
            }

            x += local.X;
            y += local.Y;
            current = parent;
        }

        return new Point(x, y);
    }


    private Vector GetScrollOffset()
    {
        if (Control is CssLayoutPanel panel)
        {
            var offset = panel.ScrollOffset;
            _scrollLeft = offset.X;
            _scrollTop = offset.Y;
            return offset;
        }

        if (Control is ScrollViewer viewer)
        {
            var offset = viewer.Offset;
            _scrollLeft = offset.X;
            _scrollTop = offset.Y;
            return offset;
        }

        if (TryGetScrollInfo(out _, out var scrollable))
        {
            var offset = scrollable.Offset;
            if (double.IsFinite(offset.X) && double.IsFinite(offset.Y))
            {
                _scrollLeft = offset.X;
                _scrollTop = offset.Y;
                return offset;
            }
        }

        return new Vector(_scrollLeft, _scrollTop);
    }

    private void SetScrollOffset(double left, double top)
    {
        OwnerDocument.EnsureStylesCurrent();
        OwnerDocument.FlushPendingLayout();
        var desired = new Vector(left, top);
        _scrollLeft = left;
        _scrollTop = top;

        if (Control is CssLayoutPanel panel)
        {
            panel.SetScrollOffset(desired);
            var panelOffset = panel.ScrollOffset;
            _scrollLeft = panelOffset.X;
            _scrollTop = panelOffset.Y;
            return;
        }

        if (Control is ScrollViewer viewer)
        {
            var viewerViewport = viewer.Viewport;
            var viewerExtent = viewer.Extent;
            var viewerMaxX = Math.Max(0, viewerExtent.Width - viewerViewport.Width);
            var viewerMaxY = Math.Max(0, viewerExtent.Height - viewerViewport.Height);
            var viewerClampedX = Clamp(desired.X, 0, double.IsFinite(viewerMaxX) ? viewerMaxX : 0);
            var viewerClampedY = Clamp(desired.Y, 0, double.IsFinite(viewerMaxY) ? viewerMaxY : 0);

            var viewerOffset = new Vector(viewerClampedX, viewerClampedY);
            if (!AreClose(viewer.Offset.X, viewerClampedX) || !AreClose(viewer.Offset.Y, viewerClampedY))
            {
                viewer.SetValue(ScrollViewer.OffsetProperty, viewerOffset);
            }
            _scrollLeft = viewerClampedX;
            _scrollTop = viewerClampedY;
            return;
        }

        if (!TryGetScrollInfo(out var owner, out var scrollable))
        {
            return;
        }

        var viewport = scrollable.Viewport;
        var extent = scrollable.Extent;

        var maxX = Math.Max(0, extent.Width - viewport.Width);
        var maxY = Math.Max(0, extent.Height - viewport.Height);

        var clampedX = Clamp(desired.X, 0, double.IsFinite(maxX) ? maxX : 0);
        var clampedY = Clamp(desired.Y, 0, double.IsFinite(maxY) ? maxY : 0);
        _scrollLeft = clampedX;
        _scrollTop = clampedY;
        var current = scrollable.Offset;

        if (AreClose(current.X, clampedX) && AreClose(current.Y, clampedY))
        {
            return;
        }

        ApplyScrollOffset(owner, new Vector(clampedX, clampedY));
    }

    private void ApplyScrollOffset(Control owner, Vector offset)
    {
        switch (owner)
        {
            case ScrollViewer viewer:
                viewer.Offset = offset;
                return;
            default:
            {
                var property = owner.GetType().GetProperty("Offset", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
                if (property?.CanWrite == true)
                {
                    property.SetValue(owner, offset);
                    return;
                }

                var registered = FindAvaloniaProperty(owner.GetType(), "Offset");
                if (registered is not null && registered.PropertyType == typeof(Vector))
                {
                    owner.SetValue(registered, offset);
                }

                break;
            }
        }
    }

    private bool TryGetScrollInfo(out Control owner, out IScrollable scrollable)
    {
        if (_cachedScrollable is not null
            && _cachedScrollable.TryGetTarget(out var cachedScrollable)
            && _cachedScrollOwner is not null
            && _cachedScrollOwner.TryGetTarget(out var cachedOwner)
            && cachedScrollable is not null
            && cachedOwner is not null)
        {
            owner = cachedOwner;
            scrollable = cachedScrollable;
            return true;
        }

        if (Control is IScrollable direct)
        {
            owner = Control;
            scrollable = direct;
            CacheScrollable(owner, scrollable);
            return true;
        }

        // Avalonia's GetVisualDescendants() is a recursive iterator and creates
        // one iterator/enumerator per visited visual. Geometry reads call this
        // path frequently; the indexed FindDescendantOfType traversal avoids
        // that allocation while preserving the same depth-first match.
        if (Control.FindDescendantOfType<IScrollable>() is { } candidateScrollable
            && candidateScrollable is Control candidate)
        {
            owner = candidate;
            scrollable = candidateScrollable;
            CacheScrollable(owner, scrollable);
            return true;
        }

        owner = null!;
        scrollable = null!;
        return false;
    }

    private void CacheScrollable(Control owner, IScrollable scrollable)
    {
        _cachedScrollOwner = new WeakReference<Control>(owner);
        _cachedScrollable = new WeakReference<IScrollable>(scrollable);
    }

    private static bool IsFiniteSize(Size size)
        => double.IsFinite(size.Width) && double.IsFinite(size.Height) && size.Width >= 0 && size.Height >= 0;

    private static double GetExplicitOrArrangedSize(double explicitSize, double arrangedSize)
    {
        if (double.IsFinite(explicitSize) && explicitSize >= 0)
        {
            return explicitSize;
        }

        return double.IsFinite(arrangedSize) && arrangedSize >= 0 ? arrangedSize : 0;
    }

    private static double Clamp(double value, double min, double max)
    {
        if (max < min)
        {
            (min, max) = (max, min);
        }

        if (value < min)
        {
            return min;
        }

        if (value > max)
        {
            return max;
        }

        return value;
    }

    private static bool AreClose(double a, double b)
        => Math.Abs(a - b) < 0.001;

    private Point GetOffsetRelativeToParent()
    {
        // CSSOM offset geometry is a synchronous layout read. Inline style
        // mutations schedule retained layout work, but scripts are allowed to
        // read offsetTop/offsetLeft again in the same task and must observe the
        // new hypothetical position of out-of-flow descendants immediately.
        OwnerDocument.FlushPendingLayout();
        var parent = FindOffsetParentControl();
        if (parent is null)
        {
            return Control.Bounds.Position;
        }

        var translated = Control.TranslatePoint(new Point(0, 0), parent);
        if (translated.HasValue)
        {
            return RestoreScrolledLayoutOffset(translated.Value, parent);
        }

        var deltaX = Control.Bounds.Left - parent.Bounds.Left;
        var deltaY = Control.Bounds.Top - parent.Bounds.Top;
        return RestoreScrolledLayoutOffset(new Point(deltaX, deltaY), parent);
    }

    private Point RestoreScrolledLayoutOffset(Point point, Control offsetParent)
    {
        for (var current = Control.GetVisualParent() as Control; current is not null;
             current = current.GetVisualParent() as Control)
        {
            point += current switch
            {
                CssLayoutPanel panel => panel.ScrollOffset,
                ScrollViewer viewer => viewer.Offset,
                _ => default
            };
            if (ReferenceEquals(current, offsetParent)) break;
        }
        return point;
    }

    private Control? FindOffsetParentControl()
    {
        if (CssLayout.GetPosition(Control) == CssPosition.Fixed)
        {
            return null;
        }

        for (var parent = parentElement; parent is not null; parent = parent.parentElement)
        {
            if (string.Equals(parent.tagName, "BODY", StringComparison.OrdinalIgnoreCase)
                || parent.tagName is "TABLE" or "TD" or "TH"
                || CssLayout.GetPosition(parent.Control) != CssPosition.Static)
            {
                return parent.Control;
            }
        }

        return null;
    }

    private AvaloniaDomElement? FindOffsetParentElement()
    {
        var parentControl = FindOffsetParentControl();
        return parentControl is null ? null : OwnerDocument.WrapControl(parentControl);
    }

    public virtual void addEventListener(string type, object handler)
        => addEventListener(type, handler, options: null);

    public virtual void addEventListener(string type, object handler, object? options)
    {
        var adapter = OwnerDocument.ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: true);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        __htmlMlAddExternalEventListener(
            type,
            listener,
            parsed.Capture,
            parsed.Once,
            parsed.Passive);
    }

    public virtual void removeEventListener(string type, object handler)
        => removeEventListener(type, handler, options: null);

    public virtual void removeEventListener(string type, object handler, object? options)
    {
        var adapter = OwnerDocument.ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: false);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        __htmlMlRemoveExternalEventListener(type, listener, parsed.Capture);
    }

    public void __htmlMlAddExternalEventListener(
        string type,
        IExternalDomEventListener listener,
        bool capture,
        bool once,
        bool passive)
    {
        var trimmedType = type?.Trim() ?? string.Empty;
        var normalized = NormalizeEventName(trimmedType);
        if (string.IsNullOrEmpty(normalized) || listener is null)
        {
            return;
        }

        EnsureClrEventBridge(normalized, trimmedType);
        var listeners = GetOrCreateEventListeners(normalized);
        if (listeners.Any(item => item.Matches(listener, capture)))
        {
            return;
        }

        listeners.Add(new DomEventRegistration(
            listener,
            new EventListenerOptions(capture, once, passive)));
    }

    public void __htmlMlRemoveExternalEventListener(
        string type,
        IExternalDomEventListener listener,
        bool capture)
    {
        var normalized = NormalizeEventName(type);
        if (string.IsNullOrEmpty(normalized)
            || listener is null
            || !_eventListeners.TryGetValue(normalized, out var listeners))
        {
            return;
        }

        for (var index = 0; index < listeners.Count; index++)
        {
            if (listeners[index].Matches(listener, capture))
            {
                listeners.RemoveAt(index);
                break;
            }
        }

        if (listeners.Count == 0)
        {
            _eventListeners.Remove(normalized);
            TryDetachClrEvent(normalized);
        }
    }

    internal bool HasListeners(string type)
        => _eventListeners.TryGetValue(type, out var list) && list.Count > 0;

    internal List<DomEventRegistration>? GetListeners(string type)
        => _eventListeners.TryGetValue(type, out var list) ? list : null;

    internal void RemoveListener(string type, DomEventRegistration listener)
    {
        if (_eventListeners.TryGetValue(type, out var list) && list.Remove(listener) && list.Count == 0)
        {
            _eventListeners.Remove(type);
            TryDetachClrEvent(type);
        }
    }

    private List<DomEventRegistration> GetOrCreateEventListeners(string eventName)
    {
        if (!_eventListeners.TryGetValue(eventName, out var list))
        {
            list = new List<DomEventRegistration>();
            _eventListeners[eventName] = list;
        }

        return list;
    }

    internal void SetNodeNameOverride(string value)
    {
        _nodeNameOverride = string.IsNullOrEmpty(value) ? null : value;
        _tagNameCache = null;
        if (HasExplicitDomTag)
        {
            ApplyTabIndexState();
        }
        if (Control is SvgLayoutPanel svg
            && string.Equals(_nodeNameOverride, "svg", StringComparison.OrdinalIgnoreCase))
        {
            svg.SceneProvider = BuildSvgScene;
        }
    }

    internal void SetNamespaceUri(string? value)
    {
        _namespaceUri = string.IsNullOrWhiteSpace(value) ? null : value;
        _tagNameCache = null;
    }

    internal void SetXmlMode(string? namespaceUri)
    {
        _xmlMode = true;
        _namespaceUri = string.IsNullOrEmpty(namespaceUri) ? null : namespaceUri;
        _tagNameCache = null;
    }

    internal bool HasExplicitDomTag
        => _nodeNameOverride is not null
           && !string.Equals(_nodeNameOverride, "BODY", StringComparison.OrdinalIgnoreCase);

    private bool MatchesElementSelector(
        Control control,
        string selector,
        AvaloniaDomElement scopeElement)
    {
        var element = OwnerDocument.WrapControl(control);
        // Native layout controls are implementation details, not descendants
        // in the authored DOM tree, and must never leak through selectors such
        // as `:not(...)` or `*`.
        return element.HasExplicitDomTag && element.MatchesSelector(selector, scopeElement);
    }

    private bool IsCanvasControl()
    {
        return string.Equals(tagName, "CANVAS", StringComparison.OrdinalIgnoreCase)
               || Control is CanvasDrawingSurface
               || Control is CanvasOpenGlDrawingSurface;
    }

    private bool IsExplicitHtmlCanvasElement()
        => string.Equals(_nodeNameOverride, "CANVAS", StringComparison.OrdinalIgnoreCase);

    private static bool TryParseAttributeDouble(string? value, out double result)
    {
        result = 0;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        return double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out result) &&
               double.IsFinite(result) &&
               result >= 0;
    }

    private void EnsureEventBridges()
    {
        if (!_pointerHandlersAttached)
        {
            AttachPointerHandlers();
        }

        if (!_pointerOverHandlersAttached)
        {
            AttachPointerOverHandlers();
        }

        if (!_wheelHandlersAttached)
        {
            AttachWheelHandlers();
        }

        if (!_keyboardHandlersAttached)
        {
            AttachKeyboardHandlers();
        }

        if (!_textInputHandlersAttached)
        {
            AttachTextInputHandlers();
        }

        if (!_clickHandlersAttached)
        {
            AttachClickHandlers();
        }

        if (!_focusHandlersAttached)
        {
            AttachFocusHandlers();
        }
    }

    private void EnsureClrEventBridge(string normalizedEventName, string originalEventName)
    {
        if (string.IsNullOrEmpty(originalEventName) || _clrEventBridges.ContainsKey(normalizedEventName) || s_builtinEventNames.Contains(normalizedEventName))
        {
            return;
        }

        var eventInfo = FindClrEvent(originalEventName);
        if (eventInfo is null)
        {
            return;
        }

        if (!TryCreateClrEventDelegate(eventInfo, normalizedEventName, out var handler))
        {
            return;
        }

        try
        {
            eventInfo.AddEventHandler(Control, handler);
            _clrEventBridges[normalizedEventName] = new ClrEventBridge(eventInfo, handler, originalEventName);
        }
        catch
        {
            // Ignore binding failures; event will simply not be bridged.
        }
    }

    private void TryDetachClrEvent(string eventName)
    {
        var normalized = NormalizeEventName(eventName);
        if (string.IsNullOrEmpty(normalized))
        {
            return;
        }

        if (!_clrEventBridges.TryGetValue(normalized, out var bridge))
        {
            return;
        }

        try
        {
            bridge.EventInfo.RemoveEventHandler(Control, bridge.Handler);
        }
        catch
        {
        }

        _clrEventBridges.Remove(normalized);
    }

    private EventInfo? FindClrEvent(string eventName)
    {
        if (string.IsNullOrWhiteSpace(eventName))
        {
            return null;
        }

        var type = Control.GetType();
        while (type is not null)
        {
            var eventInfo = type.GetEvent(eventName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.IgnoreCase);
            if (eventInfo is not null)
            {
                return eventInfo;
            }

            type = type.BaseType;
        }

        return null;
    }

    private bool TryCreateClrEventDelegate(EventInfo eventInfo, string normalizedEventName, out Delegate handler)
    {
        handler = null!;
        var handlerType = eventInfo.EventHandlerType;
        if (handlerType is null)
        {
            return false;
        }

        var invoke = handlerType.GetMethod("Invoke");
        if (invoke is null)
        {
            return false;
        }

        var parameters = invoke.GetParameters();
        var method = typeof(AvaloniaDomElement).GetMethod(nameof(OnClrEventRaised), BindingFlags.Instance | BindingFlags.NonPublic);
        if (method is null)
        {
            return false;
        }

        if (parameters.Length == 2)
        {
            var senderParam = Expression.Parameter(parameters[0].ParameterType, "sender");
            var argsParam = Expression.Parameter(parameters[1].ParameterType, "args");
            var body = Expression.Call(Expression.Constant(this), method, Expression.Constant(normalizedEventName), Expression.Convert(senderParam, typeof(object)), Expression.Convert(argsParam, typeof(object)));
            handler = Expression.Lambda(handlerType, body, senderParam, argsParam).Compile();
            return true;
        }

        if (parameters.Length == 1)
        {
            var argsParam = Expression.Parameter(parameters[0].ParameterType, "args");
            var body = Expression.Call(Expression.Constant(this), method, Expression.Constant(normalizedEventName), Expression.Constant(null, typeof(object)), Expression.Convert(argsParam, typeof(object)));
            handler = Expression.Lambda(handlerType, body, argsParam).Compile();
            return true;
        }

        return false;
    }

    private void OnClrEventRaised(string eventKey, object? sender, object? args)
    {
        if (!HasListeners(eventKey))
        {
            return;
        }

        var eventName = eventKey;
        if (_clrEventBridges.TryGetValue(eventKey, out var bridge) && !string.IsNullOrEmpty(bridge.EventName))
        {
            eventName = bridge.EventName;
        }

        if (args is RoutedEventArgs routedArgs)
        {
            OwnerDocument.DispatchRoutedEvent(this, eventName, routedArgs, bubbles: true, cancelable: true);
            return;
        }

        var synthetic = new DomSyntheticEvent(eventName, bubbles: false, cancelable: false, Host.GetTimestamp(), args, accessor: null);
        OwnerDocument.DispatchSyntheticEvent(this, synthetic);
    }

    private void AttachPointerHandlers()
    {
        _pointerHandlersAttached = true;
        Control.AddHandler(InputElement.PointerPressedEvent, OnPointerPressed, RoutingStrategies.Direct | RoutingStrategies.Bubble, handledEventsToo: true);
        Control.AddHandler(InputElement.PointerMovedEvent, OnPointerMoved, RoutingStrategies.Direct | RoutingStrategies.Bubble, handledEventsToo: true);
        Control.AddHandler(InputElement.PointerReleasedEvent, OnPointerReleased, RoutingStrategies.Direct | RoutingStrategies.Bubble, handledEventsToo: true);
    }

    private void AttachPointerOverHandlers()
    {
        _pointerOverHandlersAttached = true;
        Control.PointerEntered += OnPointerEntered;
        Control.PointerExited += OnPointerExited;
    }

    private void AttachWheelHandlers()
    {
        _wheelHandlersAttached = true;
        Control.AddHandler(InputElement.PointerWheelChangedEvent, OnPointerWheelChanged, RoutingStrategies.Direct | RoutingStrategies.Bubble, handledEventsToo: true);
    }

    private void AttachKeyboardHandlers()
    {
        _keyboardHandlersAttached = true;
        Control.AddHandler(InputElement.KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, handledEventsToo: true);
        Control.AddHandler(InputElement.KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, handledEventsToo: true);
    }

    private void AttachTextInputHandlers()
    {
        _textInputHandlersAttached = true;
        Control.AddHandler(InputElement.TextInputEvent, OnTextInput, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, handledEventsToo: true);
    }

    private void AttachClickHandlers()
    {
        _clickHandlersAttached = true;
        if (Control is Button button)
        {
            button.Click += OnButtonClick;
        }
    }

    private void AttachFocusHandlers()
    {
        _focusHandlersAttached = true;
        Control.GotFocus += OnGotFocus;
        Control.LostFocus += OnLostFocus;
    }

    private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!AvaloniaDomDocument.TryBeginPointerDispatch(e))
        {
            return;
        }

        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        OwnerDocument.DispatchPointerEvent(target, "pointerdown", e, bubbles: true, cancelable: true);
        OwnerDocument.DispatchPointerEvent(target, "mousedown", e, bubbles: true, cancelable: true);
        if (AvaloniaDomDocument.IsSecondaryButtonPress(e, target.Control))
        {
            OwnerDocument.DispatchPointerEvent(target, "contextmenu", e, bubbles: true, cancelable: true);
        }
    }

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!AvaloniaDomDocument.TryBeginPointerDispatch(e))
        {
            return;
        }

        var activeDocument = AvaloniaDomDocument.GetActivePointerDocument(e.Pointer?.Id ?? 0);
        if (activeDocument is not null && !ReferenceEquals(activeDocument, OwnerDocument))
        {
            activeDocument.DispatchActivePointerEvent("pointermove", e, bubbles: true, cancelable: true);
            activeDocument.DispatchActivePointerEvent("mousemove", e, bubbles: true, cancelable: true);
            return;
        }

        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        OwnerDocument.DispatchPointerEvent(target, "pointermove", e, bubbles: true, cancelable: true);
        OwnerDocument.DispatchPointerEvent(target, "mousemove", e, bubbles: true, cancelable: true);
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (!AvaloniaDomDocument.TryBeginPointerDispatch(e))
        {
            return;
        }

        var pointerId = e.Pointer?.Id ?? 0;
        var activeDocument = AvaloniaDomDocument.GetActivePointerDocument(pointerId);
        if (activeDocument is not null && !ReferenceEquals(activeDocument, OwnerDocument))
        {
            activeDocument.DispatchActivePointerEvent("pointerup", e, bubbles: true, cancelable: true);
            activeDocument.DispatchActivePointerEvent("mouseup", e, bubbles: true, cancelable: true);
            activeDocument.CompletePointer(pointerId);
            return;
        }

        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        OwnerDocument.DispatchPointerEvent(target, "pointerup", e, bubbles: true, cancelable: true);
        OwnerDocument.DispatchPointerEvent(target, "mouseup", e, bubbles: true, cancelable: true);

        if (e.InitialPressMouseButton == MouseButton.Left
            && (Control is not Button || !ReferenceEquals(e.Source, Control)))
        {
            if (Control is Button)
            {
                _suppressNextNativeButtonClick = true;
                Host.Services.Dispatcher.Post(
                    () => _suppressNextNativeButtonClick = false,
                    HtmlMlDispatchPriority.Input);
            }
            OwnerDocument.DispatchPointerEvent(target, "click", e, bubbles: true, cancelable: true);
        }
        else if (AvaloniaDomDocument.IsAuxiliaryButton(e.InitialPressMouseButton))
        {
            OwnerDocument.DispatchPointerEvent(target, "auxclick", e, bubbles: true, cancelable: true);
        }

        OwnerDocument.CompletePointer(pointerId);
    }

    private void OnPointerEntered(object? sender, PointerEventArgs e)
    {
        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        OwnerDocument.UpdatePointerHover(target, e);
    }

    private void OnPointerExited(object? sender, PointerEventArgs e)
    {
        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        // Avalonia reports an old control's exit immediately before the new
        // control's enter when crossing siblings. Defer the terminal clear by
        // one input turn so the sibling enter can replace the hover target and
        // emit browser-compatible A -> B relatedTarget values. If no enter
        // follows (the pointer truly left this document), the deferred clear
        // still emits the normal events with a null relatedTarget.
        Host.Services.Dispatcher.Post(
            () => OwnerDocument.ClearPointerHover(target, e),
            HtmlMlDispatchPriority.Input);
    }

    private void OnPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (!AvaloniaDomDocument.TryBeginPointerDispatch(e))
        {
            return;
        }

        var target = OwnerDocument.ResolvePointerEventTarget(e.Source as Control, this, e);
        OwnerDocument.DispatchPointerEvent(target, "wheel", e, bubbles: true, cancelable: true);
        if (!e.Handled)
        {
            OwnerDocument.ApplyDefaultWheelScroll(target, e);
        }
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (!ReferenceEquals(e.Source, Control)
            || !AvaloniaDomDocument.TryBeginKeyboardDispatch(e))
        {
            return;
        }

        OwnerDocument.DispatchKeyboardEvent(this, "keydown", e, bubbles: true, cancelable: true);
    }

    private void DispatchTextInputKeyDown(KeyEventArgs e)
    {
        if (!AvaloniaDomDocument.TryBeginKeyboardDispatch(e))
        {
            return;
        }

        OwnerDocument.DispatchKeyboardEvent(this, "keydown", e, bubbles: true, cancelable: true);
    }

    private void DispatchNativeKeyInput()
    {
        var args = new TextInputEventArgs
        {
            RoutedEvent = InputElement.TextInputEvent,
            Source = Control,
            Text = string.Empty
        };
        OwnerDocument.DispatchTextInputEvent(this, "input", args, bubbles: true, cancelable: false);
    }

    private void OnKeyUp(object? sender, KeyEventArgs e)
    {
        if (!ReferenceEquals(e.Source, Control)
            || !AvaloniaDomDocument.TryBeginKeyboardDispatch(e))
        {
            return;
        }

        OwnerDocument.DispatchKeyboardEvent(this, "keyup", e, bubbles: true, cancelable: false);
    }

    private void OnTextInput(object? sender, TextInputEventArgs e)
    {
        if (!ReferenceEquals(e.Source, Control)
            || !AvaloniaDomDocument.TryBeginTextInputDispatch(e))
        {
            return;
        }

        OwnerDocument.DispatchTextInputEvent(this, "textinput", e, bubbles: true, cancelable: false);
        // TextInput tunnels before TextBox applies its edit. React-style input
        // handlers read currentTarget.value during the DOM `input` event, so a
        // synchronous bridge exposes the previous value and leaves controlled
        // search fields unchanged. Queue `input` behind Avalonia's native text
        // mutation while preserving the trusted text payload.
        if (string.Equals(getAttribute("data-role"), "search", StringComparison.OrdinalIgnoreCase))
        {
            _pendingSearchInputText = e.Text ?? string.Empty;
            Interlocked.Increment(ref _searchInputDebounceVersion);
            if (_searchInputDebounceTimer is null)
            {
                _searchInputDebounceTimer = new System.Threading.Timer(
                    _ =>
                    {
                        var scheduledVersion = Volatile.Read(ref _searchInputDebounceVersion);
                        Host.Services.Dispatcher.Post(
                            () => DispatchDebouncedSearchInput(scheduledVersion),
                            HtmlMlDispatchPriority.Input);
                    });
                Control.DetachedFromVisualTree += OnSearchInputDetached;
            }
            // Coalesce a burst to the next frame without adding a visible
            // pause before the first filtered result set is committed.
            _searchInputDebounceTimer.Change(16, Timeout.Infinite);
        }
        else
        {
            Host.Services.Dispatcher.Post(
                () => OwnerDocument.DispatchTextInputEvent(this, "input", e, bubbles: true, cancelable: false),
                HtmlMlDispatchPriority.Input);
        }
    }

    private void DispatchDebouncedSearchInput(int version)
    {
        if (version != Volatile.Read(ref _searchInputDebounceVersion))
        {
            return;
        }
        var args = new TextInputEventArgs
        {
            RoutedEvent = InputElement.TextInputEvent,
            Source = Control,
            Text = _pendingSearchInputText
        };
        OwnerDocument.DispatchTextInputEvent(this, "input", args, bubbles: true, cancelable: false);
    }

    private void OnSearchInputDetached(object? sender, VisualTreeAttachmentEventArgs e)
    {
        Control.DetachedFromVisualTree -= OnSearchInputDetached;
        Interlocked.Increment(ref _searchInputDebounceVersion);
        _searchInputDebounceTimer?.Dispose();
        _searchInputDebounceTimer = null;
    }

    private void OnButtonClick(object? sender, RoutedEventArgs e)
    {
        if (IsDisabledFormControl)
        {
            return;
        }
        if (_suppressNextNativeButtonClick)
        {
            _suppressNextNativeButtonClick = false;
            return;
        }
        OwnerDocument.DispatchRoutedEvent(this, "click", e, bubbles: true, cancelable: true);
    }

    private void OnGotFocus(object? sender, GotFocusEventArgs e)
    {
        if (!ReferenceEquals(e.Source, Control))
        {
            return;
        }

        OwnerDocument.NotifyActualFocus(this);
    }

    private void OnLostFocus(object? sender, RoutedEventArgs e)
    {
        if (!ReferenceEquals(e.Source, Control))
        {
            return;
        }

        OwnerDocument.NotifyActualBlur(this);
    }

    public bool dispatchEvent(object eventValue)
    {
        var synthetic = OwnerDocument.CreateSyntheticEvent(eventValue);
        if (synthetic is null)
        {
            return true;
        }

        try
        {
            OwnerDocument.DispatchSyntheticEvent(this, synthetic);
            synthetic.SyncDefaultPrevented();
        }
        finally
        {
            OwnerDocument.CompleteExternalSyntheticEventDispatch(eventValue);
        }
        return !synthetic.defaultPrevented;
    }

    internal void DispatchResourceEvent(string eventName)
    {
        var evt = new DomSyntheticEvent(
            eventName,
            bubbles: false,
            cancelable: false,
            Host.GetTimestamp(),
            detail: null,
            accessor: null);
        OwnerDocument.DispatchSyntheticEvent(this, evt);
        var handler = string.Equals(eventName, "load", StringComparison.OrdinalIgnoreCase) ? onload : onerror;
        if (handler is null)
        {
            return;
        }

        try
        {
            var callback = handler as IExternalJavaScriptCallback
                           ?? Host.ExternalCallbackAdapter?.GetCallback(handler, create: true);
            if (callback is null) return;
            using var scope = Host.EnterExternalJavaScriptCall();
            evt.SetCurrentTarget(this, DomEventPhase.AtTarget, passive: false);
            callback.Invoke(this, evt);
        }
        catch (Exception exception)
        {
            if (Host.EnableDiagnosticLogging)
            {
                Console.Error.WriteLine($"Resource {eventName} handler failed: {exception.Message}");
            }
        }
        finally
        {
            evt.ResetCurrentTarget();
        }
    }

    public virtual AvaloniaDomElement? appendChild(AvaloniaDomElement child)
        => InsertChild(child, reference: null, placeBefore: false);

    public virtual object? appendChild(object child)
    {
        if (child is DomDocumentFragment fragment)
        {
            foreach (var fragmentChild in fragment.childNodes.OfType<AvaloniaDomElement>().ToArray())
            {
                appendChild(fragmentChild);
            }
            return fragment;
        }
        return child is AvaloniaDomElement element ? appendChild(element) : null;
    }

    public virtual void append(params object?[] nodes)
    {
        foreach (var node in nodes)
        {
            if (node is DomDocumentFragment fragment)
            {
                foreach (var fragmentChild in fragment.childNodes.OfType<AvaloniaDomElement>().ToArray())
                {
                    appendChild(fragmentChild);
                }
                continue;
            }

            var child = ConvertAppendNode(node);
            if (child is not null)
            {
                appendChild(child);
            }
        }
    }

    public virtual void prepend(params object?[] nodes)
    {
        var reference = firstChild;
        foreach (var node in nodes)
        {
            var child = ConvertAppendNode(node);
            if (child is not null)
            {
                insertBefore(child, reference);
            }
        }
    }

    public virtual void before(params object?[] nodes)
    {
        var parent = parentElement;
        if (parent is null)
        {
            return;
        }

        foreach (var node in nodes)
        {
            var child = ConvertAppendNode(node);
            if (child is not null)
            {
                parent.insertBefore(child, this);
            }
        }
    }

    public virtual void after(params object?[] nodes)
    {
        var parent = parentElement;
        if (parent is null)
        {
            return;
        }

        var reference = nextSibling as AvaloniaDomElement;
        foreach (var node in nodes)
        {
            var child = ConvertAppendNode(node);
            if (child is not null)
            {
                parent.insertBefore(child, reference);
            }
        }
    }

    public virtual void replaceChildren(params object?[] nodes)
    {
        var replacements = nodes
            .Select(ConvertAppendNode)
            .Where(node => node is not null)
            .Cast<AvaloniaDomElement>()
            .ToArray();
        foreach (var replacement in replacements)
        {
            ThrowIfHierarchyCycle(replacement);
        }

        foreach (var child in GetChildElements().ToArray())
        {
            removeChild(child);
        }
        foreach (var replacement in replacements)
        {
            appendChild(replacement);
        }
    }

    public virtual void remove()
    {
        if (_domParentOverride is DomHeadElement head)
        {
            head.removeChild(this);
            return;
        }

        if (Control.Parent is not Control parent)
        {
            return;
        }

        var parentElement = OwnerDocument.WrapControl(parent);
        parentElement.removeChild(this);
    }

    public virtual AvaloniaDomElement? insertBefore(AvaloniaDomElement newChild, AvaloniaDomElement? referenceChild)
    {
        if (referenceChild is null)
        {
            return appendChild(newChild);
        }

        return InsertChild(newChild, referenceChild, placeBefore: true);
    }

    private AvaloniaDomElement? ConvertAppendNode(object? node)
    {
        if (node is AvaloniaDomElement element)
        {
            return element;
        }

        return OwnerDocument.createTextNode(Convert.ToString(node, CultureInfo.InvariantCulture) ?? string.Empty) as AvaloniaDomElement;
    }

    public AvaloniaDomElement? insertAdjacentElement(string position, AvaloniaDomElement element)
    {
        if (element == null) return null;
        var parent = parentElement;

        switch (position?.ToLowerInvariant())
        {
            case "beforebegin":
                if (parent != null)
                {
                    parent.insertBefore(element, this);
                }
                break;
            case "afterbegin":
                insertBefore(element, firstChild);
                break;
            case "beforeend":
                appendChild(element);
                break;
            case "afterend":
                if (parent != null)
                {
                    var next = nextSibling as AvaloniaDomElement;
                    if (next != null)
                    {
                        parent.insertBefore(element, next);
                    }
                    else
                    {
                        parent.appendChild(element);
                    }
                }
                break;
        }
        return element;
    }

    private void DetachFromCurrentParentWithNotification(AvaloniaDomElement child)
    {
        var oldParentControl = child.Control.Parent as Control;
        if (oldParentControl is null)
        {
            return;
        }

        var oldParentElement = OwnerDocument.WrapControl(oldParentControl);
        var (previousSibling, nextSibling) = child.GetSiblingSnapshot(oldParentControl, child.Control);
        if (DetachFromParent(child.Control))
        {
            OwnerDocument.NotifyChildListMutation(oldParentElement, null, new[] { child }, previousSibling, nextSibling);
            child.ReleaseBrowsingContextsForRemoval();
        }
    }

    private (AvaloniaDomElement? previous, AvaloniaDomElement? next) GetSiblingSnapshot(Control parentControl, Control childControl)
    {
        if (TryGetControlsCollection(parentControl, out var controlsCollection))
        {
            Control? previousControl = null;
            var found = false;
            foreach (var control in controlsCollection.OfType<Control>())
            {
                if (AvaloniaDomDocument.IsDomInfrastructureControl(control))
                {
                    continue;
                }
                if (found)
                {
                    return (
                        previousControl is null ? null : OwnerDocument.WrapControl(previousControl),
                        OwnerDocument.WrapControl(control));
                }
                if (ReferenceEquals(control, childControl))
                {
                    found = true;
                    continue;
                }
                previousControl = control;
            }
            if (found)
            {
                return (
                    previousControl is null ? null : OwnerDocument.WrapControl(previousControl),
                    null);
            }
        }

        return (null, null);
    }

    public virtual AvaloniaDomElement? removeChild(AvaloniaDomElement child)
    {
        if (child is null || !IsDirectChild(child))
        {
            throw new InvalidOperationException(
                "removeChild requires a child of this node.");
        }

        var (previousSibling, nextSibling) = child.GetSiblingSnapshot(Control, child.Control);

        if (TryGetControlsCollection(Control, out var list))
        {
            if (list.Remove(child.Control))
            {
                OwnerDocument.NotifyChildListMutation(this, null, new[] { child }, previousSibling, nextSibling);
                child.ReleaseBrowsingContextsForRemoval();
                return child;
            }

            throw new InvalidOperationException(
                "removeChild requires a child of this node.");
        }

        if (Control is ContentControl cc)
        {
            if (ReferenceEquals(cc.Content, child.Control))
            {
                cc.Content = null;
                OwnerDocument.NotifyChildListMutation(this, null, new[] { child }, previousSibling, nextSibling);
                child.ReleaseBrowsingContextsForRemoval();
                return child;
            }

            throw new InvalidOperationException(
                "removeChild requires a child of this node.");
        }

        if (Control is Decorator decorator)
        {
            if (ReferenceEquals(decorator.Child, child.Control))
            {
                decorator.Child = null;
                OwnerDocument.NotifyChildListMutation(this, null, new[] { child }, previousSibling, nextSibling);
                child.ReleaseBrowsingContextsForRemoval();
                return child;
            }

            throw new InvalidOperationException(
                "removeChild requires a child of this node.");
        }

        throw new InvalidOperationException(
            "removeChild requires a child of this node.");
    }

    public virtual AvaloniaDomElement? replaceChild(AvaloniaDomElement newChild, AvaloniaDomElement oldChild)
    {
        if (newChild is null || oldChild is null || !IsDirectChild(oldChild))
        {
            throw new InvalidOperationException(
                "replaceChild requires a child of this node.");
        }

        if (ReferenceEquals(newChild.Control, oldChild.Control))
        {
            return oldChild;
        }

        ThrowIfHierarchyCycle(newChild);

        if (TryGetControlsCollection(Control, out var list))
        {
            DetachFromCurrentParentWithNotification(newChild);
            var index = list.IndexOf(oldChild.Control);
            var (previousSibling, nextSibling) = oldChild.GetSiblingSnapshot(Control, oldChild.Control);
            list.RemoveAt(index);
            list.Insert(index, newChild.Control);
            OwnerDocument.NotifyChildListMutation(this, new[] { newChild }, new[] { oldChild }, previousSibling, nextSibling);
            oldChild.ReleaseBrowsingContextsForRemoval();
            TriggerIframeNavigationIfNeeded(newChild);
            return oldChild;
        }

        if (Control is ContentControl cc)
        {
            if (!ReferenceEquals(cc.Content, oldChild.Control))
            {
                throw new InvalidOperationException(
                    "replaceChild requires a child of this node.");
            }

            DetachFromCurrentParentWithNotification(newChild);
            cc.Content = newChild.Control;
            OwnerDocument.NotifyChildListMutation(this, new[] { newChild }, new[] { oldChild }, null, null);
            oldChild.ReleaseBrowsingContextsForRemoval();
            TriggerIframeNavigationIfNeeded(newChild);
            return oldChild;
        }

        if (Control is Decorator decorator)
        {
            if (!ReferenceEquals(decorator.Child, oldChild.Control))
            {
                throw new InvalidOperationException(
                    "replaceChild requires a child of this node.");
            }

            DetachFromCurrentParentWithNotification(newChild);
            decorator.Child = newChild.Control;
            OwnerDocument.NotifyChildListMutation(this, new[] { newChild }, new[] { oldChild }, null, null);
            oldChild.ReleaseBrowsingContextsForRemoval();
            TriggerIframeNavigationIfNeeded(newChild);
            return oldChild;
        }

        throw new InvalidOperationException(
            "replaceChild requires a child of this node.");
    }

    public virtual string? getAttribute(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return null;
        }

        if (_xmlMode)
        {
            return _xmlAttributes.TryGetValue(name, out var xmlValue) ? xmlValue : null;
        }

        name = name.ToLowerInvariant();
        if (_dataAttributes.TryGetValue(name, out var dataValue))
        {
            return dataValue;
        }

        if (_attributes.TryGetValue(name, out var attributeValue))
        {
            return attributeValue;
        }

        if (TryGetAttribute(name, out var value))
        {
            return value;
        }

        return null;
    }

    public virtual bool hasAttribute(string name)
        => !string.IsNullOrWhiteSpace(name) && getAttribute(name) is not null;

    public virtual void removeAttribute(string name)
    {
        if (string.IsNullOrWhiteSpace(name) || !hasAttribute(name))
        {
            return;
        }

        setAttribute(name, null);
    }

    public virtual void setAttribute(string name, string? value)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        if (_xmlMode)
        {
            var oldXmlValue = getAttribute(name);
            if (value is null) _xmlAttributes.Remove(name);
            else _xmlAttributes[name] = value;
            RaiseAttributeMutation(name, oldXmlValue, value);
            return;
        }

        var normalized = name.ToLowerInvariant();
        string? oldValue = null;
        try
        {
            oldValue = getAttribute(normalized);
        }
        catch (AmbiguousMatchException)
        {
        }
        catch
        {
        }
        if (value is null)
        {
            _attributeNames.Remove(normalized);
        }
        else
        {
            _attributeNames.Add(normalized);
        }

        // Authored HTML attributes remain observable independently of any
        // native-control property used to project their presentation or input
        // behavior. Class and style have dedicated token/declaration stores.
        if (normalized is not "class" and not "style")
        {
            SetGenericAttribute(normalized, value);
        }

        // SVG scene compilation and serialization read authored attributes,
        // including names such as width/height that also map to Avalonia
        // properties. Preserve the authored value even when a native property
        // projection handles the same name.
        if (IsSvgNamespace)
        {
            SetGenericAttribute(normalized, value);
        }

        if (IsSvgNamespace
            && this is AvaloniaDomImageElement
            && (string.Equals(normalized, "href", StringComparison.OrdinalIgnoreCase)
                || string.Equals(normalized, "xlink:href", StringComparison.OrdinalIgnoreCase)))
        {
            TrySetAttribute(normalized, value);
            RaiseAttributeMutation(normalized, oldValue, value);
            return;
        }

        if (ApplySvgAttribute(normalized, value))
        {
            SetGenericAttribute(normalized, value);
            RaiseAttributeMutation(normalized, oldValue, value);
            return;
        }

        switch (normalized)
        {
            case "href":
            case "rel":
            case "media":
                SetGenericAttribute(normalized, value);
                if (normalized == "href" && (localName is "a" or "area"))
                {
                    ApplyTabIndexState();
                }
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "type":
                SetGenericAttribute(normalized, value);
                if (Control is DomTextInputControl typedInput)
                {
                    typedInput.UsesTextIntrinsicSize = value?.Trim().ToLowerInvariant() is not
                        ("checkbox" or "radio" or "button" or "submit" or "reset" or "color" or "range" or "file" or "hidden" or "image");
                }
                ApplyTabIndexState();
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "checked":
                SetGenericAttribute(normalized, value);
                @checked = value is not null;
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "selected":
                SetGenericAttribute(normalized, value);
                selected = value is not null;
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "disabled":
                SetGenericAttribute(normalized, value);
                if (SupportsDisabledState)
                {
                    Control.IsEnabled = value is null;
                }
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "src":
                if (string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase))
                {
                    SetGenericAttribute(normalized, value);
                    RaiseAttributeMutation(normalized, oldValue, value);
                    if (parentElement is not null)
                    {
                        OwnerDocument.TriggerIframeVirtualization(this, value ?? "");
                    }
                    return;
                }
                break;
            case "id":
                SetGenericAttribute(normalized, value);
                ApplyCanvasPositioning();
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "name":
                SetGenericAttribute(normalized, value);
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "value":
                SetGenericAttribute(normalized, value);
                if (Control is TextBox valueInput)
                {
                    valueInput.Text = value ?? string.Empty;
                }
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "tabindex":
                SetGenericAttribute(normalized, value);
                ApplyTabIndexState();
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "class":
                classList.SetFromAttribute(value);
                RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
                ApplyCanvasPositioning();
                return;
            case "title":
                SetGenericAttribute(normalized, value);
                // An iframe title names its browsing context for accessibility;
                // browsers do not turn that title into a native hover popup over
                // all iframe content. Some components label their primary iframe
                // "Financial Chart", which otherwise obscures the crosshair.
                ToolTip.SetTip(
                    Control,
                    string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase)
                        ? null
                        : value);
                RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
                return;
            case "placeholder":
                placeholder = value;
                RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
                return;
            case "size" when Control is DomTextInputControl input:
                SetGenericAttribute(normalized, value);
                input.HtmlSize = int.TryParse(
                    value,
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out var htmlSize) && htmlSize > 0
                    ? htmlSize
                    : 20;
                RaiseAttributeMutation(normalized, oldValue, value);
                return;
            case "style":
                ApplyStyleAttribute(value);
                return;
            case "width":
                if (IsExplicitHtmlCanvasElement() && TryParseAttributeDouble(value, out var canvasWidth))
                {
                    width = canvasWidth;
                    RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
                    return;
                }
                break;
            case "height":
                if (IsExplicitHtmlCanvasElement() && TryParseAttributeDouble(value, out var canvasHeight))
                {
                    height = canvasHeight;
                    RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
                    return;
                }
                break;
        }

        if (normalized.StartsWith("data-", StringComparison.Ordinal))
        {
            SetDataAttribute(normalized, value);
            return;
        }

        if (TrySetAttribute(normalized, value))
        {
            RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
            return;
        }

        if (SetControlProperty(normalized, value))
        {
            RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
            return;
        }

        SetGenericAttribute(normalized, value);
        RaiseAttributeMutation(normalized, oldValue, SafeGetAttribute(normalized));
    }

    public virtual void setAttributeNS(string? namespaceUri, string qualifiedName, string? value)
        => setAttribute(qualifiedName, value);

    public virtual string? getAttributeNS(string? namespaceUri, string qualifiedName)
        => getAttribute(qualifiedName);

    public virtual bool hasAttributeNS(string? namespaceUri, string qualifiedName)
        => hasAttribute(qualifiedName);

    public virtual void removeAttributeNS(string? namespaceUri, string qualifiedName)
        => removeAttribute(qualifiedName);

    public virtual void classListAdd(string cls)
    {
        classList.add(cls);
    }

    public virtual void classListRemove(string cls)
    {
        classList.remove(cls);
    }

    public virtual void classListToggle(string cls)
    {
        classList.toggle(cls);
    }

    public virtual string? textContent
    {
        get
        {
            if (Control is TextBlock tb)
            {
                return tb.Text;
            }

            if (TryGetControlsCollection(Control, out var controls))
            {
                var text = new StringBuilder();
                foreach (var child in controls.OfType<Control>())
                {
                    if (AvaloniaDomDocument.IsDomInfrastructureControl(child)) continue;
                    var childText = OwnerDocument.WrapControl(child).textContent;
                    if (childText is not null)
                    {
                        text.Append(childText);
                    }
                }
                return text.ToString();
            }

            if (Control is ContentControl { Content: string contentString })
            {
                return contentString;
            }

            if (Control is ContentControl { Content: Control contentChild })
            {
                return OwnerDocument.WrapControl(contentChild).textContent;
            }

            if (Control is Decorator { Child: Control decoratorChild })
            {
                return OwnerDocument.WrapControl(decoratorChild).textContent;
            }

            return null;
        }
        set
        {
            if (Control is TextBlock tb)
            {
                tb.Text = value;
            }
            else if (TryGetControlsCollection(Control, out var list))
            {
                // Text content is a DOM text node, not an element-level input
                // target. Reuse the document text-node path so native hit
                // testing resolves through the text visual to its HTML parent.
                var removedChildren = GetChildElements().ToArray();
                list.Clear();
                foreach (var removedChild in removedChildren)
                {
                    removedChild.ReleaseBrowsingContextsForRemoval();
                }
                if (!string.IsNullOrEmpty(value))
                {
                    var textNode = (AvaloniaDomTextNode)OwnerDocument.createTextNode(value)!;
                    list.Add(textNode.Control);
                }
            }
            else if (Control is ContentControl contentControl)
            {
                var removedChild = contentControl.Content is Control existing
                    ? OwnerDocument.WrapControl(existing)
                    : null;
                contentControl.Content = string.IsNullOrEmpty(value) ? null : value;
                removedChild?.ReleaseBrowsingContextsForRemoval();
            }
            else if (Control is Decorator decorator)
            {
                var removedChild = decorator.Child is Control existing
                    ? OwnerDocument.WrapControl(existing)
                    : null;
                if (string.IsNullOrEmpty(value))
                {
                    decorator.Child = null;
                }
                else
                {
                    var textNode = (AvaloniaDomTextNode)OwnerDocument.createTextNode(value)!;
                    decorator.Child = textNode.Control;
                }
                removedChild?.ReleaseBrowsingContextsForRemoval();
            }

            OwnerDocument.NotifyTextChanged(this);
        }
    }

    // HTMLElement.innerText is layout-aware in browsers, but for the retained
    // Avalonia DOM the closest stable contract is the rendered descendant text
    // exposed by textContent. Keeping the setter on the same node-replacement
    // path also preserves MutationObserver and layout invalidation behavior.
    public virtual string innerText
    {
        get => textContent ?? string.Empty;
        set => textContent = value ?? string.Empty;
    }

    public virtual string innerHTML
    {
        get
        {
            var sb = new StringBuilder();
            foreach (var child in GetChildElements())
            {
                SerializeControl(child, sb);
            }
            return sb.ToString();
        }
        set
        {
            ClearChildren();

            if (string.IsNullOrEmpty(value))
            {
                return;
            }

            var parser = new AngleSharp.Html.Parser.HtmlParser();
            var contextDocument = parser.ParseDocument(string.Empty);
            var context = contextDocument.CreateElement(localName);
            foreach (var node in parser.ParseFragment(value, context))
            {
                var child = CreateDomNodeFromAngleSharp(node);
                if (child != null)
                {
                    appendChild(child);
                }
            }
        }
    }

    public virtual string outerHTML
    {
        get
        {
            if (IsSvgNamespace)
            {
                return SerializeSvgDocument();
            }

            var builder = new StringBuilder();
            SerializeControl(this, builder);
            return builder.ToString();
        }
    }

    private void ClearChildren()
    {
        var removedChildren = GetChildElements().ToArray();
        if (TryGetControlsCollection(Control, out var list))
        {
            list.Clear();
            if (removedChildren.Length > 0)
            {
                OwnerDocument.NotifyChildListMutation(this, null, removedChildren, null, null);
            }
            foreach (var removedChild in removedChildren)
            {
                removedChild.ReleaseBrowsingContextsForRemoval();
            }
            return;
        }

        if (Control is ContentControl cc)
        {
            cc.Content = null;
            if (removedChildren.Length > 0)
            {
                OwnerDocument.NotifyChildListMutation(this, null, removedChildren, null, null);
            }
            foreach (var removedChild in removedChildren)
            {
                removedChild.ReleaseBrowsingContextsForRemoval();
            }
            return;
        }

        if (Control is Decorator decorator)
        {
            decorator.Child = null;
            if (removedChildren.Length > 0)
            {
                OwnerDocument.NotifyChildListMutation(this, null, removedChildren, null, null);
            }
            foreach (var removedChild in removedChildren)
            {
                removedChild.ReleaseBrowsingContextsForRemoval();
            }
            return;
        }
    }

    internal AvaloniaDomElement? CreateDomNodeFromAngleSharp(AngleSharp.Dom.INode node)
    {
        if (node.NodeType == AngleSharp.Dom.NodeType.Text)
        {
            var text = node.TextContent;
            return OwnerDocument.createTextNode(text) as AvaloniaDomElement;
        }
        else if (node.NodeType == AngleSharp.Dom.NodeType.Element && node is AngleSharp.Dom.IElement el)
        {
            var tag = el.TagName;
            var created = OwnerDocument.createElementNS(el.NamespaceUri, tag) as AvaloniaDomElement;
            if (created != null)
            {
                foreach (var attr in el.Attributes)
                {
                    created.setAttribute(attr.Name, attr.Value);
                }

                if (created.Control is TextBlock)
                {
                    created.textContent = el.TextContent;
                    return created;
                }

                foreach (var childNode in el.ChildNodes)
                {
                    var childElement = CreateDomNodeFromAngleSharp(childNode);
                    if (childElement != null)
                    {
                        created.appendChild(childElement);
                    }
                }
                return created;
            }
        }
        return null;
    }

    private void SerializeControl(AvaloniaDomElement el, StringBuilder sb)
    {
        if (el is AvaloniaDomTextNode textNode)
        {
            sb.Append(System.Net.WebUtility.HtmlEncode(textNode.nodeValue ?? string.Empty));
            return;
        }

        var tag = el.nodeName.ToLowerInvariant();
        sb.Append('<').Append(tag);

        foreach (var attributeName in el._attributeNames)
        {
            var attributeValue = el.getAttribute(attributeName);
            if (attributeValue is null) continue;
            sb.Append(' ').Append(attributeName).Append("=\"")
                .Append(System.Net.WebUtility.HtmlEncode(attributeValue)).Append('"');
        }

        sb.Append('>');

        if (tag is "area" or "base" or "br" or "col" or "embed" or "hr" or "img"
            or "input" or "link" or "meta" or "param" or "source" or "track" or "wbr")
        {
            return;
        }

        foreach (var child in el.GetChildElements())
        {
            SerializeControl(child, sb);
        }

        sb.Append("</").Append(tag).Append('>');
    }

    private string SerializeSvgDocument()
    {
        var sb = new StringBuilder(256);
        SerializeSvgElement(this, sb, isRoot: true);
        return sb.ToString();
    }

    private SvgScene BuildSvgScene()
    {
        var viewBox = Control is SvgLayoutPanel { ViewBox: { } declared }
            ? declared
            : new Rect(
                0,
                0,
                ReadSvgNumber(this, "width", Math.Max(0, Control.Bounds.Width)),
                ReadSvgNumber(this, "height", Math.Max(0, Control.Bounds.Height)));
        if (viewBox.Width <= 0 || viewBox.Height <= 0)
        {
            viewBox = new Rect(0, 0, Math.Max(1, Control.Bounds.Width), Math.Max(1, Control.Bounds.Height));
        }

        long nextId = 1;
        var root = new SvgSceneNode(nextId++, SvgSceneNodeKind.Group);
        var currentColor = GetSvgValue(this, "color")
                           ?? (_computedStyleValues.TryGetValue("color", out var computedColor)
                               ? computedColor
                               : "black");
        var rootFill = GetSvgValue(this, "fill") ?? "black";
        var rootStroke = GetSvgValue(this, "stroke");
        var rootOpacity = Math.Clamp(ReadSvgNumber(this, "opacity", 1), 0, 1);
        var inherited = new SvgInheritedStyle(rootFill, rootStroke, currentColor, rootOpacity);
        foreach (var child in GetChildElements())
        {
            var node = BuildSvgSceneNode(child, inherited, ref nextId);
            if (node is not null)
            {
                root.Add(node);
            }
        }

        return new SvgScene(
            new HtmlMlRect(viewBox.X, viewBox.Y, viewBox.Width, viewBox.Height),
            root,
            Interlocked.Increment(ref _svgSceneRevision),
            Control is SvgLayoutPanel { StretchViewBox: true });
    }

    private static SvgSceneNode? BuildSvgSceneNode(
        AvaloniaDomElement element,
        SvgInheritedStyle inherited,
        ref long nextId)
    {
        var tag = element.localName.ToLowerInvariant();
        if (tag.StartsWith('#'))
        {
            return null;
        }

        var opacity = inherited.Opacity * ReadSvgNumber(element, "opacity", 1);
        var currentColor = GetSvgValue(element, "color") ?? inherited.CurrentColor;
        var fillValue = GetSvgValue(element, "fill") ?? inherited.Fill;
        var strokeValue = GetSvgValue(element, "stroke") ?? inherited.Stroke;
        var style = new SvgInheritedStyle(fillValue, strokeValue, currentColor, opacity);
        var kind = tag switch
        {
            "path" => SvgSceneNodeKind.Path,
            "circle" => SvgSceneNodeKind.Circle,
            "rect" => SvgSceneNodeKind.Rectangle,
            "text" => SvgSceneNodeKind.Text,
            "image" => SvgSceneNodeKind.Image,
            _ => SvgSceneNodeKind.Group
        };

        var bounds = kind switch
        {
            SvgSceneNodeKind.Circle => CircleBounds(element),
            SvgSceneNodeKind.Rectangle or SvgSceneNodeKind.Image => RectangleBounds(element),
            SvgSceneNodeKind.Text => TextBounds(element),
            _ => HtmlMlRect.Empty
        };
        var node = new SvgSceneNode(nextId++, kind)
        {
            Transform = ParseSvgTransform(GetSvgValue(element, "transform")),
            Bounds = bounds,
            PathData = kind == SvgSceneNodeKind.Path ? GetSvgValue(element, "d") : null,
            Text = kind == SvgSceneNodeKind.Text ? element.textContent : null,
            ResourceUri = kind == SvgSceneNodeKind.Image
                ? GetSvgValue(element, "href") ?? GetSvgValue(element, "xlink:href")
                : null,
            Resource = kind == SvgSceneNodeKind.Image
                       && element.Control is Image { Source: IImage image }
                ? HtmlMlBackendHandle.Create(image)
                : default,
            Fill = ParseSvgPaint(fillValue, currentColor, opacity * ReadSvgNumber(element, "fill-opacity", 1)),
            Stroke = ParseSvgPaint(strokeValue, currentColor, opacity * ReadSvgNumber(element, "stroke-opacity", 1)),
            StrokeWidth = Math.Max(0, ReadSvgNumber(element, "stroke-width", 1))
        };

        foreach (var child in element.GetChildElements())
        {
            var childNode = BuildSvgSceneNode(child, style, ref nextId);
            if (childNode is not null)
            {
                node.Add(childNode);
            }
        }

        return node;
    }

    private static SvgPaint? ParseSvgPaint(string? value, string? currentColor, double opacity)
    {
        var normalized = value?.Trim();
        if (string.IsNullOrEmpty(normalized)
            || string.Equals(normalized, "none", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        if (string.Equals(normalized, "currentColor", StringComparison.OrdinalIgnoreCase))
        {
            normalized = currentColor;
        }

        if (string.IsNullOrWhiteSpace(normalized))
        {
            return null;
        }

        Color color;
        if (CssValueParser.TryParseColor(normalized, out var functional))
        {
            color = functional;
        }
        else if (!Color.TryParse(normalized, out color))
        {
            return null;
        }

        return new SvgPaint(
            new HtmlMlColor(color.A, color.R, color.G, color.B),
            Math.Clamp(opacity, 0, 1));
    }

    private static GraphicsTransform ParseSvgTransform(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return GraphicsTransform.Identity;
        }

        var transform = Matrix.Identity;
        foreach (Match match in Regex.Matches(value, @"(?<name>[a-zA-Z]+)\s*\((?<args>[^)]*)\)"))
        {
            var values = ParseSvgNumberList(match.Groups["args"].Value);
            Matrix next;
            switch (match.Groups["name"].Value.ToLowerInvariant())
            {
                case "matrix" when values.Length == 6:
                    next = new Matrix(values[0], values[1], values[2], values[3], values[4], values[5]);
                    break;
                case "translate" when values.Length is 1 or 2:
                    next = Matrix.CreateTranslation(values[0], values.Length == 2 ? values[1] : 0);
                    break;
                case "scale" when values.Length is 1 or 2:
                    next = Matrix.CreateScale(values[0], values.Length == 2 ? values[1] : values[0]);
                    break;
                case "rotate" when values.Length == 1:
                    next = Matrix.CreateRotation(values[0] * Math.PI / 180d);
                    break;
                case "rotate" when values.Length == 3:
                    next = Matrix.CreateTranslation(-values[1], -values[2])
                           * Matrix.CreateRotation(values[0] * Math.PI / 180d)
                           * Matrix.CreateTranslation(values[1], values[2]);
                    break;
                default:
                    continue;
            }

            transform = next * transform;
        }

        return new GraphicsTransform(
            transform.M11,
            transform.M12,
            transform.M21,
            transform.M22,
            transform.M31,
            transform.M32);
    }

    private static double[] ParseSvgNumberList(string? value)
        => (value ?? string.Empty)
            .Split(new[] { ' ', ',', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(token => double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
                ? number
                : double.NaN)
            .Where(double.IsFinite)
            .ToArray();

    private static HtmlMlRect CircleBounds(AvaloniaDomElement element)
    {
        var cx = ReadSvgNumber(element, "cx", 0);
        var cy = ReadSvgNumber(element, "cy", 0);
        var radius = Math.Max(0, ReadSvgNumber(element, "r", 0));
        return new HtmlMlRect(cx - radius, cy - radius, radius * 2, radius * 2);
    }

    private static HtmlMlRect RectangleBounds(AvaloniaDomElement element)
        => new(
            ReadSvgNumber(element, "x", 0),
            ReadSvgNumber(element, "y", 0),
            Math.Max(0, ReadSvgNumber(element, "width", 0)),
            Math.Max(0, ReadSvgNumber(element, "height", 0)));

    private static HtmlMlRect TextBounds(AvaloniaDomElement element)
    {
        var size = Math.Max(1, ReadSvgNumber(element, "font-size", 16));
        return new HtmlMlRect(
            ReadSvgNumber(element, "x", 0),
            ReadSvgNumber(element, "y", 0) - size,
            0,
            size);
    }

    private static double ReadSvgNumber(AvaloniaDomElement element, string name, double fallback)
        => double.TryParse(
               GetSvgValue(element, name)?.Trim().TrimEnd('p', 'x'),
               NumberStyles.Float,
               CultureInfo.InvariantCulture,
               out var number)
           && double.IsFinite(number)
            ? number
            : fallback;

    private static string? GetSvgValue(AvaloniaDomElement element, string name)
    {
        // Once the CSS cascade has run, its SVG presentation value already
        // includes presentation attributes at their proper specificity,
        // matching author rules, inline style, and inherited SVG paint. Keep
        // authored-value fallbacks for detached SVG fragments that have not
        // participated in a style pass yet.
        if (IsSvgPaintPresentationProperty(name)
            && element._computedStyleValues.TryGetValue(name, out var computedValue)
            && !string.IsNullOrWhiteSpace(computedValue))
        {
            return computedValue;
        }

        return element._styleValues.TryGetValue(name, out var styleValue) && !string.IsNullOrWhiteSpace(styleValue)
            ? styleValue
            : element._attributes.TryGetValue(name, out var attributeValue)
                ? attributeValue
                : null;
    }

    private static bool IsSvgPaintPresentationProperty(string name)
        => name is "clip-rule" or "color" or "fill" or "fill-opacity" or "fill-rule" or "opacity"
            or "stroke" or "stroke-linecap" or "stroke-linejoin" or "stroke-opacity" or "stroke-width";

    private readonly record struct SvgInheritedStyle(
        string? Fill,
        string? Stroke,
        string? CurrentColor,
        double Opacity);

    private static void SerializeSvgElement(AvaloniaDomElement element, StringBuilder sb, bool isRoot)
    {
        if (element is AvaloniaDomTextNode text)
        {
            sb.Append(System.Net.WebUtility.HtmlEncode(text.nodeValue ?? string.Empty));
            return;
        }

        var tag = element.localName;
        sb.Append('<').Append(tag);
        var hasNamespace = false;
        foreach (var pair in element._attributes)
        {
            if (pair.Value is null || string.Equals(pair.Key, "style", StringComparison.OrdinalIgnoreCase)) continue;
            hasNamespace |= string.Equals(pair.Key, "xmlns", StringComparison.OrdinalIgnoreCase);
            sb.Append(' ').Append(pair.Key).Append("=\"")
                .Append(System.Net.WebUtility.HtmlEncode(pair.Value)).Append('"');
        }

        if (isRoot && !hasNamespace)
        {
            sb.Append(" xmlns=\"http://www.w3.org/2000/svg\"");
        }

        var styles = element._styleValues
            .Where(pair => !string.IsNullOrWhiteSpace(pair.Value))
            .Select(pair => $"{pair.Key}:{pair.Value}")
            .ToList();
        if (isRoot
            && element._computedStyleValues.TryGetValue("color", out var color)
            && !string.IsNullOrWhiteSpace(color)
            && !element._styleValues.ContainsKey("color"))
        {
            styles.Add($"color:{color}");
        }
        if (styles.Count > 0)
        {
            sb.Append(" style=\"")
                .Append(System.Net.WebUtility.HtmlEncode(string.Join(';', styles)))
                .Append('"');
        }

        sb.Append('>');
        foreach (var child in element.GetChildElements())
        {
            SerializeSvgElement(child, sb, isRoot: false);
        }
        sb.Append("</").Append(tag).Append('>');
    }

    private void InvalidateSvgRenderer()
    {
        for (var current = this; current is not null; current = current.parentElement)
        {
            if (current.Control is SvgLayoutPanel { SceneProvider: not null } root)
            {
                root.InvalidateSceneVisual();
                return;
            }

            if (current.Control is SvgLayoutPanel { SkiaMarkupProvider: not null } skiaRoot)
            {
                skiaRoot.InvalidateSkiaVisual();
                return;
            }
        }
    }

    internal bool TryGetDataAttribute(string attributeName, out string? value)
        => _dataAttributes.TryGetValue(attributeName.ToLowerInvariant(), out value);

    internal void SetDataAttribute(string attributeName, string? value)
    {
        attributeName = attributeName.ToLowerInvariant();
        _dataAttributes.TryGetValue(attributeName, out var oldValue);

        if (value is null)
        {
            _dataAttributes.Remove(attributeName);
        }
        else
        {
            _dataAttributes[attributeName] = value;
        }

        RaiseAttributeMutation(attributeName, oldValue, value);
    }

    internal bool RemoveDataAttribute(string attributeName)
    {
        attributeName = attributeName.ToLowerInvariant();
        if (!_dataAttributes.TryGetValue(attributeName, out var oldValue))
        {
            return false;
        }

        var removed = _dataAttributes.Remove(attributeName);
        if (removed)
        {
            RaiseAttributeMutation(attributeName, oldValue, null);
        }

        return removed;
    }

    internal void RaiseAttributeMutation(string attributeName, string? oldValue, string? newValue)
    {
        OwnerDocument.NotifyAttributeChanged(this, attributeName, oldValue, newValue);
    }

    internal void RaiseClassListMutation(string? oldValue, string newValue)
    {
        // DOMTokenList mutations operate on the reflected class attribute. A
        // mutation on an element that did not previously have `class` creates
        // the attribute; removing its last token leaves an empty attribute.
        // Without recording presence here, selectors observed the native
        // Classes collection while className/getAttribute and CSS invalidation
        // still saw a missing attribute.
        SetAttributePresence("class", present: true);
        RaiseAttributeMutation("class", oldValue, newValue);
        // Class changes must invalidate selector state before the immediate
        // presentation refresh performs its synchronous style read.
        ApplyCanvasPositioning();
    }

    internal bool HasClassAttribute => HasAttributePresence("class");

    private string? SafeGetAttribute(string attributeName)
    {
        try
        {
            return getAttribute(attributeName);
        }
        catch (AmbiguousMatchException)
        {
            return null;
        }
        catch
        {
            return null;
        }
    }

    internal IEnumerable<KeyValuePair<string, string?>> EnumerateDataset()
    {
        foreach (var pair in _dataAttributes)
        {
            yield return new KeyValuePair<string, string?>(DomStringMap.ToDatasetKey(pair.Key), pair.Value);
        }
    }

    internal void SetStyleProperty(string propertyName, string? value)
    {
        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        if (string.IsNullOrEmpty(normalized))
        {
            return;
        }
        if (!_suppressStyleMutation)
        {
            _importantStyleProperties.Remove(normalized);
        }
        if (value is not null)
        {
            if (!normalized.StartsWith("--", StringComparison.Ordinal)
                && !CssPropertyCatalog.IsSupported(normalized))
            {
                return;
            }
            if (!CssPropertyCatalog.IsValidCssomValue(normalized, value))
            {
                return;
            }
            if (value.Length == 0)
            {
                value = null;
            }
        }
        if (value is not null)
        {
            _attributeNames.Add("style");
        }

        var hadOldPropertyValue = _styleValues.TryGetValue(normalized, out var oldPropertyValue);
        if (!s_disableRedundantInlineStyleWriteSuppression
            && hadOldPropertyValue
            && value is not null
            && string.Equals(oldPropertyValue, value, StringComparison.Ordinal))
        {
            return;
        }
        var notifyMutationObservers = false;
        string? oldStyleAttributeValue = null;
        if (!_suppressStyleMutation
            && OwnerDocument.TryGetStyleMutationObservation(this, out var requiresOldValue))
        {
            notifyMutationObservers = true;
            if (requiresOldValue)
            {
                oldStyleAttributeValue = GetStyleString(Control);
            }
        }

        if (value is null)
        {
            if (HandleStyleProperty(normalized, null))
            {
                if (!_suppressStyleMutation)
                {
                    RaiseStylePropertyMutation(
                        normalized,
                        oldPropertyValue,
                        notifyMutationObservers,
                        oldStyleAttributeValue);
                }
                return;
            }

            if (_styleValues.Remove(normalized))
            {
                ApplyStyleToControl(normalized, null);

                // Clear Canvas attached values when a positioning property is removed (only when parent supports it)
                if (Control.Parent is Canvas)
                {
                    if (normalized == "left") Control.ClearValue(Canvas.LeftProperty);
                    else if (normalized == "top") Control.ClearValue(Canvas.TopProperty);
                    else if (normalized == "right") Control.ClearValue(Canvas.RightProperty);
                    else if (normalized == "bottom") Control.ClearValue(Canvas.BottomProperty);
                    else if (normalized == "z-index") Control.ClearValue(Canvas.ZIndexProperty);
                    else if (normalized == "width") Control.ClearValue(Control.WidthProperty);
                    else if (normalized == "height") Control.ClearValue(Control.HeightProperty);
                }

                // pointer-events removal always restores hit testing (generic rule, independent of parent type)
                if (normalized == "pointer-events")
                {
                    Control.IsHitTestVisible = true;
                }
            }
        }
        else
        {
            if (HandleStyleProperty(normalized, value))
            {
                if (!_suppressStyleMutation)
                {
                    RaiseStylePropertyMutation(
                        normalized,
                        oldPropertyValue,
                        notifyMutationObservers,
                        oldStyleAttributeValue);
                }
                return;
            }

            _styleValues[normalized] = value;
            ApplyStyleToControl(normalized, value);

        }

        if (!_suppressStyleMutation)
        {
            RaiseStylePropertyMutation(
                normalized,
                oldPropertyValue,
                notifyMutationObservers,
                oldStyleAttributeValue);
        }

        // Apply inline positioning immediately; scripts commonly size a canvas
        // and draw it in the same task.
        if (!_suppressStyleMutation || s_disableInlinePresentationBatching)
        {
            ApplyInlinePresentation();
        }
    }

    private void RaiseStylePropertyMutation(
        string normalizedProperty,
        string? oldPropertyValue,
        bool notifyMutationObservers,
        string? oldStyleAttributeValue)
    {
        _styleValues.TryGetValue(normalizedProperty, out var newPropertyValue);
        OwnerDocument.NotifyStylePropertyChanged(
            this,
            normalizedProperty,
            oldPropertyValue,
            newPropertyValue,
            notifyMutationObservers,
            oldStyleAttributeValue);
    }

    private bool HandleStyleProperty(string normalizedName, string? value)
    {
        switch (normalizedName)
        {
            case "margin":
                return ApplyMarginShorthand(value);
            case "margin-top":
                return ApplyMarginComponent(BoxSide.Top, value);
            case "margin-right":
                return ApplyMarginComponent(BoxSide.Right, value);
            case "margin-bottom":
                return ApplyMarginComponent(BoxSide.Bottom, value);
            case "margin-left":
                return ApplyMarginComponent(BoxSide.Left, value);
            case "padding":
                return ApplyPaddingShorthand(value);
            case "padding-top":
                return ApplyPaddingComponent(BoxSide.Top, value);
            case "padding-right":
                return ApplyPaddingComponent(BoxSide.Right, value);
            case "padding-bottom":
                return ApplyPaddingComponent(BoxSide.Bottom, value);
            case "padding-left":
                return ApplyPaddingComponent(BoxSide.Left, value);
            case "border":
                return ApplyBorderShorthand(value);
            case "border-width":
                return ApplyBorderWidthShorthand(value);
            case "border-color":
                return ApplyBorderColorShorthand(value);
            case "border-style":
                return ApplyBorderStyleShorthand(value);
            case "border-top-width":
                return ApplyBorderSideWidth(BoxSide.Top, value);
            case "border-right-width":
                return ApplyBorderSideWidth(BoxSide.Right, value);
            case "border-bottom-width":
                return ApplyBorderSideWidth(BoxSide.Bottom, value);
            case "border-left-width":
                return ApplyBorderSideWidth(BoxSide.Left, value);
            case "border-top-color":
                return ApplyBorderSideColor(BoxSide.Top, value);
            case "border-right-color":
                return ApplyBorderSideColor(BoxSide.Right, value);
            case "border-bottom-color":
                return ApplyBorderSideColor(BoxSide.Bottom, value);
            case "border-left-color":
                return ApplyBorderSideColor(BoxSide.Left, value);
            case "border-top-style":
                return ApplyBorderSideStyle(BoxSide.Top, value);
            case "border-right-style":
                return ApplyBorderSideStyle(BoxSide.Right, value);
            case "border-bottom-style":
                return ApplyBorderSideStyle(BoxSide.Bottom, value);
            case "border-left-style":
                return ApplyBorderSideStyle(BoxSide.Left, value);
            default:
                return false;
        }
    }

    private bool ApplyMarginShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            ClearAvaloniaProperty("Margin");
            RemoveThicknessStyleValues("margin");
            SetMarginLayoutLengths(null, null, null, null);
            return true;
        }

        var tokens = SplitCssTokens(value);
        if (tokens.Count is < 1 or > 4
            || !TryParseMarginLayoutLengths(tokens, out var top, out var right, out var bottom, out var left)
            || !TryParseThickness(value, allowNegative: true, allowAuto: true, out var thickness))
        {
            return false;
        }

        thickness = NormalizeThickness(thickness);
        SetAvaloniaProperty("Margin", thickness);
        _styleValues["margin"] = value;
        _styleValues["margin-top"] = tokens[0];
        _styleValues["margin-right"] = tokens.Count > 1 ? tokens[1] : tokens[0];
        _styleValues["margin-bottom"] = tokens.Count > 2 ? tokens[2] : tokens[0];
        _styleValues["margin-left"] = tokens.Count > 3
            ? tokens[3]
            : tokens.Count > 1 ? tokens[1] : tokens[0];
        SetMarginLayoutLengths(top, right, bottom, left);
        return true;
    }

    private bool ApplyMarginComponent(BoxSide side, string? value)
    {
        var margin = GetThicknessProperty("Margin");

        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            margin = SetThicknessSide(margin, side, 0);
            _styleValues.Remove($"margin-{SideToString(side)}");
            SetMarginLayoutLength(side, null);
        }
        else
        {
            if (!TryParseLength(value, GetAxisForSide(side), allowNegative: true, allowAuto: true, out var component))
            {
                return false;
            }

            if (!CssLayout.TryParseLength(value, out var layoutLength))
            {
                return false;
            }

            if (double.IsNaN(component))
            {
                component = 0;
            }

            margin = SetThicknessSide(margin, side, component);
            _styleValues[$"margin-{SideToString(side)}"] = value;
            SetMarginLayoutLength(side, layoutLength);
        }

        SetAvaloniaProperty("Margin", margin);
        RefreshMarginShorthandStyleValue(margin);
        return true;
    }

    private static bool TryParseMarginLayoutLengths(
        IReadOnlyList<string> tokens,
        out CssLength? top,
        out CssLength? right,
        out CssLength? bottom,
        out CssLength? left)
    {
        top = right = bottom = left = null;
        if (!CssLayout.TryParseLength(tokens[0], out top))
        {
            return false;
        }

        if (!CssLayout.TryParseLength(tokens.Count > 1 ? tokens[1] : tokens[0], out right)
            || !CssLayout.TryParseLength(tokens.Count > 2 ? tokens[2] : tokens[0], out bottom)
            || !CssLayout.TryParseLength(
                tokens.Count > 3 ? tokens[3] : tokens.Count > 1 ? tokens[1] : tokens[0],
                out left))
        {
            return false;
        }

        return true;
    }

    private void SetMarginLayoutLengths(
        CssLength? top,
        CssLength? right,
        CssLength? bottom,
        CssLength? left)
    {
        CssLayout.SetMarginTop(Control, top);
        CssLayout.SetMarginRight(Control, right);
        CssLayout.SetMarginBottom(Control, bottom);
        CssLayout.SetMarginLeft(Control, left);
        CssLayout.InvalidateParent(Control);
    }

    private void SetMarginLayoutLength(BoxSide side, CssLength? length)
    {
        switch (side)
        {
            case BoxSide.Top:
                CssLayout.SetMarginTop(Control, length);
                break;
            case BoxSide.Right:
                CssLayout.SetMarginRight(Control, length);
                break;
            case BoxSide.Bottom:
                CssLayout.SetMarginBottom(Control, length);
                break;
            case BoxSide.Left:
                CssLayout.SetMarginLeft(Control, length);
                break;
        }
        CssLayout.InvalidateParent(Control);
    }

    private void RefreshMarginShorthandStyleValue(Thickness fallback)
    {
        var sides = new[]
        {
            MarginStyleValue(BoxSide.Top, fallback.Top),
            MarginStyleValue(BoxSide.Right, fallback.Right),
            MarginStyleValue(BoxSide.Bottom, fallback.Bottom),
            MarginStyleValue(BoxSide.Left, fallback.Left)
        };
        _styleValues["margin"] = string.Join(" ", sides);
    }

    private string MarginStyleValue(BoxSide side, double fallback)
        => _styleValues.TryGetValue($"margin-{SideToString(side)}", out var value)
            ? value ?? FormatCssLength(fallback)
            : FormatCssLength(fallback);

    private bool ApplyPaddingShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            ClearAvaloniaProperty("Padding");
            RemoveThicknessStyleValues("padding");
            SetPaddingLayoutLengths(null, null, null, null);
            return true;
        }

        var tokens = SplitCssTokens(value);
        if (tokens.Count is < 1 or > 4
            || !TryParsePaddingLayoutLengths(tokens, out var top, out var right, out var bottom, out var left)
            || !TryParseThickness(value, allowNegative: false, allowAuto: false, out var thickness))
        {
            return false;
        }

        thickness = NormalizeThickness(thickness);
        SetAvaloniaProperty("Padding", thickness);
        _styleValues["padding"] = value;
        _styleValues["padding-top"] = tokens[0];
        _styleValues["padding-right"] = tokens.Count > 1 ? tokens[1] : tokens[0];
        _styleValues["padding-bottom"] = tokens.Count > 2 ? tokens[2] : tokens[0];
        _styleValues["padding-left"] = tokens.Count > 3
            ? tokens[3]
            : tokens.Count > 1 ? tokens[1] : tokens[0];
        SetPaddingLayoutLengths(top, right, bottom, left);
        return true;
    }

    private bool ApplyPaddingComponent(BoxSide side, string? value)
    {
        var padding = GetInlinePaddingThickness();

        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            padding = SetThicknessSide(padding, side, 0);
            _styleValues.Remove($"padding-{SideToString(side)}");
            SetPaddingLayoutLength(side, null);
        }
        else
        {
            if (!TryParseLength(value, GetAxisForSide(side), allowNegative: false, allowAuto: false, out var component))
            {
                return false;
            }
            if (!CssLayout.TryParseLength(value, out var layoutLength))
            {
                return false;
            }

            padding = SetThicknessSide(padding, side, component);
            _styleValues[$"padding-{SideToString(side)}"] = value;
            SetPaddingLayoutLength(side, layoutLength);
        }

        SetAvaloniaProperty("Padding", padding);
        RefreshPaddingShorthandStyleValue();
        return true;
    }

    private static bool TryParsePaddingLayoutLengths(
        IReadOnlyList<string> tokens,
        out CssLength? top,
        out CssLength? right,
        out CssLength? bottom,
        out CssLength? left)
    {
        top = right = bottom = left = null;
        return CssLayout.TryParseLength(tokens[0], out top)
               && CssLayout.TryParseLength(tokens.Count > 1 ? tokens[1] : tokens[0], out right)
               && CssLayout.TryParseLength(tokens.Count > 2 ? tokens[2] : tokens[0], out bottom)
               && CssLayout.TryParseLength(
                   tokens.Count > 3 ? tokens[3] : tokens.Count > 1 ? tokens[1] : tokens[0],
                   out left);
    }

    private void SetPaddingLayoutLengths(
        CssLength? top,
        CssLength? right,
        CssLength? bottom,
        CssLength? left)
    {
        CssLayout.SetPaddingTop(Control, top);
        CssLayout.SetPaddingRight(Control, right);
        CssLayout.SetPaddingBottom(Control, bottom);
        CssLayout.SetPaddingLeft(Control, left);
        CssLayout.InvalidateParent(Control);
    }

    private void SetPaddingLayoutLength(BoxSide side, CssLength? length)
    {
        switch (side)
        {
            case BoxSide.Top:
                CssLayout.SetPaddingTop(Control, length);
                break;
            case BoxSide.Right:
                CssLayout.SetPaddingRight(Control, length);
                break;
            case BoxSide.Bottom:
                CssLayout.SetPaddingBottom(Control, length);
                break;
            case BoxSide.Left:
                CssLayout.SetPaddingLeft(Control, length);
                break;
        }
        CssLayout.InvalidateParent(Control);
    }

    private Thickness GetInlinePaddingThickness()
    {
        double Parse(string property, LengthAxis axis)
            => _styleValues.TryGetValue(property, out var value)
               && value is not null
               && TryParseLength(value, axis, allowNegative: false, allowAuto: false, out var parsed)
                ? parsed
                : 0;

        return new Thickness(
            Parse("padding-left", LengthAxis.Horizontal),
            Parse("padding-top", LengthAxis.Vertical),
            Parse("padding-right", LengthAxis.Horizontal),
            Parse("padding-bottom", LengthAxis.Vertical));
    }

    private void RefreshPaddingShorthandStyleValue()
    {
        if (!_styleValues.TryGetValue("padding-top", out var top)
            || !_styleValues.TryGetValue("padding-right", out var right)
            || !_styleValues.TryGetValue("padding-bottom", out var bottom)
            || !_styleValues.TryGetValue("padding-left", out var left))
        {
            _styleValues.Remove("padding");
            return;
        }

        _styleValues["padding"] = top == right && top == bottom && top == left
            ? top
            : top == bottom && right == left
                ? $"{top} {right}"
                : right == left
                    ? $"{top} {right} {bottom}"
                    : $"{top} {right} {bottom} {left}";
    }

    private bool ApplyBorderShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            ClearAvaloniaProperty("BorderThickness");
            ClearAvaloniaProperty("BorderBrush");
            RemoveThicknessStyleValues("border", "width");
            RemoveBorderColorStyleValues();
            RemoveBorderStyleEntries();
            _styleValues.Remove("border");
            return true;
        }

        var tokens = SplitCssTokens(value);
        if (tokens.Count == 0)
        {
            return false;
        }

        var widthTokens = tokens.Where(IsLengthToken).ToArray();
        if (widthTokens.Length > 0)
        {
            ApplyBorderWidthShorthand(string.Join(" ", widthTokens));
        }

        var styleToken = tokens.FirstOrDefault(IsBorderStyleToken);
        if (styleToken is not null)
        {
            ApplyBorderStyleShorthand(styleToken);
        }

        var colorTokens = tokens.Except(widthTokens).Where(token => !IsBorderStyleToken(token)).ToArray();
        if (colorTokens.Length > 0)
        {
            ApplyBorderColorShorthand(string.Join(" ", colorTokens));
        }

        _styleValues["border"] = value;
        return true;
    }

    private bool ApplyBorderWidthShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            ClearAvaloniaProperty("BorderThickness");
            RemoveThicknessStyleValues("border", "width");
            _styleValues.Remove("border-width");
            _styleValues.Remove("border");
            return true;
        }

        if (!TryParseThickness(value, allowNegative: false, allowAuto: false, out var thickness))
        {
            return false;
        }

        thickness = NormalizeThickness(thickness);
        SetAvaloniaProperty("BorderThickness", thickness);
        UpdateThicknessStyleValues("border", thickness, "width");
        _styleValues.Remove("border");
        return true;
    }

    private bool ApplyBorderColorShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            ClearAvaloniaProperty("BorderBrush");
            RemoveBorderColorStyleValues();
            _styleValues.Remove("border");
            return true;
        }

        if (!TryParseBrush(value, out var brush))
        {
            return false;
        }

        SetAvaloniaProperty("BorderBrush", brush);
        UpdateBorderColorStyleValues(value);
        RefreshAggregateBorderColor();
        _styleValues.Remove("border");
        return true;
    }

    private bool ApplyBorderStyleShorthand(string? value)
    {
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            RemoveBorderStyleEntries();
            _styleValues.Remove("border");
            return true;
        }

        _styleValues["border-style"] = value;
        _styleValues["border-top-style"] = value;
        _styleValues["border-right-style"] = value;
        _styleValues["border-bottom-style"] = value;
        _styleValues["border-left-style"] = value;
        _styleValues.Remove("border");
        return true;
    }

    private bool ApplyBorderSideWidth(BoxSide side, string? value)
    {
        var thickness = GetThicknessProperty("BorderThickness");

        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            thickness = SetThicknessSide(thickness, side, 0);
        }
        else
        {
            if (!TryParseLength(value, GetAxisForSide(side), allowNegative: false, allowAuto: false, out var component))
            {
                return false;
            }

            thickness = SetThicknessSide(thickness, side, component);
        }

        SetAvaloniaProperty("BorderThickness", thickness);
        UpdateThicknessStyleValues("border", thickness, "width");
        _styleValues.Remove("border");
        return true;
    }

    private bool ApplyBorderSideColor(BoxSide side, string? value)
    {
        var key = $"border-{SideToString(side)}-color";
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            _styleValues.Remove(key);
            RefreshAggregateBorderColor();
            _styleValues.Remove("border");
            return true;
        }

        if (!TryParseBrush(value, out var brush))
        {
            return false;
        }

        SetAvaloniaProperty("BorderBrush", brush);
        _styleValues[key] = value;
        RefreshAggregateBorderColor();
        _styleValues.Remove("border");
        return true;
    }

    private bool ApplyBorderSideStyle(BoxSide side, string? value)
    {
        var key = $"border-{SideToString(side)}-style";
        value = value?.Trim();

        if (string.IsNullOrEmpty(value))
        {
            _styleValues.Remove(key);
        }
        else
        {
            _styleValues[key] = value;
        }

        RefreshAggregateBorderStyle();
        _styleValues.Remove("border");
        return true;
    }

    private static string SideToString(BoxSide side)
        => side switch
        {
            BoxSide.Top => "top",
            BoxSide.Right => "right",
            BoxSide.Bottom => "bottom",
            BoxSide.Left => "left",
            _ => "top"
        };

    private void UpdateThicknessStyleValues(string baseName, Thickness thickness, string? suffix = null)
    {
        var aggregateKey = suffix is null ? baseName : $"{baseName}-{suffix}";
        _styleValues[aggregateKey] = FormatThicknessString(thickness);

        SetSide(BoxSide.Top, thickness.Top);
        SetSide(BoxSide.Right, thickness.Right);
        SetSide(BoxSide.Bottom, thickness.Bottom);
        SetSide(BoxSide.Left, thickness.Left);

        void SetSide(BoxSide side, double value)
        {
            var key = $"{baseName}-{SideToString(side)}";
            if (suffix is not null)
            {
                key = $"{key}-{suffix}";
            }

            _styleValues[key] = FormatCssLength(value);
        }
    }

    private void RemoveThicknessStyleValues(string baseName, string? suffix = null)
    {
        var aggregateKey = suffix is null ? baseName : $"{baseName}-{suffix}";
        _styleValues.Remove(aggregateKey);
        foreach (BoxSide side in Enum.GetValues(typeof(BoxSide)))
        {
            var key = $"{baseName}-{SideToString(side)}";
            if (suffix is not null)
            {
                key = $"{key}-{suffix}";
            }

            _styleValues.Remove(key);
        }
    }

    private void UpdateBorderColorStyleValues(string value)
    {
        _styleValues["border-color"] = value;
        _styleValues["border-top-color"] = value;
        _styleValues["border-right-color"] = value;
        _styleValues["border-bottom-color"] = value;
        _styleValues["border-left-color"] = value;
    }

    private void RemoveBorderColorStyleValues()
    {
        _styleValues.Remove("border-color");
        _styleValues.Remove("border-top-color");
        _styleValues.Remove("border-right-color");
        _styleValues.Remove("border-bottom-color");
        _styleValues.Remove("border-left-color");
    }

    private void RemoveBorderStyleEntries()
    {
        _styleValues.Remove("border-style");
        _styleValues.Remove("border-top-style");
        _styleValues.Remove("border-right-style");
        _styleValues.Remove("border-bottom-style");
        _styleValues.Remove("border-left-style");
    }

    private Thickness GetThicknessProperty(string propertyName)
    {
        var property = FindAvaloniaProperty(Control.GetType(), propertyName);
        if (property is null)
        {
            return default;
        }

        var raw = Control.GetValue(property);
        return raw is Thickness thickness ? thickness : default;
    }

    private static Thickness SetThicknessSide(Thickness thickness, BoxSide side, double value)
        => side switch
        {
            BoxSide.Top => new Thickness(thickness.Left, value, thickness.Right, thickness.Bottom),
            BoxSide.Right => new Thickness(thickness.Left, thickness.Top, value, thickness.Bottom),
            BoxSide.Bottom => new Thickness(thickness.Left, thickness.Top, thickness.Right, value),
            BoxSide.Left => new Thickness(value, thickness.Top, thickness.Right, thickness.Bottom),
            _ => thickness
        };

    private static LengthAxis GetAxisForSide(BoxSide side)
        => side is BoxSide.Top or BoxSide.Bottom ? LengthAxis.Vertical : LengthAxis.Horizontal;

    private static Thickness NormalizeThickness(Thickness thickness)
        => new(
            NormalizeThicknessComponent(thickness.Left),
            NormalizeThicknessComponent(thickness.Top),
            NormalizeThicknessComponent(thickness.Right),
            NormalizeThicknessComponent(thickness.Bottom));

    private static double NormalizeThicknessComponent(double value)
        => double.IsNaN(value) ? 0 : value;

    private string FormatThicknessString(Thickness thickness)
        => string.Join(" ", new[]
        {
            FormatCssLength(thickness.Top),
            FormatCssLength(thickness.Right),
            FormatCssLength(thickness.Bottom),
            FormatCssLength(thickness.Left)
        });

    private static string FormatCssLength(double value)
    {
        if (double.IsNaN(value))
        {
            return "auto";
        }

        return value.ToString("0.##", CultureInfo.InvariantCulture) + "px";
    }

    private void SetAvaloniaProperty<T>(string propertyName, T value)
    {
        var property = FindAvaloniaProperty(Control.GetType(), propertyName);
        if (property is null)
        {
            return;
        }

        if (property is AvaloniaProperty<T> typed)
        {
            Control.SetValue(typed, value);
            return;
        }

        Control.SetValue(property, value);
    }

    private void ClearAvaloniaProperty(string propertyName)
    {
        var property = FindAvaloniaProperty(Control.GetType(), propertyName);
        if (property is not null)
        {
            Control.ClearValue(property);
        }
    }

    private bool TryParseBrush(string value, out IBrush? brush)
    {
        brush = null;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        if (CssValueParser.TryParseColor(value, out var functionalColor))
        {
            brush = new SolidColorBrush(functionalColor);
            return true;
        }

        try
        {
            brush = Brush.Parse(value);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private void RefreshAggregateBorderColor()
    {
        var top = _styleValues.TryGetValue("border-top-color", out var t) ? t : null;
        var right = _styleValues.TryGetValue("border-right-color", out var r) ? r : null;
        var bottom = _styleValues.TryGetValue("border-bottom-color", out var b) ? b : null;
        var left = _styleValues.TryGetValue("border-left-color", out var l) ? l : null;

        if (top is null && right is null && bottom is null && left is null)
        {
            _styleValues.Remove("border-color");
            return;
        }

        if (top is not null && top == right && top == bottom && top == left)
        {
            _styleValues["border-color"] = top;
            return;
        }

        var parts = new[] { top, right, bottom, left }
            .Select(part => part ?? "currentcolor");
        _styleValues["border-color"] = string.Join(" ", parts);
    }

    private void RefreshAggregateBorderStyle()
    {
        var top = _styleValues.TryGetValue("border-top-style", out var t) ? t : null;
        var right = _styleValues.TryGetValue("border-right-style", out var r) ? r : null;
        var bottom = _styleValues.TryGetValue("border-bottom-style", out var b) ? b : null;
        var left = _styleValues.TryGetValue("border-left-style", out var l) ? l : null;

        if (top is null && right is null && bottom is null && left is null)
        {
            _styleValues.Remove("border-style");
            return;
        }

        if (top is not null && top == right && top == bottom && top == left)
        {
            _styleValues["border-style"] = top;
            return;
        }

        var parts = new[] { top, right, bottom, left }
            .Select(part => part ?? "none");
        _styleValues["border-style"] = string.Join(" ", parts);
    }

    private bool TryParseThickness(string value, bool allowNegative, bool allowAuto, out Thickness thickness)
    {
        thickness = default;
        var tokens = SplitCssTokens(value);
        if (tokens.Count == 0)
        {
            return false;
        }

        double top;
        double right;
        double bottom;
        double left;

        if (tokens.Count == 1)
        {
            if (!TryParseLength(tokens[0], LengthAxis.Both, allowNegative, allowAuto, out var uniform))
            {
                return false;
            }

            top = right = bottom = left = uniform;
        }
        else if (tokens.Count == 2)
        {
            if (!TryParseLength(tokens[0], LengthAxis.Vertical, allowNegative, allowAuto, out var vertical)
                || !TryParseLength(tokens[1], LengthAxis.Horizontal, allowNegative, allowAuto, out var horizontal))
            {
                return false;
            }

            top = bottom = vertical;
            left = right = horizontal;
        }
        else if (tokens.Count == 3)
        {
            if (!TryParseLength(tokens[0], LengthAxis.Vertical, allowNegative, allowAuto, out top)
                || !TryParseLength(tokens[1], LengthAxis.Horizontal, allowNegative, allowAuto, out var horizontal)
                || !TryParseLength(tokens[2], LengthAxis.Vertical, allowNegative, allowAuto, out bottom))
            {
                return false;
            }

            left = right = horizontal;
        }
        else
        {
            if (!TryParseLength(tokens[0], LengthAxis.Vertical, allowNegative, allowAuto, out top)
                || !TryParseLength(tokens[1], LengthAxis.Horizontal, allowNegative, allowAuto, out right)
                || !TryParseLength(tokens[2], LengthAxis.Vertical, allowNegative, allowAuto, out bottom)
                || !TryParseLength(tokens[3], LengthAxis.Horizontal, allowNegative, allowAuto, out left))
            {
                return false;
            }
        }

        top = NormalizeThicknessComponent(top);
        right = NormalizeThicknessComponent(right);
        bottom = NormalizeThicknessComponent(bottom);
        left = NormalizeThicknessComponent(left);

        thickness = new Thickness(left, top, right, bottom);
        return true;
    }

    private bool TryParseLength(string value, LengthAxis axis, bool allowNegative, bool allowAuto, out double result)
    {
        result = 0;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var trimmed = value.Trim();
        if (allowAuto && string.Equals(trimmed, "auto", StringComparison.OrdinalIgnoreCase))
        {
            result = double.NaN;
            return true;
        }

        switch (trimmed.ToLowerInvariant())
        {
            case "thin":
                result = 1;
                return true;
            case "medium":
                result = 3;
                return true;
            case "thick":
                result = 5;
                return true;
        }

        if (CssLayout.TryParseAbsoluteLength(trimmed, out result))
        {
            if (!allowNegative && result < 0)
            {
                result = 0;
            }

            return true;
        }

        if (trimmed.EndsWith("px", StringComparison.OrdinalIgnoreCase))
        {
            trimmed = trimmed[..^2];
        }
        else if (trimmed.EndsWith("%", StringComparison.OrdinalIgnoreCase))
        {
            var numberPart = trimmed[..^1];
            if (!double.TryParse(numberPart, NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
            {
                return false;
            }

            var reference = GetReferenceSize(axis);
            result = reference * (percent / 100.0);
            return true;
        }

        if (!double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric))
        {
            return false;
        }

        if (!allowNegative && numeric < 0)
        {
            numeric = 0;
        }

        result = numeric;
        return true;
    }

    private double GetReferenceSize(LengthAxis axis)
    {
        var parent = Control.Parent as Control;
        double w = 0, h = 0;
        if (parent != null)
        {
            w = GetExplicitOrArrangedSize(parent.Width, parent.Bounds.Width);
            h = GetExplicitOrArrangedSize(parent.Height, parent.Bounds.Height);
        }

        if (w <= 0 && h <= 0)
        {
            var topLevelSize = OwnerDocument.HostViewportMetrics.ClientSize;
            if (topLevelSize.Width > 0 || topLevelSize.Height > 0)
            {
                w = topLevelSize.Width;
                h = topLevelSize.Height;
            }
            else
            {
                w = GetExplicitOrArrangedSize(Control.Width, Control.Bounds.Width);
                h = GetExplicitOrArrangedSize(Control.Height, Control.Bounds.Height);
            }
        }

        return axis switch
        {
            LengthAxis.Vertical => h,
            LengthAxis.Horizontal => w,
            _ => Math.Max(w, h)
        };
    }

    private static bool IsLengthToken(string token)
    {
        token = token.Trim();
        return token.EndsWith("px", StringComparison.OrdinalIgnoreCase)
           || token.EndsWith("%", StringComparison.OrdinalIgnoreCase)
           || token.Equals("thin", StringComparison.OrdinalIgnoreCase)
           || token.Equals("medium", StringComparison.OrdinalIgnoreCase)
           || token.Equals("thick", StringComparison.OrdinalIgnoreCase)
           || double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out _);
    }

    private static bool IsBorderStyleToken(string token)
        => token is "none" or "hidden" or "dotted" or "dashed" or "solid" or "double" or "groove" or "ridge" or "inset" or "outset";

    private static List<string> SplitCssTokens(string value)
    {
        var tokens = new List<string>();
        if (string.IsNullOrWhiteSpace(value))
        {
            return tokens;
        }

        var builder = new StringBuilder();
        var depth = 0;
        foreach (var ch in value)
        {
            if ((char.IsWhiteSpace(ch) || ch == ',') && depth == 0)
            {
                if (builder.Length > 0)
                {
                    tokens.Add(builder.ToString().Trim());
                    builder.Clear();
                }

                continue;
            }

            if (ch == '(')
            {
                depth++;
            }
            else if (ch == ')')
            {
                depth = Math.Max(0, depth - 1);
            }

            builder.Append(ch);
        }

        if (builder.Length > 0)
        {
            tokens.Add(builder.ToString().Trim());
        }

        return tokens;
    }

    private static bool AllowsNegativeLengths(string propertyName)
        => propertyName.Contains("Margin", StringComparison.OrdinalIgnoreCase);

    private static bool AllowsAutoLength(string propertyName)
        => propertyName.Contains("Width", StringComparison.OrdinalIgnoreCase)
           || propertyName.Contains("Height", StringComparison.OrdinalIgnoreCase)
           || propertyName.Contains("Margin", StringComparison.OrdinalIgnoreCase);

    private static LengthAxis DetermineAxis(string propertyName)
        => propertyName.Contains("Height", StringComparison.OrdinalIgnoreCase) ? LengthAxis.Vertical : LengthAxis.Horizontal;

    private void ApplyStyleToControl(string cssName, string? value)
    {
        // Width, height, and insets on a CSS-layout child are interpreted by
        // CssLayoutPanel during Arrange. Do not eagerly convert percentages (or
        // other relative units) to an Avalonia control Width/Height using a
        // stale window-sized fallback.
        // A disconnected DOM node must follow the same rule for *relative* values.
        if (IsCssLayoutProperty(cssName))
        {
            if (Control.Parent is CssLayoutPanel)
            {
                return; // driven by attached CssLayout props
            }
            if (HasExplicitDomTag && Control.Parent is null && IsRelativeLengthValue(value))
            {
                return; // defer %/vw/vh etc until connected to a real containing block
            }
        }

        var propertyName = CssStyleDeclaration.ToPropertyName(cssName);
        if (string.IsNullOrEmpty(propertyName))
        {
            return;
        }

        SetControlProperty(propertyName, value);
    }

    private static bool IsCssLayoutProperty(string cssName)
        => cssName is "position" or "left" or "top" or "right" or "bottom"
            or "width" or "height" or "min-width" or "min-height"
            or "max-width" or "max-height" or "overflow" or "z-index";

    private static bool IsRelativeLengthValue(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return false;
        var v = value.Trim().ToLowerInvariant();
        if (v.Contains('%')) return true;
        if (v.Contains("vw") || v.Contains("vh") || v.Contains("vmin") || v.Contains("vmax")) return true;
        // "auto", "none", unitless number (treated as px in many paths), px, em etc. are not "relative containing-block" problems here
        return false;
    }

    private void ApplyStyleAttribute(string? value)
    {
        var beforeStyle = GetStyleString(Control);
        _suppressStyleMutation = true;
        var keys = _styleValues.Keys.ToList();
        foreach (var key in keys)
        {
            ApplyStyleToControl(key, null);
        }

        _styleValues.Clear();
        _importantStyleProperties.Clear();

        try
        {
            if (!string.IsNullOrWhiteSpace(value))
            {
                void ApplyDeclaration(ReadOnlySpan<char> declaration)
                {
                    var colon = declaration.IndexOf(':');
                    if (colon < 0)
                    {
                        return;
                    }

                    var property = CssStyleDeclaration.NormalizePropertyName(
                        CssCustomPropertySyntax.TrimWhitespace(
                            declaration[..colon].ToString()));
                    if (string.IsNullOrEmpty(property))
                    {
                        return;
                    }

                    var custom = property.StartsWith("--", StringComparison.Ordinal);
                    var propertyValue = custom
                        ? CssCustomPropertySyntax.TrimWhitespace(
                            declaration[(colon + 1)..].ToString())
                        : declaration[(colon + 1)..].Trim().ToString();
                    var important = propertyValue.EndsWith(
                        "!important",
                        StringComparison.OrdinalIgnoreCase);
                    if (important)
                    {
                        propertyValue = custom
                            ? CssCustomPropertySyntax.TrimWhitespace(propertyValue[..^10])
                            : propertyValue[..^10].TrimEnd();
                    }

                    if (custom)
                    {
                        if (propertyValue.Length == 0)
                        {
                            propertyValue = " ";
                        }
                        if (_importantStyleProperties.Contains(property) && !important)
                        {
                            return;
                        }
                        SetStyleProperty(property, propertyValue);
                        if (important)
                        {
                            _importantStyleProperties.Add(property);
                        }
                        else
                        {
                            _importantStyleProperties.Remove(property);
                        }
                        return;
                    }

                    if (propertyValue.Length == 0)
                    {
                        return;
                    }
                    if (_importantStyleProperties.Contains(property) && !important)
                    {
                        return;
                    }
                    SetStyleProperty(property, propertyValue);
                    if (important)
                    {
                        _importantStyleProperties.Add(property);
                    }
                    else
                    {
                        _importantStyleProperties.Remove(property);
                    }
                }

                if (s_disableInlineStyleSpanParser)
                {
                    foreach (var declarationText in value.Split(';'))
                    {
                        ApplyDeclaration(declarationText.AsSpan());
                    }
                }
                else
                {
                    var remaining = value.AsSpan();
                    while (!remaining.IsEmpty)
                    {
                        var separator = remaining.IndexOf(';');
                        var declaration = separator >= 0 ? remaining[..separator] : remaining;
                        remaining = separator >= 0
                            ? remaining[(separator + 1)..]
                            : ReadOnlySpan<char>.Empty;
                        ApplyDeclaration(declaration);
                    }
                }

            }
        }
        finally
        {
            _suppressStyleMutation = false;
        }

        var afterStyle = GetStyleString(Control);
        RaiseAttributeMutation("style", beforeStyle, afterStyle);

        // Apply the completed inline declaration block without forcing the
        // pending document cascade between individual declarations.
        ApplyInlinePresentation();
    }

    private bool _preferInlineStyleValues;

    private void ApplyInlinePresentation()
    {
        OwnerDocument.RecordInlinePresentationApply();
        _preferInlineStyleValues = true;
        try
        {
            ApplyCanvasPositioning();
            ApplyCursor(GetStyleValue("cursor"));
        }
        finally
        {
            _preferInlineStyleValues = false;
        }
    }

    internal void ApplyCanvasPositioning(
        bool invalidateLayout = true,
        bool useLayoutChangeSet = false)
    {
        // Detached nodes cannot affect the visual tree. During chart bootstrap
        // libraries commonly set id/class/style repeatedly before insertion;
        // do not force a document-wide style read for each intermediate state.
        // ApplyInlinePresentation() deliberately bypasses this guard through
        // _preferInlineStyleValues when a node is attached to its real parent.
        if (!_preferInlineStyleValues && !OwnerDocument.IsConnectedStyleElement(this))
        {
            return;
        }

        if (!_preferInlineStyleValues && !s_disableStyleReadScope)
        {
            OwnerDocument.EnsureStylesCurrent();
        }

        if (!s_disableStyleReadScope)
        {
            _styleReadScopeDepth++;
        }
        try
        {
            var pointerEvents = GetStyleValue("pointer-events");
            var pointerEventsNone = string.Equals(pointerEvents, "none", StringComparison.OrdinalIgnoreCase);
            SetCssLayoutValue(CssLayout.PointerEventsNoneProperty, pointerEventsNone, useLayoutChangeSet);
            if (this is AvaloniaDomTextNode)
            {
                // Browser pointer targeting resolves text glyphs to their
                // containing Element. Text nodes never become independent
                // hit-test surfaces, even though pointer-events computes to
                // `auto` through their parent in this retained visual model.
                SetControlValue(Control.IsHitTestVisible, false, static (control, value) => control.IsHitTestVisible = value, useLayoutChangeSet);
            }
            else if (pointerEventsNone)
            {
                // Avalonia's native false value suppresses the complete visual
                // subtree. CSS pointer-events:none suppresses only this box;
                // descendants may restore pointer-events:auto.
                SetControlValue(Control.IsHitTestVisible, Control is CssLayoutPanel, static (control, value) => control.IsHitTestVisible = value, useLayoutChangeSet);
            }
            else
            {
                SetControlValue(Control.IsHitTestVisible, true, static (control, value) => control.IsHitTestVisible = value, useLayoutChangeSet);
            }

            var display = GetStyleValue("display");
            if (display == "none")
            {
                SetControlValue(Control.IsVisible, false, static (control, value) => control.IsVisible = value, useLayoutChangeSet);
            }
            else if (display is not null)
            {
                SetControlValue(Control.IsVisible, true, static (control, value) => control.IsVisible = value, useLayoutChangeSet);
            }

            var visibility = GetStyleValue("visibility");
            if (visibility == "hidden")
            {
                SetControlValue(Control.Opacity, 0d, static (control, value) => control.Opacity = value, useLayoutChangeSet);
            }
            else if (visibility == "visible")
            {
                // Inline layout mutations (for example, positioning a fixed
                // tooltip) use this fast path without immediately rebuilding
                // the stylesheet cascade. Preserve the previously computed
                // opacity instead of making every visible element opaque.
                if (_cssOpacityTransition is null)
                {
                    var opacityValue = GetStyleValue("opacity");
                    var restoredOpacity = _hasComputedPresentation
                                          && _cssPresentedOpacity is { } presentedOpacity
                        ? presentedOpacity
                        : double.TryParse(
                            opacityValue,
                            NumberStyles.Float,
                            CultureInfo.InvariantCulture,
                            out var opacity)
                            ? Math.Clamp(opacity, 0, 1)
                            : 1;
                    SetControlValue(Control.Opacity, restoredOpacity, static (control, value) => control.Opacity = value, useLayoutChangeSet);
                }
            }

            ApplyCssZIndexProjection();

            ApplyCssTransform(GetStyleValue("transform"), useLayoutChangeSet);
            CssLayout.SetOverflow(
                Control,
                GetStyleValue("overflow-x"),
                GetStyleValue("overflow-y"));
            if (Control is CssLayoutPanel || Control.Parent is CssLayoutPanel)
            {
                ApplyCssLayoutPanelPositioning(invalidateLayout, useLayoutChangeSet);
            }
            if (Control.Parent is Canvas)
            {
                ApplyLegacyCanvasChildPositioning();
            }
        }
        finally
        {
            if (!s_disableStyleReadScope)
            {
                _styleReadScopeDepth--;
            }
        }
    }

    private void ApplyCssZIndexProjection()
    {
        // Avalonia orders visuals only against their immediate siblings. CSS
        // positioned descendants with z-index participate in the nearest
        // ancestor stacking context, crossing intervening non-stacking boxes.
        // Project the highest positive descendant level through those boxes so
        // paint and native hit testing match that flattened CSS order.
        for (var current = this; current is not null; current = current.parentElement)
        {
            var computed = current.ComputedStyleValues.GetValueOrDefault("z-index");
            if (int.TryParse(computed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var declared))
            {
                current.Control.SetValue(Canvas.ZIndexProperty, declared);
                if (!ReferenceEquals(current, this)) break;
                continue;
            }

            var projected = 0;
            foreach (var child in current.children.OfType<AvaloniaDomElement>())
                projected = Math.Max(projected, child.Control.GetValue(Canvas.ZIndexProperty));
            current.Control.SetValue(Canvas.ZIndexProperty, projected);
        }
    }

    private bool TryStartCssOpacityTransition(
        double target,
        IReadOnlyDictionary<string, string> values)
    {
        if (!_hasComputedPresentation
            || !TryParseCssTransitionForProperty("opacity", values, out var specification)
            || specification.Duration <= TimeSpan.Zero)
        {
            CancelCssOpacityTransition(dispatchCancel: true);
            return false;
        }

        var start = _cssOpacityTransition is { } active
            ? SampleCssScalarTransition(active, Host.Services.Clock.Elapsed - active.StartedAt).Value
            : _cssPresentedOpacity ?? Control.Opacity;
        if (Math.Abs(start - target) < 0.000001)
        {
            return _cssOpacityTransition is not null;
        }

        CancelCssOpacityTransition(dispatchCancel: true);
        var startSent = specification.Delay <= TimeSpan.Zero;
        _cssOpacityTransition = new CssScalarTransition(
            start,
            target,
            specification.Duration,
            specification.Delay,
            specification.Timing,
            Host.Services.Clock.Elapsed,
            startSent);
        DispatchCssTransitionEvent("transitionrun", "opacity", 0);
        if (startSent)
        {
            DispatchCssTransitionEvent(
                "transitionstart",
                "opacity",
                Math.Min(-specification.Delay.TotalSeconds, specification.Duration.TotalSeconds));
        }
        AdvanceCssOpacityTransition(TimeSpan.Zero);
        RequestCssPaintFrame();
        return true;
    }

    private bool TryStartCssColorTransition(
        string? targetValue,
        IReadOnlyDictionary<string, string> values)
    {
        if (!TryParseCssColor(targetValue, out var target))
        {
            CancelCssColorTransition(dispatchCancel: true);
            return false;
        }
        if (!_hasComputedPresentation
            || !TryParseCssTransitionForProperty("color", values, out var specification)
            || specification.Duration <= TimeSpan.Zero
            || _cssPresentedColor is not { } presented)
        {
            CancelCssColorTransition(dispatchCancel: true);
            _cssPresentedColor = target;
            return false;
        }

        var start = _cssColorTransition is { } active
            ? SampleCssColorTransition(active, Host.Services.Clock.Elapsed - active.StartedAt).Value
            : presented;
        if (start == target)
        {
            return _cssColorTransition is not null;
        }

        CancelCssColorTransition(dispatchCancel: true);
        var startSent = specification.Delay <= TimeSpan.Zero;
        _cssColorTransition = new CssColorTransition(
            start,
            target,
            specification.Duration,
            specification.Delay,
            specification.Timing,
            Host.Services.Clock.Elapsed,
            startSent);
        DispatchCssTransitionEvent("transitionrun", "color", 0);
        if (startSent)
        {
            DispatchCssTransitionEvent(
                "transitionstart",
                "color",
                Math.Min(-specification.Delay.TotalSeconds, specification.Duration.TotalSeconds));
        }
        AdvanceCssColorTransition(TimeSpan.Zero);
        RequestCssPaintFrame();
        return true;
    }

    private static bool TryParseCssTransitionForProperty(
        string property,
        IReadOnlyDictionary<string, string> values,
        out CssTransformTransitionSpecification specification)
    {
        var properties = SplitCssTransitionList(
            values.GetValueOrDefault("transition-property") ?? string.Empty).ToArray();
        if (properties.Length > 0)
        {
            var durations = SplitCssTransitionList(
                values.GetValueOrDefault("transition-duration") ?? "0s").ToArray();
            var delays = SplitCssTransitionList(
                values.GetValueOrDefault("transition-delay") ?? "0s").ToArray();
            var timings = SplitCssTransitionList(
                values.GetValueOrDefault("transition-timing-function") ?? "ease").ToArray();
            if (durations.Length == 0) durations = ["0s"];
            if (delays.Length == 0) delays = ["0s"];
            if (timings.Length == 0) timings = ["ease"];
            for (var index = 0; index < properties.Length; index++)
            {
                var candidate = properties[index].Trim();
                if (!candidate.Equals(property, StringComparison.OrdinalIgnoreCase)
                    && !candidate.Equals("all", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                if (!TryParseCssTransitionTime(durations[index % durations.Length], out var duration))
                {
                    duration = TimeSpan.Zero;
                }
                if (!TryParseCssTransitionTime(delays[index % delays.Length], out var delay))
                {
                    delay = TimeSpan.Zero;
                }
                var timing = CssTransitionTiming.Ease;
                if (timings.Length > 0)
                {
                    CssTransitionTiming.TryParse(timings[index % timings.Length], out timing);
                }
                specification = new CssTransformTransitionSpecification(duration, delay, timing);
                return true;
            }
        }

        var shorthand = values.GetValueOrDefault("transition");
        if (!string.IsNullOrWhiteSpace(shorthand))
        {
            foreach (var item in SplitCssTransitionList(shorthand))
            {
                var candidate = "all";
                var duration = TimeSpan.Zero;
                var delay = TimeSpan.Zero;
                var timing = CssTransitionTiming.Ease;
                var sawTime = false;
                foreach (var token in SplitCssTransitionTokens(item))
                {
                    if (TryParseCssTransitionTime(token, out var time))
                    {
                        if (!sawTime) duration = time;
                        else delay = time;
                        sawTime = true;
                    }
                    else if (CssTransitionTiming.TryParse(token, out var parsedTiming))
                    {
                        timing = parsedTiming;
                    }
                    else
                    {
                        candidate = token;
                    }
                }
                if (candidate.Equals(property, StringComparison.OrdinalIgnoreCase)
                    || candidate.Equals("all", StringComparison.OrdinalIgnoreCase))
                {
                    specification = new CssTransformTransitionSpecification(duration, delay, timing);
                    return true;
                }
            }
        }

        specification = default;
        return false;
    }

    private bool TryParseCssColor(string? value, out Color color)
    {
        color = default;
        return !string.IsNullOrWhiteSpace(value)
            && TryParseBrush(value, out var brush)
            && brush is SolidColorBrush solid
            && (color = solid.Color) == solid.Color;
    }

    private void RequestCssPaintFrame()
    {
        if ((_cssOpacityTransition is null && _cssColorTransition is null)
            || !_cssPaintFrameRequest.IsEmpty)
        {
            return;
        }
        try
        {
            _cssPaintFrameRequest = Host.Services.Frames.RequestFrame(_ =>
            {
                _cssPaintFrameRequest = default;
                if (!OwnerDocument.IsConnectedStyleElement(this))
                {
                    CancelCssOpacityTransition(dispatchCancel: true);
                    CancelCssColorTransition(dispatchCancel: true);
                    return;
                }
                if (_cssOpacityTransition is { } opacity)
                {
                    AdvanceCssOpacityTransition(Host.Services.Clock.Elapsed - opacity.StartedAt);
                }
                if (_cssColorTransition is { } color)
                {
                    AdvanceCssColorTransition(Host.Services.Clock.Elapsed - color.StartedAt);
                }
                RequestCssPaintFrame();
            });
        }
        catch (ObjectDisposedException)
        {
            CancelCssOpacityTransition(dispatchCancel: false);
            CancelCssColorTransition(dispatchCancel: false);
        }
    }

    private void AdvanceCssOpacityTransition(TimeSpan elapsed)
    {
        if (_cssOpacityTransition is not { } transition) return;
        var sample = SampleCssScalarTransition(transition, elapsed);
        if (!transition.StartSent && elapsed >= transition.Delay)
        {
            transition = transition with { StartSent = true };
            _cssOpacityTransition = transition;
            DispatchCssTransitionEvent("transitionstart", "opacity", 0);
        }
        Control.Opacity = sample.Value;
        _cssPresentedOpacity = sample.Value;
        OwnerDocument.InvalidateComputedStyleSnapshots();
        if (sample.Progress < 1) return;
        _cssOpacityTransition = null;
        DispatchCssTransitionEvent(
            "transitionend", "opacity", transition.Duration.TotalSeconds);
    }

    private void AdvanceCssColorTransition(TimeSpan elapsed)
    {
        if (_cssColorTransition is not { } transition) return;
        var sample = SampleCssColorTransition(transition, elapsed);
        if (!transition.StartSent && elapsed >= transition.Delay)
        {
            transition = transition with { StartSent = true };
            _cssColorTransition = transition;
            DispatchCssTransitionEvent("transitionstart", "color", 0);
        }
        _cssPresentedColor = sample.Value;
        SetControlProperty("Foreground", CssColor(sample.Value));
        OwnerDocument.InvalidateComputedStyleSnapshots();
        if (sample.Progress < 1) return;
        _cssColorTransition = null;
        DispatchCssTransitionEvent(
            "transitionend", "color", transition.Duration.TotalSeconds);
    }

    private static CssScalarTransitionSample SampleCssScalarTransition(
        CssScalarTransition transition,
        TimeSpan elapsed)
    {
        var progress = Math.Clamp(
            (elapsed - transition.Delay).TotalMilliseconds
                / transition.Duration.TotalMilliseconds,
            0,
            1);
        var eased = transition.Timing.Evaluate(progress);
        return new CssScalarTransitionSample(
            transition.Start + (transition.Target - transition.Start) * eased,
            progress);
    }

    private static CssColorTransitionSample SampleCssColorTransition(
        CssColorTransition transition,
        TimeSpan elapsed)
    {
        var progress = Math.Clamp(
            (elapsed - transition.Delay).TotalMilliseconds
                / transition.Duration.TotalMilliseconds,
            0,
            1);
        var eased = transition.Timing.Evaluate(progress);
        static byte Channel(byte start, byte target, double progress)
            => (byte)Math.Clamp(Math.Round(start + (target - start) * progress), 0, 255);
        return new CssColorTransitionSample(
            Color.FromArgb(
                Channel(transition.Start.A, transition.Target.A, eased),
                Channel(transition.Start.R, transition.Target.R, eased),
                Channel(transition.Start.G, transition.Target.G, eased),
                Channel(transition.Start.B, transition.Target.B, eased)),
            progress);
    }

    private void CancelCssOpacityTransition(bool dispatchCancel)
    {
        if (_cssOpacityTransition is { } transition && dispatchCancel)
        {
            var elapsed = Math.Clamp(
                (Host.Services.Clock.Elapsed - transition.StartedAt - transition.Delay).TotalSeconds,
                0,
                transition.Duration.TotalSeconds);
            DispatchCssTransitionEvent("transitioncancel", "opacity", elapsed);
        }
        _cssOpacityTransition = null;
        CancelCssPaintFrameIfIdle();
    }

    private void CancelCssColorTransition(bool dispatchCancel)
    {
        if (_cssColorTransition is { } transition && dispatchCancel)
        {
            var elapsed = Math.Clamp(
                (Host.Services.Clock.Elapsed - transition.StartedAt - transition.Delay).TotalSeconds,
                0,
                transition.Duration.TotalSeconds);
            DispatchCssTransitionEvent("transitioncancel", "color", elapsed);
        }
        _cssColorTransition = null;
        CancelCssPaintFrameIfIdle();
    }

    private void CancelCssPaintFrameIfIdle()
    {
        if (_cssOpacityTransition is not null || _cssColorTransition is not null
            || _cssPaintFrameRequest.IsEmpty) return;
        Host.Services.Frames.CancelFrame(_cssPaintFrameRequest);
        _cssPaintFrameRequest = default;
    }

    private void DispatchCssTransitionEvent(string type, string property, double elapsed)
        => OwnerDocument.DispatchTransitionEvent(this, type, property, Math.Max(0, elapsed));

    private static string CssColor(Color color)
        => string.Create(
            CultureInfo.InvariantCulture,
            $"rgba({color.R}, {color.G}, {color.B}, {color.A / 255d})");

    private void ApplyCssTransform(string? value, bool useChangeSet = false)
    {
        var previousValue = _cssTransformValue;
        if ((_cssRotateTransition is not null || _cssMatrixTransition is not null)
            && string.Equals(value, _cssTransformValue, StringComparison.Ordinal))
        {
            // Layout reconciliation can reapply presentation while the
            // computed target is unchanged. Preserve the active animation
            // instead of restarting it from its current sampled angle.
            return;
        }

        _cssTransformValue = value;
        if (TryStartCssRotateTransition(previousValue, value))
        {
            return;
        }
        if (TryStartCssMatrixTransition(previousValue, value))
        {
            return;
        }

        CancelCssTransformTransition();
        ApplyCssTransformImmediately(value, useChangeSet);
    }

    private void ApplyCssTransformImmediately(string? value, bool useChangeSet = false)
    {
        if (string.IsNullOrWhiteSpace(value)
            || string.Equals(value.Trim(), "none", StringComparison.OrdinalIgnoreCase))
        {
            UpdatePercentageTransformResizeSubscription(false);
            if (!useChangeSet || Control.RenderTransform is not null)
            {
                var hadTransform = Control.RenderTransform is not null;
                Control.RenderTransform = null;
                if (hadTransform)
                {
                    OwnerDocument.InvalidateComputedStyleSnapshots();
                }
            }
            return;
        }

        var usesPercentageTranslation = false;
        var referenceWidth = ResolveTransformReference(Control.Bounds.Width, Control.DesiredSize.Width, Control.Width);
        var referenceHeight = ResolveTransformReference(Control.Bounds.Height, Control.DesiredSize.Height, Control.Height);
        var authoredTransforms = new List<Transform>();
        foreach (var function in ParseCssTransformFunctions(value))
        {
            var args = function.Arguments;
            if (function.Name == "translatex"
                && args.Length >= 1
                && TryParseTranslateLength(args[0], referenceWidth, out var x, out var relativeX))
            {
                authoredTransforms.Add(new TranslateTransform(x, 0));
                usesPercentageTranslation |= relativeX;
            }
            else if (function.Name == "translatey"
                     && args.Length >= 1
                     && TryParseTranslateLength(args[0], referenceHeight, out var y, out var relativeY))
            {
                authoredTransforms.Add(new TranslateTransform(0, y));
                usesPercentageTranslation |= relativeY;
            }
            else if (function.Name is "translate" or "translate3d"
                     && args.Length >= 1
                     && TryParseTranslateLength(args[0], referenceWidth, out x, out var combinedRelativeX))
            {
                var parsedY = 0d;
                var combinedRelativeY = false;
                if (args.Length >= 2 && !TryParseTranslateLength(args[1], referenceHeight, out parsedY, out combinedRelativeY))
                {
                    continue;
                }

                authoredTransforms.Add(new TranslateTransform(x, parsedY));
                usesPercentageTranslation |= combinedRelativeX || combinedRelativeY;
            }
            else if (function.Name is "scale" or "scale3d"
                     && TryParseCssScaleArguments(args, out var scaleX, out var scaleY))
            {
                authoredTransforms.Add(new ScaleTransform(scaleX, scaleY));
            }
            else if (function.Name == "scalex"
                     && TryParseFiniteCssNumber(args, out var scaleXOnly))
            {
                authoredTransforms.Add(new ScaleTransform(scaleXOnly, 1));
            }
            else if (function.Name == "scaley"
                     && TryParseFiniteCssNumber(args, out var scaleYOnly))
            {
                authoredTransforms.Add(new ScaleTransform(1, scaleYOnly));
            }
            else if (function.Name == "rotate"
                     && args.Length >= 1
                     && TryParseCssAngle(args[0], out var rotationDegrees))
            {
                authoredTransforms.Add(new RotateTransform(rotationDegrees));
            }
        }
        UpdatePercentageTransformResizeSubscription(usesPercentageTranslation);

        if (useChangeSet
            && Control.RenderTransform is TranslateTransform current
            && authoredTransforms.Count == 1
            && authoredTransforms[0] is TranslateTransform next
            && current.X.Equals(next.X)
            && current.Y.Equals(next.Y))
        {
            return;
        }

        if (authoredTransforms.Count == 0)
        {
            var hadTransform = Control.RenderTransform is not null;
            Control.RenderTransform = null;
            if (hadTransform)
            {
                OwnerDocument.InvalidateComputedStyleSnapshots();
            }
            return;
        }

        ApplyCssTransformOrigin(GetStyleValue("transform-origin"));

        if (authoredTransforms.Count == 1)
        {
            Control.RenderTransform = authoredTransforms[0];
            OwnerDocument.InvalidateComputedStyleSnapshots();
            return;
        }

        // CSS transform functions use column-vector composition. Avalonia's
        // Matrix uses row vectors, so the equivalent visual operation order is
        // the reverse of the authored transform list. Keeping each function as
        // its own transform also preserves non-commutative interleaving.
        var group = new TransformGroup();
        for (var index = authoredTransforms.Count - 1; index >= 0; index--)
            group.Children.Add(authoredTransforms[index]);
        Control.RenderTransform = group;
        OwnerDocument.InvalidateComputedStyleSnapshots();
    }

    private bool TryStartCssRotateTransition(string? previousValue, string? targetValue)
    {
        if (string.IsNullOrWhiteSpace(previousValue)
            || string.Equals(previousValue, targetValue, StringComparison.Ordinal)
            || !TryParseSingleCssRotation(previousValue, out var previousDegrees)
            || !TryParseSingleCssRotation(targetValue, out var targetDegrees)
            || !TryParseTransformTransition(GetStyleValue("transition"), out var specification)
            || specification.Duration <= TimeSpan.Zero)
        {
            return false;
        }

        var startDegrees = _cssRotateTransition is { } activeTransition
            ? SampleCssRotateTransition(
                activeTransition,
                Host.Services.Clock.Elapsed - activeTransition.StartedAt).Angle
            : Control.RenderTransform is RotateTransform currentRotation
                ? currentRotation.Angle
                : previousDegrees;
        if (startDegrees.Equals(targetDegrees))
        {
            return false;
        }

        CancelCssTransformTransition();
        var transition = new CssRotateTransition(
            startDegrees,
            targetDegrees,
            specification.Duration,
            specification.Delay,
            specification.Timing,
            Host.Services.Clock.Elapsed,
            null);
        transition = transition with
        {
            CompositionVisual = TryStartCssRotateCompositionTransition(transition)
        };
        _cssRotateTransition = transition;
        // Starting or retargeting a transition changes the resolved transform,
        // but subsequent interpolation samples do not invalidate layout or the
        // rest of the computed-style snapshot. The transform entry itself is a
        // live value (see CssComputedStyle), matching the browser CSSOM object.
        OwnerDocument.InvalidateComputedStyleSnapshots();
        AdvanceCssRotateTransition(TimeSpan.Zero, updateNativePresentation: transition.CompositionVisual is null);
        if (_cssRotateTransition is not null)
        {
            RequestCssTransformFrame();
        }
        return true;
    }

    private bool TryStartCssMatrixTransition(string? previousValue, string? targetValue)
    {
        if (string.IsNullOrWhiteSpace(previousValue)
            || string.Equals(previousValue, targetValue, StringComparison.Ordinal)
            || !TryParseCssTransformMatrix(previousValue, out var previousMatrix, out _)
            || !TryParseCssTransformMatrix(targetValue, out var targetMatrix, out var usesPercentageTranslation)
            || !TryParseTransformTransition(GetStyleValue("transition"), out var specification)
            || specification.Duration <= TimeSpan.Zero)
        {
            return false;
        }

        var startMatrix = _cssMatrixTransition is { } activeTransition
            ? SampleCssMatrixTransition(
                activeTransition,
                Host.Services.Clock.Elapsed - activeTransition.StartedAt).Matrix
            : Control.RenderTransform?.Value ?? previousMatrix;
        if (MatricesNearlyEqual(startMatrix, targetMatrix))
        {
            return false;
        }

        CancelCssTransformTransition();
        UpdatePercentageTransformResizeSubscription(usesPercentageTranslation);
        _cssMatrixTransition = new CssMatrixTransition(
            startMatrix,
            targetMatrix,
            specification.Duration,
            specification.Delay,
            specification.Timing,
            Host.Services.Clock.Elapsed);
        OwnerDocument.InvalidateComputedStyleSnapshots();
        AdvanceCssMatrixTransition(TimeSpan.Zero);
        if (_cssMatrixTransition is not null)
        {
            RequestCssTransformFrame();
        }
        return true;
    }

    private bool TryParseCssTransformMatrix(
        string? value,
        out Matrix matrix,
        out bool usesPercentageTranslation)
    {
        matrix = Matrix.Identity;
        usesPercentageTranslation = false;
        if (string.IsNullOrWhiteSpace(value)
            || string.Equals(value.Trim(), "none", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        var referenceWidth = ResolveTransformReference(
            Control.Bounds.Width, Control.DesiredSize.Width, Control.Width);
        var referenceHeight = ResolveTransformReference(
            Control.Bounds.Height, Control.DesiredSize.Height, Control.Height);
        var authoredTransforms = new List<Transform>();
        foreach (var function in ParseCssTransformFunctions(value))
        {
            var args = function.Arguments;
            if (function.Name == "translatex"
                && args.Length >= 1
                && TryParseTranslateLength(args[0], referenceWidth, out var x, out var relativeX))
            {
                authoredTransforms.Add(new TranslateTransform(x, 0));
                usesPercentageTranslation |= relativeX;
            }
            else if (function.Name == "translatey"
                     && args.Length >= 1
                     && TryParseTranslateLength(args[0], referenceHeight, out var y, out var relativeY))
            {
                authoredTransforms.Add(new TranslateTransform(0, y));
                usesPercentageTranslation |= relativeY;
            }
            else if (function.Name is "translate" or "translate3d"
                     && args.Length >= 1
                     && TryParseTranslateLength(
                         args[0], referenceWidth, out x, out var combinedRelativeX))
            {
                var parsedY = 0d;
                var combinedRelativeY = false;
                if (args.Length >= 2
                    && !TryParseTranslateLength(
                        args[1], referenceHeight, out parsedY, out combinedRelativeY))
                {
                    return false;
                }
                authoredTransforms.Add(new TranslateTransform(x, parsedY));
                usesPercentageTranslation |= combinedRelativeX || combinedRelativeY;
            }
            else if (function.Name is "scale" or "scale3d"
                     && TryParseCssScaleArguments(args, out var scaleX, out var scaleY))
            {
                authoredTransforms.Add(new ScaleTransform(scaleX, scaleY));
            }
            else if (function.Name == "scalex"
                     && TryParseFiniteCssNumber(args, out var scaleXOnly))
            {
                authoredTransforms.Add(new ScaleTransform(scaleXOnly, 1));
            }
            else if (function.Name == "scaley"
                     && TryParseFiniteCssNumber(args, out var scaleYOnly))
            {
                authoredTransforms.Add(new ScaleTransform(1, scaleYOnly));
            }
            else if (function.Name == "rotate"
                     && args.Length >= 1
                     && TryParseCssAngle(args[0], out var rotationDegrees))
            {
                authoredTransforms.Add(new RotateTransform(rotationDegrees));
            }
            else
            {
                return false;
            }
        }

        if (authoredTransforms.Count == 0)
        {
            return false;
        }
        if (authoredTransforms.Count == 1)
        {
            matrix = authoredTransforms[0].Value;
            return true;
        }

        var group = new TransformGroup();
        for (var index = authoredTransforms.Count - 1; index >= 0; index--)
        {
            group.Children.Add(authoredTransforms[index]);
        }
        matrix = group.Value;
        return true;
    }

    private static bool MatricesNearlyEqual(Matrix left, Matrix right)
        => Math.Abs(left.M11 - right.M11) < 0.000001
           && Math.Abs(left.M12 - right.M12) < 0.000001
           && Math.Abs(left.M21 - right.M21) < 0.000001
           && Math.Abs(left.M22 - right.M22) < 0.000001
           && Math.Abs(left.M31 - right.M31) < 0.000001
           && Math.Abs(left.M32 - right.M32) < 0.000001;

    private void RequestCssTransformFrame()
    {
        if ((_cssRotateTransition is null && _cssMatrixTransition is null)
            || !_cssTransformFrameRequest.IsEmpty)
        {
            return;
        }

        try
        {
            _cssTransformFrameRequest = Host.Services.Frames.RequestFrame(_ =>
            {
                _cssTransformFrameRequest = default;
                if ((_cssRotateTransition is null && _cssMatrixTransition is null)
                    || !OwnerDocument.IsConnectedStyleElement(this))
                {
                    CancelCssTransformTransition();
                    return;
                }

                if (_cssRotateTransition is { } rotateTransition)
                {
                    AdvanceCssRotateTransition(
                        Host.Services.Clock.Elapsed - rotateTransition.StartedAt,
                        updateNativePresentation: rotateTransition.CompositionVisual is null);
                }
                else if (_cssMatrixTransition is { } matrixTransition)
                {
                    AdvanceCssMatrixTransition(
                        Host.Services.Clock.Elapsed - matrixTransition.StartedAt);
                }
                RequestCssTransformFrame();
            });
        }
        catch (ObjectDisposedException)
        {
            CancelCssTransformTransition();
        }
    }

    private void AdvanceCssRotateTransition(TimeSpan elapsed, bool updateNativePresentation = true)
    {
        var transition = _cssRotateTransition;
        if (transition is null)
        {
            return;
        }

        var sample = SampleCssRotateTransition(transition, elapsed);
        if (updateNativePresentation)
        {
            ApplyCssTransformOrigin(GetStyleValue("transform-origin"));
            if (Control.RenderTransform is RotateTransform rotation)
            {
                rotation.Angle = sample.Angle;
            }
            else
            {
                Control.RenderTransform = new RotateTransform(sample.Angle);
            }
        }
        if (sample.Progress >= 1)
        {
            if (transition.CompositionVisual is { } compositionVisual)
            {
                ResetCssRotateCompositionAnimation(compositionVisual);
                ApplyCssTransformOrigin(GetStyleValue("transform-origin"));
                Control.RenderTransform = new RotateTransform(transition.TargetDegrees);
            }
            CancelCssTransformTransition(resetComposition: false);
            OwnerDocument.InvalidateComputedStyleSnapshots();
        }
    }

    private void AdvanceCssMatrixTransition(TimeSpan elapsed)
    {
        var transition = _cssMatrixTransition;
        if (transition is null)
        {
            return;
        }

        var sample = SampleCssMatrixTransition(transition, elapsed);
        ApplyCssTransformOrigin(GetStyleValue("transform-origin"));
        Control.RenderTransform = new MatrixTransform(sample.Matrix);
        if (sample.Progress >= 1)
        {
            CancelCssTransformTransition();
            OwnerDocument.InvalidateComputedStyleSnapshots();
        }
    }

    internal void AdvanceCssTransformTransitionForTest(TimeSpan elapsed)
    {
        if (_cssRotateTransition is { CompositionVisual: { } compositionVisual } transition)
        {
            ResetCssRotateCompositionAnimation(compositionVisual);
            _cssRotateTransition = transition with { CompositionVisual = null };
        }
        if (_cssRotateTransition is { } activeTransition)
        {
            _cssRotateTransition = activeTransition with
            {
                StartedAt = Host.Services.Clock.Elapsed - elapsed
            };
        }
        if (_cssRotateTransition is not null)
        {
            AdvanceCssRotateTransition(elapsed);
        }
        else if (_cssMatrixTransition is { } matrixTransition)
        {
            _cssMatrixTransition = matrixTransition with
            {
                StartedAt = Host.Services.Clock.Elapsed - elapsed
            };
            AdvanceCssMatrixTransition(elapsed);
        }
    }

    private void CancelCssTransformTransition(bool resetComposition = true)
    {
        if (!_cssTransformFrameRequest.IsEmpty)
        {
            Host.Services.Frames.CancelFrame(_cssTransformFrameRequest);
            _cssTransformFrameRequest = default;
        }
        if (resetComposition && _cssRotateTransition?.CompositionVisual is { } compositionVisual)
        {
            ResetCssRotateCompositionAnimation(compositionVisual);
        }
        _cssRotateTransition = null;
        _cssMatrixTransition = null;
    }

    private static void ResetCssRotateCompositionAnimation(CompositionVisual visual)
    {
        // Avalonia 11.3 does not expose CompositionObject.StopAnimation.
        // Replacing the property animation with a constant one-frame sample
        // clears the prior server-side interpolation without a visual snap.
        visual.RotationAngle = 0;
        var reset = visual.Compositor.CreateScalarKeyFrameAnimation();
        reset.Target = "RotationAngle";
        reset.Duration = TimeSpan.FromMilliseconds(1);
        reset.StopBehavior = AnimationStopBehavior.SetToFinalValue;
        reset.InsertKeyFrame(0, 0);
        reset.InsertKeyFrame(1, 0);
        visual.StartAnimation("RotationAngle", reset);
    }

    private CompositionVisual? TryStartCssRotateCompositionTransition(CssRotateTransition transition)
    {
        var visual = ElementComposition.GetElementVisual(Control);
        if (visual is null)
        {
            return null;
        }

        ApplyCssTransformOrigin(GetStyleValue("transform-origin"));
        Control.RenderTransform = new RotateTransform(transition.StartDegrees);
        var origin = Control.RenderTransformOrigin.Point;
        visual.CenterPoint = new Vector3D(
            Control.Bounds.Width * origin.X,
            Control.Bounds.Height * origin.Y,
            0);

        var deltaRadians = (float)((transition.TargetDegrees - transition.StartDegrees) * Math.PI / 180d);
        visual.RotationAngle = deltaRadians;
        var animation = visual.Compositor.CreateScalarKeyFrameAnimation();
        animation.Target = "RotationAngle";
        animation.Duration = transition.Duration;
        animation.DelayTime = transition.Delay;
        animation.DelayBehavior = AnimationDelayBehavior.SetInitialValueBeforeDelay;
        animation.StopBehavior = AnimationStopBehavior.SetToFinalValue;
        animation.InsertKeyFrame(0, 0);
        animation.InsertKeyFrame(1, deltaRadians, transition.Timing.ToAvaloniaEasing());
        visual.StartAnimation("RotationAngle", animation);
        return visual;
    }

    private static CssRotateTransitionSample SampleCssRotateTransition(
        CssRotateTransition transition,
        TimeSpan elapsed)
    {
        var activeElapsed = elapsed - transition.Delay;
        var progress = activeElapsed <= TimeSpan.Zero
            ? 0
            : Math.Clamp(activeElapsed.TotalMilliseconds / transition.Duration.TotalMilliseconds, 0, 1);
        var eased = transition.Timing.Evaluate(progress);
        return new CssRotateTransitionSample(
            transition.StartDegrees
            + (transition.TargetDegrees - transition.StartDegrees) * eased,
            progress);
    }

    private Matrix? GetComputedTransformMatrix()
    {
        if (_cssMatrixTransition is { } matrixTransition)
        {
            return SampleCssMatrixTransition(
                matrixTransition,
                Host.Services.Clock.Elapsed - matrixTransition.StartedAt).Matrix;
        }
        if (_cssRotateTransition is { } transition)
        {
            var sample = SampleCssRotateTransition(
                transition,
                Host.Services.Clock.Elapsed - transition.StartedAt);
            return Matrix.CreateRotation(sample.Angle * Math.PI / 180d);
        }

        return Control.RenderTransform?.Value;
    }

    private static CssMatrixTransitionSample SampleCssMatrixTransition(
        CssMatrixTransition transition,
        TimeSpan elapsed)
    {
        var activeElapsed = elapsed - transition.Delay;
        var progress = activeElapsed <= TimeSpan.Zero
            ? 0
            : Math.Clamp(activeElapsed.TotalMilliseconds / transition.Duration.TotalMilliseconds, 0, 1);
        var eased = transition.Timing.Evaluate(progress);
        static double Interpolate(double start, double target, double amount)
            => start + (target - start) * amount;
        return new CssMatrixTransitionSample(
            new Matrix(
                Interpolate(transition.Start.M11, transition.Target.M11, eased),
                Interpolate(transition.Start.M12, transition.Target.M12, eased),
                Interpolate(transition.Start.M21, transition.Target.M21, eased),
                Interpolate(transition.Start.M22, transition.Target.M22, eased),
                Interpolate(transition.Start.M31, transition.Target.M31, eased),
                Interpolate(transition.Start.M32, transition.Target.M32, eased)),
            progress);
    }

    private static bool TryParseSingleCssRotation(string? value, out double degrees)
    {
        degrees = 0;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var normalized = value.Trim();
        if (!normalized.StartsWith("rotate(", StringComparison.OrdinalIgnoreCase)
            || normalized.IndexOf(')') != normalized.Length - 1)
        {
            return false;
        }
        return TryParseCssRotation(normalized, out degrees);
    }

    private static bool TryParseTransformTransition(
        string? value,
        out CssTransformTransitionSpecification specification)
    {
        specification = default;
        if (string.IsNullOrWhiteSpace(value)
            || string.Equals(value.Trim(), "none", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        foreach (var item in SplitCssTransitionList(value))
        {
            string? property = null;
            var duration = TimeSpan.Zero;
            var delay = TimeSpan.Zero;
            var timeCount = 0;
            var invalid = false;
            var timing = CssTransitionTiming.Ease;
            foreach (var token in SplitCssTransitionTokens(item))
            {
                if (TryParseCssTransitionTime(token, out var time))
                {
                    if (timeCount++ == 0) duration = time;
                    else if (timeCount == 2) delay = time;
                    else invalid = true;
                    continue;
                }

                if (CssTransitionTiming.TryParse(token, out var parsedTiming))
                {
                    timing = parsedTiming;
                    continue;
                }

                if (property is null) property = token;
                else invalid = true;
            }

            property ??= "all";
            if (!invalid
                && duration >= TimeSpan.Zero
                && (string.Equals(property, "transform", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(property, "all", StringComparison.OrdinalIgnoreCase)))
            {
                specification = new CssTransformTransitionSpecification(duration, delay, timing);
                return true;
            }
        }
        return false;
    }

    private static IEnumerable<string> SplitCssTransitionList(string value)
    {
        var start = 0;
        var depth = 0;
        for (var index = 0; index < value.Length; index++)
        {
            if (value[index] == '(') depth++;
            else if (value[index] == ')') depth--;
            else if (value[index] == ',' && depth == 0)
            {
                yield return value[start..index].Trim();
                start = index + 1;
            }
        }
        yield return value[start..].Trim();
    }

    private static IEnumerable<string> SplitCssTransitionTokens(string value)
    {
        var start = 0;
        var depth = 0;
        for (var index = 0; index <= value.Length; index++)
        {
            if (index < value.Length && value[index] == '(') depth++;
            else if (index < value.Length && value[index] == ')') depth--;
            if (index < value.Length && (!char.IsWhiteSpace(value[index]) || depth != 0))
            {
                continue;
            }

            if (index > start)
            {
                yield return value[start..index].Trim();
            }
            start = index + 1;
        }
    }

    private static bool TryParseCssTransitionTime(string token, out TimeSpan time)
    {
        var normalized = token.Trim().ToLowerInvariant();
        var multiplier = normalized.EndsWith("ms", StringComparison.Ordinal) ? 1d
            : normalized.EndsWith('s') ? 1000d
            : double.NaN;
        var suffixLength = multiplier == 1 ? 2 : 1;
        if (double.IsFinite(multiplier)
            && double.TryParse(
                normalized.AsSpan(0, normalized.Length - suffixLength),
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var amount)
            && double.IsFinite(amount)
            && Math.Abs(amount * multiplier) <= TimeSpan.MaxValue.TotalMilliseconds)
        {
            time = TimeSpan.FromMilliseconds(amount * multiplier);
            return true;
        }

        time = default;
        return false;
    }

    private readonly record struct CssTransformTransitionSpecification(
        TimeSpan Duration,
        TimeSpan Delay,
        CssTransitionTiming Timing);

    private sealed record CssScalarTransition(
        double Start,
        double Target,
        TimeSpan Duration,
        TimeSpan Delay,
        CssTransitionTiming Timing,
        TimeSpan StartedAt,
        bool StartSent);

    private readonly record struct CssScalarTransitionSample(double Value, double Progress);

    private sealed record CssColorTransition(
        Color Start,
        Color Target,
        TimeSpan Duration,
        TimeSpan Delay,
        CssTransitionTiming Timing,
        TimeSpan StartedAt,
        bool StartSent);

    private readonly record struct CssColorTransitionSample(Color Value, double Progress);

    private sealed record CssRotateTransition(
        double StartDegrees,
        double TargetDegrees,
        TimeSpan Duration,
        TimeSpan Delay,
        CssTransitionTiming Timing,
        TimeSpan StartedAt,
        CompositionVisual? CompositionVisual);

    private readonly record struct CssRotateTransitionSample(double Angle, double Progress);

    private sealed record CssMatrixTransition(
        Matrix Start,
        Matrix Target,
        TimeSpan Duration,
        TimeSpan Delay,
        CssTransitionTiming Timing,
        TimeSpan StartedAt);

    private readonly record struct CssMatrixTransitionSample(Matrix Matrix, double Progress);

    private readonly record struct CssTransitionTiming(double X1, double Y1, double X2, double Y2)
    {
        public static CssTransitionTiming Ease { get; } = new(.25, .1, .25, 1);

        public double Evaluate(double progress)
        {
            if (progress <= 0) return 0;
            if (progress >= 1) return 1;

            var lower = 0d;
            var upper = 1d;
            var parameter = progress;
            for (var iteration = 0; iteration < 20; iteration++)
            {
                var x = Sample(parameter, X1, X2);
                if (Math.Abs(x - progress) < 0.000001) break;
                if (x < progress) lower = parameter;
                else upper = parameter;
                parameter = (lower + upper) / 2;
            }
            return Sample(parameter, Y1, Y2);
        }

        public SplineEasing ToAvaloniaEasing()
            => new()
            {
                X1 = X1,
                Y1 = Y1,
                X2 = X2,
                Y2 = Y2
            };

        public static bool TryParse(string token, out CssTransitionTiming timing)
        {
            var normalized = token.Trim().ToLowerInvariant();
            timing = normalized switch
            {
                "linear" => new CssTransitionTiming(0, 0, 1, 1),
                "ease" => Ease,
                "ease-in" => new CssTransitionTiming(.42, 0, 1, 1),
                "ease-out" => new CssTransitionTiming(0, 0, .58, 1),
                "ease-in-out" => new CssTransitionTiming(.42, 0, .58, 1),
                _ => default
            };
            if (normalized is "linear" or "ease" or "ease-in" or "ease-out" or "ease-in-out")
            {
                return true;
            }

            const string prefix = "cubic-bezier(";
            if (!normalized.StartsWith(prefix, StringComparison.Ordinal)
                || !normalized.EndsWith(')'))
            {
                return false;
            }

            var parts = normalized[prefix.Length..^1].Split(',', StringSplitOptions.TrimEntries);
            if (parts.Length != 4
                || !double.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var x1)
                || !double.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var y1)
                || !double.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var x2)
                || !double.TryParse(parts[3], NumberStyles.Float, CultureInfo.InvariantCulture, out var y2)
                || !double.IsFinite(x1)
                || !double.IsFinite(x2)
                || x1 is < 0 or > 1
                || x2 is < 0 or > 1
                || !double.IsFinite(y1)
                || !double.IsFinite(y2))
            {
                return false;
            }

            timing = new CssTransitionTiming(x1, y1, x2, y2);
            return true;
        }

        private static double Sample(double parameter, double first, double second)
        {
            var inverse = 1 - parameter;
            return 3 * inverse * inverse * parameter * first
                   + 3 * inverse * parameter * parameter * second
                   + parameter * parameter * parameter;
        }
    }

    private void ApplyCssTransformOrigin(string? value)
    {
        var parts = string.IsNullOrWhiteSpace(value)
            ? []
            : value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var x = 0.5d;
        var y = 0.5d;
        if (parts.Length == 1)
        {
            if (!TryParseOriginPart(parts[0], horizontal: true, out x))
                TryParseOriginPart(parts[0], horizontal: false, out y);
        }
        else if (parts.Length >= 2)
        {
            var first = parts[0].Trim().ToLowerInvariant();
            var second = parts[1].Trim().ToLowerInvariant();
            if (first is "top" or "bottom" && second is "left" or "right")
            {
                TryParseOriginPart(second, horizontal: true, out x);
                TryParseOriginPart(first, horizontal: false, out y);
            }
            else
            {
                TryParseOriginPart(first, horizontal: true, out x);
                TryParseOriginPart(second, horizontal: false, out y);
            }
        }
        Control.RenderTransformOrigin = new RelativePoint(x, y, RelativeUnit.Relative);

        static bool TryParseOriginPart(string part, bool horizontal, out double result)
        {
            result = 0.5;
            var normalized = part.Trim().ToLowerInvariant();
            if (normalized == "center") return true;
            if (horizontal && normalized == "left" || !horizontal && normalized == "top")
            {
                result = 0;
                return true;
            }
            if (horizontal && normalized == "right" || !horizontal && normalized == "bottom")
            {
                result = 1;
                return true;
            }
            if (normalized.EndsWith('%')
                && double.TryParse(normalized.AsSpan(0, normalized.Length - 1), NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
            {
                result = percent / 100d;
                return true;
            }
            return false;
        }
    }

    private void UpdatePercentageTransformResizeSubscription(bool required)
    {
        if (required == _percentageTransformResizeAttached)
        {
            return;
        }

        if (required)
        {
            Control.SizeChanged += OnPercentageTransformSizeChanged;
        }
        else
        {
            Control.SizeChanged -= OnPercentageTransformSizeChanged;
        }
        _percentageTransformResizeAttached = required;
    }

    private void OnPercentageTransformSizeChanged(object? sender, SizeChangedEventArgs args)
        => ApplyCssTransform(_cssTransformValue);

    private static double ResolveTransformReference(double bounds, double desired, double declared)
    {
        if (bounds > 0 && double.IsFinite(bounds)) return bounds;
        if (desired > 0 && double.IsFinite(desired)) return desired;
        return declared > 0 && double.IsFinite(declared) ? declared : 0;
    }

    private static bool TryParseCssRotation(string value, out double degrees)
    {
        degrees = 0;
        foreach (var function in ParseCssTransformFunctions(value))
        {
            if (function.Name == "rotate"
                && function.Arguments.Length >= 1
                && TryParseCssAngle(function.Arguments[0], out degrees))
            {
                return true;
            }
        }
        return false;
    }

    private static bool TryParseCssAngle(string value, out double degrees)
    {
        var angle = value.Trim().ToLowerInvariant();
        double multiplier;
        if (angle.EndsWith("deg", StringComparison.Ordinal))
        {
            angle = angle[..^3].Trim();
            multiplier = 1;
        }
        else if (angle.EndsWith("turn", StringComparison.Ordinal))
        {
            angle = angle[..^4].Trim();
            multiplier = 360;
        }
        else if (angle.EndsWith("rad", StringComparison.Ordinal))
        {
            angle = angle[..^3].Trim();
            multiplier = 180 / Math.PI;
        }
        else
        {
            multiplier = 1;
        }

        if (double.TryParse(angle, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
            && double.IsFinite(parsed * multiplier))
        {
            degrees = parsed * multiplier;
            return true;
        }

        degrees = 0;
        return false;
    }

    private static bool TryParseCssScaleArguments(string[] arguments, out double scaleX, out double scaleY)
    {
        scaleX = 1;
        scaleY = 1;
        if (!TryParseFiniteCssNumber(arguments, out scaleX))
        {
            return false;
        }
        scaleY = scaleX;
        return arguments.Length < 2
               || double.TryParse(arguments[1], NumberStyles.Float, CultureInfo.InvariantCulture, out scaleY)
               && double.IsFinite(scaleY);
    }

    private static bool TryParseFiniteCssNumber(string[] arguments, out double value)
    {
        value = 0;
        return arguments.Length >= 1
               && double.TryParse(arguments[0], NumberStyles.Float, CultureInfo.InvariantCulture, out value)
               && double.IsFinite(value);
    }

    private static bool TryParseTranslateLength(
        string value,
        double reference,
        out double result,
        out bool percentage)
    {
        percentage = false;
        if (CssLayout.TryParseLength(value, out var length)
            && length is { Unit: CssLengthUnit.Pixel or CssLengthUnit.Percent } parsedLength
            && CssLayout.Resolve(parsedLength, reference) is { } resolved
            && double.IsFinite(resolved))
        {
            percentage = parsedLength.Unit == CssLengthUnit.Percent;
            result = resolved;
            return true;
        }

        result = 0;
        return false;
    }

    private readonly record struct CssTransformFunction(string Name, string[] Arguments);

    private static IEnumerable<CssTransformFunction> ParseCssTransformFunctions(string value)
    {
        var index = 0;
        while (index < value.Length)
        {
            while (index < value.Length && char.IsWhiteSpace(value[index])) index++;
            var nameStart = index;
            while (index < value.Length && char.IsLetterOrDigit(value[index])) index++;
            if (nameStart == index)
            {
                index++;
                continue;
            }

            var name = value[nameStart..index].ToLowerInvariant();
            while (index < value.Length && char.IsWhiteSpace(value[index])) index++;
            if (index >= value.Length || value[index] != '(')
            {
                continue;
            }

            var depth = 1;
            var argumentsStart = ++index;
            for (; index < value.Length && depth > 0; index++)
            {
                depth += value[index] switch
                {
                    '(' => 1,
                    ')' => -1,
                    _ => 0
                };
            }
            if (depth != 0)
            {
                yield break;
            }

            var arguments = SplitCssTransformArguments(value[argumentsStart..(index - 1)]);
            yield return new CssTransformFunction(name, arguments);
        }
    }

    private static string[] SplitCssTransformArguments(string value)
    {
        var arguments = new List<string>();
        var depth = 0;
        var start = 0;
        for (var index = 0; index <= value.Length; index++)
        {
            var atEnd = index == value.Length;
            var current = atEnd ? '\0' : value[index];
            if (!atEnd)
            {
                if (current == '(')
                {
                    depth++;
                }
                else if (current == ')')
                {
                    depth--;
                }
            }

            if (!atEnd && depth != 0)
            {
                continue;
            }

            if (!atEnd && current != ',' && !char.IsWhiteSpace(current))
            {
                continue;
            }

            if (index > start)
            {
                var argument = value[start..index].Trim();
                if (argument.Length > 0)
                {
                    arguments.Add(argument);
                }
            }

            start = index + 1;
        }

        return arguments.ToArray();
    }


    private void ApplyCssLayoutPanelPositioning(bool invalidateLayout, bool useChangeSet)
    {
        ClearStaleAvaloniaLayoutValues();
        SetCssLayoutValue(CssLayout.DisplayProperty, CssLayout.ParseDisplay(GetStyleValue("display")), useChangeSet);
        var previousPosition = CssLayout.GetPosition(Control);
        var position = CssLayout.ParsePosition(GetStyleValue("position"));
        SetCssLayoutValue(CssLayout.PositionProperty, position, useChangeSet);
        SetCssLayoutValue(CssLayout.FloatProperty, CssLayout.ParseFloat(GetStyleValue("float")), useChangeSet);
        SetCssLayoutValue(CssLayout.BoxSizingProperty, CssLayout.ParseBoxSizing(GetStyleValue("box-sizing")), useChangeSet);
        SetCssLayoutValue(
            CssLayout.FixedTableLayoutProperty,
            string.Equals(GetStyleValue("table-layout")?.Trim(), "fixed", StringComparison.OrdinalIgnoreCase),
            useChangeSet);
        SetCssLayoutValue(
            CssLayout.CssHeightIsAutoProperty,
            string.Equals(GetStyleValue("height")?.Trim(), "auto", StringComparison.OrdinalIgnoreCase),
            useChangeSet);
        SetCssLayoutValue(CssLayout.VerticalAlignProperty, CssLayout.ParseVerticalAlign(GetStyleValue("vertical-align")), useChangeSet);
        SetCssLayoutValue(CssLayout.NoWrapProperty, string.Equals(GetStyleValue("white-space")?.Trim(), "nowrap", StringComparison.OrdinalIgnoreCase), useChangeSet);
        for (var index = 0; index < s_cssLayoutLengthPropertyNames.Length; index++)
        {
            SetCssLayoutLength(
                s_cssLayoutLengthPropertyNames[index],
                s_cssLayoutLengthProperties[index],
                useChangeSet);
        }
        SetCssLayoutValue(CssLayout.FlexDirectionProperty, CssLayout.ParseFlexDirection(GetStyleValue("flex-direction")), useChangeSet);
        SetCssLayoutValue(CssLayout.FlexWrapProperty, CssLayout.ParseFlexWrap(GetStyleValue("flex-wrap")), useChangeSet);
        SetCssLayoutValue(CssLayout.FlexGrowProperty, ParseCssNumber(GetStyleValue("flex-grow"), 0), useChangeSet);
        SetCssLayoutValue(CssLayout.FlexShrinkProperty, ParseCssNumber(GetStyleValue("flex-shrink"), 1), useChangeSet);
        SetCssLayoutValue(CssLayout.JustifyContentProperty, NormalizeCssKeyword(GetStyleValue("justify-content")), useChangeSet);
        SetCssLayoutValue(CssLayout.AlignContentProperty, NormalizeCssKeyword(GetStyleValue("align-content")), useChangeSet);
        SetCssLayoutValue(CssLayout.AlignItemsProperty, NormalizeCssKeyword(GetStyleValue("align-items")), useChangeSet);
        SetCssLayoutValue(CssLayout.AlignSelfProperty, NormalizeCssKeyword(GetStyleValue("align-self")), useChangeSet);
        SetCssLayoutValue(CssLayout.GridTemplateColumnsProperty, GetStyleValue("grid-template-columns"), useChangeSet);
        SetCssLayoutValue(CssLayout.GridTemplateRowsProperty, GetStyleValue("grid-template-rows"), useChangeSet);
        var gridColumn = GetStyleValue("grid-column");
        var gridColumnStart = GetStyleValue("grid-column-start");
        var gridColumnEnd = GetStyleValue("grid-column-end");
        if ((string.IsNullOrWhiteSpace(gridColumn)
             || string.Equals(gridColumn, "auto", StringComparison.OrdinalIgnoreCase))
            && !string.IsNullOrWhiteSpace(gridColumnStart)
            && !string.Equals(gridColumnStart, "auto", StringComparison.OrdinalIgnoreCase))
        {
            gridColumn = $"{gridColumnStart} / {gridColumnEnd}";
        }
        SetCssLayoutValue(CssLayout.GridColumnProperty, gridColumn, useChangeSet);
        SetCssLayoutValue(CssLayout.OrderProperty, (int)ParseCssNumber(GetStyleValue("order"), 0), useChangeSet);
        if (previousPosition != position
            && (previousPosition == CssPosition.Fixed || position == CssPosition.Fixed))
        {
            RefreshOverflowClipChain(Control.Parent as Control);
        }
        RefreshListMarkerPresentation();
        if (invalidateLayout)
        {
            CssLayout.InvalidateParent(Control);
            if (Control is CssLayoutPanel panel)
            {
                panel.InvalidateMeasure();
                panel.InvalidateArrange();
            }
        }
    }

    private static void RefreshOverflowClipChain(Control? start)
    {
        for (var current = start; current is not null; current = current.Parent as Control)
        {
            if (current is not CssLayoutPanel panel)
            {
                continue;
            }
            panel.RefreshOverflowClipForFixedDescendants();
            if (CssLayout.GetDocumentViewportRoot(panel))
            {
                break;
            }
        }
    }

    internal void RefreshOverflowClipChainAfterChildMutation()
        => RefreshOverflowClipChain(Control);

    internal void RefreshListMarkerPresentation()
    {
        if (Control is not CssLayoutPanel panel)
        {
            return;
        }

        var display = CssLayout.ParseDisplay(_computedStyleValues.GetValueOrDefault("display"));
        var type = _computedStyleValues.GetValueOrDefault("list-style-type", "disc").Trim().ToLowerInvariant() switch
        {
            "circle" => CssListStyleType.Circle,
            "square" => CssListStyleType.Square,
            "decimal" => CssListStyleType.Decimal,
            "none" => CssListStyleType.None,
            _ => CssListStyleType.Disc
        };
        if (display != CssDisplay.ListItem || type == CssListStyleType.None)
        {
            panel.SetListMarker(null);
            return;
        }

        var position = string.Equals(
            _computedStyleValues.GetValueOrDefault("list-style-position", "outside").Trim(),
            "inside",
            StringComparison.OrdinalIgnoreCase)
                ? CssListStylePosition.Inside
                : CssListStylePosition.Outside;
        var fontSize = _computedStyleValues.TryGetValue("font-size", out var sizeValue)
                       && TryParseCssPixels(sizeValue, out var parsedSize)
            ? Math.Max(1, parsedSize)
            : 16;
        var parsedLineHeight = 0d;
        var hasExplicitLineHeight = _computedStyleValues.TryGetValue("line-height", out var lineHeightValue)
                                    && TryParseCssPixels(lineHeightValue, out parsedLineHeight);
        var fontStyle = _computedStyleValues.GetValueOrDefault("font-style", "normal").Trim().ToLowerInvariant() switch
        {
            "italic" => FontStyle.Italic,
            "oblique" => FontStyle.Oblique,
            _ => FontStyle.Normal
        };
        var weightValue = _computedStyleValues.GetValueOrDefault("font-weight", "400").Trim();
        var fontWeight = weightValue.ToLowerInvariant() switch
        {
            "bold" or "bolder" => FontWeight.Bold,
            "lighter" => FontWeight.Light,
            _ when int.TryParse(weightValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var numeric)
                => (FontWeight)Math.Clamp(numeric, 1, 1000),
            _ => FontWeight.Normal
        };
        var family = ResolveFont(
            _computedStyleValues.GetValueOrDefault("font-family", "sans-serif"),
            _computedStyleValues).Family;
        var typeface = new Typeface(family, fontStyle, fontWeight);
        var brush = _computedStyleValues.TryGetValue("color", out var color)
                    && TryParseBrush(color, out var parsedBrush)
                    && parsedBrush is not null
            ? parsedBrush
            : Brushes.Black;
        var text = type == CssListStyleType.Decimal ? $"{ResolveHtmlListItemOrdinal()}." : string.Empty;
        var automaticLineMetrics = new FormattedText(
            type == CssListStyleType.Decimal ? text : "M",
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            typeface,
            fontSize,
            brush);
        // CSS `normal` is a font-dependent used line height. Ordinary DOM text
        // delegates that choice to Avalonia's TextLayout, so generated marker
        // content must consume the same backend metric rather than a separate
        // 1.2em heuristic. Keeping one resolved value here feeds both native
        // and portable list-marker layout.
        var lineHeight = hasExplicitLineHeight
            ? Math.Max(0, parsedLineHeight)
            : automaticLineMetrics.Height;
        Size markerSize;
        if (type == CssListStyleType.Decimal)
        {
            markerSize = new Size(automaticLineMetrics.Width, automaticLineMetrics.Height);
        }
        else
        {
            // Chromium's 16px marker formatting box is 7x18: the glyph is a
            // 6px shape centered inside the first line box, rather than a 6x6
            // principal box. Keep that distinction so geometry and paint both
            // align with the browser authority fixture.
            markerSize = new Size(Math.Max(6, fontSize * 0.4375), lineHeight);
        }

        panel.SetListMarker(new CssListMarker(
            type,
            position,
            text,
            brush,
            typeface,
            fontSize,
            lineHeight,
            markerSize));
    }

    private int ResolveHtmlListItemOrdinal()
    {
        var list = parentElement;
        if (list is null || !string.Equals(list.localName, "ol", StringComparison.OrdinalIgnoreCase))
        {
            return 1;
        }

        var items = list.GetChildElements()
            .Where(static child => child.nodeType == 1
                                   && string.Equals(child.localName, "li", StringComparison.OrdinalIgnoreCase))
            .ToArray();
        var reversed = list.hasAttribute("reversed");
        var hasExplicitStart = int.TryParse(
            list.getAttribute("start"),
            NumberStyles.Integer,
            CultureInfo.InvariantCulture,
            out var start);
        var counter = hasExplicitStart ? start : reversed ? items.Length : 1;
        if (reversed && !hasExplicitStart)
        {
            // In a reversed list the first explicit LI value anchors preceding
            // implicit ordinals too. For example, [implicit, value=6,
            // implicit] is 7/6/5 rather than 3/6/5.
            for (var index = 0; index < items.Length; index++)
            {
                if (int.TryParse(
                        items[index].getAttribute("value"),
                        NumberStyles.Integer,
                        CultureInfo.InvariantCulture,
                        out var anchoredValue))
                {
                    counter = anchoredValue + index;
                    break;
                }
            }
        }
        var step = reversed ? -1 : 1;
        foreach (var item in items)
        {
            if (int.TryParse(item.getAttribute("value"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value))
            {
                counter = value;
            }
            if (ReferenceEquals(item, this)) return counter;
            counter += step;
        }
        return counter;
    }

    private void ClearStaleAvaloniaLayoutValues()
    {
        if (!HasExplicitDomTag || Control.Parent is not CssLayoutPanel)
        {
            return;
        }

        // DOM CSS lengths are owned by CssLayoutPanel. Values previously
        // written to Avalonia properties while the element was disconnected
        // must not compete with the rectangle supplied by ArrangeOverride.
        if (Control.IsSet(Control.WidthProperty) && IsDeclaredCssLength("width")) Control.ClearValue(Control.WidthProperty);
        if (Control.IsSet(Control.HeightProperty) && IsDeclaredCssLength("height")) Control.ClearValue(Control.HeightProperty);
        if (Control.IsSet(Control.MinWidthProperty) && IsDeclaredCssLength("min-width")) Control.ClearValue(Control.MinWidthProperty);
        if (Control.IsSet(Control.MinHeightProperty) && IsDeclaredCssLength("min-height")) Control.ClearValue(Control.MinHeightProperty);
        if (Control.IsSet(Control.MaxWidthProperty) && IsDeclaredCssLength("max-width")) Control.ClearValue(Control.MaxWidthProperty);
        if (Control.IsSet(Control.MaxHeightProperty) && IsDeclaredCssLength("max-height")) Control.ClearValue(Control.MaxHeightProperty);

        // These attached values belong only to legacy XAML Canvas hosting.
        // Leaving them on a CSS child makes diagnostics and a later reparent
        // observe two conflicting positioning systems.
        if (Control.IsSet(Canvas.LeftProperty)) Control.ClearValue(Canvas.LeftProperty);
        if (Control.IsSet(Canvas.TopProperty)) Control.ClearValue(Canvas.TopProperty);
        if (Control.IsSet(Canvas.RightProperty)) Control.ClearValue(Canvas.RightProperty);
        if (Control.IsSet(Canvas.BottomProperty)) Control.ClearValue(Canvas.BottomProperty);
    }

    private bool IsDeclaredCssLength(string name)
    {
        if (!_declaredStyleProperties.Contains(name) && !_styleValues.ContainsKey(name))
        {
            return false;
        }

        var value = _computedStyleValues.TryGetValue(name, out var computed)
            ? computed
            : (_styleValues.TryGetValue(name, out var inline) ? inline : null);
        return !string.IsNullOrWhiteSpace(value)
               && !string.Equals(value.Trim(), "auto", StringComparison.OrdinalIgnoreCase)
               && !string.Equals(value.Trim(), "none", StringComparison.OrdinalIgnoreCase);
    }

    private static double ParseCssNumber(string? value, double fallback)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed) && double.IsFinite(parsed)
            ? parsed
            : fallback;

    private static string? NormalizeCssKeyword(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return value;
        }

        var span = value.AsSpan().Trim();
        var alreadyNormalized = span.Length == value.Length;
        for (var index = 0; alreadyNormalized && index < span.Length; index++)
        {
            alreadyNormalized = !char.IsUpper(span[index]);
        }

        return alreadyNormalized ? value : span.ToString().ToLowerInvariant();
    }

    private void ApplyLegacyCanvasChildPositioning()
    {
        if (Control.Parent is not Canvas parent)
        {
            return;
        }

        if (TryGetLengthInPixels("left", out var left)) Control.SetValue(Canvas.LeftProperty, left);
        if (TryGetLengthInPixels("top", out var top)) Control.SetValue(Canvas.TopProperty, top);
        if (TryGetLengthInPixels("right", out var right)) Control.SetValue(Canvas.RightProperty, right);
        if (TryGetLengthInPixels("bottom", out var bottom)) Control.SetValue(Canvas.BottomProperty, bottom);
        if (TryGetLengthInPixels("width", out var width) && width >= 0) Control.Width = width;
        if (TryGetLengthInPixels("height", out var height) && height >= 0) Control.Height = height;

        parent.InvalidateMeasure();
        parent.InvalidateArrange();
    }

    private void SetCssLayoutLength(
        string property,
        AttachedProperty<CssLength?> attachedProperty,
        bool useChangeSet)
    {
        var raw = GetStyleValue(property);
        if (!CssLayout.TryParseLength(raw, out var length))
        {
            return;
        }

        SetCssLayoutValue(attachedProperty, length, useChangeSet);
    }

    private void SetCssLayoutValue<T>(AttachedProperty<T> property, T value, bool useChangeSet)
    {
        if (!useChangeSet || !EqualityComparer<T>.Default.Equals(Control.GetValue(property), value))
        {
            Control.SetValue(property, value);
        }
    }

    private void SetControlValue<T>(
        T current,
        T value,
        Action<Control, T> setter,
        bool useChangeSet)
    {
        if (!useChangeSet || !EqualityComparer<T>.Default.Equals(current, value))
        {
            setter(Control, value);
        }
    }

    internal string? GetStyleValue(string property)
    {
        if (_preferInlineStyleValues)
        {
            if (_styleValues.TryGetValue(property, out var inline))
            {
                return inline;
            }

            return _computedStyleValues.TryGetValue(property, out var priorComputed)
                ? priorComputed
                : null;
        }

        if (s_disableStyleReadScope || _styleReadScopeDepth == 0)
        {
            OwnerDocument.EnsureStylesCurrent();
        }
        if (_computedStyleValues.TryGetValue(property, out var computed))
        {
            return computed;
        }

        _styleValues.TryGetValue(property, out var value);
        return value;
    }

    /// <summary>
    /// Uses the existing TryParseLength (which already handles px and % against a reference size).
    /// </summary>
    private bool TryGetLengthInPixels(string cssProperty, out double result)
    {
        result = 0;
        var value = GetStyleValue(cssProperty);
        if (string.IsNullOrWhiteSpace(value))
            return false;
        if (value.Trim().Equals("auto", StringComparison.OrdinalIgnoreCase)
            || value.Trim().Equals("none", StringComparison.OrdinalIgnoreCase))
            return false;

        var axis = cssProperty.Contains("height", StringComparison.OrdinalIgnoreCase)
                   || cssProperty.Equals("top", StringComparison.OrdinalIgnoreCase)
                   || cssProperty.Equals("bottom", StringComparison.OrdinalIgnoreCase)
            ? LengthAxis.Vertical
            : LengthAxis.Horizontal;

        // allow negative for left/top in some edge cases, auto for safety
        return TryParseLength(value, axis, allowNegative: true, allowAuto: true, out result);
    }

    private void ApplyCursor(string? value)
    {
        if (string.IsNullOrWhiteSpace(value) || value == "auto" || value == "initial" || value == "inherit")
        {
            Control.Cursor = null;
            return;
        }

        StandardCursorType cursorType = value.ToLowerInvariant() switch
        {
            "crosshair" => StandardCursorType.Cross,
            "pointer" or "hand" => StandardCursorType.Hand,
            "text" or "vertical-text" => StandardCursorType.Ibeam,
            "move" or "grab" or "grabbing" => StandardCursorType.SizeAll,
            "ew-resize" or "col-resize" or "e-resize" or "w-resize" => StandardCursorType.SizeWestEast,
            "ns-resize" or "row-resize" or "n-resize" or "s-resize" => StandardCursorType.SizeNorthSouth,
            "nwse-resize" or "nesw-resize" => StandardCursorType.SizeAll, // fallback for diagonals if not present in all TFMs
            "not-allowed" or "no-drop" => StandardCursorType.No,
            "wait" or "progress" => StandardCursorType.Wait,
            "help" => StandardCursorType.Help,
            "cell" => StandardCursorType.Hand,
            "copy" or "alias" or "zoom-in" or "zoom-out" => StandardCursorType.Hand,
            "all-scroll" => StandardCursorType.SizeAll,
            _ => StandardCursorType.Arrow
        };

        try
        {
            Control.Cursor = new Cursor(cursorType);
        }
        catch
        {
            Control.Cursor = null;
        }
    }

    private bool ApplySvgAttribute(string name, string? value)
    {
        if (Control is not SvgLayoutPanel and not SvgGeometryControl)
        {
            return false;
        }

        // The Skia-backed root serializes the live DOM lazily on its next
        // render, so mutations retain normal DOM identity and event behavior.
        InvalidateSvgRenderer();

        switch (name)
        {
            case "viewbox" when Control is SvgLayoutPanel svg:
                if (TryParseSvgNumbers(value, 4, out var viewBox))
                {
                    svg.ViewBox = new Rect(viewBox[0], viewBox[1], Math.Max(0, viewBox[2]), Math.Max(0, viewBox[3]));
                    svg.InvalidateMeasure();
                    svg.InvalidateVisual();
                }
                return true;
            case "preserveaspectratio" when Control is SvgLayoutPanel svg:
                svg.StretchViewBox = string.Equals(value?.Trim(), "none", StringComparison.OrdinalIgnoreCase);
                svg.InvalidateVisual();
                return true;
            case "d" when Control is SvgPathControl path:
                try
                {
                    path.Data = string.IsNullOrWhiteSpace(value) ? null : Geometry.Parse(value);
                }
                catch
                {
                    path.Data = null;
                }
                path.InvalidateVisual();
                return true;
            case "cx" when Control is SvgCircleControl circle:
                if (TryParseSvgNumber(value, out var cx)) circle.CenterX = cx;
                circle.InvalidateVisual();
                return true;
            case "cy" when Control is SvgCircleControl circle:
                if (TryParseSvgNumber(value, out var cy)) circle.CenterY = cy;
                circle.InvalidateVisual();
                return true;
            case "r" when Control is SvgCircleControl circle:
                if (TryParseSvgNumber(value, out var radius)) circle.Radius = Math.Max(0, radius);
                circle.InvalidateVisual();
                return true;
            case "fill":
                ApplySvgFill(value);
                Control.InvalidateVisual();
                return true;
            case "stroke":
                ApplySvgStroke(value);
                Control.InvalidateVisual();
                return true;
            case "stroke-width" when Control is SvgGeometryControl geometry:
                if (TryParseSvgNumber(value, out var strokeWidth)) geometry.StrokeThickness = Math.Max(0, strokeWidth);
                geometry.InvalidateVisual();
                return true;
            case "fill-rule":
            case "clip-rule":
            case "stroke-linecap":
            case "stroke-linejoin":
            case "transform":
            case "xmlns":
                // Retain these attributes in the DOM. The common icon paths
                // render without additional layout participation; transform
                // support can be extended as new runtime observations require.
                return true;
            default:
                return false;
        }
    }

    private void ApplySvgFill(string? value)
    {
        var normalized = value?.Trim() ?? string.Empty;
        if (Control is SvgLayoutPanel svg)
        {
            svg.SuppressFill = string.Equals(normalized, "none", StringComparison.OrdinalIgnoreCase);
            svg.FillUsesCurrentColor = string.Equals(normalized, "currentColor", StringComparison.OrdinalIgnoreCase);
            svg.Fill = !svg.SuppressFill && !svg.FillUsesCurrentColor && TryParseBrush(normalized, out var brush) ? brush : null;
        }
        else if (Control is SvgGeometryControl geometry)
        {
            geometry.SuppressFill = string.Equals(normalized, "none", StringComparison.OrdinalIgnoreCase);
            geometry.FillUsesCurrentColor = string.Equals(normalized, "currentColor", StringComparison.OrdinalIgnoreCase);
            geometry.Fill = !geometry.SuppressFill && !geometry.FillUsesCurrentColor && TryParseBrush(normalized, out var brush) ? brush : null;
        }
    }

    private void ApplySvgStroke(string? value)
    {
        var normalized = value?.Trim() ?? string.Empty;
        if (Control is SvgLayoutPanel svg)
        {
            svg.StrokeUsesCurrentColor = string.Equals(normalized, "currentColor", StringComparison.OrdinalIgnoreCase);
            svg.Stroke = !string.Equals(normalized, "none", StringComparison.OrdinalIgnoreCase)
                         && !svg.StrokeUsesCurrentColor
                         && TryParseBrush(normalized, out var brush)
                ? brush
                : null;
        }
        else if (Control is SvgGeometryControl geometry)
        {
            geometry.StrokeUsesCurrentColor = string.Equals(normalized, "currentColor", StringComparison.OrdinalIgnoreCase);
            geometry.Stroke = !string.Equals(normalized, "none", StringComparison.OrdinalIgnoreCase)
                              && !geometry.StrokeUsesCurrentColor
                              && TryParseBrush(normalized, out var brush)
                ? brush
                : null;
        }
    }

    private static bool TryParseSvgNumber(string? value, out double number)
        => double.TryParse(value?.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out number)
           && double.IsFinite(number);

    private static bool TryParseSvgNumbers(string? value, int count, out double[] numbers)
    {
        numbers = (value ?? string.Empty)
            .Split(new[] { ' ', ',', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
            .Select(token => double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
                ? number
                : double.NaN)
            .ToArray();
        return numbers.Length == count && numbers.All(double.IsFinite);
    }

    private bool SetControlProperty(string propertyName, string? value)
    {
        if (TrySetAvaloniaProperty(propertyName, value))
        {
            return true;
        }

        var prop = FindClrProperty(Control.GetType(), propertyName);
        if (prop is null || !prop.CanWrite)
        {
            return false;
        }

        try
        {
            var converted = ConvertToPropertyValue(propertyName, prop.PropertyType, value);
            prop.SetValue(Control, converted);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private bool TrySetAvaloniaProperty(string propertyName, string? value)
    {
        var type = Control.GetType();
        var registered = FindAvaloniaProperty(type, propertyName);
        if (registered is null)
        {
            return false;
        }

        if (value is null)
        {
            Control.ClearValue(registered);
            return true;
        }

        var converted = ConvertToPropertyValue(propertyName, registered.PropertyType, value);

        try
        {
            Control.SetValue(registered, converted);
            return true;
        }
        catch
        {
            return false;
        }
    }

    private object? ConvertToPropertyValue(string propertyName, Type propertyType, string? value)
    {
        if (value is null)
        {
            return null;
        }

        if (propertyType == typeof(string))
        {
            return value;
        }

        if (propertyType == typeof(double) || propertyType == typeof(double?))
        {
            if (string.Equals(value, "none", StringComparison.OrdinalIgnoreCase)
                && propertyName.Contains("Max", StringComparison.OrdinalIgnoreCase))
            {
                return double.PositiveInfinity;
            }

            var axis = DetermineAxis(propertyName);
            var allowNegative = AllowsNegativeLengths(propertyName);
            var allowAuto = AllowsAutoLength(propertyName);
            if (TryParseLength(value, axis, allowNegative, allowAuto, out var length))
            {
                return length;
            }
        }

        if (propertyType == typeof(Thickness) || propertyType == typeof(Thickness?))
        {
            var allowNegative = string.Equals(propertyName, "Margin", StringComparison.OrdinalIgnoreCase);
            var allowAuto = allowNegative;
            if (TryParseThickness(value, allowNegative, allowAuto, out var thickness))
            {
                return thickness;
            }
        }

        if (propertyType == typeof(FontFamily))
        {
            return ResolveFont(value, _computedStyleValues).Family;
        }

        if (TryConvertFromString(propertyType, value, out var convertedFromString))
        {
            return convertedFromString;
        }

        if (propertyType == typeof(Color) || propertyType == typeof(Color?))
        {
            if (Color.TryParse(value, out var color))
            {
                return color;
            }
        }

        if (typeof(IBrush).IsAssignableFrom(propertyType))
        {
            if (TryParseBrush(value, out var cssBrush))
            {
                return cssBrush;
            }

            try
            {
                return Brush.Parse(value);
            }
            catch
            {
            }
        }

        if (propertyType == typeof(double) && double.TryParse(value, NumberStyles.Any, CultureInfo.InvariantCulture, out var d))
        {
            return d;
        }

        if (propertyType == typeof(int) && int.TryParse(value, NumberStyles.Any, CultureInfo.InvariantCulture, out var i))
        {
            return i;
        }

        if (propertyType == typeof(bool) && bool.TryParse(value, out var b))
        {
            return b;
        }

        return value;
    }

    private CssFontResolution ResolveFont(
        string familyList,
        IReadOnlyDictionary<string, string> values)
    {
        var style = values.GetValueOrDefault("font-style", "normal").Trim().ToLowerInvariant() switch
        {
            "italic" => FontStyle.Italic,
            "oblique" => FontStyle.Oblique,
            _ => FontStyle.Normal
        };
        var weightValue = values.GetValueOrDefault("font-weight", "400").Trim();
        var weight = weightValue.ToLowerInvariant() switch
        {
            "bold" or "bolder" => FontWeight.Bold,
            "lighter" => FontWeight.Light,
            _ when int.TryParse(weightValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out var numeric)
                => (FontWeight)Math.Clamp(numeric, 1, 1000),
            _ => FontWeight.Normal
        };
        var stretch = values.GetValueOrDefault("font-stretch", "normal").Trim().ToLowerInvariant() switch
        {
            "condensed" => FontStretch.Condensed,
            "expanded" => FontStretch.Expanded,
            _ => FontStretch.Normal
        };
        return CssFontResolver.Resolve(familyList, OwnerDocument.FontFaces, style, weight, stretch);
    }

    private static bool TryConvertFromString(Type propertyType, string value, out object? converted)
    {
        var converter = TypeDescriptor.GetConverter(propertyType);
        if (converter is not null && converter.CanConvertFrom(typeof(string)))
        {
            try
            {
                converted = converter.ConvertFrom(null, CultureInfo.InvariantCulture, value);
                return true;
            }
            catch
            {
            }
        }

        converted = null;
        return false;
    }

    private static AvaloniaProperty? FindAvaloniaProperty(Type controlType, string propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName) || controlType is null)
        {
            return null;
        }

        var cacheKey = (controlType, propertyName);
        if (!s_disableNativePropertyCache)
        {
            lock (s_nativePropertyCacheLock)
            {
                if (s_nativePropertyCache.TryGetValue(cacheKey, out var cached))
                {
                    return cached;
                }
            }
        }

        var property = FindAvaloniaPropertyUncached(controlType, propertyName);
        if (!s_disableNativePropertyCache)
        {
            lock (s_nativePropertyCacheLock)
            {
                if (s_nativePropertyCache.Count < MaximumNativePropertyCacheEntries)
                {
                    s_nativePropertyCache.TryAdd(cacheKey, property);
                }
            }
        }

        return property;
    }

    private static AvaloniaProperty? FindAvaloniaPropertyUncached(Type controlType, string propertyName)
    {
        var registry = AvaloniaPropertyRegistry.Instance;

        for (var type = controlType; type is not null && typeof(AvaloniaObject).IsAssignableFrom(type); type = type.BaseType)
        {
            var property = registry.FindRegistered(type, propertyName);
            if (property is not null)
            {
                return property;
            }

            PropertyInfo? info = null;
            try
            {
                info = type.GetProperty(propertyName, BindingFlags.IgnoreCase | BindingFlags.Instance | BindingFlags.Public | BindingFlags.FlattenHierarchy);
            }
            catch (AmbiguousMatchException)
            {
                // Prefer explicit lookup below using registered property names
            }
            if (info is not null)
            {
                property = registry.FindRegistered(type, info.Name);
                if (property is not null)
                {
                    return property;
                }
            }

            var pascal = CssStyleDeclaration.ToPropertyName(propertyName);
            if (!string.Equals(pascal, propertyName, StringComparison.OrdinalIgnoreCase))
            {
                property = registry.FindRegistered(type, pascal);
                if (property is not null)
                {
                    return property;
                }
            }
        }

        return null;
    }

    private static PropertyInfo? FindClrProperty(Type controlType, string propertyName)
    {
        var cacheKey = (controlType, propertyName);
        if (!s_disableNativePropertyCache)
        {
            lock (s_nativePropertyCacheLock)
            {
                if (s_clrPropertyCache.TryGetValue(cacheKey, out var cached))
                {
                    return cached;
                }
            }
        }

        var property = controlType.GetProperty(
                           propertyName,
                           BindingFlags.IgnoreCase | BindingFlags.Instance | BindingFlags.Public | BindingFlags.DeclaredOnly)
                       ?? controlType.GetProperty(
                           propertyName,
                           BindingFlags.IgnoreCase | BindingFlags.Instance | BindingFlags.Public);
        if (!s_disableNativePropertyCache)
        {
            lock (s_nativePropertyCacheLock)
            {
                if (s_clrPropertyCache.Count < MaximumNativePropertyCacheEntries)
                {
                    s_clrPropertyCache.TryAdd(cacheKey, property);
                }
            }
        }

        return property;
    }

    internal CssComputedStyle ComputeComputedStyle(long generation)
    {
        if (_computedStyleSnapshot is not null
            && _computedStyleSnapshotGeneration == generation)
        {
            OwnerDocument.RecordComputedStyleSnapshot(cacheHit: true);
            return _computedStyleSnapshot;
        }

        var state = ComputedStyleBuilder.CaptureState(this);
        if (OwnerDocument.ComputedStyleSnapshotStateReuseEnabled
            && _computedStyleSnapshot is not null
            && _computedStyleSnapshotState is { } previousState
            && previousState.Equals(state))
        {
            _computedStyleSnapshotGeneration = generation;
            OwnerDocument.RecordComputedStyleSnapshotStateReuse();
            OwnerDocument.RecordComputedStyleSnapshot(cacheHit: true);
            return _computedStyleSnapshot;
        }

        var started = Stopwatch.GetTimestamp();
        var allocationStarted = GC.GetAllocatedBytesForCurrentThread();
        _computedStyleSnapshot = OwnerDocument.ComputedStyleSnapshotStateReuseEnabled
            ? ComputedStyleBuilder.BuildLazy(this)
            : ComputedStyleBuilder.Build(this);
        OwnerDocument.RecordComputedStyleSnapshotBuild(started, allocationStarted);
        _computedStyleSnapshotGeneration = generation;
        _computedStyleSnapshotState = state;
        OwnerDocument.RecordComputedStyleSnapshot(cacheHit: false);
        return _computedStyleSnapshot;
    }

    private readonly record struct ComputedStyleSnapshotState(
        bool IsConnected,
        CssComputedValues ComputedValues,
        CssDeclaredPropertySet DeclaredProperties,
        Rect Bounds,
        double MinWidth,
        double MinHeight,
        double MaxWidth,
        double MaxHeight,
        Thickness Margin,
        Thickness Padding,
        Thickness BorderThickness,
        string BorderBrush,
        CornerRadius CornerRadius,
        string Background,
        string Foreground,
        string FontFamily,
        double FontSize,
        FontWeight FontWeight,
        FontStyle FontStyle,
        double Opacity,
        bool IsVisible,
        bool HasActiveTransformTransition,
        Matrix? RenderTransform,
        CssDisplay Display,
        string OverflowX,
        string OverflowY);

    private sealed class ComputedStyleBuilder
    {
        private readonly AvaloniaDomElement _element;
        private readonly Control _control;
        private readonly Dictionary<string, string> _values = new(CssPropertyNameComparer.Instance);

        private ComputedStyleBuilder(AvaloniaDomElement element)
        {
            _element = element;
            _control = element.Control;
        }

        public static CssComputedStyle Build(AvaloniaDomElement element)
        {
            var builder = new ComputedStyleBuilder(element);
            builder.Populate();
            return new CssComputedStyle(builder._values);
        }

        public static CssComputedStyle BuildLazy(AvaloniaDomElement element)
        {
            var builder = new ComputedStyleBuilder(element);
            return new CssComputedStyle(
                builder.ResolveProperty,
                builder.MaterializeValues,
                IsLivePresentationProperty);
        }

        private static bool IsLivePresentationProperty(string propertyName)
            => propertyName is "transform" or "opacity" or "color";

        private Dictionary<string, string> MaterializeValues()
        {
            Populate();
            return _values;
        }

        private string ResolveProperty(string propertyName)
        {
            // Chrome exposes an empty resolved declaration for elements that do
            // not participate in a document tree. Libraries then fall back to
            // the element's authored inline declaration. Returning retained-host
            // defaults such as 0px/static here masks those authored values and
            // breaks detached construction, cloning, and show/hide preparation.
            if (!_element.OwnerDocument.IsConnectedStyleElement(_element))
            {
                return string.Empty;
            }

            var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
            if (TryResolveConnectedUsedValue(normalized, out var usedValue))
            {
                return usedValue;
            }
            if (normalized == "opacity" && _element._cssOpacityTransition is not null)
            {
                return (_element._cssPresentedOpacity ?? _control.Opacity)
                    .ToString("0.######", CultureInfo.InvariantCulture);
            }
            if (normalized == "color"
                && _element._cssColorTransition is not null
                && _element._cssPresentedColor is { } animatedColor)
            {
                return CssColor(animatedColor);
            }
            if (_element.DeclaredStyleProperties.Contains(normalized)
                && _element.ComputedStyleValues.TryGetValue(normalized, out var declaredValue)
                && !(declaredValue is "auto" or "none"
                     && normalized is "width" or "height" or "min-width" or "min-height" or "max-width" or "max-height"))
            {
                return normalized switch
                {
                    "font-weight" => SerializeComputedFontWeight(declaredValue, FontWeight.Normal),
                    "transform" => SerializeComputedTransform(),
                    "transform-origin" => SerializeComputedTransformOrigin(),
                    "color" or "background-color" or "outline-color" => SerializeColor(declaredValue),
                    _ when normalized.StartsWith("border-", StringComparison.Ordinal)
                           && normalized.EndsWith("-color", StringComparison.Ordinal)
                        => SerializeColor(declaredValue),
                    _ => declaredValue
                };
            }

            var padding = ResolveThickness("Padding", default);
            var borderThickness = ResolveThickness(
                "BorderThickness",
                (_control as Border)?.BorderThickness ?? default);
            var borderBrush = ResolveBrush("BorderBrush", (_control as Border)?.BorderBrush);
            var cornerRadius = ResolveCornerRadius(
                "CornerRadius",
                (_control as Border)?.CornerRadius ?? default);
            return normalized switch
            {
                "list-style-type" => _element.ComputedStyleValues.GetValueOrDefault("list-style-type") ?? "disc",
                "list-style-position" => _element.ComputedStyleValues.GetValueOrDefault("list-style-position") ?? "outside",
                "box-sizing" => _element.ComputedStyleValues.GetValueOrDefault("box-sizing") ?? "content-box",
                "position" => "static",
                "width" => FormatDimension(_control.Bounds.Width),
                "height" => FormatDimension(_control.Bounds.Height),
                "min-width" => FormatMinDimension(_control.MinWidth),
                "min-height" => FormatMinDimension(_control.MinHeight),
                "max-width" => FormatMaxDimension(_control.MaxWidth),
                "max-height" => FormatMaxDimension(_control.MaxHeight),
                "margin-top" => FormatFixedLength(_control.Margin.Top),
                "margin-right" => FormatFixedLength(_control.Margin.Right),
                "margin-bottom" => FormatFixedLength(_control.Margin.Bottom),
                "margin-left" => FormatFixedLength(_control.Margin.Left),
                "margin" => FormatThickness(_control.Margin),
                "padding-top" => FormatFixedLength(padding.Top),
                "padding-right" => FormatFixedLength(padding.Right),
                "padding-bottom" => FormatFixedLength(padding.Bottom),
                "padding-left" => FormatFixedLength(padding.Left),
                "padding" => FormatThickness(padding),
                "border-top-width" => FormatFixedLength(borderThickness.Top),
                "border-right-width" => FormatFixedLength(borderThickness.Right),
                "border-bottom-width" => FormatFixedLength(borderThickness.Bottom),
                "border-left-width" => FormatFixedLength(borderThickness.Left),
                "border-width" => FormatThickness(borderThickness),
                "border-top-style" => BorderStyle(borderThickness.Top),
                "border-right-style" => BorderStyle(borderThickness.Right),
                "border-bottom-style" => BorderStyle(borderThickness.Bottom),
                "border-left-style" => BorderStyle(borderThickness.Left),
                "border-style" => string.Join(" ", new[]
                {
                    BorderStyle(borderThickness.Top),
                    BorderStyle(borderThickness.Right),
                    BorderStyle(borderThickness.Bottom),
                    BorderStyle(borderThickness.Left)
                }),
                "border-top-color" or "border-right-color" or "border-bottom-color" or "border-left-color"
                    => FormatColor(borderBrush),
                "border-color" => RepeatFour(FormatColor(borderBrush)),
                "border-top-left-radius" => FormatFixedLength(cornerRadius.TopLeft),
                "border-top-right-radius" => FormatFixedLength(cornerRadius.TopRight),
                "border-bottom-right-radius" => FormatFixedLength(cornerRadius.BottomRight),
                "border-bottom-left-radius" => FormatFixedLength(cornerRadius.BottomLeft),
                "border-radius" => string.Join(" ", new[]
                {
                    FormatFixedLength(cornerRadius.TopLeft),
                    FormatFixedLength(cornerRadius.TopRight),
                    FormatFixedLength(cornerRadius.BottomRight),
                    FormatFixedLength(cornerRadius.BottomLeft)
                }),
                "background-color" => FormatColor(ResolveBrush("Background", null)),
                "background-image" => "none",
                "color" => ResolveForeground(),
                "font-family" => ResolveFontFamily(),
                "font-size" => ResolveFontSize(),
                "font-weight" => ResolveFontWeight(),
                "font-style" => ResolveFontStyle(),
                "opacity" => FormatNumber(_control.Opacity),
                "transform" => SerializeComputedTransform(),
                "transform-origin" => SerializeComputedTransformOrigin(),
                "visibility" => _control.IsVisible ? "visible" : "hidden",
                "direction" => _element.ComputedStyleValues.GetValueOrDefault("direction") ?? "ltr",
                "display" => ResolveDisplay(),
                "overflow-x" => ResolveOverflow().X,
                "overflow-y" => ResolveOverflow().Y,
                "overflow" => ResolveOverflowShorthand(),
                "grid-area" or "grid-row" or "grid-row-start" or "grid-row-end"
                    or "grid-column" or "grid-column-start" or "grid-column-end"
                    => _element.ComputedStyleValues.GetValueOrDefault(normalized) ?? "auto",
                "pointer-events" => "auto",
                "line-height" => "normal",
                _ => string.Empty
            };
        }

        private static string FormatThickness(Thickness thickness)
            => string.Join(" ", new[]
            {
                FormatFixedLength(thickness.Top),
                FormatFixedLength(thickness.Right),
                FormatFixedLength(thickness.Bottom),
                FormatFixedLength(thickness.Left)
            });

        private static string RepeatFour(string value)
            => string.Join(" ", new[] { value, value, value, value });

        private static string BorderStyle(double width) => width > 0 ? "solid" : "none";

        private string ResolveForeground()
        {
            var brush = ResolveBrush("Foreground", null);
            return brush is null ? "rgba(0, 0, 0, 1)" : FormatColor(brush);
        }

        private string ResolveFontFamily()
        {
            TryGetPropertyValue<FontFamily>("FontFamily", out var family);
            return _element.ComputedStyleValues.GetValueOrDefault("font-family")
                   ?? family?.ToString()
                   ?? "sans-serif";
        }

        private string ResolveFontSize()
        {
            if (!TryGetPropertyValue<double>("FontSize", out var size) || size <= 0) size = 16d;
            return _element.ComputedStyleValues.GetValueOrDefault("font-size") ?? FormatFixedLength(size);
        }

        private string ResolveFontWeight()
        {
            if (!TryGetPropertyValue<FontWeight>("FontWeight", out var weight)) weight = FontWeight.Normal;
            return SerializeComputedFontWeight(
                _element.ComputedStyleValues.GetValueOrDefault("font-weight"),
                weight);
        }

        private string ResolveFontStyle()
        {
            if (!TryGetPropertyValue<FontStyle>("FontStyle", out var style)) style = FontStyle.Normal;
            return _element.ComputedStyleValues.GetValueOrDefault("font-style")
                   ?? style.ToString().ToLowerInvariant();
        }

        private string ResolveDisplay()
            => CssLayout.GetDisplay(_control) switch
            {
                CssDisplay.Inline => "inline",
                CssDisplay.InlineBlock => "inline-block",
                CssDisplay.Flex => "flex",
                CssDisplay.Grid => "grid",
                CssDisplay.InlineFlex => "inline-flex",
                CssDisplay.InlineGrid => "inline-grid",
                CssDisplay.Table => "table",
                CssDisplay.InlineTable => "inline-table",
                CssDisplay.TableRowGroup => "table-row-group",
                CssDisplay.TableHeaderGroup => "table-header-group",
                CssDisplay.TableFooterGroup => "table-footer-group",
                CssDisplay.TableRow => "table-row",
                CssDisplay.TableCell => "table-cell",
                CssDisplay.TableColumnGroup => "table-column-group",
                CssDisplay.TableColumn => "table-column",
                CssDisplay.TableCaption => "table-caption",
                _ => "block"
            };

        private string ResolveOverflowShorthand()
        {
            var (x, y) = ResolveOverflow();
            return x == y ? x : $"{x} {y}";
        }

        public static ComputedStyleSnapshotState CaptureState(AvaloniaDomElement element)
        {
            var builder = new ComputedStyleBuilder(element);
            var control = element.Control;
            var padding = builder.ResolveThickness("Padding", default);
            var borderThickness = builder.ResolveThickness(
                "BorderThickness",
                (control as Border)?.BorderThickness ?? default);
            var borderBrush = builder.ResolveBrush(
                "BorderBrush",
                (control as Border)?.BorderBrush);
            var cornerRadius = builder.ResolveCornerRadius(
                "CornerRadius",
                (control as Border)?.CornerRadius ?? default);
            var background = builder.ResolveBrush("Background", null);
            var foreground = builder.ResolveBrush("Foreground", null);
            builder.TryGetPropertyValue<FontFamily>("FontFamily", out var fontFamily);
            if (!builder.TryGetPropertyValue<double>("FontSize", out var fontSize)
                || fontSize <= 0)
            {
                fontSize = 16d;
            }
            if (!builder.TryGetPropertyValue<FontWeight>("FontWeight", out var fontWeight))
            {
                fontWeight = FontWeight.Normal;
            }
            if (!builder.TryGetPropertyValue<FontStyle>("FontStyle", out var fontStyle))
            {
                fontStyle = FontStyle.Normal;
            }
            var (overflowX, overflowY) = builder.ResolveOverflow();
            return new ComputedStyleSnapshotState(
                element.OwnerDocument.IsConnectedStyleElement(element),
                element._computedStyleValues,
                element._declaredStyleProperties,
                control.Bounds,
                control.MinWidth,
                control.MinHeight,
                control.MaxWidth,
                control.MaxHeight,
                control.Margin,
                padding,
                borderThickness,
                FormatColor(borderBrush),
                cornerRadius,
                FormatColor(background),
                element._cssColorTransition is not null && element._cssPresentedColor is { } animated
                    ? CssColor(animated)
                    : foreground is null ? string.Empty : FormatColor(foreground),
                fontFamily?.ToString() ?? string.Empty,
                fontSize,
                fontWeight,
                fontStyle,
                element._cssOpacityTransition is not null
                    ? element._cssPresentedOpacity ?? control.Opacity
                    : control.Opacity,
                control.IsVisible,
                element._cssRotateTransition is not null || element._cssMatrixTransition is not null,
                element._cssRotateTransition is not null || element._cssMatrixTransition is not null
                    ? null
                    : control.RenderTransform?.Value,
                CssLayout.GetDisplay(control),
                overflowX,
                overflowY);
        }

        private void Populate()
        {
            if (!_element.OwnerDocument.IsConnectedStyleElement(_element))
            {
                return;
            }

            AddValue("box-sizing", _element.ComputedStyleValues.TryGetValue("box-sizing", out var boxSizing)
                ? boxSizing
                : "content-box");
            AddValue("position", "static");

            AddDimension("width", _control.Bounds.Width);
            AddDimension("height", _control.Bounds.Height);
            AddMinDimension("min-width", _control.MinWidth);
            AddMinDimension("min-height", _control.MinHeight);
            AddMaxDimension("max-width", _control.MaxWidth);
            AddMaxDimension("max-height", _control.MaxHeight);

            AddThicknessSet("margin", _control.Margin);

            var padding = ResolveThickness("Padding", default);
            AddThicknessSet("padding", padding);

            var borderThickness = ResolveThickness("BorderThickness", (_control as Border)?.BorderThickness ?? default);
            AddBorderThickness(borderThickness);
            AddBorderStyles(borderThickness);

            var borderBrush = ResolveBrush("BorderBrush", (_control as Border)?.BorderBrush);
            AddBorderColors(borderBrush);

            var cornerRadius = ResolveCornerRadius("CornerRadius", (_control as Border)?.CornerRadius ?? default);
            AddCornerRadius(cornerRadius);

            var background = ResolveBrush("Background", null);
            AddBackground(background);

            var foreground = ResolveBrush("Foreground", null);
            if (_element._cssColorTransition is not null
                && _element._cssPresentedColor is { } animatedColor)
            {
                AddValue("color", CssColor(animatedColor));
            }
            else
            {
                AddForeground(foreground);
            }

            AddFontProperties();
            if (_element._cssOpacityTransition is not null)
            {
                AddValue("opacity", FormatNumber(
                    _element._cssPresentedOpacity ?? _control.Opacity));
            }
            else
            {
                AddOpacity();
            }
            AddValue("transform", SerializeComputedTransform());
            AddValue("transform-origin", SerializeComputedTransformOrigin());
            AddVisibility();
            AddDisplay();
            AddOverflow();
            AddValue("grid-area", _element.ComputedStyleValues.GetValueOrDefault("grid-area") ?? "auto");
            AddValue("grid-row", _element.ComputedStyleValues.GetValueOrDefault("grid-row") ?? "auto");
            AddValue("grid-row-start", _element.ComputedStyleValues.GetValueOrDefault("grid-row-start") ?? "auto");
            AddValue("grid-row-end", _element.ComputedStyleValues.GetValueOrDefault("grid-row-end") ?? "auto");
            AddValue("grid-column", _element.ComputedStyleValues.GetValueOrDefault("grid-column") ?? "auto");
            AddValue(
                "grid-column-start",
                _element.ComputedStyleValues.GetValueOrDefault("grid-column-start") ?? "auto");
            AddValue(
                "grid-column-end",
                _element.ComputedStyleValues.GetValueOrDefault("grid-column-end") ?? "auto");

            AddValue("pointer-events", "auto");
            AddValue("line-height", "normal");
            AddValue(
                "list-style-type",
                _element.ComputedStyleValues.GetValueOrDefault("list-style-type") ?? "disc");
            AddValue(
                "list-style-position",
                _element.ComputedStyleValues.GetValueOrDefault("list-style-position") ?? "outside");

            foreach (var pair in _element.ComputedStyleValues)
            {
                var normalized = CssStyleDeclaration.NormalizePropertyName(pair.Key);
                if (!_element.DeclaredStyleProperties.Contains(normalized))
                {
                    continue;
                }
                if (pair.Value is "auto" or "none"
                    && normalized is "width" or "height" or "min-width" or "min-height" or "max-width" or "max-height")
                {
                    continue;
                }

                _values[normalized] = normalized switch
                {
                    "font-weight" => SerializeComputedFontWeight(pair.Value, FontWeight.Normal),
                    "transform" => SerializeComputedTransform(),
                    "transform-origin" => SerializeComputedTransformOrigin(),
                    "color" or "background-color" => SerializeColor(pair.Value),
                    _ when normalized.StartsWith("border-", StringComparison.Ordinal)
                           && normalized.EndsWith("-color", StringComparison.Ordinal)
                        => SerializeColor(pair.Value),
                    _ => pair.Value
                };
            }
        }

        private void AddValue(string propertyName, string value)
            => _values[propertyName] = value;

        private void AddDimension(string propertyName, double value)
            => _values[propertyName] = FormatDimension(value);

        private void AddMinDimension(string propertyName, double value)
            => _values[propertyName] = FormatMinDimension(value);

        private void AddMaxDimension(string propertyName, double value)
            => _values[propertyName] = FormatMaxDimension(value);

        private void AddThicknessSet(string prefix, Thickness thickness)
        {
            var top = FormatFixedLength(thickness.Top);
            var right = FormatFixedLength(thickness.Right);
            var bottom = FormatFixedLength(thickness.Bottom);
            var left = FormatFixedLength(thickness.Left);

            _values[$"{prefix}-top"] = top;
            _values[$"{prefix}-right"] = right;
            _values[$"{prefix}-bottom"] = bottom;
            _values[$"{prefix}-left"] = left;
            _values[prefix] = string.Join(" ", new[] { top, right, bottom, left });
        }

        private void AddBorderThickness(Thickness thickness)
        {
            var top = FormatFixedLength(thickness.Top);
            var right = FormatFixedLength(thickness.Right);
            var bottom = FormatFixedLength(thickness.Bottom);
            var left = FormatFixedLength(thickness.Left);

            _values["border-top-width"] = top;
            _values["border-right-width"] = right;
            _values["border-bottom-width"] = bottom;
            _values["border-left-width"] = left;
            _values["border-width"] = string.Join(" ", new[] { top, right, bottom, left });
        }

        private void AddBorderStyles(Thickness thickness)
        {
            var top = thickness.Top > 0 ? "solid" : "none";
            var right = thickness.Right > 0 ? "solid" : "none";
            var bottom = thickness.Bottom > 0 ? "solid" : "none";
            var left = thickness.Left > 0 ? "solid" : "none";

            _values["border-top-style"] = top;
            _values["border-right-style"] = right;
            _values["border-bottom-style"] = bottom;
            _values["border-left-style"] = left;
            _values["border-style"] = string.Join(" ", new[] { top, right, bottom, left });
        }

        private void AddBorderColors(IBrush? brush)
        {
            var color = FormatColor(brush);
            _values["border-top-color"] = color;
            _values["border-right-color"] = color;
            _values["border-bottom-color"] = color;
            _values["border-left-color"] = color;
            _values["border-color"] = string.Join(" ", new[] { color, color, color, color });
        }

        private void AddCornerRadius(CornerRadius cornerRadius)
        {
            var topLeft = FormatFixedLength(cornerRadius.TopLeft);
            var topRight = FormatFixedLength(cornerRadius.TopRight);
            var bottomRight = FormatFixedLength(cornerRadius.BottomRight);
            var bottomLeft = FormatFixedLength(cornerRadius.BottomLeft);

            _values["border-top-left-radius"] = topLeft;
            _values["border-top-right-radius"] = topRight;
            _values["border-bottom-right-radius"] = bottomRight;
            _values["border-bottom-left-radius"] = bottomLeft;
            _values["border-radius"] = string.Join(" ", new[] { topLeft, topRight, bottomRight, bottomLeft });
        }

        private void AddBackground(IBrush? brush)
        {
            _values["background-color"] = FormatColor(brush);
            _values["background-image"] = "none";
        }

        private void AddForeground(IBrush? brush)
        {
            var color = brush is null ? "rgba(0, 0, 0, 1)" : FormatColor(brush);
            _values["color"] = color;
        }

        private void AddFontProperties()
        {
            TryGetPropertyValue<FontFamily>("FontFamily", out var fontFamily);
            var family = _element.ComputedStyleValues.GetValueOrDefault("font-family")
                         ?? fontFamily?.ToString()
                         ?? "sans-serif";

            if (!TryGetPropertyValue<double>("FontSize", out var fontSize) || fontSize <= 0)
            {
                fontSize = 16d;
            }

            if (!TryGetPropertyValue<FontWeight>("FontWeight", out var fontWeight))
            {
                fontWeight = FontWeight.Normal;
            }

            if (!TryGetPropertyValue<FontStyle>("FontStyle", out var fontStyle))
            {
                fontStyle = FontStyle.Normal;
            }

            AddValue("font-family", family);
            AddValue(
                "font-size",
                _element.ComputedStyleValues.GetValueOrDefault("font-size")
                ?? FormatFixedLength(fontSize));
            AddValue(
                "font-weight",
                SerializeComputedFontWeight(
                    _element.ComputedStyleValues.GetValueOrDefault("font-weight"),
                    fontWeight));
            AddValue(
                "font-style",
                _element.ComputedStyleValues.GetValueOrDefault("font-style")
                ?? fontStyle.ToString().ToLowerInvariant());
        }

        private void AddOpacity()
            => AddValue("opacity", FormatNumber(_control.Opacity));

        private void AddVisibility()
        {
            var visible = _control.IsVisible ? "visible" : "hidden";
            AddValue("visibility", visible);
        }

        private void AddDisplay()
        {
            var display = CssLayout.GetDisplay(_control) switch
            {
                CssDisplay.Inline => "inline",
                CssDisplay.InlineBlock => "inline-block",
                CssDisplay.Flex => "flex",
                CssDisplay.Grid => "grid",
                CssDisplay.InlineFlex => "inline-flex",
                CssDisplay.InlineGrid => "inline-grid",
                CssDisplay.Table => "table",
                CssDisplay.InlineTable => "inline-table",
                CssDisplay.TableRowGroup => "table-row-group",
                CssDisplay.TableHeaderGroup => "table-header-group",
                CssDisplay.TableFooterGroup => "table-footer-group",
                CssDisplay.TableRow => "table-row",
                CssDisplay.TableCell => "table-cell",
                CssDisplay.TableColumnGroup => "table-column-group",
                CssDisplay.TableColumn => "table-column",
                CssDisplay.TableCaption => "table-caption",
                CssDisplay.ListItem => "list-item",
                _ => "block"
            };

            AddValue("display", display);
        }

        private void AddOverflow()
        {
            var (overflowX, overflowY) = ResolveOverflow();

            AddValue("overflow-x", overflowX);
            AddValue("overflow-y", overflowY);
            AddValue("overflow", overflowX == overflowY ? overflowX : $"{overflowX} {overflowY}");
        }

        private (string X, string Y) ResolveOverflow()
        {
            if (_control is ScrollViewer viewer)
            {
                return (
                    MapOverflow(viewer.HorizontalScrollBarVisibility),
                    MapOverflow(viewer.VerticalScrollBarVisibility));
            }

            if (_control is CssLayoutPanel panel)
            {
                return (panel.OverflowX, panel.OverflowY);
            }

            return ("visible", "visible");
        }

        private Thickness ResolveThickness(string propertyName, Thickness fallback)
        {
            if (TryGetPropertyValue(propertyName, out Thickness thickness))
            {
                return thickness;
            }

            return fallback;
        }

        private CornerRadius ResolveCornerRadius(string propertyName, CornerRadius fallback)
        {
            if (TryGetPropertyValue(propertyName, out CornerRadius radius))
            {
                return radius;
            }

            return fallback;
        }

        private IBrush? ResolveBrush(string propertyName, IBrush? fallback)
        {
            if (TryGetPropertyValue(propertyName, out IBrush brush) && brush is not null)
            {
                return brush;
            }

            return fallback;
        }

        private bool TryGetPropertyValue<T>(string propertyName, out T value)
        {
            var property = FindAvaloniaProperty(_control.GetType(), propertyName);
            if (property is null)
            {
                value = default!;
                return false;
            }

            var raw = _control.GetValue(property);
            if (raw is T typed)
            {
                value = typed;
                return true;
            }

            if (raw is null)
            {
                value = default!;
                return false;
            }

            try
            {
                value = (T)raw;
                return true;
            }
            catch
            {
                value = default!;
                return false;
            }
        }

        private static string FormatDimension(double value)
        {
            if (!double.IsFinite(value))
            {
                return "auto";
            }

            return FormatFixedLength(value);
        }

        private bool TryResolveConnectedUsedValue(string propertyName, out string value)
        {
            value = string.Empty;
            if (_control.Parent is not Control parent)
            {
                return false;
            }

            var parentBorder = parent switch
            {
                CssLayoutPanel panel => panel.BorderThickness,
                Border border => border.BorderThickness,
                TemplatedControl templated => templated.BorderThickness,
                _ => default
            };
            var paddingBoxWidth = Math.Max(0, parent.Bounds.Width - parentBorder.Left - parentBorder.Right);
            var paddingBoxHeight = Math.Max(0, parent.Bounds.Height - parentBorder.Top - parentBorder.Bottom);
            var paddingLeft = CssLayout.Resolve(CssLayout.GetPaddingLeft(parent), paddingBoxWidth) ?? 0;
            var paddingRight = CssLayout.Resolve(CssLayout.GetPaddingRight(parent), paddingBoxWidth) ?? 0;
            var paddingTop = CssLayout.Resolve(CssLayout.GetPaddingTop(parent), paddingBoxHeight) ?? 0;
            var paddingBottom = CssLayout.Resolve(CssLayout.GetPaddingBottom(parent), paddingBoxHeight) ?? 0;
            var contentWidth = Math.Max(0, paddingBoxWidth - paddingLeft - paddingRight);

            CssLength? length = propertyName switch
            {
                "left" or "inset-inline-start" => CssLayout.GetLeft(_control),
                "top" or "inset-block-start" => CssLayout.GetTop(_control),
                "right" or "inset-inline-end" => CssLayout.GetRight(_control),
                "bottom" or "inset-block-end" => CssLayout.GetBottom(_control),
                "margin-left" => CssLayout.GetMarginLeft(_control),
                "margin-top" => CssLayout.GetMarginTop(_control),
                "margin-right" => CssLayout.GetMarginRight(_control),
                "margin-bottom" => CssLayout.GetMarginBottom(_control),
                _ => null
            };
            if (!length.HasValue)
            {
                return false;
            }

            if (propertyName.StartsWith("margin-", StringComparison.Ordinal))
            {
                var left = CssLayout.GetMarginLeft(_control);
                var right = CssLayout.GetMarginRight(_control);
                var leftIsAuto = left is { IsAuto: true };
                var rightIsAuto = right is { IsAuto: true };
                if (length.Value.IsAuto && propertyName is "margin-left" or "margin-right")
                {
                    var resolvedLeft = leftIsAuto ? 0 : CssLayout.Resolve(left, contentWidth) ?? 0;
                    var resolvedRight = rightIsAuto ? 0 : CssLayout.Resolve(right, contentWidth) ?? 0;
                    var remaining = Math.Max(
                        0,
                        contentWidth - _control.Bounds.Width - resolvedLeft - resolvedRight);
                    var resolved = leftIsAuto && rightIsAuto ? remaining / 2 : remaining;
                    value = FormatFixedLength(resolved);
                    return true;
                }

                var margin = CssLayout.Resolve(length, contentWidth);
                if (!margin.HasValue)
                {
                    return false;
                }
                value = FormatFixedLength(margin.Value);
                return true;
            }

            var horizontal = propertyName is "left" or "right" or "inset-inline-start" or "inset-inline-end";
            var inset = CssLayout.Resolve(length, horizontal ? paddingBoxWidth : paddingBoxHeight);
            if (!inset.HasValue)
            {
                return false;
            }
            value = FormatFixedLength(inset.Value);
            return true;
        }

        private static string FormatMinDimension(double value)
        {
            if (!double.IsFinite(value))
            {
                return "0px";
            }

            return FormatFixedLength(value);
        }

        private static string FormatMaxDimension(double value)
        {
            if (!double.IsFinite(value) || value == double.PositiveInfinity)
            {
                return "none";
            }

            return FormatFixedLength(value);
        }

        private static string FormatFixedLength(double value)
        {
            var normalized = Normalize(value);
            return $"{normalized.ToString("0.##", CultureInfo.InvariantCulture)}px";
        }

        private static double Normalize(double value)
        {
            if (!double.IsFinite(value))
            {
                return 0;
            }

            return Math.Abs(value) < 0.0005 ? 0 : value;
        }

        private static string FormatFontWeight(FontWeight fontWeight)
            => ((int)fontWeight).ToString(CultureInfo.InvariantCulture);

        private static string SerializeComputedFontWeight(string? computed, FontWeight fallback)
            => computed?.Trim().ToLowerInvariant() switch
            {
                "normal" => "400",
                "bold" or "bolder" => "700",
                "lighter" => "300",
                { Length: > 0 } numeric => numeric,
                _ => FormatFontWeight(fallback)
            };

        private static string FormatNumber(double value)
        {
            if (!double.IsFinite(value))
            {
                return "1";
            }

            var normalized = Normalize(value);
            return normalized.ToString("0.###", CultureInfo.InvariantCulture);
        }

        private string SerializeComputedTransform()
        {
            if (_element.GetComputedTransformMatrix() is not { } matrix)
            {
                return "none";
            }

            return $"matrix({FormatMatrixNumber(matrix.M11)}, {FormatMatrixNumber(matrix.M12)}, " +
                   $"{FormatMatrixNumber(matrix.M21)}, {FormatMatrixNumber(matrix.M22)}, " +
                   $"{FormatMatrixNumber(matrix.M31)}, {FormatMatrixNumber(matrix.M32)})";
        }

        private string SerializeComputedTransformOrigin()
        {
            var origin = _control.RenderTransformOrigin;
            var x = origin.Unit == RelativeUnit.Relative
                ? origin.Point.X * _control.Bounds.Width
                : origin.Point.X;
            var y = origin.Unit == RelativeUnit.Relative
                ? origin.Point.Y * _control.Bounds.Height
                : origin.Point.Y;
            return $"{FormatFixedLength(x)} {FormatFixedLength(y)}";
        }

        private static string FormatMatrixNumber(double value)
        {
            // Avalonia's degree transform can retain float-sized trigonometric
            // residue at exact quarter turns (for example cos(90deg)). CSSOM
            // number serialization should not expose that implementation noise.
            if (!double.IsFinite(value) || Math.Abs(value) < 1e-7)
            {
                return "0";
            }

            var integer = Math.Round(value);
            if (Math.Abs(value - integer) < 1e-7)
            {
                value = integer;
            }

            return value.ToString("G15", CultureInfo.InvariantCulture).ToLowerInvariant();
        }

        private static string FormatColor(IBrush? brush)
        {
            if (brush is ISolidColorBrush solid)
            {
                var color = solid.Color;
                if (color.A == byte.MaxValue)
                {
                    return $"rgb({color.R}, {color.G}, {color.B})";
                }
                var alpha = Math.Round(color.A / 255d, 3);
                return $"rgba({color.R}, {color.G}, {color.B}, {alpha.ToString(CultureInfo.InvariantCulture)})";
            }

            return brush?.ToString() ?? "rgba(0, 0, 0, 0)";
        }

        private string SerializeColor(string value)
            => _element.TryParseBrush(value, out var brush) ? FormatColor(brush) : value;

        private static string MapOverflow(ScrollBarVisibility visibility)
            => visibility switch
            {
                ScrollBarVisibility.Auto => "auto",
                ScrollBarVisibility.Hidden => "hidden",
                ScrollBarVisibility.Disabled => "hidden",
                ScrollBarVisibility.Visible => "scroll",
                _ => "auto"
            };
    }

    internal IEnumerable<AvaloniaDomElement> GetChildElements()
    {
        if (TryGetControlsCollection(Control, out var controls))
        {
            foreach (var child in controls.OfType<Control>())
            {
                if (AvaloniaDomDocument.IsDomInfrastructureControl(child)) continue;
                yield return OwnerDocument.WrapControl(child);
            }
            yield break;
        }

        if (Control is ContentControl cc && cc.Content is Control content)
        {
            yield return OwnerDocument.WrapControl(content);
        }
        else if (Control is Decorator decorator && decorator.Child is Control child)
        {
            yield return OwnerDocument.WrapControl(child);
        }
    }

    private AvaloniaDomElement? GetSibling(int offset)
    {
        if (Control.Parent is Panel panel)
        {
            var list = panel.Children.OfType<Control>()
                .Where(control => !AvaloniaDomDocument.IsDomInfrastructureControl(control))
                .ToList();
            var index = list.IndexOf(Control);
            if (index >= 0)
            {
                var target = index + offset;
                if (target >= 0 && target < list.Count)
                {
                    return OwnerDocument.WrapControl(list[target]);
                }
            }
        }

        return null;
    }

    private AvaloniaDomElement? GetElementSibling(int direction)
    {
        if (direction == 0 || Control.Parent is not Panel panel)
        {
            return null;
        }

        var siblings = panel.Children
            .OfType<Control>()
            .Where(control => !AvaloniaDomDocument.IsDomInfrastructureControl(control))
            .ToList();
        var index = siblings.IndexOf(Control);
        if (index < 0)
        {
            return null;
        }

        for (var target = index + Math.Sign(direction);
             target >= 0 && target < siblings.Count;
             target += Math.Sign(direction))
        {
            var sibling = OwnerDocument.WrapControl(siblings[target]);
            if (sibling.nodeType == 1)
            {
                return sibling;
            }
        }

        return null;
    }

    private AvaloniaDomElement? InsertChild(AvaloniaDomElement? child, AvaloniaDomElement? reference, bool placeBefore)
    {
        if (child is null)
        {
            return null;
        }

        ThrowIfHierarchyCycle(child);

        if (TryGetControlsCollection(Control, out var list))
        {
            var collectInsertion = OwnerDocument.CollectPerformanceMetrics;
            var insertionStarted = collectInsertion ? Stopwatch.GetTimestamp() : 0;
            var insertionAllocationStarted = collectInsertion ? GC.GetAllocatedBytesForCurrentThread() : 0;
            void RecordInsertion()
            {
                if (!collectInsertion)
                {
                    return;
                }
                OwnerDocument.RecordDomNodeInsertion(insertionStarted, insertionAllocationStarted);
            }

            if (reference is not null)
            {
                if (list.IndexOf(reference.Control) < 0)
                {
                    throw new InvalidOperationException(
                        "insertBefore reference is not a child of this node.");
                }

                if (ReferenceEquals(child.Control, reference.Control))
                {
                    RecordInsertion();
                    return child;
                }
            }

            // Validate the reference and handle the identity no-op before
            // detaching. A rejected insertion must leave both trees intact.
            DetachFromCurrentParentWithNotification(child);

            if (OwnerDocument.IsConnectedStyleElement(this))
            {
                child.SuppressPaintUntilFirstStyleCommit();
            }

            if (reference is null)
            {
                list.Add(child.Control);
            }
            else
            {
                var index = list.IndexOf(reference.Control);
                if (index < 0)
                {
                    RecordInsertion();
                    return null;
                }

                var insertIndex = placeBefore ? index : Math.Min(index + 1, list.Count);
                list.Insert(insertIndex, child.Control);
            }

            // Styles assigned while disconnected need their real containing
            // block before percentage lengths and insets can be resolved.
            // Class-only/unstyled React nodes have no inline presentation to
            // apply here; running the complete positioning bridge for each of
            // One-character highlight spans can dominate filtering.
            // Their stylesheet presentation is applied by the coalesced pass
            // scheduled immediately below.
            if (child._styleValues.Count > 0)
            {
                child.ApplyInlinePresentation();
            }
            if (child is AvaloniaDomTextNode
                && child.Control is TextBlock textBlock
                && string.IsNullOrWhiteSpace(textBlock.Text)
                && localName is "span" or "a" or "label" or "strong" or "em" or "code")
            {
                // Collapsed inline whitespace still occupies one word-space.
                // Detached formatting whitespace under block containers stays
                // suppressed by CreateTextNodeControl.
                textBlock.IsVisible = true;
                if (double.IsNaN(textBlock.Width))
                {
                    textBlock.Width = Math.Max(3, textBlock.FontSize * 0.25);
                }
                CssLayout.SetWidth(textBlock, new CssLength(textBlock.Width, CssLengthUnit.Pixel));
                Control.MinWidth = Math.Max(Control.MinWidth, textBlock.Width);
            }

            // Building a disconnected React branch cannot affect document CSS,
            // layout, indexes, or an absent MutationObserver. Defer all of that
            // bookkeeping until the branch is attached to a connected parent.
            // This is especially important for highlighted search results,
            // which assemble hundreds of small nested spans per keystroke.
            if (OwnerDocument.RequiresChildListMutationNotification(this, child))
            {
                var (previousSibling, nextSibling) = child.GetSiblingSnapshot(Control, child.Control);
                // Notify after the element has its real parent. The scheduled style/layout
                // pass applies pre-existing styles using the correct containing block and
                // coalesces a batch of DOM insertions into one cascade computation.
                OwnerDocument.NotifyChildListMutation(this, new[] { child }, null, previousSibling, nextSibling);
            }
            InvalidateSvgRenderer();
            TriggerIframeNavigationIfNeeded(child);
            RecordInsertion();
            return child;
        }

        if (Control is ContentControl cc)
        {
            if (reference is not null && !ReferenceEquals(cc.Content, reference.Control))
            {
                throw new InvalidOperationException(
                    "insertBefore reference is not a child of this node.");
            }

            if (reference is not null && ReferenceEquals(child.Control, reference.Control))
            {
                return child;
            }

            DetachFromCurrentParentWithNotification(child);

            if (OwnerDocument.IsConnectedStyleElement(this))
            {
                child.SuppressPaintUntilFirstStyleCommit();
            }

            AvaloniaDomElement? removedElement = null;
            if (cc.Content is Control existing)
            {
                removedElement = OwnerDocument.WrapControl(existing);
            }

            cc.Content = child.Control;

            OwnerDocument.NotifyChildListMutation(this, new[] { child }, removedElement is null ? null : new[] { removedElement }, null, null);
            TriggerIframeNavigationIfNeeded(child);
            return child;
        }

        if (Control is Decorator decorator)
        {
            if (reference is not null && !ReferenceEquals(decorator.Child, reference.Control))
            {
                throw new InvalidOperationException(
                    "insertBefore reference is not a child of this node.");
            }

            if (reference is not null && ReferenceEquals(child.Control, reference.Control))
            {
                return child;
            }

            DetachFromCurrentParentWithNotification(child);

            if (OwnerDocument.IsConnectedStyleElement(this))
            {
                child.SuppressPaintUntilFirstStyleCommit();
            }

            AvaloniaDomElement? removedElement = null;
            if (decorator.Child is Control existing)
            {
                removedElement = OwnerDocument.WrapControl(existing);
            }

            decorator.Child = child.Control;

            OwnerDocument.NotifyChildListMutation(this, new[] { child }, removedElement is null ? null : new[] { removedElement }, null, null);
            TriggerIframeNavigationIfNeeded(child);
            return child;
        }

        return null;
    }

    private void ThrowIfHierarchyCycle(AvaloniaDomElement child)
    {
        for (Control? ancestor = Control; ancestor is not null; ancestor = ancestor.Parent as Control)
        {
            if (ReferenceEquals(ancestor, child.Control))
            {
                throw new InvalidOperationException(
                    "A DOM mutation cannot insert an ancestor into its descendant.");
            }
        }
    }

    private bool IsDirectChild(AvaloniaDomElement child)
    {
        if (TryGetControlsCollection(Control, out var list))
        {
            return list.IndexOf(child.Control) >= 0;
        }
        if (Control is ContentControl cc)
        {
            return ReferenceEquals(cc.Content, child.Control);
        }
        if (Control is Decorator decorator)
        {
            return ReferenceEquals(decorator.Child, child.Control);
        }
        return false;
    }

    private void TriggerIframeNavigationIfNeeded(AvaloniaDomElement child)
    {
        if (child.parentElement is null)
        {
            return;
        }

        if (string.Equals(child.nodeName, "IFRAME", StringComparison.OrdinalIgnoreCase))
        {
            var src = child.SafeGetAttribute("src") ?? string.Empty;
            if (string.IsNullOrWhiteSpace(src))
            {
                OwnerDocument.EnsureInitialIframeBrowsingContext(child);
            }
            else
            {
                OwnerDocument.TriggerIframeVirtualization(child, src);
            }
            return;
        }

        foreach (var descendant in child.GetChildElements().ToArray())
        {
            TriggerIframeNavigationIfNeeded(descendant);
        }
    }

    private static bool DetachFromParent(Control control)
    {
        var parent = control.Parent;
        switch (parent)
        {
            case Panel panel:
                return panel.Children.Remove(control);
            case Decorator decorator when decorator.Child == control:
                decorator.Child = null;
                return true;
            case ContentControl cc when ReferenceEquals(cc.Content, control):
                cc.Content = null;
                return true;
        }
        return false;
    }

    private static string NormalizeEventName(string? type)
        => string.IsNullOrWhiteSpace(type) ? string.Empty : type.Trim().ToLowerInvariant();

    protected virtual bool TryGetAttribute(string name, out string? value)
    {
        value = null;
        switch (name)
        {
            case "id":
            case "name":
                value = _attributes.TryGetValue(name, out var authoredIdentity)
                    ? authoredIdentity
                    : null;
                return true;
            case "class":
                value = HasAttributePresence("class")
                    ? string.Join(' ', ((Control as StyledElement)?.Classes ?? new Classes()).Where(c => !c.StartsWith(':')))
                    : null;
                return true;
            case "title":
                value = string.Equals(_nodeNameOverride, "IFRAME", StringComparison.OrdinalIgnoreCase)
                    ? (_attributes.TryGetValue(name, out var genericTitle) ? genericTitle : null)
                    : ToolTip.GetTip(Control)?.ToString();
                return true;
            case "style":
                value = HasAttributePresence("style") ? GetStyleString(Control) : null;
                return true;
            case "disabled":
                // Avoid the generic CLR-property fallback turning a missing
                // boolean attribute into the non-null string "False".
                value = null;
                return true;
            case "tabindex":
                // KeyboardNavigation exposes an inherited native default even
                // when no HTML attribute was authored. Attribute reflection
                // must distinguish that default from actual markup.
                value = _attributes.TryGetValue(name, out var authoredTabIndex)
                    ? authoredTabIndex
                    : null;
                return true;
        }

        return false;
    }

    protected virtual bool TrySetAttribute(string name, string? value)
    {
        if (name == "style")
        {
            ApplyStyleAttribute(value);
            return true;
        }

        return false;
    }

    protected virtual string GetStyleString(Control control)
    {
        if (_styleValues.Count == 0)
        {
            return string.Empty;
        }

        if (s_disableStyleSerializationBuilder)
        {
            return string.Join("; ", _styleValues.Select(kv =>
                $"{kv.Key}: {kv.Value}{(_importantStyleProperties.Contains(kv.Key) ? " !important" : string.Empty)}"));
        }

        var builder = new StringBuilder(_styleValues.Count * 24);
        foreach (var pair in _styleValues)
        {
            if (builder.Length > 0)
            {
                builder.Append("; ");
            }

            builder.Append(pair.Key).Append(": ").Append(pair.Value);
            if (_importantStyleProperties.Contains(pair.Key))
            {
                builder.Append(" !important");
            }
        }

        return builder.ToString();
    }

    internal string GetStyleText() => GetStyleString(Control);

    internal void SetStyleText(string? value)
    {
        ApplyStyleAttribute(value);
    }

    protected static bool TryGetControlsCollection(Control parent, out Controls controls)
    {
        if (parent is Panel panel)
        {
            controls = panel.Children;
            return true;
        }

        var prop = parent.GetType().GetProperties(BindingFlags.Instance | BindingFlags.Public)
            .FirstOrDefault(p => p.PropertyType == typeof(Controls) && string.Equals(p.Name, "content", StringComparison.OrdinalIgnoreCase));
        if (prop is not null)
        {
            controls = (Controls)prop.GetValue(parent)!;
            return true;
        }

        controls = null!;
        return false;
    }

}

public sealed class AvaloniaDomImageElement : AvaloniaDomElement
{
    private static readonly HttpClient s_httpClient = new();
    private string _src = string.Empty;
    private byte[]? _rgbaPixels;
    private int _naturalWidth;
    private int _naturalHeight;

    public AvaloniaDomImageElement(AvaloniaBrowserHost host, AvaloniaDomDocument ownerDocument, Image image)
        : base(host, ownerDocument, image)
    {
    }

    private Image ImageControl => (Image)Control;

    private readonly record struct DecodedImage(int Width, int Height, byte[] RgbaPixels, WriteableBitmap Image);

    public override string src
    {
        get => _src;
        set => SetSource(value);
    }

    public string crossOrigin { get; set; } = string.Empty;

    public string referrerPolicy { get; set; } = string.Empty;

    public string decoding { get; set; } = "auto";

    public bool complete { get; private set; }

    public double naturalWidth => _naturalWidth;

    public double naturalHeight => _naturalHeight;

    public override double width
    {
        get
        {
            var explicitWidth = base.width;
            return explicitWidth > 0 ? explicitWidth : naturalWidth;
        }
        set => base.width = value;
    }

    public override double height
    {
        get
        {
            var explicitHeight = base.height;
            return explicitHeight > 0 ? explicitHeight : naturalHeight;
        }
        set => base.height = value;
    }

    internal bool TryGetRgbaPixels(out int width, out int height, out byte[] pixels)
    {
        width = 0;
        height = 0;
        pixels = Array.Empty<byte>();

        if (_rgbaPixels is null || _naturalWidth <= 0 || _naturalHeight <= 0)
        {
            return false;
        }

        width = _naturalWidth;
        height = _naturalHeight;
        pixels = _rgbaPixels;
        return pixels.Length == width * height * 4;
    }

    internal static bool TryGetRgbaPixels(object? source, out int width, out int height, out byte[] pixels)
    {
        width = 0;
        height = 0;
        pixels = Array.Empty<byte>();

        if (source is AvaloniaDomImageElement domImage)
        {
            return domImage.TryGetRgbaPixels(out width, out height, out pixels);
        }

        if (source is AvaloniaDomElement { Control: Image domControl } && domControl.Source is Bitmap domBitmap)
        {
            width = domBitmap.PixelSize.Width;
            height = domBitmap.PixelSize.Height;
            if (width <= 0 || height <= 0)
            {
                return false;
            }

            pixels = ExtractRgbaPixels(domBitmap);
            return pixels.Length == width * height * 4;
        }

        if (source is Image image && image.Source is Bitmap bitmap)
        {
            width = bitmap.PixelSize.Width;
            height = bitmap.PixelSize.Height;
            if (width <= 0 || height <= 0)
            {
                return false;
            }

            pixels = ExtractRgbaPixels(bitmap);
            return pixels.Length == width * height * 4;
        }

        return false;
    }

    protected override bool TrySetAttribute(string name, string? value)
    {
        if (string.Equals(name, "src", StringComparison.OrdinalIgnoreCase)
            || (string.Equals(localName, "image", StringComparison.OrdinalIgnoreCase)
                && (string.Equals(name, "href", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(name, "xlink:href", StringComparison.OrdinalIgnoreCase))))
        {
            src = value ?? string.Empty;
            return true;
        }

        return base.TrySetAttribute(name, value);
    }

    private void SetSource(string? value)
    {
        _src = value ?? string.Empty;
        complete = false;
        _rgbaPixels = null;
        _naturalWidth = 0;
        _naturalHeight = 0;

        if (string.IsNullOrWhiteSpace(_src))
        {
            ImageControl.Source = null;
            return;
        }

        try
        {
            using var stream = OpenImageStream(_src);
            var decoded = DecodeImage(stream);
            ImageControl.Source = decoded.Image;
            _naturalWidth = decoded.Width;
            _naturalHeight = decoded.Height;
            _rgbaPixels = decoded.RgbaPixels;
            complete = true;
            DispatchImageEvent("load", onload);
        }
        catch
        {
            ImageControl.Source = null;
            DispatchImageEvent("error", onerror);
        }
    }

    private Stream OpenImageStream(string source)
    {
        if (source.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
        {
            return OpenDataUri(source);
        }

        if (AvaloniaBrowserHost.UrlJs.TryGetObjectUrlData(source, out var objectUrlData, out _))
        {
            return new MemoryStream(objectUrlData, writable: false);
        }

        if (Uri.TryCreate(source, UriKind.Absolute, out var uri))
        {
            if (uri.Scheme.Equals("avares", StringComparison.OrdinalIgnoreCase))
            {
                return AssetLoader.Open(uri);
            }

            if (uri.IsFile)
            {
                return File.OpenRead(uri.LocalPath);
            }

            if (uri.Scheme.Equals("http", StringComparison.OrdinalIgnoreCase) ||
                uri.Scheme.Equals("https", StringComparison.OrdinalIgnoreCase))
            {
                var bytes = s_httpClient.GetByteArrayAsync(uri).GetAwaiter().GetResult();
                return new MemoryStream(bytes);
            }
        }

        var path = source;
        if (!Path.IsPathRooted(path) && !string.IsNullOrEmpty(Host.ScriptBaseDirectory))
        {
            path = Path.GetFullPath(Path.Combine(Host.ScriptBaseDirectory, path));
        }

        return File.OpenRead(path);
    }

    private static Stream OpenDataUri(string source)
    {
        var comma = source.IndexOf(',');
        if (comma < 0)
        {
            throw new FormatException("Invalid data URI.");
        }

        var metadata = source[..comma];
        var payload = source[(comma + 1)..];
        var bytes = metadata.Contains(";base64", StringComparison.OrdinalIgnoreCase)
            ? Convert.FromBase64String(payload)
            : Encoding.UTF8.GetBytes(Uri.UnescapeDataString(payload));
        return new MemoryStream(bytes);
    }

    private static DecodedImage DecodeImage(Stream stream)
    {
        using var copy = new MemoryStream();
        stream.CopyTo(copy);
        var encoded = copy.ToArray();
        using var encodedStream = new MemoryStream(encoded, writable: false);
        using var codec = SKCodec.Create(encodedStream);
        if (codec is null)
        {
            return DecodeSvgImage(encoded);
        }
        var width = codec.Info.Width;
        var height = codec.Info.Height;
        if (width <= 0 || height <= 0)
        {
            throw new InvalidOperationException("Decoded image has invalid dimensions.");
        }

        var info = new SKImageInfo(width, height, SKColorType.Rgba8888, SKAlphaType.Unpremul);
        using var bitmap = new SKBitmap(info);
        var result = codec.GetPixels(info, bitmap.GetPixels());
        if (result != SKCodecResult.Success && result != SKCodecResult.IncompleteInput)
        {
            throw new InvalidOperationException($"Unable to decode image pixels: {result}.");
        }

        var rgbaPixels = new byte[width * height * 4];
        Marshal.Copy(bitmap.GetPixels(), rgbaPixels, 0, rgbaPixels.Length);

        return new DecodedImage(width, height, rgbaPixels, CreateWriteableBitmap(width, height, rgbaPixels));
    }

    private static DecodedImage DecodeSvgImage(byte[] encoded)
    {
        var markup = Encoding.UTF8.GetString(encoded);
        using var svg = new SKSvg();
        using var picture = svg.FromSvg(markup) ?? throw new InvalidOperationException("Unable to decode image.");
        var source = picture.CullRect;
        var width = Math.Max(1, (int)Math.Ceiling(source.Width));
        var height = Math.Max(1, (int)Math.Ceiling(source.Height));
        var info = new SKImageInfo(width, height, SKColorType.Rgba8888, SKAlphaType.Unpremul);
        using var surface = SKSurface.Create(info) ?? throw new InvalidOperationException("Unable to rasterize SVG image.");
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.Translate(-source.Left, -source.Top);
        surface.Canvas.DrawPicture(picture);
        surface.Canvas.Flush();
        using var snapshot = surface.Snapshot();
        using var bitmap = SKBitmap.FromImage(snapshot);
        var rgbaPixels = new byte[width * height * 4];
        Marshal.Copy(bitmap.GetPixels(), rgbaPixels, 0, rgbaPixels.Length);
        return new DecodedImage(width, height, rgbaPixels, CreateWriteableBitmap(width, height, rgbaPixels));
    }

    private static WriteableBitmap CreateWriteableBitmap(int width, int height, byte[] rgbaPixels)
    {
        var image = new WriteableBitmap(
            new PixelSize(width, height),
            new Vector(96, 96),
            PixelFormat.Bgra8888,
            AlphaFormat.Premul);

        using var framebuffer = image.Lock();
        var bgraRow = new byte[width * 4];
        for (var y = 0; y < height; y++)
        {
            var sourceOffset = y * width * 4;
            for (var x = 0; x < width; x++)
            {
                var source = sourceOffset + x * 4;
                var target = x * 4;
                var a = rgbaPixels[source + 3];
                var r = Premultiply(rgbaPixels[source], a);
                var g = Premultiply(rgbaPixels[source + 1], a);
                var b = Premultiply(rgbaPixels[source + 2], a);
                bgraRow[target] = b;
                bgraRow[target + 1] = g;
                bgraRow[target + 2] = r;
                bgraRow[target + 3] = a;
            }

            Marshal.Copy(bgraRow, 0, IntPtr.Add(framebuffer.Address, y * framebuffer.RowBytes), bgraRow.Length);
        }

        return image;
    }

    private static byte Premultiply(byte channel, byte alpha)
    {
        if (alpha >= byte.MaxValue)
        {
            return channel;
        }

        if (alpha == 0)
        {
            return 0;
        }

        return (byte)((channel * alpha + 127) / 255);
    }

    private void DispatchImageEvent(string eventName, object? handler)
    {
        void Deliver()
        {
            if (Host.IsExecutingJavaScript)
            {
                Host.Services.Dispatcher.Post(Deliver, HtmlMlDispatchPriority.Background);
                return;
            }

            var evt = new DomSyntheticEvent(eventName, bubbles: false, cancelable: false, Host.GetTimestamp(), detail: null, accessor: null);
            OwnerDocument.DispatchSyntheticEvent(this, evt);

            if (handler is not null)
            {
                try
                {
                    var callback = handler as IExternalJavaScriptCallback
                                   ?? Host.ExternalCallbackAdapter?.GetCallback(handler, create: true);
                    using var scope = Host.EnterExternalJavaScriptCall();
                    callback?.Invoke(this, evt);
                }
                catch
                {
                }
            }
        }

        Host.Services.Dispatcher.Post(Deliver, HtmlMlDispatchPriority.Send);
    }

    private static byte[] ExtractRgbaPixels(Bitmap bitmap)
    {
        var width = bitmap.PixelSize.Width;
        var height = bitmap.PixelSize.Height;
        if (width <= 0 || height <= 0)
        {
            return Array.Empty<byte>();
        }

        var stride = width * 4;
        var pixels = new byte[stride * height];
        var handle = System.Runtime.InteropServices.GCHandle.Alloc(pixels, System.Runtime.InteropServices.GCHandleType.Pinned);
        try
        {
            using var framebuffer = new LockedFramebuffer(
                handle.AddrOfPinnedObject(),
                new PixelSize(width, height),
                stride,
                new Vector(96, 96),
                PixelFormats.Rgba8888,
                null);
            bitmap.CopyPixels(framebuffer, AlphaFormat.Unpremul);
        }
        finally
        {
            handle.Free();
        }

        return pixels;
    }
}

public sealed class AvaloniaDomTextNode : AvaloniaDomElement
{
    private string _data;
    private string _textTransform = "none";
    private string _whiteSpace = "normal";

    public AvaloniaDomTextNode(AvaloniaBrowserHost host, AvaloniaDomDocument ownerDocument, TextBlock control)
        : base(host, ownerDocument, control)
    {
        _data = control.Text ?? string.Empty;
        UpdateDisplayedText();
    }

    private TextBlock TextBlock => (TextBlock)Control;

    public override int nodeType => 3;

    public override string nodeName => "#TEXT";

    public override string? nodeValue
    {
        get => data;
        set => data = value ?? string.Empty;
    }

    public string data
    {
        get => _data;
        set
        {
            _data = value ?? string.Empty;
            UpdateDisplayedText();
        }
    }

    internal void ApplyTextTransform(string? value)
    {
        _textTransform = DomTextNodeSemantics.NormalizeTextTransform(value);
        UpdateDisplayedText();
    }

    internal void ApplyWhiteSpace(string? value)
    {
        _whiteSpace = NormalizeWhiteSpace(value);
        UpdateDisplayedText();
    }

    private void UpdateDisplayedText()
    {
        // This concrete switch is the specialized fast path for text churn.
        // Its default branch stays identical to the pre-extraction path while
        // the non-trivial capitalization algorithm lives in the DOM core.
        var transformed = _textTransform switch
        {
            "uppercase" => _data.ToUpperInvariant(),
            "lowercase" => _data.ToLowerInvariant(),
            "capitalize" => DomTextNodeSemantics.CapitalizeWords(_data),
            _ => _data
        };
        var collapses = _whiteSpace is "normal" or "nowrap";
        var presentation = _whiteSpace switch
        {
            "normal" or "nowrap" => CollapseWhiteSpace(transformed, preserveSegmentBreaks: false),
            "pre-line" => CollapseWhiteSpace(transformed, preserveSegmentBreaks: true),
            _ => transformed
        };
        if (TextBlock is DomTextBlockControl domText)
        {
            domText.SetWhiteSpacePresentation(presentation, collapses);
        }
        else
        {
            TextBlock.Text = presentation;
            TextBlock.IsVisible = !string.IsNullOrWhiteSpace(presentation);
        }
    }

    private static string NormalizeWhiteSpace(string? value)
        => value?.Trim().ToLowerInvariant() switch
        {
            "nowrap" => "nowrap",
            "pre" => "pre",
            "pre-wrap" => "pre-wrap",
            "pre-line" => "pre-line",
            "break-spaces" => "break-spaces",
            _ => "normal"
        };

    private static string CollapseWhiteSpace(string value, bool preserveSegmentBreaks)
    {
        var result = new StringBuilder(value.Length);
        var pendingSpace = false;
        for (var index = 0; index < value.Length; index++)
        {
            var current = value[index];
            if (current == '\r')
            {
                if (index + 1 < value.Length && value[index + 1] == '\n') index++;
                current = '\n';
            }
            if (current == '\n' && preserveSegmentBreaks)
            {
                if (result.Length > 0 && result[^1] == ' ') result.Length--;
                result.Append('\n');
                pendingSpace = false;
                continue;
            }
            if (current is ' ' or '\t' or '\n' or '\f')
            {
                pendingSpace = true;
                continue;
            }
            if (pendingSpace && result.Length > 0 && result[^1] != '\n') result.Append(' ');
            result.Append(current);
            pendingSpace = false;
        }
        if (pendingSpace && result.Length > 0 && result[^1] != '\n') result.Append(' ');
        return result.ToString();
    }

    public override string? textContent
    {
        get => data;
        set => data = value ?? string.Empty;
    }
}

public sealed class DomHeadElement
{
    private readonly AvaloniaDomDocument _document;
    private DomDocumentElement? _parent;
    private readonly List<object> _children = new();

    internal DomHeadElement(AvaloniaDomDocument document)
    {
        _document = document;
        style = new DocumentStyleProbe();
    }

    internal void SetParent(DomDocumentElement parent)
    {
        _parent = parent;
    }

    public int nodeType => 1;

    public string nodeName => "HEAD";

    public string tagName => "HEAD";

    public string localName => "head";

    public DocumentStyleProbe style { get; }

    public AvaloniaDomDocument ownerDocument => _document;

    public DomDocumentElement? parentElement => _parent;

    public DomDocumentElement? parentNode => _parent;

    public object[] childNodes => _children.ToArray();

    public object[] children => childNodes;

    public object? firstChild => _children.Count > 0 ? _children[0] : null;

    public object? lastChild => _children.Count > 0 ? _children[^1] : null;

    public bool hasChildNodes => _children.Count > 0;

    public bool contains(object? node)
    {
        node = AvaloniaDomDocument.UnwrapDomNode(node);
        if (ReferenceEquals(node, this))
        {
            return true;
        }

        return node is AvaloniaDomElement element
               && ReferenceEquals(element.ownerDocument, _document)
               && Contains(element);
    }

    public int compareDocumentPosition(object? other)
        => AvaloniaDomDocument.CompareDocumentPosition(this, other);

    internal bool Contains(AvaloniaDomElement element)
    {
        foreach (var root in _children.OfType<AvaloniaDomElement>())
        {
            var current = element.Control;
            while (current is not null)
            {
                if (ReferenceEquals(current, root.Control))
                {
                    return true;
                }
                current = current.Parent as Control;
            }
        }
        return false;
    }

    public object appendChild(object node)
    {
        if (node is null)
        {
            return node!;
        }

        _children.Remove(node);
        _children.Add(node);
        if (AvaloniaDomDocument.UnwrapDomNode(node) is AvaloniaDomElement element)
        {
            element.SetDomParent(this);
        }
        _document.NotifyHeadNodeAttached(node);
        return node;
    }

    public object insertBefore(object node, object? referenceNode)
    {
        if (referenceNode is null)
        {
            return appendChild(node);
        }

        var index = _children.IndexOf(referenceNode);
        if (index < 0)
        {
            return appendChild(node);
        }

        _children.Remove(node);
        _children.Insert(index, node);
        if (AvaloniaDomDocument.UnwrapDomNode(node) is AvaloniaDomElement element)
        {
            element.SetDomParent(this);
        }
        _document.NotifyHeadNodeAttached(node);
        return node;
    }

    public object? removeChild(object node)
    {
        if (_children.Remove(node))
        {
            if (AvaloniaDomDocument.UnwrapDomNode(node) is AvaloniaDomElement element)
            {
                element.SetDomParent(null);
            }
            _document.NotifyHeadChanged();
            return node;
        }

        return null;
    }

    public object? replaceChild(object newChild, object oldChild)
    {
        var index = _children.IndexOf(oldChild);
        if (index < 0)
        {
            return null;
        }

        _children[index] = newChild;
        if (AvaloniaDomDocument.UnwrapDomNode(oldChild) is AvaloniaDomElement oldElement)
        {
            oldElement.SetDomParent(null);
        }
        if (AvaloniaDomDocument.UnwrapDomNode(newChild) is AvaloniaDomElement newElement)
        {
            newElement.SetDomParent(this);
        }
        _document.NotifyHeadNodeAttached(newChild);
        return oldChild;
    }

    public void addEventListener(string type, object handler)
        => _document.addEventListener(type, handler);

    public void addEventListener(string type, object handler, object? options)
        => _document.addEventListener(type, handler, options);

    public void removeEventListener(string type, object handler)
        => _document.removeEventListener(type, handler);

    public void removeEventListener(string type, object handler, object? options)
        => _document.removeEventListener(type, handler, options);

}

public sealed class DomDocumentElement : ICssSelectorNode
{
    private readonly AvaloniaDomDocument _document;
    private readonly DomHeadElement _head;
    private readonly Dictionary<string, string?> _attributes = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, string?> _dataAttributes = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<DomEventRegistration>> _eventListeners = new(StringComparer.OrdinalIgnoreCase);
    private DomStringMap? _dataset;

    internal DomDocumentElement(AvaloniaDomDocument document, DomHeadElement head)
    {
        _document = document;
        _head = head;
        style = new DocumentStyleProbe(_document.NotifyDocumentStyleChanged);
        _head.SetParent(this);
    }

    public int nodeType => 1;

    public string nodeName => "HTML";

    public string tagName => "HTML";

    public string localName => "html";

    public DocumentStyleProbe style { get; }

    internal IReadOnlyDictionary<string, string> StyleValues => style.StyleValues;

    public DomStringMap dataset => _dataset ??= new DomStringMap(this);

    internal IReadOnlyDictionary<string, string?> DataAttributes => _dataAttributes;

    internal bool TryGetDataAttribute(string attributeName, out string? value)
        => _dataAttributes.TryGetValue(attributeName.ToLowerInvariant(), out value);

    internal void SetDataAttribute(string attributeName, string? value)
    {
        attributeName = attributeName.ToLowerInvariant();
        if (value is null) _dataAttributes.Remove(attributeName);
        else _dataAttributes[attributeName] = value;
        _document.NotifyDocumentStyleChanged();
    }

    internal bool RemoveDataAttribute(string attributeName)
    {
        var removed = _dataAttributes.Remove(attributeName.ToLowerInvariant());
        if (removed) _document.NotifyDocumentStyleChanged();
        return removed;
    }


    public string? getAttribute(string name)
        => name.StartsWith("data-", StringComparison.OrdinalIgnoreCase)
            ? TryGetDataAttribute(name, out var dataValue) ? dataValue : null
            : _attributes.TryGetValue(name, out var value) ? value : null;

    public void setAttribute(string name, string? value)
    {
        if (name.StartsWith("data-", StringComparison.OrdinalIgnoreCase))
        {
            SetDataAttribute(name, value ?? string.Empty);
            return;
        }

        _attributes[name] = value ?? string.Empty;
        _document.NotifyDocumentStyleChanged();
    }

    public void removeAttribute(string name)
    {
        if (name.StartsWith("data-", StringComparison.OrdinalIgnoreCase))
        {
            RemoveDataAttribute(name);
            return;
        }

        if (_attributes.Remove(name)) _document.NotifyDocumentStyleChanged();
    }

    public string dir
    {
        get => getAttribute("dir") ?? string.Empty;
        set => setAttribute("dir", value ?? string.Empty);
    }

    public AvaloniaDomDocument ownerDocument => _document;

    public DomDocumentElement? parentElement => null;

    public AvaloniaDomDocument parentNode => _document;

    public double clientWidth => _document.GetDocumentViewportClientSize().Width;

    public double clientHeight => _document.GetDocumentViewportClientSize().Height;

    public double clientLeft => 0;

    public double clientTop => 0;

    public double scrollWidth => body?.scrollWidth ?? clientWidth;

    public double scrollHeight => body?.scrollHeight ?? clientHeight;

    public double scrollLeft
    {
        get
        {
            _document.EnsureStylesCurrent();
            _document.FlushPendingLayout();
            return body?.Control is CssLayoutPanel panel ? panel.ScrollOffset.X : body?.scrollLeft ?? 0;
        }
        set
        {
            _document.EnsureStylesCurrent();
            _document.FlushPendingLayout();
            if (body?.Control is CssLayoutPanel panel)
            {
                panel.SetDocumentScrollOffset(
                    new Vector(value, panel.ScrollOffset.Y),
                    _document.GetDocumentViewportClientSize());
            }
            else if (body is { } bodyElement)
            {
                bodyElement.scrollLeft = value;
            }
        }
    }

    public double scrollTop
    {
        get
        {
            _document.EnsureStylesCurrent();
            _document.FlushPendingLayout();
            return body?.Control is CssLayoutPanel panel ? panel.ScrollOffset.Y : body?.scrollTop ?? 0;
        }
        set
        {
            _document.EnsureStylesCurrent();
            _document.FlushPendingLayout();
            if (body?.Control is CssLayoutPanel panel)
            {
                panel.SetDocumentScrollOffset(
                    new Vector(panel.ScrollOffset.X, value),
                    _document.GetDocumentViewportClientSize());
            }
            else if (body is { } bodyElement)
            {
                bodyElement.scrollTop = value;
            }
        }
    }

    public DomRect getBoundingClientRect()
        => new(new Rect(0, 0, clientWidth, clientHeight));

    public DomRect[] getClientRects()
        => [getBoundingClientRect()];

    public DomHeadElement head => _head;

    public AvaloniaDomElement? body
    {
        get => _document.body as AvaloniaDomElement;
    }

    public object[] childNodes
    {
        get
        {
            var bodyElement = body;
            return bodyElement is null ? new object[] { _head } : new object[] { _head, bodyElement };
        }
    }

    public object[] children => childNodes;

    public object? firstChild => childNodes.FirstOrDefault();

    public object? lastChild => childNodes.LastOrDefault();

    public bool hasChildNodes => childNodes.Length > 0;

    public bool contains(object? node)
    {
        node = AvaloniaDomDocument.UnwrapDomNode(node);
        if (node is null)
        {
            return false;
        }

        if (ReferenceEquals(node, this) || ReferenceEquals(node, _head))
        {
            return true;
        }

        if (ReferenceEquals(node, body))
        {
            return true;
        }

        return node is AvaloniaDomElement element
               && ReferenceEquals(element.ownerDocument, _document)
               && _document.contains(element);
    }

    public int compareDocumentPosition(object? other)
        => AvaloniaDomDocument.CompareDocumentPosition(this, other);

    public object appendChild(object node)
    {
        if (node is DomHeadElement)
        {
            return node;
        }

        if (node is AvaloniaDomElement element)
        {
            body?.appendChild(element);
            return element;
        }

        return node;
    }

    public object? querySelector(string selector)
        => _document.querySelector(selector);

    public object[] querySelectorAll(string selector)
        => _document.querySelectorAll(selector);

    public object removeChild(object node)
    {
        if (node is DomHeadElement)
        {
            throw new InvalidOperationException("The live document HEAD cannot be detached by this bounded root model.");
        }
        if (node is AvaloniaDomElement element && body is { } bodyElement)
        {
            return bodyElement.removeChild(element)
                   ?? throw new InvalidOperationException("removeChild requires a child of the document root.");
        }
        throw new InvalidOperationException("removeChild requires a child of the document root.");
    }

    public void addEventListener(string type, object handler)
        => addEventListener(type, handler, options: null);

    public void addEventListener(string type, object handler, object? options)
    {
        var adapter = _document.ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: true);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        var normalized = NormalizeEventName(type);
        if (string.IsNullOrEmpty(normalized))
        {
            return;
        }

        if (!_eventListeners.TryGetValue(normalized, out var listeners))
        {
            listeners = new List<DomEventRegistration>();
            _eventListeners[normalized] = listeners;
        }
        if (!listeners.Any(item => item.Matches(listener, parsed.Capture)))
        {
            listeners.Add(new DomEventRegistration(
                listener,
                new EventListenerOptions(parsed.Capture, parsed.Once, parsed.Passive)));
        }
    }

    public void removeEventListener(string type, object handler)
        => removeEventListener(type, handler, options: null);

    public void removeEventListener(string type, object handler, object? options)
    {
        var adapter = _document.ExternalEventListenerAdapter;
        var listener = adapter?.GetEventListener(handler, create: false);
        if (listener is null)
        {
            return;
        }

        var parsed = adapter!.GetEventListenerOptions(options);
        var normalized = NormalizeEventName(type);
        if (!_eventListeners.TryGetValue(normalized, out var listeners))
        {
            return;
        }
        var registration = listeners.FirstOrDefault(item => item.Matches(listener, parsed.Capture));
        if (registration is not null)
        {
            RemoveListener(normalized, registration);
        }
    }

    public void click() => _document.DispatchProgrammaticClick(this);

    internal IReadOnlyList<DomEventRegistration>? GetListeners(string type)
    {
        var normalized = NormalizeEventName(type);
        return _eventListeners.TryGetValue(normalized, out var listeners) ? listeners : null;
    }

    internal void RemoveListener(string type, DomEventRegistration listener)
    {
        var normalized = NormalizeEventName(type);
        if (_eventListeners.TryGetValue(normalized, out var listeners)
            && listeners.Remove(listener)
            && listeners.Count == 0)
        {
            _eventListeners.Remove(normalized);
        }
    }

    private static string NormalizeEventName(string? type)
        => string.IsNullOrWhiteSpace(type) ? string.Empty : type.Trim().ToLowerInvariant();

    private DomDocumentTokenList? _classList;
    public DomDocumentTokenList classList => _classList ??= new DomDocumentTokenList(_document);

    string ICssSelectorNode.TagName => "html";
    string ICssSelectorNode.Id => getAttribute("id") ?? string.Empty;
    string ICssSelectorNode.TextContent => string.Empty;
    int ICssSelectorNode.ChildElementCount => body is null ? 0 : 1;
    bool ICssSelectorNode.IsDocumentElement => true;
    ICssSelectorNode? ICssSelectorNode.ParentElement => null;
    ICssSelectorNode? ICssSelectorNode.PreviousElementSibling => null;
    ICssSelectorNode? ICssSelectorNode.NextElementSibling => null;
    bool ICssSelectorNode.HasClass(string className) => classList.contains(className);
    string? ICssSelectorNode.GetAttribute(string name) => getAttribute(name);
    bool ICssSelectorNode.HasState(CssSelectorState state) => state == CssSelectorState.None;
}

public sealed class DomDocumentTokenList
{
    private readonly HashSet<string> _classes = new();
    private readonly AvaloniaDomDocument _document;

    internal DomDocumentTokenList(AvaloniaDomDocument document)
    {
        _document = document;
    }

    public string value => string.Join(' ', _classes);

    public void add(params string[] tokens)
    {
        if (tokens == null) return;
        var changed = false;
        foreach (var t in tokens)
        {
            if (!string.IsNullOrWhiteSpace(t))
            {
                changed |= _classes.Add(t.Trim());
            }
        }
        if (changed) _document.NotifyDocumentStyleChanged();
    }

    public void remove(params string[] tokens)
    {
        if (tokens == null) return;
        var changed = false;
        foreach (var t in tokens)
        {
            if (!string.IsNullOrWhiteSpace(t))
            {
                changed |= _classes.Remove(t.Trim());
            }
        }
        if (changed) _document.NotifyDocumentStyleChanged();
    }

    public bool toggle(string token)
    {
        if (string.IsNullOrWhiteSpace(token)) return false;
        token = token.Trim();
        if (_classes.Contains(token))
        {
            _classes.Remove(token);
            _document.NotifyDocumentStyleChanged();
            return false;
        }
        else
        {
            _classes.Add(token);
            _document.NotifyDocumentStyleChanged();
            return true;
        }
    }

    public bool contains(string token)
    {
        if (string.IsNullOrWhiteSpace(token)) return false;
        return _classes.Contains(token.Trim());
    }

    public void SetFromString(string? value)
    {
        var next = string.IsNullOrWhiteSpace(value)
            ? Array.Empty<string>()
            : value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (_classes.SetEquals(next)) return;
        _classes.Clear();
        foreach (var token in next) _classes.Add(token);
        _document.NotifyDocumentStyleChanged();
    }

}

public sealed class DomDocumentImplementation
{
    private readonly AvaloniaDomDocument _document;

    internal DomDocumentImplementation(AvaloniaDomDocument document)
    {
        _document = document;
    }

    public DomParsedDocument createHTMLDocument(string? title)
    {
        var html = (AvaloniaDomElement?)_document.createElement("html")
                   ?? throw new InvalidOperationException("Unable to create a detached HTML root.");
        var head = (AvaloniaDomElement?)_document.createElement("head")
                   ?? throw new InvalidOperationException("Unable to create a detached HTML head.");
        var body = (AvaloniaDomElement?)_document.createElement("body")
                   ?? throw new InvalidOperationException("Unable to create a detached HTML body.");
        html.appendChild(head);
        html.appendChild(body);
        if (!string.IsNullOrEmpty(title))
        {
            var titleElement = (AvaloniaDomElement?)_document.createElement("title")
                               ?? throw new InvalidOperationException("Unable to create a detached title.");
            titleElement.textContent = title;
            head.appendChild(titleElement);
        }

        return new DomParsedDocument(_document, html, body, head);
    }

    public DomParsedDocument createDocument(string? namespaceUri, string? qualifiedName, object? doctype)
    {
        var root = string.IsNullOrEmpty(qualifiedName)
            ? null
            : _document.CreateXmlElement(qualifiedName, namespaceUri);
        return new DomParsedDocument(_document, root, xmlMode: true);
    }
}

public sealed class DocumentStyleProbe : DynamicObject, IHtmlMlCssStyleDeclarationTarget
{
    private readonly Dictionary<string, string> _values = new(CssPropertyNameComparer.Instance);
    private readonly Action? _changed;

    internal DocumentStyleProbe(Action? changed = null)
    {
        _changed = changed;
    }

    internal IReadOnlyDictionary<string, string> StyleValues => _values;

    public string cssText
    {
        get => GetCssText();
        set => SetCssText(value);
    }

    public string userSelect
    {
        get => getPropertyValue("user-select") ?? string.Empty;
        set => setProperty("user-select", value);
    }

    public string MozUserSelect
    {
        get => getPropertyValue("-moz-user-select") ?? string.Empty;
        set => setProperty("-moz-user-select", value);
    }

    public string WebkitUserSelect
    {
        get => getPropertyValue("-webkit-user-select") ?? string.Empty;
        set => setProperty("-webkit-user-select", value);
    }

    public string KhtmlUserSelect
    {
        get => getPropertyValue("-khtml-user-select") ?? string.Empty;
        set => setProperty("-khtml-user-select", value);
    }

    public int length => _values.Count;

    public string item(int index)
        => index >= 0 && index < _values.Count
            ? _values.ElementAt(index).Key
            : string.Empty;

    public string GetCssText()
        => string.Join("; ", _values.Select(pair => $"{pair.Key}: {pair.Value}"));

    public void SetCssText(string? value)
    {
        var next = new Dictionary<string, string>(CssPropertyNameComparer.Instance);
        var remaining = value.AsSpan();
        while (!remaining.IsEmpty)
        {
            var separator = remaining.IndexOf(';');
            var declaration = separator >= 0 ? remaining[..separator] : remaining;
            remaining = separator >= 0 ? remaining[(separator + 1)..] : ReadOnlySpan<char>.Empty;

            var colon = declaration.IndexOf(':');
            if (colon < 0)
            {
                continue;
            }

            var property = declaration[..colon].Trim();
            var propertyValue = declaration[(colon + 1)..].Trim();
            if (!property.IsEmpty && !propertyValue.IsEmpty)
            {
                next[CssStyleDeclaration.NormalizePropertyName(property.ToString())] = propertyValue.ToString();
            }
        }

        if (_values.Count == next.Count
            && _values.All(pair => next.TryGetValue(pair.Key, out var nextValue)
                                   && string.Equals(pair.Value, nextValue, StringComparison.Ordinal)))
        {
            return;
        }

        _values.Clear();
        foreach (var pair in next)
        {
            _values[pair.Key] = pair.Value;
        }
        _changed?.Invoke();
    }

    public override bool TrySetMember(SetMemberBinder binder, object? value)
    {
        setProperty(binder.Name, value?.ToString());
        return true;
    }

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        result = getPropertyValue(binder.Name) ?? string.Empty;
        return true;
    }

    public void setProperty(string propertyName, string? value)
        => SetProperty(propertyName, value);

    public bool supportsPropertyName(string propertyName)
        => CssPropertyCatalog.IsSupported(propertyName);

    void IHtmlMlCssStyleDeclarationTarget.SetProperty(string propertyName, string? value)
        => SetProperty(propertyName, value);

    private void SetProperty(string propertyName, string? value)
    {
        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        if (string.IsNullOrEmpty(normalized))
        {
            return;
        }

        if (value is not null)
        {
            if (!normalized.StartsWith("--", StringComparison.Ordinal)
                && !CssPropertyCatalog.IsSupported(normalized))
            {
                return;
            }
            if (!CssPropertyCatalog.IsValidCssomValue(normalized, value))
            {
                return;
            }
            if (value.Length == 0)
            {
                value = null;
            }
        }

        if (value is null)
        {
            removeProperty(normalized);
            return;
        }

        if (_values.TryGetValue(normalized, out var previous)
            && string.Equals(previous, value, StringComparison.Ordinal))
        {
            return;
        }

        _values[normalized] = value;
        _changed?.Invoke();
    }

    public void removeProperty(string propertyName)
    {
        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        if (_values.Remove(normalized))
        {
            _changed?.Invoke();
        }
    }

    public string? getPropertyValue(string propertyName)
    {
        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        return _values.TryGetValue(normalized, out var value) ? value : null;
    }
}

public sealed class DomTokenList
{
    private static readonly bool s_disableValueCache =
        string.Equals(Environment.GetEnvironmentVariable("HTMLML_DISABLE_DOM_TOKEN_VALUE_CACHE"), "1", StringComparison.Ordinal);
    private readonly AvaloniaDomElement _element;
    private string? _cachedValue;
    private bool _cachedValueValid;

    public DomTokenList(AvaloniaDomElement element)
    {
        _element = element;
        if (element.Control is StyledElement styled)
        {
            styled.Classes.CollectionChanged += OnClassesChanged;
        }
    }

    private StyledElement? Styled => _element.Control as StyledElement;

    public string value
    {
        get
        {
            if (!s_disableValueCache && _cachedValueValid)
            {
                return _cachedValue!;
            }

            var value = Styled is { } styled
                ? string.Join(' ', styled.Classes.Where(c => !c.StartsWith(':')))
                : string.Empty;
            if (!s_disableValueCache)
            {
                _cachedValue = value;
                _cachedValueValid = true;
            }

            return value;
        }
    }

    public void add(params string[] tokens)
    {
        var styled = Styled;
        if (styled is null)
        {
            return;
        }

        var changed = false;
        string? before = null;
        foreach (var token in SplitTokens(tokens))
        {
            if (!token.StartsWith(':') && !styled.Classes.Contains(token))
            {
                before ??= _element.getAttribute("class");
                styled.Classes.Add(token);
                changed = true;
            }
        }

        if (changed)
        {
            NotifyClassMutation(before);
        }
    }

    public void remove(params string[] tokens)
    {
        var styled = Styled;
        if (styled is null)
        {
            return;
        }

        var changed = false;
        string? before = null;
        foreach (var token in SplitTokens(tokens))
        {
            if (!token.StartsWith(':') && styled.Classes.Contains(token))
            {
                before ??= _element.getAttribute("class");
                styled.Classes.Remove(token);
                changed = true;
            }
        }

        if (changed)
        {
            NotifyClassMutation(before);
        }
    }

    public bool toggle(string token)
        => toggle(token, null);

    public bool toggle(string token, bool? force)
    {
        var styled = Styled;
        if (styled is null || string.IsNullOrWhiteSpace(token) || token.StartsWith(':'))
        {
            return false;
        }

        var contains = styled.Classes.Contains(token);
        var shouldAdd = force ?? !contains;

        if (shouldAdd)
        {
            if (!contains)
            {
                var before = _element.getAttribute("class");
                styled.Classes.Add(token);
                NotifyClassMutation(before);
            }
            return true;
        }

        if (contains)
        {
            var before = _element.getAttribute("class");
            styled.Classes.Remove(token);
            NotifyClassMutation(before);
        }
        return false;
    }

    public bool contains(string token)
    {
        return !token.StartsWith(':') && Styled?.Classes.Contains(token) == true;
    }

    public void SetFromString(string? value)
        => SetFromString(value, notifyAttributeMutation: true);

    internal void SetFromAttribute(string? value)
        => SetFromString(value, notifyAttributeMutation: false);

    private void SetFromString(string? value, bool notifyAttributeMutation)
    {
        var styled = Styled;
        if (styled is null)
        {
            return;
        }

        // React and chart bootstrap code frequently writes the same class
        // attribute repeatedly. Avoid allocating the comparison list/set for
        // the common exact no-op case; whitespace-only input is equivalent to
        // an empty class attribute as well.
        var currentValue = this.value;
        if (string.Equals(currentValue, value ?? string.Empty, StringComparison.Ordinal)
            || (currentValue.Length == 0 && string.IsNullOrWhiteSpace(value)))
        {
            if (notifyAttributeMutation && !_element.HasClassAttribute)
            {
                _element.RaiseClassListMutation(null, currentValue);
            }
            return;
        }

        var nextClasses = new List<string>();
        var nextClassSet = new HashSet<string>(StringComparer.Ordinal);
        foreach (var token in SplitTokens(new[] { value ?? string.Empty }))
        {
            if (!token.StartsWith(':') && nextClassSet.Add(token))
            {
                nextClasses.Add(token);
            }
        }

        var currentClasses = new List<string>();
        foreach (var cls in styled.Classes)
        {
            if (!cls.StartsWith(':'))
            {
                currentClasses.Add(cls);
            }
        }

        if (currentClasses.Count == nextClasses.Count && currentClasses.SequenceEqual(nextClasses, StringComparer.Ordinal))
        {
            return;
        }

        var before = _element.getAttribute("class");
        foreach (var cls in currentClasses)
        {
            styled.Classes.Remove(cls);
        }

        foreach (var cls in nextClasses)
        {
            styled.Classes.Add(cls);
        }

        if (notifyAttributeMutation)
        {
            NotifyClassMutation(before);
        }
    }


    // This is the Avalonia-specialized copy of DomTokenListSemantics.SplitTokens.
    // Keeping the iterator in this assembly preserves the measured class-write
    // path; DOM-only tests define the shared behavior that this fast path follows.
    private static IEnumerable<string> SplitTokens(IEnumerable<string> tokens)
    {
        foreach (var token in tokens)
        {
            if (string.IsNullOrEmpty(token))
            {
                continue;
            }

            foreach (var part in token.Split(new[] { ' ', '\t', '\r', '\n', '\f' }, StringSplitOptions.RemoveEmptyEntries))
            {
                yield return part;
            }
        }
    }

    private void NotifyClassMutation(string? before)
    {
        var after = value;
        _element.RaiseClassListMutation(before, after);
    }

    private void OnClassesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        _cachedValueValid = false;
    }
}

public readonly struct AvaloniaDomDatasetAdapter : IDomDatasetAdapter
{
    private readonly AvaloniaDomElement? _element;
    private readonly DomDocumentElement? _documentElement;

    internal AvaloniaDomDatasetAdapter(AvaloniaDomElement element)
    {
        _element = element;
        _documentElement = null;
    }

    internal AvaloniaDomDatasetAdapter(DomDocumentElement element)
    {
        _element = null;
        _documentElement = element;
    }

    public IReadOnlyDictionary<string, string?> DataAttributes
        => _element?.DataAttributes ?? _documentElement!.DataAttributes;

    public void SetDataAttribute(string attributeName, string? value)
    {
        if (_element is not null) _element.SetDataAttribute(attributeName, value);
        else _documentElement!.SetDataAttribute(attributeName, value);
    }

    public bool TryGetDataAttribute(string attributeName, out string? value)
        => _element is not null
            ? _element.TryGetDataAttribute(attributeName, out value)
            : _documentElement!.TryGetDataAttribute(attributeName, out value);

    public bool RemoveDataAttribute(string attributeName)
        => _element is not null
            ? _element.RemoveDataAttribute(attributeName)
            : _documentElement!.RemoveDataAttribute(attributeName);
}

public sealed class DomStringMap : DomStringMapCore<AvaloniaDomDatasetAdapter>
{
    public DomStringMap(AvaloniaDomElement element)
        : base(new AvaloniaDomDatasetAdapter(element))
    {
    }

    internal DomStringMap(DomDocumentElement element)
        : base(new AvaloniaDomDatasetAdapter(element))
    {
    }
}

public sealed class CssStyleDeclaration : DynamicObject, IHtmlMlCssStyleDeclarationTarget
{
    private readonly AvaloniaDomElement _element;

    public CssStyleDeclaration(AvaloniaDomElement element)
    {
        _element = element;
    }

    public string cssText
    {
        get => _element.GetStyleText();
        set => _element.SetStyleText(value);
    }

    // ClearScript can route direct property access on DynamicObject through
    // TryGet/SetMember before the CLR property. Explicit methods keep the CSSOM
    // cssText path distinct from an authored property named "css-text".
    public string GetCssText() => cssText;

    public void SetCssText(string? value) => _element.SetStyleText(value);

    public int length => _element.StyleValues.Count;

    public string item(int index)
        => index >= 0 && index < _element.StyleValues.Count
            ? _element.StyleValues.ElementAt(index).Key
            : string.Empty;

    public override bool TrySetMember(SetMemberBinder binder, object? value)
    {
        _element.SetStyleProperty(binder.Name, value?.ToString());
        return true;
    }

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        var normalized = NormalizePropertyName(binder.Name);
        if (_element.StyleValues.TryGetValue(normalized, out var value))
        {
            result = value;
            return true;
        }

        result = null;
        return true;
    }

    public void setProperty(string propertyName, string? value)
    {
        _element.SetStyleProperty(propertyName, value);
    }

    public bool supportsPropertyName(string propertyName)
        => CssPropertyCatalog.IsSupported(propertyName);

    void IHtmlMlCssStyleDeclarationTarget.SetProperty(string propertyName, string? value)
        => _element.SetStyleProperty(propertyName, value);

    public void removeProperty(string propertyName)
    {
        _element.SetStyleProperty(propertyName, null);
    }

    public string? getPropertyValue(string propertyName)
    {
        var normalized = NormalizePropertyName(propertyName);
        return _element.StyleValues.TryGetValue(normalized, out var value) ? value : null;
    }

    internal static string NormalizePropertyName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return string.Empty;
        }

        if (char.IsWhiteSpace(name[0]) || char.IsWhiteSpace(name[^1]))
        {
            return string.Empty;
        }

        if (name.StartsWith("--", StringComparison.Ordinal))
        {
            return CssCustomPropertySyntax.IsValidName(name) ? name : string.Empty;
        }

        var containsUppercase = false;
        var containsHyphen = false;
        foreach (var c in name)
        {
            containsUppercase |= char.IsUpper(c);
            containsHyphen |= c == '-';
        }

        // Computed-style reads overwhelmingly use already-normalized CSS names
        // (for example "width" and "border-left-width"). Returning the input
        // avoids a new string and StringBuilder on every JavaScript bridge call.
        if (!containsUppercase)
        {
            return CanonicalizeLegacyGridGapAlias(name);
        }

        if (containsHyphen)
        {
            return CanonicalizeLegacyGridGapAlias(name.ToLowerInvariant());
        }

        var builder = new StringBuilder(name.Length + 4);
        for (var i = 0; i < name.Length; i++)
        {
            var c = name[i];
            if (char.IsUpper(c) && i > 0)
            {
                builder.Append('-');
            }

            builder.Append(char.ToLowerInvariant(c));
        }

        return CanonicalizeLegacyGridGapAlias(builder.ToString());
    }

    private static string CanonicalizeLegacyGridGapAlias(string name)
        => name switch
        {
            "grid-gap" => "gap",
            "grid-row-gap" => "row-gap",
            "grid-column-gap" => "column-gap",
            _ => name
        };

    internal static string ToPropertyName(string cssName)
    {
        if (string.IsNullOrWhiteSpace(cssName))
        {
            return string.Empty;
        }

        var parts = cssName.Split('-', StringSplitOptions.RemoveEmptyEntries);
        var builder = new StringBuilder();
        foreach (var part in parts)
        {
            if (part.Length == 0)
            {
                continue;
            }

            builder.Append(char.ToUpper(part[0], CultureInfo.InvariantCulture));
            if (part.Length > 1)
            {
                builder.Append(part.Substring(1));
            }
        }

        return builder.ToString();
    }

    internal static string ToCamelCase(string cssName)
    {
        var property = ToPropertyName(cssName);
        if (string.IsNullOrEmpty(property))
        {
            return property;
        }

        if (property.Length == 1)
        {
            return property.ToLowerInvariant();
        }

        return char.ToLowerInvariant(property[0]) + property.Substring(1);
    }
}

public sealed class CssComputedStyle : DynamicObject,
    IHtmlMlComputedStyleTarget,
    IHtmlMlComputedStylePropertySupportTarget,
    IHtmlMlLiveComputedStyleTarget
{
    private Dictionary<string, string> _values;
    private readonly Func<string, string>? _valueResolver;
    private readonly Func<Dictionary<string, string>>? _materializer;
    private readonly Func<string, bool>? _isLiveProperty;
    private bool _materialized;

    internal static CssComputedStyle Empty { get; } = new(new Dictionary<string, string>(CssPropertyNameComparer.Instance));

    internal CssComputedStyle(Dictionary<string, string> values)
    {
        // Callers transfer an otherwise private, case-insensitive snapshot.
        // ClearScript consumes IHtmlMlComputedStyleTarget directly, so eagerly
        // cloning it and materializing a second camel-case dictionary made every
        // getComputedStyle read pay for two collections it never enumerated.
        _values = values;
        _materialized = true;
    }

    internal CssComputedStyle(
        Func<string, string> valueResolver,
        Func<Dictionary<string, string>> materializer,
        Func<string, bool>? isLiveProperty = null)
    {
        _values = new Dictionary<string, string>(4, CssPropertyNameComparer.Instance);
        _valueResolver = valueResolver;
        _materializer = materializer;
        _isLiveProperty = isLiveProperty;
    }

    public int length => EnsureMaterialized().Count;

    int IHtmlMlComputedStyleTarget.Length => length;

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        var normalized = CssStyleDeclaration.NormalizePropertyName(binder.Name);
        result = GetResolvedValue(normalized);
        return true;
    }

    public override bool TryGetIndex(GetIndexBinder binder, object[] indexes, out object? result)
    {
        if (indexes.Length == 1 && indexes[0] is string name)
        {
            result = getPropertyValue(name);
            return true;
        }

        result = null;
        return false;
    }

    public string? getPropertyValue(string propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName))
        {
            return string.Empty;
        }

        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        return GetResolvedValue(normalized);
    }

    string IHtmlMlComputedStyleTarget.GetPropertyValue(string propertyName)
        => getPropertyValue(propertyName) ?? string.Empty;

    bool IHtmlMlComputedStylePropertySupportTarget.SupportsPropertyName(string propertyName)
        => CssPropertyCatalog.IsSupported(propertyName);

    bool IHtmlMlLiveComputedStyleTarget.IsPropertyLive(string propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName))
        {
            return false;
        }

        var normalized = CssStyleDeclaration.NormalizePropertyName(propertyName);
        return _isLiveProperty?.Invoke(normalized) == true;
    }

    public string item(int index)
    {
        var values = EnsureMaterialized();
        if (index < 0 || index >= values.Count)
        {
            return string.Empty;
        }

        return values.ElementAt(index).Key;
    }

    string IHtmlMlComputedStyleTarget.Item(int index) => item(index);

    public override IEnumerable<string> GetDynamicMemberNames()
        => EnsureMaterialized().Keys.Select(CssStyleDeclaration.ToCamelCase);

    private string GetResolvedValue(string normalized)
    {
        // A CSSStyleProperties object returned by getComputedStyle is live.
        // Presentation-only transition values therefore cannot be frozen in
        // the per-object cache, including after length/item materializes the
        // complete declaration.
        if (_isLiveProperty?.Invoke(normalized) == true)
        {
            return _valueResolver?.Invoke(normalized) ?? string.Empty;
        }

        if (_values.TryGetValue(normalized, out var value))
        {
            return value;
        }

        value = _valueResolver?.Invoke(normalized) ?? string.Empty;
        _values[normalized] = value;
        return value;
    }

    private Dictionary<string, string> EnsureMaterialized()
    {
        if (_materialized)
        {
            return _values;
        }

        _values = _materializer?.Invoke() ?? _values;
        _materialized = true;
        return _values;
    }
}

public sealed class DomMutationObserver : DomMutationObserverCore<AvaloniaDomElement>
{
    private readonly AvaloniaDomDocument _document;
    private readonly IExternalJavaScriptCallback _externalCallback;
    private object? _externalObserver;

    internal DomMutationObserver(
        AvaloniaDomDocument document,
        IExternalJavaScriptCallback callback)
    {
        _document = document ?? throw new ArgumentNullException(nameof(document));
        _externalCallback = callback ?? throw new ArgumentNullException(nameof(callback));
        _document.RegisterMutationObserver(this);
    }

    public void observe(object target)
        => __htmlMlObserve(target, childList: true, attributes: false, subtree: false, attributeOldValue: false);

    public void __htmlMlObserve(
        object target,
        bool childList,
        bool attributes,
        bool subtree,
        bool attributeOldValue)
    {
        if (target is not AvaloniaDomElement element)
        {
            return;
        }

        ObserveCore(
            element,
            childList,
            attributes,
            subtree,
            attributeOldValue);
        _document.RegisterMutationObserver(this);
    }

    public void disconnect()
    {
        DisconnectCore();
        _document.UnregisterMutationObserver(this);
    }

    public object[] takeRecords() => TakeRecordsCore();

    public void __htmlMlSetExternalObserver(object observer)
    {
        _externalObserver = observer;
    }

    internal bool TryQueue(DomMutationRecord record)
    {
        if (Observations.Count == 0)
        {
            return false;
        }

        if (record.TargetElement is not AvaloniaDomElement targetElement)
        {
            return false;
        }

        var matched = false;
        var includeOldValue = false;

        foreach (var observation in Observations)
        {
            if (!observation.Options.MatchesRecordType(record.type))
            {
                continue;
            }

            if (ReferenceEquals(targetElement, observation.Target) ||
                (observation.Options.Subtree && IsDescendantOf(targetElement, observation.Target)))
            {
                matched = true;
                includeOldValue |= observation.Options.AttributeOldValue;
            }
        }

        if (!matched)
        {
            return false;
        }

        QueueRecordCore(record, includeOldValue);
        return true;
    }

    internal bool ObservesAttributeMutation(
        AvaloniaDomElement targetElement,
        out bool includeOldValue)
    {
        includeOldValue = false;
        var matched = false;
        foreach (var observation in Observations)
        {
            if (!observation.Options.Attributes)
            {
                continue;
            }

            if (ReferenceEquals(targetElement, observation.Target)
                || (observation.Options.Subtree && IsDescendantOf(targetElement, observation.Target)))
            {
                matched = true;
                includeOldValue |= observation.Options.AttributeOldValue;
            }
        }

        return matched;
    }

    internal void Deliver()
    {
        if (QueuedRecordCount == 0)
        {
            return;
        }

        var records = DrainRecordsCore();

        try
        {
            _externalCallback.Invoke(null, records, _externalObserver ?? this);
        }
        catch (Exception exception)
        {
            if (_document.DiagnosticLoggingEnabled)
            {
                Console.Error.WriteLine($"External MutationObserver callback failed: {exception}");
            }
        }
    }

    private static bool IsDescendantOf(AvaloniaDomElement candidate, AvaloniaDomElement ancestor)
    {
        for (var current = candidate; current is not null; current = current.parentElement)
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }
        }

        return false;
    }
}

public sealed class DomRect
{
    public DomRect(Rect rect)
    {
        x = rect.X;
        y = rect.Y;
        width = rect.Width;
        height = rect.Height;
        left = rect.Left;
        top = rect.Top;
        right = rect.Right;
        bottom = rect.Bottom;
    }

    public double x { get; }

    public double y { get; }

    public double width { get; }

    public double height { get; }

    public double left { get; }

    public double top { get; }

    public double right { get; }

    public double bottom { get; }
}

public sealed class DomAttribute
{
    public DomAttribute(string name, string value, AvaloniaDomDocument? ownerDocument = null)
    {
        this.name = name;
        localName = name;
        nodeName = name;
        this.value = value;
        nodeValue = value;
        this.ownerDocument = ownerDocument;
    }

    public int nodeType => 2;

    public AvaloniaDomDocument? ownerDocument { get; }

    public string name { get; }

    public string localName { get; }

    public string nodeName { get; }

    public string value { get; }

    public string nodeValue { get; }
}

public sealed class DomComment : DomNodeCore
{
    private string _data;

    public DomComment(string data, AvaloniaDomDocument ownerDocument)
    {
        _data = data ?? string.Empty;
        this.ownerDocument = ownerDocument;
    }

    public int nodeType => 8;

    public string nodeName => "#comment";

    public AvaloniaDomDocument ownerDocument { get; }

    public object? parentNode => null;

    public string data
    {
        get => _data;
        set => _data = value ?? string.Empty;
    }

    public string nodeValue
    {
        get => _data;
        set => _data = value ?? string.Empty;
    }

    public string textContent
    {
        get => _data;
        set => _data = value ?? string.Empty;
    }
}

public class DomRange : DomRangeCore
{
    private readonly AvaloniaDomDocument _document;

    public DomRange(AvaloniaDomDocument document)
    {
        _document = document;
    }

    protected override object? CreateContextualContainer()
        => _document.createElement("div") as AvaloniaDomElement;

    protected override void SetInnerHtml(object container, string html)
        => ((AvaloniaDomElement)container).innerHTML = html;

    protected override object CreateFragment(object container)
        => new DomDocumentFragment((AvaloniaDomElement)container);
}

public class DomDocumentFragment : DomDocumentFragmentCore<AvaloniaDomElement>
{
    private readonly AvaloniaDomElement _container;

    public DomDocumentFragment(AvaloniaDomElement container)
        : base(container)
    {
        _container = container;
    }

    public void append(params object?[] nodes) => _container.append(nodes);

    public string? textContent
    {
        get => _container.textContent;
        set => _container.textContent = value;
    }
}

public sealed class DomParsedDocument : DomParsedDocumentCore<AvaloniaDomElement>
{
    private readonly AvaloniaDomDocument _document;
    private readonly AvaloniaDomElement? _head;
    private readonly bool _xmlMode;
    private DomDocumentImplementation? _implementation;

    public DomParsedDocument(
        AvaloniaDomDocument document,
        AvaloniaDomElement? documentElement,
        AvaloniaDomElement? body = null,
        AvaloniaDomElement? head = null,
        bool xmlMode = false)
        : base(documentElement, body)
    {
        _document = document;
        _head = head;
        _xmlMode = xmlMode;
    }

    public int nodeType => 9;

    public string nodeName => "#document";

    public object? defaultView => null;

    public object? location => null;

    public string cookie
    {
        get => string.Empty;
        set { }
    }

    public AvaloniaDomElement? head => _head;

    public object? createElement(string tagName)
        => _xmlMode ? _document.CreateXmlElement(tagName) : _document.createElement(tagName);

    public object? createElementNS(string? namespaceUri, string qualifiedName)
        => _xmlMode
            ? _document.CreateXmlElement(qualifiedName, namespaceUri)
            : _document.createElementNS(namespaceUri, qualifiedName);

    public object? createTextNode(string data) => _document.createTextNode(data);

    public DomComment createComment(string data) => _document.createComment(data);

    public DomAttribute createAttribute(string name) => _document.createAttribute(name);

    public object? createDocumentFragment() => _document.createDocumentFragment();

    public DomDocumentImplementation implementation => _implementation ??= new(_document);
}

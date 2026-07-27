using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;

namespace JavaScript.Avalonia;

/// <summary>
/// Document-scoped stylesheet registry and cascade engine.  This intentionally
/// owns browser-facing CSS state instead of translating stylesheet rules into
/// Avalonia styles: layout, getComputedStyle, and presentation all consume the
/// same winning declarations.
/// </summary>
internal sealed class CssStyleEngine
{
    private const int CascadeWinnerCapacity = 32;
    private const int ComputedPropertyCapacity = 64;
    private const int PropertyCapacityHeadroom = 16;
    private const int MaximumRetainedStyleScratchCapacity = 512;
    private const int MaximumMatchedRuleCacheEntries = 2048;
    private const int MaximumSharedOrdinaryStyleEntries = 1024;
    private const int OrdinaryStyleProbationSlotCount = 2048;
    private const int MaximumCascadeTemplateEntries = 1024;
    private const int CascadeTemplateProbationSlotCount = 2048;
    private const int MaximumCompiledStylesheetCacheEntries = 256;
    private const int MaximumCompiledStylesheetCacheCharacters = 8 * 1024 * 1024;
    private const int MaximumSingleCompiledStylesheetCharacters = 2 * 1024 * 1024;
    private static readonly bool s_profileCss =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_CSS_PROFILE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableCandidateRuleScratch =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_CANDIDATE_SCRATCH"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableSharedCustomProperties =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_SHARED_CUSTOM_PROPERTIES"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableComputedStyleScratch =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_COMPUTED_STYLE_SCRATCH"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableCascadeWinnerScratch =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_WINNER_SCRATCH"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableOrderedWinnerScratch =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_ORDERED_WINNER_SCRATCH"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableMatchedRuleCache =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_MATCHED_RULE_CACHE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableAppendOnlyStylesheetUpdates =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_APPEND_ONLY_STYLESHEET_UPDATES"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableScopedAppendStylesheetCascade =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_SCOPED_APPEND_STYLESHEET_CASCADE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableScopedClassInvalidation =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_SCOPED_CLASS_INVALIDATION"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableCustomPropertyConsumerInvalidation =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CUSTOM_PROPERTY_CONSUMER_INVALIDATION"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableInlineStyleClassificationSpans =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_INLINE_STYLE_CLASSIFICATION_SPANS"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableOrdinaryStyleSharing =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_STYLE_SHARING"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableCascadeTemplateCache =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_CASCADE_TEMPLATE_CACHE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableStylesheetNormalizationGuards =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_STYLESHEET_NORMALIZATION_GUARDS"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableCompiledStylesheetCache =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_COMPILED_STYLESHEET_CACHE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableMediaQueryOutcomeCache =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_MEDIA_QUERY_OUTCOME_CACHE"), "1", StringComparison.Ordinal);
    private static readonly bool s_disableViewportPresentationChangeSet =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_VIEWPORT_PRESENTATION_CHANGE_SET"), "1", StringComparison.Ordinal);
    private static readonly bool s_traceInlineStyles =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_TRACE_INLINE_STYLES"), "1", StringComparison.Ordinal);
    private static readonly bool s_traceChildListInvalidation =
        string.Equals(Environment.GetEnvironmentVariable("WEBSCENE_TRACE_CSS_INVALIDATIONS"), "1", StringComparison.Ordinal);
    private static readonly object s_compiledStylesheetCacheGate = new();
    private static readonly Dictionary<string, CachedStyleSheet> s_compiledStylesheetCache =
        new(StringComparer.Ordinal);
    private static readonly Queue<string> s_compiledStylesheetCacheInsertionOrder = new();
    private static int s_compiledStylesheetCacheCharacters;
    private static readonly IReadOnlySet<string> s_inheritedProperties =
        CssMutationInvalidationPlanner.InheritedProperties;
    private static readonly string[] s_svgPresentationAttributeProperties =
    [
        "clip-rule", "color", "fill", "fill-opacity", "fill-rule", "opacity", "stroke",
        "stroke-linecap", "stroke-linejoin", "stroke-opacity", "stroke-width"
    ];

    private readonly AvaloniaDomDocument _document;
    private readonly List<CascadeRule> _rules = new();
    private readonly List<CascadeRule> _childListAncestorRules = new();
    private readonly List<CascadeRule> _siblingCombinatorRules = new();

    // Rightmost-selector indexes for fast rule candidate selection.
    // Most stylesheet rules have a simple tag, #id or .class as their subject (rightmost) selector.
    // Bucketing by those lets us skip the vast majority of non-matching rules on each element.
    private readonly Dictionary<string, List<CascadeRule>> _rulesByTag = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<CascadeRule>> _rulesById = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<CascadeRule>> _rulesByClass = new(StringComparer.Ordinal);
    private readonly List<CascadeRule> _universalRules = new(); // *, [attr], or complex rightmost we conservatively always test
    private readonly Dictionary<string, List<CascadeRule>> _pseudoRulesByTag = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, List<CascadeRule>> _pseudoRulesById = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<CascadeRule>> _pseudoRulesByClass = new(StringComparer.Ordinal);
    private readonly List<CascadeRule> _pseudoUniversalRules = new();
    // CSS work is serialized per document on the UI thread. Reuse this scratch
    // set instead of allocating one candidate de-duplication set per element
    // on every cascade pass.
    private readonly HashSet<CascadeRule> _candidateRuleScratch = new();
    // Cascade work is serialized per document. Keep bounded detached style
    // collections so the next element can reuse their backing storage.
    // Custom-property overlays remain immutable and shared.
    private CssPropertyValueStore? _computedValueScratch;
    private CssPropertyNameSet? _declaredPropertyScratch;
    private readonly Dictionary<int, List<SharedOrdinaryStyle>> _sharedOrdinaryStyles = new();
    private readonly uint[] _ordinaryStyleProbation = new uint[OrdinaryStyleProbationSlotCount];
    private int _sharedOrdinaryStyleEntryCount;
    private readonly Dictionary<int, List<CascadeTemplate>> _cascadeTemplates = new();
    private readonly uint[] _cascadeTemplateProbation = new uint[CascadeTemplateProbationSlotCount];
    private readonly uint[] _cascadeTemplateSecondaryProbation = new uint[CascadeTemplateProbationSlotCount];
    private int _cascadeTemplateEntryCount;
    private Dictionary<string, CascadeWinner>? _cascadeWinnerScratch;
    private List<KeyValuePair<string, CascadeWinner>>? _orderedWinnerScratch;
    private readonly Dictionary<AvaloniaDomElement, CachedStyleSheet> _parsedStyleSheets = new();
    private List<StylesheetInput>? _lastStylesheetInputs;
    private string? _lastBaseHref;
    private bool _hasViewportDependentMediaQueries;
    private readonly Dictionary<string, bool> _mediaQueryOutcomes = new(StringComparer.Ordinal);
    private double _lastMediaViewportWidth = -1;
    private double _lastMediaViewportHeight = -1;
    private double _lastMediaDevicePixelRatio = -1;
    private bool _viewportReconciliationPending;

    private readonly Dictionary<AvaloniaDomElement, string> _loadedLinks = new();
    private bool _stylesheetsDirty = true;
    private bool _stylesDirty = true;
    private bool _fullStylesDirty = true;
    private bool _documentWideStylesDirty = true;
    private readonly HashSet<AvaloniaDomElement> _dirtyRoots = new();
    // Custom-property-only inline mutations do not necessarily require a full
    // cascade for every descendant. Their inherited custom-property maps must
    // still be rebased, but only var() consumers (or descendants of a changed
    // inherited ordinary value) need selector/cascade/presentation work.
    private readonly HashSet<AvaloniaDomElement> _dirtyCustomPropertyRoots = new();
    private readonly HashSet<AvaloniaDomElement> _dirtyElements = new();
    private readonly HashSet<AvaloniaDomElement> _dirtyClassElements = new();
    // Class mutations are a distinct transaction source from child-list and
    // dynamic-state direct work. A same-task lazy stylesheet reload may
    // supersede the latter queues, but must retain an already queued class
    // removal so append-only reload cannot strand a measuring presentation.
    private readonly HashSet<AvaloniaDomElement> _pendingClassMutationElements = new();
    // React filters keyed lists by issuing many removeChild calls in one UI
    // turn. Preserve the precise invalidation path for a single removal, then
    // collapse later removals from the same parent into one subtree cascade.
    // This avoids repeatedly walking every remaining sibling for structural
    // selectors while keeping :first/:last/:nth and sibling combinators sound.
    private readonly HashSet<AvaloniaDomElement> _pendingRemovalTargets = new();
    private readonly HashSet<AvaloniaDomElement> _batchedRemovalTargets = new();
    private readonly HashSet<string> _ancestorSensitiveClasses = new(StringComparer.Ordinal);
    private readonly List<CssAttributeSelector> _ancestorSensitiveClassAttributeSelectors = new();
    private readonly HashSet<string> _explicitlyInheritedProperties = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> _inlineExplicitlyInheritedProperties = new(StringComparer.OrdinalIgnoreCase);
    // Inline style changes cannot alter stylesheet selector matches unless a
    // selector reads the style attribute. Cache the static matched-rule subset
    // lazily for those updates, while re-evaluating stateful pseudo rules and
    // still running the complete cascade and native presentation for every
    // affected descendant.
    private readonly Dictionary<AvaloniaDomElement, MatchedRuleCacheEntry> _matchedRuleCache = new();
    private CssComputedValues? _documentElementComputedValues;
    private CssDeclaredPropertySet? _documentElementDeclaredProperties;
    private bool _isComputing;
    private string? _baseHref;
    private bool _hasStyleAttributeSelectors;
    private bool _hasBroadExplicitInheritance;
    private bool _hasBroadInlineExplicitInheritance;
    private bool _reuseMatchedRulesForDirtyPass;
    private int _pseudoElementRuleCount;

    public CssStyleEngine(AvaloniaDomDocument document)
    {
        _document = document;
    }

    public int RuleCount => _rules.Count;

    internal int StylesheetParseCount { get; private set; }

    internal long CompiledStylesheetCacheHitCount { get; private set; }

    internal long MediaQueryOutcomeCacheHitCount { get; private set; }

    internal long ViewportPresentationReapplyElementCount { get; private set; }

    internal static int CompiledStylesheetCacheEntryCount
    {
        get
        {
            lock (s_compiledStylesheetCacheGate)
            {
                return s_compiledStylesheetCache.Count;
            }
        }
    }

    internal int StyleRecomputeCount { get; private set; }

    internal long ElementStyleComputeCount { get; private set; }

    internal long ElementStyleApplyCount { get; private set; }

    internal long SharedOrdinaryStyleHitCount { get; private set; }

    internal int SharedOrdinaryStyleEntryCount => _sharedOrdinaryStyleEntryCount;

    internal long CascadeTemplateHitCount { get; private set; }

    internal int CascadeTemplateEntryCount => _cascadeTemplateEntryCount;

    internal long SelectorMatchEvaluationCount { get; private set; }

    internal long MatchedRuleCacheHitCount { get; private set; }

    internal long ScopedClassInvalidationCount { get; private set; }

    internal long ClassInvalidationFallbackCount { get; private set; }

    internal long ClassInvalidationPropagationCount { get; private set; }

    internal long InheritedCursorRebaseElementCount { get; private set; }

    internal long InheritedPropagationPrunedElementCount { get; private set; }

    internal long AppendStylesheetCandidateEvaluationCount { get; private set; }

    internal long EnsureCurrentTicks { get; private set; }

    internal long EnsureCurrentAllocatedBytes { get; private set; }

    internal long StylesheetNormalizationTicks { get; private set; }

    internal long StylesheetNormalizationAllocatedBytes { get; private set; }

    internal long StylesheetParserTicks { get; private set; }

    internal long StylesheetParserAllocatedBytes { get; private set; }

    internal long StylesheetRuleCompilationTicks { get; private set; }

    internal long StylesheetRuleCompilationAllocatedBytes { get; private set; }

    internal long StylesheetIndexingTicks { get; private set; }

    internal long StylesheetIndexingAllocatedBytes { get; private set; }

    internal long ElementStyleCascadeTicks { get; private set; }

    internal long ElementStyleCascadeAllocatedBytes { get; private set; }

    internal long ElementStyleRuleMatchTicks { get; private set; }

    internal long ElementStyleRuleMatchAllocatedBytes { get; private set; }

    internal long ElementStyleValueInitializationTicks { get; private set; }

    internal long ElementStyleValueInitializationAllocatedBytes { get; private set; }

    internal long ElementStyleResolutionTicks { get; private set; }

    internal long ElementStyleResolutionAllocatedBytes { get; private set; }

    internal long ElementStyleCommitTicks { get; private set; }

    internal long ElementStyleCommitAllocatedBytes { get; private set; }

    internal long PseudoElementTicks { get; private set; }

    internal long PseudoElementAllocatedBytes { get; private set; }

    internal bool HasPendingWork => _stylesheetsDirty || _stylesDirty;

    internal bool PendingDirtyRootCovers(AvaloniaDomElement target)
    {
        if (_documentWideStylesDirty
            || _fullStylesDirty && !_stylesheetsDirty)
        {
            return true;
        }
        for (var current = target; current is not null; current = current.parentElement)
        {
            if (_dirtyRoots.Contains(current))
            {
                return true;
            }
        }
        return false;
    }

    private bool TryGetPendingDirtyAncestor(
        AvaloniaDomElement target,
        out AvaloniaDomElement dirtyAncestor)
    {
        for (var current = target.parentElement; current is not null; current = current.parentElement)
        {
            if (_dirtyRoots.Contains(current))
            {
                dirtyAncestor = current;
                return true;
            }
        }

        dirtyAncestor = null!;
        return false;
    }

    public string? BaseHref => _baseHref;

    internal CssComputedStyle GetDocumentElementComputedStyle()
        => _documentElementComputedValues is null
            ? CssComputedStyle.Empty
            : new CssComputedStyle(new Dictionary<string, string>(
                _documentElementComputedValues,
                CssPropertyNameComparer.Instance));

    public void Invalidate(AvaloniaDomElement? target = null, bool stylesheetsChanged = false)
    {
        // Mutations on a detached subtree cannot affect the connected document.
        // When that subtree is attached, the child-list mutation on its connected
        // parent invalidates and computes the entire newly connected branch.
        if (target is not null
            && !stylesheetsChanged
            && !_document.IsConnectedStyleElement(target))
        {
            return;
        }

        ClearMatchedRuleCache();

        _stylesDirty = true;
        _stylesheetsDirty |= stylesheetsChanged;
        if (target is null)
        {
            _fullStylesDirty = true;
            _documentWideStylesDirty |= !stylesheetsChanged;
            // A full stylesheet reload subsumes broad subtree/direct queues.
            // Retain only explicit class mutations: an append-only lazy load
            // may downgrade this request to a scoped cascade, and must not
            // discard a same-task measuring-class removal.
            _dirtyRoots.Clear();
            _dirtyCustomPropertyRoots.Clear();
            _dirtyElements.Clear();
            _dirtyClassElements.Clear();
            if (stylesheetsChanged)
            {
                _dirtyClassElements.UnionWith(_pendingClassMutationElements);
            }
            else
            {
                _pendingClassMutationElements.Clear();
            }
            return;
        }

        if (stylesheetsChanged)
        {
            // A later DOM mutation in the same transaction may add a dirty
            // branch. Preserve existing roots as well: an append-only
            // stylesheet update can now avoid a document-wide cascade.
            _fullStylesDirty = true;
            return;
        }

        // Keep dirty roots even when a previous stylesheet mutation requested a
        // full pass. If that mutation turns out to have the same effective CSS,
        // the roots still need their incremental recompute.
        AddDirtyRoot(target);
    }

    internal void InvalidateViewportStyles()
    {
        // Defer the media decision until EnsureCurrent, when the final viewport
        // bounds for this resize reconciliation are available. Do not discard
        // matched-rule or cascade-template caches unless a media-query outcome
        // actually changes.
        _viewportReconciliationPending = true;
        _stylesDirty = true;
        _stylesheetsDirty = true;
        _fullStylesDirty = true;
    }

    internal void InvalidateInlineStyle(AvaloniaDomElement target, string? oldStyle, string? newStyle)
    {
        var portablePlan = CssMutationInvalidationPlanner.PlanInlineStyle(oldStyle, newStyle);
        var customPropertyOnly = IsCustomPropertyOnlyMutation(target, oldStyle);
        if (!customPropertyOnly)
        {
            TrackInlineExplicitInheritance(oldStyle);
            TrackInlineExplicitInheritance(newStyle);
        }
        var inherited = customPropertyOnly
                        || portablePlan.Affects(CssMutationInvalidationScope.Descendants);
        var targetOnlyInlineStyles = _document.TargetOnlyInlineStylesEnabled;
        var fallback = !targetOnlyInlineStyles
                       || _stylesheetsDirty
                       || _fullStylesDirty
                       || _hasStyleAttributeSelectors;
        if (s_traceInlineStyles && targetOnlyInlineStyles)
        {
            Console.WriteLine($"[CSS INLINE] target={target.localName} id={target.id} classes={target.className} " +
                              $"fallback={fallback} inherited={inherited} old={oldStyle} new={newStyle}");
        }

        if (fallback)
        {
            Invalidate(target);
            return;
        }

        if (!_document.IsConnectedStyleElement(target))
        {
            return;
        }

        _stylesDirty = true;
        if (!_reuseMatchedRulesForDirtyPass
            && _dirtyRoots.Count == 0
            && _dirtyCustomPropertyRoots.Count == 0
            && _dirtyClassElements.Count == 0)
        {
            _reuseMatchedRulesForDirtyPass = true;
        }
        if (customPropertyOnly && !s_disableCustomPropertyConsumerInvalidation)
        {
            AddDirtyCustomPropertyRoot(target);
        }
        else if (inherited)
        {
            AddDirtyRoot(target);
        }
        else
        {
            _dirtyElements.Add(target);
        }
        if (portablePlan.Affects(CssMutationInvalidationScope.Layout))
        {
            _document.InvalidateLayoutFromStyleMutation();
        }
    }

    internal void InvalidateInlineStyleProperty(
        AvaloniaDomElement target,
        string property,
        string? oldValue,
        string? newValue)
    {
        if (string.Equals(oldValue, newValue, StringComparison.Ordinal))
        {
            return;
        }

        var customProperty = property.StartsWith("--", StringComparison.Ordinal);
        if (!customProperty
            && (IsExplicitInherit(oldValue) || IsExplicitInherit(newValue)))
        {
            TrackInlineExplicitInheritanceProperty(property);
        }

        var inherited = customProperty
                        || string.Equals(property, "all", StringComparison.Ordinal)
                        || OrdinaryPropertyMayPropagateToDescendants(property);
        var targetOnlyInlineStyles = _document.TargetOnlyInlineStylesEnabled;
        var fallback = !targetOnlyInlineStyles
                       || _stylesheetsDirty
                       || _fullStylesDirty
                       || _hasStyleAttributeSelectors;
        if (fallback)
        {
            Invalidate(target);
            return;
        }

        if (!_document.IsConnectedStyleElement(target))
        {
            return;
        }

        _stylesDirty = true;
        if (!_reuseMatchedRulesForDirtyPass
            && _dirtyRoots.Count == 0
            && _dirtyCustomPropertyRoots.Count == 0
            && _dirtyClassElements.Count == 0)
        {
            _reuseMatchedRulesForDirtyPass = true;
        }
        if (customProperty && !s_disableCustomPropertyConsumerInvalidation)
        {
            AddDirtyCustomPropertyRoot(target);
        }
        else if (inherited)
        {
            AddDirtyRoot(target);
        }
        else
        {
            _dirtyElements.Add(target);
        }
        if (IsLayoutStyleProperty(property.AsSpan()))
        {
            _document.InvalidateLayoutFromStyleMutation();
        }
    }

    private static bool IsExplicitInherit(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var span = value.AsSpan().Trim();
        if (span.EndsWith("!important", StringComparison.OrdinalIgnoreCase))
        {
            span = span[..^"!important".Length].TrimEnd();
        }
        return span.Equals("inherit", StringComparison.OrdinalIgnoreCase);
    }

    internal void InvalidateChildList(
        AvaloniaDomElement target,
        IReadOnlyList<AvaloniaDomElement>? addedNodes,
        IReadOnlyList<AvaloniaDomElement>? removedNodes,
        AvaloniaDomElement? previousSibling,
        AvaloniaDomElement? nextSibling)
    {
        var appendAtEnd = addedNodes is { Count: > 0 }
                          && removedNodes is not { Count: > 0 }
                          && nextSibling is null;
        var removalOnly = removedNodes is { Count: > 0 }
                          && addedNodes is not { Count: > 0 };
        if (!appendAtEnd && removalOnly
            && !_documentWideStylesDirty
            && !(_fullStylesDirty && !_stylesheetsDirty))
        {
            if (_pendingRemovalTargets.Add(target))
            {
                InvalidateRemoval(target, removedNodes!, previousSibling, nextSibling);
                // Even a selector-neutral removal needs a cheap style turn so
                // this transaction marker is cleared before a later mutation.
                _stylesDirty = true;
            }
            else
            {
                foreach (var removed in removedNodes!)
                {
                    RemoveMatchedRuleCacheSubtree(removed);
                }
                if (_batchedRemovalTargets.Add(target))
                {
                    RemoveMatchedRuleCacheSubtree(target);
                    AddDirtyRoot(target);
                }
                _stylesDirty = true;
                _reuseMatchedRulesForDirtyPass = true;
                _document.InvalidateLayoutFromStyleMutation();
            }
            return;
        }

        if (!appendAtEnd
            || _documentWideStylesDirty
            || (_fullStylesDirty && !_stylesheetsDirty))
        {
            Invalidate(target);
            return;
        }

        if (!_document.IsConnectedStyleElement(target))
        {
            return;
        }

        var styleAffected = false;
        _reuseMatchedRulesForDirtyPass = true;

        foreach (var added in addedNodes!)
        {
            RemoveMatchedRuleCacheSubtree(added);
            if (added is AvaloniaDomTextNode)
            {
                added.ApplyInheritedTextPresentationFrom(target);
            }
            else
            {
                AddDirtyRoot(added);
                styleAffected = true;
            }
        }

        // Appending cannot change forward child positions or the selector
        // context of an existing element. It can only make the parent non-empty
        // and change :last-*/:only-*/:nth-last-* state on older siblings.
        if (previousSibling is null
            && (CollectCandidateRules(target).Any(rule => rule.Selector.RightmostDependsOnEmpty)
                || CollectCandidateRules(target, pseudoElements: true)
                    .Any(rule => rule.Selector.RightmostDependsOnEmpty)))
        {
            _matchedRuleCache.Remove(target);
            _dirtyClassElements.Add(target);
            styleAffected = true;
        }

        var previousElement = previousSibling?.nodeType == 1
            ? previousSibling
            : previousSibling?.previousElementSibling;
        for (var sibling = previousElement; sibling is not null; sibling = sibling.previousElementSibling)
        {
            var directAffected = false;
            var hasCachedRules = _matchedRuleCache.TryGetValue(sibling, out var cachedRules);
            foreach (var rule in CollectCandidateRules(sibling))
            {
                if (!rule.Selector.RightmostDependsOnAppendAtEnd) continue;
                if (!hasCachedRules
                    ? rule.Selector.CouldMatchIgnoringChildList(sibling, _document)
                    : cachedRules!.StaticMatchedRules.Contains(rule) != rule.Selector.Matches(sibling, _document))
                {
                    directAffected = true;
                    break;
                }
            }
            if (!directAffected)
            {
                foreach (var rule in CollectCandidateRules(sibling, pseudoElements: true))
                {
                    if (!rule.Selector.RightmostDependsOnAppendAtEnd
                        || rule.Selector.PseudoElementName is not { } pseudoName) continue;
                    if (!hasCachedRules
                        ? rule.Selector.CouldMatchPseudoElementIgnoringChildList(sibling, _document, pseudoName)
                        : cachedRules!.PseudoMatchedRules.Contains(rule)
                          != rule.Selector.MatchesPseudoElement(sibling, _document, pseudoName))
                    {
                        directAffected = true;
                        break;
                    }
                }
            }
            var descendantAffected = _childListAncestorRules.Any(rule =>
                rule.Selector.AncestorAppendAtEndDependencyCouldMatch(sibling, _document));
            if (descendantAffected)
            {
                RemoveMatchedRuleCacheSubtree(sibling);
                AddDirtyRoot(sibling);
                styleAffected = true;
            }
            else if (directAffected)
            {
                _matchedRuleCache.Remove(sibling);
                _dirtyClassElements.Add(sibling);
                styleAffected = true;
            }
        }

        _stylesDirty |= styleAffected;

        _document.InvalidateLayoutFromStyleMutation();
    }

    private void InvalidateRemoval(
        AvaloniaDomElement target,
        IReadOnlyList<AvaloniaDomElement> removedNodes,
        AvaloniaDomElement? previousSibling,
        AvaloniaDomElement? nextSibling)
    {
        if (!_document.IsConnectedStyleElement(target))
        {
            return;
        }

        foreach (var removed in removedNodes)
        {
            RemoveMatchedRuleCacheSubtree(removed);
        }

        var affected = false;
        var directCount = 0;
        var rootCount = 0;
        void MarkDirect(AvaloniaDomElement element)
        {
            _matchedRuleCache.Remove(element);
            _dirtyClassElements.Add(element);
            affected = true;
            directCount++;
        }
        void MarkRoot(AvaloniaDomElement element)
        {
            RemoveMatchedRuleCacheSubtree(element);
            AddDirtyRoot(element);
            affected = true;
            rootCount++;
        }
        void MarkPositionAffected(AvaloniaDomElement sibling, bool fromStart)
        {
            var direct = false;
            var hasCachedRules = _matchedRuleCache.TryGetValue(sibling, out var cachedRules);
            foreach (var rule in CollectCandidateRules(sibling))
            {
                if (!rule.Selector.RightmostDependsOnChildPosition(fromStart)) continue;
                if (!hasCachedRules)
                {
                    if (rule.Selector.CouldMatchIgnoringChildList(sibling, _document))
                    {
                        direct = true;
                        break;
                    }
                    continue;
                }

                var matchedBefore = cachedRules!.StaticMatchedRules.Contains(rule);
                var matchesNow = rule.Selector.Matches(sibling, _document);
                if (matchedBefore != matchesNow)
                {
                    direct = true;
                    break;
                }
            }
            if (!direct)
            {
                foreach (var rule in CollectCandidateRules(sibling, pseudoElements: true))
                {
                    if (!rule.Selector.RightmostDependsOnChildPosition(fromStart)
                        || rule.Selector.PseudoElementName is not { } pseudoName) continue;
                    if (!hasCachedRules
                        ? rule.Selector.CouldMatchPseudoElementIgnoringChildList(sibling, _document, pseudoName)
                        : cachedRules!.PseudoMatchedRules.Contains(rule)
                          != rule.Selector.MatchesPseudoElement(sibling, _document, pseudoName))
                    {
                        direct = true;
                        break;
                    }
                }
            }
            var descendants = _childListAncestorRules.Any(rule =>
                rule.Selector.AncestorChildPositionDependencyCouldMatch(
                    sibling,
                    _document,
                    fromStart));
            if (descendants) MarkRoot(sibling);
            else if (direct) MarkDirect(sibling);
        }

        // Removing children changes :empty on the parent and shifts forward
        // positions after the hole and backward positions before it. Recompute
        // only selector subjects whose child-list pseudos can observe that
        // shift, rather than recascading the complete (often 150-row) list.
        if (!target.GetChildElements().Any()
            && (CollectCandidateRules(target).Any(rule => rule.Selector.RightmostDependsOnEmpty)
                || CollectCandidateRules(target, pseudoElements: true)
                    .Any(rule => rule.Selector.RightmostDependsOnEmpty)))
        {
            MarkDirect(target);
        }

        var previousElement = previousSibling?.nodeType == 1
            ? previousSibling
            : previousSibling?.previousElementSibling;
        var nextElement = nextSibling?.nodeType == 1
            ? nextSibling
            : nextSibling?.nextElementSibling;
        for (var sibling = previousElement; sibling is not null; sibling = sibling.previousElementSibling)
        {
            MarkPositionAffected(sibling, fromStart: false);
        }
        for (var sibling = nextElement; sibling is not null; sibling = sibling.nextElementSibling)
        {
            MarkPositionAffected(sibling, fromStart: true);
        }

        // Adjacent-sibling selectors can only change at the newly formed seam.
        // General-sibling selectors are considered for later siblings, but the
        // removed node's left-hand selector must plausibly match first.
        if (nextElement is not null)
        {
            for (var sibling = nextElement; sibling is not null; sibling = sibling.nextElementSibling)
            {
                var isAdjacent = ReferenceEquals(sibling, nextElement);
                var siblingAffected = removedNodes.Any(removed =>
                    _siblingCombinatorRules.Any(rule =>
                        rule.Selector.RemovedSiblingCouldAffectSubtree(
                            removed,
                            sibling,
                            _document,
                            allowAdjacent: isAdjacent)));
                if (siblingAffected)
                {
                    MarkRoot(sibling);
                }
            }
        }

        if (affected)
        {
            _stylesDirty = true;
            _reuseMatchedRulesForDirtyPass = true;
        }
        if (s_traceChildListInvalidation)
        {
            Console.WriteLine(
                $"[CSS INVALIDATE] removal-scope target={target.localName}.{target.className.Replace(' ', '.')} " +
                $"direct={directCount} roots={rootCount} affected={affected}");
        }
        _document.InvalidateLayoutFromStyleMutation();
    }

    internal void InvalidateClass(
        AvaloniaDomElement target,
        string? oldClassName,
        string? newClassName)
    {
        if (!_document.IsConnectedStyleElement(target))
        {
            return;
        }

        _pendingClassMutationElements.Add(target);
        var pendingDirtyAncestor = TryGetPendingDirtyAncestor(target, out var dirtyAncestor);

        // Connected UI branches are commonly appended and then have state
        // classes applied to their descendants before the dispatcher drains.
        // The pending ancestor cascade already covers the target, descendants,
        // and following siblings that a class selector can affect. Retain the
        // selector cache for unrelated retained UI (for example, a chart), and
        // discard only entries that the ancestor cascade will recompute.
        if (!s_disableScopedClassInvalidation
            && !_stylesheetsDirty
            && !_fullStylesDirty
            && pendingDirtyAncestor)
        {
            _stylesDirty = true;
            RemoveMatchedRuleCacheSubtree(dirtyAncestor);
            _reuseMatchedRulesForDirtyPass = true;
            ScopedClassInvalidationCount++;
            if (s_traceChildListInvalidation)
            {
                Console.WriteLine(
                    $"[CSS INVALIDATE] class target={target.localName}.{target.className.Replace(' ', '.')} " +
                    "fallback=False pendingAncestor=True");
            }
            _document.InvalidateLayoutFromStyleMutation();
            return;
        }

        var fallback = s_disableScopedClassInvalidation
            || _stylesheetsDirty
            || _fullStylesDirty
            || ChangedClassMayAffectOtherSubjects(oldClassName, newClassName);
        if (s_traceChildListInvalidation)
        {
            Console.WriteLine(
                $"[CSS INVALIDATE] class target={target.localName}.{target.className.Replace(' ', '.')} " +
                $"fallback={fallback}");
        }
        if (fallback)
        {
            ClearMatchedRuleCache();
            ClassInvalidationFallbackCount++;
            Invalidate(target);
            return;
        }

        _stylesDirty = true;
        _matchedRuleCache.Remove(target);
        _reuseMatchedRulesForDirtyPass = true;
        ScopedClassInvalidationCount++;
        _dirtyClassElements.Add(target);
        _document.InvalidateLayoutFromStyleMutation();
    }

    internal bool InvalidateDynamicState(IEnumerable<AvaloniaDomElement> changedHoverRoots)
    {
        // A hover transition can change selectors on the hovered element and
        // its descendants (for example, `.row:hover .favorite`). It must not
        // mark every changed ancestor as a dirty root: entering a chart canvas
        // would then recascade and relayout the entire retained chart subtree.
        // Select only elements that are possible subjects of dynamic rules.
        if (_stylesheetsDirty || _fullStylesDirty)
        {
            return false;
        }

        var seenRoots = new HashSet<AvaloniaDomElement>();
        var seenSubjects = new HashSet<AvaloniaDomElement>();
        var added = false;
        foreach (var root in changedHoverRoots)
        {
            if (!seenRoots.Add(root) || !_document.IsConnectedStyleElement(root))
            {
                continue;
            }

            var directAffected = CollectCandidateRules(root)
                                     .Any(rule => rule.Selector.RightmostDependsOnDynamicState)
                                 || CollectCandidateRules(root, pseudoElements: true)
                                     .Any(rule => rule.Selector.RightmostDependsOnDynamicState);
            if (directAffected && seenSubjects.Add(root))
            {
                _dirtyClassElements.Add(root);
                added = true;
            }

            var scope = CssDynamicDependencyScope.None;
            foreach (var rule in _rules)
            {
                scope = (CssDynamicDependencyScope)Math.Max(
                    (int)scope,
                    (int)rule.Selector.GetDynamicDependencyScope(root, _document));
                if (scope == CssDynamicDependencyScope.Siblings)
                {
                    break;
                }
            }
            if (scope == CssDynamicDependencyScope.None)
            {
                continue;
            }

            var scopeRoot = scope == CssDynamicDependencyScope.Siblings
                ? root.parentElement ?? root
                : root;
            foreach (var element in _document.EnumerateStyleElements(scopeRoot))
            {
                if (!seenSubjects.Add(element))
                {
                    continue;
                }

                var dynamicCandidate = CollectCandidateRules(element)
                                           .Any(rule => rule.Selector.DependsOnDynamicState)
                                       || CollectCandidateRules(element, pseudoElements: true)
                                           .Any(rule => rule.Selector.DependsOnDynamicState);
                if (!dynamicCandidate)
                {
                    continue;
                }
                _dirtyClassElements.Add(element);
                added = true;
            }
        }

        _stylesDirty |= added;
        return added;
    }

    public void EnsureCurrent()
    {
        if (_isComputing || (!_stylesheetsDirty && !_stylesDirty))
        {
            return;
        }

        var collectPerformance = _document.CollectPerformanceMetrics;
        var started = collectPerformance || _document.HasUiThreadWorkBudget
            ? Stopwatch.GetTimestamp()
            : 0;
        var allocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        _isComputing = true;
        try
        {
            if (_stylesheetsDirty)
            {
                // Consume the current invalidation before doing work. A script or
                // resource callback can attach another stylesheet reentrantly;
                // that new invalidation must survive for the next pass.
                _stylesheetsDirty = false;
                if (!ReloadStylesheets() && !_documentWideStylesDirty)
                {
                    // A head mutation can be unrelated to CSS (for example an
                    // attribute on a LINK), leaving the same effective rule
                    // set. Preserve any non-head dirty roots, but do not run a
                    // document-wide cascade for an unchanged stylesheet set.
                    _fullStylesDirty = false;
                }
            }

            if (_stylesDirty)
            {
                // As above, clear the work item before recomputing so mutations
                // raised during layout/cascade are not overwritten afterward.
                _stylesDirty = false;
                if (_fullStylesDirty)
                {
                    RecomputeDocumentStyles();
                }
                else
                {
                    RecomputeDirtyStyles();
                }
                if (!_stylesDirty)
                {
                    _fullStylesDirty = false;
                    _dirtyRoots.Clear();
                    _dirtyCustomPropertyRoots.Clear();
                    _dirtyElements.Clear();
                    _dirtyClassElements.Clear();
                    _pendingClassMutationElements.Clear();
                    _pendingRemovalTargets.Clear();
                    _batchedRemovalTargets.Clear();
                    _documentWideStylesDirty = false;
                    _reuseMatchedRulesForDirtyPass = false;
                }
            }
        }
        finally
        {
            _isComputing = false;
            var elapsedTicks = started == 0
                ? 0
                : Stopwatch.GetTimestamp() - started;
            if (collectPerformance)
            {
                EnsureCurrentTicks += elapsedTicks;
                EnsureCurrentAllocatedBytes += GC.GetAllocatedBytesForCurrentThread() - allocationStarted;
            }
            if (elapsedTicks > 0)
            {
                _document.RecordUiThreadWork(
                    UiThreadWorkKind.Css,
                    elapsedTicks);
            }
        }
    }

    private bool ReloadStylesheets()
    {
        var stopwatch = s_profileCss ? Stopwatch.StartNew() : null;
        var parsesBefore = StylesheetParseCount;
        var compiledCacheHitsBefore = CompiledStylesheetCacheHitCount;
        _baseHref = null;
        var inputs = new List<StylesheetInput>();
        var liveStylesheets = new HashSet<AvaloniaDomElement>();
        var mediaViewport = _document.GetDocumentViewportClientSize();
        var mediaDevicePixelRatio = _document.GetDocumentDevicePixelRatio();

        foreach (var node in _document.StylesheetNodes)
        {
            switch (node.tagName.ToUpperInvariant())
            {
                case "BASE":
                    if (string.IsNullOrWhiteSpace(_baseHref))
                    {
                        _baseHref = node.getAttribute("href");
                    }
                    break;
                case "STYLE":
                {
                    var css = node.textContent;
                    if (!string.IsNullOrWhiteSpace(css))
                    {
                        liveStylesheets.Add(node);
                        inputs.Add(new StylesheetInput(
                            node,
                            css,
                            _document.baseURI,
                            $"style:{inputs.Count}|media={node.getAttribute("media")}|type={node.getAttribute("type")}|disabled={node.getAttribute("disabled")}"));
                    }
                    break;
                }
                case "LINK" when string.Equals(node.getAttribute("rel"), "stylesheet", StringComparison.OrdinalIgnoreCase):
                {
                    var href = node.getAttribute("href");
                    if (string.IsNullOrWhiteSpace(href))
                    {
                        break;
                    }

                    try
                    {
                        var resource = _document.LoadTextResourceDetails(href, _baseHref);
                        liveStylesheets.Add(node);
                        inputs.Add(new StylesheetInput(
                            node,
                            resource.Content ?? string.Empty,
                            ResolveStylesheetBaseAddress(href, resource.DisplayName),
                            $"{href}|media={node.getAttribute("media")}|disabled={node.getAttribute("disabled")}"));

                        // An empty stylesheet is still a successfully loaded
                        // resource and must settle link.onload / loader Promises.
                        if (!_loadedLinks.TryGetValue(node, out var loadedHref)
                            || !string.Equals(loadedHref, href, StringComparison.Ordinal))
                        {
                            _loadedLinks[node] = href;
                            _document.ScheduleResourceEvent(node, "load");
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.Error.WriteLine($"Stylesheet load failed for '{href}' against base '{_baseHref}': {ex.Message}");
                        if (!_loadedLinks.TryGetValue(node, out var loadedHref)
                            || !string.Equals(loadedHref, href, StringComparison.Ordinal))
                        {
                            _loadedLinks[node] = href;
                            _document.ScheduleResourceEvent(node, "error");
                        }
                    }
                    break;
                }
            }
        }

        var sameInputs = string.Equals(_lastBaseHref, _baseHref, StringComparison.Ordinal)
                         && _lastStylesheetInputs is not null
                         && _lastStylesheetInputs.Count == inputs.Count
                         && _lastStylesheetInputs.SequenceEqual(inputs);
        var mediaViewportChanged = _hasViewportDependentMediaQueries
                                   && (Math.Abs(_lastMediaViewportWidth - mediaViewport.Width) >= 0.01
                                       || Math.Abs(_lastMediaViewportHeight - mediaViewport.Height) >= 0.01
                                       || Math.Abs(_lastMediaDevicePixelRatio - mediaDevicePixelRatio) >= 0.01);
        var mediaQueryOutcomesChanged = mediaViewportChanged
                                        && (s_disableMediaQueryOutcomeCache
                                            || HaveMediaQueryOutcomesChanged());
        if (sameInputs && !mediaQueryOutcomesChanged)
        {
            var presentationElements = 0;
            if (_viewportReconciliationPending)
            {
                presentationElements = ReapplyDocumentPresentation();
                MediaQueryOutcomeCacheHitCount++;
            }
            _viewportReconciliationPending = false;
            if (mediaViewportChanged)
            {
                _lastMediaViewportWidth = mediaViewport.Width;
                _lastMediaViewportHeight = mediaViewport.Height;
                _lastMediaDevicePixelRatio = mediaDevicePixelRatio;
            }
            if (stopwatch is not null)
            {
                Console.WriteLine(
                    $"[CSS PROFILE] reload={stopwatch.Elapsed.TotalMilliseconds:0.###}ms " +
                    $"stylesheets={liveStylesheets.Count} newParses=0 " +
                    $"compiledCacheHits={CompiledStylesheetCacheHitCount - compiledCacheHitsBefore} " +
                    $"rules={_rules.Count} unchanged=true " +
                    $"mediaOutcomesUnchanged={mediaViewportChanged} " +
                    $"presentationElements={presentationElements}");
            }

            return false;
        }

        _viewportReconciliationPending = false;
        ClearMatchedRuleCache();
        ClearCascadeTemplateCache();

        var previousInputCount = _lastStylesheetInputs?.Count ?? 0;
        var appendOnly = !s_disableAppendOnlyStylesheetUpdates
                         && string.Equals(_lastBaseHref, _baseHref, StringComparison.Ordinal)
                         && _lastStylesheetInputs is not null
                         && inputs.Count > previousInputCount
                         && _lastStylesheetInputs.SequenceEqual(inputs.Take(previousInputCount));
        if (appendOnly)
        {
            var firstAppendedRule = _rules.Count;
            var appendSourceOrder = _rules.Count;
            for (var index = previousInputCount; index < inputs.Count; index++)
            {
                var input = inputs[index];
                AddStyleSheet(input.Owner, input.Css, input.Source, ref appendSourceOrder);
            }

            _lastBaseHref = _baseHref;
            _lastStylesheetInputs = inputs;
            _lastMediaViewportWidth = mediaViewport.Width;
            _lastMediaViewportHeight = mediaViewport.Height;
            _lastMediaDevicePixelRatio = mediaDevicePixelRatio;
            SynchronizeFontFaces(inputs);
            var appendedRules = _rules.Skip(firstAppendedRule).ToArray();
            // Source order is normally contiguous with the rule list, but use
            // the appended rules themselves as the boundary so media-filtered
            // rules and future indexing changes cannot invalidate the filter.
            var firstAppendedSourceOrder = appendedRules.Length == 0
                ? int.MaxValue
                : appendedRules.Min(static rule => rule.SourceOrder);
            var matchedElements = 0;
            var rootCustomPropertiesRebased = false;
            var documentElementAffected = appendedRules.Any(rule =>
                rule.Selector.MatchesDocumentElement(_document));
            if (documentElementAffected
                && !_documentWideStylesDirty
                && TryRebaseAppendOnlyRootCustomProperties(appendedRules, firstAppendedRule))
            {
                rootCustomPropertiesRebased = true;
                documentElementAffected = false;
            }
            if (!s_disableScopedAppendStylesheetCascade
                && !_documentWideStylesDirty
                && !documentElementAffected)
            {
                foreach (var element in _document.EnumerateStyleElements())
                {
                    var matched = false;
                    var candidates = _document.IndexedAppendStylesheetMatchingEnabled
                        ? CollectAppendedCandidateRules(element, firstAppendedSourceOrder)
                        : appendedRules;
                    foreach (var rule in candidates)
                    {
                        AppendStylesheetCandidateEvaluationCount++;
                        if (!RuleMatchesElement(rule, element))
                        {
                            continue;
                        }

                        matched = true;
                        break;
                    }

                    if (!matched)
                    {
                        continue;
                    }

                    matchedElements++;
                    // A descendant can explicitly inherit any property, not
                    // only properties that inherit by default. Recompute the
                    // matched branch so an appended rule cannot leave an
                    // explicit `inherit` or custom-property consumer stale.
                    AddDirtyRoot(element);
                }

                _fullStylesDirty = false;
            }
            if (stopwatch is not null)
            {
                Console.WriteLine(
                    $"[CSS PROFILE] reload={stopwatch.Elapsed.TotalMilliseconds:0.###}ms " +
                    $"stylesheets={liveStylesheets.Count} newParses={StylesheetParseCount - parsesBefore} " +
                    $"compiledCacheHits={CompiledStylesheetCacheHitCount - compiledCacheHitsBefore} " +
                    $"rules={_rules.Count} appendOnly=true appendedRules={appendedRules.Length} " +
                    $"matchedElements={matchedElements} dirtyRoots={_dirtyRoots.Count} " +
                    $"documentElementAffected={documentElementAffected} " +
                    $"rootCustomPropertiesRebased={rootCustomPropertiesRebased} scoped={!_fullStylesDirty}");
            }

            return true;
        }

        _rules.Clear();
        _childListAncestorRules.Clear();
        _siblingCombinatorRules.Clear();
        _rulesByTag.Clear();
        _rulesById.Clear();
        _rulesByClass.Clear();
        _universalRules.Clear();
        _pseudoRulesByTag.Clear();
        _pseudoRulesById.Clear();
        _pseudoRulesByClass.Clear();
        _pseudoUniversalRules.Clear();
        _pseudoElementRuleCount = 0;
        _hasStyleAttributeSelectors = false;
        _ancestorSensitiveClasses.Clear();
        _ancestorSensitiveClassAttributeSelectors.Clear();
        _explicitlyInheritedProperties.Clear();
        _hasBroadExplicitInheritance = false;
        _hasViewportDependentMediaQueries = false;
        _mediaQueryOutcomes.Clear();
        var sourceOrder = 0;
        foreach (var input in inputs)
        {
            AddStyleSheet(input.Owner, input.Css, input.Source, ref sourceOrder);
        }

        foreach (var stale in _parsedStyleSheets.Keys.Where(node => !liveStylesheets.Contains(node)).ToArray())
        {
            _parsedStyleSheets.Remove(stale);
        }

        _lastBaseHref = _baseHref;
        _lastStylesheetInputs = inputs;
        _lastMediaViewportWidth = mediaViewport.Width;
        _lastMediaViewportHeight = mediaViewport.Height;
        _lastMediaDevicePixelRatio = mediaDevicePixelRatio;
        SynchronizeFontFaces(inputs);

        if (stopwatch is not null)
        {
            Console.WriteLine(
                $"[CSS PROFILE] reload={stopwatch.Elapsed.TotalMilliseconds:0.###}ms " +
                $"stylesheets={liveStylesheets.Count} newParses={StylesheetParseCount - parsesBefore} " +
                $"compiledCacheHits={CompiledStylesheetCacheHitCount - compiledCacheHitsBefore} " +
                $"rules={_rules.Count}");
        }

        return true;
    }

    private IEnumerable<CascadeRule> CollectAppendedCandidateRules(
        AvaloniaDomElement element,
        int firstAppendedSourceOrder)
    {
        foreach (var rule in CollectCandidateRules(element))
        {
            if (rule.SourceOrder >= firstAppendedSourceOrder)
            {
                yield return rule;
            }
        }

        // IndexRuleForMatching puts a rule in exactly one of the ordinary or
        // pseudo index families, so concatenating the two candidate streams
        // cannot duplicate a rule.
        foreach (var rule in CollectCandidateRules(element, pseudoElements: true))
        {
            if (rule.SourceOrder >= firstAppendedSourceOrder)
            {
                yield return rule;
            }
        }
    }

    private bool HaveMediaQueryOutcomesChanged()
    {
        if (_mediaQueryOutcomes.Count == 0)
        {
            return true;
        }

        foreach (var outcome in _mediaQueryOutcomes)
        {
            if (MatchesMediaQuery(outcome.Key) != outcome.Value)
            {
                return true;
            }
        }

        return false;
    }

    private bool EvaluateMediaQuery(string query)
    {
        var matches = MatchesMediaQuery(query);
        _mediaQueryOutcomes[query] = matches;
        return matches;
    }

    private int ReapplyDocumentPresentation()
    {
        var elementCount = 0;
        foreach (var element in _document.EnumerateStyleElements())
        {
            if (s_disableViewportPresentationChangeSet)
            {
                element.ReapplyComputedPresentation();
            }
            else
            {
                element.ReapplyViewportPresentation();
            }
            elementCount++;
        }
        ViewportPresentationReapplyElementCount += elementCount;
        _document.CompleteViewportPresentationReconciliation();
        return elementCount;
    }

    private bool TryRebaseAppendOnlyRootCustomProperties(
        IReadOnlyList<CascadeRule> appendedRules,
        int firstAppendedRule)
    {
        if (s_disableSharedCustomProperties
            || _documentElementComputedValues is null
            || _documentElementDeclaredProperties is null)
        {
            return false;
        }

        var appendedRootRules = appendedRules
            .Where(rule => rule.Selector.MatchesDocumentElement(_document))
            .ToArray();
        if (appendedRootRules.Length == 0
            || appendedRootRules.SelectMany(rule => rule.Declarations)
                .Any(declaration => !declaration.Name.StartsWith("--", StringComparison.Ordinal)))
        {
            return false;
        }

        var changedNames = new HashSet<string>(
            appendedRootRules.SelectMany(rule => rule.Declarations).Select(declaration => declaration.Name),
            StringComparer.Ordinal);
        if (changedNames.Count == 0)
        {
            return false;
        }

        // A pre-existing var() consumer may have resolved a fallback before
        // this definition arrived. In that case its ordinary computed value
        // must be cascaded again. False positives here only select the safe
        // full-cascade path.
        if (_rules.Take(firstAppendedRule)
                .SelectMany(rule => rule.Declarations)
                .Any(declaration => ReferencesAnyCustomProperty(declaration.Value, changedNames))
            || _document.EnumerateStyleElements()
                .SelectMany(element => element.StyleValues.Values)
                .Any(value => value is not null && ReferencesAnyCustomProperty(value, changedNames)))
        {
            return false;
        }

        var previousComputed = _documentElementComputedValues;
        var previousDeclared = _documentElementDeclaredProperties;
        var nextComputed = ComputeForDocumentElement(out var nextDeclared);
        if (!previousComputed.OrdinaryContentEquals(nextComputed)
            || !previousDeclared.OrdinaryContentEquals(nextDeclared))
        {
            return false;
        }

        var plans = new List<CustomPropertyRebasePlan>();
        var oldMaps = new Dictionary<AvaloniaDomElement, CssCustomPropertyMap>();
        var newMaps = new Dictionary<AvaloniaDomElement, CssCustomPropertyMap>();
        foreach (var element in _document.EnumerateStyleElements())
        {
            if (_dirtyRoots.Any(root => IsDescendantOrSelf(element, root)))
            {
                continue;
            }

            var parent = element.parentElement;
            var inheritsFromDocumentElement = string.Equals(
                element.tagName,
                "BODY",
                StringComparison.OrdinalIgnoreCase);
            var oldInherited = inheritsFromDocumentElement
                ? previousComputed.CustomProperties
                : oldMaps[parent!];
            var newInherited = inheritsFromDocumentElement
                ? nextComputed.CustomProperties
                : newMaps[parent!];
            var oldCurrent = element.ComputedCustomProperties;
            oldMaps[element] = oldCurrent;
            if (!element.TryPlanCustomPropertyRebase(
                    oldInherited,
                    newInherited,
                    out var rebasedComputed,
                    out var rebasedDeclared))
            {
                return false;
            }

            newMaps[element] = rebasedComputed;
            plans.Add(new CustomPropertyRebasePlan(element, rebasedComputed, rebasedDeclared));
        }

        _documentElementComputedValues = nextComputed;
        _documentElementDeclaredProperties = nextDeclared;
        foreach (var plan in plans)
        {
            plan.Element.ApplyCustomPropertyRebase(plan.Computed, plan.Declared);
        }

        return true;
    }

    private static bool ReferencesAnyCustomProperty(
        string value,
        IReadOnlySet<string> names)
        => value.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0
           && names.Any(name => value.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0);

    private string ResolveStylesheetBaseAddress(string href, string loadedDisplayName)
    {
        if (Uri.TryCreate(href, UriKind.Absolute, out var absoluteHref))
        {
            return absoluteHref.ToString();
        }

        if (Uri.TryCreate(_document.baseURI, UriKind.Absolute, out var documentBase))
        {
            return new Uri(documentBase, href).ToString();
        }

        return loadedDisplayName;
    }

    private void SynchronizeFontFaces(IEnumerable<StylesheetInput> inputs)
    {
        var faces = new List<CssFontFaceSource>();
        foreach (var input in inputs)
        {
            if (!_parsedStyleSheets.TryGetValue(input.Owner, out var cached)) continue;
            faces.AddRange(cached.FontFaces
                .Where(face => face.MediaQueries.Count == 0
                               || face.MediaQueries.All(EvaluateMediaQuery))
                .Select(face => new CssFontFaceSource(face, input.BaseAddress)));
        }
        _document.FontFaces.Synchronize(faces);
    }

    private void AddStyleSheet(AvaloniaDomElement owner, string css, string source, ref int sourceOrder)
    {
        var collectPerformance = _document.CollectPerformanceMetrics;
        if (!_parsedStyleSheets.TryGetValue(owner, out var cached)
            || !string.Equals(cached.Css, css, StringComparison.Ordinal))
        {
            if (TryGetCompiledStylesheet(css, out cached))
            {
                _parsedStyleSheets[owner] = cached;
                CompiledStylesheetCacheHitCount++;
            }
            else
            {
                try
                {
                    var compilation = CssStylesheetCompiler.Compile(
                        css,
                        s_disableStylesheetNormalizationGuards,
                        collectPerformance);
                    StylesheetNormalizationTicks += compilation.NormalizationTicks;
                    StylesheetNormalizationAllocatedBytes += compilation.NormalizationAllocatedBytes;
                    StylesheetParserTicks += compilation.ParserTicks;
                    StylesheetParserAllocatedBytes += compilation.ParserAllocatedBytes;
                    StylesheetRuleCompilationTicks += compilation.RuleCompilationTicks;
                    StylesheetRuleCompilationAllocatedBytes += compilation.RuleCompilationAllocatedBytes;
                    var parsedRules = compilation.Rules
                        .Select(static rule => new ParsedRule(
                            CssSelectorParser.Create(rule.Selector),
                            rule.Declarations,
                            rule.MediaQueries))
                        .ToArray();
                    cached = AddCompiledStylesheet(new CachedStyleSheet(css, parsedRules, compilation.FontFaces));
                    _parsedStyleSheets[owner] = cached;
                    StylesheetParseCount++;
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine($"Stylesheet parse failed for '{source}': {ex}");
                    return;
                }
            }
        }

        var ownerMedia = owner.getAttribute("media")?.Trim();
        if (!string.IsNullOrWhiteSpace(ownerMedia))
        {
            _hasViewportDependentMediaQueries = true;
            if (!EvaluateMediaQuery(ownerMedia))
            {
                return;
            }
        }

        var indexingStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
        var indexingAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        foreach (var parsedRule in cached.Rules)
        {
            if (parsedRule.MediaQueries.Count > 0)
            {
                _hasViewportDependentMediaQueries = true;
                if (parsedRule.MediaQueries.Any(query => !EvaluateMediaQuery(query)))
                {
                    continue;
                }
            }

            var cascadeRule = new CascadeRule(
                parsedRule.Selector,
                parsedRule.Declarations,
                sourceOrder++,
                source);
            _rules.Add(cascadeRule);
            if (parsedRule.Selector.HasAncestorChildListDependency)
                _childListAncestorRules.Add(cascadeRule);
            if (parsedRule.Selector.HasSiblingCombinator)
                _siblingCombinatorRules.Add(cascadeRule);
            _hasStyleAttributeSelectors |= parsedRule.Selector.DependsOnAttribute("style");
            parsedRule.Selector.CollectAncestorClassDependencies(
                _ancestorSensitiveClasses,
                _ancestorSensitiveClassAttributeSelectors);
            TrackExplicitInheritance(parsedRule.Declarations);
            IndexRuleForMatching(cascadeRule);
        }
        if (collectPerformance)
        {
            StylesheetIndexingTicks += Stopwatch.GetTimestamp() - indexingStarted;
            StylesheetIndexingAllocatedBytes +=
                GC.GetAllocatedBytesForCurrentThread() - indexingAllocationStarted;
        }
    }

    private static bool TryGetCompiledStylesheet(string css, out CachedStyleSheet cached)
    {
        if (s_disableCompiledStylesheetCache
            || css.Length > MaximumSingleCompiledStylesheetCharacters)
        {
            cached = null!;
            return false;
        }

        lock (s_compiledStylesheetCacheGate)
        {
            return s_compiledStylesheetCache.TryGetValue(css, out cached!);
        }
    }

    private static CachedStyleSheet AddCompiledStylesheet(CachedStyleSheet cached)
    {
        if (s_disableCompiledStylesheetCache
            || cached.Css.Length > MaximumSingleCompiledStylesheetCharacters)
        {
            return cached;
        }

        lock (s_compiledStylesheetCacheGate)
        {
            // Another document can finish compiling the same stylesheet while
            // this document is parsing. Reuse the already-published immutable
            // rule graph in that case.
            if (s_compiledStylesheetCache.TryGetValue(cached.Css, out var existing))
            {
                return existing;
            }

            while (s_compiledStylesheetCacheInsertionOrder.Count > 0
                   && (s_compiledStylesheetCache.Count >= MaximumCompiledStylesheetCacheEntries
                       || s_compiledStylesheetCacheCharacters + cached.Css.Length
                       > MaximumCompiledStylesheetCacheCharacters))
            {
                var evictedCss = s_compiledStylesheetCacheInsertionOrder.Dequeue();
                if (s_compiledStylesheetCache.Remove(evictedCss))
                {
                    s_compiledStylesheetCacheCharacters -= evictedCss.Length;
                }
            }

            s_compiledStylesheetCache.Add(cached.Css, cached);
            s_compiledStylesheetCacheInsertionOrder.Enqueue(cached.Css);
            s_compiledStylesheetCacheCharacters += cached.Css.Length;
            return cached;
        }
    }

    private bool MatchesMediaQuery(string query)
    {
        var viewport = _document.GetDocumentViewportClientSize();
        var devicePixelRatio = _document.GetDocumentDevicePixelRatio();
        return CssMediaQueryEvaluator.Matches(
            query,
            new CssMediaEnvironment(viewport.Width, viewport.Height, devicePixelRatio));
    }

    private bool RuleMatchesElement(CascadeRule rule, AvaloniaDomElement element)
        => rule.Selector.PseudoElementName is { } pseudoElement
            ? rule.Selector.MatchesPseudoElement(element, _document, pseudoElement)
            : rule.Selector.Matches(element, _document);

    private void IndexRuleForMatching(CascadeRule rule)
    {
        if (rule.Selector.PseudoElementName is not null)
        {
            _pseudoElementRuleCount++;
            IndexRuleForMatching(
                rule,
                _pseudoRulesByTag,
                _pseudoRulesById,
                _pseudoRulesByClass,
                _pseudoUniversalRules);
            return;
        }

        IndexRuleForMatching(rule, _rulesByTag, _rulesById, _rulesByClass, _universalRules);
    }

    private static void IndexRuleForMatching(
        CascadeRule rule,
        IDictionary<string, List<CascadeRule>> rulesByTag,
        IDictionary<string, List<CascadeRule>> rulesById,
        IDictionary<string, List<CascadeRule>> rulesByClass,
        ICollection<CascadeRule> universalRules)
    {
        // Rightmost part determines the subject element the rule can match.
        var rightmost = rule.Selector.Parts.Count > 0 ? rule.Selector.Parts[rule.Selector.Parts.Count - 1].Simple : null;
        if (rightmost is null)
        {
            universalRules.Add(rule);
            return;
        }

        bool indexed = false;

        if (!string.IsNullOrEmpty(rightmost.Tag) && rightmost.Tag != "*")
        {
            if (!rulesByTag.TryGetValue(rightmost.Tag, out var list))
            {
                list = new List<CascadeRule>();
                rulesByTag[rightmost.Tag] = list;
            }
            list.Add(rule);
            indexed = true;
        }

        if (!string.IsNullOrEmpty(rightmost.Id))
        {
            if (!rulesById.TryGetValue(rightmost.Id, out var list))
            {
                list = new List<CascadeRule>();
                rulesById[rightmost.Id] = list;
            }
            list.Add(rule);
            indexed = true;
        }

        if (rightmost.Classes.Count > 0)
        {
            foreach (var cls in rightmost.Classes)
            {
                if (string.IsNullOrEmpty(cls)) continue;
                if (!rulesByClass.TryGetValue(cls, out var list))
                {
                    list = new List<CascadeRule>();
                    rulesByClass[cls] = list;
                }
                list.Add(rule);
            }
            indexed = true;
        }

        // If the rightmost is only universal (*) or only attribute/pseudo without tag/id/class,
        // or complex rightmost we couldn't cheaply key, always consider the rule.
        if (!indexed)
        {
            universalRules.Add(rule);
        }
    }

    private void RecomputeDocumentStyles()
    {
        var stopwatch = s_profileCss ? Stopwatch.StartNew() : null;
        var elementCount = 0;
        _documentElementComputedValues = ComputeForDocumentElement(out var documentElementDeclarations);
        _documentElementDeclaredProperties = documentElementDeclarations;
        foreach (var element in _document.EnumerateStyleElements())
        {
            if (ApplyTextNodePresentation(element))
            {
                continue;
            }
            ComputeAndApplyElement(element);
            elementCount++;
        }
        StyleRecomputeCount++;
        _document.CompleteViewportPresentationReconciliation();
        ElementStyleComputeCount += elementCount;
        if (stopwatch is not null)
        {
            Console.WriteLine(
                $"[CSS PROFILE] recompute={stopwatch.Elapsed.TotalMilliseconds:0.###}ms " +
                $"elements={elementCount} pass={StyleRecomputeCount} totalElements={ElementStyleComputeCount} " +
                $"totalApplies={ElementStyleApplyCount}");
        }
    }

    private void RecomputeDirtyStyles()
    {
        if (_dirtyRoots.Count == 0
            && _dirtyCustomPropertyRoots.Count == 0
            && _dirtyElements.Count == 0
            && _dirtyClassElements.Count == 0)
        {
            return;
        }

        var stopwatch = s_profileCss ? Stopwatch.StartNew() : null;
        var elementCount = 0;
        HashSet<AvaloniaDomElement>? recomputedElements = _dirtyCustomPropertyRoots.Count > 0
            ? new HashSet<AvaloniaDomElement>()
            : null;

        // A general dirty root already performs a complete subtree cascade. A
        // contained custom-property root therefore needs no separate rebase.
        // Process the remaining custom roots first so a broader custom root can
        // also satisfy a contained direct/class invalidation if another
        // mutation cleared the matched-rule cache in the same transaction.
        foreach (var root in _dirtyCustomPropertyRoots.ToArray())
        {
            if (_dirtyRoots.Any(generalRoot => IsDescendantOrSelf(root, generalRoot))
                || !_document.IsConnectedStyleElement(root)
                || recomputedElements!.Contains(root))
            {
                continue;
            }

            RecomputeCustomPropertySubtree(root, recomputedElements!, ref elementCount);
        }

        foreach (var root in _dirtyRoots.ToArray())
        {
            if (!_document.IsConnectedStyleElement(root))
            {
                continue;
            }

            foreach (var element in _document.EnumerateStyleElements(root))
            {
                if (ApplyTextNodePresentation(element))
                {
                    continue;
                }
                if (recomputedElements is not null && !recomputedElements.Add(element))
                {
                    continue;
                }

                ComputeAndApplyElement(element, _reuseMatchedRulesForDirtyPass);
                elementCount++;
            }
        }

        foreach (var element in _dirtyClassElements
                     .Where(element => !_dirtyRoots.Any(root => IsDescendantOrSelf(element, root)))
                     .OrderBy(GetElementDepth))
        {
            if (!_document.IsConnectedStyleElement(element)
                || recomputedElements?.Contains(element) == true)
            {
                continue;
            }

            var previousValues = element.ComputedStyleValues;
            ComputeAndApplyElement(element, _reuseMatchedRulesForDirtyPass);
            elementCount++;
            (recomputedElements ??= new HashSet<AvaloniaDomElement>()).Add(element);
            if (!InheritedValuesMayHaveChanged(previousValues, element.ComputedStyleValues))
            {
                continue;
            }

            ClassInvalidationPropagationCount++;
            if (_document.InheritedCursorRebaseEnabled
                && TryGetSingleInheritedCursorChange(
                    previousValues,
                    element.ComputedStyleValues,
                    out var previousCursor,
                    out var currentCursor))
            {
                RebaseInheritedCursorDescendants(
                    element,
                    previousCursor,
                    currentCursor,
                    recomputedElements,
                    ref elementCount);
            }
            else
            {
                RecomputeInheritedDescendants(element, recomputedElements, ref elementCount);
            }
        }

        foreach (var element in _dirtyElements
                     .Where(element => !_dirtyRoots.Any(root => IsDescendantOrSelf(element, root)))
                     .OrderBy(GetElementDepth))
        {
            if (!_document.IsConnectedStyleElement(element)
                || recomputedElements?.Contains(element) == true)
            {
                continue;
            }

            ComputeAndApplyElement(element, _reuseMatchedRulesForDirtyPass);
            elementCount++;
            recomputedElements?.Add(element);
        }

        StyleRecomputeCount++;
        ElementStyleComputeCount += elementCount;
        if (stopwatch is not null)
        {
            var roots = string.Join(
                ", ",
                _dirtyRoots.Take(8).Select(root =>
                    $"{root.localName}#{root.id}.{root.className.Replace(' ', '.')}"));
            Console.WriteLine(
                $"[CSS PROFILE] incremental={stopwatch.Elapsed.TotalMilliseconds:0.###}ms " +
                $"roots={_dirtyRoots.Count} custom={_dirtyCustomPropertyRoots.Count} " +
                $"class={_dirtyClassElements.Count} direct={_dirtyElements.Count} " +
                $"elements={elementCount} pass={StyleRecomputeCount} " +
                $"totalElements={ElementStyleComputeCount} totalApplies={ElementStyleApplyCount} rootSamples=[{roots}]");
        }
    }

    private void RecomputeCustomPropertySubtree(
        AvaloniaDomElement root,
        ISet<AvaloniaDomElement> recomputedElements,
        ref int elementCount)
    {
        var previousValues = root.ComputedStyleValues;
        var previousCustomProperties = root.ComputedCustomProperties;
        ComputeAndApplyElement(root, _reuseMatchedRulesForDirtyPass);
        elementCount++;
        recomputedElements.Add(root);

        var inheritedOrdinaryValuesChanged = InheritedOrdinaryValuesMayHaveChanged(
            previousValues,
            root.ComputedStyleValues);
        RecomputeOrRebaseCustomPropertyDescendants(
            root,
            previousCustomProperties,
            root.ComputedCustomProperties,
            inheritedOrdinaryValuesChanged,
            recomputedElements,
            ref elementCount);
    }

    private void RecomputeOrRebaseCustomPropertyDescendants(
        AvaloniaDomElement parent,
        CssCustomPropertyMap previousInheritedCustomProperties,
        CssCustomPropertyMap currentInheritedCustomProperties,
        bool inheritedOrdinaryValuesChanged,
        ISet<AvaloniaDomElement> recomputedElements,
        ref int elementCount)
    {
        foreach (var child in parent.GetChildElements())
        {
            if (ApplyTextNodePresentation(child))
            {
                continue;
            }
            var previousValues = child.ComputedStyleValues;
            var previousCustomProperties = child.ComputedCustomProperties;
            var canRebase = child.TryPlanCustomPropertyRebase(
                previousInheritedCustomProperties,
                currentInheritedCustomProperties,
                out var rebasedComputed,
                out var rebasedDeclared);
            var requiresCascade = inheritedOrdinaryValuesChanged
                                  || ElementMayResolveVariables(child)
                                  || !canRebase;

            if (requiresCascade)
            {
                ComputeAndApplyElement(child, _reuseMatchedRulesForDirtyPass);
                elementCount++;
                recomputedElements.Add(child);
            }
            else
            {
                child.ApplyCustomPropertyRebase(rebasedComputed, rebasedDeclared);
            }

            RecomputeOrRebaseCustomPropertyDescendants(
                child,
                previousCustomProperties,
                child.ComputedCustomProperties,
                requiresCascade && InheritedOrdinaryValuesMayHaveChanged(
                    previousValues,
                    child.ComputedStyleValues),
                recomputedElements,
                ref elementCount);
        }
    }

    private static bool ApplyTextNodePresentation(AvaloniaDomElement element)
    {
        if (element is not AvaloniaDomTextNode || element.parentElement is not { } parent)
        {
            return false;
        }
        element.ApplyInheritedTextPresentationFrom(parent);
        return true;
    }

    private void ComputeAndApplyElement(AvaloniaDomElement element, bool reuseMatchedRules = false)
    {
        var collectPerformance = _document.CollectPerformanceMetrics;
        var parent = element.parentElement;
        var inheritsFromDocumentElement = string.Equals(element.tagName, "BODY", StringComparison.OrdinalIgnoreCase);
        var inherited = inheritsFromDocumentElement
            ? _documentElementComputedValues
            : parent?.ComputedStyleValues;
        var inheritedDeclarations = inheritsFromDocumentElement
            ? _documentElementDeclaredProperties
            : parent?.DeclaredStyleProperties;
        var phaseStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
        var phaseAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        CssComputedValues computed;
        CssDeclaredPropertySet declared;
        bool computedFromScratch;
        bool declaredFromScratch;
        bool ordinaryStyleFromTemplate;
        try
        {
            computed = ComputeForElement(
                element,
                inherited,
                inheritedDeclarations,
                reuseMatchedRules,
                out declared,
                out computedFromScratch,
                out declaredFromScratch,
                out ordinaryStyleFromTemplate);
        }
        finally
        {
            if (collectPerformance)
            {
                ElementStyleCascadeTicks += Stopwatch.GetTimestamp() - phaseStarted;
                ElementStyleCascadeAllocatedBytes +=
                    GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            }
        }

        phaseStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
        phaseAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        CssPropertyValueStore? shareRecyclableValues = null;
        CssPropertyNameSet? shareRecyclableDeclarations = null;
        if (!ordinaryStyleFromTemplate)
        {
            ShareOrdinaryStyle(
                element,
                ref computed,
                ref declared,
                ref computedFromScratch,
                ref declaredFromScratch,
                out shareRecyclableValues,
                out shareRecyclableDeclarations);
        }
        CssPropertyValueStore? recyclableValues;
        CssPropertyNameSet? recyclableDeclarations;
        var applied = false;
        try
        {
            applied = element.SetComputedStyleValues(
                computed,
                declared,
                computedFromScratch,
                declaredFromScratch,
                usePresentationChangeSet: reuseMatchedRules,
                out recyclableValues,
                out recyclableDeclarations);
        }
        finally
        {
            if (collectPerformance)
            {
                ElementStyleCommitTicks += Stopwatch.GetTimestamp() - phaseStarted;
                ElementStyleCommitAllocatedBytes +=
                    GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            }
        }
        if (applied)
        {
            ElementStyleApplyCount++;
        }

        phaseStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
        phaseAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        try
        {
            ApplyGeneratedPseudoElements(element, computed);
        }
        finally
        {
            if (collectPerformance)
            {
                PseudoElementTicks += Stopwatch.GetTimestamp() - phaseStarted;
                PseudoElementAllocatedBytes +=
                    GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            }
        }
        ReturnComputedStyleScratch(shareRecyclableValues, shareRecyclableDeclarations);
        ReturnComputedStyleScratch(recyclableValues, recyclableDeclarations);
    }

    private void ShareOrdinaryStyle(
        AvaloniaDomElement element,
        ref CssComputedValues computed,
        ref CssDeclaredPropertySet declared,
        ref bool computedFromScratch,
        ref bool declaredFromScratch,
        out CssPropertyValueStore? recyclableValues,
        out CssPropertyNameSet? recyclableDeclarations)
    {
        recyclableValues = null;
        recyclableDeclarations = null;
        var currentComputed = (CssComputedValues)element.ComputedStyleValues;
        var currentDeclared = (CssDeclaredPropertySet)element.DeclaredStyleProperties;
        if (s_disableOrdinaryStyleSharing
            || (currentComputed.OrdinaryContentEquals(computed)
                && currentDeclared.OrdinaryContentEquals(declared)))
        {
            return;
        }

        var candidateValues = computed.OrdinaryValues;
        var candidateDeclarations = declared.OrdinaryProperties;
        var hash = HashCode.Combine(
            candidateValues.GetContentHashCode(),
            candidateDeclarations.GetContentHashCode());
        if (_sharedOrdinaryStyles.TryGetValue(hash, out var bucket))
        {
            foreach (var shared in bucket)
            {
                if (!shared.Values.ContentEquals(candidateValues)
                    || !shared.Declarations.SetEquals(candidateDeclarations))
                {
                    continue;
                }

                SharedOrdinaryStyleHitCount++;
                computed = new CssComputedValues(shared.Values, computed.CustomProperties);
                declared = new CssDeclaredPropertySet(shared.Declarations, declared.CustomProperties);
                computedFromScratch = false;
                declaredFromScratch = false;
                recyclableValues = candidateValues;
                recyclableDeclarations = candidateDeclarations;
                return;
            }
        }

        if (_sharedOrdinaryStyleEntryCount >= MaximumSharedOrdinaryStyleEntries)
        {
            return;
        }

        // A style must recur before it is retained strongly. This avoids
        // filling the document pool with animation/frame-specific values while
        // requiring no per-candidate probation objects. Hash matches only
        // control admission; full content equality above controls sharing.
        var fingerprint = unchecked((uint)hash);
        if (fingerprint == 0)
        {
            fingerprint = 1;
        }
        var probationSlot = (int)(fingerprint & (OrdinaryStyleProbationSlotCount - 1));
        if (_ordinaryStyleProbation[probationSlot] != fingerprint)
        {
            _ordinaryStyleProbation[probationSlot] = fingerprint;
            return;
        }

        var sharedValues = computedFromScratch
            ? candidateValues.Clone(frozen: true)
            : candidateValues;
        var sharedDeclarations = declaredFromScratch
            ? candidateDeclarations.Clone(frozen: true)
            : candidateDeclarations;
        sharedValues.Freeze();
        sharedDeclarations.Freeze();
        if (computedFromScratch)
        {
            recyclableValues = candidateValues;
        }
        if (declaredFromScratch)
        {
            recyclableDeclarations = candidateDeclarations;
        }

        computed = new CssComputedValues(sharedValues, computed.CustomProperties);
        declared = new CssDeclaredPropertySet(sharedDeclarations, declared.CustomProperties);
        computedFromScratch = false;
        declaredFromScratch = false;
        (bucket ??= new List<SharedOrdinaryStyle>(1)).Add(
            new SharedOrdinaryStyle(sharedValues, sharedDeclarations));
        _sharedOrdinaryStyles[hash] = bucket;
        _sharedOrdinaryStyleEntryCount++;
    }

    private static int GetElementDepth(AvaloniaDomElement element)
    {
        var depth = 0;
        for (var current = element.parentElement; current is not null; current = current.parentElement)
        {
            depth++;
        }

        return depth;
    }

    private bool ChangedClassMayAffectOtherSubjects(string? oldClassName, string? newClassName)
    {
        var portablePlan = CssMutationInvalidationPlanner.PlanClassChange(
            oldClassName,
            newClassName,
            _rules.Select(static rule => rule.Selector.PortableDependencies));
        if (portablePlan.Affects(
                CssMutationInvalidationScope.Descendants
                | CssMutationInvalidationScope.FollowingSiblings))
        {
            return true;
        }

        var changedClasses = new HashSet<string>(
            SplitClassNames(oldClassName),
            StringComparer.Ordinal);
        changedClasses.SymmetricExceptWith(SplitClassNames(newClassName));
        if (changedClasses.Overlaps(_ancestorSensitiveClasses))
        {
            return true;
        }

        foreach (var selector in _ancestorSensitiveClassAttributeSelectors)
        {
            if (selector.MatchesValue(oldClassName) != selector.MatchesValue(newClassName))
            {
                return true;
            }
        }

        return false;
    }

    private static IEnumerable<string> SplitClassNames(string? className)
        => string.IsNullOrWhiteSpace(className)
            ? Array.Empty<string>()
            : className.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);

    private void TrackExplicitInheritance(IReadOnlyList<CssCascadeDeclaration> declarations)
    {
        foreach (var declaration in declarations)
        {
            if (!string.Equals(declaration.Value.Trim(), "inherit", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var name = CssStyleDeclaration.NormalizePropertyName(declaration.Name);
            if (name is "all" or "border" or "border-color" or "border-style" or "border-width"
                or "background" or "margin" or "padding" or "inset" or "flex" or "grid")
            {
                _hasBroadExplicitInheritance = true;
            }
            else
            {
                _explicitlyInheritedProperties.Add(name);
            }
        }
    }

    private void TrackInlineExplicitInheritance(string? style)
    {
        if (string.IsNullOrWhiteSpace(style))
        {
            return;
        }

        if (!s_disableInlineStyleClassificationSpans)
        {
            var remaining = style.AsSpan();
            while (!remaining.IsEmpty)
            {
                var terminator = remaining.IndexOf(';');
                var declaration = terminator >= 0 ? remaining[..terminator] : remaining;
                remaining = terminator >= 0 ? remaining[(terminator + 1)..] : ReadOnlySpan<char>.Empty;
                var separator = declaration.IndexOf(':');
                if (separator <= 0)
                {
                    continue;
                }

                var value = declaration[(separator + 1)..].Trim();
                if (value.EndsWith("!important", StringComparison.OrdinalIgnoreCase))
                {
                    value = value[..^"!important".Length].TrimEnd();
                }
                if (!value.Equals("inherit", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                TrackInlineExplicitInheritanceProperty(
                    CssStyleDeclaration.NormalizePropertyName(declaration[..separator].Trim().ToString()));
            }

            return;
        }

        foreach (var declaration in style.Split(';'))
        {
            var separator = declaration.IndexOf(':');
            if (separator <= 0)
            {
                continue;
            }

            var value = declaration[(separator + 1)..].Trim();
            if (value.EndsWith("!important", StringComparison.OrdinalIgnoreCase))
            {
                value = value[..^"!important".Length].TrimEnd();
            }
            if (!string.Equals(value, "inherit", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var name = CssStyleDeclaration.NormalizePropertyName(declaration[..separator]);
            TrackInlineExplicitInheritanceProperty(name);
        }
    }

    private void TrackInlineExplicitInheritanceProperty(string name)
    {
        if (name is "all" or "border" or "border-color" or "border-style" or "border-width"
            or "background" or "margin" or "padding" or "inset" or "flex" or "grid")
        {
            _hasBroadInlineExplicitInheritance = true;
        }
        else
        {
            _inlineExplicitlyInheritedProperties.Add(name);
        }
    }

    private bool InheritedValuesMayHaveChanged(
        IReadOnlyDictionary<string, string> previous,
        IReadOnlyDictionary<string, string> current)
    {
        foreach (var pair in previous)
        {
            if (current.TryGetValue(pair.Key, out var currentValue)
                && string.Equals(pair.Value, currentValue, StringComparison.Ordinal))
            {
                continue;
            }

            if (PropertyMayPropagateToDescendants(pair.Key))
            {
                return true;
            }
        }

        foreach (var pair in current)
        {
            if (!previous.ContainsKey(pair.Key)
                && PropertyMayPropagateToDescendants(pair.Key))
            {
                return true;
            }
        }

        return false;
    }

    private bool InheritedOrdinaryValuesMayHaveChanged(
        IReadOnlyDictionary<string, string> previous,
        IReadOnlyDictionary<string, string> current)
    {
        foreach (var pair in previous)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal)
                || !OrdinaryPropertyMayPropagateToDescendants(pair.Key))
            {
                continue;
            }

            if (!current.TryGetValue(pair.Key, out var currentValue)
                || !string.Equals(pair.Value, currentValue, StringComparison.Ordinal))
            {
                return true;
            }
        }

        foreach (var pair in current)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                && !previous.ContainsKey(pair.Key)
                && OrdinaryPropertyMayPropagateToDescendants(pair.Key))
            {
                return true;
            }
        }

        return false;
    }

    private void RecomputeInheritedDescendants(
        AvaloniaDomElement parent,
        ISet<AvaloniaDomElement> recomputedElements,
        ref int elementCount)
    {
        // Inherited CSS applies to text nodes as well as element nodes. Walking
        // only firstElementChild/nextElementSibling left dynamically-created
        // popup labels at an ancestor's earlier visibility:hidden state until
        // a direct getComputedStyle(textNode) happened to reconcile them.
        foreach (var child in parent.GetChildElements())
        {
            if (ApplyTextNodePresentation(child))
            {
                recomputedElements.Add(child);
                continue;
            }
            var previousValues = child.ComputedStyleValues;
            ComputeAndApplyElement(child, _reuseMatchedRulesForDirtyPass);
            elementCount++;
            recomputedElements.Add(child);
            if (InheritedValuesMayHaveChanged(previousValues, child.ComputedStyleValues))
            {
                RecomputeInheritedDescendants(child, recomputedElements, ref elementCount);
            }
        }
    }

    private void RebaseInheritedCursorDescendants(
        AvaloniaDomElement parent,
        string previousCursor,
        string currentCursor,
        ISet<AvaloniaDomElement> recomputedElements,
        ref int elementCount)
    {
        foreach (var child in parent.GetChildElements())
        {
            if (ApplyTextNodePresentation(child))
            {
                recomputedElements.Add(child);
                continue;
            }

            var previousValues = child.ComputedStyleValues;
            if (!previousValues.TryGetValue("cursor", out var childCursor))
            {
                RecomputeInheritedBranch(child, previousValues, recomputedElements, ref elementCount);
                continue;
            }

            // A value that differs from the old parent value establishes an
            // override boundary. Descendants inherit from that unchanged child,
            // so the complete branch can be pruned.
            if (!string.Equals(childCursor, previousCursor, StringComparison.Ordinal))
            {
                InheritedPropagationPrunedElementCount++;
                continue;
            }

            // A declaration whose computed value happens to equal the parent's
            // may be either an explicit value or a CSS-wide inheritance keyword.
            // Re-run that element conservatively, then continue from its actual
            // result.
            if (ElementMayDeclareCursor(child))
            {
                RecomputeInheritedBranch(child, previousValues, recomputedElements, ref elementCount);
                continue;
            }

            child.ApplyInheritedCursorRebase(currentCursor);
            InheritedCursorRebaseElementCount++;
            recomputedElements.Add(child);
            RebaseInheritedCursorDescendants(
                child,
                previousCursor,
                currentCursor,
                recomputedElements,
                ref elementCount);
        }
    }

    private void RecomputeInheritedBranch(
        AvaloniaDomElement child,
        IReadOnlyDictionary<string, string> previousValues,
        ISet<AvaloniaDomElement> recomputedElements,
        ref int elementCount)
    {
        ComputeAndApplyElement(child, _reuseMatchedRulesForDirtyPass);
        elementCount++;
        recomputedElements.Add(child);
        if (!InheritedValuesMayHaveChanged(previousValues, child.ComputedStyleValues))
        {
            return;
        }

        var previousCursor = string.Empty;
        var currentCursor = string.Empty;
        if (_document.InheritedCursorRebaseEnabled
            && TryGetSingleInheritedCursorChange(
                previousValues,
                child.ComputedStyleValues,
                out previousCursor,
                out currentCursor))
        {
            RebaseInheritedCursorDescendants(
                child,
                previousCursor,
                currentCursor,
                recomputedElements,
                ref elementCount);
        }
        else
        {
            RecomputeInheritedDescendants(child, recomputedElements, ref elementCount);
        }
    }

    private bool TryGetSingleInheritedCursorChange(
        IReadOnlyDictionary<string, string> previous,
        IReadOnlyDictionary<string, string> current,
        out string previousCursor,
        out string currentCursor)
    {
        previousCursor = string.Empty;
        currentCursor = string.Empty;
        var foundCursorChange = false;
        foreach (var pair in previous)
        {
            if (current.TryGetValue(pair.Key, out var currentValue)
                && string.Equals(pair.Value, currentValue, StringComparison.Ordinal))
            {
                continue;
            }

            if (!PropertyMayPropagateToDescendants(pair.Key))
            {
                continue;
            }

            if (!string.Equals(pair.Key, "cursor", StringComparison.OrdinalIgnoreCase)
                || !current.TryGetValue(pair.Key, out currentValue)
                || foundCursorChange)
            {
                return false;
            }

            previousCursor = pair.Value;
            currentCursor = currentValue;
            foundCursorChange = true;
        }

        foreach (var pair in current)
        {
            if (previous.ContainsKey(pair.Key)
                || !PropertyMayPropagateToDescendants(pair.Key))
            {
                continue;
            }

            // New inherited properties need a full cascade because a child may
            // already carry a CSS-wide keyword or variable dependency.
            return false;
        }

        return foundCursorChange;
    }

    private static bool ElementMayDeclareCursor(AvaloniaDomElement element)
        => element.HasOwnCursorDeclaration;

    private bool PropertyMayPropagateToDescendants(string property)
    {
        return property.StartsWith("--", StringComparison.Ordinal)
               || OrdinaryPropertyMayPropagateToDescendants(property);
    }

    private bool OrdinaryPropertyMayPropagateToDescendants(string property)
        => _hasBroadExplicitInheritance
           || _hasBroadInlineExplicitInheritance
           || s_inheritedProperties.Contains(property)
           || _explicitlyInheritedProperties.Contains(property)
           || _inlineExplicitlyInheritedProperties.Contains(property);

    private static bool InlineStyleMayAffectDescendants(string? oldStyle, string? newStyle)
    {
        return ContainsInheritedStyleProperty(oldStyle) || ContainsInheritedStyleProperty(newStyle);
    }

    private static bool IsCustomPropertyOnlyMutation(AvaloniaDomElement target, string? oldStyle)
    {
        var currentOrdinaryCount = 0;
        foreach (var pair in target.StyleValues)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                currentOrdinaryCount++;
            }
        }

        var oldOrdinaryCount = 0;
        var remaining = oldStyle.AsSpan();
        while (!remaining.IsEmpty)
        {
            var terminator = remaining.IndexOf(';');
            var declaration = terminator >= 0 ? remaining[..terminator] : remaining;
            remaining = terminator >= 0 ? remaining[(terminator + 1)..] : ReadOnlySpan<char>.Empty;
            var separator = declaration.IndexOf(':');
            if (separator <= 0)
            {
                continue;
            }

            var nameSpan = declaration[..separator].Trim();
            if (nameSpan.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            oldOrdinaryCount++;
            var name = CssStyleDeclaration.NormalizePropertyName(nameSpan.ToString());
            if (!target.StyleValues.TryGetValue(name, out var currentValue)
                || currentValue is null
                || !declaration[(separator + 1)..].Trim().Equals(
                    currentValue.AsSpan(),
                    StringComparison.Ordinal))
            {
                return false;
            }
        }

        return oldOrdinaryCount == currentOrdinaryCount;
    }

    private static bool ContainsInheritedStyleProperty(string? style)
    {
        if (string.IsNullOrWhiteSpace(style))
        {
            return false;
        }

        if (!s_disableInlineStyleClassificationSpans)
        {
            var remaining = style.AsSpan();
            while (!remaining.IsEmpty)
            {
                var terminator = remaining.IndexOf(';');
                var declaration = terminator >= 0 ? remaining[..terminator] : remaining;
                remaining = terminator >= 0 ? remaining[(terminator + 1)..] : ReadOnlySpan<char>.Empty;
                var separator = declaration.IndexOf(':');
                if (separator <= 0)
                {
                    continue;
                }

                var name = declaration[..separator].Trim();
                if (name.StartsWith("--", StringComparison.Ordinal)
                    || name.Equals("all", StringComparison.OrdinalIgnoreCase)
                    || IsInheritedStyleProperty(name))
                {
                    return true;
                }
            }

            return false;
        }

        foreach (var declaration in style.Split(';'))
        {
            var separator = declaration.IndexOf(':');
            if (separator <= 0)
            {
                continue;
            }

            var name = CssStyleDeclaration.NormalizePropertyName(declaration[..separator]);
            if (name.StartsWith("--", StringComparison.Ordinal)
                || name.Equals("all", StringComparison.OrdinalIgnoreCase)
                || s_inheritedProperties.Contains(name))
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsInheritedStyleProperty(ReadOnlySpan<char> name)
    {
        foreach (var property in s_inheritedProperties)
        {
            if (name.Equals(property, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    private static bool InlineStyleMayAffectLayout(string? oldStyle, string? newStyle)
        => ContainsLayoutStyleProperty(oldStyle) || ContainsLayoutStyleProperty(newStyle);

    private static bool ContainsLayoutStyleProperty(string? style)
    {
        if (string.IsNullOrWhiteSpace(style))
        {
            return false;
        }

        if (!s_disableInlineStyleClassificationSpans)
        {
            var remaining = style.AsSpan();
            while (!remaining.IsEmpty)
            {
                var terminator = remaining.IndexOf(';');
                var declaration = terminator >= 0 ? remaining[..terminator] : remaining;
                remaining = terminator >= 0 ? remaining[(terminator + 1)..] : ReadOnlySpan<char>.Empty;
                var separator = declaration.IndexOf(':');
                if (separator <= 0)
                {
                    continue;
                }

                var name = declaration[..separator].Trim();
                if (name.StartsWith("--", StringComparison.Ordinal))
                {
                    continue;
                }

                if (IsLayoutStyleProperty(name))
                {
                    return true;
                }
            }

            return false;
        }

        foreach (var declaration in style.Split(';'))
        {
            var separator = declaration.IndexOf(':');
            if (separator <= 0)
            {
                continue;
            }

            var name = CssStyleDeclaration.NormalizePropertyName(declaration[..separator]);
            if (name is "display" or "position" or "top" or "right" or "bottom" or "left"
                or "inset" or "width" or "height" or "min-width" or "min-height"
                or "max-width" or "max-height" or "margin" or "padding" or "overflow"
                or "box-sizing" or "flex" or "flex-basis" or "flex-direction" or "flex-flow"
                or "flex-grow" or "flex-shrink" or "flex-wrap" or "grid" or "grid-template-columns"
                or "grid-template-rows" or "grid-area" or "grid-row" or "grid-row-start" or "grid-row-end"
                or "grid-column" or "grid-column-start" or "grid-column-end"
                or "align-content" or "align-items" or "align-self"
                or "justify-content" or "gap" or "order" or "row-gap" or "column-gap"
                or "z-index" or "white-space" or "vertical-align")
            {
                return true;
            }
        }

        return false;
    }

    private static bool IsLayoutStyleProperty(ReadOnlySpan<char> name)
    {
        foreach (var property in s_layoutStyleProperties)
        {
            if (name.Equals(property, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    private static readonly string[] s_layoutStyleProperties =
    [
        "display", "position", "top", "right", "bottom", "left", "inset", "width", "height",
        "min-width", "min-height", "max-width", "max-height", "margin", "padding", "overflow",
        "box-sizing", "flex", "flex-basis", "flex-direction", "flex-flow", "flex-grow", "flex-shrink", "flex-wrap", "grid",
        "grid-template-columns", "grid-template-rows", "grid-area", "grid-row", "grid-row-start", "grid-row-end",
        "grid-column", "grid-column-start", "grid-column-end",
        "align-content", "align-items", "align-self", "justify-content",
        "gap", "order", "row-gap", "column-gap", "z-index", "white-space", "vertical-align"
    ];

    private void AddDirtyRoot(AvaloniaDomElement target)
    {
        foreach (var existing in _dirtyRoots.ToArray())
        {
            if (IsDescendantOrSelf(target, existing))
            {
                return;
            }

            if (IsDescendantOrSelf(existing, target))
            {
                _dirtyRoots.Remove(existing);
            }
        }

        _dirtyRoots.Add(target);
    }

    private void AddDirtyCustomPropertyRoot(AvaloniaDomElement target)
    {
        foreach (var existing in _dirtyCustomPropertyRoots.ToArray())
        {
            if (IsDescendantOrSelf(target, existing))
            {
                return;
            }

            if (IsDescendantOrSelf(existing, target))
            {
                _dirtyCustomPropertyRoots.Remove(existing);
            }
        }

        _dirtyCustomPropertyRoots.Add(target);
    }

    private static bool IsDescendantOrSelf(AvaloniaDomElement candidate, AvaloniaDomElement ancestor)
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

    private IEnumerable<CascadeRule> CollectCandidateRules(
        AvaloniaDomElement element,
        bool pseudoElements = false)
    {
        var universalRules = pseudoElements ? _pseudoUniversalRules : _universalRules;
        var rulesByTag = pseudoElements ? _pseudoRulesByTag : _rulesByTag;
        var rulesById = pseudoElements ? _pseudoRulesById : _rulesById;
        var rulesByClass = pseudoElements ? _pseudoRulesByClass : _rulesByClass;
        var seen = s_disableCandidateRuleScratch
            ? new HashSet<CascadeRule>()
            : _candidateRuleScratch;
        seen.Clear();

        // Always include universal/attribute-heavy rules.
        foreach (var r in universalRules)
        {
            if (seen.Add(r)) yield return r;
        }

        // Tag bucket
        var tag = element.tagName;
        if (!string.IsNullOrEmpty(tag) && rulesByTag.TryGetValue(tag, out var byTag))
        {
            foreach (var r in byTag)
            {
                if (seen.Add(r)) yield return r;
            }
        }

        // Id bucket (note: element.id may be empty)
        var id = element.id;
        if (!string.IsNullOrEmpty(id) && rulesById.TryGetValue(id, out var byId))
        {
            foreach (var r in byId)
            {
                if (seen.Add(r)) yield return r;
            }
        }

        // Class buckets - an element can match many class-keyed rules
        // We iterate the element's actual classes and union the buckets.
        // To avoid huge duplication we use a cheap identity heuristic: source+order as key is overkill; for perf
        // during bootstrap the duplicate Matches calls are still far fewer than the full rule list.
        // For even better we could unique, but for now yield and let Matches be the filter (it is cheap on miss).
        var clsList = element.Control.Classes;
        if (clsList != null)
        {
            foreach (var cls in clsList)
            {
                if (string.IsNullOrEmpty(cls)) continue;
                if (rulesByClass.TryGetValue(cls, out var byCls))
                {
                    foreach (var r in byCls)
                    {
                        if (seen.Add(r)) yield return r;
                    }
                }
            }
        }
    }

    private bool ElementMayResolveVariables(AvaloniaDomElement element)
    {
        foreach (var pair in element.StyleValues)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                && pair.Value?.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return true;
            }
        }

        // The cache is populated by the first target-only inline-style pass.
        // Without it we cannot safely distinguish matched from merely indexed
        // rules, so retain one conservative full subtree pass to warm it.
        if (!_matchedRuleCache.TryGetValue(element, out var cachedRules))
        {
            return true;
        }

        if (cachedRules.StaticMatchedRules.Any(RuleMayResolveVariables)
            || cachedRules.DynamicCandidateRules.Any(RuleMayResolveVariables))
        {
            return true;
        }

        if (_pseudoElementRuleCount == 0)
        {
            return false;
        }

        // Generated pseudo-elements share the originating element's custom
        // property state. Candidate false positives are cheap and preferable
        // to leaving a generated brush stale.
        return CollectCandidateRules(element, pseudoElements: true)
            .Any(RuleMayResolveVariables);
    }

    private static bool RuleMayResolveVariables(CascadeRule rule)
        => rule.Declarations.Any(static declaration =>
            !declaration.Name.StartsWith("--", StringComparison.Ordinal)
            && declaration.Value.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0);

    private CssComputedValues ComputeForElement(
        AvaloniaDomElement element,
        IReadOnlyDictionary<string, string>? inherited,
        IReadOnlySet<string>? inheritedDeclarations,
        bool reuseMatchedRules,
        out CssDeclaredPropertySet declaredProperties,
        out bool computedValuesFromScratch,
        out bool declaredPropertiesFromScratch,
        out bool ordinaryStyleFromTemplate)
    {
        var collectPerformance = _document.CollectPerformanceMetrics;
        ordinaryStyleFromTemplate = false;
        var phaseStarted = collectPerformance ? Stopwatch.GetTimestamp() : 0;
        var phaseAllocationStarted = collectPerformance ? GC.GetAllocatedBytesForCurrentThread() : 0;
        var winners = RentCascadeWinners();

        // HTML's element-role defaults belong to the user-agent origin, not to
        // CSS initial values. Model that origin below every author selector by
        // using a specificity lower than the universal selector. This keeps the
        // existing HTML/table roles while allowing `display: initial` to resolve
        // to CSS's true initial value (`inline`).
        ApplyHtmlUserAgentDeclarations(element, winners);

        // SVG presentation attributes participate in the author cascade with
        // specificity zero and precede stylesheet rules. They therefore beat
        // inherited values, but a matching author rule (including `*`) and an
        // inline declaration still override them.
        foreach (var property in s_svgPresentationAttributeProperties)
        {
            if (element.TryGetSvgPresentationAttribute(property, out var value))
            {
                SetWinner(
                    winners,
                    property,
                    value,
                    important: false,
                    specificity: 0,
                    sourceOrder: int.MinValue);
            }
        }

        if (reuseMatchedRules
            && !s_disableMatchedRuleCache
            && _matchedRuleCache.TryGetValue(element, out var cachedRules))
        {
            MatchedRuleCacheHitCount++;
            ApplyMatchedRules(winners, cachedRules.StaticMatchedRules);
            foreach (var rule in cachedRules.DynamicCandidateRules)
            {
                SelectorMatchEvaluationCount++;
                if (rule.Selector.Matches(element, _document))
                {
                    ApplyMatchedRule(winners, rule);
                }
            }
        }
        else
        {
            List<CascadeRule>? staticMatchedRules = reuseMatchedRules && !s_disableMatchedRuleCache
                ? new List<CascadeRule>()
                : null;
            List<CascadeRule>? dynamicCandidateRules = null;
            // Use rightmost indexes + universal bucket to avoid O(total rules) per element.
            foreach (var rule in CollectCandidateRules(element))
            {
                var dynamic = rule.Selector.DependsOnDynamicState;
                if (dynamic && staticMatchedRules is not null)
                {
                    (dynamicCandidateRules ??= new List<CascadeRule>()).Add(rule);
                }
                SelectorMatchEvaluationCount++;
                if (!rule.Selector.Matches(element, _document))
                {
                    continue;
                }

                ApplyMatchedRule(winners, rule);
                if (!dynamic)
                {
                    staticMatchedRules?.Add(rule);
                }
            }

            if (staticMatchedRules is not null
                && _matchedRuleCache.Count < MaximumMatchedRuleCacheEntries)
            {
                _matchedRuleCache[element] = new MatchedRuleCacheEntry(
                    staticMatchedRules.Count == 0 ? Array.Empty<CascadeRule>() : staticMatchedRules.ToArray(),
                    dynamicCandidateRules?.ToArray() ?? Array.Empty<CascadeRule>());
            }
        }

        var inlineOrder = int.MaxValue / 2;
        foreach (var pair in element.StyleValues)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal)
                || !string.IsNullOrWhiteSpace(pair.Value))
            {
                SetWinner(winners, CssStyleDeclaration.NormalizePropertyName(pair.Key), pair.Value!,
                    important: element.IsInlineStyleImportant(pair.Key),
                    specificity: 1000,
                    sourceOrder: inlineOrder++);
            }
        }

        // `winners` contains only declarations authored for this element; the
        // inherited values are introduced below. Persist this distinction so a
        // later parent-only cursor change can rebase descendants without
        // guessing from their equal computed values or relying on rule caches.
        element.HasOwnCursorDeclaration = winners.ContainsKey("cursor")
                                          || winners.ContainsKey("all");

        if (collectPerformance)
        {
            ElementStyleRuleMatchTicks += Stopwatch.GetTimestamp() - phaseStarted;
            ElementStyleRuleMatchAllocatedBytes +=
                GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            phaseStarted = Stopwatch.GetTimestamp();
            phaseAllocationStarted = GC.GetAllocatedBytesForCurrentThread();
        }

        var inheritedComputed = inherited as CssComputedValues;
        var inheritedCustomProperties = inheritedComputed?.CustomProperties
                                        ?? CreateCustomPropertyMap(inherited);
        if (s_disableSharedCustomProperties)
        {
            inheritedCustomProperties = inheritedCustomProperties.CloneFlat();
        }
        var inheritedDeclared = inheritedDeclarations as CssDeclaredPropertySet;
        var cacheEligible = !s_disableCascadeTemplateCache
                            && (inherited is null || inheritedComputed is not null)
                            && (inheritedDeclarations is null || inheritedDeclared is not null)
                            // Dependency-specific keys belong in a later stage.
                            // Keep var()-dependent values on the established
                            // allocation-sensitive path for now.
                            && !OrdinaryWinnersMayResolveVariables(winners);
        if (cacheEligible
            && TryGetCascadeTemplate(
                element.tagName,
                winners,
                inheritedComputed?.OrdinaryValues,
                inheritedDeclared?.OrdinaryProperties,
                customDependency: null,
                out var template))
        {
            var cachedCustomProperties = CreateEffectiveCustomProperties(
                winners,
                inherited,
                inheritedCustomProperties);
            computedValuesFromScratch = false;
            declaredPropertiesFromScratch = false;
            ordinaryStyleFromTemplate = true;
            declaredProperties = new CssDeclaredPropertySet(template.Declarations, cachedCustomProperties);
            ReturnCascadeWinners(winners);
            if (collectPerformance)
            {
                ElementStyleValueInitializationTicks += Stopwatch.GetTimestamp() - phaseStarted;
                ElementStyleValueInitializationAllocatedBytes +=
                    GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            }
            return new CssComputedValues(template.Values, cachedCustomProperties);
        }

        var inheritedOrdinaryCount = inheritedComputed?.OrdinaryValues.Count
                                     ?? Math.Min(inherited?.Count ?? 0, ComputedPropertyCapacity);
        var estimatedPropertyCount = Math.Max(
            ComputedPropertyCapacity,
            inheritedOrdinaryCount + winners.Count + PropertyCapacityHeadroom);
        var values = CreateInitialValues(element, estimatedPropertyCount, out computedValuesFromScratch);
        var estimatedDeclaredCount = Math.Max(
            ComputedPropertyCapacity,
            (inheritedDeclared?.OrdinaryProperties.Count ?? Math.Min(inheritedDeclarations?.Count ?? 0, ComputedPropertyCapacity))
            + winners.Count
            + PropertyCapacityHeadroom);
        var ordinaryDeclaredProperties = RentDeclaredPropertySet(estimatedDeclaredCount, out declaredPropertiesFromScratch);
        if (inherited is not null)
        {
            var inheritedOrdinaryValues = inheritedComputed?.OrdinaryValues ?? inherited;
            foreach (var pair in inheritedOrdinaryValues)
            {
                if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                    && s_inheritedProperties.Contains(pair.Key))
                {
                    values[pair.Key] = pair.Value;
                    if (inheritedDeclarations?.Contains(pair.Key) == true)
                    {
                        ordinaryDeclaredProperties.Add(pair.Key);
                    }
                }
            }
        }

        // Keep the pre-cascade value for invalid-at-computed-value-time
        // declarations. CSS var() without either a defined custom property or
        // a valid fallback does not compute to an empty string: the declaration
        // becomes invalid and the property falls back to its inherited/initial
        // value.
        // Invalid-at-computed-value-time fallback is only consulted for normal
        // properties. Custom properties are the inputs to var() resolution and
        // are never read from this map, so cloning large inherited custom-
        // property sets here only multiplies allocation on every recompute.
        var invalidDeclarationFallbacks = CaptureInvalidDeclarationFallbacks(values, winners);

        if (collectPerformance)
        {
            ElementStyleValueInitializationTicks += Stopwatch.GetTimestamp() - phaseStarted;
            ElementStyleValueInitializationAllocatedBytes +=
                GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
            phaseStarted = Stopwatch.GetTimestamp();
            phaseAllocationStarted = GC.GetAllocatedBytesForCurrentThread();
        }

        Dictionary<string, string>? customOverrides = null;
        var initialValues = new CssComputedValues(values, inheritedCustomProperties);
        var orderedWinners = RentOrderedWinners(winners);
        foreach (var pair in orderedWinners)
        {
            var isCustomProperty = pair.Key.StartsWith("--", StringComparison.Ordinal);
            if (!TryResolveCssWideKeyword(pair.Key, pair.Value.Value, inherited, initialValues, out var resolvedValue))
            {
                continue;
            }

            if (isCustomProperty)
            {
                customOverrides ??= new Dictionary<string, string>(StringComparer.Ordinal);
                customOverrides[pair.Key] = resolvedValue;
                continue;
            }

            values[pair.Key] = resolvedValue;
            ordinaryDeclaredProperties.Add(pair.Key);
            var physicalName = MapLogicalProperty(pair.Key);
            if (!string.Equals(physicalName, pair.Key, StringComparison.OrdinalIgnoreCase))
            {
                values[physicalName] = resolvedValue;
                ordinaryDeclaredProperties.Add(physicalName);
            }
        }

        var customProperties = inheritedCustomProperties.WithOverrides(customOverrides);
        var computedValues = new CssComputedValues(values, customProperties);
        ResolveCustomProperties(values, computedValues, invalidDeclarationFallbacks);
        ResolveRelativeFontSize(values, inherited);
        // A custom property may substitute an entire shorthand value. CSS
        // parses that shorthand only after var() substitution has completed;
        // expanding first leaves stale initial longhands such as
        // border-top-color behind.
        CssComputedValueNormalizer.ExpandShorthands(values);
        ResolveGridPlacementCascadeProxies(values, invalidDeclarationFallbacks);
        ResolveListStyleCascadeProxies(
            values,
            inherited,
            invalidDeclarationFallbacks);
        ResolveBorderRadiusCascadeProxies(values);
        ResolveBorderCascadeProxies(values);
        ResolveOutlineCascadeProxies(values, invalidDeclarationFallbacks);
        ResolveCurrentColorValues(values);
        ExpandDeclaredShorthands(ordinaryDeclaredProperties);
        CssComputedValueNormalizer.NormalizeOverflow(values);
        declaredProperties = new CssDeclaredPropertySet(ordinaryDeclaredProperties, customProperties);
        if (cacheEligible
            && TryAddCascadeTemplate(
                element.tagName,
                winners,
                inheritedComputed?.OrdinaryValues,
                inheritedDeclared?.OrdinaryProperties,
                customDependency: null,
                values,
                ordinaryDeclaredProperties,
                out var addedTemplate))
        {
            computedValues = new CssComputedValues(addedTemplate.Values, customProperties);
            declaredProperties = new CssDeclaredPropertySet(addedTemplate.Declarations, customProperties);
            ReturnComputedStyleScratch(values, ordinaryDeclaredProperties);
            computedValuesFromScratch = false;
            declaredPropertiesFromScratch = false;
            ordinaryStyleFromTemplate = true;
        }
        ReturnOrderedWinners(orderedWinners);
        ReturnCascadeWinners(winners);
        if (collectPerformance)
        {
            ElementStyleResolutionTicks += Stopwatch.GetTimestamp() - phaseStarted;
            ElementStyleResolutionAllocatedBytes +=
                GC.GetAllocatedBytesForCurrentThread() - phaseAllocationStarted;
        }
        return computedValues;
    }

    private static CssCustomPropertyMap CreateEffectiveCustomProperties(
        Dictionary<string, CascadeWinner> winners,
        IReadOnlyDictionary<string, string>? inherited,
        CssCustomPropertyMap inheritedCustomProperties)
    {
        Dictionary<string, string>? customOverrides = null;
        foreach (var pair in winners)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                || !TryResolveCssWideKeyword(
                    pair.Key,
                    pair.Value.Value,
                    inherited,
                    inheritedCustomProperties,
                    out var resolvedValue))
            {
                continue;
            }

            customOverrides ??= new Dictionary<string, string>(StringComparer.Ordinal);
            customOverrides[pair.Key] = resolvedValue;
        }
        return inheritedCustomProperties.WithOverrides(customOverrides);
    }

    private bool TryGetCascadeTemplate(
        string tagName,
        Dictionary<string, CascadeWinner> winners,
        CssPropertyValueStore? inheritedValues,
        CssPropertyNameSet? inheritedDeclarations,
        CssCustomPropertyMap? customDependency,
        out CascadeTemplate template)
    {
        template = null!;
        if (s_disableCascadeTemplateCache)
        {
            return false;
        }

        var hash = GetCascadeTemplateHash(
            tagName,
            winners,
            inheritedValues,
            inheritedDeclarations,
            customDependency,
            out _);
        if (!_cascadeTemplates.TryGetValue(hash, out var bucket))
        {
            return false;
        }

        foreach (var candidate in bucket)
        {
            if (!candidate.Matches(
                    tagName,
                    winners,
                    inheritedValues,
                    inheritedDeclarations,
                    customDependency))
            {
                continue;
            }

            template = candidate;
            CascadeTemplateHitCount++;
            return true;
        }

        return false;
    }

    private bool TryAddCascadeTemplate(
        string tagName,
        Dictionary<string, CascadeWinner> winners,
        CssPropertyValueStore? inheritedValues,
        CssPropertyNameSet? inheritedDeclarations,
        CssCustomPropertyMap? customDependency,
        CssPropertyValueStore values,
        CssPropertyNameSet declarations,
        out CascadeTemplate addedTemplate)
    {
        addedTemplate = null!;
        if (s_disableCascadeTemplateCache
            || _cascadeTemplateEntryCount >= MaximumCascadeTemplateEntries)
        {
            return false;
        }

        var hash = GetCascadeTemplateHash(
            tagName,
            winners,
            inheritedValues,
            inheritedDeclarations,
            customDependency,
            out var ordinaryWinnerCount);
        var fingerprint = unchecked((uint)hash);
        if (fingerprint == 0)
        {
            fingerprint = 1;
        }
        var probationSlot = (int)(fingerprint & (CascadeTemplateProbationSlotCount - 1));
        var secondarySlot = (int)(((fingerprint >> 16) ^ (fingerprint * 2654435761U))
                                  & (CascadeTemplateProbationSlotCount - 1));
        if (_cascadeTemplateProbation[probationSlot] != fingerprint
            && _cascadeTemplateSecondaryProbation[secondarySlot] != fingerprint)
        {
            if (_cascadeTemplateProbation[probationSlot] == 0)
            {
                _cascadeTemplateProbation[probationSlot] = fingerprint;
            }
            else
            {
                _cascadeTemplateSecondaryProbation[secondarySlot] = fingerprint;
            }
            return false;
        }

        var signature = new KeyValuePair<string, CascadeWinner>[ordinaryWinnerCount];
        var signatureIndex = 0;
        foreach (var pair in winners)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                signature[signatureIndex++] = pair;
            }
        }

        var templateValues = values.Clone(frozen: true);
        var templateDeclarations = declarations.Clone(frozen: true);
        addedTemplate = new CascadeTemplate(
            tagName,
            inheritedValues,
            inheritedDeclarations,
            customDependency,
            signature,
            templateValues,
            templateDeclarations);
        if (!_cascadeTemplates.TryGetValue(hash, out var bucket))
        {
            bucket = new List<CascadeTemplate>(1);
            _cascadeTemplates[hash] = bucket;
        }
        bucket.Add(addedTemplate);
        _cascadeTemplateEntryCount++;
        return true;
    }

    private static int GetCascadeTemplateHash(
        string tagName,
        Dictionary<string, CascadeWinner> winners,
        CssPropertyValueStore? inheritedValues,
        CssPropertyNameSet? inheritedDeclarations,
        CssCustomPropertyMap? customDependency,
        out int ordinaryWinnerCount)
    {
        var winnerHash = 0;
        ordinaryWinnerCount = 0;
        foreach (var pair in winners)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            ordinaryWinnerCount++;
            winnerHash ^= HashCode.Combine(
                StringComparer.OrdinalIgnoreCase.GetHashCode(pair.Key),
                StringComparer.Ordinal.GetHashCode(pair.Value.Value),
                pair.Value.Important,
                pair.Value.Specificity,
                pair.Value.SourceOrder,
                pair.Value.Sequence);
        }

        return HashCode.Combine(
            StringComparer.OrdinalIgnoreCase.GetHashCode(tagName),
            ordinaryWinnerCount,
            winnerHash,
            inheritedValues is null ? 0 : RuntimeHelpers.GetHashCode(inheritedValues),
            inheritedDeclarations is null ? 0 : RuntimeHelpers.GetHashCode(inheritedDeclarations),
            customDependency is null ? 0 : RuntimeHelpers.GetHashCode(customDependency));
    }

    private static bool OrdinaryWinnersMayResolveVariables(
        Dictionary<string, CascadeWinner> winners)
    {
        foreach (var pair in winners)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                && pair.Value.Value.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                return true;
            }
        }
        return false;
    }

    private void ClearCascadeTemplateCache()
    {
        _cascadeTemplates.Clear();
        Array.Clear(_cascadeTemplateProbation);
        Array.Clear(_cascadeTemplateSecondaryProbation);
        _cascadeTemplateEntryCount = 0;
    }

    private static void ApplyMatchedRules(
        IDictionary<string, CascadeWinner> winners,
        IReadOnlyList<CascadeRule> rules)
    {
        for (var index = 0; index < rules.Count; index++)
        {
            ApplyMatchedRule(winners, rules[index]);
        }
    }

    private static void ApplyMatchedRule(
        IDictionary<string, CascadeWinner> winners,
        CascadeRule rule)
    {
        foreach (var declaration in rule.Declarations)
        {
            SetWinner(winners, declaration.Name, declaration.Value, declaration.Important,
                rule.Selector.Specificity, rule.SourceOrder);
        }
    }

    private void ClearMatchedRuleCache()
    {
        _matchedRuleCache.Clear();
        _reuseMatchedRulesForDirtyPass = false;
    }

    private void RemoveMatchedRuleCacheSubtree(AvaloniaDomElement root)
    {
        foreach (var element in _document.EnumerateStyleElements(root))
        {
            _matchedRuleCache.Remove(element);
        }
    }

    private void ApplyGeneratedPseudoElements(
        AvaloniaDomElement element,
        IReadOnlyDictionary<string, string> originatingValues)
    {
        if (element.Control is not CssLayoutPanel panel)
        {
            return;
        }

        if (_pseudoElementRuleCount == 0
            && panel.BeforePseudoElement is null
            && panel.AfterPseudoElement is null)
        {
            return;
        }

        List<CascadeRule>? matchedPseudoRules = _matchedRuleCache.ContainsKey(element)
            ? new List<CascadeRule>()
            : null;
        var beforeChanged = panel.SetGeneratedPseudoElement(
            "before",
            ComputePseudoElement(element, originatingValues, "before", matchedPseudoRules));
        var afterChanged = panel.SetGeneratedPseudoElement(
            "after",
            ComputePseudoElement(element, originatingValues, "after", matchedPseudoRules));
        if (matchedPseudoRules is not null
            && _matchedRuleCache.TryGetValue(element, out var cacheEntry))
        {
            cacheEntry.PseudoMatchedRules = matchedPseudoRules.Count == 0
                ? Array.Empty<CascadeRule>()
                : matchedPseudoRules.Distinct().ToArray();
        }
        if (beforeChanged || afterChanged)
        {
            panel.RefreshGeneratedPseudoElements();
        }
    }

    private IReadOnlyDictionary<string, string>? ComputePseudoElement(
        AvaloniaDomElement element,
        IReadOnlyDictionary<string, string> originatingValues,
        string pseudoElement,
        List<CascadeRule>? matchedRules)
    {
        var winners = RentCascadeWinners();
        foreach (var rule in CollectCandidateRules(element, pseudoElements: true))
        {
            if (!rule.Selector.MatchesPseudoElement(element, _document, pseudoElement))
            {
                continue;
            }

            matchedRules?.Add(rule);

            foreach (var declaration in rule.Declarations)
            {
                SetWinner(winners, declaration.Name, declaration.Value, declaration.Important,
                    rule.Selector.Specificity, rule.SourceOrder);
            }
        }

        if (winners.Count == 0)
        {
            ReturnCascadeWinners(winners);
            return null;
        }

        var originatingComputed = originatingValues as CssComputedValues;
        var inheritedCustomProperties = originatingComputed?.CustomProperties
                                        ?? CreateCustomPropertyMap(originatingValues);
        if (s_disableSharedCustomProperties)
        {
            inheritedCustomProperties = inheritedCustomProperties.CloneFlat();
        }
        var values = new CssPropertyValueStore(
            Math.Max(0, winners.Count + PropertyCapacityHeadroom - CssKnownProperties.Count));
        var originatingOrdinaryValues = originatingComputed?.OrdinaryValues ?? originatingValues;
        foreach (var pair in originatingOrdinaryValues)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal)
                && s_inheritedProperties.Contains(pair.Key))
            {
                values[pair.Key] = pair.Value;
            }
        }
        var invalidDeclarationFallbacks = CaptureInvalidDeclarationFallbacks(values, winners);
        Dictionary<string, string>? customOverrides = null;
        var orderedWinners = RentOrderedWinners(winners);
        foreach (var pair in orderedWinners)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                customOverrides ??= new Dictionary<string, string>(StringComparer.Ordinal);
                customOverrides[pair.Key] = pair.Value.Value;
                continue;
            }

            values[pair.Key] = pair.Value.Value;
            var physicalName = MapLogicalProperty(pair.Key);
            if (!string.Equals(physicalName, pair.Key, StringComparison.OrdinalIgnoreCase))
            {
                values[physicalName] = pair.Value.Value;
            }
        }

        var computedValues = new CssComputedValues(
            values,
            inheritedCustomProperties.WithOverrides(customOverrides));
        ResolveCustomProperties(values, computedValues, invalidDeclarationFallbacks);
        ResolveRelativeFontSize(values, originatingValues);
        CssComputedValueNormalizer.ExpandShorthands(values);
        ResolveGridPlacementCascadeProxies(values, invalidDeclarationFallbacks);
        ResolveListStyleCascadeProxies(
            values,
            originatingValues,
            invalidDeclarationFallbacks);
        ResolveBorderRadiusCascadeProxies(values);
        ResolveBorderCascadeProxies(values);
        ResolveOutlineCascadeProxies(values, invalidDeclarationFallbacks);
        ResolveCurrentColorValues(values);
        CssComputedValueNormalizer.NormalizeOverflow(values);
        ReturnOrderedWinners(orderedWinners);
        ReturnCascadeWinners(winners);
        return computedValues;
    }

    private static bool TryResolveCssWideKeyword(
        string propertyName,
        string value,
        IReadOnlyDictionary<string, string>? inherited,
        IReadOnlyDictionary<string, string> initialValues,
        out string resolvedValue)
    {
        var keyword = value.Trim().ToLowerInvariant();
        var inherits = propertyName.StartsWith("--", StringComparison.Ordinal)
                       || s_inheritedProperties.Contains(propertyName);
        if (keyword == "inherit" || (keyword == "unset" && inherits))
        {
            if (inherited is not null && inherited.TryGetValue(propertyName, out resolvedValue!))
            {
                return true;
            }

            resolvedValue = string.Empty;
            return false;
        }

        if (keyword == "initial" || keyword == "unset")
        {
            if (keyword == "initial"
                && propertyName is "list-style-type" or "list-style-position")
            {
                resolvedValue = InitialListStyleLonghand(propertyName);
                return true;
            }
            if (initialValues.TryGetValue(propertyName, out resolvedValue!))
            {
                return true;
            }

            resolvedValue = string.Empty;
            return false;
        }

        resolvedValue = value;
        return true;
    }

    private static CssCustomPropertyMap CreateCustomPropertyMap(
        IReadOnlyDictionary<string, string>? values)
    {
        if (values is null || values.Count == 0)
        {
            return CssCustomPropertyMap.Empty;
        }

        Dictionary<string, string>? customProperties = null;
        foreach (var pair in values)
        {
            if (!pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            customProperties ??= new Dictionary<string, string>(StringComparer.Ordinal);
            customProperties[pair.Key] = pair.Value;
        }

        return CssCustomPropertyMap.Create(customProperties);
    }

    private CssComputedValues ComputeForDocumentElement(out CssDeclaredPropertySet declaredProperties)
    {
        var winners = new Dictionary<string, CascadeWinner>(
            CascadeWinnerCapacity,
            CssPropertyNameComparer.Instance);
        foreach (var rule in _rules)
        {
            if (!rule.Selector.MatchesDocumentElement(_document))
            {
                continue;
            }

            foreach (var declaration in rule.Declarations)
            {
                SetWinner(winners, declaration.Name, declaration.Value, declaration.Important,
                    rule.Selector.Specificity, rule.SourceOrder);
            }
        }

        var inlineOrder = int.MaxValue / 2;
        foreach (var pair in _document.documentElement.StyleValues)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal)
                || !string.IsNullOrWhiteSpace(pair.Value))
            {
                SetWinner(
                    winners,
                    pair.Key,
                    pair.Value,
                    important: false,
                    specificity: 1000,
                    sourceOrder: inlineOrder++);
            }
        }

        var estimatedPropertyCount = Math.Max(
            ComputedPropertyCapacity,
            winners.Count + PropertyCapacityHeadroom);
        var values = new CssPropertyValueStore(
            Math.Max(0, estimatedPropertyCount - CssKnownProperties.Count));
        var ordinaryDeclaredProperties = new CssPropertyNameSet(
            Math.Max(0, estimatedPropertyCount - CssKnownProperties.Count));
        Dictionary<string, string>? customProperties = null;
        foreach (var pair in winners.OrderBy(pair => pair.Value.SourceOrder))
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal))
            {
                customProperties ??= new Dictionary<string, string>(StringComparer.Ordinal);
                customProperties[pair.Key] = pair.Value.Value;
                continue;
            }

            values[pair.Key] = pair.Value.Value;
            ordinaryDeclaredProperties.Add(pair.Key);
            var physicalName = MapLogicalProperty(pair.Key);
            if (!string.Equals(physicalName, pair.Key, StringComparison.OrdinalIgnoreCase))
            {
                values[physicalName] = pair.Value.Value;
                ordinaryDeclaredProperties.Add(physicalName);
            }
        }

        var customPropertyMap = CssCustomPropertyMap.Create(customProperties);
        var computedValues = new CssComputedValues(values, customPropertyMap);
        ResolveCustomProperties(values, computedValues);
        ResolveRelativeFontSize(values, inherited: null);
        CssComputedValueNormalizer.ExpandShorthands(values);
        ResolveGridPlacementCascadeProxies(values);
        ResolveListStyleCascadeProxies(values);
        ResolveBorderRadiusCascadeProxies(values);
        ResolveBorderCascadeProxies(values);
        ResolveOutlineCascadeProxies(values);
        ResolveCurrentColorValues(values);
        ExpandDeclaredShorthands(ordinaryDeclaredProperties);
        CssComputedValueNormalizer.NormalizeOverflow(values);
        declaredProperties = new CssDeclaredPropertySet(ordinaryDeclaredProperties, customPropertyMap);
        return computedValues;
    }

    private static string MapLogicalProperty(string propertyName)
        => propertyName.ToLowerInvariant() switch
        {
            "inset-inline-start" => "left",
            "inset-inline-end" => "right",
            "margin-inline-start" => "margin-left",
            "margin-inline-end" => "margin-right",
            "padding-inline-start" => "padding-left",
            "padding-inline-end" => "padding-right",
            "border-inline-start-width" => "border-left-width",
            "border-inline-end-width" => "border-right-width",
            "border-inline-start-color" => "border-left-color",
            "border-inline-end-color" => "border-right-color",
            _ => propertyName
        };

    private static void ExpandDeclaredShorthands(CssPropertyNameSet declared)
    {
        static void AddBox(CssPropertyNameSet values, string shorthand, string prefix, string? suffix = null)
        {
            if (!values.Contains(shorthand)) return;
            foreach (var side in new[] { "top", "right", "bottom", "left" })
            {
                values.Add(suffix is null ? $"{prefix}-{side}" : $"{prefix}-{side}-{suffix}");
            }
        }

        AddBox(declared, "margin", "margin");
        AddBox(declared, "padding", "padding");
        AddBox(declared, "border-width", "border", "width");
        AddBox(declared, "border-color", "border", "color");
        AddBox(declared, "border-style", "border", "style");
        if (declared.Contains("border"))
        {
            foreach (var side in new[] { "top", "right", "bottom", "left" })
            {
                declared.Add($"border-{side}-width");
                declared.Add($"border-{side}-color");
                declared.Add($"border-{side}-style");
            }
        }
        foreach (var side in new[] { "top", "right", "bottom", "left" })
        {
            if (!declared.Contains($"border-{side}")) continue;
            declared.Add($"border-{side}-width");
            declared.Add($"border-{side}-color");
            declared.Add($"border-{side}-style");
        }
        if (declared.Contains("inset"))
        {
            declared.UnionWith(new[] { "top", "right", "bottom", "left" });
        }
        if (declared.Contains("overflow")) declared.UnionWith(new[] { "overflow-x", "overflow-y" });
        if (declared.Contains("gap")) declared.UnionWith(new[] { "row-gap", "column-gap" });
        if (declared.Contains("grid-area"))
        {
            declared.UnionWith(new[]
            {
                "grid-row-start", "grid-column-start", "grid-row-end", "grid-column-end"
            });
        }
        if (declared.Contains("grid-row"))
        {
            declared.UnionWith(new[] { "grid-row-start", "grid-row-end" });
        }
        if (declared.Contains("grid-column"))
        {
            declared.UnionWith(new[] { "grid-column-start", "grid-column-end" });
        }
        if (declared.Contains("flex-flow")) declared.UnionWith(new[] { "flex-direction", "flex-wrap" });
        if (declared.Contains("flex")) declared.UnionWith(new[] { "flex-grow", "flex-shrink", "flex-basis" });
        if (declared.Contains("background")) declared.Add("background-color");
        if (declared.Contains("outline"))
        {
            declared.UnionWith(new[] { "outline-color", "outline-style", "outline-width" });
        }
        if (declared.Contains("font"))
        {
            declared.UnionWith(new[]
            {
                "font-family", "font-size", "font-style", "font-variant", "font-weight", "line-height"
            });
        }
        if (declared.Contains("list-style"))
        {
            declared.UnionWith(new[] { "list-style-type", "list-style-position" });
        }
    }

    private CssPropertyValueStore CreateInitialValues(
        AvaloniaDomElement element,
        int capacity,
        out bool fromScratch)
    {
        var values = RentComputedValues(capacity, out fromScratch);
        values["display"] = "inline";
        values["position"] = "static";
        values["left"] = "auto";
        values["top"] = "auto";
        values["right"] = "auto";
        values["bottom"] = "auto";
        values["width"] = "auto";
        values["height"] = "auto";
        values["min-width"] = "auto";
        values["min-height"] = "auto";
        values["max-width"] = "none";
        values["max-height"] = "none";
        values["box-sizing"] = "content-box";
        values["overflow-x"] = "visible";
        values["overflow-y"] = "visible";
        values["visibility"] = "visible";
        values["direction"] = "ltr";
        values["pointer-events"] = "auto";
        values["opacity"] = "1";
        values["background-color"] = "transparent";
        values["fill"] = "black";
        values["fill-rule"] = "nonzero";
        values["stroke"] = "none";
        values["stroke-linecap"] = "butt";
        values["stroke-linejoin"] = "miter";
        values["stroke-width"] = "1px";
        values["font-family"] = "sans-serif";
        values["font-size"] = "16px";
        values["font-style"] = "normal";
        values["font-variant"] = "normal";
        values["font-weight"] = "400";
        values["line-height"] = "normal";
        values["list-style-type"] = "disc";
        values["list-style-position"] = "outside";
        values["vertical-align"] = "baseline";
        values["letter-spacing"] = "normal";
        values["z-index"] = "auto";
        values["margin-top"] = "0px";
        values["margin-right"] = "0px";
        values["margin-bottom"] = "0px";
        values["margin-left"] = "0px";
        values["padding-top"] = "0px";
        values["padding-right"] = "0px";
        values["padding-bottom"] = "0px";
        values["padding-left"] = "0px";
        values["border-top-width"] = "0px";
        values["border-right-width"] = "0px";
        values["border-bottom-width"] = "0px";
        values["border-left-width"] = "0px";
        values["border-top-style"] = "none";
        values["border-right-style"] = "none";
        values["border-bottom-style"] = "none";
        values["border-left-style"] = "none";
        values["border-top-color"] = "currentcolor";
        values["border-right-color"] = "currentcolor";
        values["border-bottom-color"] = "currentcolor";
        values["border-left-color"] = "currentcolor";
        values["outline-color"] = "currentcolor";
        values["outline-style"] = "none";
        values["outline-width"] = "medium";
        values["outline-offset"] = "0px";
        values["flex-direction"] = "row";
        values["flex-wrap"] = "nowrap";
        values["flex-grow"] = "0";
        values["flex-shrink"] = "1";
        values["flex-basis"] = "auto";
        values["justify-content"] = "normal";
        values["align-content"] = "normal";
        values["align-items"] = "normal";
        values["align-self"] = "auto";
        values["order"] = "0";
        values["row-gap"] = "0px";
        values["column-gap"] = "0px";
        values["grid-template-columns"] = "none";
        values["grid-template-rows"] = "none";
        values["grid-area"] = "auto";
        values["grid-row"] = "auto";
        values["grid-row-start"] = "auto";
        values["grid-row-end"] = "auto";
        values["grid-column"] = "auto";
        values["grid-column-start"] = "auto";
        values["grid-column-end"] = "auto";
        return values;
    }

    private static void ApplyHtmlUserAgentDeclarations(
        AvaloniaDomElement element,
        IDictionary<string, CascadeWinner> winners)
    {
        const int userAgentSpecificity = -1;
        const int userAgentSourceOrder = int.MinValue;
        var tag = element.tagName.ToLowerInvariant();
        var display = tag switch
        {
            "address" or "article" or "aside" or "blockquote" or "body" or "dd" or "details" or "dialog" or "div" or
            "dl" or "dt" or "fieldset" or "figcaption" or "figure" or "footer" or "form" or
            "h1" or "h2" or "h3" or "h4" or "h5" or "h6" or "header" or "hgroup" or "hr" or
            "html" or "legend" or "main" or "menu" or "nav" or "ol" or "p" or "pre" or
            "search" or "section" or "ul" => "block",
            "li" or "summary" => "list-item",
            "input" when string.Equals(
                element.getAttribute("type")?.Trim(),
                "hidden",
                StringComparison.OrdinalIgnoreCase) => "none",
            "img" or "canvas" or "input" or "textarea" or "button" or "select" => "inline-block",
            "table" => "table",
            "tbody" => "table-row-group",
            "thead" => "table-header-group",
            "tfoot" => "table-footer-group",
            "tr" => "table-row",
            "td" or "th" => "table-cell",
            "colgroup" => "table-column-group",
            "col" => "table-column",
            "caption" => "table-caption",
            "style" or "link" or "base" or "script" or "head" or "meta" or "title" or "template" => "none",
            _ => null
        };
        if (display is not null)
        {
            SetWinner(winners, "display", display, false, userAgentSpecificity, userAgentSourceOrder);
        }

        if (tag is "ul" or "ol" or "menu")
        {
            SetWinner(winners, "margin-top", "1em", false, userAgentSpecificity, userAgentSourceOrder);
            SetWinner(winners, "margin-bottom", "1em", false, userAgentSpecificity, userAgentSourceOrder);
            SetWinner(winners, "padding-left", "40px", false, userAgentSpecificity, userAgentSourceOrder);
        }
        if (tag == "ol")
        {
            SetWinner(winners, "list-style-type", "decimal", false, userAgentSpecificity, userAgentSourceOrder);
        }
        else if (tag is "ul" or "menu")
        {
            SetWinner(winners, "list-style-type", "disc", false, userAgentSpecificity, userAgentSourceOrder);
        }
    }

    private CssPropertyValueStore RentComputedValues(int capacity, out bool fromScratch)
    {
        var values = s_disableComputedStyleScratch ? null : _computedValueScratch;
        _computedValueScratch = null;
        if (values is null)
        {
            fromScratch = false;
            return new CssPropertyValueStore(Math.Max(0, capacity - CssKnownProperties.Count));
        }

        fromScratch = true;
        values.Clear();
        return values;
    }

    private CssPropertyNameSet RentDeclaredPropertySet(int capacity, out bool fromScratch)
    {
        var properties = s_disableComputedStyleScratch ? null : _declaredPropertyScratch;
        _declaredPropertyScratch = null;
        if (properties is null)
        {
            fromScratch = false;
            return new CssPropertyNameSet(Math.Max(0, capacity - CssKnownProperties.Count));
        }

        fromScratch = true;
        properties.Clear();
        return properties;
    }

    private Dictionary<string, CascadeWinner> RentCascadeWinners()
    {
        var winners = s_disableCascadeWinnerScratch ? null : _cascadeWinnerScratch;
        _cascadeWinnerScratch = null;
        if (winners is null)
        {
            return new Dictionary<string, CascadeWinner>(
                CascadeWinnerCapacity,
                CssPropertyNameComparer.Instance);
        }

        winners.Clear();
        winners.EnsureCapacity(CascadeWinnerCapacity);
        return winners;
    }

    private void ReturnCascadeWinners(Dictionary<string, CascadeWinner> winners)
    {
        if (s_disableCascadeWinnerScratch
            || _cascadeWinnerScratch is not null
            || winners.EnsureCapacity(0) > MaximumRetainedStyleScratchCapacity)
        {
            return;
        }

        winners.Clear();
        _cascadeWinnerScratch = winners;
    }

    private List<KeyValuePair<string, CascadeWinner>> RentOrderedWinners(
        Dictionary<string, CascadeWinner> winners)
    {
        var ordered = s_disableOrderedWinnerScratch ? null : _orderedWinnerScratch;
        _orderedWinnerScratch = null;
        if (ordered is null)
        {
            ordered = new List<KeyValuePair<string, CascadeWinner>>(
                Math.Max(CascadeWinnerCapacity, winners.Count));
        }
        else
        {
            ordered.Clear();
            ordered.EnsureCapacity(winners.Count);
        }

        ordered.AddRange(winners);
        ordered.Sort(static (left, right) =>
        {
            var sourceOrder = left.Value.SourceOrder.CompareTo(right.Value.SourceOrder);
            return sourceOrder != 0
                ? sourceOrder
                : left.Value.Sequence.CompareTo(right.Value.Sequence);
        });
        return ordered;
    }

    private void ReturnOrderedWinners(List<KeyValuePair<string, CascadeWinner>> ordered)
    {
        if (s_disableOrderedWinnerScratch
            || _orderedWinnerScratch is not null
            || ordered.Capacity > MaximumRetainedStyleScratchCapacity)
        {
            return;
        }

        ordered.Clear();
        _orderedWinnerScratch = ordered;
    }

    private void ReturnComputedStyleScratch(
        CssPropertyValueStore? values,
        CssPropertyNameSet? declaredProperties)
    {
        if (s_disableComputedStyleScratch)
        {
            return;
        }

        if (_computedValueScratch is null
            && values is not null
            && !values.IsFrozen
            && values.Count <= MaximumRetainedStyleScratchCapacity)
        {
            _computedValueScratch = values;
        }

        if (_declaredPropertyScratch is null
            && declaredProperties is not null
            && !declaredProperties.IsFrozen
            && declaredProperties.Count <= MaximumRetainedStyleScratchCapacity)
        {
            _declaredPropertyScratch = declaredProperties;
        }
    }

    private readonly record struct SharedOrdinaryStyle(
        CssPropertyValueStore Values,
        CssPropertyNameSet Declarations);

    private const string BorderCascadeProxyPrefix = "-webscene-border-cascade:";
    private const string BorderRadiusCascadeProxyPrefix = "-webscene-border-radius-cascade:";
    private const string GridPlacementCascadeProxyPrefix = "-webscene-grid-placement-cascade:";
    private const string ListStyleCascadeProxyPrefix = "-webscene-list-style-cascade:";
    private const string OutlineCascadeProxyPrefix = "-webscene-outline-cascade:";

    private static void SetWinner(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        name = CssStyleDeclaration.NormalizePropertyName(name);
        if (name == "all")
        {
            var keyword = value.Trim().ToLowerInvariant();
            if (keyword is not ("initial" or "inherit" or "unset"))
            {
                return;
            }

            // `all` participates in the cascade as if the CSS-wide keyword
            // were declared for every ordinary property. Expand it before
            // choosing per-property winners so declarations before/after it,
            // !important, selector specificity, and the later inline origin
            // retain their normal precedence. CSS explicitly excludes custom
            // properties and direction from this shorthand.
            CssCascade.ApplyWinner(
                winners, name, keyword, important, specificity, sourceOrder);
            var targets = CssKnownProperties.Names
                .Concat(winners.Keys)
                .Where(static property =>
                    property is not ("all" or "direction" or "unicode-bidi")
                    && !property.StartsWith("--", StringComparison.Ordinal))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray();
            foreach (var property in targets)
            {
                CssCascade.ApplyWinner(
                    winners, property, keyword, important, specificity, sourceOrder);
            }
            return;
        }
        if (TryApplyGapCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyGridPlacementCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyListStyleCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyBackgroundCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyBorderRadiusCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyBorderCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }
        if (TryApplyOutlineCascadeDeclaration(
                winners,
                name,
                value,
                important,
                specificity,
                sourceOrder))
        {
            return;
        }

        CssCascade.ApplyWinner(winners, name, value, important, specificity, sourceOrder);
    }

    private static bool TryApplyGridPlacementCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name is not ("grid-area" or "grid-row" or "grid-column")) return false;

        var normalized = value.Trim();
        void Apply(string property, string component)
            => CssCascade.ApplyWinner(
                winners,
                property,
                component,
                important,
                specificity,
                sourceOrder);

        if (normalized.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
        {
            Apply(name, normalized);
            if (name is "grid-area" or "grid-row")
            {
                Apply("grid-row-start", $"{GridPlacementCascadeProxyPrefix}0:{name}:{normalized}");
                Apply("grid-row-end", $"{GridPlacementCascadeProxyPrefix}2:{name}:{normalized}");
            }
            if (name is "grid-area" or "grid-column")
            {
                Apply("grid-column-start", $"{GridPlacementCascadeProxyPrefix}1:{name}:{normalized}");
                Apply("grid-column-end", $"{GridPlacementCascadeProxyPrefix}3:{name}:{normalized}");
            }
            return true;
        }

        if (!CssComputedValueNormalizer.TryExpandGridPlacementShorthand(
                name,
                normalized,
                out var rowStart,
                out var columnStart,
                out var rowEnd,
                out var columnEnd))
        {
            // Invalid shorthand declarations do not disturb prior longhands.
            return true;
        }

        Apply(name, normalized);
        if (name is "grid-area" or "grid-row")
        {
            Apply("grid-row-start", rowStart);
            Apply("grid-row-end", rowEnd);
        }
        if (name is "grid-area" or "grid-column")
        {
            Apply("grid-column-start", columnStart);
            Apply("grid-column-end", columnEnd);
        }
        return true;
    }

    private static bool TryApplyListStyleCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name != "list-style") return false;

        var normalized = value.Trim();
        if (normalized.IndexOf("var(", StringComparison.OrdinalIgnoreCase) < 0
            && !IsCssWideListStyleKeyword(normalized)
            && !TryParseListStyleShorthand(normalized, out _, out _))
        {
            // A syntactically invalid shorthand is ignored as a declaration;
            // it must not reset either longhand to its initial value.
            return true;
        }
        CssCascade.ApplyWinner(winners, name, normalized, important, specificity, sourceOrder);
        var cssWide = IsCssWideListStyleKeyword(normalized);
        CssCascade.ApplyWinner(
            winners,
            "list-style-type",
            cssWide ? normalized : $"{ListStyleCascadeProxyPrefix}0:{normalized}",
            important,
            specificity,
            sourceOrder);
        CssCascade.ApplyWinner(
            winners,
            "list-style-position",
            cssWide ? normalized : $"{ListStyleCascadeProxyPrefix}1:{normalized}",
            important,
            specificity,
            sourceOrder);
        return true;
    }

    private static void ResolveGridPlacementCascadeProxies(
        CssPropertyValueStore values,
        IReadOnlyDictionary<string, string>? invalidDeclarationFallbacks = null)
    {
        Resolve("grid-row-start", expectedComponent: 0);
        Resolve("grid-column-start", expectedComponent: 1);
        Resolve("grid-row-end", expectedComponent: 2);
        Resolve("grid-column-end", expectedComponent: 3);
        return;

        void Resolve(string property, int expectedComponent)
        {
            if (!values.TryGetValue(property, out var candidate)
                || !candidate.StartsWith(GridPlacementCascadeProxyPrefix, StringComparison.Ordinal))
            {
                return;
            }

            var componentSeparator = candidate.IndexOf(':', GridPlacementCascadeProxyPrefix.Length);
            var nameSeparator = componentSeparator < 0
                ? -1
                : candidate.IndexOf(':', componentSeparator + 1);
            if (componentSeparator < 0
                || nameSeparator < 0
                || !int.TryParse(
                    candidate.AsSpan(
                        GridPlacementCascadeProxyPrefix.Length,
                        componentSeparator - GridPlacementCascadeProxyPrefix.Length),
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out var encodedComponent)
                || encodedComponent != expectedComponent)
            {
                return;
            }

            var shorthandName = candidate[(componentSeparator + 1)..nameSeparator];
            var shorthand = candidate[(nameSeparator + 1)..];
            if (!CssComputedValueNormalizer.TryExpandGridPlacementShorthand(
                    shorthandName,
                    shorthand,
                    out var rowStart,
                    out var columnStart,
                    out var rowEnd,
                    out var columnEnd))
            {
                values[property] = invalidDeclarationFallbacks is not null
                                   && invalidDeclarationFallbacks.TryGetValue(property, out var fallback)
                    ? fallback
                    : "auto";
                return;
            }

            values[property] = encodedComponent switch
            {
                0 => rowStart,
                1 => columnStart,
                2 => rowEnd,
                _ => columnEnd
            };
        }
    }

    private static void ResolveListStyleCascadeProxies(
        CssPropertyValueStore values,
        IReadOnlyDictionary<string, string>? inherited = null,
        IReadOnlyDictionary<string, string>? invalidDeclarationFallbacks = null)
    {
        Resolve("list-style-type", component: 0);
        Resolve("list-style-position", component: 1);
        return;

        void Resolve(string property, int component)
        {
            if (!values.TryGetValue(property, out var candidate)
                || !candidate.StartsWith(ListStyleCascadeProxyPrefix, StringComparison.Ordinal))
            {
                return;
            }

            var separator = candidate.IndexOf(':', ListStyleCascadeProxyPrefix.Length);
            if (separator < 0
                || !int.TryParse(
                    candidate.AsSpan(ListStyleCascadeProxyPrefix.Length, separator - ListStyleCascadeProxyPrefix.Length),
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out var encodedComponent))
            {
                return;
            }

            var shorthand = candidate[(separator + 1)..].Trim();
            if (IsCssWideListStyleKeyword(shorthand))
            {
                values[property] = shorthand.Equals("initial", StringComparison.OrdinalIgnoreCase)
                    ? InitialListStyleLonghand(property)
                    : inherited is not null && inherited.TryGetValue(property, out var inheritedValue)
                        ? inheritedValue
                        : InitialListStyleLonghand(property);
                return;
            }

            if (!TryParseListStyleShorthand(shorthand, out var type, out var position))
            {
                values[property] = invalidDeclarationFallbacks is not null
                                   && invalidDeclarationFallbacks.TryGetValue(property, out var fallback)
                    ? fallback
                    : InitialListStyleLonghand(property);
                return;
            }
            if (encodedComponent != component) return;
            values[property] = component == 0 ? type : position;
        }
    }

    private static bool TryParseListStyleShorthand(
        string value,
        out string type,
        out string position)
    {
        type = "disc";
        position = "outside";
        var tokens = SplitCssTokens(value.Trim());
        if (tokens.Count is < 1 or > 2) return false;

        var hasType = false;
        var hasPosition = false;
        foreach (var token in tokens)
        {
            switch (token.ToLowerInvariant())
            {
                case "inside":
                case "outside":
                    if (hasPosition) return false;
                    position = token.ToLowerInvariant();
                    hasPosition = true;
                    break;
                case "disc":
                case "circle":
                case "square":
                case "decimal":
                case "none":
                    if (hasType) return false;
                    type = token.ToLowerInvariant();
                    hasType = true;
                    break;
                default:
                    return false;
            }
        }
        return true;
    }

    private static bool IsCssWideListStyleKeyword(string value)
        => value.Trim().ToLowerInvariant() is "inherit" or "initial" or "unset";

    private static string InitialListStyleLonghand(string property)
        => property == "list-style-position" ? "outside" : "disc";

    private static void ResolveRelativeFontSize(
        CssPropertyValueStore values,
        IReadOnlyDictionary<string, string>? inherited)
    {
        if (!values.TryGetValue("font-size", out var authored)) return;
        var normalized = authored.Trim();
        var inheritedPixels = 16d;
        if (inherited is not null
            && inherited.TryGetValue("font-size", out var inheritedSize)
            && CssLengthParser.TryParseAbsoluteLength(inheritedSize, out var parsedInherited))
        {
            inheritedPixels = parsedInherited;
        }

        double factor;
        if (normalized.EndsWith('%')
            && double.TryParse(normalized.AsSpan(0, normalized.Length - 1), NumberStyles.Float, CultureInfo.InvariantCulture, out var percent))
        {
            factor = percent / 100d;
        }
        else if (normalized.EndsWith("em", StringComparison.OrdinalIgnoreCase)
                 && !normalized.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
                 && double.TryParse(normalized.AsSpan(0, normalized.Length - 2), NumberStyles.Float, CultureInfo.InvariantCulture, out var em))
        {
            factor = em;
        }
        else
        {
            return;
        }

        values["font-size"] = (inheritedPixels * factor).ToString("0.###", CultureInfo.InvariantCulture) + "px";
    }

    private static bool TryApplyGapCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name != "gap") return false;
        var tokens = SplitCssTokens(value.Trim());
        if (tokens.Count is < 1 or > 2) return false;
        CssCascade.ApplyWinner(
            winners, "row-gap", tokens[0], important, specificity, sourceOrder);
        CssCascade.ApplyWinner(
            winners, "column-gap", tokens.Count == 2 ? tokens[1] : tokens[0], important, specificity, sourceOrder);
        return true;
    }

    private static bool TryApplyBorderRadiusCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name != "border-radius") return false;

        // Retain the shorthand for computed-style serialization while the
        // corner proxies below participate independently in the cascade.
        CssCascade.ApplyWinner(
            winners,
            name,
            value.Trim(),
            important,
            specificity,
            sourceOrder);
        for (var index = 0; index < BorderRadiusCorners.Length; index++)
        {
            CssCascade.ApplyWinner(
                winners,
                BorderRadiusCorners[index],
                $"{BorderRadiusCascadeProxyPrefix}{index}:{value.Trim()}",
                important,
                specificity,
                sourceOrder);
        }
        return true;
    }

    private static bool TryApplyBackgroundCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name != "background")
        {
            return false;
        }

        var normalized = value.Trim();
        var tokens = SplitCssTokens(normalized);
        string? color = null;
        if (tokens.Count == 1)
        {
            color = tokens[0];
        }
        else
        {
            // currentColor is a valid <color> in the final position of a
            // background layer. This is the component whose loss in the
            // third-party shorthand parser motivated the protected declaration.
            color = tokens.LastOrDefault(token =>
                string.Equals(token, "currentcolor", StringComparison.OrdinalIgnoreCase));
        }

        if (string.IsNullOrWhiteSpace(color))
        {
            // Retain the existing fallback for background forms outside the
            // supported color-only subset.
            return false;
        }

        CssCascade.ApplyWinner(
            winners,
            "background-color",
            color,
            important,
            specificity,
            sourceOrder);
        return true;
    }

    private static bool TryApplyBorderCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        var side = name switch
        {
            "border-top" => "top",
            "border-right" => "right",
            "border-bottom" => "bottom",
            "border-left" => "left",
            _ => null
        };
        if (name != "border"
            && side is null
            && name is not "border-width" and not "border-style" and not "border-color")
        {
            return false;
        }

        void Apply(string property, string componentValue)
            => CssCascade.ApplyWinner(
                winners,
                property,
                componentValue,
                important,
                specificity,
                sourceOrder);

        var normalized = value.Trim();
        var cssWideKeyword = normalized.ToLowerInvariant();
        var isCssWideKeyword = cssWideKeyword is "inherit" or "initial" or "unset" or "revert" or "revert-layer";
        if (name is "border-width" or "border-style" or "border-color")
        {
            var suffix = name["border-".Length..];
            if (isCssWideKeyword)
            {
                foreach (var currentSide in BorderSides)
                {
                    Apply($"border-{currentSide}-{suffix}", cssWideKeyword);
                }
                return true;
            }

            var tokens = SplitCssTokens(normalized);
            if (tokens.Count is < 1 or > 4)
            {
                return true;
            }

            ApplyBorderBox(tokens, suffix, Apply);
            return true;
        }

        if (isCssWideKeyword)
        {
            foreach (var currentSide in side is null ? BorderSides : [side])
            {
                Apply($"border-{currentSide}-width", cssWideKeyword);
                Apply($"border-{currentSide}-style", cssWideKeyword);
                Apply($"border-{currentSide}-color", cssWideKeyword);
            }
            return true;
        }

        var targetSides = side is null ? BorderSides : [side];
        if (normalized.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
        {
            var proxy = BorderCascadeProxyPrefix + name + ":" + normalized;
            foreach (var currentSide in targetSides)
            {
                Apply($"border-{currentSide}-width", proxy);
                Apply($"border-{currentSide}-style", proxy);
                Apply($"border-{currentSide}-color", proxy);
            }
            return true;
        }

        var components = ParseBorderComponents(normalized);
        foreach (var currentSide in targetSides)
        {
            Apply($"border-{currentSide}-width", components.Width);
            Apply($"border-{currentSide}-style", components.Style);
            Apply($"border-{currentSide}-color", components.Color);
        }
        return true;
    }

    private static bool TryApplyOutlineCascadeDeclaration(
        IDictionary<string, CascadeWinner> winners,
        string name,
        string value,
        bool important,
        int specificity,
        int sourceOrder)
    {
        if (name != "outline") return false;

        var normalized = value.Trim();
        var keyword = normalized.ToLowerInvariant();
        var isCssWide = keyword is "inherit" or "initial" or "unset" or "revert" or "revert-layer";
        CssCascade.ApplyWinner(winners, name, normalized, important, specificity, sourceOrder);

        void Apply(string property, string component)
            => CssCascade.ApplyWinner(winners, property, component, important, specificity, sourceOrder);

        if (isCssWide)
        {
            Apply("outline-color", keyword);
            Apply("outline-style", keyword);
            Apply("outline-width", keyword);
            return true;
        }

        if (normalized.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
        {
            var proxy = OutlineCascadeProxyPrefix + normalized;
            Apply("outline-color", proxy);
            Apply("outline-style", proxy);
            Apply("outline-width", proxy);
            return true;
        }

        var expanded = new CssPropertyValueStore { ["outline"] = normalized };
        CssComputedValueNormalizer.ExpandShorthands(expanded);
        if (!expanded.TryGetValue("outline-color", out var color)
            || !expanded.TryGetValue("outline-style", out var style)
            || !expanded.TryGetValue("outline-width", out var width))
        {
            // Invalid shorthands are ignored without resetting longhands.
            return true;
        }
        Apply("outline-color", color);
        Apply("outline-style", style);
        Apply("outline-width", width);
        return true;
    }

    private static readonly string[] BorderSides = ["top", "right", "bottom", "left"];
    private static readonly string[] BorderRadiusCorners =
    [
        "border-top-left-radius",
        "border-top-right-radius",
        "border-bottom-right-radius",
        "border-bottom-left-radius"
    ];

    private static void ApplyBorderBox(
        IReadOnlyList<string> tokens,
        string suffix,
        Action<string, string> apply)
    {
        var top = tokens[0];
        var right = tokens.Count > 1 ? tokens[1] : top;
        var bottom = tokens.Count > 2 ? tokens[2] : top;
        var left = tokens.Count > 3 ? tokens[3] : right;
        apply($"border-top-{suffix}", top);
        apply($"border-right-{suffix}", right);
        apply($"border-bottom-{suffix}", bottom);
        apply($"border-left-{suffix}", left);
    }

    private static (string Width, string Style, string Color) ParseBorderComponents(string value)
    {
        var tokens = SplitCssTokens(value);
        var width = tokens.FirstOrDefault(token =>
            token is "thin" or "medium" or "thick"
            || CssLayout.TryParseLength(token, out _));
        var style = tokens.FirstOrDefault(token => token is
            "none" or "hidden" or "dotted" or "dashed" or "solid" or "double" or
            "groove" or "ridge" or "inset" or "outset");
        var color = tokens.FirstOrDefault(token =>
            !string.Equals(token, width, StringComparison.Ordinal)
            && !string.Equals(token, style, StringComparison.Ordinal));
        return (width ?? "medium", style ?? "none", color ?? "currentcolor");
    }

    private static void ResolveBorderCascadeProxies(CssPropertyValueStore values)
    {
        foreach (var side in BorderSides)
        foreach (var suffix in new[] { "width", "style", "color" })
        {
            var property = $"border-{side}-{suffix}";
            if (!values.TryGetValue(property, out var value)
                || !value.StartsWith(BorderCascadeProxyPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            var declarationStart = BorderCascadeProxyPrefix.Length;
            var separator = value.IndexOf(':', declarationStart);
            if (separator < 0)
            {
                continue;
            }

            var components = ParseBorderComponents(value[(separator + 1)..]);
            values[property] = suffix switch
            {
                "width" => components.Width,
                "style" => components.Style,
                _ => components.Color
            };
        }
    }

    private static void ResolveOutlineCascadeProxies(
        CssPropertyValueStore values,
        IReadOnlyDictionary<string, string>? invalidDeclarationFallbacks = null)
    {
        Resolve("outline-color", "currentcolor");
        Resolve("outline-style", "none");
        Resolve("outline-width", "medium");

        void Resolve(string property, string initial)
        {
            if (!values.TryGetValue(property, out var candidate)
                || !candidate.StartsWith(OutlineCascadeProxyPrefix, StringComparison.Ordinal))
            {
                return;
            }

            var expanded = new CssPropertyValueStore
            {
                ["outline"] = candidate[OutlineCascadeProxyPrefix.Length..]
            };
            CssComputedValueNormalizer.ExpandShorthands(expanded);
            values[property] = expanded.TryGetValue(property, out var resolved)
                ? resolved
                : invalidDeclarationFallbacks is not null
                  && invalidDeclarationFallbacks.TryGetValue(property, out var fallback)
                    ? fallback
                    : initial;
        }
    }

    private static void ResolveBorderRadiusCascadeProxies(CssPropertyValueStore values)
    {
        for (var index = 0; index < BorderRadiusCorners.Length; index++)
        {
            var property = BorderRadiusCorners[index];
            if (!values.TryGetValue(property, out var value)
                || !value.StartsWith(BorderRadiusCascadeProxyPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            var declarationStart = BorderRadiusCascadeProxyPrefix.Length;
            var separator = value.IndexOf(':', declarationStart);
            if (separator < 0
                || !int.TryParse(value.AsSpan(declarationStart, separator - declarationStart), out var corner)
                || corner < 0
                || corner >= BorderRadiusCorners.Length)
            {
                continue;
            }

            var expanded = new CssPropertyValueStore
            {
                ["border-radius"] = value[(separator + 1)..]
            };
            CssComputedValueNormalizer.ExpandShorthands(expanded);
            if (expanded.TryGetValue(BorderRadiusCorners[corner], out var resolved))
            {
                values[property] = resolved;
            }
        }
    }

    private static void ResolveCurrentColorValues(CssPropertyValueStore values)
    {
        var color = values.TryGetValue("color", out var computedColor)
                    && !string.Equals(computedColor.Trim(), "currentcolor", StringComparison.OrdinalIgnoreCase)
            ? computedColor
            : "black";
        Resolve("background-color");
        foreach (var side in BorderSides)
        {
            Resolve($"border-{side}-color");
        }
        Resolve("outline-color");

        void Resolve(string property)
        {
            if (values.TryGetValue(property, out var value)
                && string.Equals(value.Trim(), "currentcolor", StringComparison.OrdinalIgnoreCase))
            {
                values[property] = color;
            }
        }
    }

    private static void ExpandShorthands(CssPropertyValueStore values)
    {
        ExpandBox(values, "margin");
        ExpandBox(values, "padding");
        ExpandBox(values, "border-width", "border", "width");
        ExpandBox(values, "border-color", "border", "color");
        ExpandBox(values, "border-style", "border", "style");
        ExpandBorderDeclaration(values, "border", null);
        foreach (var side in new[] { "top", "right", "bottom", "left" })
        {
            ExpandBorderDeclaration(values, $"border-{side}", side);
        }

        if (values.TryGetValue("background", out var background))
        {
            var tokens = SplitCssTokens(background);
            if (tokens.Count == 1)
            {
                values["background-color"] = tokens[0];
            }
        }

        ExpandFont(values);
        NormalizeLineHeightForNonPixelAbsoluteFont(values);

        if (values.TryGetValue("inset", out var inset))
        {
            ExpandBoxTokens(values, SplitCssTokens(inset), "top", "right", "bottom", "left");
        }

        if (values.TryGetValue("overflow", out var overflow))
        {
            var tokens = SplitCssTokens(overflow);
            if (tokens.Count > 0)
            {
                values["overflow-x"] = tokens[0];
                values["overflow-y"] = tokens.Count > 1 ? tokens[1] : tokens[0];
            }
        }

        if (values.TryGetValue("gap", out var gap))
        {
            var tokens = SplitCssTokens(gap);
            if (tokens.Count > 0)
            {
                values["row-gap"] = tokens[0];
                values["column-gap"] = tokens.Count > 1 ? tokens[1] : tokens[0];
            }
        }

        if (values.TryGetValue("flex-flow", out var flexFlow))
        {
            foreach (var token in SplitCssTokens(flexFlow))
            {
                if (token is "row" or "row-reverse" or "column" or "column-reverse") values["flex-direction"] = token;
                if (token is "nowrap" or "wrap" or "wrap-reverse") values["flex-wrap"] = token;
            }
        }

        if (values.TryGetValue("flex", out var flex))
        {
            var tokens = SplitCssTokens(flex);
            if (tokens.Count == 1 && tokens[0] == "none")
            {
                values["flex-grow"] = "0";
                values["flex-shrink"] = "0";
                values["flex-basis"] = "auto";
            }
            else if (tokens.Count == 1 && tokens[0] == "auto")
            {
                values["flex-grow"] = "1";
                values["flex-shrink"] = "1";
                values["flex-basis"] = "auto";
            }
            else
            {
                if (tokens.Count > 0 && IsNumber(tokens[0])) values["flex-grow"] = tokens[0];
                if (tokens.Count > 1 && IsNumber(tokens[1])) values["flex-shrink"] = tokens[1];
                var basis = tokens.LastOrDefault(token => !IsNumber(token));
                if (!string.IsNullOrWhiteSpace(basis)) values["flex-basis"] = basis;
            }
        }
    }

    private static bool ExpandFont(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("font", out var font))
        {
            return false;
        }

        var tokens = SplitCssTokens(font);
        var sizeIndex = -1;
        for (var index = 0; index < tokens.Count; index++)
        {
            var sizeToken = tokens[index].Split('/', 2)[0];
            if (CssLayout.TryParseAbsoluteLength(sizeToken, out _)
                || sizeToken.EndsWith("em", StringComparison.OrdinalIgnoreCase)
                || sizeToken.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
                || sizeToken.EndsWith("%", StringComparison.OrdinalIgnoreCase))
            {
                sizeIndex = index;
                break;
            }
        }

        if (sizeIndex < 0 || sizeIndex + 1 >= tokens.Count)
        {
            return false;
        }

        var authoredSize = tokens[sizeIndex].Split('/', 2)[0];
        if (authoredSize.EndsWith("px", StringComparison.OrdinalIgnoreCase)
            || !CssLayout.TryParseAbsoluteLength(authoredSize, out _))
        {
            // Preserve the established font shorthand path for pixel and
            // relative sizes. This expansion exists to add the non-pixel
            // absolute CSS units that the Avalonia adapter previously lacked.
            return false;
        }

        values["font-style"] = "normal";
        values["font-variant"] = "normal";
        values["font-weight"] = "normal";
        foreach (var token in tokens.Take(sizeIndex))
        {
            if (token is "italic" or "oblique" or "normal") values["font-style"] = token;
            else if (token is "small-caps") values["font-variant"] = token;
            else if (token is "bold" or "bolder" or "lighter"
                     || int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out _))
            {
                values["font-weight"] = token;
            }
        }

        var sizeAndLineHeight = tokens[sizeIndex].Split('/', 2);
        values["font-size"] = sizeAndLineHeight[0];
        var familyStart = sizeIndex + 1;
        if (sizeAndLineHeight.Length == 2)
        {
            values["line-height"] = sizeAndLineHeight[1];
        }
        else if (familyStart + 1 < tokens.Count && tokens[familyStart] == "/")
        {
            values["line-height"] = tokens[familyStart + 1];
            familyStart += 2;
        }
        else
        {
            values["line-height"] = "normal";
        }

        if (familyStart < tokens.Count)
        {
            values["font-family"] = string.Join(" ", tokens.Skip(familyStart));
        }

        return true;
    }

    private static void NormalizeLineHeightForNonPixelAbsoluteFont(CssPropertyValueStore values)
    {
        if (!values.TryGetValue("font-size", out var fontSize)
            || fontSize.Trim().EndsWith("px", StringComparison.OrdinalIgnoreCase)
            || !CssLayout.TryParseAbsoluteLength(fontSize, out var fontSizePixels)
            || !values.TryGetValue("line-height", out var lineHeight)
            || string.Equals(lineHeight.Trim(), "normal", StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        var normalized = lineHeight.Trim();
        double lineHeightPixels;
        if (normalized.EndsWith("em", StringComparison.OrdinalIgnoreCase)
            && !normalized.EndsWith("rem", StringComparison.OrdinalIgnoreCase)
            && double.TryParse(
                normalized[..^2],
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var em))
        {
            lineHeightPixels = fontSizePixels * em;
        }
        else if (double.TryParse(
                     normalized,
                     NumberStyles.Float,
                     CultureInfo.InvariantCulture,
                     out var multiplier))
        {
            lineHeightPixels = fontSizePixels * multiplier;
        }
        else
        {
            return;
        }

        values["line-height"] = lineHeightPixels.ToString("0.###", CultureInfo.InvariantCulture) + "px";
    }

    private static void ExpandBorderDeclaration(
        CssPropertyValueStore values,
        string shorthand,
        string? side)
    {
        if (!values.TryGetValue(shorthand, out var value))
        {
            return;
        }

        var tokens = SplitCssTokens(value);
        var width = tokens.FirstOrDefault(token =>
            token is "thin" or "medium" or "thick"
            || CssLayout.TryParseLength(token, out _));
        var style = tokens.FirstOrDefault(token => token is
            "none" or "hidden" or "dotted" or "dashed" or "solid" or "double" or
            "groove" or "ridge" or "inset" or "outset");
        var color = tokens.FirstOrDefault(token => !string.Equals(token, width, StringComparison.Ordinal)
                                                   && !string.Equals(token, style, StringComparison.Ordinal));
        var sides = side is null ? new[] { "top", "right", "bottom", "left" } : new[] { side };
        foreach (var currentSide in sides)
        {
            if (width is not null) values[$"border-{currentSide}-width"] = width;
            if (style is not null) values[$"border-{currentSide}-style"] = style;
            if (color is not null) values[$"border-{currentSide}-color"] = color;
        }
    }

    private static void NormalizeComputedOverflow(CssPropertyValueStore values)
    {
        var overflowX = values.TryGetValue("overflow-x", out var x)
            ? x.Trim().ToLowerInvariant()
            : "visible";
        var overflowY = values.TryGetValue("overflow-y", out var y)
            ? y.Trim().ToLowerInvariant()
            : "visible";
        var originalX = overflowX;
        var originalY = overflowY;

        // CSS Overflow 3: when one axis is neither visible nor clip, visible
        // on the other axis computes to auto (and clip computes to hidden).
        // Complex component layouts rely on `overflow-x: hidden` to make otherwise
        // visible indicator-list Y axis a native scrolling viewport.
        if (originalX == "visible" && originalY is not "visible" and not "clip")
        {
            overflowX = "auto";
        }
        else if (originalX == "clip" && originalY is not "visible" and not "clip")
        {
            overflowX = "hidden";
        }

        if (originalY == "visible" && originalX is not "visible" and not "clip")
        {
            overflowY = "auto";
        }
        else if (originalY == "clip" && originalX is not "visible" and not "clip")
        {
            overflowY = "hidden";
        }

        values["overflow-x"] = overflowX;
        values["overflow-y"] = overflowY;
        values["overflow"] = string.Equals(overflowX, overflowY, StringComparison.Ordinal)
            ? overflowX
            : $"{overflowX} {overflowY}";
    }

    private static void ExpandBox(
        CssPropertyValueStore values,
        string shorthand,
        string? prefix = null,
        string? suffix = null)
    {
        if (!values.TryGetValue(shorthand, out var value))
        {
            return;
        }

        prefix ??= shorthand;
        var names = new[] { "top", "right", "bottom", "left" }
            .Select(side => suffix is null ? $"{prefix}-{side}" : $"{prefix}-{side}-{suffix}")
            .ToArray();
        ExpandBoxTokens(values, SplitCssTokens(value), names);
    }

    private static void ExpandBoxTokens(CssPropertyValueStore values, IReadOnlyList<string> tokens, params string[] names)
    {
        if (tokens.Count is < 1 or > 4 || names.Length != 4)
        {
            return;
        }

        var top = tokens[0];
        var right = tokens.Count > 1 ? tokens[1] : top;
        var bottom = tokens.Count > 2 ? tokens[2] : top;
        var left = tokens.Count > 3 ? tokens[3] : right;
        values[names[0]] = top;
        values[names[1]] = right;
        values[names[2]] = bottom;
        values[names[3]] = left;
    }

    private static void ResolveCustomProperties(
        CssPropertyValueStore values,
        IReadOnlyDictionary<string, string> allValues,
        IReadOnlyDictionary<string, string>? invalidDeclarationFallbacks = null)
    {
        // Most computed values are initial/inherited literals and contain no
        // custom-property reference. Snapshot only the keys that can actually
        // change or be removed; this also avoids one cycle-detection set per
        // unaffected property.
        List<string>? keysWithVariables = null;
        foreach (var pair in values)
        {
            if (pair.Value.IndexOf("var(", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                (keysWithVariables ??= new List<string>()).Add(pair.Key);
            }
        }

        if (keysWithVariables is null)
        {
            return;
        }

        foreach (var key in keysWithVariables)
        {
            if (CssVariableResolver.TryResolve(values[key], allValues, out var resolved))
            {
                values[key] = resolved;
            }
            else if (invalidDeclarationFallbacks?.TryGetValue(key, out var fallback) == true)
            {
                values[key] = fallback;
            }
            else
            {
                values.Remove(key);
            }
        }
    }

    private static Dictionary<string, string>? CaptureInvalidDeclarationFallbacks(
        IReadOnlyDictionary<string, string> values,
        IReadOnlyDictionary<string, CascadeWinner> winners)
    {
        Dictionary<string, string>? fallbacks = null;

        foreach (var pair in winners)
        {
            if (pair.Key.StartsWith("--", StringComparison.Ordinal)
                || pair.Value.Value.IndexOf("var(", StringComparison.OrdinalIgnoreCase) < 0)
            {
                continue;
            }

            CaptureInvalidDeclarationFallback(values, pair.Key, ref fallbacks);
            var physicalName = MapLogicalProperty(pair.Key);
            if (!string.Equals(physicalName, pair.Key, StringComparison.OrdinalIgnoreCase))
            {
                CaptureInvalidDeclarationFallback(values, physicalName, ref fallbacks);
            }

            switch (pair.Key)
            {
                case "margin":
                    CaptureInvalidDeclarationFallback(values, "margin-top", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "margin-right", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "margin-bottom", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "margin-left", ref fallbacks);
                    break;
                case "padding":
                    CaptureInvalidDeclarationFallback(values, "padding-top", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "padding-right", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "padding-bottom", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "padding-left", ref fallbacks);
                    break;
                case "border-width":
                    CaptureInvalidDeclarationFallback(values, "border-top-width", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "border-right-width", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "border-bottom-width", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "border-left-width", ref fallbacks);
                    break;
                case "inset":
                    CaptureInvalidDeclarationFallback(values, "top", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "right", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "bottom", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "left", ref fallbacks);
                    break;
                case "overflow":
                    CaptureInvalidDeclarationFallback(values, "overflow-x", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "overflow-y", ref fallbacks);
                    break;
                case "gap":
                    CaptureInvalidDeclarationFallback(values, "row-gap", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "column-gap", ref fallbacks);
                    break;
                case "flex-flow":
                    CaptureInvalidDeclarationFallback(values, "flex-direction", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "flex-wrap", ref fallbacks);
                    break;
                case "flex":
                    CaptureInvalidDeclarationFallback(values, "flex-grow", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "flex-shrink", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "flex-basis", ref fallbacks);
                    break;
                case "background":
                    CaptureInvalidDeclarationFallback(values, "background-color", ref fallbacks);
                    break;
                case "outline":
                    CaptureInvalidDeclarationFallback(values, "outline-color", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "outline-style", ref fallbacks);
                    CaptureInvalidDeclarationFallback(values, "outline-width", ref fallbacks);
                    break;
            }
        }

        return fallbacks;
    }

    private static void CaptureInvalidDeclarationFallback(
        IReadOnlyDictionary<string, string> values,
        string property,
        ref Dictionary<string, string>? fallbacks)
    {
        if (values.TryGetValue(property, out var value))
        {
            (fallbacks ??= new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase))[property] = value;
        }
    }

    private static List<string> SplitCssTokens(string value)
    {
        var result = new List<string>();
        var current = new StringBuilder();
        var depth = 0;
        foreach (var ch in value)
        {
            if (ch == '(') depth++;
            else if (ch == ')') depth--;
            if (char.IsWhiteSpace(ch) && depth == 0)
            {
                if (current.Length > 0)
                {
                    result.Add(current.ToString());
                    current.Clear();
                }
            }
            else
            {
                current.Append(ch);
            }
        }
        if (current.Length > 0) result.Add(current.ToString());
        return result;
    }

    private static bool IsNumber(string value)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out _);

    private sealed record CascadeRule(
        CssComplexSelector Selector,
        IReadOnlyList<CssCascadeDeclaration> Declarations,
        int SourceOrder,
        string Source);

    private sealed record MatchedRuleCacheEntry(
        CascadeRule[] StaticMatchedRules,
        CascadeRule[] DynamicCandidateRules)
    {
        public CascadeRule[] PseudoMatchedRules { get; set; } = Array.Empty<CascadeRule>();
    }

    private sealed record CascadeTemplate(
        string TagName,
        CssPropertyValueStore? InheritedValues,
        CssPropertyNameSet? InheritedDeclarations,
        CssCustomPropertyMap? CustomDependency,
        KeyValuePair<string, CascadeWinner>[] OrdinaryWinners,
        CssPropertyValueStore Values,
        CssPropertyNameSet Declarations)
    {
        internal bool Matches(
            string tagName,
            Dictionary<string, CascadeWinner> winners,
            CssPropertyValueStore? inheritedValues,
            CssPropertyNameSet? inheritedDeclarations,
            CssCustomPropertyMap? customDependency)
        {
            if (!string.Equals(TagName, tagName, StringComparison.OrdinalIgnoreCase)
                || !ReferenceEquals(InheritedValues, inheritedValues)
                || !ReferenceEquals(InheritedDeclarations, inheritedDeclarations)
                || !ReferenceEquals(CustomDependency, customDependency))
            {
                return false;
            }

            var ordinaryWinnerCount = 0;
            foreach (var pair in winners)
            {
                if (!pair.Key.StartsWith("--", StringComparison.Ordinal))
                {
                    ordinaryWinnerCount++;
                }
            }
            if (ordinaryWinnerCount != OrdinaryWinners.Length)
            {
                return false;
            }

            foreach (var pair in OrdinaryWinners)
            {
                if (!winners.TryGetValue(pair.Key, out var winner)
                    || !winner.Equals(pair.Value))
                {
                    return false;
                }
            }
            return true;
        }
    }

    private sealed record StylesheetInput(
        AvaloniaDomElement Owner,
        string Css,
        string? BaseAddress,
        string Source);

    private sealed record CustomPropertyRebasePlan(
        AvaloniaDomElement Element,
        CssCustomPropertyMap Computed,
        CssCustomPropertyMap Declared);

    private sealed record ParsedRule(
        CssComplexSelector Selector,
        IReadOnlyList<CssCascadeDeclaration> Declarations,
        IReadOnlyList<string> MediaQueries);

    private sealed record CachedStyleSheet(
        string Css,
        IReadOnlyList<ParsedRule> Rules,
        IReadOnlyList<WebScene.Css.CssCompiledFontFace> FontFaces);

}

internal enum CssCombinator
{
    None,
    Descendant,
    Child,
    AdjacentSibling,
    GeneralSibling
}

internal enum CssDynamicDependencyScope
{
    None,
    Descendants,
    Siblings
}

internal sealed class CssComplexSelector
{
    private static readonly bool s_disablePortableMatcher =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_PORTABLE_SELECTOR_MATCHER"),
            "1",
            StringComparison.Ordinal);
    private readonly CssSelectorSyntax _portableSyntax;
    private readonly CssSelectorDependencyProfile _portableDependencies;

    public CssComplexSelector(
        IReadOnlyList<CssSelectorPart> parts,
        int specificity,
        CssSelectorSyntax portableSyntax)
    {
        Parts = parts;
        Specificity = specificity;
        _portableSyntax = portableSyntax;
        _portableDependencies = CssSelectorDependencyAnalyzer.Analyze(portableSyntax);
    }

    public IReadOnlyList<CssSelectorPart> Parts { get; }

    public int Specificity { get; }

    internal CssSelectorDependencyProfile PortableDependencies => _portableDependencies;

    public bool DependsOnDynamicState
        => (_portableDependencies.All & CssSelectorDependency.DynamicState) != 0;

    public bool DependsOnEmpty
        => (_portableDependencies.All & CssSelectorDependency.Empty) != 0;

    public bool DependsOnAppendAtEnd
        => (_portableDependencies.All & CssSelectorDependency.AppendAtEnd) != 0;

    public bool RightmostDependsOnDynamicState
        => (_portableDependencies.Rightmost & CssSelectorDependency.DynamicState) != 0;

    public CssDynamicDependencyScope GetDynamicDependencyScope(
        AvaloniaDomElement element,
        AvaloniaDomDocument document)
    {
        var subject = CssSelectorSubject.ForElement(element);
        var scope = CssDynamicDependencyScope.None;
        for (var index = 0; index < Parts.Count - 1; index++)
        {
            var simple = Parts[index].Simple;
            if (!simple.DependsOnDynamicState
                || !simple.Matches(
                    subject,
                    document,
                    ignorePseudoElements: true,
                    ignoreDynamicPseudos: true))
            {
                continue;
            }

            var relation = Parts[index + 1].CombinatorToPrevious;
            if (relation is CssCombinator.AdjacentSibling or CssCombinator.GeneralSibling)
            {
                return CssDynamicDependencyScope.Siblings;
            }

            scope = CssDynamicDependencyScope.Descendants;
        }

        return scope;
    }

    public bool RightmostDependsOnEmpty
        => (_portableDependencies.Rightmost & CssSelectorDependency.Empty) != 0;

    public bool RightmostDependsOnAppendAtEnd
        => (_portableDependencies.Rightmost & CssSelectorDependency.AppendAtEnd) != 0;

    public bool HasAncestorChildListDependency
        => (_portableDependencies.Ancestors
            & (CssSelectorDependency.PositionFromStart | CssSelectorDependency.AppendAtEnd)) != 0;

    public bool HasSiblingCombinator
        => _portableDependencies.HasSiblingCombinator;

    public bool RightmostDependsOnChildPosition(bool fromStart)
        => (_portableDependencies.Rightmost
            & (fromStart ? CssSelectorDependency.PositionFromStart : CssSelectorDependency.AppendAtEnd)) != 0;

    public bool AncestorChildPositionDependencyCouldMatch(
        AvaloniaDomElement element,
        AvaloniaDomDocument document,
        bool fromStart)
    {
        var subject = CssSelectorSubject.ForElement(element);
        for (var index = 0; index < Parts.Count - 1; index++)
        {
            var simple = Parts[index].Simple;
            var depends = fromStart ? simple.DependsOnPositionFromStart : simple.DependsOnAppendAtEnd;
            if (depends
                && simple.Matches(subject, document, ignorePseudoElements: true, ignoreChildListPseudos: true))
            {
                return true;
            }
        }
        return false;
    }

    public bool RemovedSiblingCouldAffectSubtree(
        AvaloniaDomElement removed,
        AvaloniaDomElement following,
        AvaloniaDomDocument document,
        bool allowAdjacent)
    {
        var removedSubject = CssSelectorSubject.ForElement(removed);
        var followingSubject = CssSelectorSubject.ForElement(following);
        for (var index = 1; index < Parts.Count; index++)
        {
            var combinator = Parts[index].CombinatorToPrevious;
            if (combinator == CssCombinator.AdjacentSibling && !allowAdjacent) continue;
            if (combinator is not (CssCombinator.AdjacentSibling or CssCombinator.GeneralSibling)) continue;
            if (Parts[index - 1].Simple.Matches(
                    removedSubject,
                    document,
                    ignorePseudoElements: true,
                    ignoreChildListPseudos: true)
                && Parts[index].Simple.Matches(
                    followingSubject,
                    document,
                    ignorePseudoElements: true,
                    ignoreChildListPseudos: true))
            {
                return true;
            }
        }
        return false;
    }

    public bool AncestorAppendAtEndDependencyCouldMatch(
        AvaloniaDomElement element,
        AvaloniaDomDocument document)
    {
        var subject = CssSelectorSubject.ForElement(element);
        for (var index = 0; index < Parts.Count - 1; index++)
        {
            var simple = Parts[index].Simple;
            if (simple.DependsOnAppendAtEnd
                && simple.Matches(subject, document, ignorePseudoElements: true, ignoreChildListPseudos: true))
            {
                return true;
            }
        }

        return false;
    }

    public bool DependsOnAttribute(string name)
        => _portableDependencies.DependsOnAttribute(name);

    public void CollectAllClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        foreach (var part in Parts)
        {
            part.Simple.CollectAllClassDependencies(classes, classAttributeSelectors);
        }
    }

    public void CollectAncestorClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        for (var index = 0; index < Parts.Count - 1; index++)
        {
            Parts[index].Simple.CollectAllClassDependencies(classes, classAttributeSelectors);
        }

        if (Parts.Count > 0)
        {
            Parts[^1].Simple.CollectNestedAncestorClassDependencies(classes, classAttributeSelectors);
        }
    }

    public bool Matches(AvaloniaDomElement element, AvaloniaDomDocument document)
        => s_disablePortableMatcher
            ? PseudoElementName is null
              && MatchPart(CssSelectorSubject.ForElement(element), Parts.Count - 1, document, ignorePseudoElements: false)
            : CssSelectorMatcher.Matches(_portableSyntax, element);

    public bool Matches(
        AvaloniaDomElement element,
        AvaloniaDomDocument document,
        AvaloniaDomElement scopeElement)
        => s_disablePortableMatcher
            ? Matches(element, document)
            : CssSelectorMatcher.Matches(
                _portableSyntax,
                element,
                new CssSelectorMatchOptions(ScopeNode: scopeElement));

    public bool CouldMatchIgnoringChildList(AvaloniaDomElement element, AvaloniaDomDocument document)
        => s_disablePortableMatcher
            ? PseudoElementName is null
              && MatchPart(
                  CssSelectorSubject.ForElement(element),
                  Parts.Count - 1,
                  document,
                  ignorePseudoElements: false,
                  ignoreChildListPseudos: true)
            : CssSelectorMatcher.Matches(
                _portableSyntax,
                element,
                new CssSelectorMatchOptions(IgnoreChildListPseudos: true));

    public string? PseudoElementName
        => Parts.Count == 0
            ? null
            : Parts[^1].Simple.Pseudos.FirstOrDefault(pseudo => pseudo.IsElement)?.Name;

    public bool MatchesPseudoElement(AvaloniaDomElement element, AvaloniaDomDocument document, string name)
        => s_disablePortableMatcher
            ? string.Equals(PseudoElementName, name, StringComparison.OrdinalIgnoreCase)
              && MatchPart(CssSelectorSubject.ForElement(element), Parts.Count - 1, document, ignorePseudoElements: true)
            : CssSelectorMatcher.Matches(
                _portableSyntax,
                element,
                new CssSelectorMatchOptions(PseudoElementName: name));

    public bool CouldMatchPseudoElementIgnoringChildList(
        AvaloniaDomElement element,
        AvaloniaDomDocument document,
        string name)
        => s_disablePortableMatcher
            ? string.Equals(PseudoElementName, name, StringComparison.OrdinalIgnoreCase)
              && MatchPart(
                  CssSelectorSubject.ForElement(element),
                  Parts.Count - 1,
                  document,
                  ignorePseudoElements: true,
                  ignoreChildListPseudos: true)
            : CssSelectorMatcher.Matches(
                _portableSyntax,
                element,
                new CssSelectorMatchOptions(
                    IgnoreChildListPseudos: true,
                    PseudoElementName: name));

    public bool MatchesDocumentElement(AvaloniaDomDocument document)
        => s_disablePortableMatcher
            ? PseudoElementName is null
              && MatchPart(CssSelectorSubject.ForDocumentElement(), Parts.Count - 1, document, ignorePseudoElements: false)
            : CssSelectorMatcher.Matches(_portableSyntax, document.documentElement);

    private bool MatchPart(
        CssSelectorSubject subject,
        int index,
        AvaloniaDomDocument document,
        bool ignorePseudoElements,
        bool ignoreChildListPseudos = false)
    {
        if (index < 0 || !Parts[index].Simple.Matches(
                subject,
                document,
                ignorePseudoElements,
                ignoreChildListPseudos))
        {
            return false;
        }

        if (index == 0)
        {
            return true;
        }

        return Parts[index].CombinatorToPrevious switch
        {
            CssCombinator.Child => subject.Parent(document) is { } parent && MatchPart(parent, index - 1, document, ignorePseudoElements, ignoreChildListPseudos),
            CssCombinator.AdjacentSibling => subject.PreviousSibling() is { } sibling && MatchPart(sibling, index - 1, document, ignorePseudoElements, ignoreChildListPseudos),
            CssCombinator.GeneralSibling => MatchPreviousSibling(subject.PreviousSibling(), index - 1, document, ignorePseudoElements, ignoreChildListPseudos),
            _ => MatchAncestor(subject.Parent(document), index - 1, document, ignorePseudoElements, ignoreChildListPseudos)
        };
    }

    private bool MatchAncestor(CssSelectorSubject? subject, int index, AvaloniaDomDocument document, bool ignorePseudoElements, bool ignoreChildListPseudos = false)
    {
        while (subject is { } current)
        {
            if (MatchPart(current, index, document, ignorePseudoElements, ignoreChildListPseudos)) return true;
            subject = current.Parent(document);
        }
        return false;
    }

    private bool MatchPreviousSibling(CssSelectorSubject? subject, int index, AvaloniaDomDocument document, bool ignorePseudoElements, bool ignoreChildListPseudos = false)
    {
        while (subject is { } current)
        {
            if (MatchPart(current, index, document, ignorePseudoElements, ignoreChildListPseudos)) return true;
            subject = current.PreviousSibling();
        }
        return false;
    }
}

internal sealed record CssSelectorPart(CssSimpleSelector Simple, CssCombinator CombinatorToPrevious);

internal sealed class CssSimpleSelector
{
    public string? Tag { get; init; }
    public string? Id { get; init; }
    public List<string> Classes { get; } = new();
    public List<CssAttributeSelector> Attributes { get; } = new();
    public List<CssPseudoSelector> Pseudos { get; } = new();

    public bool DependsOnDynamicState
        => Pseudos.Any(pseudo => pseudo.DependsOnDynamicState);

    public bool DependsOnEmpty
        => Pseudos.Any(pseudo => pseudo.DependsOnEmpty);

    public bool DependsOnAppendAtEnd
        => Pseudos.Any(pseudo => pseudo.DependsOnAppendAtEnd);

    public bool DependsOnPositionFromStart
        => Pseudos.Any(pseudo => pseudo.DependsOnPositionFromStart);

    public bool DependsOnChildPosition
        => DependsOnPositionFromStart || DependsOnAppendAtEnd;

    public bool DependsOnAttribute(string name)
        => Attributes.Any(attribute => string.Equals(attribute.Name, name, StringComparison.OrdinalIgnoreCase))
           || Pseudos.Any(pseudo => pseudo.DependsOnAttribute(name));

    public void CollectAllClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        classes.UnionWith(Classes);
        foreach (var attribute in Attributes)
        {
            if (string.Equals(attribute.Name, "class", StringComparison.OrdinalIgnoreCase))
            {
                classAttributeSelectors.Add(attribute);
            }
        }
        foreach (var pseudo in Pseudos)
        {
            pseudo.CollectAllClassDependencies(classes, classAttributeSelectors);
        }
    }

    public void CollectNestedAncestorClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        foreach (var pseudo in Pseudos)
        {
            pseudo.CollectAncestorClassDependencies(classes, classAttributeSelectors);
        }
    }

    public bool Matches(
        CssSelectorSubject subject,
        AvaloniaDomDocument document,
        bool ignorePseudoElements = false,
        bool ignoreChildListPseudos = false,
        bool ignoreDynamicPseudos = false)
    {
        if (Tag is { Length: > 0 } && Tag != "*" && !string.Equals(Tag, subject.TagName, StringComparison.OrdinalIgnoreCase))
            return false;
        if (Id is { Length: > 0 } && !string.Equals(Id, subject.Id, StringComparison.Ordinal))
            return false;
        for (var index = 0; index < Classes.Count; index++)
        {
            if (!subject.HasClass(Classes[index], document))
            {
                return false;
            }
        }

        for (var index = 0; index < Attributes.Count; index++)
        {
            if (!Attributes[index].Matches(subject, document))
            {
                return false;
            }
        }

        for (var index = 0; index < Pseudos.Count; index++)
        {
            var pseudo = Pseudos[index];
            if (ignoreChildListPseudos && (pseudo.DependsOnEmpty || pseudo.DependsOnAppendAtEnd))
            {
                continue;
            }
            if (ignoreDynamicPseudos && pseudo.DependsOnDynamicState)
            {
                continue;
            }
            if (pseudo.IsElement ? !ignorePseudoElements : !pseudo.Matches(subject, document))
            {
                return false;
            }
        }

        return true;
    }
}

internal sealed record CssAttributeSelector(string Name, string? Operator, string? Value, bool CaseInsensitive)
{
    public bool Matches(CssSelectorSubject subject, AvaloniaDomDocument document)
        => MatchesValue(subject.GetAttribute(Name, document));

    public bool MatchesValue(string? actual)
    {
        if (actual is null) return false;
        if (Operator is null) return true;
        var comparison = CaseInsensitive ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        var expected = Value ?? string.Empty;
        return Operator switch
        {
            "=" => string.Equals(actual, expected, comparison),
            "~=" => actual.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Any(token => string.Equals(token, expected, comparison)),
            "|=" => string.Equals(actual, expected, comparison) || actual.StartsWith(expected + "-", comparison),
            "^=" => actual.StartsWith(expected, comparison),
            "$=" => actual.EndsWith(expected, comparison),
            "*=" => actual.IndexOf(expected, comparison) >= 0,
            _ => false
        };
    }
}

internal sealed record CssPseudoSelector(string Name, string? Argument, bool IsElement = false)
{
    private static readonly bool s_disableArgumentSelectorCache =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_PSEUDO_ARGUMENT_SELECTOR_CACHE"),
            "1",
            StringComparison.Ordinal);
    private CssComplexSelector[]? _argumentSelectors;

    public bool DependsOnDynamicState
        => Name is "hover" or "active" or "focus" or "focus-visible"
            or "disabled" or "enabled" or "checked"
           || (Name is "is" or "not" or "where"
               && GetArgumentSelectors().Any(selector => selector.DependsOnDynamicState));

    public bool DependsOnEmpty
        => Name == "empty"
           || (Name is "is" or "not" or "where"
               && GetArgumentSelectors().Any(selector => selector.DependsOnEmpty));

    public bool DependsOnAppendAtEnd
        => Name is "last-child" or "only-child" or "last-of-type" or "only-of-type"
            or "nth-last-child" or "nth-last-of-type"
           || (Name is "is" or "not" or "where"
               && GetArgumentSelectors().Any(selector => selector.DependsOnAppendAtEnd));

    public bool DependsOnPositionFromStart
        => Name is "first-child" or "only-child" or "first-of-type" or "only-of-type"
            or "nth-child" or "nth-of-type"
           || (Name is "is" or "not" or "where"
               && GetArgumentSelectors().Any(selector =>
                   selector.Parts.Any(part => part.Simple.DependsOnPositionFromStart)));

    public bool DependsOnAttribute(string name)
    {
        if (Name is not ("is" or "not" or "where") || string.IsNullOrWhiteSpace(Argument))
        {
            return false;
        }

        foreach (var selector in GetArgumentSelectors())
        {
            if (selector.DependsOnAttribute(name))
            {
                return true;
            }
        }

        return false;
    }

    public void CollectAllClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        if (Name is not ("is" or "not" or "where"))
        {
            return;
        }
        foreach (var selector in GetArgumentSelectors())
        {
            selector.CollectAllClassDependencies(classes, classAttributeSelectors);
        }
    }

    public void CollectAncestorClassDependencies(
        ISet<string> classes,
        ICollection<CssAttributeSelector> classAttributeSelectors)
    {
        if (Name is not ("is" or "not" or "where"))
        {
            return;
        }
        foreach (var selector in GetArgumentSelectors())
        {
            selector.CollectAncestorClassDependencies(classes, classAttributeSelectors);
        }
    }

    public bool Matches(CssSelectorSubject subject, AvaloniaDomDocument document)
    {
        switch (Name)
        {
            case "root": return subject.IsDocumentElement;
            case "empty": return subject.Element is { childElementCount: 0 } element && string.IsNullOrEmpty(element.textContent);
            case "first-child": return subject.Element?.previousElementSibling is null;
            case "last-child": return subject.Element?.nextElementSibling is null;
            case "only-child": return subject.Element?.previousElementSibling is null && subject.Element?.nextElementSibling is null;
            case "first-of-type": return !EnumeratePrevious(subject).Any(s => string.Equals(s.TagName, subject.TagName, StringComparison.OrdinalIgnoreCase));
            case "last-of-type": return !EnumerateNext(subject).Any(s => string.Equals(s.TagName, subject.TagName, StringComparison.OrdinalIgnoreCase));
            case "only-of-type": return !EnumeratePrevious(subject).Concat(EnumerateNext(subject)).Any(s => string.Equals(s.TagName, subject.TagName, StringComparison.OrdinalIgnoreCase));
            case "nth-child": return MatchesNth(GetChildIndex(subject, ofType: false), Argument);
            case "nth-last-child": return MatchesNth(GetReverseChildIndex(subject, ofType: false), Argument);
            case "nth-of-type": return MatchesNth(GetChildIndex(subject, ofType: true), Argument);
            case "nth-last-of-type": return MatchesNth(GetReverseChildIndex(subject, ofType: true), Argument);
            case "not": return !MatchesSelectorArgument(subject, document);
            case "is":
            case "where": return MatchesSelectorArgument(subject, document);
            case "hover": return subject.Element is { } hoveredElement && document.IsPointerHovered(hoveredElement);
            case "active": return subject.Element?.Control.Classes.Contains(":pressed") == true;
            case "focus": return subject.Element?.Control.IsFocused == true;
            case "focus-visible": return subject.Element?.Control.Classes.Contains(":focus-visible") == true;
            case "disabled": return subject.Element?.IsDisabledFormControl == true;
            case "enabled": return subject.Element is { SupportsDisabledState: true, IsDisabledFormControl: false };
            case "checked": return subject.Element?.MatchesCheckedPseudoClass == true;
            case "link": return string.Equals(subject.TagName, "a", StringComparison.OrdinalIgnoreCase) && subject.GetAttribute("href", document) is not null;
            case "lang": return true;
            default:
                // Unknown pseudo-classes represent a state we cannot prove.
                return false;
        }
    }

    private bool MatchesSelectorArgument(CssSelectorSubject subject, AvaloniaDomDocument document)
    {
        if (string.IsNullOrWhiteSpace(Argument) || subject.Element is null) return false;
        foreach (var selector in GetArgumentSelectors())
        {
            if (selector.Matches(subject.Element, document))
                return true;
        }
        return false;
    }

    private CssComplexSelector[] GetArgumentSelectors()
    {
        if (s_disableArgumentSelectorCache)
        {
            return ParseArgumentSelectors();
        }

        return _argumentSelectors ??= ParseArgumentSelectors();
    }

    private CssComplexSelector[] ParseArgumentSelectors()
    {
        if (string.IsNullOrWhiteSpace(Argument))
        {
            return Array.Empty<CssComplexSelector>();
        }

        List<CssComplexSelector>? selectors = null;
        foreach (var selectorText in CssSelectorSyntaxParser.SplitSelectorList(Argument))
        {
            if (CssSelectorParser.TryParse(selectorText, out var selector))
            {
                (selectors ??= new List<CssComplexSelector>()).Add(selector);
            }
        }

        return selectors?.ToArray() ?? Array.Empty<CssComplexSelector>();
    }

    private static IEnumerable<CssSelectorSubject> EnumeratePrevious(CssSelectorSubject subject)
    {
        var current = subject.PreviousSibling();
        while (current is { } sibling)
        {
            yield return sibling;
            current = sibling.PreviousSibling();
        }
    }

    private static IEnumerable<CssSelectorSubject> EnumerateNext(CssSelectorSubject subject)
    {
        var current = subject.NextSibling();
        while (current is { } sibling)
        {
            yield return sibling;
            current = sibling.NextSibling();
        }
    }

    private static int GetChildIndex(CssSelectorSubject subject, bool ofType)
        => 1 + EnumeratePrevious(subject).Count(s => !ofType || string.Equals(s.TagName, subject.TagName, StringComparison.OrdinalIgnoreCase));

    private static int GetReverseChildIndex(CssSelectorSubject subject, bool ofType)
        => 1 + EnumerateNext(subject).Count(s => !ofType || string.Equals(s.TagName, subject.TagName, StringComparison.OrdinalIgnoreCase));

    private static bool MatchesNth(int index, string? expression)
    {
        var text = expression?.Replace(" ", string.Empty, StringComparison.Ordinal).ToLowerInvariant();
        if (string.IsNullOrEmpty(text)) return false;
        if (text == "odd") return index % 2 == 1;
        if (text == "even") return index % 2 == 0;
        if (int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var exact)) return index == exact;
        var n = text.IndexOf('n');
        if (n < 0) return false;
        var aText = text[..n];
        var bText = text[(n + 1)..];
        var a = aText switch { "" or "+" => 1, "-" => -1, _ => int.TryParse(aText, out var coefficient) ? coefficient : 0 };
        var b = string.IsNullOrEmpty(bText) ? 0 : int.TryParse(bText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var offset) ? offset : 0;
        if (a == 0) return index == b;
        var delta = index - b;
        return delta / a >= 0 && delta % a == 0;
    }
}

internal readonly struct CssSelectorSubject
{
    private CssSelectorSubject(AvaloniaDomElement? element, bool documentElement)
    {
        Element = element;
        IsDocumentElement = documentElement;
    }

    public AvaloniaDomElement? Element { get; }
    public bool IsDocumentElement { get; }
    public string TagName => IsDocumentElement ? "html" : Element?.tagName ?? string.Empty;
    public string Id => IsDocumentElement ? string.Empty : Element?.id ?? string.Empty;

    public static CssSelectorSubject ForElement(AvaloniaDomElement element) => new(element, false);
    public static CssSelectorSubject ForDocumentElement() => new(null, true);

    public bool HasClass(string cssClass, AvaloniaDomDocument document)
        => IsDocumentElement
            ? document.documentElement.classList.contains(cssClass)
            : Element?.Control.Classes.Contains(cssClass) == true;

    public string? GetAttribute(string name, AvaloniaDomDocument document)
    {
        if (IsDocumentElement)
        {
            if (string.Equals(name, "class", StringComparison.OrdinalIgnoreCase)) return document.documentElement.classList.value;
            return document.documentElement.getAttribute(name);
        }
        return Element?.getAttribute(name);
    }

    public CssSelectorSubject? Parent(AvaloniaDomDocument document)
    {
        if (IsDocumentElement) return null;
        if (Element is null) return null;
        if (string.Equals(Element.tagName, "BODY", StringComparison.OrdinalIgnoreCase)) return ForDocumentElement();
        var parent = Element.parentElement;
        return parent is null ? null : ForElement(parent);
    }

    public CssSelectorSubject? PreviousSibling()
        => Element?.previousElementSibling is { } sibling ? ForElement(sibling) : null;

    public CssSelectorSubject? NextSibling()
        => Element?.nextElementSibling is { } sibling ? ForElement(sibling) : null;
}

internal static class CssSelectorParser
{
    public static IEnumerable<string> SplitSelectorList(string selectorText)
        => CssSelectorSyntaxParser.SplitSelectorList(selectorText);

    public static bool TryParse(string text, out CssComplexSelector selector)
    {
        if (!CssSelectorSyntaxParser.TryParse(text, out var syntax))
        {
            selector = null!;
            return false;
        }

        selector = Create(syntax);
        return true;
    }

    public static CssComplexSelector Create(CssSelectorSyntax syntax)
    {
        ArgumentNullException.ThrowIfNull(syntax);
        var parts = new List<CssSelectorPart>(syntax.Parts.Count);
        foreach (var part in syntax.Parts)
        {
            var simple = new CssSimpleSelector { Tag = part.Simple.Tag, Id = part.Simple.Id };
            simple.Classes.AddRange(part.Simple.Classes);
            simple.Attributes.AddRange(part.Simple.Attributes.Select(static attribute =>
                new CssAttributeSelector(attribute.Name, attribute.Operator, attribute.Value, attribute.CaseInsensitive)));
            simple.Pseudos.AddRange(part.Simple.Pseudos.Select(static pseudo =>
                new CssPseudoSelector(pseudo.Name, pseudo.Argument, pseudo.IsElement)));
            parts.Add(new CssSelectorPart(simple, part.CombinatorToPrevious switch
            {
                CssSelectorCombinator.Descendant => CssCombinator.Descendant,
                CssSelectorCombinator.Child => CssCombinator.Child,
                CssSelectorCombinator.AdjacentSibling => CssCombinator.AdjacentSibling,
                CssSelectorCombinator.GeneralSibling => CssCombinator.GeneralSibling,
                _ => CssCombinator.None
            }));
        }

        return new CssComplexSelector(parts, syntax.Specificity, syntax);
    }

    private static List<SelectorToken> Tokenize(string text)
    {
        var result = new List<SelectorToken>();
        var current = new StringBuilder();
        var square = 0;
        var round = 0;
        char quote = '\0';
        var pendingWhitespace = false;

        void Flush()
        {
            if (current.Length == 0) return;
            result.Add(SelectorToken.Simple(current.ToString()));
            current.Clear();
        }

        for (var i = 0; i < text.Length; i++)
        {
            var ch = text[i];
            if (quote != '\0')
            {
                current.Append(ch);
                if (ch == quote && (i == 0 || text[i - 1] != '\\')) quote = '\0';
                continue;
            }
            if (ch is '\'' or '"')
            {
                quote = ch;
                current.Append(ch);
            }
            else if (ch == '[') { square++; current.Append(ch); }
            else if (ch == ']') { square--; current.Append(ch); }
            else if (ch == '(') { round++; current.Append(ch); }
            else if (ch == ')') { round--; current.Append(ch); }
            else if (square == 0 && round == 0 && char.IsWhiteSpace(ch))
            {
                Flush();
                pendingWhitespace = result.Count > 0 && !result[^1].IsCombinator;
            }
            else if (square == 0 && round == 0 && ch is '>' or '+' or '~')
            {
                Flush();
                if (result.Count > 0 && result[^1].IsCombinator) result.RemoveAt(result.Count - 1);
                result.Add(SelectorToken.ForCombinator(ch switch
                {
                    '>' => CssCombinator.Child,
                    '+' => CssCombinator.AdjacentSibling,
                    _ => CssCombinator.GeneralSibling
                }));
                pendingWhitespace = false;
            }
            else
            {
                if (pendingWhitespace)
                {
                    result.Add(SelectorToken.ForCombinator(CssCombinator.Descendant));
                    pendingWhitespace = false;
                }
                current.Append(ch);
            }
        }
        Flush();
        return result;
    }

    private static bool TryParseSimple(string text, out CssSimpleSelector simple, out int specificity)
    {
        simple = new CssSimpleSelector();
        specificity = 0;
        var i = 0;
        if (i < text.Length && (text[i] == '*' || IsIdentStart(text[i])))
        {
            var tag = text[i] == '*' ? "*" : ReadIdentifier(text, ref i);
            if (tag == "*") i++;
            simple = new CssSimpleSelector { Tag = tag };
            if (tag != "*") specificity += 1;
        }

        while (i < text.Length)
        {
            switch (text[i])
            {
                case '#':
                    i++;
                    simple = Clone(simple, id: ReadIdentifier(text, ref i));
                    specificity += 100;
                    break;
                case '.':
                    i++;
                    simple.Classes.Add(ReadIdentifier(text, ref i));
                    specificity += 10;
                    break;
                case '[':
                {
                    var content = ReadBalanced(text, ref i, '[', ']');
                    if (!TryParseAttribute(content, out var attribute)) return false;
                    simple.Attributes.Add(attribute);
                    specificity += 10;
                    break;
                }
                case ':':
                {
                    i++;
                    var isElement = i < text.Length && text[i] == ':';
                    if (isElement) i++;
                    var name = ReadIdentifier(text, ref i).ToLowerInvariant();
                    isElement |= name is "before" or "after";
                    string? argument = null;
                    if (i < text.Length && text[i] == '(') argument = ReadBalanced(text, ref i, '(', ')');
                    if (isElement && simple.Pseudos.Any(pseudo => pseudo.IsElement)) return false;
                    simple.Pseudos.Add(new CssPseudoSelector(name, argument, isElement));
                    if (isElement) specificity += 1;
                    else if (name != "where") specificity += 10;
                    break;
                }
                default:
                    // Namespace separators and escaped identifiers are not needed by
                    // the current HTML integration; reject instead of mis-matching.
                    return false;
            }
        }
        return true;
    }

    private static CssSimpleSelector Clone(CssSimpleSelector source, string? id = null)
    {
        var clone = new CssSimpleSelector { Tag = source.Tag, Id = id ?? source.Id };
        clone.Classes.AddRange(source.Classes);
        clone.Attributes.AddRange(source.Attributes);
        clone.Pseudos.AddRange(source.Pseudos);
        return clone;
    }

    private static bool TryParseAttribute(string content, out CssAttributeSelector attribute)
    {
        attribute = null!;
        content = content.Trim();
        var caseInsensitive = content.EndsWith(" i", StringComparison.OrdinalIgnoreCase);
        if (caseInsensitive) content = content[..^2].TrimEnd();
        string? op = null;
        var opIndex = -1;
        foreach (var candidate in new[] { "~=", "|=", "^=", "$=", "*=", "=" })
        {
            opIndex = content.IndexOf(candidate, StringComparison.Ordinal);
            if (opIndex >= 0) { op = candidate; break; }
        }
        var name = (opIndex >= 0 ? content[..opIndex] : content).Trim();
        if (name.Length == 0) return false;
        var value = opIndex >= 0 ? content[(opIndex + op!.Length)..].Trim().Trim('\'', '"') : null;
        attribute = new CssAttributeSelector(name, op, value, caseInsensitive);
        return true;
    }

    private static string ReadBalanced(string text, ref int index, char open, char close)
    {
        index++;
        var start = index;
        var depth = 1;
        char quote = '\0';
        while (index < text.Length)
        {
            var ch = text[index];
            if (quote != '\0')
            {
                if (ch == quote && text[index - 1] != '\\') quote = '\0';
            }
            else if (ch is '\'' or '"') quote = ch;
            else if (ch == open) depth++;
            else if (ch == close && --depth == 0)
            {
                var result = text[start..index];
                index++;
                return result;
            }
            index++;
        }
        return text[start..];
    }

    private static string ReadIdentifier(string text, ref int index)
    {
        var builder = new StringBuilder();
        while (index < text.Length)
        {
            var ch = text[index];
            if (ch == '\\' && index + 1 < text.Length)
            {
                builder.Append(text[index + 1]);
                index += 2;
            }
            else if (char.IsLetterOrDigit(ch) || ch is '-' or '_')
            {
                builder.Append(ch);
                index++;
            }
            else break;
        }
        return builder.ToString();
    }

    private static bool IsIdentStart(char ch) => char.IsLetter(ch) || ch is '-' or '_';

    private readonly record struct SelectorToken(string Text, bool IsCombinator, CssCombinator Combinator)
    {
        public static SelectorToken Simple(string text) => new(text, false, CssCombinator.None);
        public static SelectorToken ForCombinator(CssCombinator combinator) => new(string.Empty, true, combinator);
    }
}

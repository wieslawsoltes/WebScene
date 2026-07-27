using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;

namespace WebScene.Css;

/// <summary>
/// Immutable inherited custom-property state. Most elements add no custom
/// properties and share their parent's instance without copying its entries.
/// </summary>
internal sealed class CssCustomPropertyMap : IReadOnlyDictionary<string, string>
{
    private const int MaximumOverlayDepth = 16;
    private static readonly StringComparer s_comparer = StringComparer.Ordinal;
    private static readonly bool s_disableContentHashFastPath =
        string.Equals(
            Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CUSTOM_PROPERTY_HASH"),
            "1",
            StringComparison.Ordinal);
    private readonly CssCustomPropertyMap? _parent;
    private readonly Dictionary<string, string> _overrides;
    private readonly int _count;
    private readonly int _contentHash;

    private CssCustomPropertyMap(
        CssCustomPropertyMap? parent,
        Dictionary<string, string> overrides,
        int count,
        int depth,
        int contentHash)
    {
        _parent = parent;
        _overrides = overrides;
        _count = count;
        _contentHash = contentHash;
        Depth = depth;
    }

    internal static CssCustomPropertyMap Empty { get; } =
        new(null, new Dictionary<string, string>(s_comparer), 0, 0, 0);

    internal int Depth { get; }

    public int Count => _count;

    public IEnumerable<string> Keys => this.Select(pair => pair.Key);

    public IEnumerable<string> Values => this.Select(pair => pair.Value);

    public string this[string key] => TryGetValue(key, out var value)
        ? value
        : throw new KeyNotFoundException();

    internal static CssCustomPropertyMap Create(Dictionary<string, string>? values)
    {
        if (values is null || values.Count == 0)
        {
            return Empty;
        }

        // Callers may supply a dictionary with an ordinary CSS-property
        // comparer. Custom-property names are case-sensitive, so retain an
        // immutable ordinal snapshot rather than inheriting the caller's
        // comparer or subsequent mutations.
        values = new Dictionary<string, string>(values, s_comparer);
        var contentHash = 0;
        foreach (var pair in values)
        {
            contentHash ^= GetPairHash(pair.Key, pair.Value);
        }
        return new CssCustomPropertyMap(null, values, values.Count, 0, contentHash);
    }

    internal CssCustomPropertyMap WithOverrides(Dictionary<string, string>? overrides)
    {
        if (overrides is null || overrides.Count == 0)
        {
            return this;
        }

        var effective = new Dictionary<string, string>(overrides.Count, s_comparer);
        var added = 0;
        var contentHash = _contentHash;
        foreach (var pair in overrides)
        {
            if (TryGetValue(pair.Key, out var inheritedValue))
            {
                if (string.Equals(inheritedValue, pair.Value, StringComparison.Ordinal))
                {
                    continue;
                }

                contentHash ^= GetPairHash(pair.Key, inheritedValue);
            }
            else
            {
                added++;
            }

            effective[pair.Key] = pair.Value;
            contentHash ^= GetPairHash(pair.Key, pair.Value);
        }

        if (effective.Count == 0)
        {
            return this;
        }

        if (Depth >= MaximumOverlayDepth)
        {
            var flattened = new Dictionary<string, string>(Count + added, s_comparer);
            foreach (var pair in this)
            {
                flattened[pair.Key] = pair.Value;
            }
            foreach (var pair in effective)
            {
                flattened[pair.Key] = pair.Value;
            }
            return Create(flattened);
        }

        return new CssCustomPropertyMap(
            this,
            effective,
            Count + added,
            Depth + 1,
            contentHash);
    }

    internal CssCustomPropertyMap CloneFlat()
    {
        if (Count == 0)
        {
            return Empty;
        }

        var values = new Dictionary<string, string>(Count, s_comparer);
        foreach (var pair in this)
        {
            values[pair.Key] = pair.Value;
        }
        return Create(values);
    }

    internal bool TryRebaseOnto(
        CssCustomPropertyMap oldParent,
        CssCustomPropertyMap newParent,
        out CssCustomPropertyMap rebased)
    {
        if (ReferenceEquals(this, oldParent))
        {
            rebased = newParent;
            return true;
        }

        if (ReferenceEquals(_parent, oldParent))
        {
            rebased = newParent.WithOverrides(_overrides);
            return true;
        }

        rebased = this;
        return false;
    }

    public bool ContainsKey(string key) => TryGetValue(key, out _);

    public bool TryGetValue(string key, out string value)
    {
        if (_overrides.TryGetValue(key, out value!))
        {
            return true;
        }

        if (_parent is not null)
        {
            return _parent.TryGetValue(key, out value!);
        }

        value = null!;
        return false;
    }

    internal bool ContentEquals(CssCustomPropertyMap other)
    {
        if (ReferenceEquals(this, other))
        {
            return true;
        }

        return Count == other.Count
               && (s_disableContentHashFastPath || _contentHash == other._contentHash)
               && this.All(pair => other.TryGetValue(pair.Key, out var value)
                                   && string.Equals(pair.Value, value, StringComparison.Ordinal));
    }

    private static int GetPairHash(string key, string value)
        => HashCode.Combine(
            s_comparer.GetHashCode(key),
            StringComparer.Ordinal.GetHashCode(value));

    public IEnumerator<KeyValuePair<string, string>> GetEnumerator()
    {
        if (_parent is not null)
        {
            foreach (var pair in _parent)
            {
                yield return _overrides.TryGetValue(pair.Key, out var overridden)
                    ? new KeyValuePair<string, string>(pair.Key, overridden)
                    : pair;
            }
        }

        foreach (var pair in _overrides)
        {
            if (_parent?.ContainsKey(pair.Key) != true)
            {
                yield return pair;
            }
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

/// <summary>
/// Complete computed style view backed by per-element ordinary properties and
/// shared inherited custom properties.
/// </summary>
internal sealed class CssComputedValues : IReadOnlyDictionary<string, string>
{
    internal CssComputedValues(
        CssPropertyValueStore ordinaryValues,
        CssCustomPropertyMap customProperties)
    {
        OrdinaryValues = ordinaryValues;
        CustomProperties = customProperties;
    }

    internal static CssComputedValues Empty { get; } = new(
        new CssPropertyValueStore(),
        CssCustomPropertyMap.Empty);

    internal CssPropertyValueStore OrdinaryValues { get; }

    internal CssCustomPropertyMap CustomProperties { get; }

    public int Count => OrdinaryValues.Count + CustomProperties.Count;

    public IEnumerable<string> Keys => OrdinaryValues.Keys.Concat(CustomProperties.Keys);

    public IEnumerable<string> Values => OrdinaryValues.Values.Concat(CustomProperties.Values);

    public string this[string key] => TryGetValue(key, out var value)
        ? value
        : throw new KeyNotFoundException();

    public bool ContainsKey(string key) => TryGetValue(key, out _);

    public bool TryGetValue(string key, out string value)
        => key.StartsWith("--", StringComparison.Ordinal)
            ? CustomProperties.TryGetValue(key, out value!)
            : OrdinaryValues.TryGetValue(key, out value!);

    internal bool ContentEquals(CssComputedValues other)
    {
        if (ReferenceEquals(this, other))
        {
            return true;
        }

        return OrdinaryContentEquals(other)
               && CustomProperties.ContentEquals(other.CustomProperties);
    }

    internal bool OrdinaryContentEquals(CssComputedValues other)
    {
        if (ReferenceEquals(OrdinaryValues, other.OrdinaryValues))
        {
            return true;
        }

        return OrdinaryValues.ContentEquals(other.OrdinaryValues);
    }

    public IEnumerator<KeyValuePair<string, string>> GetEnumerator()
    {
        foreach (var pair in OrdinaryValues)
        {
            yield return pair;
        }
        foreach (var pair in CustomProperties)
        {
            yield return pair;
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}

/// <summary>
/// Declared ordinary properties plus the keys of shared custom-property state.
/// </summary>
internal sealed class CssDeclaredPropertySet : IReadOnlySet<string>
{
    internal CssDeclaredPropertySet(
        CssPropertyNameSet ordinaryProperties,
        CssCustomPropertyMap customProperties)
    {
        OrdinaryProperties = ordinaryProperties;
        CustomProperties = customProperties;
    }

    internal static CssDeclaredPropertySet Empty { get; } = new(
        new CssPropertyNameSet(),
        CssCustomPropertyMap.Empty);

    internal CssPropertyNameSet OrdinaryProperties { get; }

    internal CssCustomPropertyMap CustomProperties { get; }

    public int Count => OrdinaryProperties.Count + CustomProperties.Count;

    public bool Contains(string item)
        => item.StartsWith("--", StringComparison.Ordinal)
            ? CustomProperties.ContainsKey(item)
            : OrdinaryProperties.Contains(item);

    internal bool ContentEquals(CssDeclaredPropertySet other)
        => ReferenceEquals(this, other)
           || (OrdinaryContentEquals(other)
               && CustomProperties.ContentEquals(other.CustomProperties));

    internal bool OrdinaryContentEquals(CssDeclaredPropertySet other)
        => ReferenceEquals(OrdinaryProperties, other.OrdinaryProperties)
           || OrdinaryProperties.SetEquals(other.OrdinaryProperties);

    public bool SetEquals(IEnumerable<string> other)
    {
        if (other is CssDeclaredPropertySet set)
        {
            return ContentEquals(set);
        }

        var comparison = new HashSet<string>(other, CssPropertyNameComparer.Instance);
        return comparison.Count == Count && this.All(comparison.Contains);
    }

    public bool IsSubsetOf(IEnumerable<string> other) => AsHashSet().IsSubsetOf(other);

    public bool IsSupersetOf(IEnumerable<string> other) => AsHashSet().IsSupersetOf(other);

    public bool IsProperSubsetOf(IEnumerable<string> other) => AsHashSet().IsProperSubsetOf(other);

    public bool IsProperSupersetOf(IEnumerable<string> other) => AsHashSet().IsProperSupersetOf(other);

    public bool Overlaps(IEnumerable<string> other) => other.Any(Contains);

    public IEnumerator<string> GetEnumerator()
    {
        foreach (var property in OrdinaryProperties)
        {
            yield return property;
        }
        foreach (var property in CustomProperties.Keys)
        {
            yield return property;
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private HashSet<string> AsHashSet() => new(this, CssPropertyNameComparer.Instance);
}

using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssPropertyStorageTests
{
    [Fact]
    public void KnownPropertyCatalogHasStableCaseInsensitiveIds()
    {
        for (var index = 0; index < CssKnownProperties.Names.Length; index++)
        {
            var name = CssKnownProperties.Names[index];
            Assert.True(CssKnownProperties.TryGetId(name, out var directId));
            Assert.True(CssKnownProperties.TryGetId(name.ToUpperInvariant(), out var fallbackId));
            Assert.Equal(index, directId);
            Assert.Equal(index, fallbackId);
        }

        Assert.Contains("outline", CssKnownProperties.Names);
        Assert.Contains("outline-color", CssKnownProperties.Names);
        Assert.Contains("outline-offset", CssKnownProperties.Names);
        Assert.Contains("outline-style", CssKnownProperties.Names);
        Assert.Contains("outline-width", CssKnownProperties.Names);
    }

    [Fact]
    public void ValueStorePreservesUnknownPropertiesAndFreezesCommittedState()
    {
        var values = new CssPropertyValueStore();
        values["display"] = "flex";
        values["vendor-widget-mode"] = "compact";

        var clone = values.Clone(frozen: true);

        Assert.True(values.ContentEquals(clone));
        Assert.Equal(values.GetContentHashCode(), clone.GetContentHashCode());
        Assert.Equal("flex", clone["DISPLAY"]);
        Assert.Equal("compact", clone["VENDOR-WIDGET-MODE"]);
        Assert.Throws<InvalidOperationException>(() => clone["display"] = "block");
    }

    [Fact]
    public void DeclaredPropertyHashIsOrderIndependentForFallbackNames()
    {
        var first = new CssPropertyNameSet();
        first.Add("display");
        first.Add("vendor-first");
        first.Add("vendor-second");

        var second = new CssPropertyNameSet();
        second.Add("VENDOR-SECOND");
        second.Add("DISPLAY");
        second.Add("VENDOR-FIRST");

        Assert.True(first.SetEquals(second));
        Assert.Equal(first.GetContentHashCode(), second.GetContentHashCode());
    }

    [Fact]
    public void ValueStoreSupportsOverwriteRemoveClearCloneAndEnumerationContracts()
    {
        var values = new CssPropertyValueStore(fallbackCapacity: 2)
        {
            ["color"] = "red",
            ["vendor-mode"] = "one"
        };
        values["color"] = "blue";
        values["vendor-mode"] = "two";
        Assert.Equal(2, values.Count);
        Assert.True(values.ContainsKey("COLOR"));
        Assert.True(values.ContainsKey("VENDOR-MODE"));
        Assert.False(values.ContainsKey("missing"));
        Assert.False(values.TryGetValue("missing", out _));
        Assert.Equal(new[] { "color", "vendor-mode" }, values.Keys.ToArray());
        Assert.Equal(new[] { "blue", "two" }, values.Values.ToArray());
        Assert.Throws<KeyNotFoundException>(() => _ = values["missing"]);

        var enumerator = values.GetEnumerator();
        Assert.True(enumerator.MoveNext());
        Assert.Throws<NotSupportedException>(() => enumerator.Reset());
        enumerator.Dispose();
        Assert.Equal(2, ((System.Collections.IEnumerable)values).Cast<object>().Count());

        Assert.False(values.Remove("missing"));
        Assert.True(values.Remove("color"));
        Assert.True(values.Remove("vendor-mode"));
        Assert.Empty(values);
        values["width"] = "10px";
        values.Clear();
        Assert.Empty(values);
    }

    [Fact]
    public void ValueStoreContentComparisonDetectsKnownAndFallbackDifferences()
    {
        var baseline = new CssPropertyValueStore { ["color"] = "red", ["vendor"] = "one" };
        Assert.True(baseline.ContentEquals(baseline));
        Assert.False(baseline.ContentEquals(new CssPropertyValueStore { ["color"] = "red" }));
        Assert.False(baseline.ContentEquals(new CssPropertyValueStore { ["color"] = "blue", ["vendor"] = "one" }));
        Assert.False(baseline.ContentEquals(new CssPropertyValueStore { ["color"] = "red", ["vendor"] = "two" }));
        Assert.False(baseline.ContentEquals(new CssPropertyValueStore { ["color"] = "red", ["other"] = "one" }));

        var noFallback = new CssPropertyValueStore { ["color"] = "red" };
        Assert.True(noFallback.ContentEquals(noFallback.Clone()));
        Assert.Equal(noFallback.GetContentHashCode(), noFallback.Clone().GetContentHashCode());
        noFallback.Freeze();
        Assert.True(noFallback.IsFrozen);
        Assert.Throws<InvalidOperationException>(() => noFallback.Remove("color"));
        Assert.Throws<InvalidOperationException>(() => noFallback.Clear());
    }

    [Fact]
    public void PropertyNameSetImplementsMutationFreezeAndSetContracts()
    {
        var names = new CssPropertyNameSet(fallbackCapacity: 2);
        Assert.True(names.Add("color"));
        Assert.False(names.Add("COLOR"));
        Assert.True(names.Add("vendor-one"));
        Assert.False(names.Add("VENDOR-ONE"));
        names.UnionWith(new[] { "width", "vendor-two" });
        Assert.Equal(4, names.Count);
        Assert.True(names.Contains("COLOR"));
        Assert.False(names.Contains("missing"));
        Assert.True(names.SetEquals(new[] { "color", "width", "vendor-one", "vendor-two" }));
        Assert.True(names.IsSubsetOf(names.Concat(new[] { "extra" })));
        Assert.True(names.IsSupersetOf(new[] { "color" }));
        Assert.True(names.IsProperSubsetOf(names.Concat(new[] { "extra" })));
        Assert.True(names.IsProperSupersetOf(new[] { "color" }));
        Assert.True(names.Overlaps(new[] { "none", "width" }));
        Assert.Equal(4, ((System.Collections.IEnumerable)names).Cast<object>().Count());

        var clone = names.Clone(frozen: true);
        Assert.True(clone.IsFrozen);
        Assert.True(names.SetEquals(clone));
        Assert.Equal(names.GetContentHashCode(), clone.GetContentHashCode());
        Assert.Throws<InvalidOperationException>(() => clone.Add("height"));
        Assert.Throws<InvalidOperationException>(() => clone.Clear());
        names.Clear();
        Assert.Empty(names);
    }
}

using System.Collections;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssComputedValuesTests
{
    [Fact]
    public void CustomPropertyOverlaysShareFlattenEnumerateAndCompareContent()
    {
        var root = CssCustomPropertyMap.Create(new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["--accent"] = "red",
            ["--gap"] = "4px"
        });
        Assert.Same(root, root.WithOverrides(null));
        Assert.Same(root, root.WithOverrides(new Dictionary<string, string>()));
        Assert.Same(root, root.WithOverrides(new Dictionary<string, string> { ["--accent"] = "red" }));

        var child = root.WithOverrides(new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["--accent"] = "blue",
            ["--local"] = "yes"
        });
        Assert.Equal(3, child.Count);
        Assert.Equal("blue", child["--accent"]);
        Assert.False(child.ContainsKey("--ACCENT"));
        Assert.Equal("4px", child["--gap"]);
        Assert.Equal("yes", child["--local"]);
        Assert.True(child.ContainsKey("--accent"));
        Assert.False(child.TryGetValue("--missing", out _));
        Assert.Throws<KeyNotFoundException>(() => _ = child["--missing"]);
        Assert.Equal(new[] { "blue", "4px", "yes" }, child.Values.ToArray());

        var flat = child.CloneFlat();
        Assert.True(child.ContentEquals(flat));
        Assert.False(child.ContentEquals(CssCustomPropertyMap.Create(
            new Dictionary<string, string> { ["--accent"] = "green" })));

        var deep = child;
        for (var index = 0; index < 18; index++)
        {
            deep = deep.WithOverrides(new Dictionary<string, string> { [$"--depth-{index}"] = index.ToString() });
        }
        Assert.True(deep.Depth < 17);
        Assert.Equal("17", deep["--depth-17"]);
    }

    [Fact]
    public void CustomPropertyRebaseHandlesIdentityDirectOverlayAndUnrelatedMaps()
    {
        var oldParent = CssCustomPropertyMap.Create(new Dictionary<string, string> { ["--a"] = "old" });
        var newParent = CssCustomPropertyMap.Create(new Dictionary<string, string> { ["--a"] = "new" });
        Assert.True(oldParent.TryRebaseOnto(oldParent, newParent, out var parentResult));
        Assert.Same(newParent, parentResult);

        var child = oldParent.WithOverrides(new Dictionary<string, string> { ["--b"] = "child" });
        Assert.True(child.TryRebaseOnto(oldParent, newParent, out var childResult));
        Assert.Equal("new", childResult["--a"]);
        Assert.Equal("child", childResult["--b"]);

        var unrelated = CssCustomPropertyMap.Create(new Dictionary<string, string> { ["--z"] = "1" });
        Assert.False(unrelated.TryRebaseOnto(oldParent, newParent, out var unrelatedResult));
        Assert.Same(unrelated, unrelatedResult);
        Assert.Same(CssCustomPropertyMap.Empty, CssCustomPropertyMap.Empty.CloneFlat());
    }

    [Fact]
    public void ComputedValuesCombineOrdinaryAndCustomStorage()
    {
        var ordinary = new CssPropertyValueStore { ["color"] = "red", ["vendor-mode"] = "wide" };
        var custom = CssCustomPropertyMap.Create(new Dictionary<string, string> { ["--accent"] = "blue" });
        var values = new CssComputedValues(ordinary, custom);
        var equal = new CssComputedValues(ordinary.Clone(), custom.CloneFlat());

        Assert.Equal(3, values.Count);
        Assert.Equal(new[] { "color", "vendor-mode", "--accent" }, values.Keys.ToArray());
        Assert.Equal(new[] { "red", "wide", "blue" }, values.Values.ToArray());
        Assert.Equal("red", values["color"]);
        Assert.Equal("blue", values["--accent"]);
        Assert.True(values.ContainsKey("--accent"));
        Assert.False(values.TryGetValue("missing", out _));
        Assert.Throws<KeyNotFoundException>(() => _ = values["missing"]);
        Assert.True(values.ContentEquals(values));
        Assert.True(values.ContentEquals(equal));
        Assert.True(values.OrdinaryContentEquals(new CssComputedValues(ordinary, CssCustomPropertyMap.Empty)));
        equal.OrdinaryValues["color"] = "green";
        Assert.False(values.ContentEquals(equal));
        Assert.Equal(3, ((IEnumerable)values).Cast<object>().Count());
    }

    [Fact]
    public void DeclaredPropertySetsImplementSetContractAcrossBothStorageKinds()
    {
        var ordinary = new CssPropertyNameSet();
        ordinary.Add("color");
        ordinary.Add("vendor-mode");
        var custom = CssCustomPropertyMap.Create(new Dictionary<string, string> { ["--accent"] = "red" });
        var declared = new CssDeclaredPropertySet(ordinary, custom);
        var equal = new CssDeclaredPropertySet(ordinary.Clone(), custom.CloneFlat());

        Assert.Equal(3, declared.Count);
        Assert.True(declared.Contains("color"));
        Assert.True(declared.Contains("--accent"));
        Assert.True(declared.ContentEquals(equal));
        Assert.True(declared.OrdinaryContentEquals(equal));
        Assert.True(declared.SetEquals(equal));
        Assert.True(declared.SetEquals(new[] { "COLOR", "VENDOR-MODE", "--accent" }));
        Assert.False(declared.SetEquals(new[] { "COLOR", "VENDOR-MODE", "--ACCENT" }));
        Assert.True(declared.IsSubsetOf(new[] { "color", "vendor-mode", "--accent", "extra" }));
        Assert.True(declared.IsSupersetOf(new[] { "color" }));
        Assert.True(declared.IsProperSubsetOf(new[] { "color", "vendor-mode", "--accent", "extra" }));
        Assert.True(declared.IsProperSupersetOf(new[] { "color" }));
        Assert.True(declared.Overlaps(new[] { "none", "--accent" }));
        Assert.Equal(3, ((IEnumerable)declared).Cast<object>().Count());
    }
}

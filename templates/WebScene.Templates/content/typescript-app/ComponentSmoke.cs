using WebScene.Sdk;

namespace WebSceneTypeScriptApp;

internal static class ComponentSmoke
{
    public static void Run()
    {
        var cache = new WebSceneSharedAssetCache();
        var package = WebSceneComponentPackage.Open(Path.Combine(AppContext.BaseDirectory, "Component"), cache);
        var entry = package.GetEntryPoint();
        var compatibility = WebSceneCompatibilityChecker.Check(System.Text.Encoding.UTF8.GetString(entry.Content.Span), package.Manifest, package.Manifest.EntryPoint);
        if (!compatibility.IsCompatible) throw new InvalidDataException("The packaged component is outside Component Profile 1.");
        using var first = package.CreateInstance();
        using var second = package.CreateInstance();
        first.Mount(); second.Mount();
        first.SetState("value", 1); second.SetState("value", 2);
        if (ReferenceEquals(first, second) || cache.Count != 1) throw new InvalidOperationException("Component isolation/cache smoke failed.");
        Console.WriteLine($"PASS {package.Manifest.Id}@{package.Manifest.Version}: offline package, compatibility, cache, two isolated instances");
    }
}

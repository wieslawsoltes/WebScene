using System.Text.Json;
using WebScene.Sdk;

var repository = FindRepositoryRoot();
var componentsRoot = Path.Combine(repository, "samples", "components");
var catalogPath = Path.Combine(componentsRoot, "catalog.json");
using var catalog = JsonDocument.Parse(File.ReadAllText(catalogPath));
var declaredSamples = catalog.RootElement.GetProperty("samples")
    .EnumerateArray()
    .Select(static item => item.GetProperty("id").GetString()!)
    .ToArray();
if (declaredSamples.Length != 12)
{
    throw new InvalidDataException($"R5 sample catalog must contain 12 scenarios; found {declaredSamples.Length}.");
}
var requestedSample = args.FirstOrDefault();
var samplesToRun = string.IsNullOrWhiteSpace(requestedSample)
    ? declaredSamples
    : declaredSamples.Where(sample => string.Equals(sample, requestedSample, StringComparison.Ordinal)).ToArray();
if (samplesToRun.Length == 0)
{
    throw new ArgumentException($"Unknown R5 sample '{requestedSample}'.", nameof(args));
}

var sharedCache = new WebSceneSharedAssetCache();
foreach (var sample in samplesToRun)
{
    var root = Path.Combine(componentsRoot, sample);
    var package = WebSceneComponentPackage.Open(root, sharedCache);
    var sourcePath = Directory.EnumerateFiles(Path.Combine(root, "src"), "*.*", SearchOption.AllDirectories)
        .Single(static path => path.EndsWith(".ts", StringComparison.Ordinal) || path.EndsWith(".tsx", StringComparison.Ordinal));
    var compatibility = WebSceneCompatibilityChecker.Check(
        File.ReadAllText(sourcePath),
        package.Manifest,
        Path.GetRelativePath(repository, sourcePath));
    if (!compatibility.IsCompatible)
    {
        throw new InvalidDataException(string.Join(
            Environment.NewLine,
            compatibility.Diagnostics.Select(static value => $"{value.Source}:{value.Line}:{value.Column} {value.Code}: {value.Message}")));
    }

    var firstAsset = package.GetEntryPoint();
    var secondAsset = package.GetEntryPoint();
    if (firstAsset.Content.Length < 50_000)
    {
        throw new InvalidDataException($"{sample} entry bundle is only {firstAsset.Content.Length} bytes; expected the built interactive React application, not a lifecycle placeholder.");
    }
    if (!ReferenceEquals(firstAsset, secondAsset))
    {
        throw new InvalidOperationException($"{sample} did not reuse its immutable cached entry point.");
    }

    using var first = package.CreateInstance();
    using var second = package.CreateInstance();
    first.Mount();
    second.Mount();
    first.SetState("instance", 1);
    second.SetState("instance", 2);
    if (!first.TryGetState<int>("instance", out var firstValue)
        || !second.TryGetState<int>("instance", out var secondValue)
        || firstValue != 1
        || secondValue != 2)
    {
        throw new InvalidOperationException($"{sample} leaked mutable state between instances.");
    }
    first.Unmount();
    second.Unmount();
    Console.WriteLine($"PASS {sample}: offline assets, compatibility, shared cache, isolated lifecycle");
}

Console.WriteLine($"R5 sample catalog passed: {samplesToRun.Length}/{declaredSamples.Length} selected scenarios, cache entries={sharedCache.Count}, hits={sharedCache.Hits}.");

static string FindRepositoryRoot()
{
    var directory = new DirectoryInfo(AppContext.BaseDirectory);
    while (directory is not null)
    {
        if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln")))
        {
            return directory.FullName;
        }
        directory = directory.Parent;
    }
    throw new DirectoryNotFoundException("Could not locate WebScene.sln from the sample smoke output directory.");
}

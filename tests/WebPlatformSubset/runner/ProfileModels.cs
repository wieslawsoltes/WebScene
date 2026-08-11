using System.Text.Json.Serialization;

namespace WebScene.WebPlatformSubset.Runner;

internal sealed class ProfileManifest
{
    public required string Profile { get; init; }
    public required string WptRevision { get; init; }
    public required string Runtime { get; init; }
    public ViewportSettings Viewport { get; init; } = new();
    public List<ProfileTest> Required { get; init; } = [];
    public List<ProfileTest> Candidate { get; init; } = [];
    public List<ProfileTest> HarnessBlocked { get; init; } = [];
    public List<ExcludedArea> Excluded { get; init; } = [];
}

internal sealed class ViewportSettings
{
    public int Width { get; init; } = 800;
    public int Height { get; init; } = 600;
    public double DeviceScaleFactor { get; init; } = 1;
}

internal sealed class ProfileTest
{
    public required string Path { get; init; }
    public string Type { get; init; } = "testharness";
    public string? Reference { get; init; }
    public List<string> Capabilities { get; init; } = [];
    public List<string> Evidence { get; init; } = [];
    public List<VisualColorCheck> VisualChecks { get; init; } = [];
    public List<VisualColorGapCheck> VisualGapChecks { get; init; } = [];
    public string? Reason { get; init; }
}

internal sealed class VisualColorCheck
{
    public required string Color { get; init; }
    public int? MinimumPixels { get; init; }
    public int? MaximumPixels { get; init; }
    public string? Description { get; init; }
}

internal sealed class VisualColorGapCheck
{
    public required string FirstColor { get; init; }
    public required string SecondColor { get; init; }
    public string Axis { get; init; } = "vertical";
    public int? MinimumPixels { get; init; }
    public int? MaximumPixels { get; init; }
    public string? Description { get; init; }
}

internal sealed class ExcludedArea
{
    public required string Area { get; init; }
    public required string Reason { get; init; }
}

internal sealed class RunArtifact
{
    public required string Schema { get; init; }
    public required string Profile { get; init; }
    public required string ProfileSha256 { get; init; }
    public required string WptRevision { get; init; }
    public required string Runtime { get; init; }
    public required string Engine { get; init; }
    public string? NativeEngineIdentity { get; init; }
    public string? ChromiumIdentity { get; init; }
    public required DateTimeOffset StartedAt { get; init; }
    public required TimeSpan Duration { get; init; }
    public required string Selection { get; init; }
    public required RunSummary Summary { get; init; }
    public required List<TestResult> Results { get; init; }
}

internal sealed class RunSummary
{
    public int Tests { get; init; }
    public int Passed { get; init; }
    public int Failed { get; init; }
    public int TimedOut { get; init; }
    public int HarnessErrors { get; init; }
    public int Subtests { get; init; }
    public int SubtestsPassed { get; init; }
    public int SubtestsFailed { get; init; }
}

internal sealed class TestResult
{
    public required string Path { get; init; }
    public required string Type { get; init; }
    public required string Status { get; init; }
    public required TimeSpan Duration { get; init; }
    public string? Message { get; init; }
    public List<SubtestResult> Subtests { get; init; } = [];
    public Dictionary<string, string>? Artifacts { get; init; }
    public ChromiumOracleResult? ChromiumOracle { get; init; }
}

internal sealed class ChromiumOracleResult
{
    public required string Status { get; init; }
    public string? Message { get; init; }
    public PixelComparison? ChromiumTestToReference { get; init; }
    public PixelComparison? NativeToChromiumTest { get; init; }
    public Dictionary<string, string>? Artifacts { get; init; }
}

internal sealed class PixelComparison
{
    public required int Width { get; init; }
    public required int Height { get; init; }
    public required long TotalPixels { get; init; }
    public required long DifferingPixels { get; init; }
    public required double DifferingRatio { get; init; }
    public required int MaximumChannelDelta { get; init; }
    public string? DifferenceBounds { get; init; }
}

internal sealed class SubtestResult
{
    public required string Name { get; init; }
    public required string Status { get; init; }
    public string? Message { get; init; }
    public string? Stack { get; init; }
}

internal sealed class HarnessState
{
    public bool Complete { get; init; }
    public HarnessStatus? Harness { get; init; }
    public List<SubtestResult> Results { get; init; } = [];
    public List<string> Errors { get; init; } = [];
    public List<string> Diagnostics { get; init; } = [];
}

internal sealed class HarnessStatus
{
    public int Status { get; init; }
    public string? Message { get; init; }
    public string? Stack { get; init; }
}

internal sealed record RunnerOptions
{
    public required string RepositoryRoot { get; init; }
    public required string ManifestPath { get; init; }
    public required string OutputDirectory { get; init; }
    public string Selection { get; init; } = "required";
    public string? TestFilter { get; init; }
    public bool ListOnly { get; init; }
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(10);
    public string? NativeLibraryPath { get; init; }
    public string? NativeCacheDirectory { get; init; }
    public string? ChromiumPath { get; init; }
}

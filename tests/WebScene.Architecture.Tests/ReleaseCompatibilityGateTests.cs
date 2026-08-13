using System.Text.Json;
using Xunit;

namespace WebScene.Architecture.Tests;

public sealed class ReleaseCompatibilityGateTests
{
    private static readonly string s_repositoryRoot = FindRepositoryRoot();

    [Fact]
    public void RuntimeBuildersRunTheCompleteRequiredCompatibilityProfile()
    {
        AssertBuilderRunsCompleteRequiredProfile("build-native-engine-runtime.sh");
        AssertBuilderRunsCompleteRequiredProfile("build-native-engine-runtime.ps1");
    }

    [Fact]
    public void RequiredProfileContainsTheEstablishedReleaseDenominator()
    {
        var profilePath = Path.Combine(
            s_repositoryRoot,
            "tests",
            "WebPlatformSubset",
            "webscene-component-profile.json");
        using var profile = JsonDocument.Parse(File.ReadAllText(profilePath));
        var required = profile.RootElement.GetProperty("required");

        Assert.True(
            required.GetArrayLength() >= 110,
            $"The required compatibility denominator unexpectedly shrank to {required.GetArrayLength()} documents.");
    }

    [Fact]
    public void CandidateProfileContainsTheEstablishedDiscoveryDenominator()
    {
        var profilePath = Path.Combine(
            s_repositoryRoot,
            "tests",
            "WebPlatformSubset",
            "webscene-component-profile.json");
        using var profile = JsonDocument.Parse(File.ReadAllText(profilePath));
        var candidate = profile.RootElement.GetProperty("candidate");

        Assert.True(
            candidate.GetArrayLength() >= 79,
            $"The candidate compatibility denominator unexpectedly shrank to {candidate.GetArrayLength()} documents.");

        Assert.Contains(
            candidate.EnumerateArray(),
            test => string.Equals(
                test.GetProperty("path").GetString(),
                "contracts/dom-library-core-primitives.html",
                StringComparison.Ordinal));

        Assert.Empty(profile.RootElement.GetProperty("harnessBlocked").EnumerateArray());
        Assert.Equal(
            3,
            candidate.EnumerateArray().Count(test =>
                test.GetProperty("path").GetString()?.EndsWith(
                    ".any.js",
                    StringComparison.Ordinal) == true));
    }

    [Fact]
    public void CandidateVisualTestsRejectFailurePixelsAndBlankRenders()
    {
        var profilePath = Path.Combine(
            s_repositoryRoot,
            "tests",
            "WebPlatformSubset",
            "webscene-component-profile.json");
        using var profile = JsonDocument.Parse(File.ReadAllText(profilePath));
        var visualTests = profile.RootElement.GetProperty("candidate")
            .EnumerateArray()
            .Where(test => test.GetProperty("type").GetString() == "visual")
            .ToList();

        Assert.NotEmpty(visualTests);
        foreach (var visualTest in visualTests)
        {
            var path = visualTest.GetProperty("path").GetString();
            var checks = visualTest.GetProperty("visualChecks").EnumerateArray().ToList();
            Assert.True(
                checks.Any(check => check.TryGetProperty("maximumPixels", out var maximum)
                    && maximum.GetInt32() == 0),
                $"Visual test '{path}' has no zero-tolerance failure-color check.");
            var hasSuccessColor = checks.Any(check =>
                check.TryGetProperty("minimumPixels", out var minimum)
                && minimum.GetInt32() > 0);
            var hasComponentShape = visualTest.TryGetProperty(
                    "visualComponentChecks",
                    out var componentChecks)
                && componentChecks.EnumerateArray().Any(check =>
                    check.TryGetProperty("minimumPixels", out var minimum)
                    && minimum.GetInt32() > 0
                    && check.TryGetProperty("minimumFillRatio", out var fillRatio)
                    && fillRatio.GetDouble() >= 0.5
                    && check.TryGetProperty("maximumWidth", out var maximumWidth)
                    && maximumWidth.GetInt32() > 0
                    && check.TryGetProperty("maximumHeight", out var maximumHeight)
                    && maximumHeight.GetInt32() > 0);
            Assert.True(
                hasSuccessColor || hasComponentShape,
                $"Visual test '{path}' has no non-blank success-color or bounded component-shape check.");
        }

        Assert.Contains(
            visualTests,
            test => test.TryGetProperty("visualGapChecks", out var gapChecks)
                && gapChecks.EnumerateArray().Any(check =>
                    check.TryGetProperty("minimumPixels", out var minimum)
                    && minimum.GetInt32() > 0));
        Assert.Contains(
            visualTests,
            test => test.TryGetProperty(
                    "visualForegroundOffsetChecks",
                    out var offsetChecks)
                && offsetChecks.EnumerateArray().Any(check =>
                    check.TryGetProperty("minimumOffsetPixels", out var minimum)
                    && minimum.GetInt32() > 0));
        Assert.Contains(
            visualTests,
            test => test.TryGetProperty(
                    "visualComponentColorRegionChecks",
                    out var regionChecks)
                && regionChecks.EnumerateArray().Any(check =>
                    check.TryGetProperty("maximumPixels", out var maximum)
                    && maximum.GetInt32() >= 0)
                && regionChecks.EnumerateArray().Any(check =>
                    check.TryGetProperty("minimumPixels", out var minimum)
                    && minimum.GetInt32() > 0));
    }

    [Fact]
    public void RuntimeWorkflowRunsForProfileChangesAndPublishesPerRidEvidence()
    {
        var workflow = File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            ".github",
            "workflows",
            "native-runtime-packages.yml"));

        Assert.Contains("- 'tests/WebPlatformSubset/**'", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "- 'scripts/verify-cross-rid-compatibility.py'",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains(
            "- 'scripts/tests/test_verify_cross_rid_compatibility.py'",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains(
            "name: compatibility-required-${{ matrix.rid }}-${{ needs.metadata.outputs.package-version }}",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains(
            "path: artifacts/native-engine-runtime-build/**/wpt-results/**",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains("if-no-files-found: error", workflow, StringComparison.Ordinal);
        Assert.Contains("name: Run candidate compatibility discovery", workflow, StringComparison.Ordinal);
        Assert.Contains("--selection candidate", workflow, StringComparison.Ordinal);
        Assert.Contains("continue-on-error: true", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "path: artifacts/native-engine-runtime-build/**/wpt-candidate-results/**",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains("candidate-evidence:", workflow, StringComparison.Ordinal);
        Assert.Contains("required-evidence:", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "name: Verify cross-RID required evidence",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains("--selection required", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "name: compatibility-required-summary-${{ needs.metadata.outputs.package-version }}",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains(
            "needs: [metadata, packages, native, required-evidence]",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains(
            "name: Verify cross-RID candidate evidence",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains("name: Test cross-RID evidence verifier", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "scripts/verify-cross-rid-compatibility.py",
            workflow,
            StringComparison.Ordinal);
        Assert.Contains("--expected-rid osx-arm64", workflow, StringComparison.Ordinal);
        Assert.Contains("--expected-rid linux-x64", workflow, StringComparison.Ordinal);
        Assert.Contains("--expected-rid win-x64", workflow, StringComparison.Ordinal);
        Assert.Contains(
            "name: compatibility-candidate-summary-${{ needs.metadata.outputs.package-version }}",
            workflow,
            StringComparison.Ordinal);

        var verifier = File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            "scripts",
            "verify-cross-rid-compatibility.py"));
        Assert.Contains("webscene-wpt-subset-result-v3", verifier, StringComparison.Ordinal);
        Assert.Contains("profileSha256", verifier, StringComparison.Ordinal);
    }

    private static void AssertBuilderRunsCompleteRequiredProfile(string fileName)
    {
        var builder = File.ReadAllText(Path.Combine(s_repositoryRoot, "scripts", fileName));
        const string runnerProject = "WebScene.WebPlatformSubset.Runner.csproj";
        var invocationStart = builder.IndexOf(runnerProject, StringComparison.Ordinal);
        Assert.True(invocationStart >= 0, $"{fileName} does not invoke the compatibility runner.");
        var invocationEnd = builder.IndexOf("wpt-results", invocationStart, StringComparison.Ordinal);
        Assert.True(invocationEnd > invocationStart, $"{fileName} does not retain compatibility results.");
        var invocation = builder[invocationStart..invocationEnd];

        Assert.Contains("--selection required", invocation, StringComparison.Ordinal);
        Assert.DoesNotContain("--test ", invocation, StringComparison.Ordinal);
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "WebScene.sln")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the WebScene repository root.");
    }
}

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
            candidate.GetArrayLength() >= 53,
            $"The candidate compatibility denominator unexpectedly shrank to {candidate.GetArrayLength()} documents.");
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
            "name: compatibility-${{ matrix.rid }}-${{ needs.metadata.outputs.package-version }}",
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
        Assert.Contains(
            "name: Verify cross-RID candidate evidence",
            workflow,
            StringComparison.Ordinal);
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

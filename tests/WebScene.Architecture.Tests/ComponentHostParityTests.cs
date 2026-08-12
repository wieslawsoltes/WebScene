using Xunit;

namespace WebScene.Architecture.Tests;

public sealed class ComponentHostParityTests
{
    private static readonly string s_repositoryRoot = FindRepositoryRoot();

    [Fact]
    public void AvaloniaAndUnoRetainTheSameComponentLifecycleCore()
    {
        var avalonia = ReadHost("WebScene.Sdk.Avalonia");
        var uno = ReadHost("WebScene.Sdk.Uno");

        AssertEquivalentSection(
            avalonia,
            uno,
            "public enum WebSceneComponentHostState",
            "/// <summary>");
        AssertEquivalentSection(
            avalonia,
            uno,
            "private static readonly UTF8Encoding",
            "public WebSceneComponentHost()");

        var orderedBoundaries = new[]
        {
            "public void RegisterHostCapability(",
            "public bool RemoveHostCapability(",
            "public async Task MountAsync(",
            "public async Task UnmountAsync(",
            "public async Task ReloadAsync(",
            "public async ValueTask DisposeAsync(",
            "public static string ResolveNativeLibraryPath(",
        };
        for (var index = 0; index < orderedBoundaries.Length - 1; index++)
        {
            AssertEquivalentSection(
                avalonia,
                uno,
                orderedBoundaries[index],
                orderedBoundaries[index + 1]);
        }

        var coreBoundaries = new[]
        {
            "private async Task MountCoreAsync(",
            "private async Task CleanupCoreAsync(",
            "private async Task EvaluateDiscardAsync(",
            "private static string CreateLifecycleInvocation(",
            "private static string ResolvePackagePath(",
            "private static string NativeLibraryFileName(",
            "private void SetState(",
            "private void Report(",
            "private async Task RunAutomaticMountAsync(",
            "private async Task RunAutomaticUnmountAsync(",
            "private sealed class ForwardingDiagnosticSink(",
        };
        for (var index = 0; index < coreBoundaries.Length - 1; index++)
        {
            AssertEquivalentSection(
                avalonia,
                uno,
                coreBoundaries[index],
                coreBoundaries[index + 1]);
        }
    }

    private static void AssertEquivalentSection(
        string avalonia,
        string uno,
        string start,
        string end)
        => Assert.Equal(
            Normalize(Extract(avalonia, start, end)),
            Normalize(Extract(uno, start, end)));

    private static string Normalize(string source)
        => source
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace("firstDocumentSceneTimeout", "documentBarrierTimeout", StringComparison.Ordinal)
            .Replace("                Loaded -= OnLoaded;\n", string.Empty, StringComparison.Ordinal)
            .Replace("                Unloaded -= OnUnloaded;\n", string.Empty, StringComparison.Ordinal);

    private static string Extract(string source, string start, string end)
    {
        var startIndex = source.IndexOf(start, StringComparison.Ordinal);
        Assert.True(startIndex >= 0, $"Could not find section start '{start}'.");
        var endIndex = source.IndexOf(end, startIndex + start.Length, StringComparison.Ordinal);
        Assert.True(endIndex > startIndex, $"Could not find section end '{end}'.");
        return source[startIndex..endIndex];
    }

    private static string ReadHost(string project)
        => File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            "src",
            project,
            "WebSceneComponentHost.cs"));

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

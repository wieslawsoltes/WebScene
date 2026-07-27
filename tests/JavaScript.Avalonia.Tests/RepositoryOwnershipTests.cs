using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class RepositoryOwnershipTests
{
    [Fact]
    public void ProductRepositoryOwnsAdvancedControlSamples()
    {
        var repositoryRoot = FindRepositoryRoot();
        Assert.True(
            File.Exists(Path.Combine(
                repositoryRoot,
                "samples",
                "NativeMonacoEditor",
                "NativeMonacoEditor.csproj")),
            "The public Monaco editor sample is missing from the WebScene product repository.");
        Assert.True(
            File.Exists(Path.Combine(
                repositoryRoot,
                "samples",
                "NativeTradingViewTerminal",
                "NativeTradingViewTerminal.csproj")),
            "The public TradingView terminal sample is missing from the WebScene product repository.");
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln"))) return directory.FullName;
        }
        throw new DirectoryNotFoundException("Could not locate WebScene.sln.");
    }
}

using System.Globalization;
using System.Xml.Linq;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

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

    [Fact]
    public void TradingViewSampleAllowsCompactViewportVerification()
    {
        var repositoryRoot = FindRepositoryRoot();
        var window = XDocument.Load(Path.Combine(
            repositoryRoot,
            "samples",
            "NativeTradingViewTerminal",
            "MainWindow.axaml")).Root;

        Assert.NotNull(window);
        Assert.True(
            double.TryParse(
                window.Attribute("MinWidth")?.Value,
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var minimumWidth),
            "The TradingView sample must declare a numeric MinWidth.");
        Assert.True(
            minimumWidth <= 480,
            $"The TradingView sample MinWidth ({minimumWidth}) prevents compact viewport verification.");
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

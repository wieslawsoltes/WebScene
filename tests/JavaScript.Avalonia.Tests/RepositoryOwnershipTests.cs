using System.Text;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class RepositoryOwnershipTests
{
    private static readonly HashSet<string> TextExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".cs", ".cpp", ".h", ".js", ".mjs", ".ts", ".tsx", ".html", ".css",
        ".json", ".md", ".sh", ".py", ".props", ".targets", ".csproj", ".axaml"
    };

    [Fact]
    public void ReusableImplementationDoesNotOwnProductIntegrationArtifacts()
    {
        var repositoryRoot = FindRepositoryRoot();
        var roots = new[]
        {
            "src", "tests", "benchmarks", "samples", "tooling", "build",
            Path.Combine("third-party", "clearscript-patches"), "experiments"
        };
        var forbidden = new[]
        {
            "trading" + "view",
            "charting" + "_library",
            "__" + "tv"
        };
        var violations = new List<string>();

        foreach (var rootName in roots)
        {
            var root = Path.Combine(repositoryRoot, rootName);
            if (!Directory.Exists(root)) continue;
            foreach (var path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                if (IsGeneratedPath(path) || !TextExtensions.Contains(Path.GetExtension(path))) continue;
                var relative = Path.GetRelativePath(repositoryRoot, path);
                if (forbidden.Any(token => relative.Contains(token, StringComparison.OrdinalIgnoreCase)))
                {
                    violations.Add(relative);
                    continue;
                }
                var content = File.ReadAllText(path, Encoding.UTF8);
                if (forbidden.Any(token => content.Contains(token, StringComparison.OrdinalIgnoreCase)))
                {
                    violations.Add(relative);
                }
            }
        }

        Assert.True(
            violations.Count == 0,
            "Product integration artifacts belong in their product repository: "
                + string.Join(", ", violations.Order(StringComparer.Ordinal)));
    }

    private static bool IsGeneratedPath(string path)
    {
        var segments = path.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return segments.Any(segment => segment is ".git" or "bin" or "obj" or "node_modules" or "TestResults");
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

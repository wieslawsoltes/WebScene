namespace JavaScript.Avalonia.Benchmarks;

internal static class BenchmarkPaths
{
    internal static string RepoRoot { get; } = FindRepoRoot();

    internal static string PlaygroundRoot => Path.Combine(RepoRoot, "samples", "JavaScriptPlayground");

    private static string FindRepoRoot()
    {
        var configured = Environment.GetEnvironmentVariable("WEBSCENE_REPO_ROOT");
        if (!string.IsNullOrWhiteSpace(configured) && IsRepoRoot(configured))
        {
            return Path.GetFullPath(configured);
        }

        foreach (var start in new[] { Directory.GetCurrentDirectory(), AppContext.BaseDirectory })
        {
            for (var directory = new DirectoryInfo(start); directory is not null; directory = directory.Parent)
            {
                if (IsRepoRoot(directory.FullName))
                {
                    return directory.FullName;
                }
            }
        }

        throw new DirectoryNotFoundException(
            "Could not locate the WebScene repository. Set WEBSCENE_REPO_ROOT when running benchmarks externally.");
    }

    private static bool IsRepoRoot(string path)
        => File.Exists(Path.Combine(path, "WebScene.sln"))
           && Directory.Exists(Path.Combine(path, "src", "JavaScript.Avalonia"));
}

using WebScene.WebPlatformSubset.Runner;

var options = CommandLine.Parse(args);
return new WptSubsetRunner(options).Run();

internal static class CommandLine
{
    internal static RunnerOptions Parse(string[] args)
    {
        var repositoryRoot = FindRepositoryRoot(AppContext.BaseDirectory);
        var manifestPath = Path.Combine(repositoryRoot, "tests", "WebPlatformSubset", "webscene-component-profile.json");
        var outputDirectory = Path.Combine(repositoryRoot, "TestResults", "WebPlatformSubset");
        var selection = "required";
        string? filter = null;
        var listOnly = false;
        var timeout = TimeSpan.FromSeconds(10);
        string? nativeLibraryPath = null;
        string? nativeCacheDirectory = null;
        string? chromiumPath = null;

        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--manifest":
                    manifestPath = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--output":
                    outputDirectory = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--selection":
                    selection = RequireValue(args, ref index).ToLowerInvariant();
                    if (selection is not ("required" or "candidate" or "all"))
                    {
                        throw new ArgumentException("--selection must be required, candidate, or all.");
                    }
                    break;
                case "--test":
                    filter = RequireValue(args, ref index);
                    break;
                case "--timeout-seconds":
                    timeout = TimeSpan.FromSeconds(double.Parse(RequireValue(args, ref index)));
                    break;
                case "--native-library":
                    nativeLibraryPath = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--native-cache-directory":
                    nativeCacheDirectory = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--chromium-path":
                    chromiumPath = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--list":
                    listOnly = true;
                    break;
                case "--help":
                case "-h":
                    PrintHelp();
                    Environment.Exit(0);
                    break;
                default:
                    throw new ArgumentException($"Unknown argument '{args[index]}'. Use --help for usage.");
            }
        }

        return new RunnerOptions
        {
            RepositoryRoot = repositoryRoot,
            ManifestPath = manifestPath,
            OutputDirectory = outputDirectory,
            Selection = selection,
            TestFilter = filter,
            ListOnly = listOnly,
            Timeout = timeout,
            NativeLibraryPath = nativeLibraryPath,
            NativeCacheDirectory = nativeCacheDirectory,
            ChromiumPath = chromiumPath
        };
    }

    private static string RequireValue(string[] args, ref int index)
    {
        if (++index >= args.Length)
        {
            throw new ArgumentException($"Missing value after '{args[index - 1]}'.");
        }

        return args[index];
    }

    private static string FindRepositoryRoot(string start)
    {
        for (var directory = new DirectoryInfo(start); directory is not null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln")))
            {
                return directory.FullName;
            }
        }

        var current = Directory.GetCurrentDirectory();
        if (File.Exists(Path.Combine(current, "WebScene.sln")))
        {
            return current;
        }

        throw new DirectoryNotFoundException("Could not locate the WebScene repository root.");
    }

    private static void PrintHelp()
    {
        Console.WriteLine("WebScene component conformance subset runner");
        Console.WriteLine("  --selection required|candidate|all  Tests to run (default: required)");
        Console.WriteLine("  --test <substring>                  Restrict to matching upstream paths");
        Console.WriteLine("  --timeout-seconds <seconds>         Per-document timeout (default: 10)");
        Console.WriteLine("  --native-library <path>              Native engine library for native mode");
        Console.WriteLine("  --native-cache-directory <path>      Native V8 compilation cache");
        Console.WriteLine("  --chromium-path <path>               Optional Chromium reftest oracle executable");
        Console.WriteLine("  --output <directory>                Artifact directory");
        Console.WriteLine("  --manifest <path>                   Profile manifest path");
        Console.WriteLine("  --list                              List selected tests without running");
    }
}

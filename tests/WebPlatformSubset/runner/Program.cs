using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using WebScene.WebPlatformSubset.Runner;

var options = CommandLine.Parse(args);
if (options.Engine == "both")
{
    // ClearScript and the directly linked native engine each own V8 platform
    // process globals. Keep the parity lanes process-isolated so a test result
    // measures engine behavior rather than competing V8 initialization.
    var managed = CommandLine.RunChild(options, "managed");
    var native = CommandLine.RunChild(options, "native");
    return managed == 0 && native == 0 ? 0 : 1;
}

return new WptSubsetRunner(options).Run();

internal static class CommandLine
{
    internal static int RunChild(RunnerOptions options, string engine)
    {
        var processPath = Environment.ProcessPath
                          ?? throw new InvalidOperationException("Cannot locate the WPT runner executable.");
        var start = new ProcessStartInfo(processPath)
        {
            UseShellExecute = false
        };
        if (string.Equals(
                Path.GetFileNameWithoutExtension(processPath),
                "dotnet",
                StringComparison.OrdinalIgnoreCase))
        {
            start.ArgumentList.Add(
                Assembly.GetEntryAssembly()?.Location
                ?? throw new InvalidOperationException("Cannot locate the WPT runner assembly."));
        }

        Add(start, "--engine", engine);
        Add(start, "--manifest", options.ManifestPath);
        Add(start, "--output", Path.Combine(options.OutputDirectory, engine));
        Add(start, "--selection", options.Selection);
        Add(start, "--timeout-seconds", options.Timeout.TotalSeconds.ToString(CultureInfo.InvariantCulture));
        if (options.TestFilter is not null) Add(start, "--test", options.TestFilter);
        if (options.NativeLibraryPath is not null) Add(start, "--native-library", options.NativeLibraryPath);
        if (options.NativeCacheDirectory is not null)
        {
            Add(start, "--native-cache-directory", options.NativeCacheDirectory);
        }
        if (options.ListOnly) start.ArgumentList.Add("--list");

        using var process = Process.Start(start)
                            ?? throw new InvalidOperationException($"Could not start the {engine} WPT lane.");
        process.WaitForExit();
        return process.ExitCode;
    }

    private static void Add(ProcessStartInfo start, string name, string value)
    {
        start.ArgumentList.Add(name);
        start.ArgumentList.Add(value);
    }

    internal static RunnerOptions Parse(string[] args)
    {
        var repositoryRoot = FindRepositoryRoot(AppContext.BaseDirectory);
        var manifestPath = Path.Combine(repositoryRoot, "tests", "WebPlatformSubset", "webscene-component-profile.json");
        var outputDirectory = Path.Combine(repositoryRoot, "TestResults", "WebPlatformSubset");
        var selection = "required";
        string? filter = null;
        var listOnly = false;
        var timeout = TimeSpan.FromSeconds(10);
        var engine = "managed";
        string? nativeLibraryPath = null;
        string? nativeCacheDirectory = null;

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
                case "--engine":
                    engine = RequireValue(args, ref index).ToLowerInvariant();
                    if (engine is not ("managed" or "native" or "both"))
                    {
                        throw new ArgumentException("--engine must be managed, native, or both.");
                    }
                    break;
                case "--native-library":
                    nativeLibraryPath = Path.GetFullPath(RequireValue(args, ref index));
                    break;
                case "--native-cache-directory":
                    nativeCacheDirectory = Path.GetFullPath(RequireValue(args, ref index));
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
            Engine = engine,
            NativeLibraryPath = nativeLibraryPath,
            NativeCacheDirectory = nativeCacheDirectory
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
        Console.WriteLine("  --engine managed|native|both        Runtime adapter (default: managed)");
        Console.WriteLine("  --native-library <path>              Native engine library for native mode");
        Console.WriteLine("  --native-cache-directory <path>      Native V8 compilation cache");
        Console.WriteLine("  --output <directory>                Artifact directory");
        Console.WriteLine("  --manifest <path>                   Profile manifest path");
        Console.WriteLine("  --list                              List selected tests without running");
    }
}

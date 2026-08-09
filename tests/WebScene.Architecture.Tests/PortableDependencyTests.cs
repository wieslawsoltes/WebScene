using System.Reflection;
using System.Xml.Linq;
using WebScene.Core;
using WebScene.Css;
using WebScene.Dom;
using WebScene.Graphics;
using WebScene.Sdk;
using WebScene.Backends;
using WebScene.Backends.Avalonia;
using Xunit;

namespace WebScene.Architecture.Tests;

public sealed class PortableDependencyTests
{
    private static readonly string s_repositoryRoot = FindRepositoryRoot();
    private static readonly string[] s_forbiddenPortableReferences =
    [
        "Avalonia",
        "Microsoft.ClearScript",
        "Microsoft.UI.Xaml",
        "PresentationCore",
        "PresentationFramework",
        "ProGPU",
        "Uno"
    ];

    [Fact]
    public void CoreAssemblyHasNoUiFrameworkOrJavaScriptEngineReferences()
    {
        var references = typeof(WebScenePoint).Assembly
            .GetReferencedAssemblies()
            .Select(reference => reference.Name ?? string.Empty)
            .ToArray();

        foreach (var forbidden in s_forbiddenPortableReferences)
        {
            Assert.DoesNotContain(references, name => name.StartsWith(forbidden, StringComparison.Ordinal));
        }
    }

    [Fact]
    public void CoreProjectAndSourcesContainNoForbiddenFrameworkDependencies()
    {
        var coreDirectory = Path.Combine(s_repositoryRoot, "src", "WebScene.Core");
        var project = XDocument.Load(Path.Combine(coreDirectory, "WebScene.Core.csproj"));
        var dependencies = project
            .Descendants()
            .Where(element => element.Name.LocalName is "PackageReference" or "ProjectReference")
            .Select(element => (string?)element.Attribute("Include") ?? string.Empty)
            .ToArray();

        Assert.Empty(dependencies);

        var source = string.Join(
            '\n',
            Directory.EnumerateFiles(coreDirectory, "*.cs", SearchOption.AllDirectories)
                .OrderBy(path => path, StringComparer.Ordinal)
                .Select(File.ReadAllText));
        foreach (var forbidden in new[]
                 {
                     "using Avalonia",
                     "using Microsoft.ClearScript",
                     "using Microsoft.UI.Xaml",
                     "using System.Windows",
                     "using ProGPU",
                     "using Uno"
                 })
        {
            Assert.DoesNotContain(forbidden, source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void DomCoreHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(DomEvent).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "WebScene.Dom",
            "../WebScene.Core/WebScene.Core.csproj");
    }

    [Fact]
    public void CssCoreHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(CssPropertyValueStore).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "WebScene.Css",
            "../WebScene.Core/WebScene.Core.csproj",
            "AngleSharp",
            "AngleSharp.Css");
    }

    [Fact]
    public void GraphicsCoreHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(CanvasPacketReader).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "WebScene.Graphics",
            "../WebScene.Core/WebScene.Core.csproj");
    }

    [Fact]
    public void ProductSdkHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(WebSceneComponentManifest).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "WebScene.Sdk",
            "../WebScene.Core/WebScene.Core.csproj");
    }

    [Fact]
    public void ManagedEngineProjectsAreAbsentAndComponentHostsAreNativeOnly()
    {
        Assert.False(File.Exists(Path.Combine(
            s_repositoryRoot,
            "src",
            "JavaScript.Avalonia.ClearScript",
            "JavaScript.Avalonia.ClearScript.csproj")));
        var componentHostProject = Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.Sdk.Avalonia",
            "WebScene.Sdk.Avalonia.csproj");
        Assert.True(File.Exists(componentHostProject));
        var componentHost = File.ReadAllText(componentHostProject);
        Assert.Contains("WebScene.Sdk.csproj", componentHost, StringComparison.Ordinal);
        Assert.Contains("WebScene.Backend.Avalonia.csproj", componentHost, StringComparison.Ordinal);
        Assert.DoesNotContain("ClearScript", componentHost, StringComparison.OrdinalIgnoreCase);
        var unoComponentHostProject = Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.Sdk.Uno",
            "WebScene.Sdk.Uno.csproj");
        Assert.True(File.Exists(unoComponentHostProject));
        var unoComponentHost = File.ReadAllText(unoComponentHostProject);
        Assert.Contains("WebScene.Sdk.csproj", unoComponentHost, StringComparison.Ordinal);
        Assert.Contains("WebScene.Backend.Uno.csproj", unoComponentHost, StringComparison.Ordinal);
        Assert.DoesNotContain("ClearScript", unoComponentHost, StringComparison.OrdinalIgnoreCase);
        var legacyAvaloniaDirectory = Path.Combine(s_repositoryRoot, "src", "JavaScript.Avalonia");
        Assert.True(
            !Directory.Exists(legacyAvaloniaDirectory)
            || !Directory.EnumerateFiles(
                legacyAvaloniaDirectory,
                "*.cs",
                SearchOption.TopDirectoryOnly).Any());
        Assert.False(File.Exists(Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.JavaScript",
            "WebScene.JavaScript.csproj")));

        var solution = File.ReadAllText(Path.Combine(s_repositoryRoot, "WebScene.sln"));
        Assert.DoesNotContain("ClearScript", solution, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("WebScene.Sdk.Avalonia", solution, StringComparison.Ordinal);
        Assert.Contains("WebScene.Sdk.Uno", solution, StringComparison.Ordinal);
        Assert.DoesNotContain("src\\WebScene.JavaScript\\WebScene.JavaScript.csproj", solution, StringComparison.Ordinal);
    }

    [Fact]
    public void ProductionRuntimeBuildersAlwaysIncludeV8Inspector()
    {
        var unixBuilder = File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            "scripts",
            "build-native-engine-runtime.sh"));
        Assert.Contains(
            "-DWEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR=ON",
            unixBuilder,
            StringComparison.Ordinal);
        Assert.Contains(
            "-p:WebSceneNativeEngineV8Inspector=true",
            unixBuilder,
            StringComparison.Ordinal);
        Assert.DoesNotContain("--v8-inspector", unixBuilder, StringComparison.Ordinal);
        Assert.DoesNotContain("--no-v8-inspector", unixBuilder, StringComparison.Ordinal);

        var windowsBuilder = File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            "scripts",
            "build-native-engine-runtime.ps1"));
        Assert.Contains(
            "-DWEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR=ON",
            windowsBuilder,
            StringComparison.Ordinal);
        Assert.Contains(
            "-p:WebSceneNativeEngineV8Inspector=true",
            windowsBuilder,
            StringComparison.Ordinal);
        Assert.DoesNotContain("[switch] $V8Inspector", windowsBuilder, StringComparison.Ordinal);
        Assert.DoesNotContain("NoV8Inspector", windowsBuilder, StringComparison.Ordinal);

        var packageProject = File.ReadAllText(Path.Combine(
            s_repositoryRoot,
            "packaging",
            "WebScene.NativeEngine.Runtime",
            "WebScene.NativeEngine.Runtime.csproj"));
        Assert.Contains(
            "<WebSceneNativeEngineV8Inspector>true</WebSceneNativeEngineV8Inspector>",
            packageProject,
            StringComparison.Ordinal);
        Assert.Contains(
            "Published native runtime packages must include V8 Inspector support.",
            packageProject,
            StringComparison.Ordinal);
    }

    [Fact]
    public void AvaloniaBackendPackageDependsInwardAndOwnsTheImplementation()
    {
        var projectPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.Backend.Avalonia",
            "WebScene.Backend.Avalonia.csproj");
        var references = LoadDeclaredReferences(projectPath);

        Assert.Contains("../WebScene.Core/WebScene.Core.csproj", references);
        Assert.Contains("../WebScene.Backend.Abstractions/WebScene.Backend.Abstractions.csproj", references);
        Assert.Contains("../WebScene.Css/WebScene.Css.csproj", references);
        Assert.Contains("../WebScene.JavaScript.Interop/WebScene.JavaScript.Interop.csproj", references);
        Assert.DoesNotContain("../WebScene.Dom/WebScene.Dom.csproj", references);
        Assert.DoesNotContain("../WebScene.Graphics/WebScene.Graphics.csproj", references);
        Assert.DoesNotContain("../WebScene.JavaScript/WebScene.JavaScript.csproj", references);
        Assert.DoesNotContain("../JavaScript.Avalonia/JavaScript.Avalonia.csproj", references);
        Assert.Equal(
            "WebScene.Backend.Avalonia",
            typeof(WebScene.Backends.Avalonia.Native.NativeSceneSurface).Assembly.GetName().Name);
    }

    [Fact]
    public void LegacyJavaScriptAvaloniaPackageIsAbsent()
    {
        var projectPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "JavaScript.Avalonia",
            "JavaScript.Avalonia.csproj");
        Assert.False(File.Exists(projectPath));

        var solution = File.ReadAllText(Path.Combine(s_repositoryRoot, "WebScene.sln"));
        Assert.DoesNotContain("src\\JavaScript.Avalonia\\JavaScript.Avalonia.csproj", solution, StringComparison.Ordinal);
    }

    [Fact]
    public void AvaloniaPresenterDoesNotPublishTheRemovedManagedBackendManifest()
    {
        var manifestPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.Backend.Avalonia",
            "webscene-backend.json");
        Assert.False(File.Exists(manifestPath));

        var schemaPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "WebScene.Backend.Abstractions",
            "schemas",
            "webscene-backend-capabilities.schema.json");
        using var schema = System.Text.Json.JsonDocument.Parse(File.ReadAllText(schemaPath));
        Assert.Equal(
            "https://json-schema.org/draft/2020-12/schema",
            schema.RootElement.GetProperty("$schema").GetString());
    }

    [Fact]
    public void SourceProjectReferenceGraphIsAcyclic()
    {
        var sourceRoot = Path.Combine(s_repositoryRoot, "src");
        var projects = Directory
            .EnumerateFiles(sourceRoot, "*.csproj", SearchOption.AllDirectories)
            .Select(Path.GetFullPath)
            .ToDictionary(path => path, LoadProjectReferences, StringComparer.OrdinalIgnoreCase);

        var visiting = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var project in projects.Keys)
        {
            Visit(project, projects, visiting, visited, new Stack<string>());
        }
    }

    private static IReadOnlyList<string> LoadProjectReferences(string projectPath)
    {
        var directory = Path.GetDirectoryName(projectPath)!;
        return XDocument.Load(projectPath)
            .Descendants()
            .Where(element => element.Name.LocalName == "ProjectReference")
            .Select(element => (string?)element.Attribute("Include"))
            .Where(include => !string.IsNullOrWhiteSpace(include))
            .Select(include => Path.GetFullPath(Path.Combine(directory, include!)))
            .ToArray();
    }

    private static string[] LoadDeclaredReferences(string projectPath)
        => XDocument.Load(projectPath)
            .Descendants()
            .Where(element => element.Name.LocalName is "PackageReference" or "ProjectReference")
            .Select(element => ((string?)element.Attribute("Include") ?? string.Empty).Replace('\\', '/'))
            .ToArray();

    private static void AssertAssemblyHasNoReferences(
        Assembly assembly,
        IReadOnlyList<string> forbiddenReferences)
    {
        var references = assembly.GetReferencedAssemblies()
            .Select(reference => reference.Name ?? string.Empty)
            .ToArray();
        foreach (var forbidden in forbiddenReferences)
        {
            Assert.DoesNotContain(
                references,
                name => name.StartsWith(forbidden, StringComparison.Ordinal));
        }
    }

    private static void AssertPortableProject(
        string projectName,
        params string[] allowedReferences)
    {
        var directory = Path.Combine(s_repositoryRoot, "src", projectName);
        var projectPath = Path.Combine(directory, projectName + ".csproj");
        var references = LoadDeclaredReferences(projectPath);
        Assert.All(references, reference => Assert.Contains(reference, allowedReferences));

        var source = string.Join(
            '\n',
            Directory.EnumerateFiles(directory, "*.cs", SearchOption.AllDirectories)
                .OrderBy(path => path, StringComparer.Ordinal)
                .Select(File.ReadAllText));
        foreach (var forbidden in new[]
                 {
                     "using Avalonia",
                     "using Microsoft.ClearScript",
                     "using Microsoft.UI.Xaml",
                     "using System.Windows",
                     "using ProGPU",
                     "using Uno"
                 })
        {
            Assert.DoesNotContain(forbidden, source, StringComparison.Ordinal);
        }
    }

    private static void Visit(
        string project,
        IReadOnlyDictionary<string, IReadOnlyList<string>> projects,
        ISet<string> visiting,
        ISet<string> visited,
        Stack<string> path)
    {
        if (visited.Contains(project))
        {
            return;
        }

        if (!visiting.Add(project))
        {
            var cycle = string.Join(" -> ", path.Reverse().Append(project).Select(Path.GetFileNameWithoutExtension));
            Assert.Fail($"Project-reference cycle detected: {cycle}");
        }

        path.Push(project);
        if (projects.TryGetValue(project, out var references))
        {
            foreach (var reference in references.Where(projects.ContainsKey))
            {
                Visit(reference, projects, visiting, visited, path);
            }
        }

        path.Pop();
        visiting.Remove(project);
        visited.Add(project);
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null;
             directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "WebScene.sln")))
            {
                return directory.FullName;
            }
        }

        throw new DirectoryNotFoundException("Could not locate WebScene.sln from the test output directory.");
    }
}

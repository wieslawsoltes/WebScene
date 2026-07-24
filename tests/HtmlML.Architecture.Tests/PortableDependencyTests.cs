using System.Reflection;
using System.Xml.Linq;
using HtmlML.Core;
using HtmlML.Css;
using HtmlML.Graphics;
using HtmlML.JavaScript;
using HtmlML.Sdk;
using HtmlML.Backends;
using HtmlML.Backends.Avalonia;
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace HtmlML.Architecture.Tests;

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
        var references = typeof(HtmlMlPoint).Assembly
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
        var coreDirectory = Path.Combine(s_repositoryRoot, "src", "HtmlML.Core");
        var project = XDocument.Load(Path.Combine(coreDirectory, "HtmlML.Core.csproj"));
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
    public void JavaScriptContractsHaveNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(IHtmlMlJavaScriptHost).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "HtmlML.JavaScript",
            "../HtmlML.Core/HtmlML.Core.csproj");
    }

    [Fact]
    public void DomCoreHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(DomEvent).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "HtmlML.Dom",
            "../HtmlML.Core/HtmlML.Core.csproj");
    }

    [Fact]
    public void CssCoreHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(CssPropertyValueStore).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "HtmlML.Css",
            "../HtmlML.Core/HtmlML.Core.csproj",
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
            "HtmlML.Graphics",
            "../HtmlML.Core/HtmlML.Core.csproj");
    }

    [Fact]
    public void ProductSdkHasNoUiFrameworkOrEngineReferences()
    {
        AssertAssemblyHasNoReferences(
            typeof(HtmlMlComponentManifest).Assembly,
            s_forbiddenPortableReferences);
        AssertPortableProject(
            "HtmlML.Sdk",
            "../HtmlML.Core/HtmlML.Core.csproj",
            "../HtmlML.JavaScript/HtmlML.JavaScript.csproj");
    }

    [Fact]
    public void ClearScriptAdapterHasNoUiFrameworkOrAvaloniaRuntimeReference()
    {
        AssertAssemblyHasNoReferences(
            typeof(ClearScriptV8Runtime).Assembly,
            [
                "Avalonia",
                "JavaScript.Avalonia",
                "Microsoft.UI.Xaml",
                "PresentationCore",
                "PresentationFramework",
                "ProGPU",
                "Uno"
            ]);

        var projectPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "JavaScript.Avalonia.ClearScript",
            "JavaScript.Avalonia.ClearScript.csproj");
        var references = LoadDeclaredReferences(projectPath);
        Assert.Contains("../HtmlML.JavaScript/HtmlML.JavaScript.csproj", references);
        Assert.DoesNotContain("../JavaScript.Avalonia/JavaScript.Avalonia.csproj", references);
    }

    [Fact]
    public void AvaloniaBackendPackageDependsInwardAndOwnsTheImplementation()
    {
        var projectPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "HtmlML.Backend.Avalonia",
            "HtmlML.Backend.Avalonia.csproj");
        var references = LoadDeclaredReferences(projectPath);

        Assert.Contains("../HtmlML.Core/HtmlML.Core.csproj", references);
        Assert.Contains("../HtmlML.Backend.Abstractions/HtmlML.Backend.Abstractions.csproj", references);
        Assert.Contains("../HtmlML.Css/HtmlML.Css.csproj", references);
        Assert.Contains("../HtmlML.Dom/HtmlML.Dom.csproj", references);
        Assert.Contains("../HtmlML.Graphics/HtmlML.Graphics.csproj", references);
        Assert.Contains("../HtmlML.JavaScript/HtmlML.JavaScript.csproj", references);
        Assert.DoesNotContain("../JavaScript.Avalonia/JavaScript.Avalonia.csproj", references);
        Assert.Equal("HtmlML.Backend.Avalonia", typeof(AvaloniaBrowserHost).Assembly.GetName().Name);
        Assert.Equal("HtmlML.Backend.Avalonia", typeof(AvaloniaBackendHost).Assembly.GetName().Name);
        Assert.Equal(
            "HtmlML.Backend.Avalonia",
            typeof(HtmlML.Backends.Avalonia.Native.NativeSceneSurface).Assembly.GetName().Name);
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

        var solution = File.ReadAllText(Path.Combine(s_repositoryRoot, "HtmlML.sln"));
        Assert.DoesNotContain("src\\JavaScript.Avalonia\\JavaScript.Avalonia.csproj", solution, StringComparison.Ordinal);
    }

    [Fact]
    public void AvaloniaCapabilityManifestUsesThePublishedSchemaAndRuntimeType()
    {
        var manifestPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "HtmlML.Backend.Avalonia",
            "htmlml-backend.json");
        var manifest = HtmlMlBackendManifestSerializer.Parse(File.ReadAllText(manifestPath));
        Assert.Equal(HtmlMlBackendManifest.CurrentSchemaVersion, manifest.SchemaVersion);
        Assert.Equal(HtmlMlBackendSupportLevel.Advanced, manifest.MaximumSupportLevel);
        Assert.Equal(typeof(AvaloniaBackendHost).FullName, manifest.BackendType);
        Assert.Equal(
            AvaloniaBackendHost.DefaultCapabilities,
            HtmlMlBackendManifestSerializer.ResolveCapabilities(manifest));

        var schemaPath = Path.Combine(
            s_repositoryRoot,
            "src",
            "HtmlML.Backend.Abstractions",
            "schemas",
            "htmlml-backend-capabilities.schema.json");
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
            if (File.Exists(Path.Combine(directory.FullName, "HtmlML.sln")))
            {
                return directory.FullName;
            }
        }

        throw new DirectoryNotFoundException("Could not locate HtmlML.sln from the test output directory.");
    }
}

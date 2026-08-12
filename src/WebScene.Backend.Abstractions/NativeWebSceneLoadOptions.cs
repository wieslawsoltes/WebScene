using WebScene.Core;

namespace WebScene.Backends.Native;

/// <summary>
/// A JavaScript program that runs during document start, before authored
/// scripts in the selected browsing contexts.
/// </summary>
public sealed record WebSceneDocumentScript(
    string Source,
    string Name,
    bool AllFrames = true);

/// <summary>Options for loading a document in a native WebScene backend.</summary>
public sealed record NativeWebSceneLoadOptions
{
    public required string Source { get; init; }

    public required string NativeLibraryPath { get; init; }

    /// <summary>
    /// Gets an optional path to the RID-specific native GPU provider. When
    /// omitted, the runtime probes for the provider beside the native engine.
    /// A missing or incompatible provider never enables a software fallback.
    /// </summary>
    public string? NativeGpuLibraryPath { get; init; }

    /// <summary>
    /// Gets the capabilities that must be resolved before document startup.
    /// Missing capabilities fail the load before authored JavaScript runs.
    /// </summary>
    public WebSceneBackendCapabilities RequiredCapabilities { get; init; }
        = WebSceneBackendCapabilities.None;

    public string? CompilationCacheDirectory { get; init; }

    public IReadOnlyList<WebSceneDocumentScript> DocumentStartScripts { get; init; } = [];

    /// <summary>
    /// Gets the resource policy for this document. When omitted, the platform
    /// backend's default resource loader is used.
    /// </summary>
    public IWebSceneResourceLoader? ResourceLoader { get; init; }
}

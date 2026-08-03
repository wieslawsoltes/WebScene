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

    public string? CompilationCacheDirectory { get; init; }

    public IReadOnlyList<WebSceneDocumentScript> DocumentStartScripts { get; init; } = [];
}

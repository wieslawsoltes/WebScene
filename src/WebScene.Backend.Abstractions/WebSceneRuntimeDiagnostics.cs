namespace WebScene.Backends.Native;

public enum WebSceneRuntimeState { Unloaded, Loading, Ready, Failed, Disposed }

/// <summary>A copied, bounded value, never a live JavaScript object.</summary>
public sealed record WebSceneConsoleArgument(string Type, string Value);

/// <summary>Immutable metadata copied at the runtime boundary. Locations may be unavailable (zero/null).</summary>
public sealed record WebSceneDiagnosticContext(
    long Generation, long Sequence, DateTimeOffset Timestamp,
    string? DocumentUrl, uint FrameId, string? Source, int Line, int Column,
    bool Truncated = false);

public sealed record WebSceneJavaScriptException(
    string Message, string? Stack, bool IsUnhandledPromiseRejection, WebSceneDiagnosticContext Context);

public sealed record WebSceneConsoleMessage(
    string Level, string Message, string? Stack,
    IReadOnlyList<WebSceneConsoleArgument> Arguments, WebSceneDiagnosticContext Context);

/// <summary>A terminal failure for one loaded runtime; ordinary uncaught JavaScript exceptions are not terminal.</summary>
public sealed record WebSceneRuntimeFailure(
    string Message, string? Stack, string Stage, WebSceneDiagnosticContext Context);

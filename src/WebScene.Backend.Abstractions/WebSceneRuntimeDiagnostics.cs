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

/// <summary>A failed host resource request, including failures caught by JavaScript.
/// A request can subsequently recover (for example from cache); this is not a terminal runtime failure.
/// URLs exclude user information, query strings and fragments. HTTP status is null when unavailable.</summary>
public sealed record WebSceneResourceFailure(
    string Url, string Method, string ResourceType, string ErrorCode,
    int? HttpStatus, TimeSpan Duration, string Message, WebSceneDiagnosticContext Context);

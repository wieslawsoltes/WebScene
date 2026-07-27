namespace JavaScript.Avalonia;

/// <summary>
/// Exposes the opaque engine-owned object behind a backend-neutral synthetic
/// event without coupling an engine adapter to a particular DOM assembly.
/// </summary>
public interface IExternalSyntheticEventSource
{
    object? SourceEvent { get; }
}

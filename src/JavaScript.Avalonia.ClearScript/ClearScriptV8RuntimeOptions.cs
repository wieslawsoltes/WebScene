namespace JavaScript.Avalonia.ClearScript;

/// <summary>
/// Configures the ClearScript V8 runtime that executes JavaScript against the
/// engine-neutral WebScene browser host.
/// </summary>
public sealed class ClearScriptV8RuntimeOptions
{
    /// <summary>
    /// Shares immutable source text and V8 compilation cache data across isolated
    /// runtimes. Mutable JavaScript and DOM state always remains runtime-local.
    /// Set to <see langword="null"/> only for diagnostic comparison.
    /// </summary>
    public ClearScriptV8SharedCache? SharedCache { get; set; } = ClearScriptV8SharedCache.ProcessWide;

    public bool EnableCanvasBatching { get; set; } = true;

    public bool EnableDomMethodCaching { get; set; } = true;

    /// <summary>
    /// Avoids crossing the host boundary for DOMTokenList writes whose result is
    /// already known from the facade's current token snapshot. Other class
    /// mutation paths invalidate the snapshot before it is reused.
    /// </summary>
    public bool EnableDomTokenListWriteShadow { get; set; } = true;

    /// <summary>
    /// Reuses the JavaScript facade and resolved string values for an immutable
    /// computed-style snapshot. A new facade is created after style or layout
    /// invalidation produces a new backend snapshot.
    /// </summary>
    public bool EnableComputedStyleReadCaching { get; set; } = true;

    /// <summary>
    /// Routes Document.getComputedStyle through the typed computed-style facade
    /// without running the generic host-result classifier first.
    /// </summary>
    public bool EnableTypedComputedStyleAccess { get; set; } = true;

    /// <summary>
    /// Routes changed inline-style writes through the typed CSS declaration
    /// contract. The declaration still performs the normal mutation observer,
    /// cascade invalidation, layout, and presentation work.
    /// </summary>
    public bool EnableTypedInlineStyleWrites { get; set; } = true;

    public bool EnableCanvasStateDeduplication { get; set; } = true;

    /// <summary>
    /// Delivers ResizeObserver notifications from Avalonia's completed size
    /// changes. Disable only to compare against the timer-polled fallback.
    /// </summary>
    public bool EnableNativeResizeObserverNotifications { get; set; } = true;

    /// <summary>
    /// Allows owner and virtual-iframe contexts created by this dedicated runtime
    /// to exchange direct V8 objects. This requires the matching ClearScript native
    /// context-group support; it is disabled by default.
    /// </summary>
    public bool EnableTrustedSameOriginContextSharing { get; set; }
}

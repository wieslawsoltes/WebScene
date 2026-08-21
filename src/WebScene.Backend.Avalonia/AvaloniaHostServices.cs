using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net;
using System.Net.Http.Headers;
using System.Runtime.ExceptionServices;
using System.Text;
using System.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Platform;
using Avalonia.Threading;
using WebScene.Core;

namespace WebScene.Backends.Avalonia;

/// <summary>
/// The Avalonia-specific implementation of the portable host-service seam. Existing
/// DOM presentation remains in this package while scheduling, frames, viewport,
/// resources, clipboard and normalized input are reached through these contracts.
/// </summary>
internal sealed class AvaloniaHostServices : IWebSceneHostServices, IDisposable
{
    private readonly TopLevel _topLevel;
    private readonly AvaloniaViewport _viewport;

    public AvaloniaHostServices(TopLevel topLevel)
    {
        _topLevel = topLevel ?? throw new ArgumentNullException(nameof(topLevel));
        RootHandle = WebSceneBackendHandle.Create(topLevel);
        Dispatcher = new AvaloniaWebSceneDispatcher();
        Clock = new StopwatchClock();
        Frames = new AvaloniaFrameScheduler(topLevel);
        _viewport = new AvaloniaViewport(topLevel);
        Viewport = _viewport;
        ResourceLoader = new AvaloniaResourceLoader();
        Resources = ResourceLoader;
        Clipboard = new AvaloniaClipboard(topLevel);
        InputSource = new AvaloniaInputSource(topLevel);
        Input = InputSource;
    }

    public WebSceneBackendHandle RootHandle { get; }

    internal TopLevel TopLevel => _topLevel;

    public IWebSceneDispatcher Dispatcher { get; }

    public IWebSceneClock Clock { get; }

    public IWebSceneFrameScheduler Frames { get; }

    public IWebSceneViewport Viewport { get; }

    public IWebSceneResourceLoader Resources { get; }

    public IWebSceneClipboard Clipboard { get; }

    public IWebSceneInputSource Input { get; }

    internal AvaloniaResourceLoader ResourceLoader { get; }

    internal AvaloniaInputSource InputSource { get; }

    internal void SetDocumentViewportProvider(Func<Control?> provider)
        => _viewport.SetDocumentViewportProvider(provider);

    public void Dispose()
    {
        InputSource.Dispose();
        _viewport.Dispose();
        if (Frames is IDisposable disposableFrames)
        {
            disposableFrames.Dispose();
        }
    }

    private sealed class StopwatchClock : IWebSceneClock
    {
        private readonly Stopwatch _stopwatch = Stopwatch.StartNew();

        public TimeSpan Elapsed => _stopwatch.Elapsed;
    }
}

internal sealed class AvaloniaWebSceneDispatcher : IWebSceneDispatcher
{
    public bool CheckAccess() => Dispatcher.UIThread.CheckAccess();

    public void VerifyAccess() => Dispatcher.UIThread.VerifyAccess();

    public void Post(Action callback, WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default)
    {
        ArgumentNullException.ThrowIfNull(callback);
        Dispatcher.UIThread.Post(callback, MapPriority(priority));
    }

    public IWebSceneScheduledWork Schedule(
        TimeSpan delay,
        Action callback,
        WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default)
    {
        ArgumentNullException.ThrowIfNull(callback);
        return new AvaloniaScheduledWork(delay, callback, MapPriority(priority));
    }

    internal static DispatcherPriority MapPriority(WebSceneDispatchPriority priority)
        => priority switch
        {
            WebSceneDispatchPriority.Send => DispatcherPriority.Send,
            WebSceneDispatchPriority.Input => DispatcherPriority.Input,
            WebSceneDispatchPriority.Render => DispatcherPriority.Render,
            WebSceneDispatchPriority.Background => DispatcherPriority.Background,
            _ => DispatcherPriority.Default
        };

    private sealed class AvaloniaScheduledWork : IWebSceneScheduledWork
    {
        private readonly DispatcherTimer _timer;
        private bool _disposed;

        public AvaloniaScheduledWork(TimeSpan delay, Action callback, DispatcherPriority priority)
        {
            _timer = new DispatcherTimer(priority)
            {
                Interval = delay >= TimeSpan.Zero ? delay : TimeSpan.Zero
            };
            _timer.Tick += OnTick;
            _timer.Start();
            return;

            void OnTick(object? sender, EventArgs args)
            {
                if (_disposed)
                {
                    return;
                }

                _timer.Stop();
                _timer.Tick -= OnTick;
                _disposed = true;
                callback();
            }
        }

        public bool IsCancellationRequested { get; private set; }

        public void Cancel()
        {
            if (_disposed)
            {
                return;
            }

            IsCancellationRequested = true;
            _disposed = true;
            _timer.Stop();
        }

        public void Dispose() => Cancel();
    }
}

internal sealed class AvaloniaFrameScheduler : IWebSceneFrameScheduler, IDisposable
{
    private readonly TopLevel _topLevel;
    private readonly SortedDictionary<long, Action<TimeSpan>> _pending = new();
    private long _sequence;
    private bool _frameScheduled;
    private bool _disposed;

    public AvaloniaFrameScheduler(TopLevel topLevel) => _topLevel = topLevel;

    public WebSceneFrameRequest RequestFrame(Action<TimeSpan> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        ObjectDisposedException.ThrowIf(_disposed, this);
        var id = Interlocked.Increment(ref _sequence);
        _pending.Add(id, callback);
        EnsureFrameScheduled();
        return new WebSceneFrameRequest(id);
    }

    private void EnsureFrameScheduled()
    {
        if (_frameScheduled || _disposed || _pending.Count == 0)
        {
            return;
        }
        _frameScheduled = true;
        _topLevel.RequestAnimationFrame(timestamp =>
        {
            _frameScheduled = false;
            if (_disposed || _pending.Count == 0)
            {
                return;
            }

            // A browser invokes all callbacks queued for one rendering
            // opportunity with the same timestamp. Coalescing here avoids a
            // separate Avalonia frame request for CSS transitions, JavaScript
            // requestAnimationFrame, and canvas work that are due together.
            var callbacks = _pending.Values.ToArray();
            _pending.Clear();
            ExceptionDispatchInfo? firstFailure = null;
            foreach (var callback in callbacks)
            {
                try
                {
                    callback(timestamp);
                }
                catch (Exception exception)
                {
                    // One animation callback must not prevent the remaining
                    // callbacks for the same rendering opportunity. Preserve
                    // the first failure after the batch has been delivered.
                    firstFailure ??= ExceptionDispatchInfo.Capture(exception);
                }
            }
            firstFailure?.Throw();
        });
    }

    public bool CancelFrame(WebSceneFrameRequest request) => _pending.Remove(request.Value);

    public void Dispose()
    {
        _disposed = true;
        _frameScheduled = false;
        _pending.Clear();
    }
}

internal sealed class AvaloniaViewport : IWebSceneViewport, IDisposable
{
    private readonly TopLevel _topLevel;
    private Func<Control?>? _documentViewportProvider;
    private WebSceneViewportMetrics _metrics;

    public AvaloniaViewport(TopLevel topLevel)
    {
        _topLevel = topLevel;
        _metrics = ReadMetrics();
        _topLevel.PropertyChanged += OnPropertyChanged;
        _topLevel.LayoutUpdated += OnLayoutUpdated;
    }

    public WebSceneViewportMetrics Metrics => ReadMetrics();

    public WebSceneViewportMetrics HostMetrics => ReadHostMetrics();

    public event EventHandler<WebSceneViewportChangedEventArgs>? Changed;

    public void SetDocumentViewportProvider(Func<Control?> provider)
        => _documentViewportProvider = provider ?? throw new ArgumentNullException(nameof(provider));

    private WebSceneViewportMetrics ReadMetrics()
    {
        var viewport = _documentViewportProvider?.Invoke();
        var size = viewport is not null && viewport.Bounds.Width > 0 && viewport.Bounds.Height > 0
            ? viewport.Bounds.Size
            : _topLevel.ClientSize.Width > 0 && _topLevel.ClientSize.Height > 0
                ? _topLevel.ClientSize
                : _topLevel.Bounds.Size;
        return new WebSceneViewportMetrics(
            new WebSceneSize(size.Width, size.Height),
            Math.Max(1, _topLevel.RenderScaling),
            _topLevel.IsVisible);
    }

    private WebSceneViewportMetrics ReadHostMetrics()
    {
        var size = _topLevel.ClientSize.Width > 0 && _topLevel.ClientSize.Height > 0
            ? _topLevel.ClientSize
            : _topLevel.Bounds.Size;
        return new WebSceneViewportMetrics(
            new WebSceneSize(size.Width, size.Height),
            Math.Max(1, _topLevel.RenderScaling),
            _topLevel.IsVisible);
    }

    private void OnPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs args) => PublishIfChanged();

    private void OnLayoutUpdated(object? sender, EventArgs args) => PublishIfChanged();

    private void PublishIfChanged()
    {
        var current = ReadMetrics();
        if (current == _metrics)
        {
            return;
        }

        var previous = _metrics;
        _metrics = current;
        Changed?.Invoke(this, new WebSceneViewportChangedEventArgs(previous, current));
    }

    public void Dispose()
    {
        _topLevel.PropertyChanged -= OnPropertyChanged;
        _topLevel.LayoutUpdated -= OnLayoutUpdated;
        Changed = null;
    }
}

/// <summary>
/// Loads WebScene text resources from files, Avalonia assets, data URIs, or HTTP(S).
/// Runtime adapters can share this service so resource policy stays in WebScene
/// instead of being reimplemented by individual samples and host applications.
/// </summary>
public sealed class AvaloniaResourceLoader : IWebSceneResourceLoader
{
    private static readonly HttpClient s_httpClient = new();
    private readonly List<string> _resourceSearchDirectories = new();
    private readonly List<MountedResourceDirectory> _mountedDirectories = new();
    private readonly object _archiveGate = new();
    private AvaloniaResourceArchive? _captureArchive;
    private AvaloniaResourceArchive? _replayArchive;
    private Exception? _resourceReplayFailure;

    public string ScriptBaseDirectory { get; set; } = AppContext.BaseDirectory;

    /// <summary>
    /// Gets the directory to which resolved HTTP(S) text and binary responses
    /// are captured. Call <see cref="FlushResourceCapture"/> after the final
    /// resource has loaded.
    /// </summary>
    public string? ResourceCaptureDirectory { get; init; }

    /// <summary>
    /// Gets an existing deterministic HTTP(S) resource archive. Missing
    /// resources fail closed and never fall back to the network.
    /// </summary>
    public string? ResourceReplayDirectory { get; init; }

    public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
    {
        if (string.IsNullOrWhiteSpace(request.Specifier))
        {
            throw new ArgumentException("A resource URI is required.", nameof(request));
        }

        var specifier = request.Specifier;
        if (specifier.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
        {
            return new WebSceneTextResource(specifier, DecodeDataUri(specifier), specifier, null);
        }

        var resolved = ResolveAddress(specifier, request.BaseAddress);
        if (resolved.IsFile)
        {
            return LoadFile(resolved.LocalPath);
        }

        if (resolved.Scheme.Equals("avares", StringComparison.OrdinalIgnoreCase))
        {
            using var stream = AssetLoader.Open(resolved);
            using var reader = new StreamReader(stream);
            return new WebSceneTextResource(resolved.ToString(), reader.ReadToEnd(), resolved.ToString(), null);
        }

        if (resolved.Scheme is "http" or "https")
        {
            if (GetReplayArchiveTracked() is { } replayArchive)
            {
                try
                {
                    return replayArchive.ReplayText(resolved, request.Kind);
                }
                catch (Exception error)
                {
                    RememberReplayFailure(error);
                    throw;
                }
            }

            WebSceneTextResource resource;
            if (TryResolveMountedResource(resolved, out var mountedPath))
            {
                resource = LoadFile(mountedPath);
            }
            else if (TryResolvePackagedResource(resolved.AbsolutePath, out var packagedPath))
            {
                resource = LoadFile(packagedPath);
            }
            else
            {
                using var message = new HttpRequestMessage(HttpMethod.Get, resolved);
                message.Version = HttpVersion.Version20;
                message.VersionPolicy = HttpVersionPolicy.RequestVersionOrLower;
                if (!string.IsNullOrWhiteSpace(request.IfNoneMatch)
                    && EntityTagHeaderValue.TryParse(request.IfNoneMatch, out var entityTag))
                {
                    message.Headers.IfNoneMatch.Add(entityTag);
                }
                if (request.IfModifiedSince is { } modifiedSince)
                {
                    message.Headers.IfModifiedSince = modifiedSince;
                }
                using var response = s_httpClient.SendAsync(
                        message,
                        HttpCompletionOption.ResponseHeadersRead)
                    .GetAwaiter()
                    .GetResult();
                var responseEntityTag = response.Headers.ETag?.ToString() ?? request.IfNoneMatch;
                var responseLastModified = response.Content.Headers.LastModified
                                           ?? request.IfModifiedSince;
                var cachePolicy = ReadHttpCachePolicy(response, responseLastModified);
                if (response.StatusCode == HttpStatusCode.NotModified)
                {
                    resource = new WebSceneTextResource(
                        resolved.ToString(),
                        string.Empty,
                        resolved.ToString(),
                        null)
                    {
                        EntityTag = responseEntityTag,
                        LastModified = responseLastModified,
                        FreshUntil = cachePolicy.FreshUntil,
                        IsCacheable = cachePolicy.IsCacheable,
                        NotModified = true
                    };
                }
                else
                {
                    response.EnsureSuccessStatusCode();
                    resource = new WebSceneTextResource(
                        resolved.ToString(),
                        response.Content.ReadAsStringAsync().GetAwaiter().GetResult(),
                        resolved.ToString(),
                        null)
                    {
                        EntityTag = responseEntityTag,
                        LastModified = responseLastModified,
                        FreshUntil = cachePolicy.FreshUntil,
                        IsCacheable = cachePolicy.IsCacheable
                    };
                }
            }

            GetCaptureArchive()?.CaptureText(resolved, request.Kind, resource);
            return resource;
        }

        throw new NotSupportedException($"Unsupported resource scheme '{resolved.Scheme}'.");
    }

    internal bool TryLoadUtf8(
        in WebSceneResourceRequest request,
        out AvaloniaUtf8Resource resource)
    {
        resource = default;
        if (string.IsNullOrWhiteSpace(request.Specifier)
            || request.Specifier.StartsWith("data:", StringComparison.OrdinalIgnoreCase)
            || string.IsNullOrWhiteSpace(ResourceReplayDirectory))
        {
            return false;
        }

        var resolved = ResolveAddress(request.Specifier, request.BaseAddress);
        if (resolved.Scheme is not ("http" or "https")
            || GetReplayArchiveTracked() is not { } replayArchive)
        {
            return false;
        }

        try
        {
            resource = replayArchive.ReplayUtf8Text(resolved, request.Kind);
            return true;
        }
        catch (Exception error)
        {
            RememberReplayFailure(error);
            throw;
        }
    }

    internal static (DateTimeOffset? FreshUntil, bool IsCacheable) ReadHttpCachePolicy(
        HttpResponseMessage response,
        DateTimeOffset? lastModified)
    {
        var cacheControl = response.Headers.CacheControl;
        if (cacheControl?.NoStore == true)
        {
            return (null, false);
        }
        if (cacheControl?.NoCache == true)
        {
            return (null, true);
        }

        var receivedAt = DateTimeOffset.UtcNow;
        var responseDate = response.Headers.Date ?? receivedAt;
        var responseAge = response.Headers.Age ?? TimeSpan.Zero;
        var apparentAge = receivedAt > responseDate ? receivedAt - responseDate : TimeSpan.Zero;
        var currentAge = responseAge > apparentAge ? responseAge : apparentAge;
        TimeSpan? freshnessLifetime = cacheControl?.MaxAge;
        if (freshnessLifetime is null && response.Content.Headers.Expires is { } expires)
        {
            freshnessLifetime = expires - responseDate;
        }
        if (freshnessLifetime is null
            && lastModified is { } modified
            && responseDate > modified)
        {
            // RFC HTTP caches may use heuristic freshness when an origin sends
            // validators but no explicit lifetime. Ten percent of the resource
            // age is conventional; the one-hour cap keeps unversioned documents
            // responsive to origin updates while hashed bundles remain instant
            // across normal application restarts.
            var heuristic = TimeSpan.FromTicks((responseDate - modified).Ticks / 10);
            freshnessLifetime = TimeSpan.FromTicks(Math.Clamp(
                heuristic.Ticks,
                TimeSpan.FromMinutes(1).Ticks,
                TimeSpan.FromHours(1).Ticks));
        }
        if (freshnessLifetime is not { } lifetime || lifetime <= currentAge)
        {
            return (null, true);
        }
        return (receivedAt + lifetime - currentAge, true);
    }

    internal async Task<AvaloniaBinaryResource> LoadBytesAsync(
        string specifier,
        string? baseAddress,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(specifier))
        {
            throw new ArgumentException("A resource URI is required.", nameof(specifier));
        }

        if (specifier.StartsWith("data:", StringComparison.OrdinalIgnoreCase))
        {
            return new AvaloniaBinaryResource(specifier, DecodeBinaryDataUri(specifier), specifier);
        }

        var resolved = ResolveAddress(specifier, baseAddress);
        if (resolved.IsFile)
        {
            var fullPath = Path.GetFullPath(resolved.LocalPath);
            if (!File.Exists(fullPath))
            {
                throw new FileNotFoundException($"Resource file '{resolved.LocalPath}' not found.", fullPath);
            }
            return new AvaloniaBinaryResource(
                fullPath,
                await File.ReadAllBytesAsync(fullPath, cancellationToken).ConfigureAwait(false),
                new Uri(fullPath).AbsoluteUri);
        }

        if (resolved.Scheme.Equals("avares", StringComparison.OrdinalIgnoreCase))
        {
            await using var stream = AssetLoader.Open(resolved);
            using var memory = new MemoryStream();
            await stream.CopyToAsync(memory, cancellationToken).ConfigureAwait(false);
            return new AvaloniaBinaryResource(resolved.ToString(), memory.ToArray(), resolved.ToString());
        }

        if (resolved.Scheme is "http" or "https")
        {
            if (GetReplayArchiveTracked() is { } replayArchive)
            {
                try
                {
                    return replayArchive.ReplayBinary(resolved);
                }
                catch (Exception error)
                {
                    RememberReplayFailure(error);
                    throw;
                }
            }

            AvaloniaBinaryResource resource;
            if (TryResolveMountedResource(resolved, out var mountedPath))
            {
                resource = new AvaloniaBinaryResource(
                    mountedPath,
                    await File.ReadAllBytesAsync(mountedPath, cancellationToken).ConfigureAwait(false),
                    new Uri(mountedPath).AbsoluteUri);
            }
            else if (TryResolvePackagedResource(resolved.AbsolutePath, out var packagedPath))
            {
                resource = new AvaloniaBinaryResource(
                    packagedPath,
                    await File.ReadAllBytesAsync(packagedPath, cancellationToken).ConfigureAwait(false),
                    new Uri(packagedPath).AbsoluteUri);
            }
            else
            {
                resource = new AvaloniaBinaryResource(
                    resolved.ToString(),
                    await s_httpClient.GetByteArrayAsync(resolved, cancellationToken).ConfigureAwait(false),
                    resolved.ToString());
            }

            GetCaptureArchive()?.CaptureBinary(resolved, resource);
            return resource;
        }

        throw new NotSupportedException($"Unsupported resource scheme '{resolved.Scheme}'.");
    }

    public void ClearSearchDirectories() => _resourceSearchDirectories.Clear();

    /// <summary>Writes a pending deterministic resource-capture manifest.</summary>
    public void FlushResourceCapture()
    {
        lock (_archiveGate)
        {
            _captureArchive?.Flush();
        }
    }

    /// <summary>
    /// Throws when the native callback boundary observed an archive miss or
    /// corrupt replay object. Native resource callbacks cannot propagate
    /// managed exceptions directly, so deterministic benchmark hosts should
    /// call this while waiting for readiness and once after the readiness gate.
    /// </summary>
    public void ThrowIfResourceReplayFailed()
    {
        if (Volatile.Read(ref _resourceReplayFailure) is not { } error)
        {
            return;
        }

        throw new InvalidOperationException(
            "Deterministic WebScene resource replay failed. The origin was not contacted.",
            error);
    }

    /// <summary>
    /// Validates and preloads every replay object. Benchmark hosts can call
    /// this before starting their navigation timer, matching a browser whose
    /// interception fixture is already resident in memory.
    /// </summary>
    public void PrepareResourceReplay()
    {
        try
        {
            GetReplayArchiveTracked()?.Preload();
        }
        catch (Exception error)
        {
            RememberReplayFailure(error);
            throw;
        }
    }

    public void MountDirectory(string addressPrefix, string directory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(addressPrefix);
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);

        var prefix = new Uri(
            addressPrefix.EndsWith("/", StringComparison.Ordinal)
                ? addressPrefix
                : addressPrefix + '/',
            UriKind.Absolute);
        if (prefix.Scheme is not ("http" or "https"))
        {
            throw new ArgumentException(
                "Mounted resource prefixes must use http or https.",
                nameof(addressPrefix));
        }

        var root = Path.GetFullPath(directory);
        if (!Directory.Exists(root))
        {
            throw new DirectoryNotFoundException(
                $"Mounted resource directory '{root}' does not exist.");
        }

        _mountedDirectories.RemoveAll(mount => mount.Prefix == prefix);
        _mountedDirectories.Add(new MountedResourceDirectory(prefix, root));
    }

    private Uri ResolveAddress(string specifier, string? baseAddress)
    {
        if (Uri.TryCreate(specifier, UriKind.Absolute, out var absolute))
        {
            return absolute;
        }

        if (!string.IsNullOrWhiteSpace(baseAddress))
        {
            if (Path.IsPathRooted(baseAddress))
            {
                var rootedBase = Path.GetFullPath(baseAddress);
                var rootedDirectory = Path.HasExtension(rootedBase) ? Path.GetDirectoryName(rootedBase)! : rootedBase;
                return new Uri(Path.GetFullPath(Path.Combine(rootedDirectory, specifier)));
            }

            if (Uri.TryCreate(baseAddress, UriKind.Absolute, out var absoluteBase))
            {
                return new Uri(absoluteBase, specifier);
            }

            var basePath = Path.GetFullPath(Path.Combine(ScriptBaseDirectory, baseAddress));
            var baseDirectory = Path.HasExtension(basePath) ? Path.GetDirectoryName(basePath)! : basePath;
            return new Uri(Path.GetFullPath(Path.Combine(baseDirectory, specifier)));
        }

        return new Uri(Path.GetFullPath(Path.Combine(ScriptBaseDirectory, specifier)));
    }

    private WebSceneTextResource LoadFile(string path)
    {
        var fullPath = Path.GetFullPath(path);
        if (!File.Exists(fullPath) && string.IsNullOrEmpty(Path.GetExtension(fullPath)) && File.Exists(fullPath + ".js"))
        {
            fullPath += ".js";
        }

        if (!File.Exists(fullPath) && TryResolvePackagedResource(path, out var packagedPath))
        {
            fullPath = packagedPath;
        }

        if (!File.Exists(fullPath))
        {
            throw new FileNotFoundException($"Resource file '{path}' not found.", fullPath);
        }

        var directory = Path.GetDirectoryName(fullPath) ?? ScriptBaseDirectory;
        if (!_resourceSearchDirectories.Contains(directory, StringComparer.OrdinalIgnoreCase))
        {
            _resourceSearchDirectories.Add(directory);
        }

        return new WebSceneTextResource(fullPath, File.ReadAllText(fullPath), fullPath, directory);
    }

    private bool TryResolvePackagedResource(string resourcePath, out string fullPath)
    {
        var relativePath = Uri.UnescapeDataString(resourcePath).TrimStart('/', '\\').Replace('/', Path.DirectorySeparatorChar);
        var fileName = Path.GetFileName(relativePath);
        for (var index = _resourceSearchDirectories.Count - 1; index >= 0; index--)
        {
            foreach (var candidate in new[]
                     {
                         Path.Combine(_resourceSearchDirectories[index], relativePath),
                         Path.Combine(_resourceSearchDirectories[index], fileName)
                     })
            {
                if (!File.Exists(candidate))
                {
                    continue;
                }

                fullPath = Path.GetFullPath(candidate);
                return true;
            }
        }

        fullPath = string.Empty;
        return false;
    }

    private bool TryResolveMountedResource(Uri address, out string fullPath)
    {
        for (var index = _mountedDirectories.Count - 1; index >= 0; index--)
        {
            var mount = _mountedDirectories[index];
            if (!mount.Prefix.IsBaseOf(address))
            {
                continue;
            }

            var relative = Uri.UnescapeDataString(mount.Prefix.MakeRelativeUri(address).ToString())
                .Replace('/', Path.DirectorySeparatorChar);
            var candidate = Path.GetFullPath(Path.Combine(mount.Directory, relative));
            var rootPrefix = mount.Directory.EndsWith(Path.DirectorySeparatorChar)
                ? mount.Directory
                : mount.Directory + Path.DirectorySeparatorChar;
            if (!candidate.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase)
                || !File.Exists(candidate))
            {
                continue;
            }

            fullPath = candidate;
            return true;
        }

        fullPath = string.Empty;
        return false;
    }

    private sealed record MountedResourceDirectory(Uri Prefix, string Directory);

    private AvaloniaResourceArchive? GetCaptureArchive()
    {
        ValidateArchiveConfiguration();
        if (string.IsNullOrWhiteSpace(ResourceCaptureDirectory))
        {
            return null;
        }

        lock (_archiveGate)
        {
            return _captureArchive ??= AvaloniaResourceArchive.CreateCapture(ResourceCaptureDirectory);
        }
    }

    private AvaloniaResourceArchive? GetReplayArchive()
    {
        ValidateArchiveConfiguration();
        if (string.IsNullOrWhiteSpace(ResourceReplayDirectory))
        {
            return null;
        }

        lock (_archiveGate)
        {
            return _replayArchive ??= AvaloniaResourceArchive.OpenReplay(ResourceReplayDirectory);
        }
    }

    private AvaloniaResourceArchive? GetReplayArchiveTracked()
    {
        try
        {
            return GetReplayArchive();
        }
        catch (Exception error)
        {
            RememberReplayFailure(error);
            throw;
        }
    }

    private void ValidateArchiveConfiguration()
    {
        if (!string.IsNullOrWhiteSpace(ResourceCaptureDirectory)
            && !string.IsNullOrWhiteSpace(ResourceReplayDirectory))
        {
            throw new InvalidOperationException(
                "Resource capture and replay directories are mutually exclusive.");
        }
    }

    private void RememberReplayFailure(Exception error)
        => Interlocked.CompareExchange(ref _resourceReplayFailure, error, null);

    private static string DecodeDataUri(string value)
    {
        var comma = value.IndexOf(',');
        if (comma < 0)
        {
            throw new FormatException("Invalid data URI.");
        }

        var metadata = value[..comma];
        var payload = value[(comma + 1)..];
        return metadata.EndsWith(";base64", StringComparison.OrdinalIgnoreCase)
            ? Encoding.UTF8.GetString(Convert.FromBase64String(payload))
            : Uri.UnescapeDataString(payload);
    }

    private static byte[] DecodeBinaryDataUri(string value)
    {
        var comma = value.IndexOf(',');
        if (comma < 0)
        {
            throw new FormatException("Invalid data URI.");
        }

        var metadata = value[..comma];
        var payload = value[(comma + 1)..];
        return metadata.EndsWith(";base64", StringComparison.OrdinalIgnoreCase)
            ? Convert.FromBase64String(payload)
            : Encoding.UTF8.GetBytes(Uri.UnescapeDataString(payload));
    }
}

internal readonly record struct AvaloniaBinaryResource(string CacheKey, byte[] Content, string DisplayName);

internal sealed class AvaloniaClipboard : IWebSceneClipboard
{
    private readonly TopLevel _topLevel;
    private readonly Dictionary<string, byte[]> _lastData = new(StringComparer.OrdinalIgnoreCase);
    private string? _lastText;

    public AvaloniaClipboard(TopLevel topLevel) => _topLevel = topLevel;

    public string? GetText()
    {
        try
        {
            return _topLevel.Clipboard?.GetTextAsync().GetAwaiter().GetResult() ?? _lastText;
        }
        catch
        {
            return _lastText;
        }
    }

    public void SetText(string? text)
    {
        _lastText = text ?? string.Empty;
        try
        {
            _topLevel.Clipboard?.SetTextAsync(_lastText).GetAwaiter().GetResult();
        }
        catch
        {
        }
    }

    public byte[]? GetData(string format)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(format);
        return _lastData.TryGetValue(format, out var data) ? data.ToArray() : null;
    }

    public void SetData(string format, ReadOnlyMemory<byte> data)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(format);
        var bytes = data.ToArray();
        _lastData[format] = bytes;

        try
        {
            var clipboardData = new DataObject();
            clipboardData.Set(format, bytes);
            if (format.Equals("image/png", StringComparison.OrdinalIgnoreCase))
            {
                // Native pasteboards use different conventional identifiers;
                // publish the MIME, macOS UTI and registered Windows name.
                clipboardData.Set("public.png", bytes);
                clipboardData.Set("PNG", bytes);
            }
            _topLevel.Clipboard?.SetDataObjectAsync(clipboardData).GetAwaiter().GetResult();
        }
        catch
        {
            // Retain the in-process value when a platform clipboard is absent
            // (notably headless tests) or rejects a custom MIME type.
        }
    }
}

internal sealed class AvaloniaInputSource : IWebSceneInputSource, IDisposable
{
    private readonly TopLevel _topLevel;
    private bool _attached;
    private EventHandler<WebScenePointerInputEventArgs>? _pointer;
    private EventHandler<WebSceneKeyboardInputEventArgs>? _keyboard;
    private EventHandler<WebSceneTextInputEventArgs>? _textInput;

    public AvaloniaInputSource(TopLevel topLevel) => _topLevel = topLevel;

    public event EventHandler<WebScenePointerInputEventArgs>? Pointer
    {
        add { _pointer += value; EnsureAttached(); }
        remove { _pointer -= value; }
    }

    public event EventHandler<WebSceneKeyboardInputEventArgs>? Keyboard
    {
        add { _keyboard += value; EnsureAttached(); }
        remove { _keyboard -= value; }
    }

    public event EventHandler<WebSceneTextInputEventArgs>? TextInput
    {
        add { _textInput += value; EnsureAttached(); }
        remove { _textInput -= value; }
    }

    private void EnsureAttached()
    {
        if (_attached || _topLevel is not InputElement input)
        {
            return;
        }

        _attached = true;
        input.AddHandler(InputElement.PointerPressedEvent, OnPointerPressed, handledEventsToo: true);
        input.AddHandler(InputElement.PointerMovedEvent, OnPointerMoved, handledEventsToo: true);
        input.AddHandler(InputElement.PointerReleasedEvent, OnPointerReleased, handledEventsToo: true);
        input.AddHandler(InputElement.PointerWheelChangedEvent, OnPointerWheel, handledEventsToo: true);
        input.AddHandler(InputElement.KeyDownEvent, OnKeyDown, handledEventsToo: true);
        input.AddHandler(InputElement.KeyUpEvent, OnKeyUp, handledEventsToo: true);
        input.AddHandler(InputElement.TextInputEvent, OnTextInput, handledEventsToo: true);
    }

    private void OnPointerPressed(object? sender, PointerPressedEventArgs args)
        => PublishPointer(args, WebScenePointerEventKind.Pressed);

    private void OnPointerMoved(object? sender, PointerEventArgs args)
        => PublishPointer(args, WebScenePointerEventKind.Moved);

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs args)
        => PublishPointer(args, WebScenePointerEventKind.Released);

    private void OnPointerWheel(object? sender, PointerWheelEventArgs args)
        => PublishPointer(args, WebScenePointerEventKind.Wheel, args.Delta);

    private void PublishPointer(PointerEventArgs args, WebScenePointerEventKind kind, Vector delta = default)
    {
        var current = args.GetCurrentPoint(_topLevel);
        var properties = current.Properties;
        var modifiers = args.KeyModifiers;
        var normalized = new WebScenePointerInputEventArgs
        {
            Kind = kind,
            PointerType = args.Pointer.Type switch
            {
                PointerType.Mouse => WebScenePointerType.Mouse,
                PointerType.Touch => WebScenePointerType.Touch,
                PointerType.Pen => WebScenePointerType.Pen,
                _ => WebScenePointerType.Unknown
            },
            PointerId = args.Pointer.Id,
            Position = new WebScenePoint(current.Position.X, current.Position.Y),
            Delta = new WebScenePoint(delta.X, delta.Y),
            Button = properties.PointerUpdateKind switch
            {
                PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 0,
                PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 1,
                PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 2,
                _ => -1
            },
            Buttons = (properties.IsLeftButtonPressed ? 1 : 0)
                      | (properties.IsRightButtonPressed ? 2 : 0)
                      | (properties.IsMiddleButtonPressed ? 4 : 0),
            AltKey = modifiers.HasFlag(KeyModifiers.Alt),
            ControlKey = modifiers.HasFlag(KeyModifiers.Control),
            MetaKey = modifiers.HasFlag(KeyModifiers.Meta),
            ShiftKey = modifiers.HasFlag(KeyModifiers.Shift),
            SourceHandle = args.Source is { } source
                ? WebSceneBackendHandle.Create(source)
                : default,
            NativeEventHandle = WebSceneBackendHandle.Create(args)
        };
        _pointer?.Invoke(this, normalized);
        if (normalized.Handled)
        {
            args.Handled = true;
        }
    }

    private void OnKeyDown(object? sender, KeyEventArgs args) => PublishKeyboard(args, "keydown");

    private void OnKeyUp(object? sender, KeyEventArgs args) => PublishKeyboard(args, "keyup");

    private void PublishKeyboard(KeyEventArgs args, string type)
    {
        var modifiers = args.KeyModifiers;
        var normalized = new WebSceneKeyboardInputEventArgs
        {
            Type = type,
            Key = args.Key.ToString(),
            Code = args.PhysicalKey.ToString(),
            IsRepeat = false,
            AltKey = modifiers.HasFlag(KeyModifiers.Alt),
            ControlKey = modifiers.HasFlag(KeyModifiers.Control),
            MetaKey = modifiers.HasFlag(KeyModifiers.Meta),
            ShiftKey = modifiers.HasFlag(KeyModifiers.Shift),
            SourceHandle = args.Source is { } source
                ? WebSceneBackendHandle.Create(source)
                : default,
            NativeEventHandle = WebSceneBackendHandle.Create(args)
        };
        _keyboard?.Invoke(this, normalized);
        if (normalized.Handled)
        {
            args.Handled = true;
        }
    }

    private void OnTextInput(object? sender, TextInputEventArgs args)
    {
        var normalized = new WebSceneTextInputEventArgs
        {
            Text = args.Text ?? string.Empty,
            SourceHandle = args.Source is { } source
                ? WebSceneBackendHandle.Create(source)
                : default,
            NativeEventHandle = WebSceneBackendHandle.Create(args)
        };
        _textInput?.Invoke(this, normalized);
        if (normalized.Handled)
        {
            args.Handled = true;
        }
    }

    public void Dispose()
    {
        if (_attached && _topLevel is InputElement input)
        {
            input.RemoveHandler(InputElement.PointerPressedEvent, OnPointerPressed);
            input.RemoveHandler(InputElement.PointerMovedEvent, OnPointerMoved);
            input.RemoveHandler(InputElement.PointerReleasedEvent, OnPointerReleased);
            input.RemoveHandler(InputElement.PointerWheelChangedEvent, OnPointerWheel);
            input.RemoveHandler(InputElement.KeyDownEvent, OnKeyDown);
            input.RemoveHandler(InputElement.KeyUpEvent, OnKeyUp);
            input.RemoveHandler(InputElement.TextInputEvent, OnTextInput);
        }

        _attached = false;
        _pointer = null;
        _keyboard = null;
        _textInput = null;
    }
}

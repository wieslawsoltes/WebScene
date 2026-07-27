using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.Security.Cryptography;

namespace WebScene.Sdk;

public enum WebSceneComponentState
{
    Created,
    Mounted,
    Unmounted,
    Disposed
}

public enum WebSceneDiagnosticSeverity
{
    Info,
    Warning,
    Error
}

public readonly record struct WebSceneSdkDiagnostic(
    string Code,
    WebSceneDiagnosticSeverity Severity,
    string Message,
    string? ComponentId = null,
    DateTimeOffset? Timestamp = null);

public interface IWebSceneDiagnosticSink
{
    void Report(in WebSceneSdkDiagnostic diagnostic);
}

public sealed class WebSceneDiagnosticCollector : IWebSceneDiagnosticSink
{
    private readonly object _gate = new();
    private readonly List<WebSceneSdkDiagnostic> _diagnostics = [];

    public IReadOnlyList<WebSceneSdkDiagnostic> Diagnostics
    {
        get
        {
            lock (_gate)
            {
                return _diagnostics.ToArray();
            }
        }
    }

    public void Report(in WebSceneSdkDiagnostic diagnostic)
    {
        lock (_gate)
        {
            _diagnostics.Add(diagnostic with { Timestamp = diagnostic.Timestamp ?? DateTimeOffset.UtcNow });
        }
    }
}

public sealed record WebSceneCachedAsset(
    string ComponentId,
    string ComponentVersion,
    string Path,
    ReadOnlyMemory<byte> Content,
    string Sha256);

/// <summary>Process-wide immutable package bytes; component instance state never enters this cache.</summary>
public sealed class WebSceneSharedAssetCache
{
    private readonly ConcurrentDictionary<string, Lazy<WebSceneCachedAsset>> _assets = new(StringComparer.Ordinal);
    private long _hits;
    private long _misses;

    public long Hits => Interlocked.Read(ref _hits);

    public long Misses => Interlocked.Read(ref _misses);

    public int Count => _assets.Count;

    public WebSceneCachedAsset GetOrAdd(WebSceneComponentManifest manifest, string path, Func<byte[]> loader)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(loader);
        var key = string.Concat(manifest.Id, "@", manifest.Version, "/", path);
        var created = new Lazy<WebSceneCachedAsset>(
            () => CreateAsset(manifest, path, loader()),
            LazyThreadSafetyMode.ExecutionAndPublication);
        var value = _assets.GetOrAdd(key, created);
        if (ReferenceEquals(value, created))
        {
            Interlocked.Increment(ref _misses);
        }
        else
        {
            Interlocked.Increment(ref _hits);
        }
        return value.Value;
    }

    private static WebSceneCachedAsset CreateAsset(WebSceneComponentManifest manifest, string path, byte[] content)
    {
        ArgumentNullException.ThrowIfNull(content);
        var immutableCopy = content.ToArray();
        return new WebSceneCachedAsset(
            manifest.Id,
            manifest.Version,
            path,
            immutableCopy,
            Convert.ToHexString(SHA256.HashData(immutableCopy)).ToLowerInvariant());
    }
}

public sealed class WebSceneComponentPackage
{
    private readonly string _rootDirectory;
    private readonly WebSceneSharedAssetCache _cache;

    private WebSceneComponentPackage(
        string rootDirectory,
        WebSceneComponentManifest manifest,
        WebSceneSharedAssetCache cache)
    {
        _rootDirectory = rootDirectory;
        Manifest = manifest;
        _cache = cache;
    }

    public WebSceneComponentManifest Manifest { get; }

    public static WebSceneComponentPackage Open(
        string rootDirectory,
        WebSceneSharedAssetCache cache,
        string manifestFileName = "webscene-component.json")
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rootDirectory);
        ArgumentNullException.ThrowIfNull(cache);
        var root = Path.GetFullPath(rootDirectory);
        var manifestPath = Path.Combine(root, manifestFileName);
        using var stream = File.OpenRead(manifestPath);
        var manifest = WebSceneComponentManifestSerializer.Read(stream);
        foreach (var asset in manifest.Assets)
        {
            var path = ResolveContainedPath(root, asset);
            if (!File.Exists(path))
            {
                throw new FileNotFoundException($"Component asset '{asset}' does not exist.", path);
            }
        }
        return new WebSceneComponentPackage(root, manifest, cache);
    }

    public WebSceneCachedAsset GetAsset(string path)
    {
        if (!Manifest.Assets.Contains(path, StringComparer.Ordinal))
        {
            throw new FileNotFoundException($"Asset '{path}' is not declared by component '{Manifest.Id}'.");
        }
        var fullPath = ResolveContainedPath(_rootDirectory, path);
        return _cache.GetOrAdd(Manifest, path, () => File.ReadAllBytes(fullPath));
    }

    public WebSceneCachedAsset GetEntryPoint() => GetAsset(Manifest.EntryPoint);

    public WebSceneComponentInstance CreateInstance(IWebSceneDiagnosticSink? diagnostics = null)
        => new(this, diagnostics);

    private static string ResolveContainedPath(string root, string relativePath)
    {
        var fullPath = Path.GetFullPath(Path.Combine(root, relativePath));
        var prefix = root.EndsWith(Path.DirectorySeparatorChar) ? root : root + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(prefix, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Asset '{relativePath}' escapes the component package.");
        }
        return fullPath;
    }
}

public sealed class WebSceneComponentInstance : IDisposable
{
    private readonly Dictionary<string, object?> _state = new(StringComparer.Ordinal);
    private readonly IWebSceneDiagnosticSink? _diagnostics;

    internal WebSceneComponentInstance(WebSceneComponentPackage package, IWebSceneDiagnosticSink? diagnostics)
    {
        Package = package;
        _diagnostics = diagnostics;
        InstanceId = Guid.NewGuid();
    }

    public Guid InstanceId { get; }

    public WebSceneComponentPackage Package { get; }

    public WebSceneComponentState State { get; private set; }

    public IReadOnlyDictionary<string, object?> StateValues => new ReadOnlyDictionary<string, object?>(_state);

    public void Mount()
    {
        ObjectDisposedException.ThrowIf(State == WebSceneComponentState.Disposed, this);
        if (State == WebSceneComponentState.Mounted)
        {
            return;
        }
        State = WebSceneComponentState.Mounted;
        Report("component.mounted", WebSceneDiagnosticSeverity.Info, $"Mounted instance {InstanceId}.");
    }

    public void Unmount()
    {
        ObjectDisposedException.ThrowIf(State == WebSceneComponentState.Disposed, this);
        if (State != WebSceneComponentState.Mounted)
        {
            throw new InvalidOperationException($"Cannot unmount component in state '{State}'.");
        }
        State = WebSceneComponentState.Unmounted;
        Report("component.unmounted", WebSceneDiagnosticSeverity.Info, $"Unmounted instance {InstanceId}.");
    }

    public void SetState(string key, object? value)
    {
        ObjectDisposedException.ThrowIf(State == WebSceneComponentState.Disposed, this);
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        _state[key] = value;
    }

    public bool TryGetState<T>(string key, out T? value)
    {
        if (_state.TryGetValue(key, out var raw) && raw is T typed)
        {
            value = typed;
            return true;
        }
        value = default;
        return false;
    }

    public void Dispose()
    {
        if (State == WebSceneComponentState.Disposed)
        {
            return;
        }
        _state.Clear();
        State = WebSceneComponentState.Disposed;
        Report("component.disposed", WebSceneDiagnosticSeverity.Info, $"Disposed instance {InstanceId}.");
    }

    private void Report(string code, WebSceneDiagnosticSeverity severity, string message)
        => _diagnostics?.Report(new WebSceneSdkDiagnostic(code, severity, message, Package.Manifest.Id));
}

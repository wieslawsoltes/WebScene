using System.Buffers;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using WebScene.JavaScript;
using JavaScript.Avalonia;
using Microsoft.ClearScript.V8;

namespace JavaScript.Avalonia.ClearScript;

public readonly record struct V8SharedCacheMetrics(
    long SourceHits,
    long SourceMisses,
    long CodeHits,
    long CodeMisses,
    long CodeAccepted,
    long CodeVerified,
    long CodeUpdated,
    long CodeBytes,
    int SourceEntries,
    int CodeEntries,
    long DiskHits,
    long DiskMisses,
    long DiskInvalidEntries,
    long DiskWrites,
    long DiskBytesRead,
    long DiskBytesWritten,
    long CompilationLeaders,
    long CompilationWaiters)
{
    public static V8SharedCacheMetrics operator -(
        V8SharedCacheMetrics left,
        V8SharedCacheMetrics right)
        => new(
            left.SourceHits - right.SourceHits,
            left.SourceMisses - right.SourceMisses,
            left.CodeHits - right.CodeHits,
            left.CodeMisses - right.CodeMisses,
            left.CodeAccepted - right.CodeAccepted,
            left.CodeVerified - right.CodeVerified,
            left.CodeUpdated - right.CodeUpdated,
            left.CodeBytes - right.CodeBytes,
            left.SourceEntries - right.SourceEntries,
            left.CodeEntries - right.CodeEntries,
            left.DiskHits - right.DiskHits,
            left.DiskMisses - right.DiskMisses,
            left.DiskInvalidEntries - right.DiskInvalidEntries,
            left.DiskWrites - right.DiskWrites,
            left.DiskBytesRead - right.DiskBytesRead,
            left.DiskBytesWritten - right.DiskBytesWritten,
            left.CompilationLeaders - right.CompilationLeaders,
            left.CompilationWaiters - right.CompilationWaiters);
}

public sealed class ClearScriptV8SharedCacheOptions
{
    public int MaxSourceEntries { get; set; } = 512;

    public int MaxCodeEntries { get; set; } = 512;

    public long MaxCodeBytes { get; set; } = 128L * 1024 * 1024;

    /// <summary>
    /// Optional root for persistent V8 code-cache files. Each compatibility identity
    /// gets an isolated child directory. Source text and mutable runtime state are not
    /// persisted.
    /// </summary>
    public string? PersistentDirectory { get; set; }

    public int MaxPersistentEntries { get; set; } = 2048;

    public long MaxPersistentBytes { get; set; } = 512L * 1024 * 1024;

    /// <summary>
    /// Optional application/native-build identity added to the automatic ClearScript,
    /// RID, architecture, and cache-schema identity.
    /// </summary>
    public string? CompatibilityTag { get; set; }
}

public readonly record struct V8CompilationSource(string DocumentName, string Code);

public readonly record struct V8PrecompileResult(
    int Requested,
    int Compiled,
    int Reused,
    int WorkerThreadId,
    bool RanOnThreadPool);

/// <summary>
/// Bounded process-level cache for immutable script sources and V8 compilation data.
/// Module exports, globals, DOM objects, and other mutable chart state are never shared.
/// </summary>
public sealed class ClearScriptV8SharedCache : IWebSceneJavaScriptModuleCache
{
    private const int PersistentSchemaVersion = 1;
    private const int PersistentHeaderLength = 8 + sizeof(int) + SHA256.HashSizeInBytes * 2 + sizeof(int);
    private static readonly byte[] PersistentMagic = "HMLV8C01"u8.ToArray();

    private readonly object _gate = new();
    private readonly object[] _sourceKeyGates = CreateKeyGates();
    private readonly object[] _codeKeyGates = CreateKeyGates();
    private readonly Dictionary<string, SourceEntry> _sources = new(StringComparer.Ordinal);
    private readonly Queue<string> _sourceOrder = new();
    private readonly Dictionary<string, byte[]> _code = new(StringComparer.Ordinal);
    private readonly Queue<string> _codeOrder = new();
    private readonly int _maxSourceEntries;
    private readonly int _maxCodeEntries;
    private readonly long _maxCodeBytes;
    private readonly int _maxPersistentEntries;
    private readonly long _maxPersistentBytes;
    private readonly string? _persistentDirectory;
    private long _sourceHits;
    private long _sourceMisses;
    private long _codeHits;
    private long _codeMisses;
    private long _codeAccepted;
    private long _codeVerified;
    private long _codeUpdated;
    private long _codeBytes;
    private long _diskHits;
    private long _diskMisses;
    private long _diskInvalidEntries;
    private long _diskWrites;
    private long _diskBytesRead;
    private long _diskBytesWritten;
    private long _compilationLeaders;
    private long _compilationWaiters;

    public ClearScriptV8SharedCache(
        int maxSourceEntries = 512,
        int maxCodeEntries = 512,
        long maxCodeBytes = 128L * 1024 * 1024)
        : this(new ClearScriptV8SharedCacheOptions
        {
            MaxSourceEntries = maxSourceEntries,
            MaxCodeEntries = maxCodeEntries,
            MaxCodeBytes = maxCodeBytes
        })
    {
    }

    public ClearScriptV8SharedCache(ClearScriptV8SharedCacheOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.MaxSourceEntries);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.MaxCodeEntries);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.MaxCodeBytes);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.MaxPersistentEntries);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.MaxPersistentBytes);
        _maxSourceEntries = options.MaxSourceEntries;
        _maxCodeEntries = options.MaxCodeEntries;
        _maxCodeBytes = options.MaxCodeBytes;
        _maxPersistentEntries = options.MaxPersistentEntries;
        _maxPersistentBytes = options.MaxPersistentBytes;

        if (!string.IsNullOrWhiteSpace(options.PersistentDirectory))
        {
            var identity = CreateCompatibilityIdentity(options.CompatibilityTag);
            _persistentDirectory = Path.Combine(
                Path.GetFullPath(options.PersistentDirectory),
                Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(identity))));
        }
    }

    public static ClearScriptV8SharedCache ProcessWide { get; } = CreateProcessWide();

    public string? PersistentDirectory => _persistentDirectory;

    public V8SharedCacheMetrics GetMetrics()
    {
        lock (_gate)
        {
            return new V8SharedCacheMetrics(
                _sourceHits,
                _sourceMisses,
                _codeHits,
                _codeMisses,
                _codeAccepted,
                _codeVerified,
                _codeUpdated,
                _codeBytes,
                _sources.Count,
                _code.Count,
                _diskHits,
                _diskMisses,
                _diskInvalidEntries,
                _diskWrites,
                _diskBytesRead,
                _diskBytesWritten,
                _compilationLeaders,
                _compilationWaiters);
        }
    }

    /// <summary>
    /// Clears process memory and metrics. Persistent entries remain available to the
    /// next runtime or process; use <see cref="ClearPersistent"/> to delete them.
    /// </summary>
    public void Clear()
    {
        lock (_gate)
        {
            _sources.Clear();
            _sourceOrder.Clear();
            _code.Clear();
            _codeOrder.Clear();
            _sourceHits = 0;
            _sourceMisses = 0;
            _codeHits = 0;
            _codeMisses = 0;
            _codeAccepted = 0;
            _codeVerified = 0;
            _codeUpdated = 0;
            _codeBytes = 0;
            _diskHits = 0;
            _diskMisses = 0;
            _diskInvalidEntries = 0;
            _diskWrites = 0;
            _diskBytesRead = 0;
            _diskBytesWritten = 0;
            _compilationLeaders = 0;
            _compilationWaiters = 0;
        }
    }

    public void ClearPersistent()
    {
        if (_persistentDirectory is null)
        {
            return;
        }

        try
        {
            if (Directory.Exists(_persistentDirectory))
            {
                Directory.Delete(_persistentDirectory, recursive: true);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    internal ExternalJavaScriptSource ResolveSource(
        string resolutionKey,
        Func<ExternalJavaScriptSource> resolver)
    {
        using var keyLease = EnterKeyGate(_sourceKeyGates, resolutionKey, recordWaiter: false);
        lock (_gate)
        {
            if (_sources.TryGetValue(resolutionKey, out var cached) && cached.IsCurrent())
            {
                _sourceHits++;
                return cached.Source;
            }

            _sources.Remove(resolutionKey);
            _sourceMisses++;
        }

        var source = resolver();
        var entry = SourceEntry.Create(source);
        lock (_gate)
        {
            if (!_sources.ContainsKey(resolutionKey))
            {
                _sourceOrder.Enqueue(resolutionKey);
            }
            _sources[resolutionKey] = entry;
            TrimSources();
        }
        return source;
    }

    ExternalJavaScriptSource IWebSceneJavaScriptModuleCache.Resolve(
        string resolutionKey,
        Func<ExternalJavaScriptSource> resolver)
        => ResolveSource(resolutionKey, resolver);

    internal string CreateCodeKey(string documentName, string code)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(documentName);
        ArgumentNullException.ThrowIfNull(code);
        return string.Concat(documentName, "|", ComputeUtf8Hash(code));
    }

    internal IDisposable EnterCodeCompilation(string key)
        => EnterKeyGate(_codeKeyGates, key, recordWaiter: true);

    internal bool TryGetCode(string key, out byte[] cacheBytes)
    {
        lock (_gate)
        {
            if (_code.TryGetValue(key, out var cached))
            {
                _codeHits++;
                cacheBytes = cached;
                return true;
            }
        }

        if (TryReadPersistentCode(key, out cacheBytes))
        {
            StoreCodeInMemory(key, cacheBytes);
            lock (_gate)
            {
                _codeHits++;
            }
            return true;
        }

        lock (_gate)
        {
            _codeMisses++;
        }
        cacheBytes = Array.Empty<byte>();
        return false;
    }

    internal void StoreCode(string key, byte[] cacheBytes)
    {
        if (cacheBytes.Length == 0 || cacheBytes.LongLength > _maxCodeBytes)
        {
            return;
        }

        StoreCodeInMemory(key, cacheBytes);
        WritePersistentCode(key, cacheBytes);
    }

    internal void RecordCompilationLeader()
    {
        lock (_gate)
        {
            _compilationLeaders++;
        }
    }

    internal void RecordCodeResult(V8CacheResult result)
    {
        lock (_gate)
        {
            switch (result)
            {
                case V8CacheResult.Accepted:
                    _codeAccepted++;
                    break;
                case V8CacheResult.Verified:
                    _codeVerified++;
                    break;
                case V8CacheResult.Updated:
                    _codeUpdated++;
                    break;
            }
        }
    }

    internal string GetPersistentPathForProbe(string key)
        => GetPersistentPath(key)
           ?? throw new InvalidOperationException("Persistent cache is not enabled.");

    private static ClearScriptV8SharedCache CreateProcessWide()
    {
        var persistentDirectory = Environment.GetEnvironmentVariable("WEBSCENE_V8_CACHE_DIRECTORY");
        return new ClearScriptV8SharedCache(new ClearScriptV8SharedCacheOptions
        {
            PersistentDirectory = string.IsNullOrWhiteSpace(persistentDirectory)
                ? null
                : persistentDirectory
        });
    }

    private static string CreateCompatibilityIdentity(string? compatibilityTag)
    {
        var assembly = typeof(V8Runtime).Assembly;
        return string.Join(
            "|",
            "webscene-v8-cache",
            PersistentSchemaVersion,
            assembly.GetName().Version?.ToString() ?? "unknown",
            assembly.ManifestModule.ModuleVersionId,
            RuntimeInformation.RuntimeIdentifier,
            RuntimeInformation.ProcessArchitecture,
            compatibilityTag ?? string.Empty);
    }

    private static string ComputeUtf8Hash(string value)
    {
        var maximumBytes = Encoding.UTF8.GetMaxByteCount(value.Length);
        var rented = ArrayPool<byte>.Shared.Rent(maximumBytes);
        Span<byte> hash = stackalloc byte[SHA256.HashSizeInBytes];
        try
        {
            var count = Encoding.UTF8.GetBytes(value.AsSpan(), rented.AsSpan());
            SHA256.HashData(rented.AsSpan(0, count), hash);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
        return Convert.ToHexString(hash);
    }

    private static object[] CreateKeyGates()
        => Enumerable.Range(0, 127).Select(static _ => new object()).ToArray();

    private KeyGateLease EnterKeyGate(object[] gates, string key, bool recordWaiter)
    {
        var gate = gates[(int)((uint)StringComparer.Ordinal.GetHashCode(key) % gates.Length)];
        if (!Monitor.TryEnter(gate))
        {
            if (recordWaiter)
            {
                lock (_gate)
                {
                    _compilationWaiters++;
                }
            }
            Monitor.Enter(gate);
        }
        return new KeyGateLease(gate);
    }

    private void StoreCodeInMemory(string key, byte[] cacheBytes)
    {
        var stored = cacheBytes.ToArray();
        lock (_gate)
        {
            if (_code.TryGetValue(key, out var previous))
            {
                _codeBytes -= previous.LongLength;
            }
            else
            {
                _codeOrder.Enqueue(key);
            }
            _code[key] = stored;
            _codeBytes += stored.LongLength;
            TrimCode();
        }
    }

    private bool TryReadPersistentCode(string key, out byte[] cacheBytes)
    {
        cacheBytes = Array.Empty<byte>();
        var path = GetPersistentPath(key);
        if (path is null)
        {
            return false;
        }

        if (!File.Exists(path))
        {
            lock (_gate)
            {
                _diskMisses++;
            }
            return false;
        }

        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read | FileShare.Delete,
                bufferSize: 64 * 1024,
                FileOptions.SequentialScan);
            if (stream.Length < PersistentHeaderLength
                || stream.Length > PersistentHeaderLength + _maxCodeBytes)
            {
                return InvalidatePersistentEntry(path, out cacheBytes);
            }

            using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);
            var magic = reader.ReadBytes(PersistentMagic.Length);
            var schema = reader.ReadInt32();
            var storedKeyHash = reader.ReadBytes(SHA256.HashSizeInBytes);
            var payloadHash = reader.ReadBytes(SHA256.HashSizeInBytes);
            var payloadLength = reader.ReadInt32();
            var expectedKeyHash = SHA256.HashData(Encoding.UTF8.GetBytes(key));
            if (!magic.AsSpan().SequenceEqual(PersistentMagic)
                || schema != PersistentSchemaVersion
                || !storedKeyHash.AsSpan().SequenceEqual(expectedKeyHash)
                || payloadLength <= 0
                || payloadLength > _maxCodeBytes
                || stream.Length != PersistentHeaderLength + payloadLength)
            {
                return InvalidatePersistentEntry(path, out cacheBytes);
            }

            cacheBytes = reader.ReadBytes(payloadLength);
            if (cacheBytes.Length != payloadLength
                || !SHA256.HashData(cacheBytes).AsSpan().SequenceEqual(payloadHash))
            {
                return InvalidatePersistentEntry(path, out cacheBytes);
            }

            try
            {
                File.SetLastAccessTimeUtc(path, DateTime.UtcNow);
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }

            lock (_gate)
            {
                _diskHits++;
                _diskBytesRead += cacheBytes.LongLength;
            }
            return true;
        }
        catch (IOException)
        {
            return InvalidatePersistentEntry(path, out cacheBytes);
        }
        catch (UnauthorizedAccessException)
        {
            return InvalidatePersistentEntry(path, out cacheBytes);
        }
    }

    private bool InvalidatePersistentEntry(string path, out byte[] cacheBytes)
    {
        cacheBytes = Array.Empty<byte>();
        lock (_gate)
        {
            _diskInvalidEntries++;
            _diskMisses++;
        }
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
        return false;
    }

    private void WritePersistentCode(string key, byte[] cacheBytes)
    {
        var path = GetPersistentPath(key);
        if (path is null
            || cacheBytes.Length == 0
            || cacheBytes.LongLength > _maxPersistentBytes)
        {
            return;
        }

        var directory = Path.GetDirectoryName(path)!;
        var temporaryPath = string.Concat(path, ".", Environment.ProcessId, ".", Guid.NewGuid().ToString("N"), ".tmp");
        try
        {
            Directory.CreateDirectory(directory);
            using (var stream = new FileStream(
                       temporaryPath,
                       FileMode.CreateNew,
                       FileAccess.Write,
                       FileShare.None,
                       bufferSize: 64 * 1024,
                       FileOptions.WriteThrough))
            using (var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: false))
            {
                writer.Write(PersistentMagic);
                writer.Write(PersistentSchemaVersion);
                writer.Write(SHA256.HashData(Encoding.UTF8.GetBytes(key)));
                writer.Write(SHA256.HashData(cacheBytes));
                writer.Write(cacheBytes.Length);
                writer.Write(cacheBytes);
            }
            File.Move(temporaryPath, path, overwrite: true);
            lock (_gate)
            {
                _diskWrites++;
                _diskBytesWritten += cacheBytes.LongLength;
            }
            TrimPersistentCode();
        }
        catch (IOException)
        {
            TryDelete(temporaryPath);
        }
        catch (UnauthorizedAccessException)
        {
            TryDelete(temporaryPath);
        }
    }

    private string? GetPersistentPath(string key)
    {
        if (_persistentDirectory is null)
        {
            return null;
        }
        var fileName = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(key))) + ".v8cache";
        return Path.Combine(_persistentDirectory, fileName);
    }

    private void TrimSources()
    {
        while (_sources.Count > _maxSourceEntries && _sourceOrder.Count > 0)
        {
            _sources.Remove(_sourceOrder.Dequeue());
        }
    }

    private void TrimCode()
    {
        while ((_code.Count > _maxCodeEntries || _codeBytes > _maxCodeBytes)
               && _codeOrder.Count > 0)
        {
            var key = _codeOrder.Dequeue();
            if (_code.Remove(key, out var removed))
            {
                _codeBytes -= removed.LongLength;
            }
        }
    }

    private void TrimPersistentCode()
    {
        if (_persistentDirectory is null)
        {
            return;
        }

        try
        {
            var files = new DirectoryInfo(_persistentDirectory)
                .EnumerateFiles("*.v8cache", SearchOption.TopDirectoryOnly)
                .OrderByDescending(static file => file.LastAccessTimeUtc)
                .ThenByDescending(static file => file.LastWriteTimeUtc)
                .ToArray();
            long bytes = 0;
            for (var index = 0; index < files.Length; index++)
            {
                var file = files[index];
                bytes += file.Length;
                if (index >= _maxPersistentEntries || bytes > _maxPersistentBytes)
                {
                    TryDelete(file.FullName);
                }
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private sealed class KeyGateLease : IDisposable
    {
        private object? _gate;

        internal KeyGateLease(object gate)
        {
            _gate = gate;
        }

        public void Dispose()
        {
            var gate = Interlocked.Exchange(ref _gate, null);
            if (gate is not null)
            {
                Monitor.Exit(gate);
            }
        }
    }

    private sealed record SourceEntry(
        ExternalJavaScriptSource Source,
        DateTime? LastWriteTimeUtc,
        long? Length)
    {
        internal static SourceEntry Create(ExternalJavaScriptSource source)
        {
            if (Path.IsPathRooted(source.FileName) && File.Exists(source.FileName))
            {
                var info = new FileInfo(source.FileName);
                return new SourceEntry(source, info.LastWriteTimeUtc, info.Length);
            }
            return new SourceEntry(source, null, null);
        }

        internal bool IsCurrent()
        {
            if (LastWriteTimeUtc is null)
            {
                return true;
            }
            if (!File.Exists(Source.FileName))
            {
                return false;
            }
            var info = new FileInfo(Source.FileName);
            return info.LastWriteTimeUtc == LastWriteTimeUtc.Value && info.Length == Length;
        }
    }
}

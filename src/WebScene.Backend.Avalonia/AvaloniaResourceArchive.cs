using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using WebScene.Core;

namespace WebScene.Backends.Avalonia;

internal sealed class AvaloniaResourceArchive
{
    private const int CurrentSchemaVersion = 1;
    private const string ManifestFileName = "manifest.json";
    private readonly object _gate = new();
    private readonly string _rootDirectory;
    private readonly string _objectsDirectory;
    private readonly Dictionary<string, ResourceArchiveEntry> _entries;
    private readonly Dictionary<string, byte[]> _contentByFile = new(StringComparer.Ordinal);
    private bool _dirty;

    private AvaloniaResourceArchive(
        string directory,
        Dictionary<string, ResourceArchiveEntry> entries)
    {
        _rootDirectory = Path.GetFullPath(directory);
        _objectsDirectory = Path.Combine(_rootDirectory, "objects");
        _entries = entries;
    }

    internal static AvaloniaResourceArchive CreateCapture(string directory)
    {
        var manifestPath = Path.Combine(Path.GetFullPath(directory), ManifestFileName);
        if (File.Exists(manifestPath))
        {
            return OpenReplay(directory);
        }

        var archive = new AvaloniaResourceArchive(
            directory,
            new Dictionary<string, ResourceArchiveEntry>(StringComparer.Ordinal));
        Directory.CreateDirectory(archive._objectsDirectory);
        return archive;
    }

    internal static AvaloniaResourceArchive OpenReplay(string directory)
    {
        var root = Path.GetFullPath(directory);
        var manifestPath = Path.Combine(root, ManifestFileName);
        if (!File.Exists(manifestPath))
        {
            throw new FileNotFoundException(
                $"WebScene resource replay manifest '{manifestPath}' was not found.",
                manifestPath);
        }

        var manifest = JsonSerializer.Deserialize<ResourceArchiveManifest>(
                           File.ReadAllText(manifestPath))
                       ?? throw new InvalidDataException(
                           $"WebScene resource replay manifest '{manifestPath}' is empty.");
        if (manifest.SchemaVersion != CurrentSchemaVersion)
        {
            throw new InvalidDataException(
                $"WebScene resource replay manifest schema {manifest.SchemaVersion} is not supported; "
                + $"expected {CurrentSchemaVersion}.");
        }

        var entries = new Dictionary<string, ResourceArchiveEntry>(StringComparer.Ordinal);
        foreach (var entry in manifest.Entries)
        {
            if (string.IsNullOrWhiteSpace(entry.Key)
                || string.IsNullOrWhiteSpace(entry.ContentFile)
                || !entries.TryAdd(entry.Key, entry))
            {
                throw new InvalidDataException(
                    $"WebScene resource replay manifest '{manifestPath}' contains an invalid or duplicate entry.");
            }
        }

        return new AvaloniaResourceArchive(root, entries);
    }

    internal void CaptureText(
        Uri address,
        WebSceneResourceKind kind,
        in WebSceneTextResource resource)
    {
        if (resource.NotModified)
        {
            throw new InvalidOperationException(
                $"Cannot capture the 304 response for '{address}' without its cached body. "
                + "Use an empty WebScene resource cache for the capture run.");
        }

        var content = Encoding.UTF8.GetBytes(resource.Content);
        lock (_gate)
        {
            _entries[TextKey(address, kind)] = new ResourceArchiveEntry
            {
                Key = TextKey(address, kind),
                Address = address.ToString(),
                ResourceType = "text",
                Kind = kind,
                ContentFile = StoreContent(content),
                ContentLength = content.LongLength,
                CacheKey = resource.CacheKey,
                DisplayName = resource.DisplayName,
                Directory = resource.Directory,
                EntityTag = resource.EntityTag,
                LastModified = resource.LastModified,
                FreshUntil = resource.FreshUntil,
                IsCacheable = resource.IsCacheable
            };
            _dirty = true;
        }
    }

    internal WebSceneTextResource ReplayText(Uri address, WebSceneResourceKind kind)
    {
        var resource = ReplayUtf8Text(address, kind);
        return new WebSceneTextResource(
            resource.CacheKey,
            Encoding.UTF8.GetString(resource.Content.Span),
            resource.DisplayName,
            resource.Directory)
        {
            EntityTag = resource.EntityTag,
            LastModified = resource.LastModified,
            FreshUntil = resource.FreshUntil,
            IsCacheable = resource.IsCacheable
        };
    }

    internal AvaloniaUtf8Resource ReplayUtf8Text(
        Uri address,
        WebSceneResourceKind kind)
    {
        ResourceArchiveEntry entry;
        lock (_gate)
        {
            if (!_entries.TryGetValue(TextKey(address, kind), out entry!))
            {
                throw MissingResource(address, $"text/{kind}");
            }
        }

        return new AvaloniaUtf8Resource(
            entry.CacheKey ?? entry.Address,
            ReadContent(entry),
            entry.DisplayName ?? entry.Address,
            entry.Directory)
        {
            EntityTag = entry.EntityTag,
            LastModified = entry.LastModified,
            FreshUntil = entry.FreshUntil,
            IsCacheable = entry.IsCacheable
        };
    }

    internal void CaptureBinary(Uri address, in AvaloniaBinaryResource resource)
    {
        lock (_gate)
        {
            _entries[BinaryKey(address)] = new ResourceArchiveEntry
            {
                Key = BinaryKey(address),
                Address = address.ToString(),
                ResourceType = "binary",
                ContentFile = StoreContent(resource.Content),
                ContentLength = resource.Content.LongLength,
                CacheKey = resource.CacheKey,
                DisplayName = resource.DisplayName
            };
            _dirty = true;
        }
    }

    internal AvaloniaBinaryResource ReplayBinary(Uri address)
    {
        ResourceArchiveEntry entry;
        lock (_gate)
        {
            if (!_entries.TryGetValue(BinaryKey(address), out entry!))
            {
                throw MissingResource(address, "binary");
            }
        }

        return new AvaloniaBinaryResource(
            entry.CacheKey ?? entry.Address,
            ReadContent(entry),
            entry.DisplayName ?? entry.Address);
    }

    internal void Preload()
    {
        lock (_gate)
        {
            foreach (var entry in _entries.Values)
            {
                if (!_contentByFile.ContainsKey(entry.ContentFile))
                {
                    _contentByFile.Add(entry.ContentFile, ReadContentFromDisk(entry));
                }
            }
        }
    }

    internal void Flush()
    {
        lock (_gate)
        {
            if (!_dirty)
            {
                return;
            }

            Directory.CreateDirectory(_rootDirectory);
            var manifest = new ResourceArchiveManifest
            {
                SchemaVersion = CurrentSchemaVersion,
                Entries = _entries.Values
                    .OrderBy(entry => entry.Key, StringComparer.Ordinal)
                    .ToArray()
            };
            var manifestPath = Path.Combine(_rootDirectory, ManifestFileName);
            var temporaryPath = Path.Combine(_rootDirectory, $".{ManifestFileName}.{Guid.NewGuid():N}.tmp");
            File.WriteAllText(
                temporaryPath,
                JsonSerializer.Serialize(
                    manifest,
                    new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporaryPath, manifestPath, overwrite: true);
            _dirty = false;
        }
    }

    private string StoreContent(ReadOnlySpan<byte> content)
    {
        var hash = Convert.ToHexString(SHA256.HashData(content)).ToLowerInvariant();
        var relativePath = Path.Combine("objects", hash + ".bin");
        var fullPath = Path.Combine(_rootDirectory, relativePath);
        if (!File.Exists(fullPath))
        {
            File.WriteAllBytes(fullPath, content.ToArray());
        }
        return relativePath.Replace(Path.DirectorySeparatorChar, '/');
    }

    private byte[] ReadContent(ResourceArchiveEntry entry)
    {
        lock (_gate)
        {
            if (_contentByFile.TryGetValue(entry.ContentFile, out var content))
            {
                return content;
            }

            content = ReadContentFromDisk(entry);
            _contentByFile.Add(entry.ContentFile, content);
            return content;
        }
    }

    private byte[] ReadContentFromDisk(ResourceArchiveEntry entry)
    {
        var relativePath = entry.ContentFile.Replace('/', Path.DirectorySeparatorChar);
        var fullPath = Path.GetFullPath(Path.Combine(_rootDirectory, relativePath));
        var rootPrefix = _rootDirectory.EndsWith(Path.DirectorySeparatorChar)
            ? _rootDirectory
            : _rootDirectory + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(rootPrefix, StringComparison.Ordinal)
            || !File.Exists(fullPath))
        {
            throw new InvalidDataException(
                $"WebScene resource replay object '{entry.ContentFile}' is missing or outside the archive.");
        }

        var content = File.ReadAllBytes(fullPath);
        if (content.LongLength != entry.ContentLength)
        {
            throw new InvalidDataException(
                $"WebScene resource replay object '{entry.ContentFile}' has length {content.LongLength}; "
                + $"expected {entry.ContentLength}.");
        }
        return content;
    }

    private static FileNotFoundException MissingResource(Uri address, string type)
        => new(
            $"Resource '{address}' ({type}) is not present in the deterministic WebScene replay archive. "
            + "Replay never falls back to the network.");

    private static string TextKey(Uri address, WebSceneResourceKind kind)
        => $"text:{(int)kind}:{address}";

    private static string BinaryKey(Uri address) => $"binary:{address}";

    private sealed class ResourceArchiveManifest
    {
        public int SchemaVersion { get; set; }

        public ResourceArchiveEntry[] Entries { get; set; } = [];
    }

    private sealed class ResourceArchiveEntry
    {
        public string Key { get; set; } = string.Empty;

        public string Address { get; set; } = string.Empty;

        public string ResourceType { get; set; } = string.Empty;

        public WebSceneResourceKind? Kind { get; set; }

        public string ContentFile { get; set; } = string.Empty;

        public long ContentLength { get; set; }

        public string? CacheKey { get; set; }

        public string? DisplayName { get; set; }

        public string? Directory { get; set; }

        public string? EntityTag { get; set; }

        public DateTimeOffset? LastModified { get; set; }

        public DateTimeOffset? FreshUntil { get; set; }

        public bool IsCacheable { get; set; } = true;
    }
}

internal readonly record struct AvaloniaUtf8Resource(
    string CacheKey,
    ReadOnlyMemory<byte> Content,
    string DisplayName,
    string? Directory)
{
    internal string? EntityTag { get; init; }

    internal DateTimeOffset? LastModified { get; init; }

    internal DateTimeOffset? FreshUntil { get; init; }

    internal bool IsCacheable { get; init; } = true;
}

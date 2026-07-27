using System.Globalization;
using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Text.RegularExpressions;
using Avalonia.Media;
using Avalonia.Media.Fonts;
using Avalonia.Platform;
using WebScene.Css;

namespace JavaScript.Avalonia;

/// <summary>
/// Owns downloadable faces for one DOM document. Fetching happens off the UI
/// thread; Avalonia font collections are installed on the document dispatcher.
/// </summary>
internal sealed class CssFontFaceRegistry : IDisposable
{
    private const int MaximumFaces = 64;
    private readonly AvaloniaDomDocument _document;
    private readonly string _registryId = Guid.NewGuid().ToString("N");
    private readonly Dictionary<string, FaceEntry> _entries = new(StringComparer.Ordinal);
    private bool _disposed;

    internal CssFontFaceRegistry(AvaloniaDomDocument document)
        => _document = document;

    internal int LoadedFaceCount => _entries.Values.Count(static entry => entry.LoadedFamily is not null);
    internal int FaceCount => _entries.Count;
    internal IReadOnlyList<string> LoadErrors => _entries.Values
        .Where(static entry => entry.LastError is not null)
        .Select(static entry => entry.LastError!)
        .ToArray();

    internal void Synchronize(IEnumerable<CssFontFaceSource> sources)
    {
        if (_disposed) return;

        var live = new HashSet<string>(StringComparer.Ordinal);
        foreach (var source in sources.Take(MaximumFaces))
        {
            var family = Unquote(source.Face.Family);
            if (family.Length == 0) continue;

            var key = CreateKey(source, family);
            live.Add(key);
            if (_entries.ContainsKey(key)) continue;

            var entry = new FaceEntry(
                key,
                family,
                ParseStyle(source.Face.Style),
                ParseWeightRange(source.Face.Weight),
                ParseStretch(source.Face.Stretch),
                source.BaseAddress);
            _entries.Add(key, entry);
            _ = LoadAsync(entry, CssFontSourceParser.ExtractUrls(source.Face.Source));
        }

        foreach (var staleKey in _entries.Keys.Where(key => !live.Contains(key)).ToArray())
        {
            Remove(_entries[staleKey]);
            _entries.Remove(staleKey);
        }
    }

    internal CssFontResolution? Resolve(
        string? familyList,
        FontStyle style,
        FontWeight weight,
        FontStretch stretch)
    {
        var requestedWeight = (int)weight;
        foreach (var family in CssFontResolver.ParseFamilyList(familyList))
        {
            FaceEntry? best = null;
            var bestScore = int.MaxValue;
            foreach (var entry in _entries.Values)
            {
                if (entry.LoadedFamily is null
                    || !string.Equals(entry.Family, family, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                var weightDistance = requestedWeight < entry.MinimumWeight
                    ? entry.MinimumWeight - requestedWeight
                    : requestedWeight > entry.MaximumWeight
                        ? requestedWeight - entry.MaximumWeight
                        : 0;
                var score = weightDistance
                            + (entry.Style == style ? 0 : 10_000)
                            + (entry.Stretch == stretch ? 0 : 1_000);
                if (score < bestScore)
                {
                    best = entry;
                    bestScore = score;
                }
            }

            if (best?.LoadedFamily is { } loaded)
            {
                return new CssFontResolution(loaded, CssFontMetricProfile.None);
            }
        }

        return null;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        foreach (var entry in _entries.Values) Remove(entry);
        _entries.Clear();
    }

    private async Task LoadAsync(FaceEntry entry, IReadOnlyList<string> urls, int startIndex = 0)
    {
        for (var index = startIndex; index < urls.Count; index++)
        {
            var url = urls[index];
            try
            {
                var resource = await _document.LoadBinaryResourceAsync(
                    url,
                    entry.BaseAddress,
                    entry.Cancellation.Token).ConfigureAwait(false);
                if (!IsPotentialFontResource(resource.DisplayName, resource.Content)) continue;

                var nextIndex = index + 1;
                _document.PostFontFaceCompletion(() =>
                {
                    if (!Install(entry, resource.Content, resource.DisplayName))
                    {
                        _ = LoadAsync(entry, urls, nextIndex);
                    }
                });
                return;
            }
            catch (OperationCanceledException) when (entry.Cancellation.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
            {
                // CSS src is an ordered fallback list. Continue with the next
                // supported URL just as a browser does after fetch/decode failure.
                entry.LastError = exception.GetType().Name + ": " + exception.Message;
            }
        }
    }

    private bool Install(FaceEntry entry, byte[] bytes, string displayName)
    {
        if (_disposed || entry.Cancellation.IsCancellationRequested || !_entries.ContainsKey(entry.Key))
        {
            DeleteTemporaryFile(entry);
            return true;
        }

        var collectionKey = new Uri($"fonts:webscene-{_registryId}-{entry.CollectionOrdinal}");
        var collection = new WebSceneDownloadedFontCollection(collectionKey, bytes);
        FontManager.Current.AddFontCollection(collection);
        if (collection.Count == 0)
        {
            FontManager.Current.RemoveFontCollection(collectionKey);
            (collection as IDisposable)?.Dispose();
            entry.LastError = $"The platform font manager could not decode '{displayName}'.";
            DeleteTemporaryFile(entry);
            return false;
        }

        entry.Collection = collection;
        entry.CollectionKey = collectionKey;
        // A CSS family is an alias. The underlying OpenType name is deliberately
        // not required to equal the authored @font-face family.
        entry.LoadedFamily = new FontFamily(collectionKey.AbsoluteUri + "#" + collection[0].Name);
        CssFontResolver.ClearCache();
        _document.NotifyFontFacesChanged();
        return true;
    }

    private void Remove(FaceEntry entry)
    {
        entry.Cancellation.Cancel();
        if (entry.CollectionKey is { } key)
        {
            FontManager.Current.RemoveFontCollection(key);
        }
        (entry.Collection as IDisposable)?.Dispose();
        entry.Cancellation.Dispose();
        DeleteTemporaryFile(entry);
    }

    private static void DeleteTemporaryFile(FaceEntry entry)
    {
        if (entry.TemporaryPath is not { } path) return;
        try { File.Delete(path); }
        catch { }
        entry.TemporaryPath = null;
    }

    private static string CreateKey(CssFontFaceSource source, string family)
        => string.Join("\n", family, source.Face.Source, source.Face.Style, source.Face.Weight,
            source.Face.Stretch, source.BaseAddress ?? string.Empty);

    private static bool IsPotentialFontResource(string source, byte[] content)
    {
        var path = source;
        if (Uri.TryCreate(source, UriKind.Absolute, out var uri)) path = uri.AbsolutePath;
        var extension = Path.GetExtension(path).ToLowerInvariant();
        if (extension is ".ttf" or ".otf" or ".ttc" or ".woff" or ".woff2") return true;
        if (content.Length < 4) return false;
        return (content[0], content[1], content[2], content[3]) is
            (0x00, 0x01, 0x00, 0x00)
            or ((byte)'O', (byte)'T', (byte)'T', (byte)'O')
            or ((byte)'t', (byte)'t', (byte)'c', (byte)'f')
            or ((byte)'w', (byte)'O', (byte)'F', (byte)'F')
            or ((byte)'w', (byte)'O', (byte)'F', (byte)'2');
    }

    private static FontStyle ParseStyle(string value)
        => value.Trim().ToLowerInvariant() switch
        {
            "italic" => FontStyle.Italic,
            "oblique" => FontStyle.Oblique,
            _ => FontStyle.Normal
        };

    private static (int Minimum, int Maximum) ParseWeightRange(string value)
    {
        var numbers = Regex.Matches(value, @"\b\d{1,4}\b", RegexOptions.CultureInvariant)
            .Select(match => int.Parse(match.Value, CultureInfo.InvariantCulture))
            .Select(number => Math.Clamp(number, 1, 1000))
            .ToArray();
        if (numbers.Length > 0)
        {
            return (numbers.Min(), numbers.Max());
        }
        return value.Trim().Equals("bold", StringComparison.OrdinalIgnoreCase) ? (700, 700) : (400, 400);
    }

    private static FontStretch ParseStretch(string value)
        => value.Trim().ToLowerInvariant() switch
        {
            "ultra-condensed" => FontStretch.UltraCondensed,
            "extra-condensed" => FontStretch.ExtraCondensed,
            "condensed" => FontStretch.Condensed,
            "semi-condensed" => FontStretch.SemiCondensed,
            "semi-expanded" => FontStretch.SemiExpanded,
            "expanded" => FontStretch.Expanded,
            "extra-expanded" => FontStretch.ExtraExpanded,
            "ultra-expanded" => FontStretch.UltraExpanded,
            _ => FontStretch.Normal
        };

    private static string Unquote(string value)
    {
        var result = value.Trim();
        if (result.Length >= 2 && result[0] == result[^1] && result[0] is '\'' or '"')
        {
            result = result[1..^1];
        }
        return result.Trim();
    }

    private sealed class FaceEntry
    {
        private static int s_nextOrdinal;

        internal FaceEntry(
            string key,
            string family,
            FontStyle style,
            (int Minimum, int Maximum) weight,
            FontStretch stretch,
            string? baseAddress)
        {
            Key = key;
            Family = family;
            Style = style;
            MinimumWeight = weight.Minimum;
            MaximumWeight = weight.Maximum;
            Stretch = stretch;
            BaseAddress = baseAddress;
            CollectionOrdinal = Interlocked.Increment(ref s_nextOrdinal);
        }

        internal string Key { get; }
        internal string Family { get; }
        internal FontStyle Style { get; }
        internal int MinimumWeight { get; }
        internal int MaximumWeight { get; }
        internal FontStretch Stretch { get; }
        internal string? BaseAddress { get; }
        internal int CollectionOrdinal { get; }
        internal CancellationTokenSource Cancellation { get; } = new();
        internal WebSceneDownloadedFontCollection? Collection { get; set; }
        internal Uri? CollectionKey { get; set; }
        internal FontFamily? LoadedFamily { get; set; }
        internal string? TemporaryPath { get; set; }
        internal string? LastError { get; set; }
    }
}

/// <summary>
/// Avalonia 11's EmbeddedFontCollection only discovers application assets.
/// This collection supplies downloaded bytes to the public font-manager stream
/// seam while retaining the same bounded collection lifetime.
/// </summary>
internal sealed class WebSceneDownloadedFontCollection : FontCollectionBase
{
    private readonly Uri _key;
    private readonly byte[] _bytes;
    private readonly List<FontFamily> _families = new(1);

    internal WebSceneDownloadedFontCollection(Uri key, byte[] bytes)
    {
        _key = key;
        _bytes = bytes;
    }

    public override Uri Key => _key;
    public override int Count => _families.Count;
    public override FontFamily this[int index] => _families[index];

    public override void Initialize(IFontManagerImpl fontManager)
    {
        using var stream = new MemoryStream(_bytes, writable: false);
        var streamFactory = fontManager.GetType().GetMethod(
            "TryCreateGlyphTypeface",
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            binder: null,
            types: [typeof(Stream), typeof(FontSimulations), typeof(IGlyphTypeface).MakeByRefType()],
            modifiers: null);
        if (streamFactory is null) return;
        object?[] arguments = [stream, FontSimulations.None, null];
        if (streamFactory.Invoke(fontManager, arguments) is not true
            || arguments[2] is not IGlyphTypeface glyphTypeface)
        {
            return;
        }
        Add(glyphTypeface.FamilyName, glyphTypeface);
    }

    public override bool TryGetGlyphTypeface(
        string familyName,
        FontStyle style,
        FontWeight weight,
        FontStretch stretch,
        [NotNullWhen(true)] out IGlyphTypeface? glyphTypeface)
    {
        glyphTypeface = null;
        if (!_glyphTypefaceCache.TryGetValue(familyName, out var faces)) return false;

        var requested = new FontCollectionKey(style, weight, stretch);
        if (faces.TryGetValue(requested, out glyphTypeface) && glyphTypeface is not null) return true;

        var bestScore = int.MaxValue;
        foreach (var pair in faces)
        {
            if (pair.Value is null) continue;
            var score = Math.Abs((int)pair.Key.Weight - (int)weight)
                        + (pair.Key.Style == style ? 0 : 10_000)
                        + (pair.Key.Stretch == stretch ? 0 : 1_000);
            if (score >= bestScore) continue;
            bestScore = score;
            glyphTypeface = pair.Value;
        }
        return glyphTypeface is not null;
    }

    public override IEnumerator<FontFamily> GetEnumerator() => _families.GetEnumerator();

    private void Add(string familyName, IGlyphTypeface glyphTypeface)
    {
        if (string.IsNullOrWhiteSpace(familyName)) return;
        var faces = _glyphTypefaceCache.GetOrAdd(
            familyName,
            name =>
            {
                _families.Add(new FontFamily(_key, name));
                return new ConcurrentDictionary<FontCollectionKey, IGlyphTypeface?>();
            });
        faces.TryAdd(
            new FontCollectionKey(glyphTypeface.Style, glyphTypeface.Weight, glyphTypeface.Stretch),
            glyphTypeface);
    }
}

internal readonly record struct CssFontFaceSource(CssCompiledFontFace Face, string? BaseAddress);

internal static class CssFontSourceParser
{
    private static readonly Regex s_url = new(
        @"url\(\s*(?:(?<quote>['""])(?<quoted>.*?)\k<quote>|(?<plain>[^)]*?))\s*\)",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant | RegexOptions.Compiled);

    internal static IReadOnlyList<string> ExtractUrls(string source)
        => s_url.Matches(source)
            .Select(match => match.Groups["quote"].Success
                ? match.Groups["quoted"].Value
                : match.Groups["plain"].Value.Trim())
            .Where(value => value.Length > 0)
            .ToArray();
}

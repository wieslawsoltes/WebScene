using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

public readonly record struct NativeRendererMemoryMetrics(
    int RetainedLayerCount,
    long RetainedCommandCount,
    ulong LogicalBitmapBytes,
    int IsolationLayerCount,
    ulong IsolationLogicalBitmapBytes,
    uint DomCommandCount,
    int StringCount,
    ulong StringBytes,
    int TypefaceCount,
    int SvgPictureCount,
    int ProcessSvgPictureCount,
    int ProcessSvgPictureReferenceCount,
    long ProcessSvgPictureMemoryHits);

internal sealed class SharedSvgPictureLease : IDisposable
{
    private SharedSvgPictureCache.Entry? _entry;

    internal SharedSvgPictureLease(SharedSvgPictureCache.Entry entry)
    {
        _entry = entry;
    }

    internal string Markup
        => _entry?.Markup
            ?? throw new ObjectDisposedException(nameof(SharedSvgPictureLease));

    internal SKPicture Picture
        => _entry?.Picture
            ?? throw new ObjectDisposedException(nameof(SharedSvgPictureLease));

    public void Dispose()
    {
        var entry = Interlocked.Exchange(ref _entry, null);
        if (entry is not null)
        {
            SharedSvgPictureCache.Release(entry);
        }
    }
}

internal static class SharedSvgPictureCache
{
    internal sealed class Entry(string markup, SKSvg svg)
    {
        internal string Markup { get; } = markup;
        internal SKSvg Svg { get; } = svg;
        internal SKPicture Picture { get; } =
            svg.Picture ?? throw new InvalidOperationException("SVG has no picture.");
        internal int References { get; set; } = 1;
    }

    private static readonly object Gate = new();
    private static readonly Dictionary<string, Entry> Entries =
        new(StringComparer.Ordinal);
    private static long s_memoryHits;

    internal static int EntryCount
    {
        get
        {
            lock (Gate) return Entries.Count;
        }
    }

    internal static int ReferenceCount
    {
        get
        {
            lock (Gate) return Entries.Values.Sum(static entry => entry.References);
        }
    }

    internal static long MemoryHitCount => Interlocked.Read(ref s_memoryHits);

    internal static SharedSvgPictureLease? Acquire(string markup)
    {
        lock (Gate)
        {
            if (Entries.TryGetValue(markup, out var known))
            {
                known.References++;
                Interlocked.Increment(ref s_memoryHits);
                return new SharedSvgPictureLease(known);
            }

            var svg = new SKSvg();
            try
            {
                if (svg.FromSvg(markup) is null || svg.Picture is null)
                {
                    svg.Dispose();
                    return null;
                }
                var entry = new Entry(markup, svg);
                Entries.Add(entry.Markup, entry);
                return new SharedSvgPictureLease(entry);
            }
            catch
            {
                svg.Dispose();
                return null;
            }
        }
    }

    internal static void Release(Entry entry)
    {
        lock (Gate)
        {
            if (--entry.References != 0)
            {
                return;
            }
            Entries.Remove(entry.Markup);
            entry.Svg.Dispose();
        }
    }
}

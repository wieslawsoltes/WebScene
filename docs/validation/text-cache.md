# Shared text cache validation

This extracts shaped-run and text-blob caching developed during Windows Kestrel
work. It builds on the system-font probe cache already merged in PR #49; that
earlier cache handles macOS system-UI family detection. These additional caches
live in `NativeTextShaping`, shared by Avalonia and Uno, and require no WebGPU,
native engine, canvas checkpoint or frame scheduling changes.

Shaped runs are cached by typeface object identity, text, size, horizontal scale
and encoding, with a 2,048-entry / estimated 4 MiB bound. Identity is checked after
hash lookup, and weak typeface references avoid retaining document fonts in this
cache. Short unspaced, unscaled draws additionally reuse up to 512 native text
blobs, keyed by font styling and rasterization profile. Eviction disposes blobs;
native fonts owned by these blobs remain retained until eviction. Draw location
and color are applied during replay. Spacing and horizontal-advance adjustments
retain the existing per-draw positioning path.

## Windows validation

- .NET 8 and .NET 10: all 71 `NativeTextShapingTests` / `ShapedRunCacheTests` pass.
- Uno Release build passes with zero warnings and errors.
- Tests compare glyph data with uncached HarfBuzz, compare CPU-rendered pixels at
  different baselines and font styles, distinguish typefaces, and exercise bounds
  and eviction. PR #49's web-font registration/isolation regression also passes.

Run the CPU-only probe:

```sh
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- probe text-cache
```

It warms a repeated label, runs six rounds of 10,000 operations, and reverses
operation order on alternate rounds. Recorded Windows .NET 10 medians:

| Operation | Uncached | Cached |
| --- | ---: | ---: |
| Shape repeated label | 3.00 µs | 0.15 µs |
| Shape and draw onto CPU bitmap | 7.07 µs | 3.26 µs |

Cached operations reported zero managed allocations in this probe. This does
not measure native Skia allocations, cold/miss-heavy workloads, concurrent
contention, application frame rates or other operating systems. JIT/CPU variation
is visible across rounds. Raw samples are in [text-cache-windows.json](text-cache-windows.json).

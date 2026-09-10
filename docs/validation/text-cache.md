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
blobs, keyed by font styling and rasterization profile. Eviction retires blobs;
disposal waits for active draw readers to return. Drawing runs outside the cache
lock. Native fonts owned by these blobs remain retained until eviction and the
final active reader returns. Draw location and color are applied during replay. Spacing and horizontal-advance adjustments
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
operation order on alternate rounds. Recorded Windows .NET 10 medians at `0d32a5cf`, before the reader-lifetime change:

| Operation | Uncached | Cached |
| --- | ---: | ---: |
| Shape repeated label | 3.00 µs | 0.15 µs |
| Shape and draw onto CPU bitmap | 7.07 µs | 3.26 µs |

Cached operations reported zero managed allocations in this probe. This does
not measure native Skia allocations, cold/miss-heavy workloads, concurrent
contention, application frame rates or other operating systems. JIT/CPU variation
is visible across rounds. Raw samples are in [text-cache-windows.json](text-cache-windows.json).

## Follow-up validation on macOS

The expanded focused suite passes 75 tests on each of .NET 8 and .NET 10.
New tests check LRU eviction, native handle disposal, multiple active readers
across cache disposal, oversize bypass, rasterization-profile separation, color
reuse, and pixel equivalence during concurrent draws with frequent eviction.
The cache pins blobs during drawing and releases the global lock before calling
Skia, while retaining zero managed allocations on warm hits.

The same CPU probe on macOS arm64 (.NET 10, Helvetica), after this change:

| Operation | Uncached | Cached |
| --- | ---: | ---: |
| Shape repeated label | 3.70 µs | 0.15 µs |
| Shape and draw onto CPU bitmap | 6.96 µs | 2.14 µs |

Both cached operations report zero managed allocations. These are warm-label
measurements, not an application frame-rate or concurrent-throughput claim.
Raw samples: [text-cache-macos.json](text-cache-macos.json).

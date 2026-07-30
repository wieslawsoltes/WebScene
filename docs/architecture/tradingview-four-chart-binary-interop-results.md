# TradingView-shaped four-chart generated binary interop

- **Date:** 2026-07-30
- **Runtime:** .NET 10.0.9, macOS 26.4.1 arm64, Apple M4
- **Native engine:** the same V8-enabled library for both variants
- **Application reference:** `feature/native-chart` and
  `/Volumes/SSD/repos/StackWich` were inspected read-only; that worktree was
  not modified
- **Hot API shape:** generated
  `onRealtimeUpdate(subscriberUid, SandwichTradingViewBar)` calls
- **Steady workload:** four independent warm engines, 60 updates/sec/engine,
  600 ticks, 2,400 generated calls over ten seconds
- **Trials:** three fresh-process trials in alternating order

## Compared paths

The measured JSON control modeled the former StackWich boundary:
`SemaphoreSlim`,
linked cancellation, `Task.Run`, blocking JSON evaluation, generated
JavaScript source, JSON argument parsing, and JSON result parsing.

The candidate uses the generated `.d.ts` facade end to end:

1. a static call-site descriptor identifies the retained target and member;
2. a generated codec writes the subscriber and bar DTO into pooled tagged
   request storage;
3. native code copies into a recycled request record and invokes V8 directly;
4. one pooled operation slot reports completion;
5. the tagged result is decoded directly and its size-classed arena is
   released.

There is no JSON, generated source, reflection, `Task`, `TaskCompletionSource`,
dictionary node, or result-lease allocation on the materialized void hot path.

## Four-chart median results

| Metric | Removed JSON control | Generated binary | Change |
| --- | ---: | ---: | ---: |
| Process CPU / 10 s | 856.9 ms | 604.3 ms | **-29.5%** |
| Normalized process CPU | 8.57% | 6.04% | **-29.5%** |
| Managed allocated bytes | 7,204,992 | 240,248 | **-96.7%** |
| Managed bytes/call | 3,002.1 B | 100.1 B | **-96.7%** |
| End working set | 99.95 MiB | 86.02 MiB | **-13.9%** |
| Working-set growth | 17.23 MiB | 10.20 MiB | **-40.8%** |
| Gen 0/1/2 collections | 0 / 0 / 0 | 0 / 0 / 0 | unchanged |
| Delivered updates | 2,400 | 2,400 | unchanged |

Ten seconds at 60 Hz does not allocate enough to force a collection in either
process. A same-duration 600 Hz/engine burst (24,000 calls) makes the GC effect
visible:

| Burst metric | Removed JSON control | Generated binary | Change |
| --- | ---: | ---: | ---: |
| Process CPU / 10 s | 4,007.2 ms | 3,098.0 ms | **-22.7%** |
| Managed allocated bytes | 72,045,088 | 2,285,848 | **-96.8%** |
| Gen 0 collections | 8 | 0 | **-100%** |
| Gen 1/2 collections | 0 / 0 | 0 / 0 | unchanged |
| End working set | 109.36 MiB | 89.89 MiB | **-17.8%** |
| Delivered updates | 24,000 | 24,000 | unchanged |

All binary trials ended with:

- zero outstanding result leases and zero active operation slots;
- four result-arena misses followed by 2,660 hits at 60 Hz;
- four request-record misses followed by 2,660 hits;
- four available operation slots with a high-water mark of four;
- 1,048 retained result bytes, all in the 4 KiB size class;
- no oversize request or result allocation.

## Generated API microbenchmarks

BenchmarkDotNet exercised the generated facade with 1, 4, and 8 warm engines.
The operation includes request encoding, queueing, V8 invocation, completion,
and result release.

| Engines | Removed JSON update | Binary update | JSON allocation | Binary allocation |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 15.55 us | 10.98 us | 3,080 B | 174 B |
| 4 | 59.45 us | 47.33 us | 11,984 B | 364 B |
| 8 | 110.79 us | 90.65 us | 23,856 B | 618 B |

The residual fixed managed allocation includes the benchmark's async loop and
completion scheduling; it does not scale with the DTO payload.

For a 256-bar promise result, policy generated an additional
`BorrowHistoryAsync` API. It returns a disposable lease and stack-only array
view; values and UTF-8 strings remain in the native arena:

| Engines | Materialized | Borrowed | Materialized allocation | Borrowed allocation |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 236.63 us | 221.21 us | 20,704 B | 480 B |
| 4 | 932.34 us | 867.44 us | 82,408 B | 1,512 B |
| 8 | 1,856.73 us | 1,733.66 us | 164,680 B | 2,888 B |

Borrowing removes 97.7-98.2% of managed allocation. Its fixed lease/control
cost grows with engine count, not with the 256-item payload.

## Correctness and stress

The V8 test suite covers direct globals, receiver-preserving members, tagged
DTO arguments, retained, stale, and host-provided handles,
immediate and delayed promise fulfillment, delayed rejection, cancellation,
engine destruction with a live result lease, malformed ranges, and pool reuse.

With `WEBSCENE_INTEROP_STRESS=1`, 100,000 operations alternate arbitrary
leased evaluation and generated direct tagged invocation, including promises,
while retaining 32 result arenas concurrently. The run ends with no live
result or operation and no breach of the 8 MiB retained-capacity limit.

The completion-lifetime probe additionally cancels 3,200 delayed promises
while disposing 100 managed transports. All 3,200 complete as cancellation,
with no faults, outstanding results, or active native operations.

## Bidirectional completion follow-up

The reverse-callback integration audit also exposed two small forward-call
control allocations that the earlier numbers included: a linked
`ConcurrentStack` node whenever a decoder returned to its pool, and a
ThreadPool continuation wrapper for each native completion. The transport now
uses capacity-retaining locked stacks and queues the already-pooled operation
slot itself as the work item. A publisher/consumer handshake prevents a slot
or decoder source from being reused until both `SetResult` and `GetResult`
have unwound, and materialized results release their native lease before
resuming caller code.

Fresh-process validation after that change:

| Four-chart workload | Calls | Managed allocation | Bytes/call | GC | Outstanding leases |
| --- | ---: | ---: | ---: | ---: | ---: |
| 60 updates/sec/chart | 2,400 | 11,064 B | 4.61 B | 0/0/0 | 0 |
| 600 updates/sec/chart | 24,000 | 18,264 B | 0.761 B | 0/0/0 | 0 |

The nearly flat total as call count increases shows that the remaining bytes
are amortized runtime/ThreadPool queue-segment bookkeeping, not a managed
request, result, DTO, string, operation, or decoder object per call. The
pre-fix 60 Hz acceptance run measured 236,808 B (98.67 B/call), so this removes
95.3% at the normal rate and 99.2% on the burst workload.

## Reproduction

Build the V8 library and run correctness/stress:

```bash
cmake --build artifacts/native-engine-interop-v8 -j 6
WEBSCENE_INTEROP_STRESS=1 \
  ctest --test-dir artifacts/native-engine-interop-v8 \
  -C Release --output-on-failure
```

Run the four-chart binary acceptance process:

```bash
WEBSCENE_NATIVE_ENGINE_PATH="$PWD/artifacts/native-engine-interop-v8/libwebscene_native_engine.dylib" \
dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe generated-realtime-chart \
  --mode binary --charts 4 --ticks 600 --rate 60
```

The JSON control and its public mode were removed after these measurements
because the ABI was unreleased and the accepted design is binary-only for
codec-supported generated calls and callbacks.
Commit `760d18a` preserves the matched pre-removal benchmark implementation and
must be paired with its native library to reproduce the historical JSON
column. The current BenchmarkDotNet command runs the binary regression across
the 1/4/8-engine matrix:

```bash
WEBSCENE_NATIVE_ENGINE_PATH="$PWD/artifacts/native-engine-interop-v8/libwebscene_native_engine.dylib" \
dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  --filter '*GeneratedRealtimeChartInteropBenchmarks*' --job short
```

Exercise cancellation and transport-disposal races with:

```bash
WEBSCENE_NATIVE_ENGINE_PATH="$PWD/artifacts/native-engine-interop-v8/libwebscene_native_engine.dylib" \
dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe native-interop-race --batches 100 --width 32
```

The actual StackWich policy/manifests can be compiled read-only by overriding
the generator compile project's additional files; no file under
`/Volumes/SSD/repos/StackWich` is written.

## Interpretation

The earlier result-only experiment did not improve whole-application CPU
because chart rendering dominated and only a few startup evaluations crossed
the boundary. The generated direct path changes that conclusion for live data:
each realtime DTO update now avoids JavaScript source generation, JSON
serialization/parsing, transient strings, blocking `Task.Run`, and the
associated GC pressure.

This is strong evidence for adopting the generated binary path for hot,
schema-known APIs. The unreleased native JSON ABI was subsequently removed.
Arbitrary evaluation and diagnostics now use a leased tagged result too;
JSON-compatible text is materialized only when a tooling caller explicitly
requests it.

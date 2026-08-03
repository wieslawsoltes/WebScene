# V8 Inspector production performance gate

The ordinary WebScene native package compiles V8 Inspector out. The opt-in
Inspector package is a separate diagnostic flavor. This gate checks that the
compile-time separation leaves the ordinary production runtime performance-neutral.

## Matched comparison

Captured on 2026-08-03 on macOS 26.4.1 arm64 with .NET SDK 10.0.201 and the
same pinned V8 15.3.10 `ReleasePartitionAlloc` SDK for both native libraries.
The control was built from `origin/main` at `8b5de34`; the candidate native
source was `9dde60b`, and the corrected managed probe was `ae9fe95`.

Both libraries used Release mode, html5ever, cssparser, Servo selectors,
generated DOM bindings, the bootstrap snapshot, PartitionAlloc, dense linking,
pointer compression, and the shared pointer-compression cage. Each variant ran
in a fresh process five times, alternating control then candidate. The table
reports the median of those five process-level measurements.

| Measurement | `origin/main` control | Inspector-disabled candidate | Delta |
| --- | ---: | ---: | ---: |
| Native library | 31,125,088 B | 31,125,568 B | +480 B (+0.0015%) |
| Prewarm | 1.9587 ms | 2.0640 ms | +5.3760% |
| Warm context create, mean of 10 contexts | 0.19414 ms | 0.18921 ms | -2.5394% |
| First scene, mean of 10 contexts | 1.26072 ms | 1.21479 ms | -3.6432% |
| Idle normalized process CPU | 0.32125% | 0.32479% | +0.00354 percentage points |
| Idle worker waits, four contexts/1.5 s | 283 | 282 | -0.3534% |
| 800 timers + 240 animation frames, elapsed | 3,203.5112 ms | 3,236.1385 ms | +1.0185% |
| 800 timers + 240 animation frames, process CPU | 39.154 ms | 39.975 ms | +2.0968% |
| 4,000 console calls, elapsed | 4.7147 ms | 5.0418 ms | +6.9379% |
| 4,000 console calls, process CPU | 16.312 ms | 14.701 ms | -9.8762% |
| Four 1,000-node DOM workloads, elapsed | 77.6997 ms | 77.2617 ms | -0.5637% |
| Four 1,000-node DOM workloads, process CPU | 311.205 ms | 311.068 ms | -0.0440% |
| Four-view incremental RSS | 5,177,344 B | 5,111,808 B | -1.2658% |
| Post-workload process RSS | 115,769,344 B | 116,047,872 B | +0.2406% |
| Total V8 used heap, four views | 1,272,384 B | 1,272,384 B | 0% |
| Total V8 physical heap, four views | 3,145,728 B | 3,145,728 B | 0% |

Every process completed exactly 800 timer callbacks, 240 animation-frame
callbacks, 4,000 console messages, four console completion signals, and four
DOM-workload completion signals. Both libraries reported build features `0`,
so neither measurement accidentally loaded the Inspector flavor. The control
and candidate SHA-256 values were respectively
`9918c4ec097f19e25b16e6b1ce90b4eb4f8e4f4da41ff7e848ed68cc5c7f69b0` and
`df65a2a3ec7cb414fd8f7c2c0eb6a7dcbb47f24020254e6cce98751a76cefc95`.

The sub-millisecond console delta moves opposite its process-CPU delta, while
the longer timer and representative DOM workloads remain within 2.1%. Startup,
V8 heap, and multi-view RSS are neutral or lower. This matched gate therefore
finds no material production-runtime regression from the compile-time Inspector
separation.

## Reproduce

Build the benchmark once in Release mode, then point each fresh process at one
native library:

```bash
dotnet build \
  benchmarks/WebScene.NativeEngine.Benchmarks/WebScene.NativeEngine.Benchmarks.csproj \
  -c Release

WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run \
  --project benchmarks/WebScene.NativeEngine.Benchmarks/WebScene.NativeEngine.Benchmarks.csproj \
  -c Release --no-build -- \
  probe native-inspector-disabled-performance
```

Use at least five alternating processes per variant. The defaults exercise four
concurrent contexts, ten startup samples, 1.5 seconds of idle time, 200 timers
and 60 animation frames per context, 1,000 console calls per context, and a
1,000-node representative DOM workload per context. Completion markers prevent
asynchronous script dispatch from shortening console or workload measurements.

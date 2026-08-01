# Sandwich multi-chart performance evaluation

**Status:** Complete

**Date:** 2026-08-01

This ledger records the evidence used to accept, reject, or defer every work
package in
[the performance roadmap](sandwich-multi-chart-performance-recommendations.md).
Focused results are not treated as whole-application wins. The 10% adoption
threshold is applied to the package, not used to discard every smaller result:
independently proven, low-risk improvements are retained when their benefits
can add together and they do not impose a material correctness, maintenance,
memory, or throughput tradeoff.

## Reproducible control

| Item | Value |
| --- | --- |
| HtmlML control revision | `ada5ca231f5664d3ad8210ddef654cd20816f41a` (detached exact-source worktree) |
| HtmlML candidate | The working-tree change set documented here, based on `ada5ca231f5664d3ad8210ddef654cd20816f41a` |
| Sandwich revision | `3359a99c985bb829fe24b2075e7fe4e4d389a8e4` |
| Configuration | Release, macOS arm64, V8 enabled, certification disabled |
| .NET SDK | 10.0.301 |
| CMake | 4.0.3 |
| Initial current-source library SHA-256 | `feab1e58398064d3daf240bc0325ef948d8ddbca06f83ac53300c40926db9bd7` |
| Detached `ada5ca2` control library SHA-256 | `38b9236421210f72b1f8fc04c3ca60940d759297a96e9debc795081e186f194d` |
| Final candidate library SHA-256 | `bae3c515aa0ba1820d130db45ad473ba48648acc1d59f225456f164be4b18137` |
| Control native package SHA-256 | `b52e825cd3a78fc25a8a460ae161610ce62afc0f01baca0cfcd4eae9a48aff90` |
| Candidate native package SHA-256 | `40629ce665578dd4b81381a07bbdb31569de3bb519e40bbd666720156248a19f` |

The exact-source native rebuild passed all four CTest targets. The managed
generated-realtime probe then produced this control result:

| Metric | Current control |
| --- | ---: |
| Mode / charts / calls | binary / 4 / 2,400 |
| Elapsed | 10,001.005 ms |
| Process CPU | 592.629 ms |
| Normalized CPU | 5.9257% (`0.0593` core) |
| Managed allocation | 10,040 bytes (`4.183` bytes/call) |
| Managed collections | 0 / 0 / 0 |
| Managed heap delta | 0 bytes |
| Working-set delta | 9,748,480 bytes |
| Result/request pool hits | 2,660 / 2,660 |
| Result/request pool misses | 4 / 4 |
| Outstanding results / active operation slots | 0 / 0 |

This probe validates the transport boundary; it is not a substitute for the
60-second, warmed Sandwich CPU/RSS acceptance workload.

## Recommendation disposition

| Work package | Current evidence | Status and next gate |
| --- | --- | --- |
| 1. Unified diagnostics | Native, interop, renderer, cache, memory, and compositor metrics already existed as separate APIs. A read-on-demand aggregate snapshot and same-context delta facility have been added. Opt-in native counters now cover timers, RAF, microtask checkpoints, worker wake reasons, scene builds, and generated/evaluation interop paths. Native and managed tests cover ABI sizing, monotonic deltas, invalid baselines, and path accounting. | **Accepted with profiling caveat.** Detailed counters remain off for unsampled contexts; the visible four-chart package comparison was within variance at +2.0% CPU and -0.6% RSS. The explicitly enabled path is for profiling and retains a focused saturated overhead risk; do not enable it continuously in production until an app-integrated enabled/disabled gate is below 1%. |
| 2. Scheduling | Compositor-demand RAF, one pending surface wake, coalesced publication, and unchanged-render suppression already exist. A measured four-context idle run found 940 timeout wakes in 5.01 seconds but only 26.556 ms process CPU (`0.0053` core), bounding the maximum benefit of removing the 16 ms watchdog well below the application adoption threshold. Visibility now dispatches `visibilitychange`; hidden documents run required timers but defer scene construction/publication until restored. Hidden four-chart ABBA measured 0.1621 candidate versus 0.2126 control cores (-23.8%) and 434.5 versus 626.6 MiB mean RSS (-30.7%). | **Accepted.** Retain the watchdog because its maximum saving is small and it protects V8 foreground tasks. Keep the standards-correct visibility and hidden scene-suppression changes; equivalent RAF/timer activity continued while settled presentation work stayed at zero. |
| 3. DOM/style/layout | Selector caching, common document properties, and duplicate ResizeObserver suppression already have focused wins. A root-scoped, generation-invalidated ID/name index now replaces recursive full-tree lookup while preserving duplicate tree order, mutation, detach, and parsed-document isolation. The focused 4,000-node/20,000-call medians improved from 156.698 to 0.779 ms for ID lookup (99.5%) and from 255.032 to 2.547 ms for window named lookup (99.0%). | **Partly accepted.** Retain the independently proven lookup indexes. Continue ranking style/layout work; do not broaden invalidation or computed-style caching without a symbolized consumer profile and dependency tests. |
| 4. Per-context memory | A 0/1/2/4-context, 2,000-row model measures about 63.3 MiB fixed post-prewarm RSS. In shared-isolate mode the populated increments are 56.8/68.0/83.4 MiB; four independent isolates use 86.2 MiB, so the existing shared mode saves about 2.8 MiB (3.3%) in this fixture. Per-context gauges attribute about 3.82 MiB to the native node pool, 2.2–3.3 MiB to the shard's V8 physical heap, 0.54 MiB to attributes, 0.39 MiB to the scene, and 0.17 MiB to wrappers. | **Measured/retain existing small win.** Keep shared-isolate mode available and include its saving in cumulative accounting, but do not force it as the default: a saturated 240,000-call probe used 13,187 versus 11,700 ms CPU (+12.7%), a material throughput tradeoff. The remaining linear owners are mutable context state; avoid speculative sharing. |
| 5. Generated binary interop | The four-context probe is correct, allocation-light, uses pooled request/result records, and ends with zero results and operation slots outstanding. Opt-in counters separate generated invokes/callbacks and encoded request bytes from arbitrary evaluation. A four-context cancellation probe completed 12,800 delayed operations with no faults and zero outstanding leases/slots in every context; a counter-enabled 1,280-operation run attributed every call to generated invoke and none to arbitrary evaluation. | **Accepted/audited.** Retain the existing ABI and pool. The stress probe now makes cancellation, path attribution, and final drain explicit; no replacement transport is justified. |
| 6. Assets/fonts/images | Resource, script, and selector caches exist and are bounded. The process-static family-only web-font map allowed cross-document aliasing and never evicted entries. Web font family maps are now engine-owned; immutable typefaces are shared by SHA-256 content identity while referenced, then disposed when the final engine releases them. Cache entries/references/hits/misses are exposed in the aggregate snapshot. Tests prove same-content reuse, different-content isolation under the same authored family, and release. | **Correctness fix accepted.** Retain the scoped/ref-counted cache. Cold/warm startup and decoded-image sharing remain measurement-gated because the current consumer profile does not attribute a material cost to them. |
| 7. Composition/damage | Retained replay, damage-bounded invalidation, full-frame fallbacks, a one-wake UI gate, and unchanged-render suppression already avoid the known fresh-frame clipping failure. Prior attribution places Avalonia/Skia at about `0.0100` core, below native/V8, and the aggregate snapshot now exposes wake/render/diff/invalidation/full-frame/unchanged-render counters. | **Measured/deferred.** Keep the existing additive optimizations and counters. A new partial-frame architecture is rejected for now because its maximum attributed upside is small and the earlier candidate had black-region/flicker correctness failures. Re-open only with refreshed composition attribution and pixel tests. |
| 8. Lifecycle/visibility | A headless native-view acceptance probe moves one loaded view between two parents, then into a second window, toggles presentation inactive/active, and disposes it. The diagnostic context ID remains `1`, JS state advances `41 -> 42 -> 43`, a hidden zero-delay timer reaches `44`, and shared-isolate slot `0` is reused after disposal while the sentinel returns to its baseline occupancy. The real four-chart consumer also completed resize, minimize/restore, and detach/reattach of all four charts without reload or error. | **Accepted.** Stable compatible reparenting, floating-window state, hidden JS progress, visibility recovery, context disposal, shared-slot reuse, and consumer reparenting are automated. Add a longer repeated RSS plateau only if a future soak indicates retained growth. |

## Commands

```sh
cmake --build artifacts/native-engine-runtime-build/osx-arm64 -j 6
ctest --test-dir artifacts/native-engine-runtime-build/osx-arm64 \
  -C Release --output-on-failure

dotnet build \
  benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-restore

WEBSCENE_NATIVE_ENGINE_PATH="$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib" \
  dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe generated-realtime-chart --mode binary --charts 4 --ticks 600 --rate 60

dotnet test \
  tests/JavaScript.Avalonia.Tests/JavaScript.Avalonia.Tests.csproj \
  -c Release --no-restore \
  --filter FullyQualifiedName~NativePerformanceSnapshotTests
```

The consumer package runs used:

```sh
SANDWICH_CHART_BENCHMARK_REPLAY=1 \
DOTNET_ThreadPool_UnfairSemaphoreSpinLimit=0 \
  ./SandwichDesktop --chart-benchmark-2x2

SANDWICH_CHART_BENCHMARK_REPLAY=1 \
SANDWICH_CHART_BENCHMARK_HIDE_AFTER_READY=1 \
DOTNET_ThreadPool_UnfairSemaphoreSpinLimit=0 \
  ./SandwichDesktop --chart-benchmark-2x2
```

Each lane waited for `BENCHMARK_READY`, warmed for 30 seconds, then used the
process CPU-time delta over 60 seconds and 60 one-second RSS samples. Control
and candidate were published from the same Sandwich revision and executed in
ABBA order.

## Deterministic four-chart acceptance

### Visible

| Order | Variant | CPU cores | Mean RSS | Settled rendered scenes |
| ---: | --- | ---: | ---: | --- |
| 1 | Control A1 | 0.35233 | 629.59 MiB | 120/120/120/120 per 30 s |
| 2 | Candidate B1 | 0.37350 | 620.87 MiB | 120/120/120/120 per 30 s |
| 3 | Candidate B2 | 0.37383 | 615.27 MiB | 120/120/120/120 per 30 s |
| 4 | Control A2 | 0.38000 | 614.01 MiB | 120/120/120/120 per 30 s |

Control averaged `0.36617` core and `621.80 MiB`; candidate averaged
`0.37367` core and `618.07 MiB`. The candidate delta is +2.0% CPU and -3.73
MiB (-0.6%) RSS. The ordering reverses the apparent direction between the
first and second pair, while every settled interval performs the same 480
renders. Treat this as variance: the package neither establishes a visible
application win nor a measurable regression.

### Hidden after ready

| Order | Variant | CPU cores | Mean RSS | Settled renders/diffs/invalidations |
| ---: | --- | ---: | ---: | --- |
| 1 | Control A1 | 0.20683 | 626.59 MiB | 0 / 0 / 0 |
| 2 | Candidate B1 | 0.14333 | 449.85 MiB | 0 / 0 / 0 |
| 3 | Candidate B2 | 0.18083 | 419.23 MiB | 0 / 0 / 0 |
| 4 | Control A2 | 0.21833 | 626.61 MiB | 0 / 0 / 0 |

Control averaged `0.21258` core and `626.60 MiB`; candidate averaged
`0.16208` core and `434.54 MiB`: -23.8% CPU and -30.7% RSS. Both variants
continued approximately 960 RAF scheduler turns per settled 30 seconds while
presenting no frames. The candidate additionally suppresses native scene
construction/publication and exposes browser-compatible visibility to the
application. The unusually large RSS drop is therefore recorded as an
observed product response to correct hidden state, not attributed solely to a
native allocator change.

## Deferred evidence and next profile

- The consumer harness currently exposes empty or four-chart modes, not one-
  and two-chart modes. The native 0/1/2/4-context memory model supplies the
  ownership/scaling evidence; add consumer chart-count selection before the
  next broad startup study.
- Cold/warm decoded-image and application-snapshot sharing remain deferred
  because the current profile does not attribute a material cost to them.
- Style/observer duration counters and decoded-image cache attribution remain
  useful diagnostics extensions, but adding unconditional timing to those hot
  paths would conflict with the disabled-overhead requirement.
- Refresh the deterministic Time Profiler after these changes. Native/V8 was
  the largest measured bucket and named-property/DOM lookup was its largest
  actionable symbol; the lookup is now fixed, so the next native symbol must
  be measured rather than guessed.

## Focused diagnostics overhead

The detached control and an intermediate diagnostics candidate were run in
ABBA order with 240,000 generated calls over approximately ten seconds per
lane. All lanes delivered 60,064 updates to each of four contexts and ended
with zero outstanding results.

| Detailed-counter policy | Control mean CPU | Candidate mean CPU | Delta |
| --- | ---: | ---: | ---: |
| Unconditionally collected | 10,724.683 ms | 11,078.584 ms | +3.30% |
| Explicitly enabled; disabled for this run | 11,651.849 ms | 11,820.403 ms | +1.45% |

The unconditional design was rejected. The retained design leaves detailed
counters off until the snapshot API enables them. The saturated focused delta
is not a four-chart application result and remains above the strict 1% target.
The final normal consumer package comparison, where unsampled contexts keep
the counters disabled, was within run-order variance. Treat continuous enabled
collection as profiling-only until an app-integrated enabled/disabled ABBA run
clears 1%; this caveat does not add overhead to normal production contexts.

## Rejected scheduling hypothesis: longer idle watchdog

Command:

```sh
WEBSCENE_NATIVE_ENGINE_PATH="$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib" \
  dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe native-runtime-work --contexts 4 --seconds 5
```

Result: 944 waits, 940 timeout wakes, no timer or RAF callbacks, one startup
scene build per context, and 26.556 ms process CPU over 5,010.066 ms. Removing
all timeout wakes could save at most about `0.0053` core in this fixture and
would risk delaying V8 foreground tasks that have no host notification edge.
The scheduler watchdog is therefore retained.

## Accepted DOM lookup indexes

Commands (run against the detached control and the candidate library):

```sh
WEBSCENE_NATIVE_ENGINE_PATH="<library>" \
  dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe native-dom-lookup --kind id --nodes 4000 --lookups 20000 --samples 7

WEBSCENE_NATIVE_ENGINE_PATH="<library>" \
  dotnet run \
  --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
  -c Release --no-build -- \
  probe native-dom-lookup --kind named --nodes 4000 --lookups 20000 --samples 7
```

| Operation | Control median | Candidate median | Change |
| --- | ---: | ---: | ---: |
| `Document.getElementById`, alternating late hit/miss | 156.698 ms | 0.779 ms | -99.5% (`201.2x`) |
| Window named property, alternating late hit/miss | 255.032 ms | 2.547 ms | -99.0% (`100.1x`) |

The index is built only when the corresponding API is used, is scoped to the
specific document root, keeps the first tree-order entry for duplicate keys,
and is discarded after the native document generation changes. Native tests
exercise duplicate reorder, ID/name mutation, detach, document named-form
filtering, and separate `DOMParser` roots. All four native CTest targets pass.

## Context memory and isolate policy

The `native-context-memory` probe prewarms V8, creates 0/1/2/4 documents, and
builds a 2,000-row (4,003-node) model in every context. Post-prewarm fixed RSS
is approximately 63.3 MiB.

| Contexts | Independent isolates | Shared-isolate mode |
| ---: | ---: | ---: |
| 1 | 56.9 MiB | 56.8 MiB |
| 2 | 69.3 MiB | 68.0 MiB |
| 4 | 86.2 MiB | 83.4 MiB |

The existing sharded shared-isolate mode saves approximately 2.8 MiB (3.3%)
at four contexts. It remains opt-in: a saturated 240,000-call generated-interop
run used 13,187 ms CPU shared versus 11,700 ms independent (+12.7%). This is a
real throughput tradeoff, so the small memory win is retained as an option and
in cumulative accounting rather than forced globally.

## Generated interop and lifecycle stress

The final four-context cancellation run launched 12,800 delayed generated
operations, disposed every invoker immediately, and completed with 12,800
cancellations, zero faults, and zero outstanding results, slots, or leases in
all contexts. Counters reported 12,800 generated calls, 614,400 encoded request
bytes, and zero arbitrary-evaluation calls.

The headless lifecycle probe passed in both modes. Independent mode preserved
context identity and state; shared mode additionally returned the active
context count from two to the sentinel baseline of one and reused slot zero.
The packaged Sandwich lifecycle run reached ready, resized twice, minimized and
restored, then detached and reattached all four charts before reporting
`reparent-complete` without a chart or runtime error.

## Regression and visual gates

- Native parser/runtime CTest: 4/4 passed in independent-isolate mode and 4/4
  passed with two shared-isolate shards.
- Managed Avalonia tests: 354/354 passed on .NET 8 and 354/354 on .NET 10.
- The managed suite includes retained-canvas reference-pixel checks,
  headless text pixel tolerance, SVG raster coverage, responsive visual
  comparisons, damage fallback, resize, and clipped-layer regression tests.
- Scoped web-font tests: 7/7 passed on each target framework, including
  same-content reuse, different-content isolation under the same family name,
  reference accounting, and final release.
- Benchmark project: Release build passed with zero warnings and zero errors.
- `git diff --check` passed.

## Rejected or deferred hypotheses

- **Remove/lengthen the worker watchdog:** rejected; its entire measured idle
  cost is only 0.0053 core and it is the V8 foreground-task safety net.
- **Make shared-isolate mode the global default:** rejected; 3.3% fixture RSS
  saving does not justify a 12.7% saturated interop throughput regression.
- **Broaden computed-style/layout memoization now:** deferred; invalidation
  complexity is high and no refreshed symbolized profile ranks it above lookup.
- **Build a new partial-frame renderer:** rejected at this evidence level;
  composition attribution was about 0.0100 core and the previous experiment
  produced black regions and flicker.
- **Share mutable context owners:** rejected on semantics and isolation grounds.
- **Add decoded-image/application-snapshot sharing speculatively:** deferred
  until cold/warm startup attribution demonstrates a material duplicated cost.

The next action is a fresh deterministic Time Profiler capture of the final
package. The former top actionable native path—repeated ID/named-property tree
scans—is now approximately 100-200x faster, so selecting another optimization
without re-profiling would be guesswork.

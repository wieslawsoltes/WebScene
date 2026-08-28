# Concurrent WebScene Engine Design

## Purpose

WebScene applications may host several independent JavaScript components at the
same time. A common case is multiple TradingView charts loading the same
scripts and resources concurrently. The runtime should preserve isolation
between components without repeating expensive work for identical inputs.

The primary target is:

> Concurrent requests for an identical compilation unit compile once. One
> producer performs the work, all other requesters wait for that result, and
> every requester reuses the greatest amount of immutable data that V8 safely
> permits.

This design covers compilation and resource reuse inside one process first.
Cross-process coordination and shared-isolate hosting are later extensions.

## Existing Behaviour

WebScene currently provides several useful layers of reuse:

- V8, its platform and ICU data are initialized once per process.
- Each native engine owns a separate V8 isolate, context, heap, DOM and event
  loop.
- Each isolate retains an in-memory cache of `v8::UnboundScript` handles.
- V8 code-cache data is persisted in content-addressed `.v8cache` files.
- Resource responses are persisted separately and have a bounded process-wide
  memory cache.
- Cache entries include integrity checks and a V8 cache-version tag.
- Persistent writes use uniquely named temporary files followed by rename, so
  readers cannot observe a partially written final entry.

The compilation key contains the WebScene runtime identity, document name and
exact source bytes. Consequently, different scripts cannot alias merely
because they use the same URL.

The native engine now coordinates concurrent cold misses process-wide. The
first isolate becomes the producer for a compilation digest; other isolates
wait without holding the coordinator lock and then consume the same immutable
V8 cached-data buffer. The ready buffers are bounded by entry and byte limits.
Resource callbacks use the same per-key single-flight rule, while unrelated
scripts and URLs remain parallel.

The remaining density cost is isolate-local state. Each chart still owns its
context, heap, DOM, retained scene, timers and bound scripts. Cached-data bytes
can cross isolates, but V8 handles and executable objects cannot.

## Required Invariants

1. Within one process, only one producer compiles a given cold compilation key
   at a time.
2. Other requesters wait for the producer without holding a global cache lock.
3. Success, rejection and failure are delivered consistently to every waiter.
4. A failed or destroyed producer cannot leave waiters blocked indefinitely.
5. Cache entries remain bounded and evictable.
6. An isolate never receives a V8 handle created by another isolate.
7. Persistent cache files remain valid across concurrent readers and writers.
8. Different compilation keys proceed independently.
9. Cache coordination must not serialize unrelated engines, input delivery or
   rendering.
10. Metrics must distinguish compilation, waiting, bytecode consumption and
    execution.

## Process-Wide Single-Flight Compilation

The native runtime has a process-wide compilation coordinator keyed by the
existing compilation digest.

Each coordinator entry has one of these states:

- `producing`: one engine is compiling while zero or more engines wait;
- `ready`: immutable cached-code bytes are available;
- `failed`: the producer failed and the failure is available to waiters.

The shared result should contain:

- the compilation key and V8 cache-version tag;
- an immutable, reference-counted byte buffer containing V8 cached-code data;
- source identity metadata needed for validation;
- success or structured failure information;
- timestamps and byte counts for metrics.

The first requester atomically installs a `producing` entry and becomes its
producer. Later requesters obtain a future or condition associated with that
entry, release the coordinator lock, and wait. The producer compiles in its
own isolate, calls `v8::ScriptCompiler::CreateCodeCache`, publishes the
immutable result, persists it once, and wakes all waiters.

Waiters then consume the same immutable byte buffer using
`v8::ScriptCompiler::kConsumeCodeCache`. The pinned V8 API also supports
background code-cache consumption, which should be evaluated so waiting
isolates can move deserialization work away from their latency-sensitive
runtime thread.

The coordinator lock must protect only entry lookup and state transitions. It
must never be held while compiling, reading a large file, writing a cache file
or entering V8.

## What Can Be Shared

| Asset | Current or proposed scope | Notes |
| --- | --- | --- |
| Native engine library code | Process and operating-system mapping | One loaded image serves all instances in a process. |
| V8 platform and ICU data | Process | Already initialized once. |
| Fetched resource bytes | Process and persistent disk cache | Process cache is bounded; immutable backing storage can reduce copies further. |
| JavaScript source bytes | Process | Proposed immutable source blobs avoid one full source copy per instance. |
| V8 cached-code bytes | Process and persistent disk cache | Proposed single-flight result; safely consumable by compatible isolates. |
| `v8::UnboundScript` | One isolate | A V8 handle cannot cross isolate boundaries. |
| Bound script, context, heap and DOM | One engine/context | Required for independent global state and DOM ownership. |

Single-flight therefore means one full source compilation. Separate isolates
still consume the cached-code data and instantiate isolate-local V8 objects.
It does not imply that an `UnboundScript` or all generated machine-code objects
can be shared directly across isolates.

## Shared-Isolate Hosting

Literal sharing of an `UnboundScript` is possible only when multiple component
contexts live in the same isolate. A script can then be compiled once in that
isolate and bound to each context.

This should be treated as an optional high-density mode rather than the
default:

- JavaScript execution within an isolate is serialized.
- A long-running component can delay every context in the isolate.
- Garbage collection and fatal isolate failures affect the whole group.
- WebScene currently stores runtime state on the isolate; multi-context hosting
  would require context-local embedder state and callback routing.
- Scheduling, timer ownership, microtask checkpoints and DOM bindings would
  need explicit per-context isolation.

If measurements justify it, use an isolate pool where each isolate hosts a
small bounded number of contexts. This provides code sharing within a shard
while retaining parallelism and limiting failure coupling between shards.

V8 places the current isolates in its default `IsolateGroup`, which is its
most memory-efficient group configuration. An isolate group shares lower-level
V8 infrastructure, but it does not make ordinary V8 handles transferable
between isolates.

## Persistent and Cross-Process Coordination

The current temporary-file and rename protocol prevents partial final files.
Hash and version validation protect readers from invalid cache content.

Single-flight initially coordinates only engines in the same process. Two
different application processes may still compile the same cold unit. If
cross-process duplication is material, add a per-key lock file with:

- atomic ownership acquisition;
- owner process identity;
- a bounded lease or stale-owner recovery;
- final-cache recheck after acquiring the lock;
- timeout and fallback compilation;
- platform-specific validation on macOS, Linux and Windows.

Cross-process locking must be justified by measurement because its recovery
and filesystem semantics are substantially more complex than process-local
coordination.

## Lifetime, Failure and Eviction

- A coordinator entry remains alive while a producer or waiter references it.
- Ready byte buffers use reference counting and can outlive their map entry.
- The coordinator applies entry-count and byte-count limits independently of
  each isolate's `UnboundScript` cache.
- Completed entries participate in an LRU policy. Producing entries are never
  evicted.
- Producer exceptions publish a structured failure before leaving the entry.
- A rejected V8 cache result invalidates the ready entry and persistent file.
  At most one requester is elected to rebuild it.
- Runtime shutdown cancels only that runtime's wait. It does not cancel a
  producer still needed by other engines.
- Waiting must have diagnostics and a bounded shutdown path, but normal
  compilation should not be given an arbitrary short timeout.

## Resource Sharing

The existing process resource cache avoids repeated network and disk reads,
but returning resources by value can still duplicate large strings. It can be
evolved to store immutable, reference-counted resource bodies while keeping
headers and freshness metadata small.

Resource loading uses the same single-flight principle:

- one in-flight fetch or disk read per resource key;
- concurrent engines await the same immutable result;
- conditional revalidation is performed once;
- failure and cancellation follow the compilation rules;
- component-specific response objects reference shared body storage without
  sharing mutable DOM or JavaScript state.

JSON resource parsing is separate from JavaScript compilation. Source text
that is executed as JavaScript uses the compilation coordinator; fetched JSON
uses the resource coordinator and is parsed into each isolate's own object
graph.

Resource bodies are immutable reference-counted strings in the process cache.
JavaScript compilation preserves that backing through V8 external strings;
HTML/CSS parsers still materialize engine-local structures because their
resulting DOM and cascade state are intentionally independent.

## Metrics

Expose aggregate and per-engine counters for:

- compilation requests;
- memory script-cache hits;
- persistent code-cache hits and misses;
- single-flight producers;
- single-flight waiters;
- duplicate cold compilations;
- producer compilation duration;
- waiter duration;
- cached-code consumption duration;
- background-consumption duration;
- cached-code bytes shared, read and written;
- cache rejection and rebuild counts;
- resource producers, waiters and shared bytes;
- isolate heap usage and process resident-set size;
- incremental memory cost per additional engine.

The success target for identical simultaneous cold requests is exactly one
producer, all remaining requests recorded as waiters, and zero duplicate cold
compilations.

The native ABI exposes additive process-cache metrics for:

- compilation memory hits, leaders, waiters and shared cached-data bytes;
- resource memory hits, load leaders, waiters and shared response bytes.

The four-chart TradingView sample aggregates these counters together with
working set, managed heap size, process thread count and per-engine resource
cache totals.

The native ABI also exposes a worker-thread memory snapshot for each engine:
V8 used, total, executable, physical, external and malloced bytes plus the
latest retained scene size. Process compilation/resource cache bytes are
reported separately and must be counted once, not once per engine.

The sample's `--multi-instance-probe` mode loads charts sequentially, retains
all of them, and writes a machine-readable report at 0, 1, 2 and 4 instances.
Three matched cold-process macOS arm64 Debug runs on 2026-07-24 reported the
following medians:

| V8 configuration | Four-chart RSS | V8 used heap | V8 physical heap | Resize inputs/second/chart |
| --- | ---: | ---: | ---: | ---: |
| Existing uncompressed build | 519.7 MiB | 150.5 MiB | 197.7 MiB | 55.6 |
| Pointer compression + shared cage | 448.4 MiB | 102.0 MiB | 137.1 MiB | 56.9 |
| Pointer compression + `--optimize-for-size` | 416.2 MiB | 94.9 MiB | 106.4 MiB | 55.9 |

The matched process baseline was 113.9 MiB. Pointer compression plus
`--optimize-for-size` therefore saved 103.5 MiB of total process RSS (19.9%)
and 25.5% of the RSS added by the four charts. It reduced aggregate V8 used
heap by 37.0% and physical V8 heap by 46.2%.

All three configurations applied 120/120 animation frames by the two-second
60 Hz deadline with no pending input. Concurrent pan applied 60.5 pointer
moves per second per engine, drained its final coalesced move and changed every
visible range. Median resize CPU was about 10.1 CPU-seconds over a two-second
wall-clock interval, or roughly five cores. Pointer compression did not
materially increase this cost. The size-optimized variant completed first
chart startup in a median 1.54 seconds versus 1.77 seconds for the uncompressed
build, but longer soak/interaction runs are still required before making the
size flag a release default.

The native-allocation passes reduced `dom_node` from 3,384 to 1,304 bytes by
moving pseudo-element styles, canvas state, animation definitions and runtime
state, custom properties, background-image state, grid state and authored
inline styles into cold allocations. Across the four-chart workload this
reduced fixed inline DOM storage from 11.71 MiB to 4.51 MiB. Only 420 of 3,628
live nodes allocate animation definition/runtime state. Including all retained
cold states, the measured native DOM total is about 7.08 MiB.

Parsed CSS rule payloads are now immutable and interned process-wide while
stylesheet ownership, media-query matches and selector indices remain
engine-local. Four charts still expose 10,428 logical rules, but these resolve
to 2,607 shared payloads. Measured CSS rule/index storage fell from 7.60 MiB
to 2.43 MiB, saving 5.16 MiB. A native multi-engine regression test verifies
that identical live stylesheets share payloads.

With pointer compression, `--optimize-for-size`, a 48 MiB experimental
per-isolate maximum heap and those native allocation changes, three cold runs
reported these working-set medians:

| Ready charts | Process working set | Increase from previous milestone |
| ---: | ---: | ---: |
| 0 | 115.8 MiB | — |
| 1 | 280.5 MiB | 164.7 MiB |
| 2 | 332.9 MiB | 52.4 MiB |
| 4 | 404.9 MiB | 72.0 MiB total, 36.0 MiB/chart |

At four charts the amortized increase over the empty sample is 72.3 MiB per
chart. This is not the steady-state marginal cost: the first chart also pays
for process-wide native/rendering initialization and shared resources. The
current four-chart exact buckets include 106.4 MiB of V8 physical heap,
7.08 MiB of native DOM, 2.43 MiB of CSS, roughly 10--11 MiB of managed heap,
0.45 MiB of retained scene data and 5.10 MiB of process caches. The remaining
working set must be attributed before choosing the next large optimization;
likely contributors include Skia/Avalonia surfaces and caches, native
allocator fragmentation, stacks and resident executable/data pages.

A macOS `vmmap` comparison measured about 162.3 MiB of physical footprint for
the shell-only process and about 629 MiB with four live charts. It identified
large graphics/surface regions and substantial default-malloc dirty or swapped
pages; one snapshot attributed roughly 69 MiB, or 47%, of its default-malloc
footprint to fragmentation. Resident thread stacks increased by only about
1.2 MiB. Disabling the retained Avalonia composition visual and enabling the
macOS nano allocator did not materially lower total footprint, so neither is a
production memory strategy.

The compositor also previously treated an incremental scene publication with
zero changed layers and zero damage rectangles as a full-frame invalidation.
TradingView publishes these synchronization diffs on animation frames. The
host now acknowledges them without rendering: a live trace changed from 300
full-frame-like invalidations totalling about 98% viewport damage to 1,800
accepted publications containing only 77 changed layers, 107 real damage
rectangles and about 4.9% summed viewport damage. This primarily reduces
CPU/GPU work and power rather than retained surface memory.

## V8 Density Variants

The original package build explicitly disabled V8 pointer compression. That
is unusual for 64-bit arm64/x64 V8. The release build now enables pointer
compression and its process-wide shared cage, and its package manifest records
both requirements so a stale uncompressed monolith cannot be published under
the same profile. V8's published measurements report up to 43% smaller V8
heaps and up to 20% lower renderer-process memory, with real-world CPU/GC
improvements:

- <https://v8.dev/blog/pointer-compression>
- <https://v8.dev/blog/v8-release-92>

The shared cage also gives V8 a process-wide code range, but does not make
ordinary isolate-local heap objects or handles transferable.

The runtime has experimental controls for:

- `WEBSCENE_V8_MAX_HEAP_MIB` and `WEBSCENE_V8_INITIAL_HEAP_MIB`, translated into
  `v8::ResourceConstraints` before isolate creation;
- `WEBSCENE_V8_MEMORY_SAVER`, translated into `Isolate::SetMemorySaverMode`;
- `WEBSCENE_V8_OPTIMIZE_FOR_SIZE`, translated into V8's process-start
  `--optimize-for-size` flag. Release packages default this to enabled; setting
  the value to `0`, `false`, `no` or `off` disables it before the first engine
  initializes. In the pinned V8 this reduces the maximum semi-space size to
  1 MiB without disabling JIT;
- `WEBSCENE_V8_PLATFORM_THREADS`, which bounds the one process-wide V8 worker
  pool;
- platform idle-task support is enabled by default and is budgeted only after
  observable browser task sources drain. `WEBSCENE_V8_DISABLE_IDLE_TASKS`
  disables it for controlled comparisons; the earlier
  `WEBSCENE_V8_IDLE_TASKS` opt-in is no longer required.

Heap limits are safety/GC-frequency controls, not free memory reductions. A
limit below the chart's live set will increase GC work and can terminate the
process if V8 cannot recover. The JavaScript stack limit similarly bounds
recursion; it does not shrink the operating-system stack reserved by
WebScene's `std::jthread`. Measurements show the V8 platform threads are mostly
process-wide and additional engines add roughly one worker thread each, so
thread-stack tuning is lower priority than heap representation and chart
grouping.

V8 permits several contexts in one isolate, but only one thread at a time may
execute that isolate. The four-isolate resize probe currently consumes about
five cores to maintain cadence. Therefore a single-isolate/four-chart mode
cannot execute the same simultaneous JavaScript work in parallel and is
unlikely to preserve 60 Hz during resize or other all-chart activity.

The first shared-engine spike also found a more immediate constraint:
WebScene currently owns one active iframe context and one mutable iframe CSS
rule set per engine. Four sibling TradingView iframes cannot yet reach
independent ready states inside one engine. Supporting that topology requires
per-frame contexts, CSS state, timers, listeners, microtask ownership and DOM
callback routing; it is not a scheduler-only switch.

The candidate production policy is therefore adaptive:

1. keep independently active charts in separate isolates;
2. retain the default `IsolateGroup` and shared pointer-compression cage so
   V8 uses its most memory-efficient multi-isolate configuration;
3. completed for the enabled macOS lane: use pointer compression and verify
   the profile in the release package manifest; repeat platform validation
   before re-enabling Windows and Linux lanes;
4. use `--optimize-for-size` as the release default with an explicit process
   opt-out, while retaining soak and GC-jank gates;
5. toggle memory-saver mode and throttle animation/timers for obscured or
   inactive charts;
6. allow a bounded two-context shard only after multi-frame context isolation
   exists and its measured frame-time gate passes;
7. never place an untrusted or latency-dominating component in another
   component's isolate.

The benchmark must compare one isolate with four contexts, two isolates with
two contexts each, and four independent isolates. The decision metric is not
RSS alone: startup, p95/p99 host frame interval, missed applied frames, input
queue depth, resize presentations, CPU, GC pauses and failure coupling are all
gates.

## Next Density Layers

After pointer compression, the largest remaining opportunities do not come
from shrinking JavaScript's recursion limit:

1. Completed: share immutable parsed CSS rule payloads process-wide while
   retaining small engine-local ownership, media and selector-index state.
2. Completed for large ASCII JavaScript: back V8 external strings with one
   immutable, weakly interned process allocation. The external resource keeps
   the backing alive only while at least one isolate references it, and V8
   heap snapshots identify the allocation as shared. Non-ASCII UTF-8 input
   deliberately retains the decoding path; a later two-byte representation
   must prove that its conversion/storage cost is worthwhile.
3. Memoize immutable market-history payloads process-wide. A shared
   `SharedArrayBuffer` backing store can expose binary data to multiple
   isolates, although TradingView's expected bar objects will still be
   materialized per isolate unless its datafeed boundary changes.
4. Add an active/background engine policy. An active chart keeps normal V8
   tuning and full frame delivery; hidden charts enter memory-saver mode,
   receive a reduced animation cadence and may receive a low-memory
   notification after an idle grace period.
5. Evaluate a custom context snapshot for WebScene's stable host bootstrap. This
   can improve creation time, but deserialized mutable TradingView heaps remain
   isolate-local, so snapshots must not be counted as live heap sharing.
6. Measure a V8 build without unused optional subsystems such as WebAssembly
   only after feature-inventory evidence proves the hosted component never
   depends on them. Intl/ICU must remain because the current page calls
   `Intl.DateTimeFormat`.

The external-source implementation is covered by a four-engine native test
that requires all isolates to retain external source, at least `N - 1`
process-source hits and attribution of the shared bytes. Three four-chart
TradingView runs reused 93 source instances and 4.00 MiB of repeated source
references. The unique backing is about 1.33 MiB; aggregate external source
reported across four isolates is 5.33 MiB. Median V8 used heap fell from
94.9 MiB to 89.9 MiB and median V8 physical heap from 106.4 MiB to 100.4 MiB.
The first implementation left a second copy in the process resource cache, so
four-chart RSS remained within noise at 408.6 MiB versus 408.8 MiB even though
all three runs passed animation, resize and pan gates.

The resource cache and V8 external strings now retain the same immutable
`shared_ptr` backing. Cache hits no longer copy multi-megabyte source through a
temporary per-engine `std::string`, which also avoids dirty allocator pages
left by those transient allocations. Three fully passing cold runs measured
the following medians (one additional run missed the resize threshold only at
54.999997 applied inputs/second with no queued work):

| Ready charts | Process working set | Increase |
| ---: | ---: | ---: |
| 0 | 113.5 MiB | — |
| 1 | 283.9 MiB | 170.4 MiB |
| 2 | 323.1 MiB | 39.2 MiB |
| 4 | 391.7 MiB | 68.6 MiB total, 34.3 MiB/chart |

The current amortized four-chart cost is 69.5 MiB/chart and the steady
marginal cost after the first is about 35.9 MiB/chart. This is 13.2 MiB below
the pre-external-source 404.9 MiB median and 17.0 MiB below the intermediate
duplicate-backing implementation. Median V8 used and physical heaps are
89.9 MiB and 101.2 MiB respectively. All fully passing runs sustained the
animation, resize and concurrent-pan gates with empty input queues.

The managed Skia renderer now also reports its retained canvas topology. The
four-chart sample retains 28 non-empty canvas display lists containing 5,584
commands and representing 12.50 MiB of logical RGBA canvas area. Conservative
command analysis found that 16 layers, representing 6.35 MiB, contain
destructive operations that require an independent offscreen isolation layer;
the other 12 can be replayed directly after redundant leading clears are
removed. Three animation/resize/pan runs passed after enabling that path. The
median four-chart RSS was 390.6 MiB, only 1.1 MiB below the preceding median,
so this is not counted as a material live-memory reduction; its principal
value is avoiding unnecessary transient render surfaces.

SVG pictures used by the DOM renderer are now immutable and reference-counted
across chart renderers. The four-chart workload requested 81 SVG-picture
references but contained only 27 unique pictures, producing 54 in-process
memory hits. The cache owns no permanent entries: the last renderer lease
removes and disposes each picture. Renderer reset now also disposes cached
typefaces, pictures and SVG leases immediately on engine replacement or
surface detachment rather than relying on managed finalization. Three
four-chart runs with shared SVG pictures all passed the cadence and pan gates;
their median shell/one/two/four working sets were 116.0, 272.2, 324.8 and
390.5 MiB respectively. The RSS difference remains near measurement noise,
but the 3:1 SVG object deduplication and bounded lifecycle are directly
observed.

Native DOM nodes now come from a document-owned fixed-size pool with bounded
chunks. Clearing or navigating a document destroys the live nodes and releases
the pool's high-water allocation before recreating the body. A generic
mixed-size PMR pool was also measured and rejected: its retained hash buckets
raised median four-chart RSS from 386.6 MiB to 391.5 MiB. Further pooling must
therefore remain lifetime- and type-specific rather than becoming a global
allocator layer.

Element attributes now use a compact contiguous collection instead of one
node-based hash table per element, and text-selection direction is a byte enum
instead of a repeated heap-backed string. Together with the earlier cold-state
extraction, `dom_node` is now 1,264 bytes. The current four-chart document has
3,628 nodes, so fixed inline node storage is 4.37 MiB, 142 KiB below the
1,304-byte layout and 7.34 MiB below the original 3,384-byte layout. Its 9,208
attributes occupy 1.14 MiB of measured backing storage. Native tests cover map
semantics, pool release/reinitialization and ABI memory attribution.

The engine ABI also exposes a non-blocking low-memory request. The request is
consumed on the owning engine thread before V8 receives
`LowMemoryNotification()`, preserving isolate thread affinity. Four explicit
post-ready notifications reduced median aggregate V8 used heap by about
4.23 MiB and physical V8 heap by about 9.55 MiB. Process RSS remained too noisy
to claim an equivalent reduction. The notification is therefore an
inactive/detached-chart policy tool, not a per-frame or active-chart action.

Three final cold runs of the uncapped dense profile (pointer compression,
shared cage and `--optimize-for-size`) measured:

| Ready charts | Median process working set | Increase |
| ---: | ---: | ---: |
| 0 | 113.7 MiB | — |
| 1 | 269.6 MiB | 155.9 MiB |
| 2 | 317.5 MiB | 47.9 MiB |
| 4 | 386.0 MiB | 68.5 MiB total, 34.3 MiB/chart |

This makes the four-chart amortized cost 68.1 MiB/chart and the observed
post-second-chart marginal cost about 34.3 MiB/chart. Two runs passed every
strict cadence gate; one narrowly missed the resize gate when one engine
applied 106 rather than the required 108 updates by the two-second deadline,
then drained to zero queued work. A separate build with this policy compiled
in as its default passed all gates at 380.8 MiB. The same source with the
ordinary uncompressed V8 build measured 113.8, 293.8, 365.8 and 471.6 MiB at
the same milestones. Pointer compression plus the dense V8 policy therefore
saved about 85.6 MiB at four charts without a heap cap. Heap caps remain
containment rather than a density default and require long-soak headroom
evidence.

The probe now supports eight independent chart engines in one fixed
1,920-by-1,200 surface. Its cold dense run reached 516.2 MiB and its warm run
reached 508.9 MiB. Charts five through eight therefore added 120.1 MiB cold,
or 30.0 MiB/chart. Warm persistent caches reduced first-chart startup from
4.46 seconds to 0.68 seconds and total setup from 16.8 to 12.6 seconds.
Animation and concurrent pan still sustained 60 updates/s/engine, but
simultaneous resize reached only 50.1 cold and 49.7 warm. Resize consumed about
88% of the machine's aggregate CPU capacity in the warm run, drained its
queues after the deadline and changed every viewport. The eight-chart limit is
therefore currently CPU/layout throughput rather than compilation or
superlinear retained memory.

Scene publication no longer rebuilds the full native canvas/runtime diagnostic
string for every frame. Diagnostics remain available, but their snapshot is
refreshed at most every 250 ms and is assembled outside the reader mutex. Three
fresh-process four-chart runs after this change all passed animation, resize
and pan gates and produced the following pre-link density baseline:

| Ready charts | Median process working set | Increment |
| ---: | ---: | ---: |
| 0 | 116.3 MiB | — |
| 1 | 251.6 MiB | 135.3 MiB |
| 2 | 314.9 MiB | 63.3 MiB |
| 4 | 384.6 MiB | 69.7 MiB total, 34.9 MiB/chart |

The reliable four-chart amortized cost is therefore 67.1 MiB/chart above the
empty sample. The first chart is not representative of another chart: it pays
for process-wide V8, graphics, font and shared resource initialization.

The release linker now hides every V8/C++ implementation symbol, exports
exactly the 32-function WebScene C ABI, dead-strips unreachable sections and
removes local link symbols. On macOS arm64 the pointer-compressed dylib fell
from 48 MiB to 25 MiB: about 20 MiB of that is package/link metadata and
3.6 MiB is removed text/constants. Three fresh-process four-chart runs all
passed the cadence gates and measured 116.5, 245.3, 309.1 and 378.8 MiB at
0, 1, 2 and 4 charts. The stable RSS reduction is therefore about 5.8 MiB at
four charts, and the current amortized cost is 65.6 MiB/chart above the shell.
The package manifest records dense-link status and release verification
rejects an unstripped native package.

Three corresponding eight-chart runs had a median of 480.5 MiB, or
45.5 MiB/chart amortized above the 116.6 MiB shell. Charts five through eight
added a median 98.9 MiB, or 24.7 MiB/chart. The individual eight-chart results
ranged from 449.3 to 513.6 MiB, so this is directional until macOS footprint
or dirty-page accounting replaces working-set sampling. The resize median was
50.8 updates/s/engine; animation and pan remained at about 60.

Two newly attributed native caches are not useful compaction targets. At four
charts the JavaScript/DOM wrapper registry retained 3,536 handles in about
144.6 KiB and the text-measurement cache retained 558 entries in about
115 KiB. Together they account for only about 260 KiB, so replacing either
with a dense custom container would add risk without materially changing
density.

The compilation and resource caches now map immutable persistent bodies of at
least 64 KiB read-only and share the same mapping across isolates. Smaller
entries remain owned buffers because macOS has 16 KiB pages and each additional
mapping has VM bookkeeping cost. In one warm four-chart workload, 4.91 MiB of
the 5.52 MiB process compilation cache (89%) was file-backed. A separate
four-chart resource run mapped 3.63 MiB of its 4.60 MiB resource cache (79%)
while retaining the script bodies as external V8 strings, with no per-isolate
source copy. `vmmap` confirms that the `.v8cache` bodies are clean mapped-file
regions rather than dirty malloc pages. The compilation run reached 115.0,
252.7, 291.8 and 345.0 MiB and passed the 60 Hz animation/pan and 57 Hz resize
gates; the resource run reached 114.4, 260.8, 316.3 and 380.2 MiB and passed
60 Hz animation/pan and 57.4 Hz resize. Working-set variance is too high to
claim either difference as an RSS saving; the proven result is improved
reclaimability and one physical cache representation per process.

Controlled macOS VM inspection also found three active roughly 9 MiB
full-window RGBA allocations and about 22.4 MiB resident in empty large malloc
regions. A coordinated V8 low-memory request reduced the application-tagged
physical footprint by roughly 8--10 MiB across four idle charts but did not
release those allocator regions. A direct allocator pressure-relief call
returned no memory and has therefore been removed. Surface lifetime and idle
V8 policy remain separate targets.

An empty-window hold subsequently isolated the surface cost before any WebScene
engine or chart existed. The 1,920-by-1,200 Avalonia top level already owned
two 9,008 KiB Skia allocations and one freed allocation of the same size.
Those buffers are top-level swapchain/backbuffer cost, not per-chart WebScene
canvas surfaces. Retained WebScene canvas layers are SKPicture command streams;
their reported logical bitmap dimensions do not represent retained RGBA
allocations. Renderer buffer pooling is therefore rejected as a chart-density
optimization unless later allocation stacks identify a separate per-chart
surface.

A four-chart shared-isolate spike also failed before readiness: the native
runtime currently owns one iframe V8 context, one iframe base URL and one
iframe CSS rule set per engine, so four hosted chart frames overwrite singleton
browsing-context state and 0/4 frames become ready. True shared-isolate hosting
requires per-frame contexts, CSS realms, timers, listeners, wrapper identity,
resize routing and lifecycle state throughout the runtime. It would also
serialize four active charts on one worker. Separate isolates therefore remain
the supported responsive architecture; multi-frame shared-isolate hosting is
a distinct experimental project rather than a memory shortcut.

Sparse textual computed-style state has now moved out of every native DOM node
into a copy-on-write block. Ten mostly empty `std::string` objects no longer
inflate every style: `sizeof(node_style)` fell from 880 to 656 bytes and
`sizeof(dom_node)` from 1,264 to 1,040 bytes. At four charts, 3,628 nodes retain
3.60 MiB inline plus 320.7 KiB across 672 textual blocks, compared with
4.37 MiB of inline nodes before this change. The attributable net saving is
about 0.46 MiB. Allocation accounting includes an allocated textual block even
after all of its strings have been cleared; the measured workload retained no
such empty blocks. Native tests pass in both ordinary and pointer-compressed
builds.

Table geometry and live form-control state are now cold node records as well.
Ordinary DOM nodes no longer carry a table column vector plus row/cell span
geometry, or an input value string, selection endpoints, selected/checked
state and caret flags. `sizeof(dom_node)` fell again from 1,040 to 960 bytes.
In the four-chart workload, 3,628 nodes now retain 3,482,880 inline bytes and
the node pool reserves 3,569,952 bytes. Only 24 table records (1,344 bytes)
were live and no form-control record was needed in the settled chart. This is
an attributable 290,240-byte inline/pool saving at four charts. Dedicated
allocation metrics and native regression tests prove that 128 ordinary nodes
allocate neither cold record.

Instrumenting the node pool itself disproved fragmentation as a material
target. Before the two additional cold splits, 3,628 live nodes occupied
3,773,120 bytes and the pool reserved 3,860,192 bytes: only 87,072 bytes of
chunk/alignment overhead, with reserved and peak values equal. The allocator
was not retaining a transient high-water mark. Replacing the pool would
therefore add complexity for an upper bound of roughly 85 KiB in this
workload.

Native event-listener storage has also been compacted. TradingView retained
6,788 registrations across four charts. The previous representation kept the
callback, context, target, capture/once flags, diagnostic name and registration
sequence in seven separately keyed maps with synchronized vectors. It consumed
1,048,356 attributable bytes. One registration record and one vector per
event type now retain the same callbacks and dispatch metadata in 795,900
bytes, a measured 252,456-byte reduction. This also removes seven parallel
lookups from event dispatch and makes listener removal/once compaction atomic
at the record level. The complete native event/input suite passes in ordinary
and pointer-compressed builds. Five four-chart runs retained 60 Hz animation,
59.6--60 Hz pan and 56.7--57.7 Hz resize; RSS variance remained much larger
than the exact 247 KiB container saving.

The explicit/hidden low-memory path now also releases safe native container
slack after startup. On the engine worker it right-sizes event-listener,
timer, observer, resource, hydration, detached-root, cookie, CSS-rule and CSS
index vectors before asking V8 to reclaim. It does not run on an active input
or resize path. In the four-chart post-ready probe, event-listener storage
fell from 795,900 to 579,068 bytes without changing the 6,788 live
registrations: another exact 216,832-byte reduction. From the original
parallel-map representation to the compacted low-memory state, the combined
attributable reduction is 469,288 bytes. The same run retained 60 Hz animation,
60 Hz pan and 57.25 Hz cooperative resize. Three current runs retained the
same exact listener count/storage and reached 56.9--57.5 Hz resize. Their
375.6--385.1 MiB RSS observations remain inside the established run-to-run
range and are not used as evidence for this sub-megabyte saving.

Five current pointer-compressed/shared-cage runs after the cold splits reported
a 114.6 MiB median empty-window base and 263.9, 316.6 and 378.1 MiB medians at
one, two and four ready charts. The implied increments are about 149.3 MiB for
the first chart, 52.7 MiB for the second, and 30.8 MiB each for charts three
and four; net average cost is 65.9 MiB/chart. RSS varied from 360.1 to
392.6 MiB at four charts, so the roughly 2.9 MiB difference from the preceding
375.2 MiB median is noise rather than evidence that the cold split increased
memory. The exact native allocation counters, not process RSS, prove the
290 KiB reduction. All five runs retained 60 Hz animation/pan and
56.5--57.6 Hz cooperative resize cadence.

The current supported macOS arm64 Debug density result uses the release-shaped
native runtime: pointer compression, the shared compression cage,
`--optimize-for-size`, dense linking and four independent isolates. Five
current active-chart runs after event-listener compaction reported a 383.8 MiB
median from a 114.6 MiB empty-window base, or 67.3 MiB/chart amortized. Three
post-ready low-memory/native-capacity runs reported 375.6--385.1 MiB
(378.8 MiB median) from the same 114.6 MiB base, or 66.0 MiB/chart amortized.
The first chart still carries process-level V8, resource, renderer and
application setup that later charts reuse, so neither average is a linear
per-isolate allocation.

The roughly 5.0 MiB median RSS difference is plausible but remains smaller
than run-to-run RSS variance. Exact counters give the stronger result: median
aggregate V8 physical heap fell from 100.2 MiB to 88.25 MiB and native
listener capacity fell by 216,832 bytes. All runs retained 60 Hz animation and
pan plus 56.7--57.7 Hz cooperative resize. The low-memory path is therefore
appropriate for idle/hidden charts, but must not run in an active input or
resize path.

The final active-chart attribution pass now reports V8 heap spaces through the
additive engine ABI while preserving the preceding ABI tail. A matched
four-chart active/reclaimed pair produced:

| V8 space | Active used | Active physical | Reclaimed used | Reclaimed physical |
| --- | ---: | ---: | ---: | ---: |
| Young generation | 2.08 MiB | 8.00 MiB | 0.01 MiB | 0.50 MiB |
| Old generation | 46.31 MiB | 49.25 MiB | 45.45 MiB | 47.41 MiB |
| Code | 8.28 MiB | 9.75 MiB | 7.92 MiB | 8.20 MiB |
| Large objects | 21.37 MiB | 21.44 MiB | 21.37 MiB | 21.44 MiB |
| Trusted/internal | 10.51 MiB | 12.50 MiB | 10.30 MiB | 10.84 MiB |
| Shared read-only (process-wide) | 1.72 MiB | 1.72 MiB | 1.72 MiB | 1.72 MiB |

The per-isolate physical total fell from 100.94 to 88.39 MiB. Of that
12.55 MiB release, 7.50 MiB was young-generation capacity and the remainder
was small amounts of old, code and trusted-space slack. Large-object space was
unchanged byte-for-byte, proving its 21.37 MiB is live application state rather
than unused committed capacity. Both probes retained 60 Hz animation/pan and
56.1 Hz cooperative resize.

This is the stop point for broad active-chart memory micro-optimization. Native
DOM plus CSS has a bounded roughly 10.52 MiB total, and the remaining V8
categories are predominantly live isolated application state. Further active
work requires a retained-object hypothesis with at least a 5 MiB four-chart
upper bound; build-flag or container experiments without that evidence are not
justified. The next density feature is host-driven parking for charts hidden in
unselected tabs: retain a last scene and restorable chart state, destroy the
inactive isolate, and recreate it asynchronously before the tab is presented.
That targets most of a hidden chart's roughly 25--35 MiB steady marginal cost
instead of another sub-megabyte active-chart container.

The first parking slice is now implemented in the private four-chart integration.
Its public .NET-facing lifecycle is asynchronous:

- `SuspendAsync` serializes the complete low-level TradingView layout, detaches
  the surface, releases subscriptions and destroys that chart's engine
  generation;
- `ResumeAsync` creates a new generation from the process-wide resource and V8
  caches, injects the saved layout through TradingView's constructor
  `saved_data` option, obtains fresh history/subscriptions and does not complete
  until the current data and first ready presentation are available;
- the explicit states are `Ready`, `Suspending`, `Suspended`, `Resuming` and
  `Ready`; cancellation or restore failure preserves the serialized suspended
  state so the operation can be retried;
- callers can start `ResumeAsync` as soon as tab selection is anticipated and
  await it before exposing the live chart.

The application-facing facade now builds as the reusable
`WebScene.TradingView.Avalonia` assembly rather than being compiled into the
sample executable. It is packable independently, consumes either the local or
NuGet WebScene Avalonia backend, and leaves the matching RID-native runtime as a
host-application dependency. This keeps `SuspendAsync`/`ResumeAsync`, the
datafeed contract, and native session factory usable by another Avalonia
application without bringing the private diagnostic executable with them.

The frozen presentation slice is also implemented. Suspension requests one
immutable native scene checkpoint and transfers its reference into a
non-interactive Avalonia draw control. The old engine, DOM and isolate are then
destroyed. On resume, the new live surface initializes underneath the frozen
control; the facade removes and disposes the checkpoint only after the live
generation has current data and a ready presentation. This avoids a blank tab
without allocating a full pixel bitmap or retaining the old runtime.

The deterministic four-chart probe suspends three charts and proves that each
restored layout has the same top-level schema and exact serialized size, that a
new generation was created, and that each restored active chart has a symbol,
resolution and current bars. Matched runs released 70--72 MiB of aggregate V8
physical heap while the three engines were absent, or roughly 23--24 MiB per
parked chart. Process RSS did not immediately fall because the allocator retains
freed address-space pages, so the exact engine counters are the release gate.

After removing one redundant data-ready evaluation, three matched runs restored
the selected chart in 171.0, 171.4 and 176.2 ms (171.4 ms median). Their
warm-empty-chart medians were 173.9, 175.6 and 174.9 ms (174.9 ms median).
Restore won two individual comparisons and lost one; the roughly 3.5 ms median
difference is noise, so the supported result is performance parity with an
empty warm chart while additionally restoring the user's panes, indicators,
drawings and viewport and confirming current data. The first process-cold chart
took roughly 1.25--1.52 seconds.

Stage attribution bounds further latency work. Engine creation was
0.01--0.04 ms, saved-layout priming 7.8--10.4 ms, and current-data confirmation
11.5--15.0 ms. TradingView's restored widget readiness dominated at
118--137 ms. That is live application reconstruction rather than native
allocation/cache overhead. Keep parking for its substantial hidden-memory
saving and move back to compatibility/performance certification rather than
chasing noise in the remaining restore interval.

The completed frozen-frame gate retained three checkpoint scenes whose native
source size totalled about 223 KiB. Three headless pixel comparisons over the
parked quadrants reported mean absolute RGB deltas of 0.57--1.72/255 between
the live and frozen frames. The probe observed the frozen-over-live staging stack
during resume and proved every checkpoint was released after the atomic swap.
Repeated lifecycle restores completed in 169--186 ms. A deliberately cancelled intermediate resume restored
the frozen suspended state and a subsequent retry succeeded; the measured
generation delta was exactly one cancelled plus two successful generations.
The matched active-path regression retained 60 Hz animation/pan and 57.2 Hz
cooperative resize.

An eight-chart density run reached 409.1 MiB after the same low-memory barrier,
but simultaneous resize fell to 49.2 updates/s/chart. That point demonstrates
good retained-memory amortization, not supported eight-chart interactive
capacity; four simultaneously active charts remain the current performance
target.

The next memory experiments are prioritized by attributable upper bound and
risk:

1. Completed for explicitly hidden/detached charts. The host now reports
   visibility through the native ABI. Hiding schedules a worker-thread V8
   low-memory notification after a 500 ms debounce; restoring visibility
   cancels it, so transient reparenting and tab switches do not collect.
   Explicit host memory-pressure requests remain available separately. A
   native regression test proves cancellation and one-shot sustained-hidden
   reclamation, and the TradingView surface drives the policy on visual-tree
   detach/attach. The current median RSS difference is about 5.0 MiB across
   four charts, with the exact V8 physical-heap and native-capacity changes
   reported above.
2. Completed for compilation and resource bodies. Large persistent bodies are
   mapped once per process; measured coverage was 89% of the compilation cache
   and 79% of the resource cache. This improves fixed cost and reclaimability
   rather than per-chart scaling.
3. Rejected after measurement: a custom V8 startup snapshot containing the
   largest pure-JavaScript binding bootstrap preserved compatibility and the
   60 Hz interaction gates, but three pointer-compressed runs reached a
   384.8 MiB four-chart median versus 378.1 MiB in the five-run unsnapshotted
   baseline, with no startup improvement. Creating the snapshot at runtime
   retains a serialized blob without gaining file-backed page sharing. The
   native DOM callback templates cannot be serialized safely without
   maintaining a large complete external-reference address table, while live
   contexts, wrappers and mutable application state remain isolate-local. The
   experimental switch has been removed.
4. Continue attributing transient native allocations, but do not treat the
   top-level Skia swapchain as per-chart memory. The full-window buffers exist
   before WebScene starts. Generic PMR pooling, allocator pressure relief and the
   macOS nano allocator have already been rejected by measurements.
5. Continue compact native representation only where metrics show density:
   atomize repeated DOM/CSS identifiers, use enums for closed CSS value sets,
   and right-size retained vector capacities after startup. DOM plus CSS is
   now only about 10.52 MiB at four charts, so its remaining total upside is
   bounded and lower than V8/graphics work.
6. Keep separate isolates for simultaneously active charts. A shared isolate
   cannot execute their JavaScript in parallel, WebScene does not yet have
   independent multi-frame state inside one engine, and eight-chart resize is
   already CPU-bound. Shared-engine grouping remains suitable only for
   inactive or mutually exclusive charts after a dedicated scheduler design.
7. Split debug symbols and enable release dead stripping/LTO to reduce package
   and fixed code-page cost. This does not reduce JavaScript heap, DOM, canvas
   or other per-chart state, so package-size wins must not be reported as
   equivalent live-RSS wins.

Several V8 knobs have now been measured and rejected as density defaults:

- 64 MiB and 96 MiB per-isolate heap ceilings loaded four charts but did not
  reduce the retained working set. They remain containment limits with fatal
  OOM risk, not memory reclamation.
- `SetMemorySaverMode(true)` and a two-thread process-wide V8 platform pool did
  not produce a repeatable RSS reduction. The smaller pool can also increase
  startup latency.
- Enabling idle-task support alone did not reduce RSS because useful idle work
  still needs an embedder-supplied idle deadline.
- V8 Lite/JIT-less mode reduced four-chart RSS to a roughly 349.9 MiB median,
  but failed the full interaction gate: pan reached only 40.0 fps, axis drag
  41.8 fps, resize 53.8 fps and settings presentation 764 ms. It is rejected
  for active TradingView charts despite its roughly 25 MiB saving.
- A runtime-created custom startup snapshot for the pure-JavaScript
  IntersectionObserver bootstrap raised the observed four-chart median to
  384.8 MiB from 378.1 MiB and did not improve startup. It is rejected; a
  build-time mapped snapshot would still leave WebScene's native callback
  templates and per-document state isolate-local.

Independently of retained memory, incremental native layout is now the leading
throughput optimization. A TradingView resize alternates layout-affecting
width/height/top/left writes with synchronous `offsetWidth`/`offsetHeight`
reads. WebScene currently resolves each dirty read with a full document layout.
The measured post-listener layout/publication work is small in isolation, but
the repeated forced-layout boundary amplifies TradingView's resize observers
and JavaScript layout work across eight engines. The next performance spike
should add subtree invalidation and reuse unaffected layout results while
retaining synchronous geometry correctness.

The first scoped invalidation slice is implemented for CSSOM size/inset writes
to absolute/fixed boxes. Unrelated client boxes and ancestors may reuse their
last valid geometry, while the changed box and descendants still force
synchronous layout; the dirty subtree is always resolved before scene
publication. TradingView takes this path roughly once per resize dispatch and
the profiled geometry-binding time moved from about 2.36 ms to 2.15 ms. This is
a real but small saving: eight-chart resize remained within its existing
49--51 updates/s/engine range. Broader stale-geometry shortcuts are therefore
rejected; the next slice must recompute a proven formatting-context subtree
rather than weakening synchronous geometry semantics.

A subsequent broader out-of-flow partial-layout spike has also been measured
and rejected. It recomputed only a dirty absolute/fixed root when the viewport
and global style state were unchanged. Although the path was exercised hundreds
of times, repeated A/B runs showed no dependable interaction improvement and
one run left a TradingView settings dialog in an incorrect state. The
implementation and experimental switch were removed. Detailed profiling
instead bounded native geometry bindings to about 1.1 ms per sampled resize,
the final native layout to about 0.3--0.4 ms and final scene construction and
publication to well below one millisecond each.

Repeatedly starting and stopping V8's CPU profiler around every resize was
itself found to reduce observed cadence from roughly 60 to 28 updates/s. The
diagnostic mode now captures exactly one resize callback and then disposes the
profiler. That non-perturbing sample identified TradingView's synchronous
`offsetWidth`/`offsetHeight` call paths, but did not identify another
multi-megabyte allocation or a native layout cost large enough to justify a
semantic shortcut.

The active-chart memory experiment is therefore closed at the measured stop
point above. Parking remains the substantial density mechanism: three hidden
charts release about 70--72 MiB of V8 physical heap and a selected chart
restores in about 171 ms with its saved layout and current data. Further
always-visible chart work requires a new retained-object attribution with at
least a 5 MiB four-chart upper bound. The immediate performance target is scene
delivery and scheduling: recent 60-input pan/axis probes applied essentially
all pointer inputs but published only about 44--49 scenes/s, while their final
native dispatch, layout, build and publication stages were individually well
below a 16.7 ms frame budget. That points to batching, backpressure or
producer/consumer phase alignment rather than native allocation density.

Two narrow scheduling changes were then measured and rejected. Waking the
producer directly when the consumer acknowledged its one outstanding diff
moved resize only within run-to-run noise (about 52.8 to 54.8 Hz) and left
pan/axis near 42 Hz. Coalescing the 120 Hz certification clock more aggressively
while visual work was pending was counterproductive: TradingView uses those rAF
turns for gesture work, and pan/axis fell to roughly 39/37 Hz. Neither change is
retained. The performance lane now records applied and coalesced animation
frames alongside pointer, layout and publication metrics so the eventual
scheduler redesign must improve presentation without discarding useful web
animation semantics.

The next attribution slice now separates the previously combined stages. Native
metrics report RAF callback turns and elapsed time, non-frame input turns and
elapsed time, scene-publication attempts and blocks, and scene-acknowledgement
latency. The Avalonia host reports composition callbacks, applied diffs, renders,
damage rectangles and invalidation calls. Representative one-chart traces show:

- pan spends about 233--243 ms/s in input dispatch and 314--317 ms/s in RAF
  callbacks; axis scaling has a similar combined JavaScript cost;
- final native layout, scene construction and publication remain below one
  millisecond each;
- pan and axis typically publish 48--51 scenes/s, with roughly 10--17
  publication attempts blocked by the one-outstanding-diff rule;
- acknowledgement averages roughly 8--11 ms, while retained diff application
  and Skia submission remain around 1--1.6 ms at p95.

This proves that the remaining single-chart 55/60 Hz gap is producer/consumer
phase alignment around real TradingView JavaScript work, not a slow final
layout, scene build, or Skia draw.

Two compositor experiments were bounded and rejected. Acknowledging an acquired
scene before retained-picture compilation sometimes raised pan cadence but was
inconsistent, reduced axis cadence in other runs, and one repetition failed a
dialog lifecycle assertion. Acquiring the old scene before submitting the next
host RAF reduced some blocked axis publications but likewise traded resize/axis
results rather than improving all interaction lanes. Ordered application and
acknowledgement therefore remain unchanged.

One host-side cleanup is retained: all damage rectangles in one immutable diff
are unioned into one Avalonia invalidation. Pan invalidation calls fell from
roughly 426 to 42 and axis calls from roughly 314 to 44 while the original
damage-rectangle evidence remains visible. Avalonia had already coalesced most
of those calls, so this removes API/dispatcher churn but is not claimed as a
frame-rate improvement.

RAF timestamp semantics are also corrected. A callback released by host frame
N now keeps frame N's timestamp if the fair task scheduler carries it across a
worker iteration; host frame N+1 releases only callbacks that have not already
joined a rendering opportunity. A deterministic slow-callback regression
proves that a pending sibling keeps the old timestamp and a nested callback gets
the following timestamp. Fully draining each callback list atomically was
rejected: it improved pan/axis by only about 3--5 Hz but combined TradingView's
callbacks into 7 ms turns and regressed document startup from roughly
0.45--0.62 seconds to 1.5--2.4 seconds.

The large interaction regression was subsequently traced to input/render
ordering, not to the native allocation reductions. Commit `b2f14ea` introduced
continuous-input aggregation ordered by `webscene_input_event.sequence`, but the
managed host intentionally submits render opportunities with sequence zero.
When a pointer move and the following host frame occupied one aggregate, the
frame therefore ran first. TradingView's pointer handler requested RAF only
after that opportunity had already been released and its gesture update slipped
to the following frame. The native lane now preserves the real FIFO ordinal
between coalesced frame, pointer and wheel aggregates. A deterministic
pointer-then-frame test proves that the pointer handler joins that rendering
opportunity even though the frame's public sequence is zero.

The scene lane is now an ordered, bounded two-diff queue for compositors that
opt into `webscene_engine_acquire_next_scene`; the legacy latest-scene API retains
its one-scene behavior. An ordered consumer can apply one diff while the native
worker prepares the next without breaking base revisions. An A/B against the
legacy lane improved one-chart pan/axis by roughly 3--8 Hz and removed active
publication blocks. Four live charts retain eight queued scenes totalling about
727 KiB versus about 477 KiB for their four latest scenes, so the second slot
costs approximately 249 KiB total, or 62 KiB/chart. This is a deliberately
bounded sub-megabyte throughput cost, not a material explanation for process
RSS.

The Avalonia composition handler also deduplicates outstanding invalidations.
A publication schedules at most one render; applying that diff inside
`OnRender` no longer self-invalidates and create an empty follow-up render. A
counted publication signal still drains a second coalesced diff on the next host
frame and publication continues to drive synchronous painting while normal
animation callbacks are paused for live resize.

With FIFO input/frame ordering and the ordered scene lane, repeated one-chart
Release samples reached roughly 58--60 Hz resize, 55--60 Hz pan, 54--56 Hz axis
scaling and 56--60 Hz Indicators scrolling. Debug produced 58.5/56.6/55.9/54.7
Hz respectively, confirming that the developer configuration is not the source
of the regression. A final four-isolate run used 470.9 MiB RSS without an
explicit post-ready low-memory request and sustained 60 animation frames/s,
55.7 resize updates/s/engine and 59.4 pan moves/s/engine.

Two certification issues remain explicit. Settings presentation is variable
at roughly 489--604 ms and still crosses the 500 ms gate in some runs. The
headless cadence harness also previously combined a 120 Hz auxiliary RAF timer,
composition callbacks and forced 60 Hz captures. It now pauses the auxiliary
clock and enters an explicit manual-composition scope for each sustained stream:
one host RAF prepares a scene during a 16.7 ms slot and one forced composition
frame presents it at the following boundary. Publication invalidations are
suppressed only in that scoped test mode. This reduced coalesced RAFs from
tens/hundreds per stream to about 2--4 and produced p95 presentation intervals
of about 17--18 ms for pan and Indicators scrolling. Axis drag still misses
roughly three visual deadlines in some runs, resize retains its real
cooperative-live-resize clock in addition to the measurement clock, and the
separate paused-animation resize gate exposed a second ordered diff that could
remain queued when two publication messages coalesced while animation callbacks
were suspended. The handler now immediately schedules that known remaining
publication only in paused non-manual mode; two post-fix runs observed at least
three intermediate viewports, but a longer repeated gate is still required.
The remaining cadence failures stay visible rather than weakening the p95 or
resize-liveness assertions.

Four subsequent scene-delivery experiments were measured and rejected.
Draining every already-published ordered diff into one compositor presentation
raised one resize average to 60.5 fps, but reduced pan, axis and wheel
presentation to 56.9, 55.0 and 54.0 fps respectively; axis and wheel p95
intervals regressed to about 33 ms. Restricting that drain to a live-resize
notification preserved the other interactions but produced no repeatable
resize gain. Publishing immediately whenever a resize and host RAF were
consumed in one worker turn likewise failed to remove the 33 ms tail and
increased RAF coalescing. Finally, draining a ready dependent diff only when
the first scene's viewport trailed the compositor's effective size produced no
repeatable resize gain. All four shortcuts have been removed. The retained rule
is one ordered diff per presentation, with the bounded second publication
scheduled on the following real display opportunity.

The performance artifact now attributes the host composition boundary itself:
manual render request, UI dispatcher work and headless capture are timed
separately from native diff application, retained drawing, Skia submission and
the following input submission. In a representative restored baseline, resize
reached 58.0 fps while host-boundary p95 was 12.9 ms and resize-submission work
p95 was 6.4 ms. Their serialized headless path therefore exceeded one 16.7 ms
slot even though native final layout was about 0.38 ms, scene construction
0.17 ms, publication 0.24 ms and the complete composition callback about
5.0 ms. The next resize investigation must profile Avalonia arrange/capture
and the real desktop compositor separately; further native scheduler changes
are not justified by this trace.

The 64 MiB per-isolate experimental heap cap loaded and exercised four charts
at 423.8 MiB RSS, but a hard cap is not a safe density default. V8 documents
that approaching the limit triggers repeated collections and ultimately a
fatal process OOM when the live set cannot fit. Heap limits should be derived
from soak-test high-water marks with headroom and used as containment, not as
the primary memory optimization.

A single exploratory run limited V8's process-wide platform pool to two
threads. It reduced the process from 34 to 28 threads and reached 411.5 MiB RSS
with the size-optimized build, but first-chart startup increased to 3.43
seconds. This is a small memory return for a large latency risk; retain V8's
default pool until repeated startup and long-interaction traces justify a
different bound.

## Certification and Performance Gates

Add deterministic tests that create multiple engines against an initially
empty cache:

1. Start 2, 4 and 8 engines simultaneously with the same script.
2. Assert exactly one full compilation and `N - 1` waiters.
3. Assert every engine executes the same result.
4. Assert one valid persistent entry and zero temporary-file leaks.
5. Repeat warm and assert zero full compilations.
6. Repeat with different scripts and prove unrelated keys compile in parallel.
7. Inject producer compilation failure and prove every waiter completes with
   the same failure.
8. Destroy one waiter and then the producer at controlled points; prove there
   is no deadlock or stranded entry.
9. Corrupt and truncate persistent entries; prove one coordinated rebuild.
10. Run concurrent writers in separate processes to validate current atomic
    publication and any later lock-file protocol.
11. Record cold latency, warm latency, waiter latency, CPU time and peak RSS.
12. Measure resident memory at 1, 2, 4 and 8 TradingView-like instances and
    gate unexpected growth.

The tests must run under sanitizers where available and include repeated
stress runs so race freedom is demonstrated rather than inferred from a
single successful execution.

## Delivery Sequence

1. Completed: add producer, waiter, memory-hit and shared-byte metrics.
2. Completed: implement process-wide single-flight code-cache production.
3. Completed: store ready cached-code data in bounded immutable shared buffers.
4. Completed: add four-isolate cold compilation and resource-load tests.
5. Completed: apply the coordinator pattern to resource fetches.
6. In progress: the 0, 1, 2, 4 and 8 measurement path, per-engine native memory
   telemetry, three-run cold baseline, native DOM attribution and bounded
   node pool, compact attributes, parsed-CSS attribution/sharing,
   retained-canvas topology, SVG-picture sharing and asynchronous low-memory
   requests are implemented. Cold/warm eight-chart variants prove linear
   retained-memory scaling but expose a simultaneous-resize throughput limit.
   Attribute the remaining Skia/Avalonia surfaces and targeted native allocator
   fragmentation, then profile and reduce the resize layout/render CPU cost.
7. Evaluate background cached-code consumption.
8. Prototype an isolate-pool mode only if measured per-isolate duplication
   remains material.
9. Consider cross-process locking only if real workloads show a meaningful
   cold-start stampede between processes.

## Decision

WebScene retains separate isolates and worker threads as its compatibility,
isolation and responsiveness default. Native process-wide single-flight
compilation and resource loading now prevent duplicate cold work, and
immutable cached-data buffers are shared across those isolates. Shared-isolate
hosting remains an experimental density optimization subject to
responsiveness, isolation and memory benchmarks.

# Sandwich multi-chart performance recommendations

**Status:** Active implementation and evidence ledger

**Date:** 2026-08-31

**Baseline release:** WebScene 1.0.26

## Purpose

This document is a self-contained brief for an agent improving WebScene for a
large, continuously updating browser application embedded in an Avalonia
desktop application. Sandwich's four-chart benchmark is the reference
workload, but every WebScene change must remain generic. Do not add
TradingView assets, API definitions, URL checks, selector checks, or other
site-specific behavior to WebScene.

The goal is to reduce total process CPU and memory while preserving browser
semantics, visual correctness, input behavior, reparenting, and the absence of
native-child-window airspace restrictions.

## Known evidence

Treat these measurements as the starting point, not as results to reproduce
selectively:

- The deterministic four-chart workload sends one real-time bar every 250 ms
  to each chart through the production generated interop and datafeed path.
- Published WebScene 1.0.14 measured `0.35918` CPU core and `630.40 MiB` mean
  RSS in that workload.
- Two published WebScene 1.0.15 runs averaged `0.32328` CPU core and
  `633.20 MiB` mean RSS: about 10% lower CPU with effectively unchanged RSS.
- A live-data Time Profiler sample attributed approximately `0.0519` core to
  WebScene native/V8, `0.0207` to system work, `0.0100` to Avalonia/Skia,
  `0.0077` to tiered JIT, and `0.0064` to JavaScript. Thread-pool spinning was
  negligible in that capture.
- The Servo selector cache is present. A focused repeated-selector benchmark
  improved from `117.830 ms` to `29.208 ms`, a 75.2% reduction (`4.0x`).
- The WebScene 1.0.16 document-property/named-property correction improved its
  focused benchmark from `68.804 ms` to `3.081 ms`, a 95.5% reduction
  (`22.3x`). Its whole-application benefit has not yet been established.
- The generated binary ABI previously reduced CPU by 29.5% and managed
  allocation by 96.7% against the removed JSON control in a focused
  four-engine callback workload. It is already the intended hot path.
- Separate V8 isolates remain the production default. An opt-in shared-isolate
  pool exists and is an experimental density candidate; it must be evaluated
  against independent isolates for memory, serialization, state isolation,
  lifecycle behavior and completed work.
- The compositor mailbox already coalesces immutable scene publication at
  compositor boundaries. Preserve ADR 0011.
- An earlier partial-damage experiment produced black regions and flicker.
  Clipping a fresh frame to a damage rectangle is incorrect unless undamaged
  pixels are retained.

Related evidence:

- [TradingView-shaped four-chart generated binary interop](tradingview-four-chart-binary-interop-results.md)
- [ADR 0011: compositor mailbox](adr/0011-compositor-driven-native-scene-publication.md)
- [ADR 0012: pooled binary interop](adr/0012-pooled-binary-native-javascript-interop.md)
- [Runtime architectural upgrade shortlist](runtime-architectural-upgrade-shortlist.md)

## WebScene 1.0.26 checkpoint

The 2026-08-30 Release matrix established a new independent-isolate product
baseline using 30-second warm-up, 60-second steady intervals and 30-second
interaction intervals:

| Charts | Scenario | CPU cores | Mean RSS | Physical footprint |
|---:|---|---:|---:|---:|
| 0 | steady | 0.068 | 430.2 MiB | 405.6 MiB |
| 1 | steady | 0.121 | 575.4 MiB | 628.6 MiB |
| 2 | steady | 0.274 | 619.1 MiB | 685.7 MiB |
| 4 | steady | 0.311 | 730.3 MiB | 778.1 MiB |
| 8 | steady | 0.555 | 899.5 MiB | 962.0 MiB |
| 4 | pan | 2.575 | 756.7 MiB | 870.8 MiB |
| 4 | axis | 2.449 | 754.8 MiB | 833.9 MiB |
| 4 | resize | 2.355 | 738.2 MiB | 803.3 MiB |
| 8 | resize | 2.778 | 931.4 MiB | 992.2 MiB |

Steady physical footprint grows approximately 46 MiB per additional chart
from two through eight charts. Four-chart pan renders 53.3–54.9 fps per chart,
axis drag 49.0–49.6 fps, and real window resize only 8.8–9.1 fps with 331–351 ms
p95 publication latency.

A measurement-synchronized Time Profiler trace collected 87,576 weighted CPU
samples during four-chart resize. Leaf attribution was 51.8% WebScene native,
29.8% system libraries (including 8.8% malloc), 7.9% managed/JIT or unmapped,
and 5.1% Skia. The main thread represented 14.3%; four WebScene worker threads
each represented about 16%.

A matched `RelWithDebInfo` runtime and dSYM resolved the next 112,835-sample
trace. Leaf attribution remained consistent at 55.4% WebScene native, 26.5%
system (9.7% malloc), 8.3% managed/JIT or unmapped, and 4.0% Skia. Inclusive
native attribution identified recursive `layout_children` at 30.2%,
`ensure_layout` at 30.0%, `layout_child` at 29.9%, and resize dispatch at 23.1%
of total sampled CPU. Intrinsic-size computation and its hash-table lookup were
the strongest named leaf cluster. Use this attribution to investigate layout
and allocation; it still does not justify another native or Avalonia scheduler
change.

Allocator-leaf attribution in the same trace represents 10.59% of sampled
CPU. The first WebScene callers include allocation and deallocation of
`const dom_node*` and `dom_node*` vectors (about 3.16% combined) and allocation
of flex-layout `unordered_map<const dom_node*, float>` nodes (0.51%). A full
Allocations-template trace did not finalize even with a 180-second timeout, so
it is retained only as a failed diagnostic artifact and supports no claim.
This evidence led to an exact focused allocation counter and deterministic
flex/layout scratch benchmark before changing these containers.

The first symbol-led candidate replaced the intrinsic-size hash map with two
generation-stamped entries per DOM node. It passed the node footprint guard,
native suite, 123 WPT documents/453 subtests, the interop race probe and package
smoke test, but failed process acceptance. Two Release ABBA blocks measured
CPU +1.98% (95% CI -2.84% to +7.28%), RSS -4.18% (-8.64% to +0.05%), and
physical footprint +0.54% (-0.80% to +1.95%). Submitted resize ticks matched
with no drops or script errors, but internal frame/layout/render work varied
materially. The candidate was reverted and remains a rejected, unproven idea.

A second candidate reused the existing `layout_items` vector as the ordered
children list instead of copying it. It passed the same correctness suites, but
two Release ABBA blocks at a reduced 15 Hz resize cadence measured CPU -2.18%
(95% CI -10.69% to +7.32%), RSS +6.72% (+2.33% to +12.14%), and physical
footprint -0.15% (-1.23% to +0.91%). Internal work again failed equivalence.
The CPU change was unproven, the RSS regression was supported, and the
candidate was reverted.

The first accepted symbol-led change gives recurring layout vectors and flex
hash maps a lazy per-document `std::pmr::unsynchronized_pool_resource`, capped
at 1 MiB of retained scratch. In the deterministic 797-node nested-flex
fixture, identical geometry required 3,221 allocation calls and 160,344
requested bytes per layout before the change, versus 269 calls and 21,248 bytes
after it: reductions of 91.65% and 86.75%, respectively, with 11,976 bytes
retained for reuse. The native suite, 123 WPT documents/453 subtests, the
12,800-operation race probe and package smoke test passed.

Two four-chart 15 Hz resize ABBA blocks measured CPU +0.53% (95% CI -3.34% to
+4.62%), RSS +0.92% (-0.43% to +2.26%), and physical footprint +1.24% (-0.06%
to +2.53%). Submitted work and time-normalized generated-call rate matched,
with no drops or errors. Direct render/presentation rate, p95 frame interval,
and p95 resize-latency comparisons found no supported regression above 3%.
The exact allocation win is accepted and the process-level result is neutral.

A matched post-change symbol trace contained 103,416 weighted samples.
Allocator leaves shifted from 10.59% in the earlier control trace to 7.92%.
The flex `unordered_map<const dom_node*, float>` hash-node allocator disappeared
from the leading callers; mutable `dom_node*` allocation fell from 0.76% to
0.38% and deallocation from 0.41% to 0.02%. Remaining `const dom_node*` vector
allocation/deallocation stayed near 2.05% combined and is now the primary
layout-owned allocation target. These are separate attribution traces, not a
matched Release timing comparison, so the shift guides the next focused
fixture without adding another performance claim.

The accepted follow-up extends the same bounded pool to table-row,
collapsed-select-option and generic intrinsic-item pointer vectors. Against the
accepted pool predecessor, a deterministic 1,061-node fixture preserves the `259651`
geometry checksum and retained scratch at 36,600 bytes while reducing allocation calls
per layout from 2,681 to 1,165 (-56.55%) and requested bytes from 107,056 to 48,016
(-55.15%). The focused comparator requires at least a 50% call reduction.

Two cumulative Release ABBA blocks compared both accepted scratch increments with the
original 1.0.26 runtime during four-chart 15 Hz resize. CPU changed -10.75% (95% CI
-15.41% to -5.85%), RSS +0.82% (-0.48% to +2.13%), and physical footprint +0.21%
(-0.98% to +1.44%). Submitted work and time-normalized generated-call rate were
equivalent, and no process or cadence regression above 3% was supported. Native, WPT,
race and package gates passed. The ABBA decision consumes the two exact comparator JSON
files directly and is `accept`; descriptive notes alone do not qualify as proof.

A matched 62,471-sample trace of that cumulative runtime attributes 5.62% of sampled
CPU to allocator leaves, down from 7.92% after the first pool change. The targeted
`const dom_node*` allocation/deallocation falls from 2.05% combined to 0.07%.
Owner-edge attribution identifies the remaining mutable-node allocation (0.39%) as the
unconditional composed-child copy in `update_retained_canvas_paint_phase`.

The next accepted fast path uses the composed-child vector by reference when every
child has the default z-index, copying and sorting only when ordering can change.
Against its accepted cumulative predecessor, the 1,061-node fixture reduces allocation
calls per layout from 1,165 to 808 (-30.64%) and requested bytes from 48,016 to 39,536
(-17.66%), with the same `259651` geometry checksum and 36,600 retained scratch bytes.

Two cumulative Release ABBA blocks against original 1.0.26 measured CPU -8.93% (95%
CI -17.70% to +1.05%), RSS -0.56% (-1.75% to +0.65%), and physical footprint
-0.64% (-1.72% to +0.48%). Submitted work and generated-call rate were equivalent,
and no process or cadence regression above 3% was supported. The process changes are
neutral; the verified exact allocation reduction proves and accepts this small win.

A 68,269-sample post-paint symbol trace attributes 5.12% of samples to allocator
leaves. The typed `dom_node**` allocation edge owned by
`update_retained_canvas_paint_phase` is gone, validating the intended boundary. The
next typed layout-owned cluster is construction and destruction of the inline text-run
collector's `std::function<bool(dom_node&)>` at about 0.22% of total samples. A
transparent text-cache lookup experiment was measured and reverted because it changed
neither exact allocation calls nor requested bytes.

The accepted callback follow-up expresses that collector as a generic self-recursive
lambda, avoiding type-erasure allocation while preserving traversal order and early
exits. Against the accepted paint-order predecessor, the exact fixture reduces
allocation calls per layout from 808 to 616 (-23.76%) and requested bytes from 39,536
to 28,784 (-27.20%), with the same `259651` checksum and 36,600 retained bytes. All
native, WPT, race and package gates pass.

Two cumulative Release ABBA blocks against original 1.0.26 measured CPU -5.05% (95%
CI -14.07% to +5.15%), RSS -1.43% (-7.55% to +4.67%), and physical footprint -0.51%
(-1.71% to +0.66%). Scenario work and generated-call rate were equivalent, with zero
rejects, drops or script errors and no supported process or cadence regression above
3%. Four verified exact comparator reports prove the cumulative small wins, so the
decision is `accept` even though aggregate process metrics remain neutral.

The 65,463-sample post-callback trace attributes 4.89% to allocator leaves and no
longer contains the targeted `std::function<bool(dom_node&)>` owner edge. A sibling
inline-bounds callback experiment changed neither exact calls nor bytes and was
reverted. The same trace exposed recurring inline text-run, positioned-item,
static-anchor and line-alignment vectors that the prior fixture left empty.

The accepted follow-up routes those vectors through the existing bounded document
pool. A dedicated 1,013-node wrapped/aligned inline-text fixture, including positioned
inline items, preserves checksum `377393` and 20,192 retained bytes while reducing
allocation calls from 1,776 to 656 (-63.06%) and requested bytes from 46,640 to 21,552
(-53.79%). All native, WPT, race and package gates pass.

Two cumulative Release ABBA blocks against original 1.0.26 measured CPU -4.59% (95%
CI -12.73% to +4.47%), RSS -0.05% (-1.36% to +1.26%), and physical footprint +0.45%
(-0.56% to +1.50%). Work matched within one percent, with zero rejects, drops or
script errors and no supported process/cadence regression above 3%. Five verified
exact reports prove the cumulative small wins, so the decision is `accept` while the
aggregate metrics remain neutral.

A 59,058-sample post-inline-scratch trace attributes 4.73% of sampled CPU to allocator
leaves. The positioned-inline-item and line-geometry allocator owner edges targeted by
the preceding change are absent. Text measurement is now the leading layout-owned
allocator boundary, so the earlier heterogeneous cache-lookup idea was retried against
the trace-aligned inline fixture rather than the intrinsic fixture that did not exercise
it.

The accepted lookup constructs an owning text-measurement key only on cache miss.
Against the accepted inline-scratch predecessor, the 1,013-node exact fixture preserves
checksum `377393` and 20,192 retained bytes while reducing allocation calls from 656 to
584 (-10.98%) and requested bytes from 21,552 to 19,752 (-8.35%). Native, WPT, race,
and consumer package gates pass. The first two cumulative ABBA blocks failed the
generated-call-rate equivalence threshold, so all runs were retained and the same
sequence was resumed to four blocks.

Across all sixteen runs against original 1.0.26, CPU changed -6.01% (95% CI -12.54%
to +1.08%), RSS -4.73% (-8.46% to -1.45%), and physical footprint +0.54% (-0.30%
to +1.37%). Submitted work is equivalent, generated-call rate differs by -0.67%, and
no process or cadence regression above 3% is supported. Six verified exact reports and
the supported RSS reduction make the cumulative decision `accept`.

The matching post-lookup symbol trace contains 48,159 samples and attributes 4.87% to
allocator leaves. The targeted `measure_text` owner falls from 0.40% in the preceding
separate trace to 0.008%, consistent with the exact causal result but not an additional
timing claim. The next focused exact fixtures should isolate the remaining
`measure_text_width` character allocation (0.34% owner) and `canvas_save` allocation
(0.52% owner) before either path is changed.

The accepted text-width follow-up avoids constructing a mutable string when inherited
`text-transform` is `none`; transformed text retains the existing copy-and-mutate path.
Against the accepted lookup predecessor, the 1,013-node exact fixture preserves checksum
`377393` and 20,192 retained bytes while reducing allocation calls from 584 to 512
(-12.33%) and requested bytes from 19,752 to 17,952 (-9.11%). Native, WPT, race and
consumer package gates pass.

The initial two cumulative ABBA blocks reported supported CPU and RSS regressions, so
all runs were retained and the same sequence was resumed to four blocks. Across all
sixteen runs against original 1.0.26, CPU changed +5.49% (95% CI -1.48% to +12.44%),
RSS +2.88% (+0.69% to +5.55%), and physical footprint +0.26% (-0.63% to +1.14%).
Work is equivalent. The RSS increase is statistically supported but its observed point
estimate remains below the 3% practical-regression threshold; no process or cadence
regression above that threshold is supported. Seven verified exact reports make the
cumulative decision `accept`.

The 69,527-sample post-copy-elision trace attributes 4.48% to allocator leaves, but
`measure_text_width` remains a 0.38% owner. The exact fixture proves the input copy was
removed; inspection resolves the remaining trace-aligned cost as repeated owning return
of inherited font family. A new long-family fixture is required because the earlier
`sans-serif` fixture fits small-string storage and cannot expose that cost.

The accepted follow-up returns a view into stable node-style font-family storage.
Against the accepted text-transform predecessor, the 1,013-node long-family fixture
preserves checksum `377393` and 20,192 retained bytes while reducing allocation calls
from 8,016 to 512 (-93.61%) and requested bytes from 498,208 to 17,952 (-96.40%). The
known fetch-origin test setup race failed once, passed three focused reruns, and the
complete uninterrupted native, WPT, race and consumer pipeline passed.

Two cumulative ABBA blocks against original 1.0.26 measure CPU +7.24% (95% CI -1.40%
to +15.80%), RSS -4.52% (-8.86% to -0.64%), and physical footprint +1.47% (+0.29%
to +2.61%). Work is equivalent. The physical-footprint increase is statistically
supported but remains below the 3% practical-regression threshold; no process or cadence
regression above that threshold is supported. Eight verified exact reports make the
cumulative decision `accept`.

The matching post-change symbol trace contains 50,322 samples and attributes 4.16% to
allocator leaves. The targeted `measure_text_width` allocator owner falls from 0.38%
in the preceding separate trace to absent from the ranked owners, while its lower-level
`measure_text` owner falls from 0.029% to 0.018%. This is attribution consistent with
the exact causal result, not another timing claim. `canvas_save` is now the leading
WebScene allocator owner at 0.41%, making it the next focused exact-benchmark target.

While producing the matching symbol build, five repeated native-suite runs exposed the
known fetch-origin failure after two passes. The test asserted asynchronous fetch
metadata after waiting only for script submission. Moving its existing bounded promise-
result wait before the metadata assertions made five consecutive runs pass; the complete
symbol-package gates then passed 4/4 native tests, 123/123 WPT documents (453/453
subtests), 12,800 race operations, and the zero-warning consumer build. Runtime code is
unchanged by that test-only correction.

The accepted Canvas 2D follow-up reuses `emitted_paint_state` immediately after the
existing all-property synchronization in `save()`. The control rereads the same 18
JavaScript properties to build its native snapshot; the candidate copies the already-
synchronized native state. A focused V8 fixture checks all 18 properties plus line dash
after each restore and reduces exact property reads from 36 to 18 per save (-50%). Its
timing result (mean -24.01%, p50 -23.29%, p95 -24.34%) is supporting information only.

The complete Release gates pass: native 4/4, WPT 123/123 documents and 453/453
subtests, 12,800 race operations without faults or leaked results, and a zero-warning
consumer build. Two cumulative ABBA blocks against original 1.0.26 measure CPU -14.62%
(95% CI -20.72% to -8.73%), RSS -4.46% (-8.35% to -0.97%), and physical footprint
-0.55% (-1.62% to +0.56%). Work is equivalent and no supported process or cadence
regression above 3% exists. Nine machine-verified exact reports make the cumulative
decision `accept`.

The 64,270-sample post-change symbol trace attributes 3.89% to allocator leaves, versus
4.16% in the preceding separate attribution run. The targeted `canvas_save` owner falls
from 207 ms / 0.411% to 4 ms / 0.0062%, a 98.49% reduction in sampled owner share that
corroborates the exact property-read result. Resize dispatch is now the largest broad
owner at 0.33%, spanning JavaScript event work and media-query index rebuilds. The next
lower-risk typed exact target is `append_scene` paint-order allocation at 0.17%.

The accepted scene follow-up stores up to eight recursive paint-order entries inline
and preserves the original reserved vector as an unbounded spill path. Against the
accepted canvas predecessor, the 1,013-node exact scene fixture preserves checksum
`7587.43` while reducing allocation calls from 3,157 to 2,148 (-31.96%) and requested
bytes from 281,192 to 220,104 (-21.72%). Informational-only p50 and p95 scene-build
timings improve by 3.95% and 4.16%.

All ten benchmark smokes and the complete Release gates pass. Two cumulative ABBA
blocks against original 1.0.26 measure CPU -22.76% (95% CI -28.24% to -16.46%), RSS
+0.46% (-0.77% to +1.73%), and physical footprint +0.41% (-0.66% to +1.53%). Work is
equivalent and no supported process or cadence regression above 3% exists. Ten
machine-verified exact reports make the cumulative decision `accept`.

The matching 66,257-sample post-change symbol trace attributes 3.70% to allocator
leaves, versus 3.89% in the preceding separate attribution run. The targeted
`append_scene` owner falls from 110 ms / 0.171% to 48 ms / 0.072%, and its typed
`local_paint_entry` allocation edge falls from 67 ms / 0.104% to zero samples. Resize
dispatch is again the largest broad owner at 0.35%. Inspection narrows its next exact
candidate to media-only refresh: selector indexes contain all rules and do not depend
on `media_matches`, yet the current path clears and rebuilds them whenever a media
query changes. Root custom properties still require recomputation, so the candidate
must split that work while retaining the full rebuild for stylesheet mutations.

That root-only experiment is rejected. On the exact 256-rule fixture it eliminates
26,000 `index_css_rule` calls across 100 alternating media refreshes, preserves exactly
100 root-variable refreshes and checksum `4050`, and improves informational mean time
by 62.96%. Correctness and package gates pass. In two cumulative product ABBA blocks,
however, CPU regresses by 18.76% (95% CI +10.40% to +27.02%) and rendered FPS by 5.00%
(-8.64% to -0.73%). RSS (+0.13%) and physical footprint (+0.42%) are neutral and work
is equivalent. The full rebuild remains the production default; the root-only path is
retained only as an off-by-default diagnostic while real-page index normalization is
investigated.

The symbol-enabled media candidate trace does not support selector-index rebuilding as
the cause of that product regression. Across separate attribution runs, candidate
resize-dispatch inclusive time is 10,172 ms versus 10,745 ms for the accepted build
(-5.3%), while layout attribution is nearly unchanged. The 65,227-sample candidate
trace assigns 3.52% to allocator leaves and 54.41% to WebScene. The full media rebuild
therefore remains in production and the next bounded target returns to the trace's
intrinsic-size hash/constrain cluster.

The intrinsic-size direct cache is accepted on the current cumulative codebase. A
256-entry front cache was rejected before packaging because it hit only 4 of 2,248
lookups per layout (0.18%). The portable final design removes the document hash table
and stores one shared generation plus two available/size pairs in a document-owned
table indexed by stable native node ID. Exact 1,013- and 1,061-node fixtures eliminate
2,248 and 4,376 hash lookups per layout, record 112 and 620 direct hits, preserve
geometry and allocation work, and improve informational p50 time by 8.85% and 21.55%.
The bounded cost is 24 bytes per native ID while `dom_node` remains 992 bytes, restoring
headroom under the cross-library 1,024-byte limit on Linux.

All 12 benchmark smokes and the complete native, WPT, race, package, and consumer gates
pass. Two cumulative ABBA blocks against original 1.0.26 measure CPU -15.98% (95% CI
-23.86% to -7.74%), RSS +0.50% (-0.81% to +1.85%), physical footprint +0.14% (-1.07%
to +1.39%), and rendered FPS +0.24% (-4.84% to +5.86%). Work is equivalent and no
supported process or cadence regression above 3% exists. Eleven exact reports make the
decision `accept`. This cumulative comparison proves the roadmap remains beneficial
with the change; the entire CPU delta is not attributed to the direct cache alone. The
direct cache is now production default and the old hash table is benchmark control.

A two-block, eight-run product ABBA comparison of the accepted inline-node cache against
the portable document-owned storage refactor also accepts: CPU -0.18% (95% CI -2.85%
to +2.57%), RSS +0.14% (-1.08% to +1.40%), and physical footprint +0.79% (-0.25% to
+1.85%). Exact work is equivalent and no supported process or cadence regression above
3% exists, so this portability fix preserves the accepted cumulative gain.

The 60,138-sample post-acceptance trace corroborates that mechanism across separate
attribution runs. Intrinsic-key-specific hash leaves fall from 1,309 ms / 2.01% to
zero, generic `__constrain_hash` falls from 793 ms to 130 ms, and intrinsic-size
inclusive time falls from 7,772 ms / 11.92% to 5,785 ms / 9.62%. Residual generic hash
time belongs to other tables. `compute_intrinsic_size` is now the largest WebScene leaf
at 1,289 ms / 2.14%, so its typed allocation edge becomes the next bounded target.

The first follow-up is a deliberately small accepted win: replace the intrinsic table
row collector's allocating `std::function` recursion with a generic self-recursive
lambda. The exact 1,061-node fixture preserves checksum `259651`, node size, and retained
scratch while reducing allocations from 616 to 604 (-1.95%) and requested bytes from
28,784 to 28,400 (-1.33%). Informational p50 improves 3.91% and p95 regresses 10.81%,
so neither timing number supports acceptance. Complete correctness gates pass. Two
cumulative ABBA blocks measure CPU -9.85% (95% CI -19.48% to +0.51%), RSS -0.25%
(-1.49% to +1.02%), physical footprint -0.64% (-1.71% to +0.44%), and rendered FPS
-3.83% (-7.85% to +1.28%). Work is equivalent, no supported regression above 3%
exists, all 12 exact reports pass, and the decision is `accept` on exact small-win
evidence rather than timing.

Diagnostic-only intrinsic branch counters then separate the remaining common paths.
Each inline fixture performs 2,136 intrinsic computations per layout: 896 text leaves,
1,124 generic containers, and 116 definite-size returns. The accepted text-node fast
path handles all 896 text leaves before input, replaced-element, list-marker, pseudo,
and child probes, eliminating 4,480 legacy tag comparisons per layout. Both exact
fixtures preserve checksum `377393`, allocation and requested-byte totals, node size,
and retained scratch. Informational p50 changes by only -0.22% and -0.46%, so it is not
used as timing evidence.

Complete correctness gates pass. Two cumulative ABBA blocks against original 1.0.26
measure CPU -13.75% (95% CI -21.39% to -5.12%), RSS -0.30% (-1.65% to +1.05%),
physical footprint -0.61% (-1.88% to +0.68%), and rendered FPS +2.74% (-0.88% to
+7.57%). Work is equivalent, no supported process or cadence regression above 3%
exists, all 13 exact reports pass, and the decision is `accept`. The next bounded
investigation is the 1,124-1,372 generic-container computations per inline/table layout.

That instrumentation found an exact common case: none of the 168-1,372 measured
generic-container visits needed an inside list marker or generated pseudo-element, yet
the legacy path copied 1,356-2,076 child pointers per layout into a temporary PMR
vector. The accepted direct-child view skips that materialization only when all three
synthetic-item predicates are false and retains the existing vector fallback
unchanged. Paired 30-by-100 runs across all four fixtures eliminate every measured
generic pointer copy while preserving branch counts, geometry, allocation totals,
1,024-byte nodes, and retained scratch. Separate-executable timing is mixed and is not
used as evidence.

All 19 benchmark smokes and the complete native, WPT, race, package, and consumer gates
pass. Two cumulative ABBA blocks against original 1.0.26 measure CPU -29.04% (95% CI
-33.78% to -23.63%), RSS +0.25% (-0.98% to +1.50%), physical footprint -0.95% (-2.06%
to +0.24%), and rendered FPS +3.44% (-2.25% to +9.98%). Work is equivalent, no
supported process or cadence regression above 3% exists, all 14 exact reports pass,
and the decision is `accept`. The full CPU delta belongs to the cumulative roadmap and
is not attributed to this one small pointer-copy change. The next roadmap item is a
measured audit of background cached-code consumption.

That audit does not select a production experiment. In each of four accepted product
runs, four engines consume 101 cached compilation units apiece: one engine reads
792,568 persistent bytes and the other three reuse the same process buffer. Total
compilation/deserialization time across all engines ranges from 7.48 to 47.63 ms, with
a 12.88 ms median, and all of it completes before the measured workload. The current
path learns the cache key only after source construction and compiles immediately, so
there is little independent work to overlap. Background deserialization would shift
startup CPU to another thread rather than reduce it. Reconsider it only if a dedicated
startup-readiness profile identifies cached-code consumption as material; the next
steady-state step is a post-direct-view symbol trace.

That 45,447-sample resize trace assigns 54.44% to WebScene, 23.11% to system code,
10.83% to managed code, and 4.92% to Skia. Allocator leaves are 4.15%, and the removed
generic intrinsic-item pointer edge remains absent. The largest bounded allocation
owners are resize dispatch (0.216%), cascade application (0.183%), animation-frame
delivery (0.128%), CSS indexing (0.108%), canvas paint-state emission (0.103%), and
intrinsic computation (0.092%). The intrinsic allocation resolves specifically to SVG
`viewBox` stream destruction, so the next experiment is a direct four-number parser,
not a broad rewrite of all stream parsing.

A parallel positional-selector sibling experiment is rejected despite exact local
work removal. Its focused fixture preserves 115,728 matches and 2,249,472 sibling
visits while removing 229,152 vectors and 4,449,792 pointer copies; informational mean
time improves 29.98%. Complete correctness and package gates pass. Two product ABBA
blocks, however, measure presented FPS -5.61% (95% CI -10.58% to -0.74%), a supported
cadence regression above the 3% guard. CPU +0.61%, RSS -0.25%, and physical footprint
-0.85% are neutral, and work is equivalent. Production therefore retains vector
materialization; direct scans remain only behind the off-by-default
`WEBSCENE_NATIVE_ENGINE_SELECTOR_SIBLING_SCAN_EXPERIMENT` switch.

The bounded SVG parser is accepted as a proven small win. Its 257-node fixture makes
512 intrinsic `viewBox` parses per layout and removes all 512 stream constructions
while preserving every successful result, checksum `89219.3`, zero allocation work,
1,024-byte nodes, and 152,856 bytes of retained scratch. Informational p50 and p95
improve about 71.3%. Complete correctness and package gates pass. Two incremental
product ABBA blocks against the accepted direct-view build measure CPU +8.90% (95% CI
-2.18% to +20.97%), RSS +0.85% (-0.45% to +2.19%), and physical footprint +0.97%
(-0.20% to +2.21%). Work and cadence are equivalent and no supported regression above
3% exists, so exact work removal qualifies the candidate. Product CPU remains neutral;
the next bounded investigation is residual canvas paint-state string ownership.

The canvas string-value cache is rejected despite exact local work removal. In the
2,000-draw fixture it preserves 12,000 property probes, reduces UTF-8 conversions from
2,005 to 6 (-99.70%) and stack comparisons from 11,994 to zero, and records 11,994
cache hits. Informational mean/p50/p95 improve about 10-11%, and complete correctness
and package gates pass. Two incremental product ABBA blocks measure CPU +2.30% (95% CI
-0.70% to +5.42%), RSS +0.47% (-0.85% to +1.79%), and physical footprint +1.19%
(+0.21% to +2.16%). Work is equivalent, but resize-publication p95 regresses 3.86%
(95% CI +1.52% to +7.00%), violating the cadence guard. Production retains direct
comparison/conversion; the cache remains only behind the off-by-default
`WEBSCENE_NATIVE_ENGINE_CANVAS_PAINT_STRING_CACHE_EXPERIMENT` switch. The next bounded
target is CSS cascade/index ownership, excluding the already rejected media-refresh
and sibling-scan designs.

The non-owning CSS class-index lookup experiment is also rejected. Its exact
media-refresh fixture preserves 26,000 index operations, 100 root-variable refreshes,
100 class lookups, and checksum 4,050 while reducing owned lookup keys from 100 to zero
and copied key bytes from 5,100 to zero. Complete correctness and package gates pass.
Two incremental product ABBA blocks measure neutral CPU and memory, but
presentation-interval p95 regresses 9.00% (95% CI +0.66% to +17.90%). Production
therefore retains the original owning keys and map types; the view path remains only
behind `WEBSCENE_NATIVE_ENGINE_CSS_CLASS_LOOKUP_VIEW_EXPERIMENT`.

The recursive inline-box bounds walker is accepted as the next allocation win. A
1,013-node exact fixture reduces allocations per layout from 512 to 288 (-43.75%) and
requested bytes from 17,952 to 7,200 (-59.89%) while preserving checksum 377,393,
the 1,024-byte node footprint, and 20,192 bytes of retained scratch. Complete gates
pass. Two incremental product ABBA blocks measure CPU -4.24% (95% CI -8.56% to
+0.01%), neutral memory, equivalent work, and no supported regression above 3%.
Presentation-interval p95 improves 11.79% (95% CI -15.72% to -7.85%). The generic
self-recursive lambda is the production default; the old `std::function` remains only
as a benchmark control.

No additional scheduler or cross-instance change is selected. The residual animation-
frame allocation attribution belongs to task and V8 callback lifetime, not a redundant
native scratch container that can be removed without changing callback order. Shared
2x2 remains rejected because its -4.85% physical-footprint result costs +7.80% CPU,
and four-engine cached-code compilation/deserialization has a 12.88 ms median outside
the measured interaction interval. Cross-process locking remains conditional on a
future measured multi-process cold-start stampede; no current workload demonstrates
one.

## Final roadmap checkpoint

The approved review milestone stops at accepted optimization 14, the generic
intrinsic-item direct-child view. Its two-block cumulative comparison against the
original WebScene 1.0.26 build records CPU -29.04% (95% CI -33.78% to -23.63%),
neutral memory, equivalent work, no supported cadence regression above 3%, and all
14 exact reports passing.

Optimizations 15 and 16 remain implemented and exactly proven, but are held from the
approved cumulative milestone. Their combined two-block comparison against milestone
14 is neutral for CPU, memory, and cadence, preserves work, attaches both exact
reports, and records `accept`. However, two separate cumulative comparisons of the
16-change build against original 1.0.26 both fail the presentation-cadence guard:

| Confirmation | CPU | Presentation-interval p95 | Resize-publication p95 | Decision |
| --- | ---: | ---: | ---: | --- |
| First | -15.31% (-18.74% to -11.75%) | +12.85% (+8.49% to +17.05%) | -18.89% (-24.36% to -12.36%) | Hold |
| Repeat | -12.49% (-14.95% to -9.99%) | +8.19% (+1.74% to +16.28%) | -14.63% (-18.12% to -11.85%) | Hold |

This is deliberately conservative: the isolated evidence does not identify either
small win as the cause, but the original-baseline acceptance contract does not allow
promotion while the supported presentation regression remains unexplained.

The current evidence ledger is therefore:

| State | Work |
| --- | --- |
| Promoted | Optimizations 1-14: bounded layout/intrinsic scratch, callback and ownership fast paths, Canvas save reuse, inline paint order, intrinsic direct cache/collectors/text/direct-child paths |
| Held | 15: bounded SVG `viewBox` parser; 16: recursive inline-box bounds lambda |
| Rejected | root-only media refresh, positional sibling scan, Canvas paint-string cache, CSS class-key view, shared-isolate 2x2 |
| Closed without a candidate | background cached-code consumption, residual scheduler callback lifetime, timer-order changes, cross-process cache locking |

Stop this optimization roadmap here. Reopen it only when a fresh post-milestone trace
identifies a typed owner large enough to measure (roughly 0.1% or more of samples), a
production workload changes materially, a dedicated startup profile makes cached-code
consumption material, or a real multi-process cold start proves a cache stampede. Any
reopened candidate must again provide exact causal evidence, full correctness gates,
equivalent useful work, and no supported non-target regression above 3%.

Two ABBA blocks proved that the experimental shared 2x2 profile reduces
physical footprint by 4.85% (95% CI -5.49% to -4.22%) but raises CPU by 7.80%
(95% CI +0.37% to +15.40%) with equivalent useful work. It fails the
no-performance-trade gate and is not a production candidate.

## Non-negotiable constraints

1. Do not add consumer-specific code to WebScene. Optimize standards-level
   operations and generic runtime behavior.
2. Do not share a Window, Document, JavaScript global realm, DOM, storage
   namespace, or mutable application state between documents.
3. Do not move engine work onto Avalonia's UI thread.
4. Do not introduce a native child window or an airspace-restricted browser
   surface.
5. Do not report a performance win from live market data alone. Use the
   deterministic workload and matched builds.
6. Do not trade correctness for partial rendering. Damage must never create
   stale, transparent, or black pixels.
7. Reparenting a WebScene control must preserve the engine/context whenever
   ownership and lifetime still permit it. It must not reload the page merely
   because its Avalonia visual parent changed.
8. Keep normal diagnostics disabled or below 1% overhead. Detailed counters
   may be enabled explicitly for profiling.
9. One work package per commit. Include tests and before/after evidence in the
   same change or an immediately following documentation commit.

## Measurement protocol

Establish the complete protocol before optimizing. Run the directly affected
workload after every candidate change and the complete production matrix after
each accepted group so small wins can accumulate without losing cumulative
proof.

### Workloads

Measure all of the following using Release builds:

| Workload | Purpose |
| --- | --- |
| Empty benchmark window | Fixed Sandwich/Avalonia overhead |
| 1 deterministic chart | Per-document startup and steady state |
| 2 deterministic charts | Scaling and shared-resource effects |
| 4 deterministic charts, steady | Production resource acceptance workload |
| 4 charts, pan/axis/resize | Production interaction acceptance workloads |
| 8 charts, steady/resize | Scaling and stability stress workloads |
| 4 hidden after ready | Timer/background and visibility policy |
| 4 charts, reparent cycle | Lifetime correctness and leaks |

The Sandwich harness is enabled with `--chart-benchmark-count 0|1|2|4|8`;
`--chart-benchmark-2x2` remains a four-chart alias. Scenarios are selected with
`--chart-benchmark-scenario steady|pan|axis|resize`. The consumer-side runner
selects deterministic replay and independent or shared-isolate profiles. It is
maintained in the consumer repository and must not be copied into WebScene.

### Collection

- Wait until every chart reports ready.
- Warm for at least 30 seconds.
- Collect at least 60 one-second samples for CPU and RSS.
- Use two ABBA blocks for production decisions: control, candidate, candidate,
  control, repeated once.
- Record median and mean CPU, mean RSS, RSS range, engine/context count,
  rendered scene count, published/superseded scene count, diff count, and
  market update count.
- Reject a run when delivered update or rendered-scene work differs
  materially between variants.
- Use the same screen state, window size, power state, build configuration,
  package layout, and sampling tool.
- Account for all processes if a comparison includes a multi-process browser.

### Adoption thresholds

The cumulative milestone should target at least one of:

- 10% lower four-chart steady-state CPU;
- 10% lower total four-chart RSS or 15% lower incremental RSS per chart;
- 20% lower cold context startup;
- removal of at least 90% of allocation on a hot boundary;
- a material standards-correctness improvement with no measurable regression.

These are milestone targets, not per-change minimums. Smaller improvements may
be retained and accumulated when they are proven by a statistically supported
process-level result or an exact causal counter, the full product benchmark is
neutral outside the target, and no non-target metric has a supported regression
above 3%. Record every candidate in the evidence ledger and rerun the cumulative
candidate against the original milestone baseline after each accepted group.

## Work package 1: unified low-overhead diagnostics

Implement this first. Optimization without workload accounting has already
produced misleading conclusions.

Expose process and per-context counters for:

- timers scheduled, fired, cancelled, late, and coalesced;
- `requestAnimationFrame` requested, invoked, and skipped;
- microtask checkpoints;
- style invalidations, style recalculations, layouts, paints, and their time;
- DOM mutations and MutationObserver/ResizeObserver deliveries;
- scene builds, no-op builds, publications, presentations, superseded scenes,
  full checkpoints, and damage area;
- compositor wake requests and actual wakes;
- generated binary calls/callbacks, arbitrary evaluation calls, JSON
  materializations, bytes encoded/decoded, leases, and pool misses;
- selector cache, script cache, resource cache, font cache, and decoded-image
  cache hits/misses;
- V8 heap used/committed, native DOM bytes, scene bytes, mapped resource bytes,
  decoded image bytes, and retained interop arenas.

Counters must be readable without parsing log text. Prefer a snapshot API with
monotonic integers and durations. Add a reset/baseline facility for tests, but
do not make production correctness depend on reset.

Acceptance:

- Disabled overhead is unmeasurable; enabled overhead is below 1% in the
  four-chart benchmark.
- Counter invariants are tested, including publication/presentation accounting
  and zero outstanding interop leases after disposal.
- The benchmark can prove that two compared runs performed equivalent work.

## Work package 2: timer, animation-frame, and compositor scheduling

This is the highest-probability CPU investigation.

Profile callback frequency and determine which callbacks produce a style,
layout, scene, or visible change. Then evaluate generic changes such as:

- one process-level scheduler coordinating due work across contexts while
  preserving each context's event-loop ordering;
- aligning animation-frame delivery with the Avalonia compositor cadence;
- issuing at most one pending compositor wake per surface;
- suppressing scene construction/publication when no visual state changed;
- avoiding repeated clock polling when the next timer deadline is known;
- standards-compatible timer clamping and coalescing;
- throttling background documents only when host visibility is genuinely
  false, with immediate recovery when visible;
- preventing hidden documents from producing presentation work while still
  running required non-visual JavaScript at the appropriate policy rate.

Do not merge event loops or mutable realms merely because contexts share an
isolate. Preserve timer order, microtask checkpoints, promise behavior, input
ordering, and visibility events.

Acceptance:

- Deterministic updates delivered and visible results are unchanged.
- Pointer interaction, dialogs, chart drag, resize, and first-frame liveness
  remain correct.
- Hidden/visible transitions have automated coverage.
- Four-chart CPU improves by at least 10%, or the investigation is documented
  and closed with evidence identifying the next dominant cost.

## Work package 3: eliminate repeated no-op DOM/style/layout work

Use profiles and the new counters to rank operations. Do not introduce broad
memoization without invalidation rules.

Investigate:

- native accessors for frequently used standard Document/Window properties so
  misses do not fall through to whole-DOM named-property scans;
- O(1) ID/name indexes with correct mutation and tree-adoption invalidation;
- computed-style reuse keyed by the actual cascade dependencies;
- narrower style invalidation for class, attribute, state, and subtree changes;
- avoiding layout when changed properties cannot affect geometry;
- batching observer delivery at the proper microtask boundary;
- deduplicating repeated identical ResizeObserver results;
- preserving and extending the compiled selector cache without reparsing for
  validation;
- cache hit-rate and retained-size limits to prevent unbounded growth.

Start with the top symbolized operation from a deterministic Time Profiler
capture. Add a focused microbenchmark and a standards regression test before
changing it.

Acceptance:

- The focused operation improves materially, preferably at least 2x.
- Required DOM/CSS tests and relevant WPT subsets pass.
- The deterministic four-chart run does not regress CPU or RSS.

## Work package 4: reduce per-context memory duplication

The isolate is already shared. Build a 0/1/2/4-context memory model before
changing ownership. Report fixed process cost and incremental cost per context.

Attribute retained memory to:

- V8 context heaps and embedder/native objects;
- bootstrap/prototype data not covered by the existing snapshot;
- compiled scripts and code cache;
- DOM nodes, attributes, strings, style data, and layout boxes;
- immutable and in-flight scenes;
- mapped/decoded resources;
- fonts, glyphs, typefaces, images, and canvas surfaces;
- callback registrations, handles, and interop arenas.

Safe sharing candidates include immutable snapshot pages, compiled script
cache entries keyed by source and runtime identity, memory-mapped resource
bytes, immutable font/typeface objects, decoded immutable images, and bounded
compiled-selector entries. Mutable DOM, layout, canvas, storage, event, and
application objects must remain context-owned.

Add lifecycle so shared caches release or evict data. Run repeated create,
reparent, detach, destroy, and GC/idle cycles to find retained contexts.

Acceptance:

- At least 15% lower incremental RSS per chart or 10% lower total four-chart
  RSS, with no context leakage after the lifecycle soak.
- Cache ownership is thread-safe and bounded.
- Separate documents cannot observe or mutate each other's state.

## Work package 5: verify generated binary interop end to end

The binary ABI is implemented; this package is an audit and hardening task, not
a request to design another transport.

- Prove that schema-known generated calls and callbacks never fall back to
  JavaScript source generation or JSON.
- Count arbitrary evaluation separately from generated operations.
- Report bytes/call, pool hit/miss, leases, operation slots, callback latency,
  and managed allocation.
- Use borrowed/leased results for large arrays when callers can consume them
  synchronously without materialization.
- Ensure callback targets and retained handles become invalid safely during
  detach, reparent, navigation, and engine destruction.
- Add an optional benchmark assertion that fails if a generated hot operation
  materializes JSON.

Acceptance:

- Normal generated void callbacks remain near allocation-free and leave zero
  outstanding leases.
- Four-context callback stress passes cancellation/disposal races.
- The consumer's market-data and broker callback path is confirmed binary by
  counters, without adding its API files to this repository.

## Work package 6: asset, font, and image reuse

Network and parsing startup work may be duplicated even when the V8 isolate is
shared. Measure before implementing.

Investigate process-wide immutable reuse for:

- mapped response bodies and packaged resources;
- V8 code-cache entries;
- CSS source/token storage where ownership permits it;
- font files, typefaces, and glyph caches;
- decoded immutable images and SVG resources.

Keys must include all data that affects correctness: URL, origin/security
policy, response validators, runtime/ABI version, content hash, decoding
options, device scale where relevant, and font variation/style.

Acceptance:

- Context isolation and cache invalidation tests pass.
- Cold and warm startup are measured separately.
- Retained shared data is bounded and included in memory diagnostics.

## Work package 7: composition and damage, evidence gate

Do this only after deterministic profiling shows composition, rasterization,
pixel upload, or whole-surface redraw is a material remaining cost. The prior
profile placed Avalonia/Skia well below native/V8 CPU, so this is not currently
the first target.

First add evidence:

- signposts around scene traversal, Skia recording/rasterization, texture or
  pixel upload, and compositor presentation;
- bytes uploaded/copied per frame;
- full surface area versus damaged area;
- GPU/Metal and CPU time;
- frame drops and presentation latency.

If partial damage is pursued, use a correctness-preserving design:

1. Keep a retained backing store or retained compositor primitives.
2. Repaint damage into preserved content without clearing undamaged pixels.
3. Promote to a full checkpoint after attach, reparent, resize, scale change,
   compositor recreation, surface loss, or uncertain dependency.
4. Include effects whose pixels extend beyond local geometry: shadows,
   filters, clips, transforms, antialiasing, text, canvas, and SVG.
5. Validate with deterministic pixel comparisons and forced random damage.

A shared texture/IOSurface path is only a candidate after tracing proves that
CPU copies or uploads are significant. It must preserve Avalonia composition,
transforms, clipping, opacity, input, and airspace behavior. Do not replace the
renderer or embed a native child window as part of this experiment.

Acceptance:

- No black rectangles, stale pixels, flicker, or resize regression.
- Pixel tests cover damage and forced full-frame fallback.
- At least 10% whole-application CPU reduction or a separately material GPU/
  latency improvement justifies the added complexity.

## Work package 8: lifecycle, reparenting, and visibility soak

Treat this as a cross-cutting acceptance package.

Automate:

- attach, ready, detach, reattach to the same window;
- move between Avalonia parents;
- move to a floating window and back;
- minimize, hide, show, occlude where observable, and restore;
- resize and device-scale changes;
- compositor loss/recreation;
- dispose while callbacks, timers, promises, and scene publications are live.

Verify that compatible reparenting retains engine identity, document state,
subscriptions, storage, and generated handles. When recreation is genuinely
required, fail or checkpoint explicitly rather than silently presenting a
partially valid scene.

Acceptance:

- No reload during compatible reparenting.
- No runtime exception, missing market updates, blank scene, or stale input.
- Context and native resource counts return to baseline after the soak.

## Consumer-side recommendations

These belong in Sandwich or its served application, not in WebScene:

- use supported application configuration to disable unused browser-app
  features, studies, services, and persistence traffic;
- avoid high-frequency polling shims when WebScene provides the standard API;
- preserve deterministic replay as the performance acceptance workload;
- distinguish initialization time from steady-state measurement;
- audit direct remote hosting versus a small local bootstrap page separately;
- verify that all generated hot APIs use binary codecs and callbacks.

Do not patch third-party served JavaScript merely to hide a WebScene standards
gap. Implement the generic missing standard behavior in WebScene and add a
reduced regression test.

## Recommended execution order

1. Re-establish the 1.0.16 deterministic 0/1/2/4-chart baseline.
2. Implement unified diagnostics.
3. Profile and optimize scheduling/no-op frames.
4. Profile and optimize the hottest DOM/style/layout operation.
5. Attribute and reduce per-context memory.
6. Audit binary interop and shared immutable assets.
7. Re-profile. Pursue composition/damage only if it is now material.
8. Run lifecycle, pixel, and package acceptance before version bump/publish.

## Agent completion report

An implementing agent must finish with:

- commits and changed file list;
- exact control and candidate revisions;
- exact build configuration and native library/package hashes;
- commands used to reproduce tests and benchmarks;
- ABBA CPU/RSS results and work counters;
- focused benchmark and profile attribution;
- correctness, lifecycle, and visual-test results;
- rejected hypotheses and why they were rejected;
- remaining risks and the next highest measured hotspot.

Do not describe a microbenchmark improvement as an application improvement
unless the deterministic four-chart acceptance run supports it.

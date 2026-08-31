# Native engine benchmarks

This project contains native ABI, binary interop, DOM lookup, context-memory, runtime
work, and lifecycle measurements. It has no managed-engine dependency.

## Layout scratch allocation

The V8-free native layout benchmark provides deterministic fixtures for recurring
layout scratch:

- `four-chart-nested-flex-v1`: a 797-node, four-chart-shaped nested flex tree.
- `intrinsic-table-select-v1`: a 1,061-node intrinsic/table/select tree covering
  table-row, collapsed-select-option, and generic intrinsic-item vectors.
- `inline-text-v1`: a 1,013-node, four-chart-shaped tree covering wrapped and aligned
  text runs, positioned inline items, static anchors, and line geometry.
- `inline-font-family-v1`: the same 1,013-node inline workload with a realistic inherited
  font-family list long enough to expose owning-string copies.
- `intrinsic-svg-view-box-v1`: 256 auto-sized SVG elements whose intrinsic dimensions
  come from `viewBox`, isolating its four-number parser during repeated layout.

A benchmark-local global allocator counts only allocations made while
`native_document::layout` runs; fixture construction, warm-up, result serialization and
the counter itself are outside the measured interval. Every sample must produce exactly
the same allocation totals and geometry checksum.

```bash
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/layout-scratch-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=OFF \
  -DWEBSCENE_NATIVE_ENGINE_HTML_PARSER=legacy \
  -DWEBSCENE_NATIVE_ENGINE_CSS_PARSER=legacy \
  -DWEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER=legacy \
  -DWEBSCENE_NATIVE_ENGINE_DOM_BINDINGS=legacy \
  -DWEBSCENE_NATIVE_ENGINE_BUILD_LAYOUT_SCRATCH_BENCHMARK=ON
cmake --build artifacts/layout-scratch-benchmark \
  --target webscene_layout_scratch_control_benchmark \
           webscene_retained_paint_order_control_benchmark \
           webscene_layout_callback_control_benchmark \
           webscene_inline_layout_scratch_control_benchmark \
           webscene_text_measurement_lookup_control_benchmark \
           webscene_text_transform_copy_control_benchmark \
           webscene_font_family_view_control_benchmark \
           webscene_layout_scratch_benchmark
artifacts/layout-scratch-benchmark/webscene_layout_scratch_benchmark \
  --fixture four-chart-nested-flex-v1 \
  --warmups 10 --samples 30 --iterations 100
```

`webscene_layout_scratch_control_benchmark` is built from the same source but defines
`WEBSCENE_NATIVE_ENGINE_INTRINSIC_SCRATCH_CONTROL=1`. It preserves ordinary
`std::vector` allocation for intrinsic/table/select scratch while retaining the already
accepted general layout pool, allowing the second increment to be measured against its
real accepted predecessor rather than the original runtime.

`webscene_retained_paint_order_control_benchmark` retains both accepted scratch-pool
increments but defines `WEBSCENE_NATIVE_ENGINE_RETAINED_PAINT_ORDER_CONTROL=1`, which
keeps the unconditional composed-child copy. Compare it with
`webscene_layout_scratch_benchmark` to isolate the default-z-index paint-order fast
path from the already accepted cumulative source.

`webscene_layout_callback_control_benchmark` retains the accepted scratch-pool and
paint-order increments but defines `WEBSCENE_NATIVE_ENGINE_LAYOUT_CALLBACK_CONTROL=1`,
which keeps the inline text-run collector's self-recursive `std::function`. Compare it
with `webscene_layout_scratch_benchmark` to isolate the generic self-recursive lambda
from all earlier accepted changes.

`webscene_inline_layout_scratch_control_benchmark` retains the accepted recursive
callback change but defines `WEBSCENE_NATIVE_ENGINE_INLINE_LAYOUT_SCRATCH_CONTROL=1`,
keeping inline text-run, positioned-item, static-anchor, and line-alignment vectors on
the ordinary allocator. Compare it with `webscene_layout_scratch_benchmark` on
`inline-text-v1` to isolate reuse through the bounded document pool.

`webscene_text_measurement_lookup_control_benchmark` retains the accepted inline
scratch-pool change but defines
`WEBSCENE_NATIVE_ENGINE_TEXT_MEASUREMENT_LOOKUP_CONTROL=1`, constructing an owning
text-measurement key before lookup. Compare it with
`webscene_layout_scratch_benchmark` on `inline-text-v1` to isolate transparent lookup
and owning-key construction only on cache miss.

`webscene_text_transform_copy_control_benchmark` retains the accepted heterogeneous
lookup but defines `WEBSCENE_NATIVE_ENGINE_TEXT_TRANSFORM_COPY_CONTROL=1`, preserving
the owning mutable copy before resolving text transform. Compare it with
`webscene_layout_scratch_benchmark` on `inline-text-v1` to isolate the
`text-transform:none` string-copy fast path.

`webscene_font_family_view_control_benchmark` retains the accepted text-transform fast
path but defines `WEBSCENE_NATIVE_ENGINE_FONT_FAMILY_VIEW_CONTROL=1`, returning an
owning font-family string. Compare it with `webscene_layout_scratch_benchmark` on
`inline-font-family-v1` to isolate non-owning inherited font-family resolution.

`webscene_canvas_save_benchmark` is a V8-aware save/restore fixture. It wraps all 18
Canvas 2D state properties with deterministic accessors, validates those properties and
line dash after every restore, and reports getter reads performed inside `save()`. Build
the control with `WEBSCENE_NATIVE_ENGINE_CANVAS_SAVE_SNAPSHOT_CONTROL=ON`; the accepted
candidate reuses the state just synchronized by `canvas_emit_paint_state`.

`webscene_canvas_paint_state_benchmark` repeatedly draws text with a long unchanged
font value and reports exact string-property probes, UTF-8 conversions, stack
comparisons, and cached-value hits. Build the rejected cache candidate with
`WEBSCENE_NATIVE_ENGINE_CANVAS_PAINT_STRING_CACHE_EXPERIMENT=ON`; production keeps the
direct comparison path. The experiment removes 99.70% of focused conversions but
failed the product resize-publication cadence gate, so focused timing cannot promote it.

`webscene_media_refresh_benchmark` alternates the viewport across a media-query
boundary and validates the resulting root custom property, computed width, and
`matchMedia` state after every refresh. Benchmark-only counters report calls to
`index_css_rule`, root-variable refreshes, class-index lookups, and owned class-key
copies. The production control performs the full
rebuild; build the rejected candidate with
`WEBSCENE_NATIVE_ENGINE_MEDIA_REFRESH_ROOT_ONLY_EXPERIMENT=ON`. That candidate
recomputes root variables while retaining selector indexes. Although it eliminates the
targeted exact work, the cumulative product gate found a supported CPU and cadence
regression, so the experiment is off by default.

For the separate rejected class-key view experiment, build the candidate with
`WEBSCENE_NATIVE_ENGINE_CSS_CLASS_LOOKUP_VIEW_EXPERIMENT=ON` and use
`compare_css_class_lookup_benchmark.py` for an ABBA exact-counter report. It eliminates
owned class lookup keys, but failed the product presentation-cadence gate and remains
off by default.

The layout benchmark also accepts `--phase scene`. The paired
`webscene_scene_paint_order_control_benchmark` and
`webscene_scene_paint_order_benchmark` targets count allocations made by
`native_document::build_scene` after persistent output buffers are warmed, and preserve
a deterministic command/string checksum. The candidate keeps eight local paint entries
inline and uses the original reserved vector for larger sibling sets.

The paired `webscene_intrinsic_size_direct_cache_control_benchmark` and
`webscene_intrinsic_size_direct_cache_benchmark` targets compare the retained
pointer/axis hash map with a document-owned memo table indexed by stable native node ID.
Each entry shares one generation across its two axes, retaining direct O(1) lookup at
24 bytes per native ID without increasing `dom_node` beyond 992 bytes. The fixture
reports eliminated hash lookups and preserved allocation, geometry, and node-footprint
work. Product ABBA evidence accepted the direct cache, so it is the production default;
control builds can restore the hash table with
`WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_HASH_CACHE_CONTROL=ON`.

The paired `webscene_intrinsic_row_collector_control_benchmark` and
`webscene_intrinsic_row_collector_benchmark` targets isolate the table-row traversal
inside `compute_intrinsic_size`. The control retains `std::function` recursion; the
candidate uses a generic self-recursive lambda. Use `intrinsic-table-select-v1` and
accept only a reduction in exact allocation calls and requested bytes with identical
node, geometry, node-size, and retained-scratch values.

`webscene_intrinsic_size_branch_benchmark` adds thread-local diagnostic counters only
to its own binary and reports the mutually selected intrinsic-size paths per layout.
It must not add fields to `native_document` or `dom_node`. The paired
`webscene_intrinsic_text_fast_path_control_benchmark` and
`webscene_intrinsic_text_fast_path_benchmark` targets retain the legacy text traversal
or take the early `dom_node_kind::text` path. Compare exact fast-path coverage and
preserved branch, geometry, allocation, footprint, and scratch work; timing is
informational only.

The paired `webscene_intrinsic_item_copy_control_benchmark` and
`webscene_intrinsic_item_copy_benchmark` targets isolate generic-container intrinsic
item composition. The candidate directly iterates composed children when no inside
marker, `::before`, or `::after` item exists; the control always materializes the
temporary PMR pointer vector. Require identical branch selection, geometry,
allocation totals, node footprint, and retained scratch, plus exact direct-view hits
and eliminated pointer copies. The synthesized-item fallback remains unchanged.

The paired `webscene_intrinsic_view_box_control_benchmark` and
`webscene_intrinsic_view_box_benchmark` targets isolate the traced SVG `viewBox`
allocation inside `compute_intrinsic_size`. The control retains `std::istringstream`;
the candidate scans exactly four floating-point components without constructing a
stream. Use `intrinsic-svg-view-box-v1` and require identical geometry, node footprint,
and retained scratch plus a deterministic allocation reduction. Timing remains
informational until the product ABBA gate.

The paired `webscene_inline_box_bounds_control_benchmark` and
`webscene_inline_box_bounds_benchmark` targets isolate the recursive bounds propagation
for flattened inline boxes. The control retains `std::function`; the candidate uses a
generic self-recursive lambda. Use `inline-font-family-v1` and
`compare_inline_box_bounds_benchmark.py`; require identical geometry, footprint, and
retained scratch plus lower exact allocation calls and requested bytes.

Save control and candidate JSON output, then validate exact fixture equivalence,
allocation-call reduction, requested bytes and bounded retained scratch storage:

```bash
python3 experiments/WebScene.NativeEngine.Probe/benchmarks/compare_layout_scratch_benchmark.py \
  --control artifacts/layout-scratch-benchmark/control.json \
  --candidate artifacts/layout-scratch-benchmark/candidate.json \
  --minimum-call-reduction-percent 50 \
  --output artifacts/layout-scratch-benchmark/comparison.evidence.json
```

Timing fields are informational because separate executable builds are not an
interleaved timing experiment. An allocation result qualifies only the targeted causal
boundary; a production change still requires standards/correctness tests and a neutral
multi-instance product comparison. Pass the resulting JSON to Sandwich's ABBA
comparator with `--exact-counter-evidence-file`; descriptive exact-counter notes do not
qualify a candidate for acceptance.

Set `WEBSCENE_NATIVE_ENGINE_PATH` to a built native library, then run BenchmarkDotNet
or a focused probe:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release

WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-context-memory
```

The Inspector idle-cost gate builds the same source twice: once with Inspector
compiled out and once as the patched Inspector-capable production runtime. Both
variants use the same published managed benchmark executable. It reports V8 build
features, managed allocations, prewarm and warm
context startup, first scene, idle CPU, timer/animation-frame throughput, console-heavy
execution, a representative DOM workload, per-engine memory, and multi-view RSS as
machine-readable JSON:

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release --no-build -- \
  probe native-inspector-disabled-performance \
  --contexts 4 --samples 10 --duration-ms 1500
```

Run at least 20 fresh processes per variant in control/candidate/candidate/control order,
then use `scripts/compare-inspector-disabled-performance.py` to enforce the production
thresholds. Timing and idle-CPU decisions use paired bootstrap 95% intervals so process
noise is not mistaken for a regression. The comparison requires an Inspector-free
control and an Inspector-capable candidate, and fails if ordinary work initializes the
managed Inspector registry.

Run with `probe` and no recognized name to list the focused probes.

## Resize cadence

`native-resize-cadence` drives a real Avalonia window at a configurable cadence and
emits machine-readable engine, publication, render, CPU, and latency metrics. Its
default local fixture exercises generic grid, flex, text, synchronous geometry reads,
resize listeners, and animation-frame work; `--url` can qualify any external page
without adding page-specific behavior to the engine.

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resize-cadence --warmup-seconds 2 --seconds 10 --hz 60
```

Add `--composition` to exercise Avalonia's composition projection and `--enforce` to
return a failure unless p95 resize-to-render latency is at most 16.7 ms, throughput is
at least 58 rendered frames/second, no render interval exceeds 33.4 ms, and no input is
dropped. `--output <file>` writes the same JSON emitted on stdout. Use repeated fresh
control/candidate processes for performance decisions, then compare paired directories
with `scripts/compare-native-resize-cadence.py`. The comparator uses paired bootstrap
95% intervals, rejects supported regressions above 3%, and can require both a material
improvement and the practical-vsync gate. It also rejects pairs that mix certification
and production runtimes.

```bash
python3 scripts/compare-native-resize-cadence.py \
  --control-dir artifacts/resize-control \
  --candidate-dir artifacts/resize-candidate \
  --minimum-samples 10 \
  --output artifacts/resize-comparison.json
```

For a browser reference, the CDP profiler launches an isolated headed Chrome process
and applies the same 60 Hz triangular window resize. It records rAF cadence,
resize-to-rAF latency, long tasks, Chrome performance counters, and trace-stage totals:

```bash
node scripts/profile-chrome-resize-cadence.mjs \
  --url https://trading-terminal.tradingview-widget.com/ \
  --warmup-seconds 5 --seconds 10 --hz 60 \
  --output artifacts/chrome-resize.json
```

Use `--headless` only for automation smoke tests; headed Chrome is the visual smoothness
reference.

Pass that immutable Chrome result back to the native probe to report an exact matched
cadence comparison. Add `--enforce-chrome-reference` when the native run should fail
unless its measured presentation throughput and p95 interval equal or beat Chrome:

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resize-cadence --composition \
  --url https://trading-terminal.tradingview-widget.com/ \
  --warmup-seconds 5 --seconds 10 --hz 60 \
  --chrome-reference artifacts/chrome-resize.json
```

## Retained scene rendering

The retained-render probe exercises the Avalonia Skia renderer without requiring the
native V8 library. It clears the surface before every render, checks the sparse result
against a visible-only pixel reference, and reports sparse and fully-visible timing so
viewport-culling gains can be weighed against their dense-scene overhead:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-render --layers 2048 --visible 32 --iterations 40 --samples 11
```

The companion apply probe measures one-layer replacement, batched replacement, and
z-order changes independently. It is intended to catch linear identity lookup and
unnecessary total-order rebuilds in the retained consumer:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-apply --layers 4096 --batch 256 --iterations 100 --samples 11
```

The complete NativePF candidate inventory, measurements, and accepted/rejected
decisions are recorded in
[`docs/nativepf-render-optimization-audit.md`](../docs/nativepf-render-optimization-audit.md).

## Native resource bridge

The resource-bridge probe measures the synchronous managed response-envelope path
independently from HTTP, archive lookup, V8 compilation, and page execution. It reports
both the ABI's required-size probe followed by an exact copy and a speculative
single-copy call with sufficient destination capacity:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resource-bridge --payload-bytes 32768 --iterations 200 --samples 11
```

Pass `--archive` and `--url` to compare decoded-text replay with direct UTF-8 replay
for a captured script. Use `--archive-iterations` to bound work for multi-megabyte
resources. The accepted implementation and baseline/candidate measurements are
recorded in
[`docs/native-resource-bridge-optimization-audit.md`](../docs/native-resource-bridge-optimization-audit.md).

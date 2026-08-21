# NativePF render optimization audit

## Scope and acceptance gate

This audit covers the transferable rendering work in NativePF commits `00640f7`,
`658eab3`, `fcd79e6`, and merge `218a0ef`: dirty publication, damage generation and
normalization, transactional apply/presentation, preserved surfaces, spatial
candidates, incremental identity/order, recyclable scene storage, steady-state
allocation, opaque/occlusion trimming, and renderer resource profiles.

Avalonia is the primary acceptance target. A candidate is retained only when it:

1. reconstructs the same complete viewport from a cleared surface;
2. does not depend on Avalonia retaining pixels outside an invalidation;
3. improves a focused workload without regressing the corresponding dense/control
   workload materially; and
4. preserves the shared Uno build and renderer semantics.

Timing values below are medians of the per-process medians from five fresh Release
processes on the same host. They are useful for comparing the paired implementation
stages, not as cross-machine absolute targets.

## Accepted changes and measured results

### Retained rendering

The renderer now rejects canvas layers whose explicit destination rectangle is outside
the viewport, and caches the ordered in-viewport candidate list until a layer diff or
viewport change invalidates it. It still draws the DOM backdrop, all visible canvas
layers, and the DOM overlay on every Avalonia render callback.

| 2,048-layer workload | Original | Viewport culling | Cached candidates |
|---|---:|---:|---:|
| 32 visible, retained replay p50 | 265.5 us | 34.7 us | 12.6 us |
| all visible, retained replay p50 | 421.4 us | 417.3 us | 396.8 us |
| 32 visible, replace one layer then replay | n/a | 51.9 us | 41.8 us |
| managed allocation for replay | 0 B | 0 B | 0 B |

Every render benchmark iteration clears the target first. The sparse frame SHA-256 is
then compared with a renderer containing only the visible layers, so a stale or
preserved backing surface cannot make the test pass.

### Incremental retained identity and order

Each retained layer now stores its ordered index. Same-z replacements update that slot
directly. A z-order change removes the old slot, finds the new slot with binary search,
and updates only the shifted range. Checkpoints, additions, and removals retain the
conservative complete sort.

| 4,096-layer apply workload | Flat lookup/full sort | Indexed/incremental | Result |
|---|---:|---:|---:|
| replace one layer | 6.17 us | 4.04 us | 35% faster |
| replace 256 unchanged-z layers | 822.6 us | 562.8 us | 32% faster |
| move one layer through z order | 56.5 us | 5.66 us | 10.0x faster |

The apply probe and unit tests verify dictionary identity, sorted order, and stored
indices after sparse replacement, batched replacement, and movement in both directions.

### Validate before mutation

Layer flags and buffer ranges are now validated before checkpoint reset, DOM picture
replacement, or live-layer mutation. This moves the existing validation pass rather
than adding a second hot-path pass. A malformed checkpoint can no longer erase the
accepted retained scene before returning failure. This is accepted primarily as the
transactional prerequisite for future incremental work; the ordinary apply benchmark
does not show a material throughput cost.

## Full disposition matrix

| NativePF idea | WebScene evaluation | Disposition |
|---|---|---|
| Viewport/subtree culling | Canvas layers have exact root-space destination rectangles and are clipped to those same rectangles. A separation test is therefore conservative. | **Accepted and implemented.** |
| Damage-specific spatial hierarchy | WebScene has root canvas layers plus two monolithic DOM pictures, not retained DOM nodes with effective subtree bounds. For current layer counts, a cached ordered candidate list removes repeated scans with less state and maintenance. | **Accepted in reduced form:** cached viewport candidates. Full BVH discarded for this ABI. |
| Stable identity and incremental child order | Canvas node IDs and z order are already stable, but replacement previously searched linearly and z changes sorted everything. | **Accepted and implemented.** |
| Validate-then-apply transaction | Checkpoints previously called `Reset()` before validating layer ranges; an invalid checkpoint destroyed good retained state. | **Accepted and implemented** for flags/ranges before mutation. |
| Old/new damage for moves/replacements/removals | The producer already adds old and new canvas bounds, removed bounds, and conservative old/new DOM command bounds. | **Accepted, already present.** Covered by native damage and Avalonia policy tests. |
| Empty-damage suppression | An empty incremental scene is treated as a synchronization point and does not invalidate or render. | **Accepted, already present.** It preserves consumed-input sequencing without drawing. |
| Validate and clip damage to viewport | `NativeSceneDamagePolicy` validates finite values, scales and clips them, and falls back to full damage for malformed/unspecified visual changes. | **Accepted, already present.** |
| Merge/cap several damage rectangles | Avalonia's custom visual is invalidated with one `Rect`; WebScene already computes the union and records both summed and union area. A bounded eight-rect region would collapse to the same scheduling rectangle. | **Discarded as redundant** until a renderer owns a preserved surface. |
| One-device-pixel final damage inflation | DOM command damage already carries two logical pixels of antialias padding; canvas presentation is clipped to its exact layer rectangle. Extra consumer inflation would only enlarge Avalonia scheduling. | **Discarded as redundant** for current commands. |
| Treat wholly clipped damage as no work | NativePF can prove this from effective ancestor transforms/clips. WebScene transform commands do not yet provide effective subtree bounds, so an apparently offscreen command can affect visible descendants. | **Discarded for correctness** until scene bounds are authoritative. |
| Damage-only replay into preserved content | Avalonia owns the custom-visual backing and can discard it. Commit `16c339a` records exposed/cleared pixels when WebScene returned early or drew only the render clip. | **Rejected:** known flicker risk. Complete replay remains mandatory. |
| Renderer-owned preserved surface | NativePF owns a Metal texture and copies the complete result to transient drawables. WebScene receives an Avalonia Skia canvas and has no lifecycle-safe ownership of its backing texture. An extra WebScene bitmap would add a full-window allocation and copy. | **Rejected for Avalonia.** Reconsider only behind a renderer-owned surface abstraction with expose/resize/device-loss tests. |
| Transactional `presented`/`skipped`/`device_lost` acknowledgement | When Avalonia has no Skia lease, WebScene retains pending presentation state and retries. Every later callback reconstructs the scene, so native damage is not consumed as preserved pixels. | **Accepted equivalent, already present.** Avalonia does not expose NativePF's surface result contract directly. |
| Producer dirty journal and one-node DOM publication | `native_document.build_scene` still flattens a command stream, and the ABI has no stable parent/child record, effective clip, subtree bounds, or per-node command span. Pure synchronization scenes also carry consumed-input sequence. | **Discarded for this ABI change:** requires a versioned hierarchical scene contract and a separate producer benchmark/correctness project, not a local renderer shortcut. |
| Remove dense paint order in favor of hierarchy | DOM order is encoded in the flat command stream; canvas order is a small explicit retained list. Removing either without parent/child ordering changes pixels. | **Discarded for the current ABI.** Incremental canvas order maintenance captures the applicable benefit. |
| Recycle immutable scene allocations/leases | The native producer uses `shared_ptr<const scene>` across acknowledged, pending, and acquired lifetimes. Reusing its vectors safely needs exclusive lease return/custom-deleter ownership and native steady-allocation gates. | **Discarded as a local change.** Borrowed pointer views and bounded pending scenes already avoid a consumer serialization copy. |
| Reuse renderer replay containers | A prototype reused the canvas state stack and text-shaper dictionary. It saved only 136 B per trivial compiled layer (3.8%), had no stable throughput gain, and retained worst-case container capacity for the renderer lifetime. | **Measured and reverted.** |
| Zero-allocation scene update | Full layer replacement necessarily creates a new `SKPicture` and Skia wrappers. The measured trivial replacement still allocates about 3.6 KiB; removing that requires stable command chunks or mutable renderer resources, not collection pooling. | **Discarded for the current picture contract.** Render-only replay remains 0 B. |
| Opaque occlusion trimming | The scene ABI does not assert layer opacity. Canvas clear/composite/global-alpha/shadow operations and transparent overlay canvases make command-stream inference unsafe. | **Rejected for pixel correctness.** Add an explicit producer-proven opacity flag before reconsidering. |
| Minimized/hidden suspension and memory release | Detach and `SetPresentationActive(false)` already stop projection and notify the native engine; reactivation requests a checkpoint. Renderer pictures are reset on detached direct-draw surfaces. | **Accepted, already present.** Automatic OS occlusion is not portable/reliable through current Avalonia APIs. |
| Embedded/balanced/desktop Graphite resource profiles | Avalonia owns the Skia/GPU context and its budgets. WebScene cannot safely trim or reconfigure those resources from a custom visual. | **Rejected at this layer.** This belongs in Avalonia/backend configuration. |
| Idle GPU cache purge | Per-renderer pictures, typefaces, and SVG leases are released on reset; the process SVG cache is reference counted. GPU atlas/pipeline purge remains Avalonia-owned. | **Accepted existing ownership boundaries;** no extra purge added. |
| Diagnostics and bounded-work gates | WebScene already reports diff apply, retained draw, Skia submit, damage count/area, memory, revisions, and rejected diffs. The new probes add repeatable render/apply gates. | **Accepted and extended.** |

## Reproduction

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-render --layers 2048 --visible 32 --iterations 40 --samples 11

dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-apply --layers 4096 --batch 256 --iterations 100 --samples 11
```

The render probe is the flicker/pixel oracle. The apply probe reports managed
allocation and rejects inconsistent retained identity/order. Run both from fresh
processes when changing culling, ordering, picture compilation, or damage behavior.

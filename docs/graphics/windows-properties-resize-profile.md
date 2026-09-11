# Windows properties-divider profiling

Measured 2026-09-09 using the unchanged Kestrel fixture and Windows WebGPU runtime.
The probe option --properties-kestrel injects 600 left-button moves over ten seconds,
five back-and-forth drags of the right properties divider, paced at 60 Hz. ResizeObserver
notifications verify that the panel actually changes size; the final width returns to
its starting value. No fixture files were edited.

## Findings

The dominant measured cost is root-subtree CSS recalculation. Kestrel's divider handler
calls document.documentElement.style.setProperty('--right-width', ...). In WebScene,
the custom-property setter schedules recascade_connected_subtree; the event batch
flush executes recascade_connected_subtree_now and apply_css_rules_subtree on the root.

The direct recascade trace measured 8.49 ms median, 9.86 ms p95, within total input
handling of 8.68 ms median. It runs approximately once per processed drag update.
This is CPU style processing, independent of the GPU presentation backend.

Other sequential median stages: animation callback 4.13 ms; publication 5.51 ms.
Publication includes layout (1.11 ms) and DOM scene construction (0.63 ms);
these nested durations must not be added to publication again. Further subdivision
of the remaining publication cost is still needed. The combined worker stages exceed
the 16.67 ms budget. This trace does not establish pixel copies as the cause.

## Pacing comparison

Clean ten-second runs, first second excluded from rate calculations:

| Setting | Injected moves/s | Published scenes/s | Draw callbacks/s | Draw gap p95 |
|---|---:|---:|---:|---:|
| SINGLE_SCENE_PER_FRAME=1 | 60.00 | 51.13 | 51.08 | 34.12 ms |
| SINGLE_SCENE_PER_FRAME=0 | 60.00 | 51.86 | 51.86 | 33.68 ms |

Median publication-to-draw delay was 10.35 vs 9.90 ms. This small difference does not
implicate the new pacing option as the dominant resize bottleneck. No historical
bisect has established when this resize behavior changed. Draw callbacks are not
physical scanout measurements.

Evidence: artifacts/windows-kestrel/properties-clean-single.log,
properties-clean-coalesce.log, and properties-cascade.log. The earlier properties-single.log
and properties-coalesce.log are discarded: an injected offsetWidth read forced
extra synchronous layout. The clean probes use ResizeObserver instead.

## Next optimization

Track custom-property dependencies so changing a root variable can avoid re-matching
and rebuilding styles for unaffected descendants. Preserve inheritance, variable
fallbacks, dependent variables, and synchronous computed-style/geometry reads.
This investigation adds diagnostics only; it does not implement that optimization.
Kestrel and the isolated font-cache PR remain unchanged.

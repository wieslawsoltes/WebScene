# Left versus right Kestrel divider diagnosis

Measured after merging origin/main c2ceb39 with the Windows Kestrel work.

Both probes inject five identical 120px out-and-back sidebar expansions over ten
seconds at 60 Hz. ResizeObserver verifies panel width changes without synchronous
measurement inside the pointer listener. The fixture is unchanged.

| Metric | Left | Right |
|---|---:|---:|
| Injected input rate | 60.00 Hz | 60.00 Hz |
| Published scene rate | 59.76 Hz | 52.25 Hz |
| Draw callback rate | 59.78 Hz | 52.19 Hz |
| Median input processing | 0.60 ms | 8.63 ms |
| Full-subtree recascade events in measured window | 0 | 469 |
| Median full-subtree recascade | n/a | 8.39 ms |

These are application callback measurements, not physical scanout.

The left-width variable is consumed by grid-template-columns. The right-width
variable is also consumed by the notification container:

    #toast-stack { right: calc(var(--right-width) + 16px); }

recascade_dimension_custom_property accepts widths, heights, min/max dimensions
and grid-track dimensions. The additional right declaration fails that predicate,
so the entire update falls back to recascade_connected_subtree_now. This happens
even while the toast stack is empty. Publication cost is similar on both sides;
the distinguishing cost is CPU CSS recalculation.

The earlier isolated benchmark lacked a positional consumer. Its speedup remains
valid for its workload, but did not establish a fix for the actual right divider.
The prior claim that grid-template-columns was the only consumer was incorrect.

Next implementation should extend the safe geometry-property coverage to positional
offsets, preserving explicit inheritance and other conservative fallback checks,
and add a fixture representing both the grid and the toast offset. Validate both
actual dividers afterward. No production fix is included in this diagnosis.

Evidence: artifacts/windows-kestrel/divider-left.log and divider-right.log.
The left matched workload uses --sidebar-kestrel --sidebar-cycles; the right uses
--properties-kestrel. Both use the merged native runtime and the same GPU settings.

## Positional-offset fix

The fast path now accepts physical left/right/top/bottom offsets as well as
dimensions. Its explicit-inheritance and other conservative fallback checks apply
to the expanded property set. The regression fixture combines a variable grid
track with all four calc-based offsets and checks setProperty, removeProperty,
and the empty setter. All 15 focused compatibility checks pass, including the
previous inherited-dimension regressions.

The expanded 1,000-item benchmark now includes the fixed toast position and checks
its geometry on every update. Three alternating runs of the same Windows native
configuration measured median-of-medians 22.69 ms before versus 3.49 ms after,
about 85% less synchronous style/layout time. Raw results are in
artifacts/windows-kestrel/positional-benchmark.json.

The attempted post-fix live comparison is invalid: the display changed to an
822px CSS viewport, which hides the properties panel. The workload correctly
failed its resize validation; its reported callback rate is not a resize result.
Vsync was also unavailable. Live 60 Hz resizing therefore remains unverified.
The probe now rejects hidden sidebars before submitting a gesture.

# Remaining Kestrel panning costs

Follow-up: windows-chromium-pan-comparison.md shows Chrome sustaining about
59.94 RAF callbacks/sec despite comparable index work. The original ranking
below identifies measured costs, not proof that the application is the limiting
factor. Further optimization should focus on WebScene's frame pipeline.

The bounded-history fix removes the demonstrated growth problem, but sustained
60 Hz application rendering is not yet consistent. A focused 20-second run,
`artifacts/windows-kestrel/bottleneck-methods.log`, passed input validation with
1,478 real compositor-clock ticks and zero fallback ticks. At 60 Hz synthetic
input it completed 54.77 draw callbacks/sec. These are not display-present events.
Method wrappers add overhead; measurements locate expensive work rather than
establish uninstrumented performance. The fixture is unchanged.

| Operation | Median ms | 95th percentile ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Kestrel pointerMove | 6.00 | 12.38 | 30.56 |
| Kestrel ensureIndex | 5.65 | 12.06 | 29.41 |
| Kestrel snapPoint | 5.69 | 12.10 | 29.71 |
| Application RAF callback | 1.49 | 2.38 | 5.48 |
| Retained canvas preparation | 0.20 | 0.40 | 2.17 |

Method times are inclusive and nested: do not add pointerMove, snapPoint and
ensureIndex. Preparation samples cover the retained scheduling ring's tail.
Pointer dispatch averaged 8.76 ms over the workload, including native dispatch
work beyond the wrapped application handler.

## Priorities

1. Investigate spatial-index work first. In the fixture's src/app.js,
   pointerMove calls snapPoint before handling pan/orbit; snapPoint calls
   ensureIndex when object snapping is enabled, and ensureIndex calls index.build.
   This dominates the measured JavaScript handler. Establish what rebuilds and
   why, and compare identical application code in Chromium before attributing
   the cost to WebScene or altering Kestrel behavior. An application optimization
   must preserve snapping semantics; do not patch the compatibility fixture to
   improve reported WebScene results.
2. Reduce periodic checkpoint stalls. This run submitted nine checkpoints with
   a maximum synchronous capture duration of 12.39 ms. An earlier slower valid
   run recorded 31.59 ms. GPU readback is synchronous under the composition owner;
   PNG encoding already runs in the background. This is a concrete remaining
   WebScene cost, but current samples do not establish which frame gaps coincide
   with captures. Timestamp captures before assigning individual dropped frames
   to them. Any asynchronous replacement must retain image ownership, ordered
   tail replay, export and recovery correctness.
3. Keep frame-pipeline latency separate from CPU cost. Publication-to-draw had
   median 8.84 ms and p95 18.38 ms; it includes boundary waiting and cannot be added
   as if it were CPU execution time. No GPU execution-time measurement was made.

Retained preparation and typical DOM geometry lookup are not the leading costs
in this run. There is no new evidence identifying font caching as the current
bottleneck. The measured peak history remained 34,791 commands. This does not
qualify unsupported checkpoint paths or production-default vsync integration.

The analysis helper is artifacts/windows-kestrel/analyze-bottlenecks.py.
Presentation measurement limitations and run-to-run variation are documented in
windows-frame-rate-verification.md. Near-60 Hz runs show the path can be smooth;
they do not eliminate the measured spikes or prove consistent 60 Hz presentation.

## Follow-up scope and implementation

The user requested changes remain within WebScene. Kestrel and its compatibility
fixture are unchanged. Inspection confirms SpatialIndex.build keys its cache by
camera revision, so panning invalidates it; skipping rebuilds would require a
separate application change with snapping correctness validation.

WebScene now defers Windows checkpoint readback behind a zero-timeout-polled GPU
fence, retaining the immutable image until completion and reading it before new
frame draws. See windows-canvas-checkpoints.md for validation and remaining
synchronous-copy limitations. A frame-rate improvement is not yet established.

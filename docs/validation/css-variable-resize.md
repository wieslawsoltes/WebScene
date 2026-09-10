# Dimension-only CSS variable resize optimization

## History and diagnosis

The properties divider changes a root CSS variable, --right-width. Its consumers include
Kestrel's grid-template-columns declaration and the toast stack's right offset. The old CSSOM
setter nevertheless recascades the entire root subtree on every processed update.

That behavior is already present in main at 1154c70043d6ac71d599e019490ea493898961e8.
The initial portable runtime commit
[869f1a0 (20 July 2026)](https://github.com/wieslawsoltes/WebScene/commit/869f1a0)
already calls recascade_connected_subtree from the custom-property setter.
The August 1 source decomposition preserved it. Comparing main with the Windows
work branch shows no change to that setter or subtree traversal; their cascade-file
difference concerns border-color parsing. This is a longstanding invalidation cost,
not a newly introduced full-subtree invalidation in the Windows work.

The original Windows investigation measured about 8.5 ms per update in recascade
and about 51–52 draw callbacks/s during properties-divider dragging. Toggling the
new single-scene pacing option made little difference. Those are diagnostic
measurements of that branch, not results for this isolated PR. Source history does
not establish exactly when the user's perceived smoothness changed; no complete
historical frame-rate bisect is claimed.

## Change

For a custom property referenced only by width/height, min/max dimensions, or grid
track dimensions, run the complete cascade on the mutation root and matching
consumers. Unaffected descendants still participate in normal layout (including
percentage sizing), but do not need their styles reset and reconstructed.

Use the existing variable-reference index without retaining a new node cache.
Resolve consumers at the existing style-batch flush boundary. Repeated writes to
one variable coalesce; mixed variables, overlapping roots, or other mutations
promote the request to the ordinary full-subtree path.

Explicitly inherited dimensions (including var() fallbacks), aliases, inherited
properties, pseudo-elements, shadow DOM, escaped selectors,
and style-attribute dependencies retain the full path. This intentionally
conservative coverage can be expanded separately. Synchronous geometry and
computed-style reads retain their existing flush behavior.

## Validation

Windows x64, MSVC Release, pinned V8, graphics disabled, certification compiled in
but profiling environment flags unset. Baseline and patch use the same build
configuration and matching ICU/bootstrap assets. The benchmark creates a grid
containing 1,000 unrelated styled items; every sample changes one track variable
and reads the resulting width. Twenty warm-up updates precede 60 measured updates.

Three alternating baseline/fixed process pairs:

| Statistic | Main baseline | Fixed |
|---|---:|---:|
| Median of run medians | 29.92 ms | 3.17 ms |
| Run p95 range | 31.35–35.15 ms | 3.63–3.82 ms |

Approximately 9.4 times faster, or 89% less synchronous style/layout time in this
synthetic workload. Raw results: [css-variable-resize-windows.json](css-variable-resize-windows.json).
This does not measure GPU work, full Kestrel interaction, or physical presentation.

Run the benchmark with the library's matching ICU data and bootstrap files beside it:

    python benchmarks/css-variable-resize.py <native-library>

The dimension-variable-compatibility native test filter passes all 14 checks on
the corrected patch. It includes the new inheritance matrix, dimension/priority/
removal/fallback/batch tests, and existing responsive layout, font-relative layout,
subgrid, shadow DOM, iframe cascade, and style-coalescing tests. The three parser
suites also pass.

## Review regression and correction

Review identified stale explicitly inherited width/height after the optimized
parent recalculation. The new dimension-inheritance filter reproduced the
regression before the correction. It now checks 24 combinations: stylesheet versus
inline declarations, literal inherit versus var() fallback to inherit, one versus
three descendant levels, and setProperty/removeProperty/empty setter. Each checks
both width and height before and after the mutation. An additional case verifies
that stylesheet !important wins over inline inheritance.

The optimization now falls back to the ordinary ancestor-first subtree cascade
when dimension declarations can inherit, including resolved var() values. Inline
width/height inheritance is also replayed during a full cascade. The latter fixes
an older issue independently confirmed on unchanged main: the new control test
fails only the literal-inline cases and their priority check there, while the
stylesheet cases pass. The previous PR revision also failed the stylesheet cases.
The final benchmark numbers above were rerun after this correction.

The full native suite fails on both baseline and patch at the existing assertion
"iframe preparation did not overlap discovery with outer script work". The
unrelated assertion was not changed. Full-suite success is therefore not claimed.

PowerShell focused test command:

    $env:WEBSCENE_NATIVE_ENGINE_TEST_FILTER='dimension-variable-compatibility'
    ./webscene_native_engine_tests.exe

No Kestrel fixture, WebGPU implementation, frame-pacing code, or font-cache change
is included. The optimization is in the shared native CSS runtime.

The current benchmark additionally checks a positional toast offset. The table
above records the earlier dimension-only workload; results for the expanded
workload and Windows positional fix are recorded in
[windows-divider-comparison.md](../graphics/windows-divider-comparison.md).

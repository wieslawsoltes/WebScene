# Fresh macOS Chrome reference capture

All 32 runs completed with hardware acceleration confirmed. Archive verification checked all 230 referenced files successfully. All 16 WebGPU before/after layer comparisons match exactly. Fifteen of 16 composited before/after pairs match; three cases have exported 2D-overlay differences. This is captured evidence with unresolved repeatability differences, not complete pixel qualification.

The full local archive is `artifacts/chrome-reference-20260908-current`. This committed subset retains compressed metadata, the four recorded harness source files, and measured overlay differences; it does not include all raw images and traces. Durable full archival remains outstanding. The recorded repository revision identifies additional harness dependencies.

Reproduction: `node tests/GraphicsCompatibility/capture-chrome-reference.mjs --output artifacts/chrome-reference-new`. Integrity: `python3 tests/GraphicsCompatibility/verify-reference-archive.py artifacts/chrome-reference-new`.

The fixture-dpr1-light pair has identical initial/final camera state and clipping geometry with empty GPU error arrays, despite overlay export differences. Their cause is unresolved. Do not attribute them to WebScene: this capture runs unchanged Kestrel in Chrome. Physical WebScene presentation and Windows/Linux qualification remain separate outstanding gates.

## Composite mismatch inspection

The `lines-10000-dpr1-light` before and after composite pairs differ within pixel bounds `(528,636)-(918,743)` (39,344 changed RGB pixels each). Visual inspection of the crops shows an active `ERASE` selection-command banner in run 2, absent in run 1. GPU and overlay layer hashes for this pair still match. Therefore this pair is not a neutral, repeatable composite baseline. The source of command activation is unproven; no renderer defect or external-input cause is asserted.

The capture harness now checks that no tool is active and the tool banner, suggestions and file menu are hidden immediately before and after snapshot capture. It rejects contaminated captures rather than removing application UI or accepting those pixels. This does not explain the other cases' separately exported overlay differences.

The new guard passes a targeted two-run `lines-10000-dpr1-light` capture with 30 timed pans per run, and all composite/GPU/overlay before/after hashes match. Archive integrity verifies 20 referenced files. Metadata: `neutral-ui-check.json.gz`; raw archive: `artifacts/chrome-reference-neutral-ui-check`. Seven harness tests pass. This short check does not replace the full matrix or its 180-pan workload.

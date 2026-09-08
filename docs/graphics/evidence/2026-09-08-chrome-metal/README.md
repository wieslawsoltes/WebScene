# Fresh macOS Chrome reference capture

All 32 runs completed with hardware acceleration confirmed. Archive verification checked all 230 referenced files successfully. All 16 WebGPU before/after layer comparisons match exactly. Fifteen of 16 composited before/after pairs match; three cases have exported 2D-overlay differences. This is captured evidence with unresolved repeatability differences, not complete pixel qualification.

The full local archive is `artifacts/chrome-reference-20260908-current`. This committed subset retains compressed metadata, the four recorded harness source files, and measured overlay differences; it does not include all raw images and traces. Durable full archival remains outstanding. The recorded repository revision identifies additional harness dependencies.

Reproduction: `node tests/GraphicsCompatibility/capture-chrome-reference.mjs --output artifacts/chrome-reference-new`. Integrity: `python3 tests/GraphicsCompatibility/verify-reference-archive.py artifacts/chrome-reference-new`.

The fixture-dpr1-light pair has identical initial/final camera state and clipping geometry with empty GPU error arrays, despite overlay export differences. Their cause is unresolved. Do not attribute them to WebScene: this capture runs unchanged Kestrel in Chrome. Physical WebScene presentation and Windows/Linux qualification remain separate outstanding gates.

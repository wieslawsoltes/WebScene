# Complete-source Chrome reference matrix

The corrected harness completed all 32 hardware-confirmed Chrome runs: courtyard, supplied fixture, seeded 10k and 100k lines, DPR 1/2, dark/light themes, twice each with 180 timed pans. All 16 comparisons have exact before/after PNG matches for composite, WebGPU and 2D overlay layers. All 232 referenced files verify against recorded hashes, including six capture-tool sources.

`reference.json.gz` retains complete metadata, samples and hashes. `summary.json` records measured scope and limits. `harness/` retains the exact source bytes. Full raw images, traces and generated inputs remain in `artifacts/chrome-reference-final-navigation-20260908`; permanent remote retention is still outstanding. Earlier failed or differing archives remain preserved separately.

Reproduce with `node tests/GraphicsCompatibility/capture-chrome-reference.mjs --output artifacts/chrome-reference-new`, then `python3 tests/GraphicsCompatibility/verify-reference-archive.py artifacts/chrome-reference-new`.

Chrome reporter timing is derived from platform presentation feedback for the verified browser revision; it is separate from CPU submission and RAF timing and does not prove physical WebScene presentation. This matrix does not close #23 or epic #22; other platform, baseline, package and conformance gates remain.

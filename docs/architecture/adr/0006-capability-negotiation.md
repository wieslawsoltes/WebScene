# ADR 0006: Backend capabilities are explicit and immutable

- **Status:** Accepted
- **Date:** 2026-07-15

## Context

Different backends will reach Canvas, SVG, IME, accessibility, drag/drop, OpenGL and
WebGPU support at different times. Silent fallback would turn backend support into an
unreviewable collection of partial behaviors.

## Decision

A backend declares immutable `WebSceneBackendCapabilities` at construction. Component
manifests and profile tests can require capabilities. Missing required capabilities
produce a diagnostic and fail before component startup. Capability flags indicate the
presence of an implementation; profile tests prove its semantics.

Support levels L0–L3 remain defined in the strategic roadmap. A flag alone cannot
promote a backend to a support level.

## Consequences

- Differences are visible in artifacts and support tables.
- New flags require contract tests and documentation.
- Environment-dependent support must be resolved before the immutable capability set
  is exposed.

# ADR 0005: Canvas and SVG cross the backend as typed retained data

- **Status:** Accepted
- **Date:** 2026-07-15

## Context

Canvas batching is a major performance path and recently required a packet-boundary
correctness fix. Replacing it with reflection, boxed calls or framework brush objects
would lose current gains. SVG also needs reusable semantics without requiring every
backend to parse the source independently.

## Decision

Canvas state and operations cross engine/backend boundaries as versioned numeric
packets plus packet-local strings. `IWebSceneCanvasPacketSink` is the initial typed seam;
the existing V8 batching protocol remains the reference. Capacity is reserved before
string interning and replay validates headers, counts, lengths and indices.

SVG crosses as a portable retained scene/command representation. Backends compile and
cache native brushes, paths, glyphs and GPU resources but do not own DOM/SVG semantics.

## Consequences

- Packet and pixel equivalence are required before moving the implementation.
- Backend caches must invalidate when every pixel-affecting input changes.
- The full portable display-list schema is R3 work; R1 establishes allocation-safe
  interfaces without rewriting the optimized replay path.

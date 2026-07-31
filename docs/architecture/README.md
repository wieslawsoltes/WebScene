# WebScene architecture decisions

The accepted R0 decisions are recorded as ADRs so implementation changes can be
reviewed against stable constraints rather than inferred from project names.

- [ADR 0001 — Package boundaries](adr/0001-package-boundaries.md)
- [ADR 0002 — Portable CSS layout authority](adr/0002-portable-layout-authority.md)
- [ADR 0003 — DOM and backend identity](adr/0003-dom-backend-identity.md)
- [ADR 0004 — Threading and lifetimes](adr/0004-threading-and-lifetimes.md)
- [ADR 0005 — Canvas and SVG command models](adr/0005-canvas-svg-command-model.md)
- [ADR 0006 — Capability negotiation](adr/0006-capability-negotiation.md)
- [ADR 0007 — Compatibility policy](adr/0007-compatibility-policy.md)
- [ADR 0008 — Performance-neutral extraction](adr/0008-performance-neutral-extraction.md)
- [ADR 0009 — Pre-production package identities](adr/0009-preproduction-package-identities.md)
- [ADR 0010 — Managed and native engines are first-class modes](adr/0010-dual-managed-native-engines.md)
- [ADR 0011 — Compositor-driven native scene publication](adr/0011-compositor-driven-native-scene-publication.md)

New decisions supersede old ADRs; accepted ADRs are not silently rewritten when the
architecture changes.

Active investigations:

- [Native V8 + immutable scene engine](native-v8-scene-engine.md)
- [Runtime architectural upgrade shortlist](runtime-architectural-upgrade-shortlist.md)
- [ADR 0012 — Pooled binary native JavaScript interop](adr/0012-pooled-binary-native-javascript-interop.md)
- [TradingView four-chart binary interop results](tradingview-four-chart-binary-interop-results.md)

Implementation guides:

- [Managed and native backends, packaging, and extension status](../backends.md)

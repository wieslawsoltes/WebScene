# Component asset fixtures

This directory contains backend-neutral component assets used to exercise the bounded
WebScene profile and SDK tooling. Each scenario has a manifest, TypeScript source, and
a local offline bundle. `catalog.json` records purpose, expected interactions,
diagnostics, and packaging notes.

Build and test the assets with:

```bash
npm ci --prefix samples/components
npm run build --prefix samples/components
npm run check --prefix samples/components
npm test --prefix samples/components
```

The Node suite type-checks and bundles every package, evaluates the bundles in isolated
browser-shaped realms, checks meaningful output plus keyboard/pointer interactions, and
verifies unmount cleanup and independent state. `compatibility-matrix.json` pins the
exact bundle digest, toolchain, interaction oracle, and evidence lane for every catalog
row; the test fails if the catalog and matrix drift.

The native runtime suite now executes the same 12 checked-in bundles with a neutral host
stub. Every row must mount meaningful DOM, accept keyboard focus and events, perform its
primary click/update, and leave an empty body after unmount. The 2026-08-11 `osx-arm64`
current-source run passes all 12 rows. This remains candidate evidence:
`compatibility-matrix.json` records that a released-RID run is still required before
promotion, and the bounded required profile is unchanged.

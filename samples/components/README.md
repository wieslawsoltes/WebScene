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
browser-shaped realms, checks meaningful output and scenario interactions, and verifies
unmount cleanup and independent state.

These tests validate assets and packaging; they do not prove native runtime
compatibility. The former managed Avalonia catalog host was removed with the managed
engine. Native product integration evidence lives in the `samples/Native*`
applications, and a general component catalog should return only on the future native
component host.

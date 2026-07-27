# WebScene R5 sample catalog

This directory is the backend-neutral asset catalog described by the strategic
roadmap. Each scenario has a Component Profile 1 manifest, TypeScript source, and a
local offline bundle. `catalog.json` records purpose, non-goals, supported backend,
launch/test commands, expected interactions, diagnostics, and packaging notes.

The runnable .NET projects live in `samples/hosts/Avalonia`, keeping these component
packages reusable by future backends. Start the graphical catalog with:

```bash
dotnet run --project samples/hosts/Avalonia/WebScene.Sdk.SampleCatalog
```

Pass a sample id after `--` to select it at launch, for example:

```bash
dotnet run --project samples/hosts/Avalonia/WebScene.Sdk.SampleCatalog -- Hybrid.ReactIslands
```

Build and execute the shared component catalog itself with:

```bash
npm ci --prefix samples/components
npm run build --prefix samples/components
npm run check --prefix samples/components
npm test --prefix samples/components
```

The build type-checks and creates a production IIFE bundle for every package. The
runtime suite evaluates all twelve bundles in browser-shaped isolated realms, asserts
meaningful rendered content, performs a scenario-specific interaction, checks host
calls where applicable, and verifies unmount cleanup. It also proves two React islands
retain independent state.

The Avalonia CI lane additionally runs `WebScene.Sdk.SampleSmoke`, which validates all
twelve packages, checks their TypeScript against the bounded profile, rejects
placeholder bundles, loads only declared offline assets, creates two instances, proves
immutable cache reuse, and proves mutable state/lifecycle isolation. With a reviewed
native V8 binary available, run the real backend matrix with:

```bash
scripts/run-r5-catalog-runtime-smoke.sh
```

That command mounts every package through Avalonia and ClearScript V8, including two
hybrid islands, four workstation instances, peer disposal/remount, and the native
migration editor. `CanvasWorkbench.Advanced` is the generic high-complexity Canvas,
overlay, and input workload. Product-owned compatibility suites can add stricter
application behavior gates without placing their assets or wrappers in this repository.

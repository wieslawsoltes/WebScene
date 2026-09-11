# Native Web validation — 2026-09-10

Environment: Apple M4, macOS 26.6.2. WebScene base
`937d467244ee2aae5ad135e418cceeb96f78696e`, Foco base
`d8c4d169d2b65111c1a99645497191907beb4f99`, plus the accompanying working-tree changes.

## Verified

- Native smoke: compiled construction, responsive layout, text/class changes,
  insertion/removal, event disposal, focus and Canvas scene publication.
- Native contracts: child/descendant selectors, specificity, inline and important
  declarations, media rules, forward/reverse focus navigation, hit testing,
  bubbling/cancellation, removal during delivery, invalid references, owner-thread
  checks, Canvas string-table isolation and document shutdown.
- Eight compiler tests: deterministic generated output and typed values, source
  diagnostics, unsupported CSS/selectors, duplicate IDs, scripts/handlers,
  missing stylesheets and invalid unitless lengths.
- AddressSanitizer and UndefinedBehaviorSanitizer builds: all three native/compiler
  test targets pass.
- Foco focus contract: internal forward/reverse navigation and ordinary host focus
  movement when the embedded engine declines handling.
- Real Cocoa input: mouse clicks, Enter activation, Tab to the next native DOM
  button, Space activation and subsequent DOM updates; process exit is checked.
- Foco compositor PNG capture includes native DOM text, colors and Canvas bars.
- Application relocation succeeds; a missing bundled resource fails with an
  actionable error and nonzero exit.
- Application dependency scan finds only system libraries. No V8/parser entrypoints
  or V8/ICU/snapshot deployment assets are present in the application bundle.
- Parsed-vs-compiled geometry: six named elements at two viewports (1000×700 and
  500×900), within 0.01 logical pixels. The parsed reference uses a separate V8-enabled
  engine built from the same WebScene checkout; V8 is confined to that test process.

The broader existing native-engine regression executable stops at
`native-enter-keypress-commit.js:3`, reporting `__submitterWasNull is not defined`.
The same failure and message reproduce with the original, unmodified style-default
implementation restored as a control. This broader suite is therefore not reported
as passing. The extracted style defaults compile in the V8-enabled engine.

## Measurements

Full run data and qualifications are in [measurements.json](measurements.json).

| Measurement | Result |
|---|---:|
| Packaged Foco application | 12,460,638 bytes |
| Native construction + first layout, median of 20 runs | 133.5 µs |
| Mutation + layout/scene generation, median of per-run means | 46.78 µs |
| HTML/CSS compiler process wall time | 5.72 ms |
| Generated UI + app C++ translation-unit compilation | 704 ms |
| Native headless process peak RSS | 2,392,064 bytes |
| Packaged Cocoa application peak RSS | 104,628,224 bytes |

Construction timing starts inside `main`; it excludes process loading and first GPU
presentation. Each interaction mean covers 100 mutations and scene generations.
Filesystem caches were not flushed. C++ compilation excludes the already-built
engine/Foco SDK. RSS excludes GPU memory. The Cocoa smoke's wall time includes a
deliberate 1.5-second delay and is not used as a startup claim. These are measurements
of this small application, not general native-vs-browser speedup claims.

## Delivery limits

This implements the initial Native Web slice described in the sample README, not
full HTML/CSS or browser API coverage. Editable text/IME, DOM accessibility projection,
advanced Canvas/GPU/media native authoring and image/font URL compilation remain
outside its current profile. No hot design, optional-JS integration, new language
projection or typed HTML data binding is claimed.

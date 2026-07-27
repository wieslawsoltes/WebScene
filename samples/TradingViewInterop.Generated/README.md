# TradingView interop generation proof

This sample proves comprehensive declaration shapes without redistributing
TradingView. `TradingViewApi.fixture.d.ts` is a shape-only fixture, not a copy
of the licensed declaration files. It currently exercises 36 named types,
57 methods, 79 properties, 24 callbacks, and 12 promises with no discovery
fallbacks; see `TradingViewApi.coverage.json`.

The fixture is only a generator/runtime regression test. It is not an API
catalog and must not be expanded by inferring declarations from documentation.
For a real application, the user's licensed `.d.ts` files are the sole source
of the generated TradingView API.

The generated C# includes:

- widget, chart, trading-primitive, broker-host, and generic watched-value
  proxies;
- options, datafeed, symbol, bar, quote, order, position, execution, and
  account DTOs;
- a reverse-call `TradingViewDatafeed` base class;
- a reverse-call `TradingViewBroker` base class;
- typed callback wrappers and synchronous broker configuration values.

For a licensed installation, discover the same roots from the real package:

```bash
node ../../tooling/webscene/interop-discover.mjs \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output obj/TradingViewApi.webscene-interop-api.json \
  --report-output obj/TradingViewApi.coverage.json \
  --policy-output TradingView.webscene-interop-policy.json \
  --namespace MyApplication.TradingView \
  --fail-on-fallbacks
```

To discover every surface and immediately compile it through the reusable
harness:

```bash
node ../../tooling/webscene/interop-validate.mjs \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output-dir obj/licensed-validation \
  --name TradingView \
  --namespace MyApplication.TradingView \
  --compile-project ../../tests/WebScene.JavaScript.Interop.Generator.Compile/WebScene.JavaScript.Interop.Generator.Compile.csproj \
  --no-restore
```

This validation is strict by default and does not guess missing declarations.
After reviewing the initial all-surface result, pass the application's policy
with `--policy-input`; declaration fingerprint changes then require an explicit
policy update.

Omitting `--roots` discovers every named declaration. The policy scaffold is
safe by default. `--include-all-models`, `--proxy-roots A,B`, and
`--adapter-roots C,D`, `--include-all-functions`, `--function-roots E,F`,
`--include-all-globals`, and `--global-roots G,H` can select large surfaces
without editing each member.
The policy still records whether an interface is serialized data, an outbound
proxy, or an inbound adapter.

Include the API and policy JSON as MSBuild `AdditionalFiles`. Roslyn then emits
the complete selected native-engine API during compilation. Package consumers
can instead set `WebSceneInteropApiManifest` and `WebSceneInteropPolicy`; the
`WebScene.JavaScript.Interop.Generator` package adds and validates those
`AdditionalFiles` automatically.

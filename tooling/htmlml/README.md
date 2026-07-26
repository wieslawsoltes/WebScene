# @htmlml/sdk

This package publishes the bounded HtmlML Component Profile 1 declarations and the
same compatibility rules used by the .NET SDK. It does not advertise the complete
browser `lib.dom` surface.

- `htmlml-check --manifest htmlml-component.json --source src` checks JavaScript and
  TypeScript in CI.
- `htmlml()` from `@htmlml/sdk/vite` validates source during Vite builds and emits the
  final packaged manifest.
- `htmlml()` from `@htmlml/sdk/esbuild` provides the equivalent esbuild integration.
- `htmlml.host.*.invoke()` is the asynchronous capability-based host API.
- `htmlml-interop-discover --declarations <library.d.ts> --output
  obj/library.htmlml-interop-api.json --policy-output
  library.htmlml-interop-policy.json --report-output
  obj/library.coverage.json --namespace MyApp.Interop --fail-on-fallbacks` uses the TypeScript
  compiler to emit the complete named type graph, an editable policy, and a
  coverage report. Supplied entry files determine exported roots while
  transitive declarations remain available as dependency types. Use `--roots`
  to select roots explicitly, or
  `--include-all-models`, `--include-all-proxies`,
  `--include-all-adapters`, `--include-all-functions`,
  `--include-all-globals`, `--proxy-roots`, `--adapter-roots`,
  `--function-roots`, and `--global-roots` to configure a broad generated
  surface. Exported or ambient functions are emitted through a generated
  static facade, as are exported/static values and promise-valued globals;
  their policies record dotted runtime global names. The strict fallback flag
  makes declaration upgrades fail in CI if any shape could not be normalized.
- `htmlml-interop-validate --declarations <library.d.ts> --output-dir
  obj/interop-validation --namespace MyApp.Interop --compile-project
  path/to/validation.csproj` performs strict all-surface discovery, writes the
  manifest/policy/coverage set, and builds the generated C#. It fails before
  compilation on unreviewed discovery fallbacks. Supply
  `--policy-input reviewed.htmlml-interop-policy.json` to validate a maintained
  policy; a declaration fingerprint mismatch is reported as API drift instead
  of silently replacing the policy. Use `--allow-fallbacks` only after
  reviewing every reported fallback, or `--skip-compile` when only artifacts
  are required.

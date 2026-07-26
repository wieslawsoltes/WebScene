# Arbitrary declaration compile harness

This project compile-tests difficult, library-independent declaration shapes
and can also compile any API manifest and policy produced by
`htmlml-interop-discover`. It is intentionally not tied to TradingView.

```bash
dotnet build HtmlML.JavaScript.Interop.Generator.Compile.csproj \
  -p:HtmlMLInteropApiManifest=/absolute/path/library.htmlml-interop-api.json \
  -p:HtmlMLInteropPolicy=/absolute/path/library.htmlml-interop-policy.json
```

The harness derives isolated `bin` and `obj` directories from both input file
names, so compiling another library or policy cannot leave the default fixture
assembly stale. Pass `-p:HtmlMLInteropHarnessId=unique-name` when two inputs
with identical file names need separate output directories.

Use an all-model, all-proxy, all-adapter policy as a generator stress gate.
Use a reviewed policy for the actual direction and ownership semantics of a
library API. The checked-in fixture covers generic methods and aliases,
anonymous object results, wide unions, overload/name collisions,
promise-like unions, live-object mappings, optional parameters, required
function-valued properties, typed string/number index signatures, and
ambient/free functions.

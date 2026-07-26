# Native JavaScript interop source generation

## Conclusion

HtmlML can generate a strongly typed .NET facade for a JavaScript library from
the library's TypeScript declarations. The native engine should be the primary
runtime boundary. Generated methods should call
`htmlml_engine_evaluate_json` asynchronously, retain JavaScript objects through
isolate-local handles, and map JavaScript promises to `ValueTask<T>`.

TradingView is a good candidate because its licensed package contains
`charting_library/charting_library.d.ts` for the widget API and
`charting_library/datafeed-api.d.ts` for the datafeed API. HtmlML must consume
these files from the application's local licensed copy; it must not redistribute
them.

## Proposed build pipeline

```text
licensed charting_library.d.ts
              |
              v
 TypeScript compiler + type checker
              |
              v
 normalized complete HtmlML type graph
              +
 editable library policy
              |
              v
 Roslyn incremental source generator
              |
              v
 proxies + DTOs + datafeed/broker adapters
              |
              v
 NativeJavaScriptInvoker
              |
              v
 htmlml_engine_evaluate_json -> native V8 isolate
```

The TypeScript compiler stage is intentionally separate from Roslyn. A regular
expression parser cannot correctly resolve declaration merging, imports,
aliases, overloads, conditional types, or generic substitutions. A Roslyn
generator also should not start Node as a compiler side effect. The TypeScript
stage therefore writes a deterministic JSON manifest before C# compilation;
the incremental generator consumes that manifest as an `AdditionalFile`.

`htmlml-interop-discover` is the first-stage proof:

```bash
htmlml-interop-discover \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output obj/tradingview.htmlml-interop-api.json \
  --report-output obj/tradingview.coverage.json \
  --policy-output tradingview.htmlml-interop-policy.json \
  --namespace MyApplication.TradingView \
  --fail-on-fallbacks
```

The manifest records every named dependency declaration, identifies the
exported or ambient declarations from the supplied entry files as public
roots, and includes the TypeScript compiler version and SHA-256 of every input.
A TradingView upgrade therefore creates a reviewable API diff. Policies carry
a combined API fingerprint and the generator rejects a policy aimed at a
different declaration set. The generated
policy is safe by default; bulk model, proxy, and adapter selections are
available from the CLI. Method scaffolds retain optional parameters as
`JavaScriptOptional<T>`, so selecting an API does not silently remove part of
its callable surface.
The strict fallback flag makes CI fail if a declaration cannot be expressed by
the normalized type graph, while still writing the coverage report that
identifies each unsupported location.

For an end-to-end license-holder validation, the repository provides one
strict command that discovers every surface and compiles the resulting C#:

```bash
node tooling/htmlml/interop-validate.mjs \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output-dir obj/tradingview-validation \
  --name TradingView \
  --namespace MyApplication.TradingView \
  --compile-project tests/HtmlML.JavaScript.Interop.Generator.Compile/HtmlML.JavaScript.Interop.Generator.Compile.csproj \
  --no-restore
```

The command generates an all-surface policy for the breadth gate. For a
maintained application policy, pass
`--policy-input MyApplication.TradingView.htmlml-interop-policy.json`.
Fingerprint drift then fails explicitly and requires a policy review. Discovery
fallbacks also fail by default; `--allow-fallbacks` is an explicit,
review-required escape hatch.

In a normal application, the generator package performs the `AdditionalFiles`
wiring:

```xml
<ItemGroup>
  <PackageReference Include="HtmlML.JavaScript.Interop" Version="..." />
  <PackageReference Include="HtmlML.JavaScript.Interop.Generator"
                    Version="..."
                    PrivateAssets="all" />
</ItemGroup>
<PropertyGroup>
  <HtmlMLInteropApiManifest>Interop/TradingView.htmlml-interop-api.json</HtmlMLInteropApiManifest>
  <HtmlMLInteropPolicy>Interop/TradingView.htmlml-interop-policy.json</HtmlMLInteropPolicy>
</PropertyGroup>
```

Both properties are required together. The build-transitive target rejects
missing files before compilation, while Roslyn rejects invalid schemas,
fingerprint mismatches, and policy references to undiscovered declarations.

## What can be inferred

| TypeScript declaration | Generated .NET shape | Native behavior |
| --- | --- | --- |
| `string`, `number`, `boolean` | `string`, `double`, `bool` | JSON value |
| `void` | `ValueTask` | queued invocation |
| `Promise<T>` | `ValueTask<T>` | promise handle plus non-blocking result polling |
| public interface returned by a method | generated proxy class | isolate-local object handle |
| object/options interface | record or class with JSON names | JSON argument |
| optional parameter | `JavaScriptOptional<T>` | preserves `undefined` separately from JSON `null` in both directions |
| optional property | `JavaScriptOptional<T>` | preserves an absent property separately from explicit JSON `null` |
| string-literal union | generated string-backed value type | JSON string |
| structural union of arbitrary width | `JavaScriptUnion<...>` | branch-aware JSON conversion |
| array/readonly array | array/read-only list | JSON array |
| tuple, including more than seven elements | C# value tuple | JavaScript array with positional conversion |
| string/number index signature | typed `AdditionalProperties` dictionary | flattened JavaScript object properties |
| `Map`, `Set`, DOM objects, typed arrays | `JavaScriptObjectReference` | retained isolate-local object handle |
| overloads | C# overloads where unambiguous | same JavaScript member |
| generic interface | generic proxy/model | retained object handle or JSON |
| callback parameter | function reference/typed `JavaScriptAction<T...>` or tuple action | retained function handle with arbitrary argument count |
| ambient/exported function | generated static facade method | dotted global lookup and invocation |
| exported/static value | generated static facade getter | dotted global value or promise lookup |
| anonymous object in a member signature | generated structural record | JSON object |
| `T \| Promise<T>` | `ValueTask<T>` | `Promise.resolve` handles either result |
| model containing live objects | public model plus internal wire model | nested handles become proxies using the active invoker |

`IChartingLibraryWidget.activeChart(): IChartWidgetApi`, for example, becomes:

```csharp
await using var chart = await widget.ActiveChartAsync(cancellationToken);
string symbol = await chart.SymbolAsync(cancellationToken);
bool changed = await chart.SetSymbolAsync("NASDAQ:MSFT", cancellationToken);
```

The proxy holds only an `IJavaScriptInvoker` and a numeric handle. The actual
TradingView object never crosses the native ABI.

## What declarations cannot decide

A small application-owned policy file is still required:

- which interfaces are outbound proxies, serialized models, or inbound
  adapters;
- the runtime constructor mapping, such as
  `IChartingLibraryWidget -> new TradingView.widget(options)`;
- the runtime dotted name for an exported module function when it is not
  installed directly on `globalThis`;
- names and namespaces for generated .NET types;
- whether an object-shaped type is serialized data or a live JavaScript object;
- callback ownership and event unsubscription policy;
- overrides for intentionally dynamic `any`/`unknown` values, recursive
  conditional types, and APIs that the native HtmlML browser profile does not
  support.

Unsupported or ambiguous shapes must produce generator diagnostics. They must
not degrade silently to `dynamic`.

When several live union branches are represented by the same opaque object
handle, the runtime cannot infer which TypeScript interface the object
implements. The generator preserves those branches as
`JavaScriptObjectReference`, preserves distinguishable JSON branches in the
same union, and emits `HTMLMLJS003`. A policy mapping can replace the raw
handle when the library supplies a reliable discriminator.

The policy format is library-independent. Applications can keep policies
locally, while reusable versioned policies for redistributable libraries can be
published as separate packages. A policy remains tied to the declaration
manifest and its hashes, so a shared policy cannot silently claim compatibility
with a different library version.

## Native runtime model

The proof installs one bootstrap object in each native V8 isolate. That object
owns:

- a monotonically allocated object-handle table;
- method dispatch through `Reflect.apply`, preserving the JavaScript receiver;
- constructor dispatch through `Reflect.construct`;
- a promise-result table for asynchronous library methods;
- reverse-call queues for datafeed and broker adapters;
- retained JavaScript callback functions;
- synchronous factory functions for Trading Platform broker construction;
- deterministic release of object handles.

All user values and member names are JSON encoded. Generated code does not
concatenate user input into executable JavaScript. `NativeJavaScriptInvoker`
accepts the existing `NativeHtmlMlView.EvaluateJsonAsync` method directly:

```csharp
var interop = new NativeJavaScriptInvoker(view.EvaluateJsonAsync);
var widget = await TradingViewWidget.CreateAsync(interop, options, cancellationToken);
```

The current native ABI is sufficient for a correctness prototype. A production
version should eventually add dedicated native exports for handle invocation
and promise completion. That removes repeated script parsing and result polling
while retaining the same generated .NET API.

## Prototype status

Implemented:

- complete or selected TypeScript declaration discovery, including imports,
  constructors, inheritance, generics, methods, overloads, properties,
  callbacks, aliases, enums, unions, tuples, references, arrays, index
  signatures, and promises;
- declaration input hashes for API drift detection;
- automatic/bulk policy scaffolding, coverage reporting, and a JSON schema;
- incremental C# generation directly from an API manifest and policy supplied
  as MSBuild `AdditionalFiles`;
- generated option/data records, nested records, aliases, string unions,
  anonymous structural records, structural unions of arbitrary width, generic
  proxy classes and aliases, constructor inference, static methods, global and
  static value getters, properties, required
  function-valued option properties, optional/rest arguments, promise-like
  unions, arbitrary-length tuples, typed index signatures, object-returning promises, and
  invoker-aware wire models for nested retained values, including generic
  containers and generic typed dictionaries;
- native async object/value/void/property/promise invocation;
- native value/object/void/promise invocation for dotted global functions;
- branch-aware runtime result encoding: arrays and plain objects remain JSON,
  while live JavaScript objects are retained as handles;
- generated TradingView-shaped widget, chart, watched-value, trading primitive,
  datafeed, broker, broker-host, quote, and trading models;
- reverse callback registration, typed callback invocation, a callback pump,
  lossless optional callback arguments, arbitrary-width callback signatures,
  synchronous no-argument adapter
  results, and synchronous broker factories;
- discovery, code-generation, native-dispatch, datafeed, broker, callback,
  promise-object, property, optional-argument, and generic-proxy tests.

The generic compile gate also generates every model, proxy, and canonical
adapter member from an available 382-type Monaco declaration surface. That
surface includes 138 function overloads, 573 methods, 2,251 properties, and
200 optional parameters and currently compiles with no errors. Recursive
conditional aliases and live unions without runtime discriminators remain
explicit diagnostics rather than guessed mappings. This is a stress test of
the generator, not a substitute for compiling the licensed TradingView
declarations.

Remaining production work:

- run the coverage report and compile against a licensed current TradingView
  package; those private declarations are not present in this repository;
- add a dedicated native handle/callback ABI to replace script polling;
- provide a direct native synchronous callback for adapter methods that take
  arguments and return synchronously. No-argument synchronous methods and
  broker factories are already supported through cached values and native
  JavaScript closures;
- generate optional `JsonSerializerContext` metadata for NativeAOT.

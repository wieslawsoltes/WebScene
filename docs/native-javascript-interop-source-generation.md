# Native JavaScript interop source generation

## Conclusion

WebScene can generate a strongly typed .NET facade for a JavaScript library from
the library's TypeScript declarations. The native engine should be the primary
runtime boundary. For binary-supported declaration shapes, generated methods
now use static direct-invocation call sites and tagged codecs, retain JavaScript
objects through isolate-local handles, and map JavaScript promises to
`ValueTask<T>` without JSON or polling. `webscene_engine_evaluate_json` remains
the compatibility boundary for unsupported shapes and arbitrary evaluation.

TradingView is a good candidate because its licensed package contains
`charting_library/charting_library.d.ts` for the widget API and
`charting_library/datafeed-api.d.ts` for the datafeed API. WebScene must consume
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
 normalized complete WebScene type graph
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
 webscene_engine_evaluate_json -> native V8 isolate
```

The TypeScript compiler stage is intentionally separate from Roslyn. A regular
expression parser cannot correctly resolve declaration merging, imports,
aliases, overloads, conditional types, or generic substitutions. A Roslyn
generator also should not start Node as a compiler side effect. The TypeScript
stage therefore writes a deterministic JSON manifest before C# compilation;
the incremental generator consumes that manifest as an `AdditionalFile`.

`webscene-interop-discover` is the first-stage proof:

```bash
webscene-interop-discover \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output obj/tradingview.webscene-interop-api.json \
  --report-output obj/tradingview.coverage.json \
  --policy-output tradingview.webscene-interop-policy.json \
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
node tooling/webscene/interop-validate.mjs \
  --declarations /licensed/charting_library/charting_library.d.ts \
  --declarations /licensed/charting_library/datafeed-api.d.ts \
  --output-dir obj/tradingview-validation \
  --name TradingView \
  --namespace MyApplication.TradingView \
  --compile-project tests/WebScene.JavaScript.Interop.Generator.Compile/WebScene.JavaScript.Interop.Generator.Compile.csproj \
  --no-restore
```

The command generates an all-surface policy for the breadth gate. For a
maintained application policy, pass
`--policy-input MyApplication.TradingView.webscene-interop-policy.json`.
Fingerprint drift then fails explicitly and requires a policy review. Discovery
fallbacks also fail by default; `--allow-fallbacks` is an explicit,
review-required escape hatch.

In a normal application, the generator package performs the `AdditionalFiles`
wiring:

```xml
<ItemGroup>
  <PackageReference Include="WebScene.JavaScript.Interop" Version="..." />
  <PackageReference Include="WebScene.JavaScript.Interop.Generator"
                    Version="..."
                    PrivateAssets="all" />
</ItemGroup>
<PropertyGroup>
  <WebSceneInteropApiManifest>Interop/TradingView.webscene-interop-api.json</WebSceneInteropApiManifest>
  <WebSceneInteropPolicy>Interop/TradingView.webscene-interop-policy.json</WebSceneInteropPolicy>
</PropertyGroup>
```

Both properties are required together. The build-transitive target rejects
missing files before compilation, while Roslyn rejects invalid schemas,
fingerprint mismatches, and policy references to undiscovered declarations.

## What can be inferred

| TypeScript declaration | Generated .NET shape | Native behavior |
| --- | --- | --- |
| `string`, `number`, `boolean` | `string`, `double`, `bool` | tagged native value |
| `void` | `ValueTask` | pooled queued direct invocation |
| `Promise<T>` | `ValueTask<T>` | native settlement completes the original operation |
| public interface returned by a method | generated proxy class | isolate-local object handle |
| object/options interface | record or class with JavaScript names | generated tagged object codec where supported |
| optional parameter | `JavaScriptOptional<T>` | preserves `undefined` separately from `null` |
| optional property | `JavaScriptOptional<T>` | preserves an absent property separately from explicit `null` |
| string-literal union | generated string-backed value type | tagged UTF-8 string where supported |
| structural union of arbitrary width | `JavaScriptUnion<...>` | compatibility conversion until every branch has a tagged codec |
| array/readonly array | array/read-only list | tagged array, or policy-selected borrowed view |
| tuple, including more than seven elements | C# value tuple | JavaScript array with positional conversion |
| string/number index signature | typed `AdditionalProperties` dictionary | flattened JavaScript object properties |
| `Map`, `Set`, DOM objects, typed arrays | `JavaScriptObjectReference` | retained isolate-local object handle |
| overloads | C# overloads where unambiguous | same JavaScript member |
| generic interface | generic proxy/model | retained object handle or compatibility path |
| callback parameter | function reference/typed `JavaScriptAction<T...>` or tuple action | retained function handle with arbitrary argument count |
| ambient/exported function | generated static facade method | dotted global lookup and invocation |
| exported/static value | generated static facade getter | dotted global value or promise lookup |
| anonymous object in a member signature | generated structural record | compatibility path unless it has a generated tagged codec |
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
  conditional types, and APIs that the native WebScene browser profile does not
  support.

Unsupported or ambiguous shapes must produce generator diagnostics. They must
not degrade silently to `dynamic`.

When several live union branches are represented by the same opaque object
handle, the runtime cannot infer which TypeScript interface the object
implements. The generator preserves those branches as
`JavaScriptObjectReference`, preserves distinguishable JSON branches in the
same union, and emits `WEBSCENEJS003`. A policy mapping can replace the raw
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

The compatibility path JSON-encodes user values and member names. Generated
code does not concatenate user input into executable JavaScript.
`NativeJavaScriptInvoker` can still accept the existing
`NativeWebSceneView.EvaluateJsonAsync` method directly:

```csharp
var interop = new NativeJavaScriptInvoker(view.EvaluateJsonAsync);
var widget = await TradingViewWidget.CreateAsync(interop, options, cancellationToken);
```

When a native binary transport is supplied, supported generated methods emit
static call-site metadata, direct tagged request writers, and typed tagged
result codecs. Native handle/global/member invocation and direct promise
completion remove JSON, repeated script construction, parsing, polling, and
intermediate managed values while retaining the same public generated .NET
API. Unsupported declaration shapes use the compatibility implementation.
Arbitrary evaluation and diagnostics remain on the JSON path.

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
- direct generated global lookup and receiver-preserving member invocation
  over pooled tagged request/result arenas;
- direct generated invocation for binary-supported dotted global functions,
  discovered constructors, and instance property getters/setters;
- direct fulfillment, rejection, and cancellation of pending promise
  operations without JavaScript status polling;
- generated reflection-free codecs for primitive values, arrays, retained
  handles, and generated object models;
- policy-selected borrowed array results using disposable leases, stack-only
  readers, and UTF-8 spans;
- pooled managed request buffers and completion sources plus native request,
  operation, and size-classed result records;
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

- extend the binary generator to all selected global functions, constructors,
  properties, generic shapes, and discriminated unions;
- add direct native callback take/complete operations to replace compatibility
  JSON polling (callback handles already flow through direct generated calls);
- provide a direct native synchronous callback for adapter methods that take
  arguments and return synchronously. No-argument synchronous methods and
  broker factories are already supported through cached values and native
  JavaScript closures;
- complete the weighted binary-versus-leased-UTF-8 end-to-end gate and broader
  malformed-input fuzzing before promoting ABI 3;
- generate optional `JsonSerializerContext` metadata for NativeAOT.

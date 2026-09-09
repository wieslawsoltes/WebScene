# WebScene JavaScript interop source generator

This analyzer generates strongly typed .NET models, outbound proxies, and
inbound adapters from an WebScene interop API manifest and reviewed policy.

Reference both packages:

```xml
<ItemGroup>
  <PackageReference Include="WebScene.JavaScript.Interop" Version="..." />
  <PackageReference Include="WebScene.JavaScript.Interop.Generator"
                    Version="..."
                    PrivateAssets="all" />
</ItemGroup>
```

Then configure the two generator inputs. The package's build-transitive target
adds them as Roslyn `AdditionalFiles` and validates that both exist:

```xml
<PropertyGroup>
  <WebSceneInteropApiManifest>Interop/TradingView.webscene-interop-api.json</WebSceneInteropApiManifest>
  <WebSceneInteropPolicy>Interop/TradingView.webscene-interop-policy.json</WebSceneInteropPolicy>
</PropertyGroup>
```

Generate and compile a complete declaration-package validation with
`webscene-interop-validate`. Licensed declarations remain local to the
application and are never embedded in either WebScene package.

## External contracts and ABI 3 codecs

Object `typeMappings` automatically receive ABI 3 readers and writers when their
TypeScript schema and CLR contract are compatible. The codec is emitted into the
integration assembly and directly consumes the external contract. The contracts
assembly needs no WebScene, Avalonia, or backend reference.

For `interface Sample { time: number; value: number | null; }`, a referenced
`public sealed record Sample(double Time, double? Value);` works with:

```json
{
  "typeMappings": { "Sample": "global::Application.Contracts.Sample" },
  "models": [{ "source": "Sample", "include": false }]
}
```

Property names default to PascalCase. To use `Timestamp` and `Price` instead,
add `"propertyMappings": { "time": "Timestamp", "value": "Price" }` to the
`Sample` model policy. These names select CLR properties; the binary property
names and ordering still come from the TypeScript manifest. JSON attributes do
not change the native wire schema.

Both directions support direct objects, arrays, nullable values, and nested
supported external contracts. Writers reuse the generated DTO wire format and
read the supplied objects/lists directly without intermediate DTOs or projected
arrays. Readers construct the final external objects and result arrays. Struct
writes use typed access without boxing each value, including nullable structs
and struct array elements. Struct readers prefer a matching constructor.

Contracts must be accessible, non-abstract, non-generic classes or structs (including records)
with public readable properties of the mapped CLR types. Reading requires either
an accessible parameterless constructor and writable/init properties, or one
accessible constructor matching all mapped properties by name (ignoring case)
and type. Constructor parameter order may differ from wire property order.
Required members must be initialized or satisfied by a `SetsRequiredMembers`
constructor. Array properties accept `IReadOnlyList<T>` and, for non-nullable
required arrays, `T[]`. By default, optional TypeScript properties require the
existing `JavaScriptOptional<T>` representation to preserve absent versus null
semantics. External models can explicitly choose ordinary CLR properties using
`optionalProperties` as described below.

Unsupported contracts (including recursive graphs, ref structs, generics, index
signatures, incompatible properties, and unavailable constructors) emit warning
`WEBSCENEJS004` with the reason. Remove the mapping and enable the generated model,
adjust the contract/policy, or supply a custom binary invocation. Native object
and function handle mappings remain supported. Projects requiring native
transport can promote the warning with
`<WarningsAsErrors>$(WarningsAsErrors);WEBSCENEJS004</WarningsAsErrors>`.

### Integer timestamp properties

External `long` properties can map to TypeScript `number`, and `long?` properties
can map to `number | null`. The generated codec converts them to and from the
ABI's double-precision number representation. This applies to required properties
and optional properties explicitly mapped to ordinary CLR values with
`optionalProperties`. Wrappers using `JavaScriptOptional<long>` are not currently
converted.

JavaScript numbers cannot represent every 64-bit integer. To prevent silent
precision loss, both directions enforce the safe-integer interval
`[-9007199254740991, 9007199254740991]` (±(2^53 − 1)). Writes outside that interval
throw `OverflowException`, even if a particular larger integer is representable.
Reads also reject fractional values, NaN, and infinities with `OverflowException`;
they never truncate, round, or clamp. Nullable properties preserve null. Unix
millisecond timestamps in this interval round-trip exactly; full-range 64-bit
identifiers require another wire representation.

For example, a neutral `readonly record struct TradingViewBar(long TimeMilliseconds,
double Open, double High, double Low, double Close, double Volume)` can use
`"propertyMappings": { "time": "TimeMilliseconds" }` to match a candle schema
with numeric `time`, `open`, `high`, `low`, `close`, and `volume` properties.

### Optional fields in neutral contracts

External model policies can explicitly map optional TypeScript fields to ordinary
CLR properties. For example, `spreadBidPriceInBps?: number | null` can use
`double? SpreadBidPriceInBps`, and `volume?: number` can use `double Volume`:

```json
{
  "typeMappings": { "Point": "global::Application.Contracts.Point" },
  "models": [{
    "source": "Point",
    "include": false,
    "optionalProperties": {
      "spreadBidPriceInBps": { "write": "always", "read": "null" },
      "volume": { "write": "always", "read": "default", "default": 0 }
    }
  }]
}
```

Keys are TypeScript property names, even when `propertyMappings` renames their
CLR properties. Each entry must explicitly specify `write` and `read`:

- `write: "always"` emits the property on every write, including a null value when
  the declared type permits null. Values are never omitted based on their default
  value. This is the only ordinary-property write mode currently supported.
- `read: "reject"` throws `InvalidOperationException` when the property is absent.
- `read: "null"` maps absence to null and requires a nullable CLR property
  compatible with the TypeScript value type.
- `read: "default"` maps absence to the explicit JSON `default` value. Defaults
  support compatible strings, booleans, finite numbers, or null for nullable
  properties (including external objects). Int64 defaults must be safe integers.
  Object and array defaults are not supported.

Explicit JavaScript `undefined` follows the same rule as absence. Explicit null
is handled according to the declared value type: it remains null for nullable
fields and is rejected for non-nullable fields. A configured default is never
substituted for an explicit null or an invalid supplied value. In particular,
`volume?: number` with default 0 rejects explicit null; it does not become a
nullable property. Long conversions retain the range and precision checks above.

Only fields listed in `optionalProperties` use these semantics. Other optional
fields still require `JavaScriptOptional<T>` and preserve absent versus present
null/value. Ordinary properties need no WebScene reference in the contracts
assembly. The policy changes codec behavior explicitly without changing the
TypeScript declarations, and writes still avoid intermediate DTOs, projected
arrays, and per-element boxing for struct arrays. Invalid or incompatible policy
entries produce `WEBSCENEJS004`.

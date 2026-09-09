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
arrays. Readers construct the final external objects and result arrays.

Contracts must be accessible, non-abstract, non-generic classes or record classes
with public readable properties of the mapped CLR types. Reading requires either
an accessible parameterless constructor and writable/init properties, or one
accessible constructor matching all mapped properties by name (ignoring case)
and type. Constructor parameter order may differ from wire property order.
Required members must be initialized or satisfied by a `SetsRequiredMembers`
constructor. Array properties accept `IReadOnlyList<T>` and, for non-nullable
required arrays, `T[]`. Optional TypeScript properties require the existing
`JavaScriptOptional<T>` representation to preserve absent versus null semantics.

Unsupported contracts (including recursive graphs, structs, generics, index
signatures, incompatible properties, and unavailable constructors) emit warning
`WEBSCENEJS004` with the reason. Remove the mapping and enable the generated model,
adjust the contract/policy, or supply a custom binary invocation. Native object
and function handle mappings remain supported. Projects requiring native
transport can promote the warning with
`<WarningsAsErrors>$(WarningsAsErrors);WEBSCENEJS004</WarningsAsErrors>`.

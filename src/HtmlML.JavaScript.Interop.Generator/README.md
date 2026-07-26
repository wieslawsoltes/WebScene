# HtmlML JavaScript interop source generator

This analyzer generates strongly typed .NET models, outbound proxies, and
inbound adapters from an HtmlML interop API manifest and reviewed policy.

Reference both packages:

```xml
<ItemGroup>
  <PackageReference Include="HtmlML.JavaScript.Interop" Version="..." />
  <PackageReference Include="HtmlML.JavaScript.Interop.Generator"
                    Version="..."
                    PrivateAssets="all" />
</ItemGroup>
```

Then configure the two generator inputs. The package's build-transitive target
adds them as Roslyn `AdditionalFiles` and validates that both exist:

```xml
<PropertyGroup>
  <HtmlMLInteropApiManifest>Interop/TradingView.htmlml-interop-api.json</HtmlMLInteropApiManifest>
  <HtmlMLInteropPolicy>Interop/TradingView.htmlml-interop-policy.json</HtmlMLInteropPolicy>
</PropertyGroup>
```

Generate and compile a complete declaration-package validation with
`htmlml-interop-validate`. Licensed declarations remain local to the
application and are never embedded in either HtmlML package.

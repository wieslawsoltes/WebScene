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

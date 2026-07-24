# HtmlML native engine runtime

This package contains one reviewed native HtmlML V8 DOM/CSS/scene engine for the RID in the
package ID. It is produced by the release pipeline, not restored as a template.

The package includes the native library, its required `icudtl.dat`, the applicable
third-party notices, and a SHA-256/ABI/V8 build manifest. Release packages use V8
pointer compression, its process-wide shared cage, and the size-optimized runtime
policy. Those settings and dense-link status are recorded in the manifest and exposed
as transitive MSBuild properties so a stale or incompatible V8 monolith cannot
silently enter a release.
Release linkage also dead-strips unreachable native sections and restricts the
dynamic export table to HtmlML's public C ABI. Developer builds retain ordinary
symbols unless `HTMLML_NATIVE_ENGINE_DENSE_LINK=ON` is selected explicitly.
Runtime packages compile with `HTMLML_NATIVE_ENGINE_CERTIFICATION=OFF`; feature
inventories, diagnostic snapshots, native profiling state, and their hot-path
counters are not shipped. The stable
`htmlml_engine_get_build_features` ABI reports zero for these package binaries.
The library locates ICU data relative to its own module, so the package remains
relocatable.
Applications must target the same `RuntimeIdentifier`; mixing runtime packages and
RIDs is rejected during the build.

Install the package matching the application's deployment RID, for example:

```xml
<PackageReference Include="HtmlML.NativeEngine.Runtime.osx-arm64" Version="VERSION" />
```

The current release workflow builds `osx-arm64`. Linux and Windows RIDs listed by the
package definition are reserved until their faster, independently validated release
lanes are re-enabled.

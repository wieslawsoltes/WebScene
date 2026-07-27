#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
configuration="Release"
artifacts="${WEBSCENE_R5_ARTIFACTS:-$repo_root/TestResults/R5}"
feed="$artifacts/feed"
generated="$artifacts/generated"
cli_home="$artifacts/dotnet-home"
package_version="$(
  dotnet msbuild "$repo_root/src/WebScene.Core/WebScene.Core.csproj" \
    -getProperty:PackageVersion -nologo |
    tail -n 1 | tr -d '\r'
)"

rm -rf "$artifacts"
mkdir -p "$feed" "$generated" "$cli_home"

projects=(
  src/WebScene.Core/WebScene.Core.csproj
  src/WebScene.JavaScript/WebScene.JavaScript.csproj
  src/WebScene.Dom/WebScene.Dom.csproj
  src/WebScene.Css/WebScene.Css.csproj
  src/WebScene.Graphics/WebScene.Graphics.csproj
  src/WebScene.Backend.Abstractions/WebScene.Backend.Abstractions.csproj
  src/WebScene.Backend.Avalonia/WebScene.Backend.Avalonia.csproj
  src/JavaScript.Avalonia.ClearScript/JavaScript.Avalonia.ClearScript.csproj
  src/WebScene.Sdk/WebScene.Sdk.csproj
  src/WebScene.Sdk.Avalonia/WebScene.Sdk.Avalonia.csproj
  templates/WebScene.Templates/WebScene.Templates.csproj
)

for project in "${projects[@]}"; do
  dotnet pack "$repo_root/$project" -c "$configuration" -o "$feed" -p:WebSceneClearScriptNativeRequired=false -p:WebSceneClearScriptNativeRid=
done

export DOTNET_CLI_HOME="$cli_home"
dotnet new install "$feed/WebScene.Templates.$package_version.nupkg"

cat > "$generated/NuGet.config" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="webscene-r5" value="$feed" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" protocolVersion="3" />
  </packageSources>
</configuration>
EOF

templates=(webscene-component-host webscene-hybrid webscene-typescript)
for template in "${templates[@]}"; do
  output="$generated/$template"
  dotnet new "$template" -n R5Smoke -o "$output" --websceneVersion "$package_version"
  npm install --prefix "$output/web" --no-package-lock "$repo_root/tooling/webscene"
  npm run build --prefix "$output/web"
  if grep -q 'process\.env\.NODE_ENV' "$output/Component/dist/main.js"; then
    printf 'Generated %s bundle leaked process.env.NODE_ENV into the browserless V8 runtime.\n' "$template" >&2
    exit 1
  fi
  dotnet build "$output/R5Smoke.csproj" -c "$configuration" --configfile "$generated/NuGet.config"
  dotnet run --project "$output/R5Smoke.csproj" -c "$configuration" --no-build -- --webscene-smoke
  dotnet publish "$output/R5Smoke.csproj" -c "$configuration" --no-build -o "$output/publish"
done

npm test --prefix "$repo_root/tooling/webscene"
npm ci --prefix "$repo_root/samples/components"
npm run build --prefix "$repo_root/samples/components"
npm run check --prefix "$repo_root/samples/components"
npm test --prefix "$repo_root/samples/components"
dotnet test "$repo_root/tests/WebScene.Sdk.Tests/WebScene.Sdk.Tests.csproj" -c "$configuration"
dotnet run --project "$repo_root/tests/WebScene.Sdk.SampleSmoke/WebScene.Sdk.SampleSmoke.csproj" -c "$configuration"

cat > "$artifacts/summary.json" <<EOF
{
  "milestone": "R5",
  "status": "passed",
  "profileVersion": "1.0",
  "templates": ["webscene-component-host", "webscene-hybrid", "webscene-typescript"],
  "templateCount": 3,
  "sampleCount": 12,
  "componentRuntimeTestCount": 13,
  "templateBrowserlessGlobalGate": true,
  "sdkTargetFrameworks": ["net8.0", "net10.0"]
}
EOF

printf 'R5 SDK smoke passed: packages, 3 templates, Node tooling, SDK contracts, and 12 executed sample scenarios.\n'

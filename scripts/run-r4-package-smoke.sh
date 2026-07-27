#!/usr/bin/env bash

set -euo pipefail
export LC_ALL=C
export LANG=C

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

package_root="$repo_root/TestResults/R4/packages"
mkdir -p "$package_root"

version="$(sed -n -E 's:.*<VersionPrefix>([^<]+)</VersionPrefix>.*:\1:p' Directory.Build.props)"
if [[ -z "$version" ]]; then
  printf 'Unable to read VersionPrefix from Directory.Build.props.\n' >&2
  exit 1
fi

dotnet restore WebScene.sln
dotnet run --project tests/WebScene.Backend.PackageSmoke/WebScene.Backend.PackageSmoke.csproj \
  -c Release

projects=(
  src/WebScene.Core/WebScene.Core.csproj
  src/WebScene.Dom/WebScene.Dom.csproj
  src/WebScene.Css/WebScene.Css.csproj
  src/WebScene.Graphics/WebScene.Graphics.csproj
  src/WebScene.JavaScript/WebScene.JavaScript.csproj
  src/WebScene.Backend.Abstractions/WebScene.Backend.Abstractions.csproj
  src/WebScene.Backend.Avalonia/WebScene.Backend.Avalonia.csproj
)
for project in "${projects[@]}"; do
  dotnet pack "$project" -c Release -o "$package_root" --no-restore
done

python3 scripts/verify-r4-packages.py "$package_root" \
  --output "$repo_root/TestResults/R4/package-graph.json"

config=tests/WebScene.Backend.PackageSmoke/NuGet.local.config
smoke=tests/WebScene.Backend.PackageSmoke/WebScene.Backend.PackageSmoke.csproj
package_id=WebScene.Backend.Avalonia
dotnet restore "$smoke" \
  -p:WebSceneBackendPackageId="$package_id" \
  -p:WebSceneBackendPackageVersion="$version" \
  --configfile "$config"
dotnet run --project "$smoke" -c Release --no-restore \
  -p:WebSceneBackendPackageId="$package_id" \
  -p:WebSceneBackendPackageVersion="$version"

# Leave the solution in its normal project-reference restore mode.
dotnet restore "$smoke"
printf 'R4 package graph and clean-consumer smokes passed.\n'

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$repo_root/artifacts/nuget-packages"
package_version=

usage() {
  echo "Usage: $0 [--output DIR] [--package-version VERSION]" >&2
}

while (($# > 0)); do
  case "$1" in
    --output) output_dir="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$package_version" ]]; then
  package_version="$(
    dotnet msbuild "$repo_root/src/WebScene.Core/WebScene.Core.csproj" \
      -getProperty:PackageVersion -nologo |
      tail -n 1 | tr -d '\r'
  )"
fi
if [[ -z "$package_version" ]]; then
  echo "Unable to resolve the WebScene package version." >&2
  exit 1
fi

mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
dotnet msbuild "$repo_root/build/WebScenePackages.proj" \
  -t:PackPackages \
  -p:Configuration=Release \
  -p:WebScenePackageVersion="$package_version" \
  -p:PackageOutputPath="$output_dir/" \
  -p:ContinuousIntegrationBuild="${CI:-false}" \
  -nologo

python3 "$repo_root/scripts/verify-release-packages.py" \
  "$output_dir" \
  --version "$package_version" \
  --packages-only \
  --output "$output_dir/packages.json"

echo "Packages: $output_dir"

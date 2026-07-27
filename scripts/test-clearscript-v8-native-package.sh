#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT_DIR/tests/JavaScript.Avalonia.V8PackageSmoke/JavaScript.Avalonia.V8PackageSmoke.csproj"

rid=
feed=
version="$(sed -n 's:.*<VersionPrefix>\(.*\)</VersionPrefix>.*:\1:p' "$ROOT_DIR/Directory.Build.props" | head -n 1)"

while (($# > 0)); do
  case "$1" in
    --rid)
      rid="${2:-}"
      shift 2
      ;;
    --feed)
      feed="${2:-}"
      shift 2
      ;;
    --version)
      version="${2:-}"
      shift 2
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$rid" || -z "$feed" || -z "$version" ]]; then
  echo "Usage: $0 --rid <rid> --feed <package-directory> [--version <version>]" >&2
  exit 1
fi

if [[ ! -d "$feed" ]]; then
  echo "Package feed directory does not exist: $feed" >&2
  exit 1
fi

feed="$(cd "$feed" && pwd)"
restore_dir="$(mktemp -d "${TMPDIR:-/tmp}/webscene-v8-package-smoke.XXXXXX")"
trap 'rm -rf "$restore_dir"' EXIT
restore_config="$restore_dir/NuGet.config"
dotnet new nugetconfig --output "$restore_dir" --force >/dev/null
dotnet nuget add source "$feed" \
  --name webscene-v8-native \
  --configfile "$restore_config" >/dev/null

dotnet run --project "$PROJECT" \
  -c Release \
  -p:WebSceneClearScriptNativeRid="$rid" \
  -p:WebSceneClearScriptNativePackageVersion="$version" \
  -p:RestoreConfigFile="$restore_config"

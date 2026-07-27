#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT_DIR/packaging/JavaScript.Avalonia.ClearScript.Native/JavaScript.Avalonia.ClearScript.Native.csproj"

rid=
native_path=
output_dir="$ROOT_DIR/artifacts/v8-native"

while (($# > 0)); do
  case "$1" in
    --rid)
      rid="${2:-}"
      shift 2
      ;;
    --native)
      native_path="${2:-}"
      shift 2
      ;;
    --output)
      output_dir="${2:-}"
      shift 2
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

if [[ -z "$rid" || -z "$native_path" ]]; then
  echo "Usage: $0 --rid <rid> --native <path> [--output <directory>]" >&2
  exit 1
fi

case "$rid" in
  win-x86) expected="ClearScriptV8.win-x86.dll"; format='PE32.*Intel 80386' ;;
  win-x64) expected="ClearScriptV8.win-x64.dll"; format='PE32\+.*x86-64' ;;
  win-arm64) expected="ClearScriptV8.win-arm64.dll"; format='PE32\+.*Aarch64' ;;
  linux-x64) expected="ClearScriptV8.linux-x64.so"; format='ELF.*x86-64' ;;
  linux-arm) expected="ClearScriptV8.linux-arm.so"; format='ELF.*ARM' ;;
  linux-arm64) expected="ClearScriptV8.linux-arm64.so"; format='ELF.*(aarch64|ARM64)' ;;
  osx-x64) expected="ClearScriptV8.osx-x64.dylib"; format='Mach-O.*x86_64' ;;
  osx-arm64) expected="ClearScriptV8.osx-arm64.dylib"; format='Mach-O.*arm64' ;;
  *)
    echo "Unsupported RID '$rid'." >&2
    exit 1
    ;;
esac

if [[ ! -f "$native_path" ]]; then
  echo "Native library does not exist: $native_path" >&2
  exit 1
fi

if [[ "$(basename "$native_path")" != "$expected" ]]; then
  echo "Native file name must be '$expected' for RID '$rid'." >&2
  exit 1
fi

description="$(file -b "$native_path")"
if [[ ! "$description" =~ $format ]]; then
  echo "Native file format does not match RID '$rid': $description" >&2
  exit 1
fi

mkdir -p "$output_dir"
dotnet pack "$PROJECT" \
  -c Release \
  -o "$output_dir" \
  -p:WebSceneClearScriptNativeRid="$rid" \
  -p:WebSceneClearScriptNativePath="$native_path"

cache_path="$output_dir/runtimes/$rid/native/$expected"
mkdir -p "$(dirname "$cache_path")"
cp -f "$native_path" "$cache_path"

echo "Packed reviewed ClearScript V8 native asset for $rid in $output_dir"
echo "Cached native runtime for local builds at $cache_path"

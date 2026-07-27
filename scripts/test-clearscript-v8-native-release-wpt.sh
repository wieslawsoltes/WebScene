#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT_DIR/tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj"

rid=
native_path=
output_dir="$ROOT_DIR/TestResults/V8NativeReleaseWptSmoke"

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

native_path="$(cd "$(dirname "$native_path")" && pwd)/$(basename "$native_path")"
if [[ ! -f "$native_path" ]]; then
  echo "Native library does not exist: $native_path" >&2
  exit 1
fi

env -u WEBSCENE_CLEARSCRIPT_NATIVE -u WEBSCENE_CLEARSCRIPT_RID \
dotnet run --project "$PROJECT" \
  -c Release \
  -p:WebSceneClearScriptNativePath="$native_path" \
  -p:WebSceneClearScriptNativeRid="$rid" \
  -- \
  --selection required \
  --test position-absolute-chrome-bug-001 \
  --output "$output_dir"

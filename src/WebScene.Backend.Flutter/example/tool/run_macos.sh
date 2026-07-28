#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_dir="$(cd "$example_dir/.." && pwd)"
repo_dir="$(cd "$package_dir/../.." && pwd)"
runtime="${WEBSCENE_NATIVE_ENGINE_LIBRARY:-$repo_dir/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib}"
document_url="${WEBSCENE_DOCUMENT_URL:-https://trading-terminal.tradingview-widget.com/?theme=dark}"
ssd_cache_root="${WEBSCENE_FLUTTER_CACHE_ROOT:-/Volumes/SSD/danw/caches/webscene-flutter}"
monaco_web_source="$repo_dir/samples/NativeRuntimeShowcase.Web"
monaco_assets_source="$repo_dir/samples/NativeMonacoEditor/Assets"
monaco_stage="$ssd_cache_root/monaco"

mkdir -p \
  "$ssd_cache_root/pub" \
  "$ssd_cache_root/tmp" \
  "$ssd_cache_root/clang-modules" \
  "$ssd_cache_root/swift-modules" \
  "$ssd_cache_root/v8" \
  "$monaco_stage/Assets"
export PUB_CACHE="$ssd_cache_root/pub"
export TMPDIR="$ssd_cache_root/tmp/"
export CLANG_MODULE_CACHE_PATH="$ssd_cache_root/clang-modules"
export SWIFT_MODULE_CACHE_PATH="$ssd_cache_root/swift-modules"
export COMPILER_INDEX_STORE_ENABLE=NO

if ! command -v flutter >/dev/null 2>&1; then
  echo "Flutter is not on PATH. Install the stable Flutter SDK first." >&2
  exit 1
fi

if [[ ! -f "$runtime" ]]; then
  "$repo_dir/scripts/build-native-engine-runtime.sh" --rid osx-arm64
fi

if [[ -n "${WEBSCENE_MONACO_DOCUMENT_PATH:-}" ]]; then
  monaco_document="$WEBSCENE_MONACO_DOCUMENT_PATH"
else
  cp -f "$monaco_web_source/index.html" "$monaco_stage/index.html"
  cp -f "$monaco_web_source/showcase.js" "$monaco_stage/showcase.js"
  cp -f "$monaco_assets_source/monaco.js" "$monaco_stage/Assets/monaco.js"
  cp -f "$monaco_assets_source/monaco.css" "$monaco_stage/Assets/monaco.css"
  cp -f "$monaco_assets_source/codicon.ttf" "$monaco_stage/Assets/codicon.ttf"
  monaco_document="$monaco_stage/index.html"
fi
if [[ ! -f "$monaco_document" ]]; then
  echo "Monaco document not found at $monaco_document" >&2
  exit 1
fi

bridge="$("$package_dir/tool/build_bridge_macos.sh")"
(
  cd "$example_dir"
  flutter pub get
  flutter run -d macos \
    --dart-define="WEBSCENE_RUNTIME_LIBRARY=$runtime" \
    --dart-define="WEBSCENE_FLUTTER_BRIDGE=$bridge" \
    --dart-define="WEBSCENE_CACHE_DIRECTORY=$ssd_cache_root/v8" \
    --dart-define="WEBSCENE_DOCUMENT_URL=$document_url" \
    --dart-define="WEBSCENE_MONACO_DOCUMENT_PATH=$monaco_document" \
    --dart-define="WEBSCENE_INITIAL_DOCUMENT=${WEBSCENE_INITIAL_DOCUMENT:-tradingview}"
)

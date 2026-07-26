#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_dir="$(cd "$example_dir/.." && pwd)"
repo_dir="$(cd "$package_dir/../.." && pwd)"
runtime="${HTMLML_NATIVE_ENGINE_LIBRARY:-$repo_dir/artifacts/native-engine-runtime-build/osx-arm64/libhtmlml_native_engine.dylib}"
document_url="${HTMLML_DOCUMENT_URL:-https://tv.sandwichtrading.com/index56.html}"
ssd_cache_root="${HTMLML_FLUTTER_CACHE_ROOT:-/Volumes/SSD/danw/caches/htmlml-flutter}"

mkdir -p \
  "$ssd_cache_root/pub" \
  "$ssd_cache_root/tmp" \
  "$ssd_cache_root/clang-modules" \
  "$ssd_cache_root/swift-modules" \
  "$ssd_cache_root/v8"
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

bridge="$("$package_dir/tool/build_bridge_macos.sh")"
(
  cd "$example_dir"
  flutter pub get
  flutter run -d macos \
    --dart-define="HTMLML_RUNTIME_LIBRARY=$runtime" \
    --dart-define="HTMLML_FLUTTER_BRIDGE=$bridge" \
    --dart-define="HTMLML_CACHE_DIRECTORY=$ssd_cache_root/v8" \
    --dart-define="HTMLML_DOCUMENT_URL=$document_url"
)

#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$package_dir/../.." && pwd)"
output_dir="$package_dir/build/native"
output="$output_dir/libwebscene_flutter_bridge.dylib"
ssd_cache_root="${WEBSCENE_FLUTTER_CACHE_ROOT:-/Volumes/SSD/danw/caches/webscene-flutter}"

mkdir -p "$ssd_cache_root/tmp" "$ssd_cache_root/clang-modules"
export TMPDIR="$ssd_cache_root/tmp/"
export CLANG_MODULE_CACHE_PATH="$ssd_cache_root/clang-modules"

mkdir -p "$output_dir"

xcrun clang++ \
  -std=c++17 \
  -O2 \
  -fobjc-arc \
  -dynamiclib \
  -fvisibility=hidden \
  -framework Foundation \
  -framework CoreText \
  -I "$repo_dir/experiments/WebScene.NativeEngine.Probe/native" \
  "$package_dir/native/macos/webscene_flutter_bridge.mm" \
  -o "$output"

codesign --force --sign - "$output"
echo "$output"

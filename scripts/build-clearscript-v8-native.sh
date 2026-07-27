#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_PATHS=(
  "$ROOT_DIR/third-party/clearscript-patches/ClearScript-7.5.1-SharedContextSecurityToken.patch"
  "$ROOT_DIR/third-party/clearscript-patches/ClearScript-7.5.1-TypedManagedAbi.patch"
)
PACK_SCRIPT="$ROOT_DIR/scripts/pack-clearscript-v8-native.sh"
RELEASE_SMOKE_SCRIPT="$ROOT_DIR/scripts/test-clearscript-v8-native-release-wpt.sh"

source_dir="$ROOT_DIR/third-party/clearscript"
rid=
download_v8=false
output_dir="$ROOT_DIR/artifacts/v8-native"

while (($# > 0)); do
  case "$1" in
    --source)
      source_dir="${2:-}"
      shift 2
      ;;
    --rid)
      rid="${2:-}"
      shift 2
      ;;
    --download-v8)
      download_v8=true
      shift
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

if [[ -z "$rid" ]]; then
  echo "Usage: $0 --rid <rid> [--source <ClearScript checkout>] [--download-v8] [--output <directory>]" >&2
  exit 1
fi

if ! git -C "$source_dir" rev-parse --git-dir >/dev/null 2>&1 \
  || [[ ! -f "$source_dir/Unix/ClearScriptV8/Makefile" ]]; then
  echo "ClearScript source checkout not found at: $source_dir" >&2
  exit 1
fi

source_commit="$(git -C "$source_dir" rev-parse HEAD)"
source_base_commit="$(git -C "$source_dir" rev-parse 7.5.1^{commit} 2>/dev/null || true)"
if [[ -z "$source_base_commit" ]] || ! git -C "$source_dir" merge-base --is-ancestor "$source_base_commit" "$source_commit"; then
  echo "ClearScript source must descend from the exact 7.5.1 tag; got $source_commit." >&2
  exit 1
fi
source_branch="$(git -C "$source_dir" branch --show-current)"

case "$rid" in
  osx-x64) required_kernel=Darwin; cpu=x64; native_name="ClearScriptV8.osx-x64.dylib" ;;
  osx-arm64) required_kernel=Darwin; cpu=arm64; native_name="ClearScriptV8.osx-arm64.dylib" ;;
  linux-x64) required_kernel=Linux; cpu=x64; native_name="ClearScriptV8.linux-x64.so" ;;
  linux-arm) required_kernel=Linux; cpu=arm; native_name="ClearScriptV8.linux-arm.so" ;;
  linux-arm64) required_kernel=Linux; cpu=arm64; native_name="ClearScriptV8.linux-arm64.so" ;;
  win-*)
    echo "Windows RIDs must be built with V8Update.cmd and the matching ClearScriptV8.win-* project; use the commands in third-party/clearscript-patches/README.md." >&2
    exit 1
    ;;
  *)
    echo "Unsupported Unix RID '$rid'." >&2
    exit 1
    ;;
esac

kernel="$(uname -s)"
if [[ "$kernel" != "$required_kernel" ]]; then
  echo "RID '$rid' requires a $required_kernel build host; current kernel is $kernel." >&2
  exit 1
fi

for patch_path in "${PATCH_PATHS[@]}"; do
  patch_name="$(basename "$patch_path")"
  if git -C "$source_dir" apply --check "$patch_path" >/dev/null 2>&1; then
    git -C "$source_dir" apply "$patch_path"
    echo "Applied WebScene ClearScript patch: $patch_name"
  elif git -C "$source_dir" apply --reverse --check "$patch_path" >/dev/null 2>&1; then
    echo "WebScene ClearScript patch is already applied: $patch_name"
  else
    echo "Cannot apply or recognize '$patch_name' against ClearScript 7.5.1." >&2
    exit 1
  fi
done

v8_args=(-n -y "$cpu" Release Tested)
if [[ "$download_v8" == true ]]; then
  v8_args=(-y "$cpu" Release Tested)
fi

(
  cd "$source_dir/Unix"
  bash ./V8Update.sh "${v8_args[@]}"
)

# The WebScene patch makes the native build verify the monolith produced above instead
# of invoking V8Update.sh a second time and potentially replacing the reviewed cache.
make -f "$source_dir/Unix/ClearScriptV8/Makefile" CPU="$cpu"

native_path="$source_dir/bin/Release/Unix/$native_name"
if [[ ! -f "$native_path" ]]; then
  echo "Expected native output was not produced: $native_path" >&2
  exit 1
fi

host_rid=
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) host_rid=osx-arm64 ;;
  Darwin-x86_64) host_rid=osx-x64 ;;
  Linux-x86_64) host_rid=linux-x64 ;;
  Linux-aarch64) host_rid=linux-arm64 ;;
  Linux-armv7l) host_rid=linux-arm ;;
esac
if [[ "$rid" == "$host_rid" ]]; then
  "$RELEASE_SMOKE_SCRIPT" \
    --rid "$rid" \
    --native "$native_path" \
    --output "$output_dir/release-wpt-smoke/$rid"
else
  echo "Skipping Release WPT native smoke for cross-architecture RID '$rid' on '$host_rid'."
fi

"$PACK_SCRIPT" --rid "$rid" --native "$native_path" --output "$output_dir"

echo "ClearScript source: $source_commit (base tag 7.5.1, branch ${source_branch:-detached})"
echo "V8 revision: 14.7.173.23"
echo "Native output: $native_path"

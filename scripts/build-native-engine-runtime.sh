#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rid=
output_dir="$repo_root/artifacts/native-engine-runtime"
package_version=
v8_root=
v8_workspace=
v8_revision=14.7.173.23

usage() {
  echo "Usage: $0 --rid osx-arm64|osx-x64|linux-arm64|linux-x64 [--output DIR] [--package-version VERSION] [--v8-root DIR] [--v8-workspace DIR]" >&2
}

while (($# > 0)); do
  case "$1" in
    --rid) rid="${2:-}"; shift 2 ;;
    --output) output_dir="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    --v8-root) v8_root="${2:-}"; shift 2 ;;
    --v8-workspace) v8_workspace="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

case "$rid" in
  osx-arm64) expected_kernel=Darwin; expected_machine=arm64; cpu=arm64; native_name=libhtmlml_native_engine.dylib ;;
  osx-x64) expected_kernel=Darwin; expected_machine=x86_64; cpu=x64; native_name=libhtmlml_native_engine.dylib ;;
  linux-arm64) expected_kernel=Linux; expected_machine=aarch64; cpu=arm64; native_name=libhtmlml_native_engine.so ;;
  linux-x64) expected_kernel=Linux; expected_machine=x86_64; cpu=x64; native_name=libhtmlml_native_engine.so ;;
  *) usage; exit 1 ;;
esac

if [[ -z "$package_version" ]]; then
  package_version="$(
    dotnet msbuild "$repo_root/src/HtmlML.Core/HtmlML.Core.csproj" \
      -getProperty:PackageVersion -nologo |
      tail -n 1 | tr -d '\r'
  )"
fi
if [[ -z "$package_version" ]]; then
  echo "Unable to resolve the native runtime package version." >&2
  exit 1
fi

if [[ "$(uname -s)" != "$expected_kernel" || "$(uname -m)" != "$expected_machine" ]]; then
  echo "RID '$rid' must be built natively on $expected_kernel/$expected_machine; current host is $(uname -s)/$(uname -m)." >&2
  exit 1
fi

if [[ -z "$v8_root" ]]; then
  v8_workspace="${v8_workspace:-$repo_root/artifacts/native-engine-v8/$rid}"
  depot_tools="$v8_workspace/depot_tools"
  v8_root="$v8_workspace/v8"
  mkdir -p "$v8_workspace"

  if [[ ! -d "$depot_tools/.git" ]]; then
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$depot_tools"
  fi
  export PATH="$depot_tools:$PATH"
  if [[ ! -f "$depot_tools/python3_bin_reldir.txt" ]]; then
    "$depot_tools/ensure_bootstrap"
  fi
  export DEPOT_TOOLS_UPDATE=0

  if [[ ! -f "$v8_workspace/.gclient" ]]; then
    (
      cd "$v8_workspace"
      gclient config https://chromium.googlesource.com/v8/v8
    )
  fi
  (
    cd "$v8_workspace"
    gclient sync --no-history -r "$v8_revision"
  )

  apply_patch_once() {
    local checkout="$1"
    local patch_file="$2"
    if git -C "$checkout" apply --check "$patch_file" >/dev/null 2>&1; then
      git -C "$checkout" apply "$patch_file"
    elif ! git -C "$checkout" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
      echo "Cannot apply or recognize V8 patch '$patch_file' in '$checkout'." >&2
      exit 1
    fi
  }
  apply_patch_once "$v8_root" "$repo_root/third-party/clearscript/V8/V8Patch.txt"
  apply_patch_once "$v8_root" "$repo_root/packaging/HtmlML.NativeEngine.Runtime/patches/V8ToolchainPatch.txt"
  apply_patch_once "$v8_root/build" "$repo_root/third-party/clearscript/V8/BuildPatch.txt"
  apply_patch_once "$v8_root/third_party/icu" "$repo_root/third-party/clearscript/V8/ICUPatch.txt"

  if [[ "$expected_kernel" == Linux ]]; then
    "$v8_root/build/linux/sysroot_scripts/install-sysroot.py" --arch="$cpu"
  fi

  gn_args="chrome_pgo_phase=0 fatal_linker_warnings=false is_cfi=false is_component_build=false is_debug=false symbol_level=0 target_cpu=\"$cpu\" treat_warnings_as_errors=false use_clang_modules=false use_custom_libcxx=false use_thin_lto=false v8_embedder_string=\"-HtmlML\" v8_enable_fuzztest=false v8_enable_partition_alloc=false v8_enable_pointer_compression=true v8_enable_pointer_compression_shared_cage=true v8_enable_sandbox=false v8_enable_static_roots=false v8_enable_31bit_smis_on_64bit_arch=false v8_enable_temporal_support=false v8_monolithic=true v8_use_external_startup_data=false v8_target_cpu=\"$cpu\""
  (
    cd "$v8_root"
    gn gen "out/$cpu/Release" --args="$gn_args"
    ninja -C "out/$cpu/Release" obj/libv8_monolith.a
  )
fi

v8_root="$(cd "$v8_root" && pwd)"
v8_monolith="$v8_root/out/$cpu/Release/obj/libv8_monolith.a"
icu_data="$v8_root/out/$cpu/Release/icudtl.dat"
v8_args="$v8_root/out/$cpu/Release/args.gn"
v8_license="$v8_root/LICENSE"
icu_license="$v8_root/third_party/icu/LICENSE"
for required in "$v8_root/include/v8.h" "$v8_monolith" "$icu_data" "$v8_args" "$v8_license" "$icu_license"; do
  if [[ ! -f "$required" ]]; then
    echo "Required native runtime input is missing: $required" >&2
    exit 1
  fi
done
if ! grep -Eq '^v8_enable_pointer_compression *= *true$' "$v8_args" \
    || ! grep -Eq '^v8_enable_pointer_compression_shared_cage *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' is not the required pointer-compressed shared-cage build." >&2
  exit 1
fi

build_dir="$repo_root/artifacts/native-engine-runtime-build/$rid"
cmake_args=(
  -S "$repo_root/experiments/HtmlML.NativeEngine.Probe"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE=Release
  -DHTMLML_NATIVE_ENGINE_ENABLE_V8=ON
  -DHTMLML_V8_POINTER_COMPRESSION=ON
  -DHTMLML_V8_POINTER_COMPRESSION_SHARED_CAGE=ON
  -DHTMLML_V8_OPTIMIZE_FOR_SIZE_DEFAULT=ON
  -DHTMLML_NATIVE_ENGINE_DENSE_LINK=ON
  -DHTMLML_NATIVE_ENGINE_CERTIFICATION=OFF
  -DHTMLML_V8_ROOT="$v8_root"
)
if [[ "$expected_kernel" == Linux ]]; then
  # V8 is built with Clang and its Linux archive may contain Clang/LTO
  # metadata. Linking it through GCC's GNU ld produces the misleading
  # "unknown architecture ... i386:x86-64" diagnostics seen in CI.
  if ! command -v clang++ >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "Linux native runtime builds require clang++ and ld.lld to link the Clang-built V8 monolith." >&2
    exit 1
  fi
  cmake_args+=(
    -DCMAKE_CXX_COMPILER=clang++
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
  )
fi
cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config Release --parallel

native_path="$build_dir/$native_name"
if [[ ! -f "$native_path" ]]; then
  echo "Native engine build did not produce '$native_path'." >&2
  exit 1
fi

mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
pack_args=(
  "$repo_root/packaging/HtmlML.NativeEngine.Runtime/HtmlML.NativeEngine.Runtime.csproj"
  -c Release
  -o "$output_dir"
  "-p:HtmlMlNativeEngineRid=$rid"
  "-p:HtmlMlNativeEnginePath=$native_path"
  "-p:HtmlMlNativeEngineIcuDataPath=$icu_data"
  "-p:HtmlMlNativeEngineV8LicensePath=$v8_license"
  "-p:HtmlMlNativeEngineIcuLicensePath=$icu_license"
  "-p:HtmlMlNativeEngineV8PointerCompression=true"
  "-p:HtmlMlNativeEngineV8SharedCage=true"
  "-p:HtmlMlNativeEngineV8OptimizeForSizeDefault=true"
  "-p:HtmlMlNativeEngineDenseLink=true"
)
pack_args+=("-p:PackageVersion=$package_version")
dotnet pack "${pack_args[@]}"

package_path="$output_dir/HtmlML.NativeEngine.Runtime.$rid.$package_version.nupkg"
if [[ ! -f "$package_path" ]]; then
  echo "The RID package was not produced in '$output_dir'." >&2
  exit 1
fi
package_smoke_dir="$build_dir/package-smoke"
cmake -E remove_directory "$package_smoke_dir"
cmake -E make_directory "$package_smoke_dir"
(cd "$package_smoke_dir" && cmake -E tar xf "$package_path")
package_native_path="$package_smoke_dir/runtimes/$rid/native/$native_name"

dotnet run \
  --project "$repo_root/tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj" \
  -c Release -- \
  --engine native \
  --selection required \
  --test contracts/responsive-release-list.html \
  --native-library "$package_native_path" \
  --native-cache-directory "$build_dir/code-cache" \
  --output "$build_dir/wpt-results"

consumer_root="$(mktemp -d)"
consumer_dir="$consumer_root/consumer"
dotnet new console --framework net8.0 --no-restore --output "$consumer_dir"
NUGET_PACKAGES="$consumer_root/packages" dotnet add "$consumer_dir/consumer.csproj" package \
  "HtmlML.NativeEngine.Runtime.$rid" \
  --version "$package_version" \
  --no-restore
NUGET_PACKAGES="$consumer_root/packages" dotnet restore \
  "$consumer_dir/consumer.csproj" -r "$rid" \
  --source "$output_dir" \
  --source https://api.nuget.org/v3/index.json
NUGET_PACKAGES="$consumer_root/packages" dotnet build \
  "$consumer_dir/consumer.csproj" -c Release -r "$rid" --no-restore
for copied_asset in "$native_name" icudtl.dat htmlml-native-runtime.json; do
  copied_path="$consumer_dir/bin/Release/net8.0/$rid/$copied_asset"
  if [[ ! -f "$copied_path" ]]; then
    echo "The runtime package did not copy '$copied_asset' to consumer output." >&2
    exit 1
  fi
done

echo "Native runtime: $native_path"
echo "RID package: $package_path"

#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rid=
output_dir="$repo_root/artifacts/native-engine-runtime"
package_version=
v8_root=
v8_output_root=
v8_workspace=
v8_revision=14.7.173.23
thin_lto=false
upstream_v8=false
disable_wasm=false
partition_alloc=false

usage() {
  echo "Usage: $0 --rid osx-arm64|osx-x64|linux-arm64|linux-x64 [--output DIR] [--package-version VERSION] [--v8-root DIR] [--v8-output-root DIR] [--v8-workspace DIR] [--v8-revision REVISION] [--upstream-v8] [--thin-lto] [--disable-wasm] [--partition-alloc]" >&2
}

while (($# > 0)); do
  case "$1" in
    --rid) rid="${2:-}"; shift 2 ;;
    --output) output_dir="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    --v8-root) v8_root="${2:-}"; shift 2 ;;
    --v8-output-root) v8_output_root="${2:-}"; shift 2 ;;
    --v8-workspace) v8_workspace="${2:-}"; shift 2 ;;
    --v8-revision) v8_revision="${2:-}"; shift 2 ;;
    --upstream-v8) upstream_v8=true; shift ;;
    --thin-lto) thin_lto=true; shift ;;
    --disable-wasm) disable_wasm=true; shift ;;
    --partition-alloc) partition_alloc=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

case "$rid" in
  osx-arm64) expected_kernel=Darwin; expected_machine=arm64; cpu=arm64; native_name=libwebscene_native_engine.dylib ;;
  osx-x64) expected_kernel=Darwin; expected_machine=x86_64; cpu=x64; native_name=libwebscene_native_engine.dylib ;;
  linux-arm64) expected_kernel=Linux; expected_machine=aarch64; cpu=arm64; native_name=libwebscene_native_engine.so ;;
  linux-x64) expected_kernel=Linux; expected_machine=x86_64; cpu=x64; native_name=libwebscene_native_engine.so ;;
  *) usage; exit 1 ;;
esac

v8_configuration=Release
build_variant=
thin_lto_cmake=OFF
partition_alloc_cmake=OFF
v8_webassembly=true
if [[ "$thin_lto" == true ]]; then
  v8_configuration=ReleaseThinLto
  build_variant=-thinlto-llvm
  thin_lto_cmake=ON
fi
if [[ "$disable_wasm" == true ]]; then
  v8_configuration+=NoWasm
  build_variant+=-no-wasm
  v8_webassembly=false
fi
if [[ "$partition_alloc" == true ]]; then
  v8_configuration+=PartitionAlloc
  build_variant+=-partitionalloc
  partition_alloc_cmake=ON
fi

if [[ -z "$package_version" ]]; then
  package_version="$(
    dotnet msbuild "$repo_root/src/WebScene.Core/WebScene.Core.csproj" \
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
    clone_attempt=1
    while ! git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$depot_tools"; do
      if ((clone_attempt >= 3)); then
        echo "Unable to clone depot_tools after $clone_attempt attempts." >&2
        exit 1
      fi
      echo "depot_tools clone failed; retrying (attempt $((clone_attempt + 1))/3)." >&2
      rm -rf "$depot_tools"
      clone_attempt=$((clone_attempt + 1))
    done
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
  if [[ "$upstream_v8" == false ]]; then
    apply_patch_once "$v8_root" "$repo_root/third-party/clearscript/V8/V8Patch.txt"
    apply_patch_once "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ToolchainPatch.txt"
    apply_patch_once "$v8_root/build" "$repo_root/third-party/clearscript/V8/BuildPatch.txt"
    apply_patch_once "$v8_root/third_party/icu" "$repo_root/third-party/clearscript/V8/ICUPatch.txt"
  fi
  if [[ "$thin_lto" == true ]]; then
    apply_patch_once "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ThinLtoPatch.txt"
  fi
  if [[ "$partition_alloc" == true && "$expected_kernel" == Darwin ]]; then
    apply_patch_once \
      "$v8_root/third_party/partition_alloc/src" \
      "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8PartitionAllocMacVisibilityPatch.txt"
  fi
  if [[ "$expected_kernel" == Linux ]]; then
    apply_patch_once "$v8_root/build" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8BuildNoCrelPatch.txt"
  fi

  gn_args="chrome_pgo_phase=0 fatal_linker_warnings=false is_cfi=false is_component_build=false is_debug=false symbol_level=0 target_cpu=\"$cpu\" treat_warnings_as_errors=false use_clang_modules=false use_custom_libcxx=false use_thin_lto=$thin_lto v8_embedder_string=\"-WebScene\" v8_enable_fuzztest=false v8_enable_partition_alloc=$partition_alloc v8_enable_pointer_compression=true v8_enable_pointer_compression_shared_cage=true v8_enable_sandbox=false v8_enable_static_roots=false v8_enable_31bit_smis_on_64bit_arch=false v8_enable_temporal_support=false v8_enable_webassembly=$v8_webassembly v8_monolithic=true v8_use_external_startup_data=false v8_target_cpu=\"$cpu\""
  if [[ "$expected_kernel" == Linux ]]; then
    # V8 15.3 requires C++20 library headers that are newer than its downloaded
    # Debian Bullseye sysroot. Build inside the pinned Ubuntu 22.04 image
    # against that image's libstdc++ and glibc 2.35 instead.
    # Keep V8's bundled LLD for its host tools; the reviewed build patch above
    # disables only CREL emission so Jammy can consume the archive.
    gn_args+=" use_lld=true use_sysroot=false v8_monolithic_for_shared_library=true"
  fi
  (
    cd "$v8_root"
    gn gen "out/$cpu/$v8_configuration" --args="$gn_args"
    ninja -C "out/$cpu/$v8_configuration" obj/libv8_monolith.a
  )
  v8_output_root="$v8_root/out/$cpu/$v8_configuration"
fi

v8_root="$(cd "$v8_root" && pwd)"
v8_output_root="${v8_output_root:-$v8_root/out/$cpu/$v8_configuration}"
if [[ ! -d "$v8_output_root" ]]; then
  echo "V8 output directory is missing: $v8_output_root" >&2
  exit 1
fi
v8_output_root="$(cd "$v8_output_root" && pwd)"
v8_monolith="$v8_output_root/obj/libv8_monolith.a"
icu_data="$v8_output_root/icudtl.dat"
v8_args="$v8_output_root/args.gn"
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
if ! grep -Eq "^use_thin_lto *= *$thin_lto$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested ThinLTO=$thin_lto." >&2
  exit 1
fi
if ! grep -Eq "^v8_enable_partition_alloc *= *$partition_alloc$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested PartitionAlloc=$partition_alloc." >&2
  exit 1
fi
if grep -Eq '^v8_enable_webassembly *=' "$v8_args" \
    && ! grep -Eq "^v8_enable_webassembly *= *$v8_webassembly$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested WebAssembly=$v8_webassembly." >&2
  exit 1
fi
if [[ "$v8_webassembly" == false ]] \
    && ! grep -Eq '^v8_enable_webassembly *= *false$' "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not explicitly disable WebAssembly." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && ! grep -Eq '^use_lld *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' was not built with the required patched LLD configuration." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && ! grep -Eq '^v8_monolithic_for_shared_library *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' is not safe to link into a shared library." >&2
  exit 1
fi

build_dir="$repo_root/artifacts/native-engine-runtime-build/$rid$build_variant"
cmake_args=(
  -S "$repo_root/experiments/WebScene.NativeEngine.Probe"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE=Release
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON
  -DWEBSCENE_V8_POINTER_COMPRESSION=ON
  -DWEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE=ON
  -DWEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=ON
  -DWEBSCENE_V8_PARTITION_ALLOC="$partition_alloc_cmake"
  -DWEBSCENE_NATIVE_ENGINE_DENSE_LINK=ON
  -DWEBSCENE_NATIVE_ENGINE_THIN_LTO="$thin_lto_cmake"
  -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF
  -DWEBSCENE_V8_ROOT="$v8_root"
  -DWEBSCENE_V8_OUTPUT_ROOT="$v8_output_root"
)
if [[ "$thin_lto" == true ]]; then
  v8_llvm_bin="$v8_root/third_party/llvm-build/Release+Asserts/bin"
  for llvm_tool in clang clang++ llvm-ar lld; do
    if [[ ! -x "$v8_llvm_bin/$llvm_tool" ]]; then
      echo "ThinLTO requires V8's LLVM tool '$v8_llvm_bin/$llvm_tool'." >&2
      exit 1
    fi
  done
  v8_llvm_ranlib="$v8_llvm_bin/llvm-ranlib"
  if [[ ! -x "$v8_llvm_ranlib" ]]; then
    # Chromium omits the redundant multi-call symlink in some toolchain
    # bundles. llvm-ar selects ranlib mode from argv[0]. Keep the link beside
    # clang so CMake's nested IPO capability checks can discover it.
    ln -s "$v8_llvm_bin/llvm-ar" "$v8_llvm_ranlib"
  fi
  # ThinLTO bitcode is versioned. Compile and link the embedding library with
  # the exact Chromium LLVM toolchain that produced the V8 archive.
  cmake_args+=(
    -DCMAKE_C_COMPILER="$v8_llvm_bin/clang"
    -DCMAKE_CXX_COMPILER="$v8_llvm_bin/clang++"
    -DCMAKE_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_C_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_C_COMPILER_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_CXX_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_CXX_COMPILER_RANLIB="$v8_llvm_ranlib"
    "-DCMAKE_C_FLAGS=-fuse-ld=lld -Wno-unused-command-line-argument"
    "-DCMAKE_CXX_FLAGS=-fuse-ld=lld -Wno-unused-command-line-argument"
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld
  )
  if [[ "$expected_kernel" == Darwin ]]; then
    cmake_args+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0)
  fi
elif [[ "$expected_kernel" == Linux ]]; then
  # V8's Linux archive must be linked with LLD. The compiler is selectable so
  # the Ubuntu 22.04 compatibility image can use GCC 11's complete C++20
  # standard library instead of Jammy's Clang 14 source_location support.
  linux_cxx="${CXX:-clang++}"
  if ! command -v "$linux_cxx" >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "Linux native runtime builds require '$linux_cxx' and ld.lld." >&2
    exit 1
  fi
  cmake_args+=(
    -DCMAKE_CXX_COMPILER="$linux_cxx"
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
  )
fi
cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config Release --parallel
cmake -E copy_if_different "$icu_data" "$build_dir/icudtl.dat"
ctest --test-dir "$build_dir" -C Release --output-on-failure

native_path="$build_dir/$native_name"
if [[ ! -f "$native_path" ]]; then
  echo "Native engine build did not produce '$native_path'." >&2
  exit 1
fi
ixwebsocket_license="$build_dir/_deps/webscene_ixwebsocket-src/LICENSE.txt"
if [[ ! -f "$ixwebsocket_license" ]]; then
  echo "IXWebSocket license was not found at '$ixwebsocket_license'." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && readelf -SW "$native_path" 2>&1 | grep -Eq '\.crel(\.|$)'; then
  echo "Native engine output contains unsupported CREL relocation sections: $native_path" >&2
  exit 1
fi

mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
pack_args=(
  "$repo_root/packaging/WebScene.NativeEngine.Runtime/WebScene.NativeEngine.Runtime.csproj"
  -c Release
  -o "$output_dir"
  "-p:WebSceneNativeEngineRid=$rid"
  "-p:WebSceneNativeEnginePath=$native_path"
  "-p:WebSceneNativeEngineIcuDataPath=$icu_data"
  "-p:WebSceneNativeEngineV8LicensePath=$v8_license"
  "-p:WebSceneNativeEngineIcuLicensePath=$icu_license"
  "-p:WebSceneNativeEngineIXWebSocketLicensePath=$ixwebsocket_license"
  "-p:WebSceneNativeEngineV8PointerCompression=true"
  "-p:WebSceneNativeEngineV8SharedCage=true"
  "-p:WebSceneNativeEngineV8OptimizeForSizeDefault=true"
  "-p:WebSceneNativeEngineV8PartitionAlloc=$partition_alloc"
  "-p:WebSceneNativeEngineDenseLink=true"
  "-p:WebSceneNativeEngineThinLto=$thin_lto"
  "-p:WebSceneNativeEngineV8Revision=$v8_revision"
)
pack_args+=("-p:PackageVersion=$package_version")
dotnet pack "${pack_args[@]}"

package_path="$output_dir/WebScene.NativeEngine.Runtime.$rid.$package_version.nupkg"
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
  --project "$repo_root/tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj" \
  -c Release -- \
  --engine native \
  --selection required \
  --test contracts/responsive-release-list.html \
  --native-library "$package_native_path" \
  --native-cache-directory "$build_dir/code-cache" \
  --output "$build_dir/wpt-results"

consumer_smoke_root="$repo_root/artifacts/native-engine-consumer-smoke"
mkdir -p "$consumer_smoke_root"
consumer_root="$(mktemp -d "$consumer_smoke_root/consumer.XXXXXX")"
consumer_dir="$consumer_root/consumer"
cmake -E copy_if_different \
  "$repo_root/packaging/WebScene.NativeEngine.Runtime/ConsumerSmoke.Directory.Packages.props" \
  "$consumer_root/Directory.Packages.props"
consumer_framework=net8.0
if [[ "$expected_kernel" == Linux ]]; then
  # The compatibility image intentionally carries the pinned .NET 10 SDK.
  consumer_framework=net10.0
fi
dotnet new console --framework "$consumer_framework" --no-restore --output "$consumer_dir"
NUGET_PACKAGES="$consumer_root/packages" dotnet add "$consumer_dir/consumer.csproj" package \
  "WebScene.NativeEngine.Runtime.$rid" \
  --version "$package_version" \
  --no-restore
NUGET_PACKAGES="$consumer_root/packages" dotnet restore \
  "$consumer_dir/consumer.csproj" -r "$rid" \
  --source "$output_dir" \
  --source https://api.nuget.org/v3/index.json
NUGET_PACKAGES="$consumer_root/packages" dotnet build \
  "$consumer_dir/consumer.csproj" -c Release -r "$rid" --no-restore
for copied_asset in "$native_name" icudtl.dat webscene-native-runtime.json; do
  copied_path="$consumer_dir/bin/Release/$consumer_framework/$rid/$copied_asset"
  if [[ ! -f "$copied_path" ]]; then
    echo "The runtime package did not copy '$copied_asset' to consumer output." >&2
    exit 1
  fi
done

echo "Native runtime: $native_path"
echo "RID package: $package_path"

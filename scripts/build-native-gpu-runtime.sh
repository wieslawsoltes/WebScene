#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rid=
output_dir="$repo_root/artifacts/native-gpu-runtime"
workspace="$repo_root/artifacts/native-gpu-dawn/osx-arm64"
package_version=
dawn_revision=710c33013c53ab2700d332c25ff51430251a8cc4
dawn_repository=https://dawn.googlesource.com/dawn

usage() {
  echo "Usage: $0 --rid osx-arm64 [--output DIR] [--package-version VERSION] [--workspace DIR] [--dawn-revision REVISION]" >&2
}

while (($# > 0)); do
  case "$1" in
    --rid) rid="${2:-}"; shift 2 ;;
    --output) output_dir="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    --workspace) workspace="${2:-}"; shift 2 ;;
    --dawn-revision) dawn_revision="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ "$rid" != osx-arm64 ]]; then
  usage
  exit 1
fi
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo "RID 'osx-arm64' must be built natively on Darwin/arm64." >&2
  exit 1
fi
if [[ -z "$package_version" ]]; then
  package_version="$(
    dotnet msbuild "$repo_root/src/WebScene.Core/WebScene.Core.csproj" \
      -getProperty:PackageVersion -nologo | tail -n 1 | tr -d '\r'
  )"
fi
if [[ -z "$package_version" ]]; then
  echo "Unable to resolve the native GPU runtime package version." >&2
  exit 1
fi

source_dir="$workspace/src"
dawn_build_dir="$workspace/build"
dawn_install_dir="$workspace/install"
provider_build_dir="$repo_root/artifacts/native-gpu-provider-build/$rid"
mkdir -p "$workspace"
if [[ ! -d "$source_dir/.git" ]]; then
  mkdir -p "$source_dir"
  git -C "$source_dir" init
  git -C "$source_dir" remote add origin "$dawn_repository"
fi
if [[ "$(git -C "$source_dir" remote get-url origin)" != "$dawn_repository" ]]; then
  echo "Dawn checkout has an unexpected origin: $source_dir" >&2
  exit 1
fi
if ! git -C "$source_dir" cat-file -e "$dawn_revision^{commit}" 2>/dev/null; then
  git -C "$source_dir" fetch --depth 1 origin "$dawn_revision"
fi
git -C "$source_dir" checkout --detach --force "$dawn_revision"
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$dawn_revision" ]]; then
  echo "Dawn checkout did not resolve to the requested revision." >&2
  exit 1
fi

cmake -S "$source_dir" -B "$dawn_build_dir" -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$dawn_install_dir" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DBUILD_SHARED_LIBS=OFF \
  -DDAWN_FETCH_DEPENDENCIES=ON \
  -DDAWN_ENABLE_INSTALL=ON \
  -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC \
  -DDAWN_ENABLE_METAL=ON \
  -DDAWN_ENABLE_NULL=OFF \
  -DDAWN_ENABLE_D3D11=OFF \
  -DDAWN_ENABLE_D3D12=OFF \
  -DDAWN_ENABLE_DESKTOP_GL=OFF \
  -DDAWN_ENABLE_OPENGLES=OFF \
  -DDAWN_ENABLE_VULKAN=OFF \
  -DDAWN_ENABLE_WEBGPU_ON_WEBGPU=OFF \
  -DDAWN_USE_GLFW=OFF \
  -DDAWN_BUILD_SAMPLES=OFF \
  -DDAWN_BUILD_TESTS=OFF \
  -DDAWN_BUILD_NODE_BINDINGS=OFF \
  -DDAWN_BUILD_BENCHMARKS=OFF \
  -DDAWN_BUILD_FUZZERS=OFF \
  -DDAWN_BUILD_PROTOBUF=OFF \
  -DTINT_BUILD_CMD_TOOLS=OFF \
  -DTINT_BUILD_TESTS=OFF \
  -DTINT_BUILD_BENCHMARKS=OFF \
  -DTINT_BUILD_FUZZERS=OFF \
  -DTINT_BUILD_SPV_READER=OFF \
  -DTINT_BUILD_SPV_WRITER=OFF \
  -DTINT_BUILD_GLSL_VALIDATOR=OFF
cmake --build "$dawn_build_dir" --target webgpu_dawn --parallel
cmake --install "$dawn_build_dir"

cmake -S "$repo_root/experiments/WebScene.NativeGpu.Provider" \
  -B "$provider_build_dir" -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DDawn_DIR="$dawn_install_dir/lib/cmake/Dawn"
cmake --build "$provider_build_dir" --parallel
ctest --test-dir "$provider_build_dir" -C Release --output-on-failure

provider_path="$provider_build_dir/libwebscene_native_gpu.dylib"
if [[ ! -f "$provider_path" ]]; then
  echo "GPU provider build did not produce '$provider_path'." >&2
  exit 1
fi
mkdir -p "$output_dir"
dotnet pack \
  "$repo_root/packaging/WebScene.NativeGpu.Runtime/WebScene.NativeGpu.Runtime.csproj" \
  -c Release -o "$output_dir" \
  "-p:WebSceneNativeGpuRid=$rid" \
  "-p:WebSceneNativeGpuPath=$provider_path" \
  "-p:WebSceneNativeGpuDawnRevision=$dawn_revision" \
  "-p:WebSceneNativeGpuDawnLicensePath=$source_dir/LICENSE" \
  "-p:WebSceneNativeGpuAbseilLicensePath=$source_dir/third_party/abseil-cpp/LICENSE" \
  "-p:WebSceneNativeGpuWebGpuHeadersLicensePath=$source_dir/third_party/webgpu-headers/LICENSE" \
  "-p:PackageVersion=$package_version"

package_path="$output_dir/WebScene.NativeGpu.Runtime.$rid.$package_version.nupkg"
if [[ ! -f "$package_path" ]]; then
  echo "The GPU RID package was not produced in '$output_dir'." >&2
  exit 1
fi
echo "Native GPU provider: $provider_path"
echo "RID package: $package_path"

#!/usr/bin/env bash
set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
v8_root="$repo_root/artifacts/native-engine-v8/linux-x64/v8"
build_dir="$repo_root/artifacts/native-engine-runtime-build/linux-x64"

set +e
"$repo_root/scripts/build-native-engine-runtime.sh" "$@"
package_status=$?

native_test_status=0
icu_data="$v8_root/out/x64/Release/icudtl.dat"
if [[ -f "$icu_data" && -d "$build_dir" ]]; then
  cmake -E copy_if_different "$icu_data" "$build_dir/icudtl.dat"
  ctest --test-dir "$build_dir" -C Release --output-on-failure
  native_test_status=$?
fi
set -e

if ((package_status != 0)); then
  exit "$package_status"
fi
exit "$native_test_status"

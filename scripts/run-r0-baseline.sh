#!/usr/bin/env bash

set -euo pipefail
export LC_ALL=C
export LANG=C

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
profile="ci"
output_root=""

usage() {
  printf '%s\n' \
    "Usage: scripts/run-r0-baseline.sh [--profile ci|native] [--output <directory>]" \
    "  ci      Release build, portable/Avalonia tests, coverage, CSS probes, WPT list" \
    "  native  ci plus required WPT and focused V8 contract probes"
}

while (($# > 0)); do
  case "$1" in
    --profile)
      profile="${2:?Missing value after --profile}"
      shift 2
      ;;
    --output)
      output_root="${2:?Missing value after --output}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown argument: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$profile" != "ci" && "$profile" != "native" ]]; then
  printf 'Unsupported profile: %s\n' "$profile" >&2
  exit 2
fi

cd "$repo_root"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
if [[ -z "$output_root" ]]; then
  output_root="$repo_root/TestResults/R0/${timestamp}-${profile}"
else
  output_parent="$(dirname "$output_root")"
  mkdir -p "$output_parent"
  output_root="$(cd "$output_parent" && pwd)/$(basename "$output_root")"
fi
mkdir -p \
  "$output_root/logs" \
  "$output_root/coverage/net8.0" \
  "$output_root/coverage/net10.0" \
  "$output_root/coverage/core/net8.0" \
  "$output_root/coverage/core/net10.0" \
  "$output_root/coverage/css/net8.0" \
  "$output_root/coverage/css/net10.0" \
  "$output_root/coverage/graphics/net8.0" \
  "$output_root/coverage/graphics/net10.0" \
  "$output_root/coverage/backend-abstractions/net8.0" \
  "$output_root/coverage/backend-abstractions/net10.0" \
  "$output_root/wpt"
status_file="$output_root/gates.tsv"
printf 'gate\tstatus\texit_code\n' > "$status_file"

commit="$(git rev-parse HEAD)"
if [[ -n "$(git status --porcelain)" ]]; then dirty=true; else dirty=false; fi
sdk="$(dotnet --version)"
os="$(uname -s)"
architecture="$(uname -m)"
machine="$(uname -n)"
cpu_model="$(sysctl -n machdep.cpu.brand_string 2>/dev/null \
  || awk -F: '/model name/ { sub(/^[ \t]+/, "", $2); print $2; exit }' /proc/cpuinfo 2>/dev/null \
  || uname -p)"
memory_bytes="$(sysctl -n hw.memsize 2>/dev/null \
  || awk '/MemTotal/ { print $2 * 1024; exit }' /proc/meminfo 2>/dev/null \
  || printf 'unknown')"
rid="${WEBSCENE_CLEARSCRIPT_RID:-not-configured}"
native_path="${WEBSCENE_CLEARSCRIPT_NATIVE:-}"
native_sha256="not-configured"
react_root="${WEBSCENE_REACT_REPRO_ROOT:-not-configured}"
react_sha256="not-configured"
if [[ -n "$native_path" && -f "$native_path" ]]; then
  native_sha256="$(shasum -a 256 "$native_path" | awk '{print $1}')"
fi
if [[ "$profile" != "ci" ]]; then
  if [[ -z "$native_path" || ! -f "$native_path" || "$rid" == "not-configured" ]]; then
    printf 'Profile native requires WEBSCENE_CLEARSCRIPT_NATIVE and WEBSCENE_CLEARSCRIPT_RID.\n' >&2
    exit 2
  fi
  if [[ "$react_root" == "not-configured" \
        || ! -f "$react_root/react/umd/react.production.min.js" \
        || ! -f "$react_root/react-dom/umd/react-dom.production.min.js" ]]; then
    printf 'Profile native requires WEBSCENE_REACT_REPRO_ROOT with pinned React 18.2.0 UMD assets.\n' >&2
    exit 2
  fi
  react_sha256="$(
    shasum -a 256 \
      "$react_root/react/umd/react.production.min.js" \
      "$react_root/react-dom/umd/react-dom.production.min.js" \
      | shasum -a 256 \
      | awk '{print $1}'
  )"
  export WEBSCENE_CLEARSCRIPT_NATIVE="$native_path"
  export WEBSCENE_CLEARSCRIPT_RID="$rid"
  export WEBSCENE_REACT_REPRO_ROOT="$react_root"
fi

printf '{\n' > "$output_root/metadata.json"
printf '  "schemaVersion": 1,\n' >> "$output_root/metadata.json"
printf '  "profile": "%s",\n' "$profile" >> "$output_root/metadata.json"
printf '  "capturedAtUtc": "%s",\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$output_root/metadata.json"
printf '  "commit": "%s",\n' "$commit" >> "$output_root/metadata.json"
printf '  "dirty": %s,\n' "$dirty" >> "$output_root/metadata.json"
printf '  "dotnetSdk": "%s",\n' "$sdk" >> "$output_root/metadata.json"
printf '  "os": "%s",\n' "$os" >> "$output_root/metadata.json"
printf '  "architecture": "%s",\n' "$architecture" >> "$output_root/metadata.json"
printf '  "machine": "%s",\n' "$machine" >> "$output_root/metadata.json"
printf '  "cpuModel": "%s",\n' "$cpu_model" >> "$output_root/metadata.json"
printf '  "memoryBytes": "%s",\n' "$memory_bytes" >> "$output_root/metadata.json"
printf '  "runtimeTargets": ["net8.0", "net10.0"],\n' >> "$output_root/metadata.json"
printf '  "rid": "%s",\n' "$rid" >> "$output_root/metadata.json"
printf '  "nativeSha256": "%s",\n' "$native_sha256" >> "$output_root/metadata.json"
printf '  "reactFixtureVersion": "18.2.0",\n' >> "$output_root/metadata.json"
printf '  "reactFixtureSha256": "%s"\n' "$react_sha256" >> "$output_root/metadata.json"
printf '}\n' >> "$output_root/metadata.json"

run_gate() {
  local gate="$1"
  shift
  local log="$output_root/logs/${gate}.log"
  printf '\n[%s] %s\n' "$gate" "$*"
  set +e
  "$@" 2>&1 | tee "$log"
  local exit_code="${PIPESTATUS[0]}"
  set -e
  if [[ "$exit_code" -eq 0 ]]; then
    printf '%s\tpass\t0\n' "$gate" >> "$status_file"
  else
    printf '%s\tfail\t%s\n' "$gate" "$exit_code" >> "$status_file"
    return "$exit_code"
  fi
}

run_probe() {
  local probe="$1"
  shift
  run_gate "$probe" \
    dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks/JavaScript.Avalonia.Benchmarks.csproj \
      -c Release --no-build -- probe "$@"
}

check_coverage_floor() {
  local target="$1"
  local report css_report graphics_report
  report="$(find "$output_root/coverage/$target" -name coverage.cobertura.xml -print -quit)"
  css_report="$(find "$output_root/coverage/css/$target" -name coverage.cobertura.xml -print -quit)"
  graphics_report="$(find "$output_root/coverage/graphics/$target" -name coverage.cobertura.xml -print -quit)"
  if [[ -z "$report" || -z "$css_report" || -z "$graphics_report" ]]; then
    printf 'Legacy, CSS, or graphics coverage report missing for %s\n' "$target" >&2
    return 1
  fi

  local lines_covered lines_valid branches_covered branches_valid
  lines_covered=0
  lines_valid=0
  branches_covered=0
  branches_valid=0
  for current_report in "$report" "$css_report" "$graphics_report"; do
    local rates
    rates="$(sed -n '2p' "$current_report")"
    lines_covered=$((lines_covered + $(printf '%s' "$rates" | sed -E 's/.*lines-covered="([0-9]+)".*/\1/')))
    lines_valid=$((lines_valid + $(printf '%s' "$rates" | sed -E 's/.*lines-valid="([0-9]+)".*/\1/')))
    branches_covered=$((branches_covered + $(printf '%s' "$rates" | sed -E 's/.*branches-covered="([0-9]+)".*/\1/')))
    branches_valid=$((branches_valid + $(printf '%s' "$rates" | sed -E 's/.*branches-valid="([0-9]+)".*/\1/')))
  done

  local line_rate branch_rate floor_line floor_branch
  line_rate="$(awk -v covered="$lines_covered" -v valid="$lines_valid" 'BEGIN { printf "%.10f", covered / valid }')"
  branch_rate="$(awk -v covered="$branches_covered" -v valid="$branches_valid" 'BEGIN { printf "%.10f", covered / valid }')"
  floor_line="$(grep -m1 '"lineRate"' baselines/r0/coverage-floor.json | sed -E 's/.*: ([0-9.]+).*/\1/')"
  floor_branch="$(grep -m1 '"branchRate"' baselines/r0/coverage-floor.json | sed -E 's/.*: ([0-9.]+).*/\1/')"
  printf '{ "target": "%s", "lineRate": %s, "branchRate": %s, "lineFloor": %s, "branchFloor": %s, "reports": ["legacy", "css", "graphics"] }\n' \
    "$target" "$line_rate" "$branch_rate" "$floor_line" "$floor_branch" \
    > "$output_root/coverage/$target/summary.json"
  awk -v actual="$line_rate" -v floor="$floor_line" 'BEGIN { exit(actual + 0.0000001 < floor) }'
  awk -v actual="$branch_rate" -v floor="$floor_branch" 'BEGIN { exit(actual + 0.0000001 < floor) }'
}

check_core_coverage_floor() {
  local target="$1"
  local report
  report="$(find "$output_root/coverage/core/$target" -name coverage.cobertura.xml -print -quit)"
  if [[ -z "$report" ]]; then
    printf 'Core coverage report missing for %s\n' "$target" >&2
    return 1
  fi

  local rates line_rate branch_rate floor_line
  rates="$(sed -n '2p' "$report")"
  line_rate="$(printf '%s' "$rates" | sed -E 's/.*line-rate="([^"]+)".*/\1/')"
  branch_rate="$(printf '%s' "$rates" | sed -E 's/.*branch-rate="([^"]+)".*/\1/')"
  floor_line="$(grep -m1 '"newPortableChangedLineRate"' baselines/r0/coverage-floor.json | sed -E 's/.*: ([0-9.]+).*/\1/')"
  printf '{ "target": "%s", "lineRate": %s, "branchRate": %s, "lineFloor": %s }\n' \
    "$target" "$line_rate" "$branch_rate" "$floor_line" \
    > "$output_root/coverage/core/$target/summary.json"
  awk -v actual="$line_rate" -v floor="$floor_line" 'BEGIN { exit(actual + 0.0000001 < floor) }'
}

check_portable_package_coverage_floor() {
  local package="$1"
  local target="$2"
  local report
  report="$(find "$output_root/coverage/$package/$target" -name coverage.cobertura.xml -print -quit)"
  if [[ -z "$report" ]]; then
    printf '%s coverage report missing for %s\n' "$package" "$target" >&2
    return 1
  fi

  local rates line_rate branch_rate floor_line
  rates="$(sed -n '2p' "$report")"
  line_rate="$(printf '%s' "$rates" | sed -E 's/.*line-rate="([^"]+)".*/\1/')"
  branch_rate="$(printf '%s' "$rates" | sed -E 's/.*branch-rate="([^"]+)".*/\1/')"
  floor_line="$(grep -m1 '"newPortableChangedLineRate"' baselines/r0/coverage-floor.json | sed -E 's/.*: ([0-9.]+).*/\1/')"
  printf '{ "package": "%s", "target": "%s", "lineRate": %s, "branchRate": %s, "lineFloor": %s }\n' \
    "$package" "$target" "$line_rate" "$branch_rate" "$floor_line" \
    > "$output_root/coverage/$package/$target/summary.json"
  awk -v actual="$line_rate" -v floor="$floor_line" 'BEGIN { exit(actual + 0.0000001 < floor) }'
}

run_gate restore dotnet restore WebScene.sln
if [[ "$profile" == "ci" ]]; then
  run_gate release-build dotnet build WebScene.sln -c Release --no-restore \
    -p:WebSceneClearScriptNativeRequired=false -p:WebSceneClearScriptNativeRid=
else
  run_gate release-build dotnet build WebScene.sln -c Release --no-restore \
    -p:WebSceneClearScriptNativePath="$native_path" -p:WebSceneClearScriptNativeRid="$rid"
fi
run_gate core-tests-net8 dotnet test tests/WebScene.Core.Tests/WebScene.Core.Tests.csproj \
  -c Release -f net8.0 --no-build --settings coverage-core.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/core/net8.0"
run_gate core-tests-net10 dotnet test tests/WebScene.Core.Tests/WebScene.Core.Tests.csproj \
  -c Release -f net10.0 --no-build --settings coverage-core.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/core/net10.0"
run_gate core-coverage-net8 check_core_coverage_floor net8.0
run_gate core-coverage-net10 check_core_coverage_floor net10.0
run_gate javascript-tests-net8 dotnet test tests/WebScene.JavaScript.Tests/WebScene.JavaScript.Tests.csproj \
  -c Release -f net8.0 --no-build
run_gate javascript-tests-net10 dotnet test tests/WebScene.JavaScript.Tests/WebScene.JavaScript.Tests.csproj \
  -c Release -f net10.0 --no-build
run_gate dom-tests-net8 dotnet test tests/WebScene.Dom.Tests/WebScene.Dom.Tests.csproj \
  -c Release -f net8.0 --no-build
run_gate dom-tests-net10 dotnet test tests/WebScene.Dom.Tests/WebScene.Dom.Tests.csproj \
  -c Release -f net10.0 --no-build
run_gate css-tests-net8 dotnet test tests/WebScene.Css.Tests/WebScene.Css.Tests.csproj \
  -c Release -f net8.0 --no-build --settings coverage-css.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/css/net8.0"
run_gate css-tests-net10 dotnet test tests/WebScene.Css.Tests/WebScene.Css.Tests.csproj \
  -c Release -f net10.0 --no-build --settings coverage-css.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/css/net10.0"
run_gate css-coverage-net8 check_portable_package_coverage_floor css net8.0
run_gate css-coverage-net10 check_portable_package_coverage_floor css net10.0
run_gate graphics-tests-net8 dotnet test tests/WebScene.Graphics.Tests/WebScene.Graphics.Tests.csproj \
  -c Release -f net8.0 --no-build --settings coverage-graphics.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/graphics/net8.0"
run_gate graphics-tests-net10 dotnet test tests/WebScene.Graphics.Tests/WebScene.Graphics.Tests.csproj \
  -c Release -f net10.0 --no-build --settings coverage-graphics.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/graphics/net10.0"
run_gate graphics-coverage-net8 check_portable_package_coverage_floor graphics net8.0
run_gate graphics-coverage-net10 check_portable_package_coverage_floor graphics net10.0
run_gate backend-abstractions-tests-net8 dotnet test tests/WebScene.Backend.Abstractions.Tests/WebScene.Backend.Abstractions.Tests.csproj \
  -c Release -f net8.0 --no-build --settings coverage-backend-abstractions.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/backend-abstractions/net8.0"
run_gate backend-abstractions-tests-net10 dotnet test tests/WebScene.Backend.Abstractions.Tests/WebScene.Backend.Abstractions.Tests.csproj \
  -c Release -f net10.0 --no-build --settings coverage-backend-abstractions.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/backend-abstractions/net10.0"
run_gate backend-abstractions-coverage-net8 check_portable_package_coverage_floor backend-abstractions net8.0
run_gate backend-abstractions-coverage-net10 check_portable_package_coverage_floor backend-abstractions net10.0
run_gate architecture-tests dotnet test tests/WebScene.Architecture.Tests/WebScene.Architecture.Tests.csproj -c Release --no-build
run_gate backend-package-smoke dotnet run \
  --project tests/WebScene.Backend.PackageSmoke/WebScene.Backend.PackageSmoke.csproj \
  -c Release --no-build
run_gate avalonia-tests-net8 dotnet test tests/JavaScript.Avalonia.Tests/JavaScript.Avalonia.Tests.csproj \
  -c Release -f net8.0 --no-build --settings coverage.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/net8.0"
run_gate avalonia-tests-net10 dotnet test tests/JavaScript.Avalonia.Tests/JavaScript.Avalonia.Tests.csproj \
  -c Release -f net10.0 --no-build --settings coverage.runsettings \
  --collect:"XPlat Code Coverage" --results-directory "$output_root/coverage/net10.0"
run_gate coverage-net8 check_coverage_floor net8.0
run_gate coverage-net10 check_coverage_floor net10.0
run_gate wpt-list dotnet run --project tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj \
  -c Release --no-build -- --selection all --list
run_probe css-custom-properties css-custom-properties --iterations 100
run_probe css-style-storage css-style-storage --elements 2000 --variants 16 --media-resize

if [[ "$profile" == "native" ]]; then
  # The Release build has already copied the reviewed native into the runner's
  # RID output. Do not also resolve a second path to the same V8 image: macOS
  # can bind ClearScript/V8 globals across the two install identities and abort
  # during browser bootstrap. Other native gates retain the explicit path for
  # their native-presence contract checks.
  run_gate wpt-required env -u WEBSCENE_CLEARSCRIPT_NATIVE -u WEBSCENE_CLEARSCRIPT_RID \
    dotnet run --project tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj \
    -c Release --no-build -- --selection required --output "$output_root/wpt"
  run_probe v8dom v8dom
  run_probe v8canvasboundary v8canvasboundary
  run_probe v8dataset v8dataset
  run_probe v8tokens v8tokens
  run_probe v8observer v8observer
  run_probe v8textnode v8textnode
  run_probe v8attributes v8attributes
  run_probe v8react v8react
  run_probe v8reactfocus v8reactfocus
  run_probe v8iframepointer v8iframepointer
  run_probe v8domidentity v8domidentity
  run_probe v8interactioncontracts v8interactioncontracts
  run_probe v8lifecycle v8lifecycle
  run_probe v8sharedcache v8sharedcache
fi

run_gate evidence-summary python3 scripts/summarize-r0-artifacts.py "$output_root"
printf '\nR0 %s profile passed. Artifacts: %s\n' "$profile" "$output_root"

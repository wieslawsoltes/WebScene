#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

dotnet test tests/WebScene.Backend.Avalonia.Tests/WebScene.Backend.Avalonia.Tests.csproj \
  -c Release -f net10.0 --filter FullyQualifiedName~Native

arguments=(
  --repo "$repo_root"
  --output "$repo_root/TestResults/R4/renderer-matrix.json"
)
if [[ -n "${WEBSCENE_PROGPU_AVALONIA_SOURCE:-}" ]]; then
  arguments+=(--progpu-avalonia-source "$WEBSCENE_PROGPU_AVALONIA_SOURCE")
fi
python3 scripts/write-r4-renderer-matrix.py "${arguments[@]}"

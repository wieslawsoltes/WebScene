#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
npm ci --prefix "$repo_root/samples/components"
npm run build --prefix "$repo_root/samples/components"
dotnet run --project "$repo_root/samples/hosts/Avalonia/WebScene.Sdk.SampleCatalog" -- --webscene-smoke "$@"

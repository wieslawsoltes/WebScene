#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dotnet run --project "$repo_root/tests/WebScene.Sdk.SampleSmoke/WebScene.Sdk.SampleSmoke.csproj" -c Release -- "$@"

#!/bin/bash
set -euo pipefail

dotnet tool restore
dotnet docfx docfx/docfx.json --warningsAsErrors

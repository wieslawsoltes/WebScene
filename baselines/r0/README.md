# R0 protected baseline

`baseline-manifest.json` is the machine-readable inventory of required R0 gates and
their automation classification. `coverage-floor.json` records the last pre-R1
legacy result: 90 tests per target, 42.29% line coverage and 36.84% branch coverage.
The portable Core is reported separately and enforces the roadmap's 90% changed-line
minimum as a stricter total line-coverage gate.

Run the reproducible lanes from the repository root:

```sh
# Build, Core/architecture/Avalonia tests, coverage and WPT manifest integrity.
scripts/run-r0-baseline.sh --profile ci

# Add required WPT and the focused reviewed-native V8 contracts.
WEBSCENE_CLEARSCRIPT_NATIVE=/absolute/path/to/ClearScriptV8.<rid>.<ext> \
WEBSCENE_CLEARSCRIPT_RID=<rid> \
WEBSCENE_REACT_REPRO_ROOT=/absolute/path/to/react-18.2.0-node_modules \
scripts/run-r0-baseline.sh --profile native
```

Results are written below `TestResults/R0/<timestamp>-<profile>/` and are ignored by
Git. Every run contains metadata, a gate-status TSV, logs, coverage, WPT output, and
an `evidence-summary.json` index containing parsed latency, percentile, allocation,
pixel/correctness, hardware, runtime, RID, and native-hash evidence. CI uploads the
reproducible `ci` profile artifacts. Reviewed-native and
credentialed gates remain explicitly classified until durable native packages and
credentials are available to CI.

Create the pinned external React fixture without modifying the repository:

```sh
npm install --prefix /tmp/webscene-react-repro --ignore-scripts --no-package-lock \
  --no-audit --no-fund react@18.2.0 react-dom@18.2.0
export WEBSCENE_REACT_REPRO_ROOT=/tmp/webscene-react-repro/node_modules
```

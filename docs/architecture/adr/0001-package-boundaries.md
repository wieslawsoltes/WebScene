# ADR 0001: Package boundaries point inward

- **Status:** Accepted
- **Date:** 2026-07-15

## Context

`JavaScript.Avalonia` currently combines DOM, CSS, layout, rendering, scheduling and
Avalonia presentation. ClearScript is packaged separately but consumes the
Avalonia-specific host. Direct ProGPU, WPF, WinUI and Uno backends require reusable
semantics without turning Avalonia into an accidental transitive dependency.

## Decision

Dependencies point from backend and engine adapters toward portable contracts:

```text
backend / engine adapter -> runtime semantics -> WebScene.Core
```

`WebScene.Core` contains only BCL-based values and contracts. It may not reference a UI
framework or JavaScript engine. `JavaScript.Avalonia` consumes it during the seam-first
migration. Later DOM, CSS, graphics and JavaScript projects may depend on Core but may
not depend on a backend package.

Assembly creation follows proven seams, not a desired folder diagram. Public package
names beyond `WebScene.Core` remain provisional until their contracts survive Avalonia
and a second direct backend.

## Consequences

- Architecture tests reject forbidden Core references and project-reference cycles.
- Compatibility facades can preserve existing package names while dependencies move.
- Some backend-specific behavior remains in `JavaScript.Avalonia` during R1; R2/R3
  complete physical extraction.

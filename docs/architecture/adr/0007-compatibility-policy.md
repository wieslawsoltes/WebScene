# ADR 0007: Preserve public behavior through compatibility facades

- **Status:** Superseded by ADR 0013
- **Date:** 2026-07-15

## Context

This ADR records the earlier migration policy. ADR 0013 intentionally removes the
managed APIs before production because the project has no external users.

The refactor changes package boundaries while applications already construct
`AvaloniaBrowserHost`, use `AvaloniaDomElement.Control` and reference existing NuGet
identities. Combining architectural extraction with an immediate public break would
make regressions hard to distinguish from migration work.

## Decision

R0/R1 make no intentional public behavior break. Existing constructors, namespaces
and optimized commands remain. New portable properties and contracts are additive.
Future package moves retain facade packages for at least one migration cycle and use
normal semantic versioning, release notes and compile-tested migration samples.

Performance, event order, pixels, geometry, cache identity and disposal are part of
compatibility. An accepted exception requires measured evidence and an explicit
baseline/policy update.

## Consequences

- Tests exercise the old constructor and the new service/handle seams together.
- Obsolete APIs are not removed merely because a portable replacement exists.
- API-compatibility automation can be added before the first stable package split.

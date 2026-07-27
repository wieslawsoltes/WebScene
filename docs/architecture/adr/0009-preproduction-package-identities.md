# ADR 0009: Remove obsolete pre-production package identities

- **Status:** Accepted
- **Date:** 2026-07-16
- **Supersedes:** the package-facade requirement in ADR 0007

## Context

R4 originally retained a `JavaScript.Avalonia` type-forwarding package so existing
binary and XAML consumers could migrate to `WebScene.Backend.Avalonia`. WebScene is still
pre-production and has no supported external package consumers, so carrying both
identities would create documentation, testing, publishing, and dependency-graph
surface without protecting a real compatibility commitment.

## Decision

The Avalonia implementation ships only as `WebScene.Backend.Avalonia`. Projects, XAML
assembly qualifiers, package smokes, and samples reference that assembly directly.
The implementation may retain its current CLR namespace until a deliberate namespace
naming pass, but no `JavaScript.Avalonia` package or assembly is produced.

Once stable packages are published, normal semantic-versioning and migration policy
will apply. This decision does not relax behavioral compatibility gates for rendering,
input, layout, performance, cache identity, or disposal.

## Consequences

- There is one Avalonia backend package to document, test, and publish.
- Pre-production consumers must update their project/package reference and XAML
  `assembly=` qualifier immediately.
- Architecture tests reject reintroducing the obsolete facade project.
- ADR 0007 continues to describe the R0-R3 behavior-preservation policy, but its
  future-package-facade requirement no longer applies before the first stable release.

# ADR 0003: DOM identity is independent of backend objects

- **Status:** Accepted
- **Date:** 2026-07-15

## Context

DOM node identity, JavaScript expando state and listener identity must survive backend
projection changes. Using an Avalonia `Control` as the identity makes the DOM
impossible to reuse and makes reparenting or control replacement observable to JS.

## Decision

Every DOM node has a stable `WebSceneNodeId` derived from its existing
`__webSceneDomIdentity`. A framework object is carried only through
`WebSceneBackendHandle`, whose equality is native reference identity. The existing
`AvaloniaDomElement.Control` property remains temporarily for source compatibility;
portable contracts use `DomNodeId` and `BackendHandle`.

The backend owns handle creation and disposal. A handle must not be serialized,
exposed to JavaScript or used as a DOM equality key.

## Consequences

- Backend projections can be replaced without changing DOM identity.
- Existing consumers are not broken during R1.
- Later extraction must remove internal assumptions that `Control` is the node state,
  but that work is not hidden inside this ADR.

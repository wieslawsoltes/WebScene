# ADR 0004: Backends own dispatch; WebScene owns ordering

- **Status:** Accepted
- **Date:** 2026-07-15

## Context

Avalonia, WPF, WinUI, Uno and ProGPU have different dispatchers and frame lifecycles.
WebScene still needs browser-compatible task ordering, non-reentrant callbacks and
deterministic disposal with timers, rAF and observers pending.

## Decision

`IWebSceneDispatcher`, `IWebSceneFrameScheduler`, `IWebSceneClock` and
`WebSceneBackendHostBase` define the seam. Backends execute native mutations on their UI
dispatcher. WebScene chooses semantic priority and task ordering. Calls that mutate a
mounted backend verify dispatcher access.

The lifetime is:

```text
Created -> Mounted -> Unmounted -> Mounted ... -> Disposed
```

Dispose is idempotent, unmounts mounted content, cancels backend work and rejects all
later operations. Arbitrary synchronous JavaScript still runs to completion; the
dispatcher contract is not a promise of preemptive time slicing.

## Consequences

- Timers and frames can be tested with a recording dispatcher.
- Backend adapters must document cancellation and callback ownership.
- Existing Avalonia public constructors remain and create the default service adapter.

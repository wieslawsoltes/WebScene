# ADR 0012: Pooled binary native JavaScript interop

- **Status:** Experimental; generated hot path accepted for production
  hardening, ABI 3 promotion deferred
- **Date:** 2026-07-30

## Context

`TryEvaluateJson` serializes a JavaScript result, copies UTF-8 through a
temporary managed buffer, constructs a managed string, and then parses that
string into the requested type. Typed arguments travel in the other direction
through generated JavaScript source and JSON. Pooling a temporary `byte[]`
helps, but a returned .NET string and the intermediate JSON representation
cannot be returned to a pool.

The native scene bridge already establishes the useful lifetime model:
immutable native storage is leased, traversed through spans, and released.
Schema-known JavaScript calls can use the same model. Ordinary APIs materialize
only their final managed graph; policy-selected bulk APIs may keep the result
borrowed.

## Decision

Keep JSON for arbitrary evaluation, diagnostics, and compatibility tests. Add
an opt-in generated binary path with the following contracts.

### Asynchronous operation ABI

ABI v1 provides:

- `webscene_engine_begin_invoke_v1`;
- one non-user-code completion callback carrying an operation ID;
- `webscene_engine_take_invoke_result_v1`;
- `webscene_engine_cancel_invoke_v1`;
- `webscene_interop_result_release_v1`.

ABI v2 adds `webscene_engine_begin_generated_invoke_v2`. Its request contains a
versioned operation kind, static global/member names, target handle, result
mode, fixed-size tagged nodes, edge tables, and UTF-8 bytes addressed only by
validated 32-bit offsets. It never exposes a V8 or ordinary C++ object pointer.

Supported direct operations are global lookup, global invocation,
construction, property get/set, member invocation, retained-handle release,
and promise awaiting. Member invocation preserves the JavaScript receiver.

Pending promises attach V8 fulfillment/rejection handlers once. Settlement
completes the original native operation directly; there is no JavaScript
polling loop. Cancellation removes pending promise state on the engine worker.
Promise-handler metadata is V8-managed, so a timer settling after cancellation
cannot dereference freed native callback state.
Compatibility callback handles can be resolved by direct generated calls, so
callback parameters do not force the hot invocation back through JSON.

### Generated codecs

For every selected declaration whose complete argument/result shape has a
tagged codec, the `.d.ts` generator emits:

- one static `JavaScriptBinaryCallSite` per supported member, dotted global
  function/value, discovered constructor, and instance property get/set;
- readonly argument records and static reflection-free codecs;
- direct writes for strings, numbers, Booleans, nullish values, arrays,
  generated object models, and retained handles;
- direct result materialization from tagged nodes;
- a JSON fallback for unsupported shapes or invokers without the native
  transport.

The public hot method is non-`async` and returns the transport `ValueTask`
directly, avoiding a generated async state-machine allocation. Optional object
properties are omitted when absent, matching JavaScript object-presence
semantics.

A method policy may specify `borrowedName` for a binary-supported array return.
The generator then emits an additional disposable lease and stack-only array
view. Indexed values and UTF-8 spans address the immutable native arena;
`GetString()` is the explicit allocation point.

### Pooling and ownership

After warm-up, the implementation reuses:

- managed tagged request arrays and UTF-8 buffers through `ArrayPool<T>`;
- managed operation slots backed by `ManualResetValueTaskSourceCore<T>`, routed
  by operation ID through one engine-level completion bridge;
- managed decode completion sources;
- native request records and their capacity-bearing vectors;
- native operation records;
- native result records grouped into 4, 16, 64, 256 KiB, and 1 MiB retained
  size classes.

Each engine retains at most 8 MiB of result capacity. Results larger than
1 MiB are freed. A result cannot re-enter a size class until its lease is
released, so concurrent or deliberately retained views cannot be overwritten.
A result lease owns the pool state independently and may outlive engine
destruction.

Managed strings, arrays, lists, and reference DTOs returned to the caller are
not pooled. Arbitrary JavaScript strings are not interned because that would
trade allocation rate for potentially unbounded retention.

### Diagnostics

The pool ABI reports:

- outstanding result leases and their high-water mark;
- total and per-size-class retained result bytes;
- result hits, misses, and oversize frees;
- pooled request-record count, hits, misses, and oversize frees;
- active/available operation slots and slot high-water mark.

Managed finalizers recover leaked borrowed leases and increment a diagnostic
counter. Finalization is a leak guard, not normal ownership.

## Evidence

The boundary matrix compares JSON string, source-generated UTF-8 JSON, leased
UTF-8 JSON, binary materialization, and borrowed binary traversal. For
16/256/4,096 quote DTOs, binary materialization was 84-87% faster than leased
UTF-8 JSON and reduced total managed allocation by 58-63%. Borrowed traversal
allocated no payload bytes in the isolated decoder.

The generated TradingView-shaped benchmark includes request encoding, native
queueing, V8 invocation, completion, decode, and release:

| Engines | JSON update | Binary update | JSON allocation | Binary allocation |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 15.55 us | 10.98 us | 3,080 B | 174 B |
| 4 | 59.45 us | 47.33 us | 11,984 B | 364 B |
| 8 | 110.79 us | 90.65 us | 23,856 B | 618 B |

For 256 returned bars, the generated borrowed API reduced allocation from
20,704 B to 480 B on one engine and from 164,680 B to 2,888 B on eight
engines. The remaining fixed control allocation does not scale with payload.

In three alternating fresh-process four-chart trials at 60 updates/sec/chart,
the median binary path reduced process CPU by 29.5%, managed allocation by
96.7%, end working set by 13.9%, and working-set growth by 40.8%. A
600 updates/sec/chart burst caused eight Gen 0 collections on JSON and zero on
binary. Full results and reproduction commands are in
[TradingView-shaped four-chart generated binary interop](../tradingview-four-chart-binary-interop-results.md).

The actual StackWich manifest and policy on `feature/native-chart` compile
against the generator from a read-only path. Its hot
`GetWindowAsync`/`OnRealtimeUpdateAsync` calls select the direct binary route.

## Correctness gates

The current V8 and managed suites cover:

- nullish, Unicode, non-finite-number, Date, object/array, handle, and error
  encoding behavior;
- receiver preservation, retained and stale handles, and compatibility
  callback handles;
- immediate/delayed promise fulfillment, delayed rejection, and cancellation;
- malformed headers, UTF-8 ranges, edge ranges, and child indices;
- engine destruction while a taken lease remains alive;
- no reuse before release and bounded pool capacity.

With `WEBSCENE_INTEROP_STRESS=1`, 100,000 mixed operations alternate arbitrary
leased evaluation and generated tagged calls while retaining 32 leases. The
run must end with zero live result/operation state and no retained-capacity
breach.

An additional managed/native race probe starts and immediately disposes 3,200
delayed-promise operations across 100 fresh transports. Every operation is
cancelled without a fault or crash, and the run ends with zero outstanding
results and zero active native operation slots. This specifically guards
completion-bridge lifetime and promise settlement after cancellation.

## Consequences

- Schema-known realtime calls avoid all JSON and intermediate strings.
- Scalar, Boolean, void, and retained-handle calls have no payload-related
  managed allocation after warm-up.
- Materialized DTOs allocate only the final returned managed graph plus fixed
  scheduling control.
- Borrowed bulk results avoid the final payload graph until the caller
  explicitly materializes values.
- Pooling shifts risk toward lease correctness, stale generations, and bounds
  validation; these remain release gates.
- Maintaining a custom tagged format has ongoing ABI and fuzzing cost.

## Remaining ABI 3 gates

Do not declare the custom format the sole production ABI yet. Before ABI 3:

- add phase telemetry for queue, V8, encode/decode, copies, and native
  allocations;
- extend tagged codecs to the remaining policy-approved dynamic shapes,
  including wide structural unions, unresolved generics, and `JsonElement`,
  so those declarations no longer require compatibility fallback;
- replace callback take/complete JSON compatibility polling with equivalent
  direct native operations;
- add broader differential/fuzz coverage for deep/cyclic graphs, timeout, and
  randomized malformed native requests beyond the current bounds, truncated
  header, cancellation/disposal, and double-release cases;
- run the weighted end-to-end comparator against a dedicated leased UTF-8 JSON
  invocation, not only the isolated decoder comparator;
- verify no scalar, handle, or void workload regresses by more than 10%.

If the final weighted binary-versus-leased-UTF-8 gate misses 30% median latency
or 60% managed boundary allocation improvement, retain the pooled async
operation model and leased UTF-8 JSON rather than making the custom value
format the default.

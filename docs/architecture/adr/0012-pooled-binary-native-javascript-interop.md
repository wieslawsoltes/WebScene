# ADR 0012: Pooled binary native JavaScript interop

- **Status:** Accepted
- **Date:** 2026-07-30

## Context

The original native evaluation boundary serialized each JavaScript result as
JSON, copied UTF-8 through a temporary managed buffer, created a managed
`string`, and parsed that string into the requested type. Typed arguments
travelled in the other direction through generated JavaScript source and JSON.
Pooling the temporary `byte[]` reduced one allocation, but neither the returned
.NET string nor the intermediate JSON representation could be returned to a
pool.

The native scene bridge already establishes the required ownership model:
immutable native storage is leased, traversed through spans, acknowledged, and
released. Schema-known JavaScript calls and arbitrary diagnostic evaluation
can use the same model. Ordinary APIs materialize only their final managed
graph; policy-selected bulk APIs may retain a borrowed result.

This repository has not released the previous native ABI. Compatibility
exports and a fallback implementation would therefore add code, ambiguity, and
testing cost without protecting an installed consumer.

## Decision

ABI 3 is the sole native JavaScript interop ABI. The synchronous
`webscene_engine_evaluate_json` export, native JSON result path, and ABI 1/2
interop symbols are removed rather than retained as fallbacks.

### Asynchronous operation ABI

ABI 3 provides:

- `webscene_engine_begin_evaluate_v3`;
- `webscene_engine_begin_invoke_v3`;
- one non-user-code completion callback carrying an operation ID;
- `webscene_engine_take_invoke_result_v3`;
- `webscene_engine_cancel_invoke_v3`;
- `webscene_interop_result_release_v3`;
- `webscene_engine_get_interop_pool_metrics_v3`.

`begin_invoke_v3` copies a versioned request into native-owned queued work. The
request contains an operation kind, static global/member names, a target
handle, result mode, fixed-size tagged nodes, edge tables, and UTF-8 bytes
addressed only by validated 32-bit offsets. It never exposes a V8 or ordinary
C++ object pointer.

Supported direct operations are global lookup, global invocation,
construction, property get/set, member invocation, retained-handle release,
promise awaiting, callback-target/function creation, synchronous callback
factory creation, and retained function invocation. Member invocation
preserves the JavaScript receiver.

Pending promises attach V8 fulfillment/rejection handlers once. Settlement
completes the original native operation directly; there is no JavaScript
polling loop. Cancellation removes pending promise state on the engine worker.
Promise-handler metadata is V8-managed, so a timer settling after cancellation
cannot dereference released native callback state.

`begin_evaluate_v3` is retained for arbitrary scripts and diagnostics, but its
result is the same leased tagged arena. `EvaluateTextAsync` is explicitly a
tooling-edge helper: it converts the tagged result to JSON-compatible text in
managed code. It is not a native JSON transport.

### Generated codecs and routing

For every selected declaration whose complete argument/result shape has a
tagged codec, the `.d.ts` generator emits:

- one static `JavaScriptBinaryCallSite` per supported member, dotted global
  function/value, discovered constructor, and instance property get/set;
- readonly argument records and static reflection-free codecs;
- direct writes for strings, numbers, Booleans, nullish values, arrays,
  generated object models, and retained handles;
- direct result materialization from tagged nodes.

These generated methods require `IJavaScriptBinaryInvoker` and throw when ABI 3
is unavailable. They never call the generic invoker as a fallback. The public
hot method is non-`async` and returns the transport `ValueTask` directly,
avoiding a generated async state-machine allocation. Optional object
properties are omitted when absent, matching JavaScript object-presence
semantics.

The generator remains runtime-neutral and can still describe dynamic shapes
through `IJavaScriptInvoker` for non-native runtimes. The native
`NativeJavaScriptInvoker` rejects methods without a tagged codec. A shape must
gain a tagged codec before it can be selected for native use.

A method policy may specify `borrowedName` for a binary-supported array return.
The generator then emits an additional disposable lease and stack-only array
view. Indexed values and UTF-8 spans address the immutable native arena;
`GetString()` is the explicit allocation point.

### Reverse callback ABI

ABI 3 is bidirectional. Generated callback adapters whose complete signatures
have tagged codecs register native proxy objects or functions through
`CreateCallbackTarget`, `CreateCallbackFunction`, and
`CreateSynchronousFactory`. JavaScript-to-managed invocations are queued as
immutable tagged argument arenas and consumed through:

- `webscene_engine_take_callback_v3`;
- `webscene_engine_complete_callback_v3`;
- `webscene_engine_cancel_callback_v3`;
- `webscene_interop_callback_release_v3`.

Each taken callback has a process-wide generation-checked lease. Releasing the
lease returns its argument arena and callback record to their pools; completing
a Promise copies the managed completion into a pooled native completion record
before returning from the P/Invoke. Completion and release are independent, so
managed code can release a large argument arena before asynchronous user work
settles.

Native callback notification retains the empty-to-nonempty edge semantics from
commit `68ab91b`: the engine callback only wakes the managed pump and never
runs user code. The managed signal uses one reusable
`ManualResetValueTaskSourceCore<bool>` instead of allocating a `Task` or
`TaskCompletionSource` per edge. Promise settlement is delivered directly to
the retained V8 resolver; there is no JavaScript polling.

Function-valued callback arguments are retained as handles and wrapped by
generated `JavaScriptAction` codecs. Calling such an action uses
`InvokeFunction` with a tagged argument arena. Binary-compatible generated
adapters emit only this binary registration/dispatch surface—there is no JSON
adapter or runtime fallback in those generated classes.

### Pooling and ownership

After warm-up, the implementation reuses:

- managed tagged request arrays and UTF-8 buffers through `ArrayPool<T>`;
- managed operation slots backed by `ManualResetValueTaskSourceCore<T>`, routed
  by operation ID through one engine-level completion bridge;
- managed decode completion sources held in allocation-free locked stacks;
- the operation slot itself as the asynchronous ThreadPool work item, avoiding
  a completion work-item object while keeping user continuations off the
  native engine worker;
- native request records and their capacity-bearing vectors;
- native operation records;
- native callback records, tagged argument arenas, and Promise completion
  records with retained vector/string capacity;
- native result records grouped into 4, 16, 64, 256 KiB, and 1 MiB retained
  size classes.

Each engine retains at most 8 MiB of result capacity. Results larger than
1 MiB are freed. A result cannot re-enter a size class until its lease is
released, so concurrent or deliberately retained views cannot be overwritten.
A result lease owns its pool state independently and may outlive engine
destruction. Every taken result also carries a process-wide 64-bit lease
generation that the caller caches and supplies on release. A duplicate or
stale release is therefore a no-op even when the pooled result-record address
has already been reused for a newer live lease.

Managed strings, arrays, lists, and reference DTOs returned to the caller are
not pooled. Arbitrary JavaScript strings are not interned because that would
trade allocation rate for potentially unbounded retention.

### Diagnostics

The pool ABI reports:

- outstanding result leases and their high-water mark;
- total and per-size-class retained result bytes;
- result hits, misses, and oversize frees;
- pooled request-record count, hits, misses, and oversize frees;
- active/available operation slots and slot high-water mark;
- queued callbacks, taken callback leases, pending callback Promises, and
  callback queue high-water mark.

Managed finalizers recover leaked borrowed leases and increment a diagnostic
counter. Finalization is a leak guard, not normal ownership.

## Evidence

The boundary matrix compares JSON string, source-generated UTF-8 JSON, leased
UTF-8 JSON, binary materialization, and borrowed binary traversal. For
16/256/4,096 quote DTOs, binary materialization was 84-87% faster than leased
UTF-8 JSON and reduced total managed allocation by 58-63%. Borrowed traversal
allocated no payload bytes in the isolated decoder. This clears the required
30% median-latency and 60% managed-boundary-allocation gates on the weighted
bulk workload.

The generated TradingView-shaped benchmark includes request encoding, native
queueing, V8 invocation, completion, decode, and release:

| Engines | Removed JSON update | Binary update | JSON allocation | Binary allocation |
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
binary. Full measurements, retained-capacity diagnostics, and reproduction
commands are in
[TradingView-shaped four-chart generated binary interop](../tradingview-four-chart-binary-interop-results.md).

The bidirectional completion audit subsequently removed the remaining managed
control objects: `ConcurrentStack` pool nodes and
`RunContinuationsAsynchronously` ThreadPool wrappers. A fresh four-chart
60 updates/sec/chart run measured 4.61 B/call, zero collections, and zero
outstanding leases. At 600 updates/sec/chart, 24,000 calls measured 0.761
B/call. The remaining sub-byte amortized activity is ThreadPool queue-segment
and runtime bookkeeping; no request, result, decode source, operation slot, or
payload object is allocated per call after warm-up.

The actual StackWich manifest and policy compile against this generator in an
isolated worktree. Both `SW.TradingView.csproj` and the complete
`Sandwich.Desktop.csproj` graph build with zero errors. Its live datafeed and
broker bridge generate binary-only callback adapters, while hot
`GetWindowAsync`/`OnRealtimeUpdateAsync` calls select the direct binary route.

## Correctness and release gates

Automated V8 and managed suites cover:

- nullish, Unicode, non-finite-number, Date, object/array, handle, and error
  encoding behavior;
- receiver preservation and retained/stale handles;
- immediate/delayed promise fulfillment, delayed rejection, and cancellation;
- leased reverse callback arguments, void/Promise/synchronous return modes,
  retained function-valued arguments, direct function invocation, duplicate
  completion rejection, callback edge coalescing, and callback handle release;
- malformed/truncated headers, UTF-8 ranges, edge ranges, child indices, and
  property-name ranges;
- double release, stale release generations after forced address reuse, and
  stale operation IDs;
- engine destruction while a taken lease remains alive;
- no arena reuse before release and bounded pool capacity.

CTest always enables a 100,000-operation mixed stress run. It alternates
arbitrary leased evaluation and generated tagged calls while retaining 32
leases and must end with zero live result/operation state and no
retained-capacity breach.

The managed/native race probe starts and immediately disposes 3,200
delayed-promise operations across 100 fresh transports. Every operation must
complete as cancellation without a fault or crash, and the run must end with
zero outstanding results and zero active native operation slots. Both runtime
packaging scripts execute the probe against the relocated packaged native
library. Package smoke also verifies ABI 3 and asserts that the removed
`webscene_engine_evaluate_json` symbol is absent.

No scalar, handle, or void workload may regress by more than 10%. The direct
generated results above satisfy this gate.

## Consequences

- Schema-known realtime calls avoid JSON, reflection, generated source, and
  intermediate strings.
- Scalar, Boolean, void, and retained-handle calls have no payload-related
  managed allocation after warm-up.
- Materialized DTOs allocate only the final returned managed graph plus fixed
  scheduling control.
- Borrowed bulk results avoid the final payload graph until the caller
  explicitly materializes values.
- Arbitrary evaluation uses a tagged lease; producing text is an explicit
  managed diagnostic operation.
- Unsupported native declaration shapes fail explicitly instead of silently
  changing transport.
- Pooling shifts risk toward lease correctness, stale generations, and bounds
  validation, so the automated malformed, lifetime, stress, and package checks
  are release requirements.
- Maintaining a custom tagged format has ongoing ABI and fuzzing cost.

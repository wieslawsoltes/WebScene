# G02 completion scheduling and native mapping evidence

This is partial evidence for issue #24, not completion of G02 or epic #22.

On macOS ARM64 (Apple M4), the graphics-enabled V8 build passed all 12 CTests
in 13.51 seconds after adding pending-operation scheduling. After adding the
native mapping cases, all eight graphics CTests passed in 0.47 seconds.

Commands:

```sh
cmake --build artifacts/graphics-build/native-v8-enabled --parallel 4
ctest --test-dir artifacts/graphics-build/native-v8-enabled --output-on-failure
ctest --test-dir artifacts/graphics-build/native-v8-enabled -R graphics --output-on-failure
```

The completion mailbox tracks pending reservations under its existing mutex.
Publishing or cancelling a pending slot removes it from the pending count;
ready records retain their bounded slot until engine-thread delivery. An idle
Dawn instance with no pending operations does not force a periodic worker wake.
Outstanding operations retain the one-millisecond ProcessEvents polling bound.
Ready cancellation records request immediate delivery even after service close.
Future device-loss notifications must use an explicit event/wake strategy;
this does not qualify idle behavior of future persistent notification bindings.

The hardware Dawn test requests a native hardware adapter/device and submits a
real command buffer. It also maps a 4096-byte buffer without RAF or a presenter,
checks its initialized contents, then separately destroys a buffer immediately
after MapAsync and before ProcessEvents. The latter must deliver Aborted, and
both paths leave no pending or ready mailbox records. This readback is an API
mapping test, not a presentation path or a claim of zero-copy composition.

The V8 task path explicitly enters the owning context for graphics delivery and
runs a microtask checkpoint when completions are delivered. Initialization
rejects a different runtime thread. The existing runtime suite passes, but no
JavaScript WebGPU binding exists yet to prove end-to-end promise dispatch.

Still outstanding: full native device/resource management and queue integration,
navigation and promise cancellation integration, finalizer release routing,
device loss, multi-engine stress, detailed diagnostics and performance gates,
and Windows/Linux hardware evidence. No issue is closed by these results.

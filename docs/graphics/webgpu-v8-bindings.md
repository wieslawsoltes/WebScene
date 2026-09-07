# Native V8 WebGPU binding implementation

The binding contract uses the repository's existing @webref/idl 3.82.1 dependency,
installed with its package lock. `webgpu-v8-contract.json` records the WebGPU IDL
hash. This is a separate pin from the Dawn implementation. Private Dawn adapter
selection extensions must not become JavaScript descriptor members.

`v8_webgpu_adapter_options.h` implements GPURequestAdapterOptions dictionary
conversion for native V8. It supports null/undefined defaults, inherited members,
lexicographic getter access, JavaScript boolean conversion, valid power-preference
enums, and DOMString UTF-16 preservation. Getter/coercion exceptions propagate;
failed conversions leave the native descriptor unchanged. Unknown feature-level
strings are preserved for discovery to handle according to its algorithm;
featureLevel is a DOMString, not an enum in this IDL.

The native V8 runtime fixture verifies those cases, including proxies, Symbol
conversion failure, invalid power preferences, and unpaired UTF-16 surrogates.
Build and verification:

```sh
cmake --build artifacts/graphics-build/native-v8-enabled --target webscene_graphics_v8_runtime_tests -j8
ctest --test-dir artifacts/graphics-build/native-v8-enabled -R '^webscene_graphics_v8_runtime_tests$' --output-on-failure
```

Both pass against the current macOS arm64 V8 15.3.10/Dawn SDK build. This converter
is ready for the discovery binding but is not yet called by navigator.gpu.
Secure-origin exposure, asynchronous adapter/device promises, wrapper identity,
resources, pipelines and command encoding remain incomplete. No browser WebGPU
capability is advertised by this change, and no JavaScript triangle has run yet.

## Dawn adapter request mapping

`webgpu_adapter_options.h` separates the converted browser dictionary from V8 and
maps it to Dawn request options. Core/compatibility levels, power preference and
forceFallbackAdapter are preserved. Unknown feature-level strings produce no
request, matching the null-adapter outcome in the
[WebGPU requestAdapter algorithm](https://gpuweb.github.io/gpuweb/#dom-gpu-requestadapter).
XR-compatible requests currently produce no request because WebScene has no WebXR
device integration. Backend selection is a separate host argument; it is never
read from a JavaScript dictionary or exposed as a browser extension.

The Dawn event test now uses mapped browser defaults for real asynchronous adapter
discovery, followed by its device/resource/completion tests. Both
`webscene_graphics_dawn_event_tests` and `webscene_graphics_v8_runtime_tests` pass
on the macOS arm64 hardware build. Mapping tests also cover fallback/preferences,
compatibility, unknown levels and unsupported XR. This verifies native selection
plumbing, not the still-unimplemented navigator.gpu promise/wrapper exposure.

## Asynchronous adapter promise delivery

`v8_webgpu_adapter_request` bridges native Dawn discovery to a V8 promise. It
reserves the bounded graphics completion mailbox, retains native callback results
separately from V8 handles, and resolves only during engine-thread delivery for
the matching operation, owner, isolate and realm. Driver callbacks capture no V8
handles. Unsupported requests or admission backpressure resolve null; native
failure/cancellation also resolves null. Destruction abandons native storage so a
late callback cannot publish an adapter into a discarded binding object.

The runtime fixture now starts real asynchronous Dawn discovery from an active
V8 graphics callback and observes its JavaScript promise continuation while RAF
is paused. A separate request is cancelled through the mailbox; its promise
resolves null without invoking the wrapper factory. The test waits for physical
native callback retirement as well as logical cancellation. The enabled macOS
`webscene_graphics_v8_runtime_tests` passes.

This is the promise-delivery component, not complete navigator.gpu exposure. The
fixture uses a diagnostic JavaScript wrapper while retaining the real native
adapter. The standards GPUAdapter object/prototype/feature/limit registry and
secure-origin navigator integration still need implementation. Navigation-wide
binding teardown must route through the existing cancellation lifecycle before
releasing the realm; that full integration remains unqualified.

### Service-owned adapters

Discovery completion now adopts the native adapter into the graphics service's
bounded, typed generational table. The V8 fixture retains a service handle, not
an untracked native adapter. Borrowed adapter access is confined to an engine
execution scope; an asynchronous device request must take its own native
reference within that scope. Releasing a wrapper handle therefore does not
invalidate a native reference already retained by an in-flight request.

Adapter destruction and service closure are rejected during borrowed access.
Finalizers can enqueue value-only adapter release commands, with stale duplicate
releases harmless. Service closure releases adapters before closing Dawn's event
service. Hardware tests exercise foreign/stale handles, slot reuse, scope guards,
null rejection, capacity exhaustion, deferred release and reference survival.
This remains internal ownership plumbing; public GPUAdapter bindings are pending.

### Wrapper creation failures

Adapter completion consumes its resolver before invoking the wrapper factory.
The factory may return a `MaybeLocal<Value>`: a caught JavaScript exception rejects
the adapter promise with that same value, while an empty result without an
exception or a native `std::exception` rejects with a generic Error. Native error
text is not exposed. Terminated execution is not converted to an ordinary error.
Factories remain responsible for rolling back any partially registered resources.

The macOS V8 hardware fixture verifies a throwing native factory and a JavaScript
exception sentinel, observes both rejections, and rejects duplicate completion.
Handlers are installed before returning to the graphics pump because spontaneous
Dawn discovery can complete within that same dispatch batch. The targeted runtime
CTest passes; this does not qualify the still-pending public GPUAdapter bindings.

### Standards feature mapping

`generate-webgpu-features.mjs` reads the pinned WebGPU IDL and checks its SHA256
before generating `webgpu_feature_names.h`. All 23 GPUFeatureName values have
explicit Dawn spellings. Unknown names and native-only shared-texture/fence
features have no mapping. `npm run check --prefix tools/webidl-v8-bindings` now
checks this generated catalog as well as the existing DOM bindings.

The native helper can query either an adapter or a device, using HasFeature on
that exact object. The macOS hardware test checks the full standards mapping
against actual adapter support, private-feature exclusion, null rejection, and
the default device's enabled subset. It passes. This is the feature translation
layer for discovery and device descriptors; GPUSupportedFeatures setlike objects,
SameObject identity and public capability exposure still require integration.

### GPUBufferDescriptor conversion

`v8_webgpu_buffer_descriptor.h` converts the pinned buffer dictionary atomically.
It reads inherited label before mappedAtCreation, size and usage, preserves
property/coercion exceptions, replaces lone label surrogates for USVString, and
preserves embedded NUL bytes. Required size/usage members are checked before
native allocation. EnforceRange uses truncation followed by bounds checking:
GPUSize64 accepts at most 2^53−1 and usage at most 2^32−1. BigInt, Symbol,
non-finite values and out-of-range integers produce TypeError. These rules follow
[WebIDL integer conversion](https://webidl.spec.whatwg.org/#abstract-opdef-converttoint).

The V8 runtime test covers defaults, inherited properties, coercion/getter order,
USVString encoding, both integer boundaries, invalid inputs, atomic failure and
exception identity. It passes on the macOS enabled build. Usage combinations,
mapped alignment and device limits are not dictionary conversion errors: those
remain native WebGPU validation. The converter is not yet connected to a public
GPUDevice.createBuffer binding; buffer allocation and JS resource wrappers still
require integration.

### Native buffer descriptor translation

The converted buffer data now lives in a V8-independent descriptor. Its native
translator explicitly maps the ten pinned GPUBufferUsage flags instead of casting
JavaScript flags into Dawn's larger enum. Unknown bits (including Dawn's private
TexelBuffer bit at 0x400) return an invalid translation. The forthcoming public
createBuffer binding must route this through WebGPU validation/error-buffer
semantics; it must not reinterpret it as a WebIDL exception or successful buffer.

Native descriptors borrow label bytes with an explicit length, preserving embedded
NUL. Translation from temporaries is deleted to prevent an immediately dangling
label. The hardware test creates a mapped Dawn buffer from a translated descriptor,
checks size/usage/map state and mapped range, then unmaps and destroys it. It also
checks all 32 individual usage bits and unknown-bit combinations. Both the Dawn
hardware and V8 runtime CTests pass on macOS. This does not yet connect JavaScript
createBuffer to native allocation or qualify the public validation-error path.

### Device-owned buffer handles

Each native Dawn device now owns a bounded generational buffer table (default
capacity 1024). Internal native-descriptor creation returns a typed handle; scoped
access rejects a foreign device's table and stale generations. This is an internal
entry point, not the public createBuffer binding or its error-object policy.

`destroy_buffer` invokes Dawn Destroy while preserving the wrapper handle and
metadata; repeated destruction remains valid. `release_buffer` removes only the
wrapper's native reference. The service supplies a value-only deferred release
command for the existing finalizer release channel. Queued/native users retain
independent Dawn references; wrapper release does not destroy their buffer and
does not imply GPU completion. Device teardown clears its remaining buffer table.

The macOS hardware test verifies cross-device rejection, stale/reused handles,
borrowed-access destruction/release/close guards, asynchronous finalizer delivery,
mapped-buffer survival after wrapper release, and metadata after repeated Destroy.
The Dawn hardware and V8 runtime CTests pass. Public buffer wrappers, mapping
ArrayBuffer detachment, device-loss browser semantics and error scopes still need
integration and qualification.

### Buffer admission and regression checks

Buffer creation now checks table capacity before invoking Dawn, avoiding native
allocation churn when wrapper capacity is exhausted. The owner-thread admission
check treats deferred GPU resources as occupied until completion and excludes
slots whose generations cannot be reused. It is not a cross-thread reservation;
no reentrant table mutation is allowed between the check and insertion.

The hardware test configures a one-buffer device table, verifies saturation and
successful reuse after release. Resource-table tests cover zero capacity and a
slot remaining unavailable until its completion serial retires. After this change,
resource, Dawn hardware, graphics service and V8 runtime tests pass:

```sh
ctest --test-dir artifacts/graphics-build/native-v8-enabled -R '^webscene_graphics_(resource|dawn_event|service|v8_runtime)_tests$' --output-on-failure
```

Before this admission change, rebuilding the enabled graphics targets and running
`ctest --test-dir artifacts/graphics-build/native-v8-enabled -L graphics --output-on-failure`
also passed all five tests, including ANGLE ES2 and ES3 on macOS. These are native
regression results, not evidence of running an unchanged WebGPU application.

### First native-backed V8 buffer objects

`v8_webgpu_buffers.h` provides an internal realm-owned wrapper factory with native
size/usage getters and destroy callbacks. Its bounded registry reserves finalizer
release capacity before exposing a wrapper. Receiver branding, device/buffer
handles and realm identity are checked; duplicate wrapping of a handle is rejected.
Registry teardown invalidates live objects before dropping their native entry,
so retained JavaScript references cannot dereference freed binding state. GC
callbacks only publish value-only release tickets.

The V8 runtime fixture now requests a real Dawn device asynchronously, creates a
buffer on the engine, wraps it, and runs JavaScript metadata/brand/repeated-destroy
assertions. It also checks duplicate ownership, wrong-realm wrapping and retained
object calls after registry teardown, followed by device retirement before the
queued wrapper release. The rebuilt macOS runtime CTest passes.

This internal factory is deliberately not installed as a public GPUBuffer
constructor. Label, mapState, mapAsync/getMappedRange/unmap, mapping detachment,
public createBuffer/error-object integration and complete WebIDL prototypes remain
unfinished. Existing fixtures exercise explicit registry teardown; GC reclamation
and full navigation lifecycle for this specific registry still need qualification.

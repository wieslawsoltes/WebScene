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

### Live buffer mapState

The internal buffer prototype now has a branded read-only mapState getter backed
by Dawn GetMapState. It translates Unmapped, Pending and Mapped to the pinned
browser strings and rejects unknown native states. The V8 hardware fixture creates
a mapped-at-creation buffer, observes `mapped` in JavaScript, destroys it and
observes `unmapped` while size remains available. Wrong-receiver checks include
this getter. The rebuilt macOS runtime CTest passes. Pending-state behavior still
needs end-to-end qualification when mapAsync is connected; mapped-range exposure
and ArrayBuffer detachment remain unfinished.

### Buffer labels

The internal buffer prototype now implements a label getter/setter. Wrapper creation
accepts the already-converted descriptor label; subsequent writes perform USVString
conversion, pass explicit-length bytes to Dawn SetLabel and retain the converted
value for reads. Branding is checked before conversion and receiver state is
reacquired afterwards, because user conversion code may invalidate binding state.

The rebuilt macOS V8 test passes default-label, embedded NUL/lone-surrogate,
Symbol rejection, thrown conversion identity, unchanged value after failure,
wrong-receiver-before-coercion, and reentrant Destroy during ToString assertions.
Labels remain readable after buffer destruction. Full registry teardown during
coercion is guarded by reacquisition but is not yet independently exercised; public
GPUDevice/createBuffer wiring and mapping APIs remain outstanding.

### Buffer wrapper garbage collection qualification

The macOS V8 fixture now retains a buffer through a global JavaScript reference,
drops that reference, and explicitly triggers collection. Native live-buffer
counts remain unchanged during GC and reach zero only after the engine pumps its
release channel. The one-slot wrapper registry rejects overflow without taking
ownership, then successfully reuses the collected slot. Registry teardown with a
new live wrapper followed by device retirement also drains its delayed release
without stale-handle failure. The rebuilt V8 runtime CTest passes.

This closes the previously untested GC-release path for the internal buffer
registry. It does not prove mapped ArrayBuffer lifetime/detachment, in-flight GPU
submission behavior for JavaScript buffers, or full navigation integration.

### Mapped ArrayBuffer lifetime primitive

`v8_webgpu_mapped_ranges.h` creates ArrayBuffers directly over an already mapped
native memory region. Each view privately retains its buffer wrapper; the tracker
holds weak view handles so it does not make dead mappings permanently reachable.
Native storage remains owned by the buffer, never by a V8 backing-store deleter.
A private detach key prevents outside detachment. Engine-side detachment clears
all reachable views and their private owner references before native unmapping.

The tracker enforces offset/size alignment, mapping bounds and non-overlap; empty
ranges occupy no bytes. Range reservations last until unmap even if views become
unreachable. Its owner must detach it before native destruction or unmapping.
It is not yet connected to the public getMappedRange/unmap entry points.

The rebuilt macOS V8 test checks that ArrayBuffer pointers equal Dawn's mapped
addresses, JavaScript writes reach native mapped bytes, invalid ranges fail,
foreign detachment is rejected, and authorized detachment empties typed-array
views. This proves the tested mapped-memory path without a staging copy; it does
not qualify mapAsync, device-loss detachment or full public error semantics.

### JavaScript mapped-at-creation buffer operations

The internal buffer prototype now implements getMappedRange and unmap. Wrapping
a mapped-at-creation buffer attaches a full-range tracker to the wrapper entry.
getMappedRange applies GPUSize64 conversion and defaults, rechecks mapping state
after user coercion, and returns a direct mapped ArrayBuffer. Alignment, bounds,
overlap and absent-mapping errors use the caller-supplied trusted DOMException
constructor with OperationError. Registry setup must receive that constructor
from trusted runtime initialization, not discover it during an API call.

unmap and destroy detach all tracked views before invoking Dawn. Registry teardown
also detaches before invalidating wrapper entries or queuing release. The rebuilt
macOS V8 fixture exercises JavaScript writes to native memory, omitted sizes,
WebIDL errors, OperationError cases, repeated unmap, and retained ArrayBuffer and
typed-array detachment on both unmap and destroy. User numeric coercion that calls
unmap is revalidated before creating a view.

This is working internal mapped-at-creation behavior, not complete mapping support.
mapAsync and selected subrange attachment, device-loss/device-destroy detachment,
full navigation integration and public WebGPU discovery/resource exposure remain
unfinished. The runtime CTest passes; those missing paths remain unqualified.

### Device-wide mapped-view detachment hook

The buffer registry now supplies detach_device for the binding's device lifecycle.
It matches the service and complete device handle, detaches matching mappings,
and leaves wrapper/native device destruction to the caller. Calls are idempotent
and stale device generations cannot affect a live mapping. It requires the owning
isolate scope and must run before native device destruction or before JavaScript
resumes after device-loss delivery.

The rebuilt macOS V8 test creates a mapped buffer and JavaScript range, confirms
that a stale generation does not detach it, detaches with the live device handle,
then destroys the native device and disposes the wrapper registry. It passes.
This establishes the lifecycle hook and explicit ordering, not automatic device-loss
integration: the public device binding and runtime loss delivery still must call it.

### Reachable mapped views retain native buffers

The macOS V8 lifetime fixture now drops the ordinary buffer reference while
keeping only its mapped ArrayBuffer reachable. After forced collection and an
engine pump, the native buffer and wrapper release registration remain live; the
ArrayBuffer retains its byte length and accepts a JavaScript write. Dropping that
view and collecting again leaves native storage intact during GC, then the next
engine pump releases the buffer and registration. The collected registry slot is
subsequently reused by the existing teardown test. The rebuilt runtime CTest passes.

This verifies the mapped view's private owner edge as well as deferred native
release. Asynchronous mapping, device loss and application-level WebGPU exposure
remain outside this test's coverage.

### Asynchronous map promise bridge

`v8_webgpu_map_request.h` reserves a completion slot before issuing Dawn MapAsync.
Driver callbacks retain only native buffer/mailbox state. The engine owns V8
resolver and wrapper references; successful delivery attaches the selected mapping
before resolving undefined. Cancellation unmaps and rejects AbortError immediately,
then consumes the eventual native completion without attaching memory or resolving
again. Validation failure status maps to OperationError. Requests are one-shot and
completion identity/realm are checked; disposal of a still-pending request aborts
native mapping, while hosts must explicitly cancel before disposal if its promise
remains observable in a live realm.

The rebuilt macOS V8 fixture maps a real 16-byte subrange at offset 8 and observes
its promise continuation while RAF is paused. A separate request is cancelled;
its promise rejects with AbortError and late callback retirement is consumed.
Duplicate completions are rejected. The runtime CTest passes.

This bridge is not yet installed as GPUBuffer.mapAsync. That binding still needs
argument conversion, early rejection/validation semantics, pending mapState,
selected-range attachment, and unmap/destroy/device-loss cancellation wiring.
Admission saturation and mapping attachment failure policies also remain internal
behavior requiring qualification against the complete browser binding.

### Internal GPUBuffer.mapAsync integration

The internal buffer prototype now exposes mapAsync and routes native completion
records through its registry. WebIDL conversion failures return rejected promises;
pending state is tracked on the content side until the engine handles completion.
Successful requests attach their selected range before resolving. unmap, destroy
and the device-detachment hook cancel pending promises, while a separate request
list keeps canceled native operations identifiable until their callbacks retire.
An immediate remap therefore cannot be completed by the old canceled operation.

WRITE views reference Dawn mapped memory directly. READ mappings copy the selected
bytes into mutable CPU storage so JavaScript changes are discarded on unmap; a
second read verifies the native buffer remained unchanged. This explicit buffer
read operation does not add pixel readback to ordinary canvas composition.
Mapping allocation failures reject RangeError and unmap native storage.

The rebuilt macOS runtime test now invokes mapAsync from JavaScript and passes
pending/mapped/unmapped state, selected WRITE range, cancel-and-immediate-remap,
READ data and discarded writes, promise-based BigInt conversion rejection, method
arity, and injected mapping-allocation failure checks. Native callbacks still do
not enter V8 directly.

This remains an internal factory. Public navigator/device creation, complete
error-scope/uncaptured-error integration, loss delivery, all invalid-descriptor
and saturation cases, trusted DOMException-construction reentrancy, and complete
WebIDL interface exposure remain unfinished or unqualified. No app-level WebGPU
readiness or conformance claim follows from this test.

### Mapping rejection remains settled if exception construction fails

The map request now clears pending resolver ownership before invoking its exception
factory, while a local handle retains the wrapper through construction. A thrown
JavaScript exception becomes the rejection reason instead of leaving a pending
request. Repeated cancel remains inert, and completions for requests that were
never started are ignored.

The rebuilt macOS runtime test deliberately supplies a throwing exception factory
for a canceled native map. It verifies thrown-value identity in the promise
rejection, cleared pending state, repeat cancellation and late callback retirement.
The ordinary prototype cancellation/remap tests continue to verify AbortError.
Broader public error-scope and device-loss integration remains outstanding.

### JavaScript descriptor to owned buffer creation

The buffer registry's create entry point now combines GPUBufferDescriptor
conversion, native allocation and wrapper registration. It rejects a misaligned
mapped-at-creation size with RangeError before allocating. Unknown browser usage
bits enter Dawn's validation/error-buffer path via invalid usage None, never a
private native usage extension. Wrapper metadata preserves the original browser
usage, size and label even for error buffers. Metadata getters now read retained
wrapper values rather than dispatching native calls.

Creation releases its native handle if wrapping fails and propagates an exception
instead of silently returning no object. The macOS V8 fixture now creates its WRITE
mapping buffers from JavaScript descriptors via this entry point. It also verifies
misaligned mapped size without allocation, an invalid-usage error buffer with
preserved metadata and a usable mapped-at-creation region, actual Dawn validation
scope delivery, and native handle rollback when release registration is saturated.
The rebuilt runtime CTest passes and explicitly requires callback retirement.

This is the native entry point for the forthcoming GPUDevice.createBuffer method;
it does not install a public device object. Full device creation, error-scope/event
exposure, native allocation failure/admission policy and loss integration still
require implementation or qualification.

### Internal V8 device objects

`v8_webgpu_devices.h` introduces bounded, branded device wrappers with createBuffer
and destroy methods. createBuffer calls the descriptor/allocation/wrapper path and
privately links each returned buffer to its parent device wrapper. destroy first
cancels/detaches that device's buffer mappings, then invokes native Dawn destruction;
repeated destruction is inert. Explicit native device table release remains a
separate teardown step. Registry disposal invalidates device and buffer receivers
before cancellation can construct JavaScript exceptions, and queues native release.

The rebuilt macOS runtime test now calls device.createBuffer from JavaScript,
checks descriptors/metadata and receiver/arity errors, writes a mapped view,
destroys the device twice, and verifies view detachment, retained buffer metadata
and rejected mapped access. Calls through a retained device object after registry
disposal also fail safely. The runtime CTest passes.

No global GPUDevice or navigator.gpu is installed by this factory. Adapter requestDevice,
queue/resources beyond buffers, capabilities, labels, device.lost/error events,
full WebIDL prototypes and automatic loss/navigation integration remain unfinished.
The parent-device GC edge and asynchronous completion routing through this new
device registry still need dedicated qualification.

### Pending map cancellation through the device registry

The macOS runtime fixture now creates a buffer through the internal device object,
starts mapAsync from JavaScript, and destroys the device while the map is pending.
It verifies immediate unmapped state and AbortError rejection. Completion routing
passes through the device registry after the native device handle has been released;
the late callback retires safely before registry disposal. A retained device object
is then checked for rejected access after disposal. The rebuilt runtime CTest passes
and explicitly waits for native retirement rather than treating rejection as GPU
completion.

This qualifies the tested device-destroy path. Physical device loss, parent-device
GC retention, public adapter discovery and complete device/queue capabilities remain
unfinished or require dedicated verification.

### Device labels

Internal device wrappers now retain an initial converted descriptor label and
implement the label getter/setter. Writes use USVString conversion, preserve
embedded NUL via explicit byte lengths, recheck the receiver after user coercion,
and forward the label to Dawn. Failed conversion leaves the prior label intact.
The rebuilt macOS V8 test passes default, surrogate/NUL, Symbol, throwing-conversion
and post-destroy label assertions. Public discovery and remaining device/queue
capabilities are still unfinished.

### Device enabled-feature snapshot

The internal device factory now exposes a SameObject `features` snapshot backed
by a traced, inaccessible V8 Set. Only the generated standard GPUFeatureName
mapping is admitted, and contents come from the device's enabled features rather
than the adapter's available capabilities. The read-only setlike surface includes
size, has, keys/values/iteration, entries and forEach. Iterators use built-ins
captured when the factory is initialized in the trusted realm; this factory must
be constructed before running untrusted scripts. Snapshot objects retain their
backing data independently of native device ownership and factory lifetime.

The macOS V8 runtime test compares every standard feature against the actual Dawn
device, checks count, identity, iteration, callback arguments, DOMString coercion,
receiver checks, mutation rejection and exception propagation, and uses a retained
snapshot after device registry disposal. This remains an internal binding; public
navigator.gpu, requestDevice and rendering commands are still required before a
JavaScript WebGPU app can render.

### Native requestDevice promise delivery

`v8_webgpu_device_request` now connects an already-validated native device
request to a V8 promise. The Dawn callback captures only synchronized native
result storage and a completion ticket. The engine thread consumes that result,
invokes the device wrapper factory, and settles the realm-owned promise. Native
request failure rejects with an OperationError constructed through the captured
trusted DOMException constructor; native diagnostics are not leaked to scripts.
A released bridge abandons its result, and a late callback retains no V8 handles.
Completions are consumed before invoking wrapper/exception factories. Duplicate
and wrong-owner completions cannot create another wrapper, and wrong-realm
completion does not consume the pending request.

The macOS V8 runtime fixture now uses this bridge to create the actual Dawn
device used by its buffer tests. It verifies promise fulfillment, duplicate and
wrong-realm handling, and an actual Dawn rejection for an impossible requested
maxBufferSize, including the OperationError rejection object. This fixture still
uses a diagnostic result object at the promise factory boundary. Browser device
descriptor conversion, adapter validity/consumption, public GPUAdapter wiring,
device-loss promise integration and full teardown/admission qualification remain
unfinished; this is not public requestDevice exposure.

### Device descriptor WebIDL conversion

The pinned GPUDeviceDescriptor converter now reads inherited label, defaultQueue,
requiredFeatures and requiredLimits in WebIDL dictionary order. Device and queue
labels use USVString conversion. Feature sequences accept iterable objects,
cache the iterator's next method, convert every item through the standard feature
enum, and retain duplicate entries for subsequent validation. Private native
feature names are rejected. Required-limit records snapshot all own keys and
then inspect each property's descriptor before reading its value; getters can
change later properties. DOMString keys preserve unpaired UTF-16 surrogates,
undefined values remain distinct from zero, and GPUSize64 uses EnforceRange.
Conversion commits the output only after all members succeed.

These behaviors follow the WebIDL [sequence conversion](https://webidl.spec.whatwg.org/#es-sequence)
and [record conversion](https://webidl.spec.whatwg.org/#es-record) algorithms.
The macOS V8 runtime tests verify defaults, getter/proxy order, mutation during
record conversion, inherited/non-enumerable exclusions, feature iteration and
coercion, USVString versus DOMString, integer boundaries, exception identity, and
no partially committed output after failure. The converter does not yet map
required limits to Dawn or enforce adapter capabilities. Those validation steps
and the public requestDevice entry point remain required.

### Native required-limit mapping

A generated catalog maps every GPUSupportedLimits attribute in the pinned IDL to
its typed Dawn member. The four per-vertex/per-fragment storage limits map to
Dawn's CompatibilityModeLimits chain; the remaining limits map to base Limits.
The generator verifies the IDL hash, participates in npm generate/check, and
requires an explicit review for new attribute types. Unknown/native-only names
are not admitted by the catalog.

`prepare_webgpu_required_limits` validates defined values against the supplied
adapter limits, including the reversed comparison and power-of-two constraint
for alignment limits. Unknown names with undefined values are ignored. Missing
native capability values, narrowing overflow and Dawn's undefined sentinels are
rejected; none silently becomes an omitted request. Output is committed only
on success. The caller owns and chains any compatibility structures used for a
native request. Failure is intended to become requestDevice OperationError;
public promise wiring remains unfinished.

The macOS runtime tests exercise every catalog member and integer width, missing
names, limits above adapter capacity, invalid alignments, undefined entries and
atomic failure. Its real Dawn device request now uses a maxBufferSize requirement
validated against that actual adapter. The test and npm generation checks pass.
This does not establish complete adapter capability reporting or compatibility
mode qualification, nor does it expose public requestDevice.

### Prepared native device requests

The device descriptor data model is now independent of V8.
`webgpu_prepared_device_descriptor` validates required features against the actual
adapter before consumed-adapter and required-limit validation. Its result
classifies unsupported features separately (for TypeError) from operation
failures (for OperationError). Private native features are rejected even when
passed directly to the internal helper. Required features are deduplicated into
a set while preserving first-occurrence order. Adapter compatibility limits are
queried and chained only when the request includes those defined limits.

The prepared object owns device/queue labels, features, limits and optional
compatibility-chain storage. It cannot move or copy; its native descriptor is a
borrowed view used while the owner remains alive. The macOS runtime fixture now
converts an actual JavaScript device descriptor, prepares it against the real
adapter, and uses that descriptor for Dawn RequestDevice. Tests verify every
standard feature against the actual adapter, duplicate removal, consumed-state
and feature-error precedence, unknown limits, and source-label mutation not
changing prepared storage. The runtime test passed.

This helper accepts consumed state from its caller; it does not implement the
public adapter state machine. Public requestDevice still needs that state,
error-to-promise integration, trusted realm initialization and wrapper lifetime
ownership. Compatibility mode and full device-loss behavior remain unqualified.

### Checked request promise entry

The internal request bridge's `start_checked` entry combines JavaScript descriptor
conversion, adapter-state reacquisition, native preparation and asynchronous Dawn
request submission. Its ownership callback runs after all descriptor getters and
coercions, allowing the eventual adapter registry to reject invalidated or
consumed receivers instead of carrying a stale native-entry pointer across user
code. Conversion exceptions reject the returned promise with the original thrown
value. Unsupported features reject with TypeError, and limit/consumed failures
with OperationError. Rejected preparation does not reserve native completion
storage or call Dawn RequestDevice.

The macOS runtime device fixture now uses this checked entry for successful native
creation. Tests verify rejected promise types for unknown features and limits,
getter exception identity, unchanged completion occupancy after rejection, and
adapter consumed state changed by a descriptor getter. Runtime CTest passes.
This remains an internal entry: GPUAdapter prototype installation, receiver brand
handling, real consumed/expired state and public exposure are still pending.

### Adapter wrapper identity and lifetime

The realm-owned adapter registry now wraps generation-checked graphics-service
adapter handles. It rejects duplicate wrappers within the registry and foreign
realms, reserves deferred-release storage before exposure, and exposes a stable
read-only feature snapshot from the native adapter. Weak callbacks publish only
release commands; registry disposal invalidates native receiver access and queues
release instead of calling GPU APIs in GC. Retained feature snapshots remain
usable after wrapper registry disposal.

The macOS runtime fixture verifies feature membership against every standard
Dawn feature, repeated snapshot identity, duplicate/foreign-realm rejection,
receiver invalidation after disposal, snapshot survival and actual deferred
native-handle reclamation. It uses an independent graphics-service fixture
because the existing buffer test deliberately occupies every release slot.
Runtime CTest passes. This registry does not yet dispatch requestDevice or install
navigator.gpu; those remain necessary for public discovery.

### Adapter requestDevice dispatch

Internal adapter wrappers now expose requestDevice with optional-descriptor
arity. The callback delegates conversion and preparation to the checked request
bridge, reacquires adapter ownership after user-controlled conversion, and retains
the adapter wrapper while native completion is pending. Successful completion
adopts the native device into the graphics service and creates a device wrapper
through a caller-owned device registry. That registry must outlive the adapter
registry. Device wrapper registration failure rolls back the adopted handle.
The original device label survives asynchronous completion. Wrong receivers
reject with TypeError; a repeated request after admission rejects OperationError.

The macOS runtime fixture discovers a fresh adapter, calls requestDevice from
JavaScript, waits for its actual native completion, and uses the returned device
to create, map, write, unmap and destroy a buffer. It checks the label, method
arity, repeated-request/wrong-receiver rejection types and deferred reclamation
of both adapter and device handles. Runtime CTest passes. A previously consumed
native adapter is not reused to simulate fresh discovery.

This is still an internal registry. navigator.gpu exposure, unified prototypes,
limits/adapterInfo, expired/lost-device semantics and pending-request teardown
qualification remain unfinished. The registry currently consumes an adapter at
native request admission; resource-failure/lost-device behavior needs the full
adapter state machine before public exposure. No WebGPU rendering sample is yet
claimed to work.

### Pending device-request cancellation

The request bridge now supports explicit owner-realm cancellation. It clears its
resolver before constructing the rejection, abandons any native result, and
leaves the native callback responsible for retiring its completion ticket. Late
completion cannot invoke a device wrapper factory. Adapter registry teardown
invalidates every receiver before cancelling outstanding requests, then releases
its request keep-alives and queues native adapter release.

The macOS runtime test cancels an admitted native request with an impossible
limit, verifies wrong-realm rejection and idempotence, observes a rejected promise,
and waits for the real native callback to retire without wrapping. Runtime CTest
passes. This tests cancellation of the native failure path; successful native
creation racing registry teardown still needs targeted qualification. The host
teardown policy currently rejects OperationError, while full browser expired/
lost-device behavior remains separate and unfinished.

### Successful creation racing adapter-registry teardown

The macOS runtime test now discovers a second fresh adapter, starts requestDevice,
and disposes its registry while the promise is pending, before delivering the
native completion. It requires an actual successful Dawn device completion rather
than accepting a native error as evidence. The original cancellation rejection
remains unchanged, no orphan device is adopted into the graphics service, and
the completion ticket and adapter handle are reclaimed. Runtime CTest passed.
This closes the specific successful-completion teardown gap above; it does not
qualify all browser device-loss or navigation behavior.

### Discovery object connected to adapter/device registries

An internal realm-owned discovery object now exposes requestAdapter and routes
real Dawn discovery completion into the adapter registry. The host retains the
controller; service and adapter/device registries outlive it. It rechecks its
receiver after option conversion, rejects conversion/receiver errors through
promises, resolves unavailable adapter requests to null, and rolls back native
adapter adoption if wrapper construction fails. Controller disposal invalidates
its receiver and cancels pending discovery promises before native callbacks
retire independently. No navigator property or secure-context claim is installed.

The macOS runtime test exercises discovery through JavaScript, obtains the real
adapter wrapper, invokes requestDevice, and performs createBuffer/getMappedRange/
unmap/destroy on the returned native device. It verifies unsupported feature-level
null results, invalid-option rejection and final native handle reclamation.
Runtime CTest passes. Public exposure policy, complete GPU prototypes and
capabilities, canvas configuration and rendering commands remain unfinished.

### Preferred canvas format

The internal GPU discovery object now implements getPreferredCanvasFormat as a
synchronous, receiver-checked method. The host selects BGRA8Unorm or RGBA8Unorm;
other formats are rejected during controller construction. The default is
bgra8unorm, matching the current macOS IOSurface submission path. No adapter
request, allocation or readback is required to report this preference.
The macOS runtime test checks default/explicit format selection, method arity,
wrong-receiver TypeError and invalid host configuration. Runtime CTest passes.
GPUCanvasContext configuration/current-texture and ordinary presentation wiring
remain unfinished; this method alone does not enable canvas rendering.

### WGSL language capability snapshot

The internal discovery object now exposes SameObject wgslLanguageFeatures. An
explicit allowlist of 13 language extensions from the [W3C WGSL specification](https://www.w3.org/TR/WGSL/#language-extensions-sec),
reviewed 2026-09-07, is filtered against the actual Dawn instance's
HasWGSLLanguageFeature results. Chromium testing/printing extensions are excluded;
additional draft/native extension names require a standards review before adding
them. Enable extensions such as f16 are not language-feature entries.

The read-only set implementation now has distinct GPU and WGSL brands and
prototype tags while sharing its iteration/coercion implementation. Discovery
construction belongs in trusted realm initialization, before scripts can modify
Set built-ins. The macOS runtime test compares every allowlisted feature with
Dawn, checks count, iteration, identity, prototype tag and private-name exclusion,
and rejects borrowing GPU feature-set methods onto a WGSL feature object.
Runtime CTest passed. Shader compilation and public navigator exposure remain
unfinished; reporting capabilities does not establish shader execution coverage.

### Adapter and device limits snapshots

Internal adapter/device wrappers now expose SameObject limits. The GPUSupportedLimits
factory builds read-only prototype getters for every pinned catalog entry and
stores values in traced JavaScript storage, independent of native lifetimes.
Adapter values come from that adapter; device values come from that device's
GetLimits result, including compatibility-chain values. An unavailable native
sentinel aborts snapshot construction instead of being advertised as a capacity.

The macOS runtime test compares every exposed adapter/device value with the
corresponding native source, verifies stable identity, strict-mode mutation
rejection, receiver branding and the prototype tag, and reads retained limits
after device-registry disposal. Runtime CTest passes. This provides capability
inspection in the internal bindings; public exposure, rendering commands and
full browser conformance remain unfinished.

### Native adapter-information extraction

A native adapter-information value now owns vendor, architecture, device,
description and subgroup metadata independently of Dawn's temporary AdapterInfo
allocation. Identifier fields must satisfy the WebGPU normalized-identifier
pattern; malformed/unknown values become empty strings rather than exposing
nonconforming driver names. Subgroup limits use the actual adapter when the
subgroups feature is supported, or the specified 4/128 defaults otherwise.

Fallback classification is an explicit discovery-policy input. Inspection of the
pinned Dawn source found that Vulkan's forceFallbackAdapter filter identifies
SwiftShader via vendor/device IDs, while Metal rejects forced fallback. CPU
adapter type alone is therefore not used as a fallback test. The macOS runtime
test verifies copied description, subgroup values and identifier handling against
a real adapter; CTest passes. JavaScript info/adapterInfo wrappers and propagation
of authoritative discovery fallback state remain unfinished.

### JavaScript adapter information

Internal adapters now expose SameObject info and devices expose SameObject
adapterInfo. Both use the originating adapter's metadata, including subgroup
support, so disabling optional device features does not change adapter information.
Read-only prototype getters access traced snapshot data that survives registry
teardown. Private backend type and numeric vendor/device IDs are not exposed.

Fallback classification now mirrors the pinned Dawn source: Vulkan recognizes
Google SwiftShader (vendor 0x1ae0, device 0xc0de), matching BackendVk.cpp and
src/dawn/gpu_info.json; Metal, D3D, GL and Null backends reject forced fallback.
Unknown backend classifications fail explicitly. This supersedes the earlier
caller-supplied fallback flag. Updating Dawn requires rechecking this mapping.

The macOS runtime test verifies native strings, adapter/device agreement across
all seven fields, stable identity, read-only/brand behavior, private-field
exclusion, fallback classification cases and retained information after teardown.
The expanded allocations also exposed an existing GC test's unrooted probe being
collected before its explicit GC step; that probe is now rooted until the test
intentionally drops it. Runtime CTest passed. Hardware Windows/Linux fallback
qualification, public exposure and rendering remain unfinished.

### Owned native shader modules

Dawn devices now own a bounded shader-module resource table with independent
capacity, generation-checked handles, scoped borrowing and explicit reference
release. Capacity is checked before native creation. Device close retires shader
references alongside buffers; close/release during an active shader borrow is
rejected. Invalid WGSL still produces Dawn's error shader-module object, with
validation delivered through the native error scope.

The macOS Dawn event tests verify capacity, stale/reused generations, borrow
lifetime guards and actual error-scope outcomes for valid and invalid WGSL through
the owned-module path. Dawn event and V8 runtime CTests both pass. Native shader
ownership is now ready for the V8 module registry; JavaScript shader descriptors,
compilation information and rendering pipeline/command bindings remain pending.

### Deferred shader-module release

Graphics-service release commands now carry generation-checked device and shader
handles without native pointers. Finalizers can publish these commands through
the existing bounded release channel. The engine resolves and retires the native
shader reference; stale device/module handles are ignored.
The macOS Dawn event test publishes releases from another thread, verifies no
inline native retirement, and verifies a stale release cannot remove a replacement
module in a reused table slot. Release storage returns to zero after draining.
Dawn event and V8 runtime CTests pass. V8 shader-module wrappers remain pending.

### Internal V8 shader-module wrappers

A bounded realm-owned shader registry now wraps native device/module handles,
rejects duplicate ownership, and keeps a private JavaScript edge to the parent
device wrapper. Label getters retain content-side metadata; setters perform
USVString conversion, recheck the receiver after coercion and call Dawn SetLabel.
The prototype has the GPUShaderModule tag. Weak callbacks and registry disposal
publish deferred module-release commands; disposal invalidates native receivers
first. Neither path calls GPU APIs from GC.

The macOS runtime test wraps an actual compiled shader, verifies initial label,
NUL/lone-surrogate conversion, Symbol rejection, throwing coercion, receiver tag/
brand, duplicate and foreign-realm checks, and deferred rather than inline native
release on disposal. Runtime CTest passes. Device createShaderModule descriptor
conversion/dispatch and getCompilationInfo are still pending; the internal
wrapper is not a complete exposed GPUShaderModule implementation.


### Internal device shader creation

The internal GPUDevice prototype now dispatches createShaderModule to Dawn's
owned shader table and returns a GPUShaderModule wrapper retaining its parent
device. The descriptor converter reads label, required WGSL code and iterable
compilation hints in WebIDL order, preserves getter/coercion exceptions, converts
USVStrings, and commits converted state only after success. Dispatch rechecks the
device receiver after conversion, passes explicit string lengths to Dawn, and
releases the native module if wrapper adoption fails.

Compilation hints are converted but not forwarded as optimization hints. The
converter has a pipeline-layout resolver hook; genuine GPUPipelineLayout wrapper
recognition remains pending with that interface. The current internal surface
accepts omitted/auto layouts and exposes no pipeline-layout objects. This is not
complete public WebGPU exposure or shader compilation diagnostics support.

The macOS V8 runtime test creates a module from JavaScript using real WGSL,
checks module branding and label updates, required arguments, wrong receivers,
getter exception identity and native module ownership count. Converter coverage
includes iterable hints, dictionary order, invalid enums and atomic failure.
Render pipelines, command submission and normal canvas presentation remain the
next rendering integration work; this test does not draw an app or triangle.


### Owned native render pipelines

Dawn devices now own render pipelines in a separately bounded, generational
resource table. Creation checks capacity before calling Dawn, borrowed pipeline
scopes prevent release/device close, and close retires the table. A value-only
deferred release command carries both device and pipeline generations for future
V8 wrapper collection.

The macOS Dawn event test compiles vertex/fragment WGSL, creates an owned render
pipeline, encodes a triangle into an offscreen 4x4 RGBA8 texture, releases the
table's pipeline reference, and submits the retained command buffer. A completed
native validation error scope reports no error. It also verifies capacity,
borrowed-scope guards and stale-handle rejection. Dawn event and V8 runtime tests
pass. This is native command/lifetime validation, not a pixel assertion, physical
presentation check or JavaScript render-pipeline implementation. Those remain
required before claiming app rendering.


### Internal render-pipeline wrappers and native interface conversion

Shader modules and render pipelines now share a typed labeled-resource registry.
Each specialization has a distinct V8 brand and prototype, bounded wrapper
storage, private parent-device reachability, and deferred generational release.
The render-pipeline specialization wraps owned Dawn pipeline handles. Checked
native-reference conversion invokes no JavaScript and retains the native object
through later descriptor conversion; wrong-interface and forged objects are
rejected before native access. Native cross-device compatibility remains Dawn's
validation responsibility.

The macOS V8 runtime test constructs a real render pipeline using a shader
reference obtained from its wrapper, verifies exact native identity, rejects
shader/pipeline cross-brand conversion and a plain forged object, checks labels
and prototype tags, and verifies disposal invalidates wrapper access without
inline GPU release. Existing JavaScript shader-creation tests still pass through
the shared implementation. Public createRenderPipeline descriptor dispatch,
getBindGroupLayout, async pipeline creation and rendering from JavaScript are
still pending; this internal wrapper is not a complete GPURenderPipeline API.


### Programmable pipeline-stage conversion

The shared GPUProgrammableStage converter now reads constants, entryPoint and
required module in WebIDL order. It retains the native shader reference, keeps
omitted entry points distinct from empty strings, and converts constants as a
record of USVString keys to finite doubles. Record conversion snapshots own keys,
checks current enumerability before each value, propagates exceptions, and
replaces values when distinct UTF-16 keys normalize to the same USVString. The
native constant-entry view borrows stable converted key storage and explicitly
disallows access through a temporary descriptor.

MacOS V8 runtime coverage verifies defaults, shader identity, numeric coercion,
USVString replacement/NUL handling, normalized-key collisions, proxy property
order, property deletion during enumeration, invalid values and atomic failure.
The test passes against real shader wrappers. Derived vertex-buffer and fragment
color-target conversion plus complete render-pipeline dispatch remain pending.


### Render-state dictionaries and pinned enum mappings

Primitive, multisample, blend-component/blend-state and stencil-face converters
now apply WebIDL member order, defaults, boolean coercion and EnforceRange
unsigned integers before committing native state. Invalid enums and non-finite/
out-of-range integers throw TypeError; application getter exceptions propagate.
Native semantic constraints, such as supported sample counts, remain Dawn's
validation responsibility.

Eleven render-related enum catalogs are generated from the pinned IDL and an
explicit native spelling map. Generation rejects changed IDL membership/order;
compilation verifies the mappings against the pinned Dawn headers. The npm
check/generate scripts include this catalog. Browser-unknown native enum values
are excluded. macOS V8 runtime tests cover defaults, conversion order, blend and
stencil values, numeric truncation/bounds, exception identity and atomic failure.
Both the runtime test and generator check pass. Vertex layouts, depth/color
state and complete pipeline dispatch remain pending.


### Depth/stencil and color-target conversion

Depth/stencil conversion now preserves omitted depthWriteEnabled/depthCompare,
converts signed depth bias with EnforceRange, rejects non-finite float values,
and applies stencil defaults/masks in WebIDL order. Color-target conversion
requires a standard texture format, owns optional blend state and preserves
invalid write-mask bits for native validation. Its native view borrows blend
storage and cannot be obtained from a temporary descriptor.

The macOS V8 runtime test verifies omitted versus explicit depth settings,
fractional bias truncation, signed bounds, float overflow/infinity rejection,
stencil updates, target defaults, blend storage identity and atomic failures.
The runtime test passes. Vertex layouts and final pipeline descriptor assembly/
dispatch still remain before JavaScript can create a render pipeline.


### Vertex-stage and buffer-layout conversion

Vertex-state conversion now extends the shared programmable stage with iterable,
nullable buffer slots. Buffer layouts own their attribute vectors, preserve
stride/step mode and expose borrowed native views. Attribute conversion requires
format, offset and shaderLocation in WebIDL order. GPUSize64 and required
GPUIndex32 values use EnforceRange before native validation. The sequence helper
captures iterator/next once, checks iterator results and reads done before value.

The macOS V8 runtime test covers default empty buffers, Set iteration, null and
undefined slots, instance layouts, native attribute views, required fields,
integer bounds, exact property access order and atomic conversion failure. It
passes. Complete render-pipeline descriptor assembly and device dispatch remain
pending; no JavaScript draw or presentation is claimed by these tests.


### Internal createRenderPipeline dispatch

The internal GPUDevice prototype now accepts render-pipeline descriptors and
calls Dawn's owned render-pipeline creation. Conversion follows inherited label/
layout then depthStencil, fragment, multisample, primitive and vertex order.
Fragment targets preserve null slots. Native assembly keeps nested arrays,
constant keys, entry-point strings and blend pointers alive for the call. It
rechecks the device after JavaScript conversion and rolls back native ownership
if wrapper adoption fails. Returned pipeline wrappers retain their parent device.

The exposed internal layout branch is currently auto. Descriptor conversion has
an explicit-layout resolver hook, but GPUPipelineLayout objects and their creation
remain unimplemented. Async pipeline creation and getBindGroupLayout also remain
pending. This is not complete WebGPU or navigator.gpu exposure.

The macOS runtime test now creates a shader module and render pipeline from
JavaScript, checks labels/branding, rejects invalid calls, and verifies native
module/pipeline counts. Descriptor tests check property order and native nested
storage after moving converted data, including null buffer/color slots. The
bounded eight-ticket fixture drains disposed resources before its next unrelated
registration; collection still only publishes deferred releases. Runtime CTest
passes. Drawing commands, textures and normal canvas presentation remain needed
for a visible JavaScript triangle; this test does not verify rendered pixels.


### Owned native textures and views

Dawn devices now own separately bounded texture and texture-view tables with
full generational handles. Creation checks capacity before native allocation;
borrowed scopes guard release, destruction and device close. Texture destroy
invalidates storage while preserving API handles and retained views. Reference
release does not call Destroy. Deferred commands carry device/resource identity
for future wrapper collection, and device close retires both tables.

The macOS native draw test now obtains its target from these owned tables. It
releases table references before drawing through a retained view and verifies
native validation succeeds. Capacity, stale handles, borrowed-scope guards and
repeated destroy preserving handles are covered. Dawn event and V8 runtime tests
pass. JavaScript texture/view descriptor conversion and wrappers remain pending;
these tests do not establish JavaScript rendering or pixel correctness.


### Texture descriptor conversion

GPUTextureDescriptor conversion now handles required extent/format/usage,
dimension, mip/sample counts, optional binding view dimension and iterable view
formats in WebIDL order. Extent union conversion retrieves the iterator method
once; dictionary extents preserve width requirements and height/layer defaults.
Sequence shape validity is recorded after consuming the sequence, allowing the
remaining dictionary conversion to finish before native use rejects invalid
shapes. Native views retain label, format arrays and the binding-dimension chain
through a scoped call. Unknown browser usage bits become invalid native usage,
preventing accidental exposure of host-only flags.

MacOS runtime tests cover extent defaults, single iterator acquisition, exact
property order, binding/view-format storage, late shape validation, range errors
and atomic failure. Runtime and generator checks pass. Texture/view wrappers and
createTexture dispatch remain pending.


### Texture-view conversion and wrappers

A typed texture-view registry now uses the shared labeled-resource lifetime
machinery, with its own interface brand and retained native-reference conversion.
The descriptor converter preserves optional mip/layer counts, format/dimension,
aspect, base offsets, usage and DOMString swizzle. Nonidentity swizzles use Dawn's
component-swizzle chain. Explicit UINT_MAX counts cannot silently become native
unspecified sentinels; these and unknown usage bits produce an invalid native
dimension for subsequent Dawn validation. Invalid swizzle characters likewise
remain native validation errors, not WebIDL enum exceptions.

MacOS runtime coverage verifies descriptor defaults, explicit sentinel
preservation, invalid inputs and DOMString surrogate preservation. A real native
view is wrapped, retained and disposed; native table removal happens only after
engine release draining, and cross-interface conversion is rejected. Runtime and
generator checks pass. Public createTexture/createView dispatch remains pending;
full native error-scope qualification of translated invalid view descriptors is
also still required before conformance claims.


### Internal JavaScript texture creation and views

GPUDevice.createTexture now converts descriptors, validates extent shape after
conversion, creates an owned Dawn texture and wraps it with a retained parent
device. GPUTexture exposes readonly descriptor metadata, mutable label,
createView and destroy. Metadata remains available after destruction. View
creation rechecks the receiver after descriptor conversion, creates an owned
native view, and retains the texture wrapper through the view's private parent
edge. Wrapper adoption failures release the newly created native reference.

Texture wrappers extend the shared typed resource registry; their views reuse
its existing deferred release implementation. The registry now records its
owning factory for specialized callbacks. Native resource destruction remains
on the engine thread, and collection still publishes only release commands.

The macOS V8 runtime test obtains a device through adapter.requestDevice,
creates a texture/view in JavaScript, verifies dimensions/format/usage/defaults,
labels and readonly metadata, rejects wrong receivers and invalid extent shapes,
and checks metadata after repeated destroy. Runtime CTest passes. Command
encoding, queue submission and ordinary GPU canvas presentation remain pending;
this test does not draw from JavaScript or prove pixel output.


### Owned native command resources

Dawn devices now own bounded generational tables for command encoders, render
passes and command buffers. Capacity is checked before create/begin/finish;
borrowed command scopes prevent release or device close. Deferred release
commands carry complete device/resource identity. Native WebGPU recording-state
validation remains Dawn's responsibility rather than being replaced with silent
host no-ops.

The macOS triangle-command test now uses these tables to begin a render pass,
set its pipeline, draw, end, finish and submit. It releases the pass and encoder
before submission, then releases the submitted command-buffer reference. Tests
verify capacity rejection, borrowed scopes, stale-handle rejection and empty
tables, alongside the completed native validation scope. Dawn event and V8
runtime tests pass. JavaScript command methods, queue submission and canvas
presentation remain pending; this is not evidence of JavaScript pixel output.


### Internal JavaScript command encoders and finish

GPUDevice.createCommandEncoder and GPUCommandEncoder.finish now convert their
optional label dictionaries, recheck receivers after user coercion, invoke the
owned Dawn command-resource APIs and adopt typed wrappers. Command buffers retain
an encoder/device reachability chain. Failed wrapper adoption releases the newly
created native reference. The shared label converter preserves USVString and
exception semantics; native recording-state validation remains in Dawn.

The macOS V8 runtime test creates and finishes encoders from JavaScript, checks
brands, labels and defaults, rejects invalid dictionaries/receivers, and verifies
a throwing finish label getter leaves the encoder usable for a valid finish.
Runtime CTest passes. These are empty command buffers: JavaScript render-pass
recording, queue submission and canvas presentation remain pending.


### Internal JavaScript render-pass recording

Command encoders now expose beginRenderPass; render-pass wrappers expose
setPipeline, draw and end. Descriptors convert nullable color attachments,
texture-or-view references, clear colors, optional depth/stencil state and draw
limits. Texture shorthand resolves to an implicit native view after conversion.
GPUColor shape validation happens after dictionary conversion, before native pass
creation. Draw arguments use GPUSize32 conversion and recheck ownership after
coercion. Native recording-state validation remains Dawn's responsibility.

Query-set wrappers remain unexposed, so supplied query objects are rejected
rather than ignored. Occlusion/timestamp support, indexed/indirect draws,
vertex/index buffers, bind groups and the rest of the render-pass API remain
required. Explicit depth-slice sentinel values stay invalid when translated to
Dawn; full native error-scope qualification remains part of conformance work.

The macOS runtime test now creates a texture, shader and pipeline in JavaScript,
records a triangle pass, ends it and finishes a command buffer. It checks invalid
clear shapes leave the encoder usable, invalid draw/interface calls reject, and
labels/brands persist. Descriptor tests cover color conversion, dictionary order,
nullable attachments, native storage and atomic failure. Runtime and generator
checks pass. The recorded command buffer is not submitted in this test: queue
submission, pixel verification and ordinary canvas presentation remain pending.


### First verified JavaScript WebGPU pixels (macOS)

The internal device now exposes a SameObject GPUQueue with label and submit.
Queue submission converts an iterable of genuine command-buffer wrappers before
calling Dawn, retaining native references through conversion and rechecking the
queue afterward. The traced queue/device relationship preserves lifetime without
an independent GPU reference in GC callbacks. Default-queue labels survive
asynchronous requestDevice completion. Internal device-reference conversion is
branded and scoped through the owning graphics service.

The macOS V8 runtime test creates a 4x2 RGBA8 texture, WGSL shader module,
auto-layout render pipeline, command encoder and render pass entirely through
JavaScript wrappers. It records a full-target red triangle, ends and finishes,
then submits through device.queue.submit(new Set([commands])). Native diagnostic
code subsequently copies the target to a mapped readback buffer and checks all
eight pixels equal RGBA [255,0,0,255]. The mapping must complete successfully
within five seconds; missing completion or any pixel mismatch fails. This proves
actual offscreen JavaScript-driven GPU rendering, not only wrapper construction.

The runtime test passes on this macOS host. It also checks queue identity,
default/mutable labels, invalid receivers/buffers and iterator exception identity.
Diagnostic texture readback is used only to assert pixels; it is not introduced
into the composition path. navigator.gpu installation, GPUCanvasContext,
ordinary scene image consumption and window presentation are still pending.
Queue completion promises, writes and other APIs also remain incomplete. This
result does not establish Kestrel readiness or cross-platform qualification.


### Canvas configuration conversion

GPUCanvasConfiguration conversion now preserves device identity, required format,
usage, view formats, alpha mode, color space and tone mapping in WebIDL order.
PredefinedColorSpace comes from the pinned @webref/idl html.idl, including its
linear sRGB/P3 values. Requested modes are preserved for subsequent capability
negotiation; conversion does not claim the current presenter supports every mode.
The device resolver retains a genuine native GPUDevice and rejects forged
interfaces. Conversion commits only on success and propagates getter exceptions.

The macOS runtime test covers defaults, full member order, iterable view formats,
requested HDR/color modes, invalid values and atomic failure. It passes together
with the existing JavaScript triangle pixel assertion. GPUCanvasContext remains
unconnected. The existing IOSurface submission helper owns recording/submission
inside one callback; integration must instead span current-texture acquisition,
application queue submissions, shared-image end access and versioned publication
at the frame boundary, without introducing CPU texture readback.


### Shared-frame texture expiration

Shared Dawn images can now expire their texture object after EndAccess. Expiry
is idempotent, refuses active/failed access and prevents later BeginAccess.
IOSurface submission expires the per-frame texture before marking the image
ready; queued native work retains the resources it needs. This closes the route
by which an old canvas texture alias could otherwise submit writes after the
underlying image allocation becomes eligible for reuse.

The macOS fixture retains an old texture alias, attempts to clear through it
after frame completion, and requires a native validation error. Creating a view
alone does not establish invalid submission, so the test records and submits the
attempted write. The real Ganesh window probe then verifies the original pixels:
32 rendered frames, one import, completed GPU retirement, zero explicit transport
copies and four diagnostic readbacks. Physical scanout is not verified. Evidence:
`evidence/ganesh-host/frame-texture-expiry.json`. This validates native frame
expiration/presentation; JavaScript GPUCanvasContext remains unconnected.


### Handoff after application-owned submission

The IOSurface producer can now publish work already submitted on the device
queue. This path never resubmits application commands. It verifies device/native
allocation identity, ends shared access, expires the frame texture, requires
initialized contents, and waits for queue completion plus its own handoff
validation scope before exposing a versioned image. Its error scope covers only
host handoff operations, not application recording or the application's scope
stack. Publication backpressure still retains the producer through completion.

The macOS fixture now records and submits independently, then invokes this
handoff. It rejects a foreign allocation before consuming its frame, rejects
reopening an expired texture, and verifies an expired alias cannot submit writes.
The real Ganesh window pixel probe passes: 32 frames, one import, completed GPU
retirement, zero explicit transport copies and four diagnostic readbacks.
Evidence is in `evidence/ganesh-host/submitted-frame-handoff.json`; physical
scanout is not verified. This supplies a native canvas frame-boundary primitive.
JavaScript GPUCanvasContext, host device-feature provisioning and ordinary scene
consumption remain integration work, not completed by this native fixture.


### Imported canvas texture adoption

The native device texture table now accepts an already-created/imported texture
through a host-only adoption method. It verifies the supplied source device,
rejects null textures and checks capacity before retaining the object. The host
importer is responsible for supplying the texture's true source device; this is
not a JavaScript API. Adoption preserves exact native identity and performs no
texture allocation or pixel transfer.

The macOS Dawn test verifies missing-source rejection, identity preservation,
capacity handling, view creation and repeated destruction of an adopted texture.
Dawn event and V8 runtime tests pass, including the JavaScript offscreen pixel
test. GPUCanvasContext must still connect this boundary to its current texture
and shared-frame publication lifecycle.


### V8 adoption of imported canvas textures

The internal device bridge can now adopt an imported native texture directly
into its V8 texture registry. It validates the device brand, checks descriptor
metadata against native dimensions/format/usage/mips/samples, adopts the native
reference and rolls back table ownership if wrapping fails. The wrapper retains
its device and uses the ordinary texture/view implementation. No second texture
allocation or pixel transfer occurs during adoption.

The macOS runtime test rejects mismatched metadata, verifies exact native
identity, and accesses metadata/createView from JavaScript on an adopted texture.
Runtime CTest passes, including the offscreen triangle pixel assertion. The host
canvas controller still needs to provide/cache this wrapper from getCurrentTexture
and connect frame expiration/publication to ordinary scene consumption.


### Canvas texture descriptor construction

Canvas-specific content validation now restricts the base format to bgra8unorm,
rgba8unorm or rgba16float and rejects TRANSIENT_ATTACHMENT usage. The canvas
texture descriptor builder copies bitmap width/height, format, usage and view
formats, preserving zero dimensions and explicit usage rather than adding render
or host-only flags. Other texture members retain standard defaults.

MacOS runtime tests cover all three base formats, rejected sRGB base formats,
transient usage, zero dimensions and unchanged explicit usage/defaults. Runtime
CTest passes. Required-format feature checks must precede this validation;
native texture validation and presenter capability negotiation still follow.
These helpers do not expose GPUCanvasContext or establish presenter support for
all required canvas color/HDR modes.

### Internal GPUCanvasContext and rendered current texture

The host-owned V8 canvas context now implements configure, unconfigure,
getConfiguration, getCurrentTexture and the canvas getter. It retains the
configured device, returns fresh configuration snapshots and caches one texture
wrapper until the host expires the frame, resizes the bitmap or unconfigures.
Host acquisition feeds the existing native-texture adoption bridge, with no pixel
transfer in the context. The controller cannot be copied or moved because V8
receivers carry its address; released receivers are invalidated.

The macOS runtime fixture supplies a diagnostic native-texture allocator. Its
JavaScript triangle now targets getCurrentTexture, submits through GPUQueue and
verifies all eight RGBA pixels using explicit diagnostic readback. Coverage also
checks unconfigured InvalidStateError, snapshot isolation, current-texture
identity, one acquisition per frame, retirement, resized dimensions and released
receiver rejection.

This is an internal context milestone, not public canvas integration. Ordinary
HTMLCanvasElement.getContext, shared IOSurface acquisition/publication and normal
scene consumption remain unconnected. Presenter feature/color validation,
invalid-texture and allocation-failure semantics, and host retirement failure
handling still need qualification before exposing this as a complete API.

### Submitted-frame discard retirement

The IOSurface submission handoff accepts an explicit non-presenting retirement
for canvas resize/unconfigure. This path ends shared access and expires the
WebGPU texture, keeps the producer allocation alive through queue completion,
and then cancels the pool reservation without creating a scene lease. Its
terminal status is `discarded`; initialization is required for presentation but
not for a discarded image. Validation and completion failures remain failures.

The native Metal fixture exercises both an uninitialized texture and a texture
with an application-submitted clear. Both reach discarded status, produce no
scene image, wake once and leave no occupied pool slot after completion. The
Ganesh window pixel probe still reports 32 frames, one import, completed GPU
retirement, zero explicit transport copies and four diagnostic readbacks.
Evidence: `evidence/ganesh-host/submitted-frame-discard.json`. This establishes
native discard behavior; the JavaScript canvas host is not connected to it yet.

### Reusable IOSurface canvas texture import

`import_dawn_iosurface_canvas_texture` provides the native acquisition operation
for the upcoming canvas host. It imports the reserved pool frame using the
caller's texture descriptor, rejects mismatched bitmap dimensions/base format,
and retains the IOSurface independently through the shared-image owner. It does
not allocate another pixel store, copy pixels or submit commands. BeginAccess
and completion-based retirement remain explicit caller responsibilities.

Both the native recording helper and application-submission fixture now use this
operation. The macOS fixture verifies descriptor mismatch rejection, requested
usage and allocation identity, then exercises presentation and discard. The
Ganesh window pixel test passes with 32 frames, one import, zero explicit
transport copies and four diagnostic readbacks. This importer currently targets
the negotiated BGRA8 pool; it does not establish RGBA16/HDR support or connect
ordinary JavaScript canvas acquisition by itself.

### Native canvas frame provider

`dawn_iosurface_canvas_host` now combines the IOSurface pool, descriptor-preserving
import and submitted-frame retirement. It permits one active current texture,
reserves one of three pending retirement slots before acquisition, rejects foreign
texture retirement and exposes completed retained images through `take_ready`.
Failed/discarded/consumed retirements are pruned on the engine thread. The caller
must retire the active texture before destroying the provider; pending GPU work
retains its allocation independently. Scene publication must still filter canvas
generations and content serials when consuming completed images.

The Metal fixture verifies duplicate acquisition and foreign retirement
rejection, discarded-frame cleanup, then renders and returns the provider's own
image to the Ganesh window probe. The probe passes with 32 frames, one import,
completed GPU retirement, zero explicit transport copies and four diagnostic
readbacks, including presentation after provider destruction. Ordinary V8 canvas
host callbacks and scene scheduling still need to be connected to this provider.

### V8 canvas to IOSurface provider bridge

`make_iosurface_webgpu_canvas_host` connects the V8 canvas controller's validate,
acquire and retire callbacks to the native IOSurface provider. The document
supplies canvas/generation/content and producer timeline identities; the bridge
adds bitmap dimensions, alpha mode, color space and orientation. Current
negotiation is BGRA8, sRGB and standard tone mapping, with native IOSurface and
MTLSharedEvent capabilities required on the device. Acquisition uses the original
canvas texture descriptor and retirement hands already-submitted work to the
provider without resubmission or pixel transport.

A new macOS V8 runtime test requests a fresh Metal adapter/device with those
private native features, wraps the device, configures the internal canvas and
clears its current texture from JavaScript through GPUQueue.submit. The resulting
retained IOSurface image preserves canvas 123, generation 7, content serial 1 and
4x2 bitmap dimensions. After producer completion, explicit diagnostic IOSurface
inspection verifies all eight opaque red pixels; this CPU inspection is test-only.
The private sharing feature names remain absent from the JavaScript feature set.
Unconfigure/consumer completion leave the provider idle with no occupied image.
Runtime CTest passes after rebuilding the target.

The test deliberately provisions private device features internally. Ordinary
requestDevice still needs host capability provisioning, and HTMLCanvasElement
getContext, automatic frame expiration and normal scene publication remain
unconnected. This evidence is a JavaScript-to-shared-image milestone, not a
normal WebScene application or Kestrel pass.

### Host sharing capabilities on JavaScript device requests

The adapter registry accepts an internal canvas-interop policy, defaulting to
none. With IOSurface selected, native device preparation appends the two required
sharing capabilities only after browser feature/limit/consumed validation. Missing
host capabilities reject preparation; JavaScript cannot select this policy or
request the native features through requiredFeatures. The existing browser
feature snapshots continue filtering native-only capabilities.

The shared-canvas runtime test now obtains its device through JavaScript
adapter.requestDevice() with this host policy, instead of making a raw native
device request. It verifies both native features are enabled, their names remain
hidden in JavaScript, and an explicit private-feature request rejects TypeError
without consuming the adapter. The subsequent normal request renders the shared
canvas and all eight diagnostic pixels pass. Runtime CTest passes after rebuild.
The ordinary runtime still needs to install the registry with the negotiated host
policy together with navigator.gpu, getContext and scene/frame scheduling.

### Realm ownership and completion routing

`v8_webgpu_realm` owns discovery, adapter and device registries in dependency
order, routes completion records to the owning registry, and destroys them in
reverse dependency order. It carries host interop/backend/preferred-format
selection into the registries. It does not install globals or decide secure
context exposure; canvas controllers must retire before this owner is destroyed,
and the graphics service must outlive it.

The IOSurface runtime fixture now begins with JavaScript GPU.requestAdapter(),
then adapter.requestDevice(), and uses getPreferredCanvasFormat() to configure
its internal canvas. No raw native adapter/device request remains in this test.
It verifies the shared-image pixels and checks that a retained discovery wrapper
rejects calls after realm destruction. Rebuilt runtime CTest passes. This provides
the ownership and dispatch unit for runtime installation; navigator exposure and
normal DOM canvas/frame scheduling are still outstanding.

### Runtime Navigator installation entry point

`v8_dom_runtime::install_webgpu` is a host-only opt-in before application scripts.
It requires the host's secure-context decision and negotiated interop policy,
installs a stable navigator.gpu getter, and routes realm completions through the
runtime's existing graphics task pump. A denied decision returns false without
initializing graphics. Runtime ownership retires the realm before its graphics
service and enters the owning isolate during direct destruction.

Navigation explicitly removes the installed GPU property because this runtime
retains its Navigator object across loads. The next document requires another
host installation decision. Explicit shutdown invalidates retained GPU receivers.
Tests cover denied exposure, stable identity, actual JavaScript adapter/device
creation through the ordinary runtime task pump, navigation/removal/reinstall,
shutdown and direct destruction with a live realm. Rebuilt runtime CTest passes;
the V8-disabled native library also builds successfully.

This is an integration entry point, not automatic enablement in desktop hosts.
The host's origin/secure-context computation and full Navigator/WebIDL semantics
still need qualification. HTMLCanvasElement.getContext, automatic GPU frame
expiration and normal scene presentation are still pending; installing discovery
alone does not make Kestrel runnable.

### DOM WebGPU canvas binding on the opted-in macOS path

HTMLCanvasElement.getContext("webgpu") now creates the shared IOSurface context
when the main runtime realm has WebGPU installed with IOSurface interop. It
returns the same context for repeated calls, preserves the canvas object, and
locks canvas mode against Canvas 2D. Unconfigure does not release that mode.
Bitmap dimension setters/attribute resets resize the context and retire its
current texture, including already-submitted work. The bridge uses the document
canvas identity/generation/content serial and a runtime producer timeline.
The trusted DOMException constructor is retained at installation rather than
looked up again during application calls.

The runtime test obtains a normal DOM canvas, configures it with the device from
navigator.gpu, submits a clear, then resizes and verifies a replacement texture's
dimensions. It checks both directions of 2D/WebGPU exclusion and mode retention
after unconfigure. Rebuilt runtime CTest passes. Canvas controllers retire before
the realm during navigation and destruction.

This initial integration retains canvas controllers until document retirement
and uses a 64 MiB per-canvas pool cap. Detached-canvas collection, aggregate
resource budgeting, subframe exposure and failure semantics remain qualification
work. Automatic frame expiration/scene publication and normal presenter
consumption are not connected yet, so this is not a visible application pass.

### Rendering opportunities and completed scene publication

An acquired current texture now contributes to host frame demand even without
requestAnimationFrame. At a signaled rendering opportunity, the runtime expires
current textures after the last due RAF callback and hands already-submitted work
to the provider. Completed provider retirements participate in task readiness;
the normal task pump publishes matching canvas/generation/content versions into
the document. Stale versions are dropped before publication. No GPU completion
wait or pixel readback is introduced in this production path.

Configure/unconfigure invalidate the document's displayed image and advance its
content version so an older pending completion cannot restore it. Published
unchanged content requests no further frame. Navigation clears pending rendering
opportunity state along with canvas controllers.

The macOS runtime test draws through a real DOM canvas without RAF, signals a
host rendering opportunity and obtains the resulting document scene image. A
post-completion diagnostic IOSurface read verifies all 16 opaque red pixels in
the 8x2 image. It verifies automatic current-texture expiration, zero idle frame
demand, image removal on unconfigure and current-texture identity across two RAF
callbacks in one rendering opportunity. Runtime CTest passes after rebuild; the
V8-disabled native library also builds. Normal desktop host enablement and
managed v3 scene consumption remain outstanding, so this is document scene
publication evidence rather than a visible WebGPU application qualification.

### Managed scene image ownership for Ganesh

`NativeMacOSGpuSceneImages` captures indexed GPU image leases from a v3 scene
(or retains a supplied list), rolling back earlier captures if a later one fails.
It validates the negotiated image metadata, prepares every import before replay,
and retains completed imports across admission retries and unchanged renders.
Drawing resolves scene image indices; retirement releases unused CPU leases and
keeps imported resources until their host GPU fences complete. Callers must keep
the owner through successful TryComplete; it deliberately does not equate
managed disposal/finalization with GPU completion.

The Ganesh window probe now uses this owner and passes its four destination-pixel
checks across 32 frames with one import, completed retirement and zero explicit
transport copies. A native managed integration test verifies rollback when a
later source has been disposed: one passed, zero skipped on macOS/net10.0.
The probe builds with zero warnings. Normal scene acquisition, old/new scene
replacement and shutdown scheduling still need to adopt this owner in the
production composition handler; this does not yet enable ordinary GPU scenes.

### Bounded managed image-group replacement

`NativeMacOSGpuScenePresenter` holds one current image group and at most two
retiring groups. Replacement transfers ownership only when a retirement slot is
available; otherwise the caller retains the candidate. Preparation drains old
groups under the host graphics lease, prepares the current group and preserves
its imports across unchanged draws. Shutdown stops admission and requires host
graphics callbacks until every group has completed retirement. Unimported rejected
candidates can now use DiscardUnprepared to release CPU leases without a graphics
context; imported groups must use fence-based retirement.

The Ganesh window probe replaces its image group after 16 frames and checks four
destination pixels both before and after replacement. It retains the same source
image into the replacement group; this tests ownership replacement, not distinct
new image contents or cross-scene import deduplication. The run completes 32
frames, two imports, eight diagnostic readbacks, zero explicit transport copies
and successful retirement of both groups. Evidence is recorded in
`evidence/ganesh-host/scene-image-replacement.json`. Two native managed tests pass
with zero skips for capture rollback and unimported-candidate release. Probe
build has zero warnings. Production composition-handler acquisition/replacement
and shutdown scheduling still need to call this owner.

### Transactional v3 scene application

The managed GPU presenter can now apply a v3 scene while holding its SafeHandle
view: it validates version/capabilities and CPU scene shape, retains all indexed
images, verifies GPU command indices, applies the renderer diff, commits image
bindings and only then acknowledges the native scene. Admission backpressure
leaves the renderer and caller's scene lease unchanged. Rejected candidates
release their unimported leases. A presenter with no imported groups can stop
synchronously via TryDiscardUnprepared; imported groups still require host
context retirement.

A managed/native test applies and acknowledges an actual engine bootstrap v3
scene, then verifies synchronous unimported cleanup and stopped admission. It
passes on macOS/net10.0 with one pass and zero skips. This bootstrap scene has no
GPU images; actual image import/draw/retirement remains covered separately by the
window probe and native image tests. The normal composition handler is not opted
in yet: its current Stop path removes the visual without guaranteeing later GPU
retirement callbacks. That lifecycle must be connected before enabling GPU scene
admission there.

### Retirement after visual detach

The pinned Avalonia 11.3.4 implementation establishes the locking contract needed
for detached cleanup: [native CGL MakeCurrent](https://github.com/AvaloniaUI/Avalonia/blob/11.3.4/native/Avalonia.Native/src/OSX/cgl.mm)
locks/restores the native context, and [Skia DrawingContextImpl](https://github.com/AvaloniaUI/Avalonia/blob/11.3.4/src/Skia/Avalonia.Skia/DrawingContextImpl.cs)
holds the GRContext monitor while drawing. Its platform API lease flushes Skia
before raw GL access and resets the state cache afterward. The public host
IGlContext.EnsureCurrent operation reaches that native context lock.

Retained images and image groups now support retirement without a visual drawing
lease. After exclusive ownership transfers away from the rendering path, cleanup
acquires EnsureCurrent, then the GRContext monitor in Avalonia's order, releases
SKImage references, flushes pending reads, inserts/polls the GL fence with zero
GPU-wait timeout, and resets Skia's state cache. Context mismatch/loss fails while
retaining consumer ownership; it never fabricates GPU completion. The host must
keep its graphics context alive until cleanup completes. Concurrent framework
context destruction and device-loss recovery remain qualification work.

The window probe's --detach-before-retirement mode removes its control from the
window after 32 frames, suppresses subsequent draw callbacks, and retires on a
worker that is asserted different from the rendering thread. It passes with two
imports, eight diagnostic destination-pixel checks, zero explicit transport
copies and completed GPU retirement. Evidence is
`evidence/ganesh-host/detached-retirement.json`; probe build has zero warnings.
Production stop must still transfer ownership to this path before normal GPU
scene admission is enabled.

### Opt-in composition-handler GPU path

NativeSceneCompositionHandler now has explicit macOS GPU admission (default
false). The admitted branch acquires ordered v3 scenes with image/ordered-canvas
capabilities, applies transactional image bindings, prepares imports under the
Skia lease and supplies indexed GPU drawing during retained replay. Admission
backpressure keeps the publication edge available while rendering drains old
retirements. GPU-only work can request full invalidation when CPU damage is empty.
The legacy CPU acquisition path remains the default.

Stop removes the handler's presenter reference and transfers exclusive ownership
to NativeMacOSGpuRetirement. That service roots the owner independently of the
removed visual, polls detached retirement on a worker, and removes the root only
on completion. Failure/15-second timeout is traced and leaves resources retained
for diagnosis rather than fabricating completion. Framework context lifetime,
failed-owner recovery and final application shutdown coordination still require
qualification; the service is not a claim of complete device-loss handling.

The detach window probe now uses this service and verifies its retained count
returns to zero after completion: 32 frames, two imports, eight diagnostic pixel
checks and zero explicit transport copies. Three native ownership/application
tests and 21 damage/mailbox policy tests pass with zero skips on macOS/net10.0;
probe build has zero warnings. These tests do not yet drive the admitted handler
with a normal GPU-producing engine. Desktop negotiation/engine enablement,
ordinary full-path rendering, captures/frozen scenes and Uno remain outstanding.

### Per-document native host admission (2026-09-08)

The optional tail of `webscene_engine_options` now accepts a WebGPU policy
callback. On graphics-enabled macOS the runtime evaluates it for the initial
about:blank document and each successfully resolved main-document navigation,
before application scripts. IOSurface admission is an explicit host assertion
that the document is trusted/secure and the scene consumer supports GPU images.
Unknown values deny admission. Older options sizes retain their previous
stylesheet callback boundary and do not read the new fields.

The runtime regression verifies denial on the initial document, admission
visible to inline scripts, preservation after a failed resource load, and
removal before scripts in a subsequently denied document. The graphics-enabled
runtime test passed; the graphics-disabled native engine also built. This does
not yet validate the C callback through a GPU-producing engine, managed callback
lifetime, ordinary desktop host negotiation, redirects, or full origin isolation.
The existing navigation implementation reuses the global realm; this hook is
not a substitute for browser security-context conformance.

The managed `NativeWebSceneApi.EngineCreate` now accepts an optional document
admission delegate and marshals the native policy tail. Its existing resource
bridge roots the delegate until native engine destruction; the static reverse
P/Invoke delegate is also rooted. Exceptions deny admission, and non-macOS
bridges cannot approve this IOSurface route. A native integration test passed
with no skips, verifying the initial URL, runtime-worker invocation, successful
admission, and safe teardown when the policy throws. The GPU host probe builds.
The normal surface still needs producer/consumer negotiation before enabling
this option; the callback test does not establish end-to-end window rendering.

### Ordinary WebScene document window (2026-09-08)

The opt-in NativeWebSceneView constructor now connects its host admission
delegate and GPU composition consumer. The --webgpu-document host probe loads
HTML through that view, requests an adapter/device, clears a 256x128 canvas, and
submits it. The macOS window visibly displays the green canvas; screenshot:
evidence/webgpu-document/macos-clear.png. Runtime diagnostics confirmed GPU
exposure, submission, and RAF execution. Temporary tracing confirmed one
scene image imported and drawn at 256x128; tracing was removed.

This exposed and fixed ordinary task pumping failing to end GPU rendering
opportunities (only the resize-specialized pump previously did so). The native
runtime pixel-publication regression now uses the ordinary task pump and passes.
Explicit CSS display:block and dimensions are currently needed by this demo:
default inline canvas layout emitted a zero-sized GPU paint rectangle and is
still an open defect. This screenshot proves a clear pass in the ordinary view,
not Kestrel, full WebGPU support, calibrated color correctness, or lifecycle
qualification. The demo's admission callback approves only its generated local
document; it is an explicit trusted test host.

Follow-up: the zero-sized inline canvas defect is fixed. The flattened text-run
layout path now rejects replaced elements, preserving their intrinsic boxes
through general inline layout. A regression verifies a canvas nested in a span,
256x128 attribute dimensions, and 300x150 defaults after removing attributes.
The native runtime suite passes. The demo no longer supplies CSS dimensions
or display:block; the window again visibly renders the green canvas, captured
in evidence/webgpu-document/macos-intrinsic-canvas.png. The managed probe builds
with zero warnings/errors. Broader replaced-element layout conformance remains
separate from these focused canvas checks.

### Shader triangle in ordinary view (2026-09-08)

The --webgpu-document demo now draws a 400x240 interpolated-color triangle.
Its JavaScript creates a WGSL shader module and an automatic-layout render
pipeline, sets that pipeline on a render pass, and draws three vertices before
queue submission. The normal macOS NativeWebSceneView visibly renders the
triangle (evidence/webgpu-document/macos-triangle.png). Runtime evaluation
reports GPU exposure, submission, and RAF completion without a demo error.
The managed probe builds with no warnings/errors. This is visual end-to-end
evidence for the basic shader/draw route; it does not qualify exact color
management, continuous frame replacement, or full WebGPU conformance.

### Resize and repeated-frame probes (2026-09-08)

The document demo now updates its canvas backing size from innerWidth/innerHeight
on window resize and schedules a redraw. --resize-webgpu drives 640x360,
280x180, 520x320, then 400x240 at 500ms intervals. The observed run submitted
five frames, ended with a 400x240 backing store, reported no demo error, and
visibly displayed the final triangle (macos-resize-final.png). This verifies
discrete resizing and final output, not smooth live dragging, Retina backing
resolution, or resize during sustained rendering.

--stress-webgpu requests 120 RAF-driven frames and currently FAILS: one run
stopped at three frames with canvas texture acquisition unavailable; another
stopped at fifty with texture-view wrapper capacity exhausted. Temporary scene
tracing also found many zero-image scenes between completed GPU images.
Current generation/content-serial filtering excludes the previous completed
image as soon as the next texture is acquired; retaining the last completed
image until replacement needs an explicit invalidation/serial contract.
These failures remain open; the stress mode is separate from the default demo.
No continuous-rendering or smooth-resize acceptance is claimed.

### Completed image retention (2026-09-08)

Canvas backing now records the earliest content serial allowed after a bitmap
reset. Acquiring a newer frame advances the current serial without invalidating
an older completed image. Scene capture and painting accept completed images
within this interval; publication rejects regressions. Configure/unconfigure
advance the reset floor, including same-size resets, so delayed completions
cannot resurrect invalidated content. The runtime regression verifies next-frame
acquisition retains eligibility and unconfigure rejects the old image; the
runtime suite passes. Image-pool and wrapper-capacity stress failures remain
unresolved and must still be retested/fixed.

Unchanged Kestrel-CAD from the supplied archive remains the application acceptance
test. The clear/triangle/resize probes are diagnostic fixtures and must not be
counted as Kestrel compatibility or epic completion.

### Wrapper release pressure recovery (2026-09-08)

Labeled GPU-resource wrapping now makes one V8 reclamation attempt when the
bounded release-ticket pool is full, then drains only releases whose accepted
command-prefix barrier is already satisfied and retries reservation. This
does not dispatch queued commands or JS completions. Weak callbacks still only
publish tickets; native GPU releases occur afterward on the runtime owner.
Reachable wrappers remain rooted and capacity remains bounded.

The runtime regression creates 300 unreachable texture views across evaluation
scopes while retaining another view, then verifies the retained view's label
still works. The complete runtime suite passes. This is a pressure fallback,
not a performance-qualified GC scheduling policy; latency and native-memory
accounting still need qualification. The normal-window stress rerun still
failed after three frames with canvas texture acquisition unavailable, so
continuous rendering remains unqualified independently of this ticket fix.

### RAF canvas-capacity admission (2026-09-08)

The runtime now defers releasing a new RAF batch when a configured GPU canvas
has no current texture and its bounded provider cannot acquire an image.
Already-acquired textures still receive their end-of-frame opportunity. Pending
callbacks keep their waiting deadline and are reconsidered at a later host
frame; this adds no GPU wait, readback, or storage. Provider readiness accounts
for both the three image slots and submission retirement slots.

Native runtime tests pass. After rebuilding, --webgpu-document --stress-webgpu
--resize-webgpu completed 124 submissions without a demo error and ended at
400x240 after the four-size sequence. Extra submissions come from resize redraws.
The probe now waits up to ten seconds for stress completion/error before reporting.
This verifies producer progress through combined rendering and discrete resizing,
not display of every submitted frame or live-drag timing. Non-RAF acquisition
under pressure, multi-canvas fairness, device loss, Retina resolution, and
smooth-presentation qualification remain open.

### Original Kestrel startup acceptance baseline (2026-09-08)

Run the host probe with --webgpu-document --kestrel /path/to/Kestrel-CAD.zip
--verify-kestrel, with WEBSCENE_TEST_NATIVE_LIBRARY pointing to the graphics
runtime. The harness extracts the standalone HTML unchanged, reports its SHA256,
loads it through the normal GPU-enabled NativeWebSceneView, inspects the app's
own readiness/backend label and history, disposes the view, and returns failure
unless Kestrel reports WebGPU startup. Without --verify-kestrel the window stays
open for investigation. This startup check does not replace interaction,
geometry, export, resize, or performance acceptance.

The supplied original document starts but selects Canvas 2D compatibility:
"d.pushErrorScope is not a function". The real startup check returned exit 1.
The failure baseline and original hash are in evidence/kestrel/startup.json.
GPUDevice error scopes are the first observed WebGPU initialization blocker.

### GPUDevice.pushErrorScope (2026-09-08)

The device binding now implements pushErrorScope with required-argument checks,
WebIDL string conversion, exact validation/out-of-memory/internal filters,
receiver revalidation after conversion, and Dawn PushErrorScope forwarding.
Tests cover method arity, invalid filters and receivers, one conversion call,
exception propagation, and a JavaScript-pushed scope capturing a Dawn-injected
validation error retrieved through native PopErrorScope. The runtime suite passes.
The asynchronous JavaScript popErrorScope API and WebGPU error object types
remain unimplemented; this is not complete error-scope support.

Unchanged Kestrel startup was rerun and still returned exit 1, now reporting
"shader.getCompilationInfo is not a function" as its Canvas 2D fallback reason.
The original document hash is unchanged. This is the next observed startup gap.

### Owned shader diagnostic snapshots (2026-09-08)

webgpu_compilation_info copies Dawn callback messages and source positions into
owned storage with message-count and aggregate-text budgets. It rejects excess
data instead of silently reporting an incomplete successful result. Tests verify
copy independence, explicit-length and NUL-terminated text, both budgets, and
a real invalid WGSL module delivering an error through Dawn GetCompilationInfo.
The native runtime suite passes.

This is the native data-lifetime component for getCompilationInfo, not the
JavaScript API. Promise/mailbox routing, cancellation/realm teardown,
GPUCompilationInfo/GPUCompilationMessage objects, and UTF-16 position mapping
remain required. Kestrel's missing getCompilationInfo failure remains open.

Diagnostic coordinate follow-up: the pinned Dawn CompilationMessages.cpp already
attaches DawnCompilationMessageUtf16 to every diagnostic. Owned snapshots now
preserve those UTF-16 coordinates separately from base byte offsets, with bounded
extension traversal and duplicate rejection. A real invalid WGSL shader containing
a supplementary Unicode character before the error verifies that the captured
byte and UTF-16 offsets differ correctly. The runtime suite passes. This avoids
reimplementing source mapping in WebScene; V8 delivery must use the UTF-16 fields
and must not silently substitute byte offsets if the extension is unavailable.

### Asynchronous getCompilationInfo delivery (2026-09-08)

GPUShaderModule.getCompilationInfo now returns a fresh promise, retains its
shader during the request, copies Dawn diagnostics in a native-only callback,
and settles through the existing completion mailbox on the runtime thread.
Requests are bounded; teardown cancels their mailbox owner and rejects pending
promises. Results expose diagnostic text/type and UTF-16 source positions with
a frozen messages array. Tests cover valid and invalid WGSL, concurrent requests,
fresh promises, receiver rejection, and delivery through ordinary task pumping;
the runtime suite passes.

Full WebIDL object branding/prototype placement, specified failure exception
types, exhaustive allocation failure and teardown-race tests remain required.
These result objects currently use read-only own properties, not the final
GPUCompilationInfo/GPUCompilationMessage interface prototypes. This is functional
delivery, not a full conformance claim.

The unchanged Kestrel check still exits 1, now with "GPUBufferUsage is not defined".
Its shader diagnostic step completes and initialization advances to buffer setup.

### WebGPU flag namespaces (2026-09-08)

Host-approved installation now exposes GPUBufferUsage, GPUTextureUsage,
GPUMapMode, GPUShaderStage, and GPUColorWrite. Namespace constants are enumerable,
non-writable and non-configurable; namespace globals are non-enumerable and
carry their toStringTag. Navigation retirement removes their global bindings.
Values follow the WebGPU draft: https://www.w3.org/TR/2026/CRD-webgpu-20260820/
Tests verify every defined value/property descriptor, tags, and navigation
removal; the native runtime suite passes. Worker exposure and full namespace
WebIDL harness qualification remain part of the broader conformance work.

Unchanged Kestrel was rerun and exits 1 with "d.createBindGroupLayout is not a
function". Buffer setup now advances to explicit binding-layout creation.

### Explicit layout native ownership (2026-09-08)

Dawn devices now own bounded bind-group-layout and pipeline-layout tables with
guarded access, close protection, native-reference conversion, distinct internal
V8 brands, and deferred release commands. Tests create a uniform-buffer binding
layout, use it to create a pipeline layout, release the original binding handle,
and retain the native pipeline layout. They also verify scope guards, wrong-brand
rejection, and deferred wrapper release. The runtime suite passes. Resource-count
assertions now compare against their actual pre-script baseline rather than
assuming earlier queued releases have not run.

JavaScript createBindGroupLayout/createPipelineLayout descriptor conversion and
exposure remain unimplemented; Kestrel's observed binding-layout failure remains
open. The two tables currently use the existing pipeline-capacity setting.

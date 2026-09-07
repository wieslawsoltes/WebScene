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

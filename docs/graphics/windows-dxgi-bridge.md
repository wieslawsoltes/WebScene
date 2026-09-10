# Windows GPU image bridge (G04 / issue #26)

Current implementation and Windows hardware evidence are recorded in
[Windows Kestrel](windows-kestrel.md). The sections below preserve the earlier
foundation checkpoints and their then-outstanding work. Epic qualification remains
open; the newer Avalonia WebGPU result does not qualify Uno or browser WebGL.

## Capability selection

`dxgi_bridge_contract.h` selects a candidate synchronization path from native
endpoint capabilities. Adapter LUIDs must be known and equal. Color format and
alpha masks must overlap for the requested image. Shared images are single-sample;
MSAA input returns `needs_resolve` so the producer keeps private depth/MSAA storage
and resolves explicitly. Shared fences are preferred when both endpoints support
them. The initial keyed-mutex policy accepts D3D11-to-D3D11 only; D3D12 keyed-mutex
combinations remain unsupported until a dedicated bridge is implemented/proven.

These are policy results, not proof that a handle can be imported or accessed.
Real endpoints must populate capabilities from their native devices. No software
pixel-copy fallback is selected by this policy. The portable test covers LUID
mismatch/unknown identity, format/alpha rejection, multisampling, fence preference
and keyed-mutex separation; it passes locally in 0.32 seconds.

## Handle and Dawn API basis

Microsoft distinguishes NT shared handles from legacy DXGI shared handles. NT
handles created by `IDXGIResource1::CreateSharedHandle` can be duplicated and must
be closed when no longer needed. The bridge will retain its own duplicated handle
and leave caller-owned handles untouched. It will not apply NT-handle cleanup to
legacy `GetSharedHandle` values. See [CreateSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)
and [GetSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle).

The pinned Dawn headers expose `SharedTextureMemoryDXGISharedHandleDescriptor`
(including `useKeyedMutex`), shared-fence DXGI descriptors, and BeginAccess/EndAccess
fence arrays and values. Import, access, submission and consumer completion must
remain distinct lifetime stages. EndAccess output ownership and synchronization
must be implemented against this pinned SDK, not inferred from a different Dawn
revision or from the same-device Metal test path.

## Next implementation and verification

- Own/duplicate NT texture and fence handles with explicit cleanup.
- Query native adapter LUID and format/usage support; import color allocations.
- Implement and test Dawn BeginAccess/EndAccess plus each supported native consumer
  wait/signal protocol. Reject unsupported pairings explicitly.
- Integrate ANGLE D3D11 production and typed probe-consumer access through leases.
- Exercise distinct colors/frame serials, alpha/channel order, resize, delayed
  consumers, device removal, import failure and repeated resource/handle teardown.
- Record Windows GPU/driver/LUID manifests and copy/residency counters. No Windows
  hardware pass or zero-copy Windows rendering result has been obtained yet.

## NT handle ownership implementation

`nt_handle.h` implements move-only adoption, duplication, release and idempotent
reset. The Windows adapter uses DuplicateHandle with non-inheritable, same-access
semantics and closes only its owned handles. Legacy DXGI shared-handle values must
not enter this NT-handle wrapper. The Dawn texture importer now uses this wrapper; native fence import remains outstanding.

Portable ownership tests cover moves, replacement, duplicate failure, explicit
transfer and preservation of the borrowed source. CTest passes locally in 0.32
seconds, and AddressSanitizer/UndefinedBehaviorSanitizer also pass. A Windows-only
branch creates and duplicates an NT event, closes the original, then signals/waits
through the duplicate. That native branch has not run on this macOS host.

The SDK probe build now includes this test, and the hosted SDK workflow runs it
as a non-GPU test. Pending hosted execution is not a Windows handle test pass,
and neither event-handle tests nor compilation prove DXGI texture/fence sharing.

## Dawn DXGI import implementation

`dawn_dxgi_image` now implements the Windows import path: reject unsupported
endpoint pairings, require enabled Dawn DXGI texture/fence features, duplicate the
borrowed NT handle, import SharedTextureMemory and validate dimensions, format,
array depth and requested usage. The duplicate outlives the imported texture and
memory objects. Failures return explicit statuses and unwind owned references.
Texture access is restricted to an explicit BeginAccess/EndAccess interval, as
described below. Adapter capabilities are currently supplied by native callers; real
LUID/format queries still need to be wired in. Unknown API values are rejected by
the capability contract.

The common Dawn descriptor/property code compiles with the pinned SDK on macOS,
the explicit non-Windows rejection test passes within the hardware suite (0.48
seconds), and the policy test passes (0.30 seconds). The SDK Dawn probe includes
the importer header so hosted Windows builds compile its Windows branch. A fresh
local Dawn probe build also passes. None of these results proves Windows import:
the Windows branch, duplicated DXGI resource handles, property-failure paths and
GPU access require Windows compilation and hardware execution still outstanding.

## Dawn access handoff implementation

The thread-confined importer now validates incoming fence/value arrays, begins
exclusive access, exposes its texture only during access, and returns Dawn's
EndAccess state to the caller. A successful handoff does not prove GPU completion:
the next consumer must wait on all returned fences and values. Dropping an active
access is a programming error; confirmed device loss permits explicit abandonment.

The pinned Dawn SharedResourceMemory implementation can end access before fence
export fails. Accordingly, a failed EndAccess permanently invalidates this owner
and clears the outgoing handoff rather than allowing a retry with stale fences.
Native fence import/export and consumer wait/signal integration remain outstanding.

The Dawn hardware test passes locally (0.45 seconds), including negative checks
for access on an unopened importer, and the standalone Dawn probe builds. These
checks compile the common access API but do not exercise a successful DXGI access
interval. Windows GPU execution, synchronization, device-removal and handle-leak
qualification are still required before this bridge can be considered working.

## Owned fence export

`dawn_dxgi_fences.h` converts an EndAccess fence set into owned NT handles and
64-bit signal values. The pinned Dawn D3D `SharedFence::ExportInfoImpl` returns
`mHandle.Get()`, a borrowed handle; the bridge duplicates it and never closes that
borrowed value. Export checks the native fence type before requesting DXGI data.
All handles are staged privately and published together. Type/duplication failure
closes earlier duplicates and clears output, so a caller cannot use a partial wait
set. Zero fences is valid and does not synthesize a CPU completion signal.

The Windows `end_owned` entry point preserves initialized state and returns these
owned waits. Export failure makes the image owner terminal, because handing off
an image without its producer waits would permit a race. Consumers must still
open the native fences and enqueue GPU waits at the recorded values; this change
does not implement that consumer or the incoming fence import path.

Injected ownership tests verify distinct duplicates, exact 64-bit values, rollback
on the second duplication/export failure, cleared replacement output and invalid
array sizes. They run inside the Dawn test (0.30 seconds, passed); the standalone
Dawn probe builds too. These synthetic exports do not establish Windows handle or
GPU synchronization behavior. Windows compilation and hardware execution remain
mandatory outstanding evidence.

## Incoming fence import

`import_dxgi_fences` now imports a complete borrowed NT-handle/value array before
publishing it to the caller. It checks matching counts, invalid handle sentinels,
the enabled Dawn DXGI fence feature and the imported fence's exported native type
(non-null Dawn error objects do not count as success). Partial imports unwind and
leave empty output. Signal values retain their full 64-bit precision.

`begin_shared_fences` connects the imported set to the image's BeginAccess call.
Pinned Dawn D3D11/D3D12 SharedFence::Create duplicates/opens incoming handles, and
SharedResourceMemory::BeginAccess stores references to its pending fences. Thus
callers retain their borrowed handles through import, while the temporary import
vectors may be released after BeginAccess. Native BeginAccess failure now makes
the wrapper terminal: Dawn may change its access state before backend setup fails.

The Dawn test passes (0.37 seconds), covering invalid device, mismatched arrays,
null handles and disabled features; the standalone probe builds. Positive DXGI
import and native GPU waits are not exercised on macOS. The Windows producer and
probe consumer, native adapter queries, fence signaling, device-loss behavior and
hardware/copy/leak qualification remain outstanding. No Windows pass is claimed.

## Native adapter identity

`dxgi_device_identity.h` queries an actual D3D11 device through IDXGIDevice,
IDXGIAdapter and its descriptor, or an actual D3D12 device through GetAdapterLuid.
Both reject removed devices. Query failures clear identity; endpoint initialization
also clears stale format/synchronization flags. This supplies the identity portion
of capability negotiation but does not infer format support or sharing success.

LUIDs are compared only within the current machine/boot, consistent with
[Microsoft's GetAdapterLuid contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getadapterluid).
Do not persist them as durable GPU identities or substitute vendor/device IDs.

The hosted non-GPU test target includes both overloads and null-device/stale-state
checks. Its portable ownership tests pass locally (0.31 seconds); the Windows-only
query code remains uncompiled and unexecuted on this macOS host. Actual producer
and presenter devices still need to be connected to these queries and validated
against Windows hardware manifests.

## Native format capability queries

Endpoint identification now also queries the five portable color formats through
[D3D11 CheckFormatSupport](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkformatsupport)
or [D3D12 format support](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_format_support).
A candidate requires 2D texture, render-target and shader-sampling support. The
query publishes its bitmask only after all calls and the device-removal check
succeed; failures clear the result and invalidate endpoint identity. Alpha and
synchronization fields remain unset until their separate qualification.

This is a conservative candidate filter, not proof of shared allocation/import or
sRGB view compatibility. Windows test branches cover format mapping and clearing
stale masks on invalid input. The portable test target passes locally (0.20 seconds),
but those Windows branches and real format queries remain unverified on this host.

## D3D12 shared color allocation

`d3d12_shared_color` creates a committed default-heap, single-sample color texture
with a shared heap and an owned, unnamed, non-inheritable NT handle. It validates
native adapter/format support and checks GetResourceAllocationInfo's byte size
against a caller-supplied available budget before allocation. Failed creation
unwinds the native resource; successful output owns both resource and handle.
An existing output is rejected rather than implicitly discarding a potentially
in-flight allocation. Depth and MSAA resources are not shared by this allocator.

The resource starts in COMMON state; access transitions and synchronization belong
to Dawn shared-texture access and the native consumer protocol. A lease provider
must retain this allocation through producer and consumer completion. It does not
perform a device-idle wait or CPU pixel transfer, and is not yet connected to the
production lease pool. The budget value is allocation size, not a measured driver
residency counter.

API basis: Microsoft's [committed resource contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource)
and [shared handle contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle).
The hosted test target includes the allocator and a null-device rejection check.
Portable tests pass locally (0.20 seconds); the Windows branch still requires
compilation, successful allocation/import, budget/failure tests and GPU qualification.

## D3D12 lease-provider integration

`d3d12_canvas_images` anchors four shared-color slots in `owned_image_pool`,
within the existing allocation byte budget. Other providers retain the default
three-slot configuration. The fourth Windows slot allows an ordered queued
scene to coexist with current drawing, GPU retirement and producer work.
Writers reserve idle slots, freeze validated image metadata and reuse matching
allocations. Changed dimensions/formats replace only an idle allocation. Aggregate
allocation bytes are bounded, and retained scenes/consumers keep the storage alive
after canvas closure. Typed native lookup checks provider, allocation generation,
content serial and adapter identity; it returns a borrowed allocation valid only
while the consumer remains alive. Producer and consumer GPU completion still must
be reported explicitly by the submission/fence path.

The new provider is not yet wired to Dawn submission or presenter waits. Unlike
the existing Dawn allocator, it does not yet evict other idle cached slots to admit
a larger allocation; this can conservatively return memory pressure until those
caches are replaced. Hardware tests must cover this before performance acceptance.

The portable NT ownership target passes (0.20 seconds) and the existing image-lease
test passes (0.01 seconds). The Windows target includes the provider and validates
null-device rejection, but Windows compilation and successful provider allocation,
retention, resize, lookup and GPU completion remain unverified locally.

## Idle-cache reclamation under allocation pressure

The D3D12 pool now retries out-of-memory allocation after reserving and clearing
other idle cache slots. Reservations remain held across retries to prevent repeatedly
selecting the same empty slot. Busy allocations cannot be selected, and cancellations
are quiet so reclamation does not wake its own retry loop. Exceptional unwinding
releases the storage mutex before reservation destruction can signal capacity.
This supersedes the idle-cache limitation noted above.

A new portable lease test retains only an outstanding GPU consumer, reserves both
other slots, proves neither aliases the protected slot, then verifies reuse only
after explicit consumer completion. The image-lease suite passes (0.28 seconds).
This verifies the reservation invariant; Windows native allocation pressure and
successful resize after cache reclamation remain required hardware tests.

## Native D3D12 consumer waits

`d3d12_fence_waits` opens an entire borrowed NT fence/value set against the consumer
queue's actual device before submitting any wait. It verifies the expected adapter
LUID and retains the queue and opened fences. Failed imports unwind without touching
the queue. Enqueue preserves each signal value and is single-use: a failure can
leave earlier waits queued, so the caller must not submit sampling or retry the batch.
The owner must survive through consumer GPU completion.

This uses [ID3D12CommandQueue::Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-wait),
which enqueues a GPU wait without blocking the calling CPU. It is not a completion
notification. Consumer command submission, completion-fence signaling and lease
retirement still need integration; no global device-idle wait is introduced here.

The hosted native test includes the implementation and unopened/null-queue checks.
Portable tests pass locally (0.35 seconds). Windows compilation, successful fence
opening, delayed producer ordering and failed-enqueue behavior remain unverified.
The latest SDK run was pending when checked; that is not a Windows pass.

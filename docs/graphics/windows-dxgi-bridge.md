# Windows GPU image bridge (G04 / issue #26)

Status: started. G01–G03 remain open. Native Windows import/export, synchronization,
producer integration and mandatory hardware qualification are not complete.

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
not enter this NT-handle wrapper. No texture/fence import uses it yet.

Portable ownership tests cover moves, replacement, duplicate failure, explicit
transfer and preservation of the borrowed source. CTest passes locally in 0.32
seconds, and AddressSanitizer/UndefinedBehaviorSanitizer also pass. A Windows-only
branch creates and duplicates an NT event, closes the original, then signals/waits
through the duplicate. That native branch has not run on this macOS host.

The SDK probe build now includes this test, and the hosted SDK workflow runs it
as a non-GPU test. Pending hosted execution is not a Windows handle test pass,
and neither event-handle tests nor compilation prove DXGI texture/fence sharing.

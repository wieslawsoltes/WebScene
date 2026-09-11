# macOS Kestrel memory investigation — 2026-09-11

The AppScene resize investigation found two separate kinds of retention. Metal
resources stayed bounded, while the V8 heap could remain expanded after activity.
A one-off diagnostic `webscene_engine_request_low_memory` reduced process footprint
from ~338 to ~267 MiB without changing Metal allocations (~152 MiB). This was a
classification experiment, not the production memory policy.

## V8 foreground maintenance

The engine previously pumped V8 foreground tasks only from application task
processing. A quiet document has no DOM timer/input to drive delayed V8 tasks,
including memory maintenance and asynchronous compilation completion. The worker
now services that queue at idle, after application/input/rendering work. It uses
the existing eight-task/one-millisecond admission budget and offers V8 a bounded
idle deadline. An individual V8 task cannot be preempted by that admission budget.
No synthetic RAF or forced garbage collection is introduced.

The `idle-v8-platform` regression starts asynchronous Wasm compilation and waits
for its native interop completion, without timers, evaluation polling, pointer
input, or RAF. It fails against the previous packaged engine and passes with the
idle pump. The complete native engine test suite also passes.

V8 still chooses when and how far to shrink its heap. Restoring maintenance does
not impose a fixed idle footprint; repeated batches retained more heap than the
first batch. Native-only applications do not create this V8 runtime.

## Graphics ownership and transient attachments

During six resize cycles, the traced IOSurface allocator replaced its two slots
rather than accumulating previous sizes. Approximately 11.4 MiB remained in those
slots. AppScene's Graphite import census returned to one consumer: the current
canvas. These are observations for this workload, not a general leak proof.

Kestrel's depth and multisample colour attachments are cleared each pass, never
read afterward, and resolved into a separate persistent canvas. AppScene's sample
build adapter now adds `TRANSIENT_ATTACHMENT` to these two textures and discards
their contents at pass end. Dawn maps this to Metal memoryless storage on supported
Apple GPUs. The sample's 4x sample counts, shaders, and resolved output stay the
same. The canvas being presented or captured must remain persistent.

For native WebGPU authoring, the equivalent usage is
`wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TransientAttachment`,
with clear/discard attachment operations. Apply this only when the application
knows the attachment does not need to survive the pass. It is not a safe blanket
optimization for arbitrary WebGPU applications.

The generated JavaScript falls back to ordinary render attachments when
`GPUTextureUsage.TRANSIENT_ATTACHMENT` is absent. Discarding unused scratch contents
still leaves the resolve target intact. AppScene leaves Kestrel's vendored browser
sources and original HTML/CSS unchanged.

At a 1612x926 pixel canvas, settled process-wide Metal allocation decreased from
159,023,104 to 109,953,024 bytes (about 46.8 MiB). Framebuffer counts and caches can
vary between samples, so these totals are not an exclusive per-resource sum.

## Diagnostics

- `WEBSCENE_GRAPHICS_MEMORY_TRACE=1`: IOSurface allocation/release IDs, dimensions
  and bytes; WebGPU wrapper/resource-table counts at memory census boundaries.
  Wrapper counts can include explicitly destroyed objects awaiting JavaScript GC;
  they are not a count of live Metal pixel allocations.
- AppScene's `APPSCENE_MEMORY_TRACE=1`: task footprint, Metal allocated bytes,
  Graphite caches, retained backing, and imported-consumer count/bytes. Imported
  bytes count leases, not unique IOSurfaces, and overlap native device accounting.
- `vmmap -summary`: settled physical-footprint samples, including GPU-related
  ownership that RSS omits. Compare identical dimensions and rendering backends.

Final AppScene measurements and the packaged artifact are documented in that
repository's `docs/validation.md` and `tests/platform/evidence`.

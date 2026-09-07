# GPU canvas lifetime implementation (G03 / issue #25)

Status: in progress. No GPU scene/lease ABI or production GPU presentation is
available yet. G01/G02 qualification and integration gaps remain open.

## Backing state

Every allocated canvas node now owns a stable backing identity. Context modes
are exclusive: none may become 2D, WebGL 1, WebGL 2 or WebGPU; requesting the same
mode is allowed, while switching modes is rejected. Existing 2D context creation
claims this mode. Unsupported browser context factories still return null without
claiming a mode. GPU factories are future binding work.

Backing state distinguishes bitmap dimensions, allocation generation and content
serial. Publishing content advances the content serial alone. A same-size bitmap
reset advances content but permits storage reuse; changed dimensions advance the
allocation generation as well. Mode and identity survive either reset. The
metadata owns no image allocation, pixels, native texture pointer or Skia object.

The native state test checks exclusive modes, distinct identities, 1,000 content
updates without an allocation-generation change, same-size reset, changed-size
reset and zero-sized bitmaps. This demonstrates metadata behavior, not actual
GPU allocation/copy counters. Canvas command appends and backing-store resets now publish content changes.
Initial 2D context creation synchronizes dimensions, and width/height property
resets update bitmap dimensions. Existing canvas command generations remain
unchanged while the lease API is implemented. General attribute mutation and
standards-level dimension normalization still need coverage.

## Remaining G03 integration

- Complete general attribute mutation and dimension-normalization coverage;
  connect backing versions to the new scene publication path.
- Add separately versioned scene acquisition/capability negotiation without
  changing the existing scene-view ABI.
- Carry opaque allocation identity, generation/content serial, format, alpha,
  color space, orientation and producer readiness in portable scene metadata.
- Implement explicit retained resource leases and consumer GPU completion.
  Scene acknowledgement must not release or recycle leased image storage.
- Bound active canvas presentation storage to three reusable color images, with
  backpressure for busy slots and retained old-generation leases on resize.
- Integrate GPU images into retained paint order, transforms, clips, opacity,
  isolation and damage; cover old consumers and stale-scene recovery.

Advancing a frame serial must never itself allocate an image or copy pixels.
GPU completion, scene retention and image reuse are separate lifetime conditions.

## Runtime backing-reset verification

The real V8 fixture draws into a 2D canvas and verifies content advances without
an allocation-generation change. CSS width changes preserve both bitmap content
and allocation generation. Resetting the width property to the same bitmap width
advances content while retaining identity/generation. Changing bitmap width to
640 advances allocation generation and updates dimensions while preserving 2D
context ownership. Dimension conversion for backing metadata checks finiteness
and bounds before integer conversion; it does not claim a redesign of existing
HTML attribute parsing/getter semantics.

The full 14-test local suite passed after the runtime wiring; the focused V8
fixture is rerun with the separate CSS-size assertion. These remain metadata and
2D recording checks, not proof of GPU pool allocation or zero-copy presentation.

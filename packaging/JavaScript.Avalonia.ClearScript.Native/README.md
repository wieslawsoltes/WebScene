# JavaScript.Avalonia.ClearScript native runtime

This RID-specific package contains the reviewed ClearScript V8 native binary used by
WebScene's optional trusted same-origin owner/iframe runtime. It is not selected by
default and does not change the ordinary WebScene runtime.

The package includes the exact native patch, source/V8 provenance, and SHA-256 hashes.
Applications must reference the package matching their deployment RID together with
`JavaScript.Avalonia.ClearScript`.

The native binary must pass WebScene's product-independent `probe v8dom` owner/iframe object-bridge
gate on its target platform before publication.

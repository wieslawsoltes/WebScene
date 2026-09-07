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

# G01: integrated Dawn isolation, macOS ARM64

The shared Dawn C API boundary resolves the V8 integration hang observed with static co-linkage. This evidence covers the implemented SDK builder and normal CMake consumer configuration, replacing the earlier disposable Ninja experiment. No application rendering or CPU pixel presentation path is added.

Verified on Apple M4 / Metal:

- Dawn, ANGLE ES 2 and ANGLE ES 3 diagnostic probes all verified their pixels on hardware.
- Ten SDK integrity/mismatch/relocation checks passed.
- Five macOS relocation checks passed, including actual adjacent Dawn/ANGLE load paths and their absence from graphics-disabled native loading.
- The graphics-enabled V8 native runtime and three parser suites passed (11.71 seconds total). The upstream-aligned no-graphics control passed separately; its log remains in the sibling V8 integration evidence directory.
- Binary export inspection accepted only WebGPU C functions. The shared library exposes no Abseil, Tint or Dawn C++ symbols.
- Nine Python evidence/export tests passed, including rejection of leaked dependency exports and unknown symbol output.

`index.json` identifies the implementation commit and artifact hashes. Compressed JSON retains the full SDK manifests and hardware/loader results. Compressed logs retain builder, configure, link and native test output. These builds reused pinned dependency source/build caches; they do not replace the outstanding clean-build gates on other platforms.

Reproduce from the repository root with the commands in `eng/graphics/README.md`: build both SDK components, configure/build the probes, run `run-probes.py`, `check-sdk-integrity.py`, and `check-macos-relocation.py`. Both components must be rebuilt for the updated lock. For native V8 verification, use a matching patched V8 15.3.10 SDK, configure the native engine with graphics and Inspector enabled, pointer compression/shared cage enabled, PartitionAlloc disabled, Release/dense linking and the bootstrap snapshot. Copy its `icudtl.dat` beside the binary before CTest. The stale inspector-header/archive combination is rejected during configure.

Windows/Linux build and GPU qualification, complete reference archival, and runtime package integration remain outstanding. This evidence does not close #23, establish browser API conformance, or claim Kestrel runs inside WebScene.

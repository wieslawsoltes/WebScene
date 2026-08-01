# WebScene html5ever parser third-party notices

The optional WebScene standards parser statically links the Rust crates pinned in
`Cargo.lock`. The dependency set and declared SPDX expressions are:

- `cssparser`: MPL-2.0.
- `cssparser-macros`: MPL-2.0.
- `dtoa`, `dtoa-short`, and `itoa`: MIT OR Apache-2.0.
- `html5ever`, `markup5ever`, `web_atoms`, `string_cache`, `tendril`,
  `smallvec`, `parking_lot`, `parking_lot_core`, `lock_api`, `scopeguard`,
  `cfg-if`, `libc`, `fastrand`, `proc-macro2`, `quote`, `serde`, `serde_core`,
  `serde_derive`, `syn`, and `windows-link`: MIT OR Apache-2.0.
- `bitflags`: MIT OR Apache-2.0.
- `log`: MIT OR Apache-2.0.
- `phf`, `phf_codegen`, `phf_generator`, `phf_shared`, `precomputed-hash`, and
  `new_debug_unreachable`: MIT.
- `siphasher`: MIT/Apache-2.0.
- `unicode-ident`: (MIT OR Apache-2.0) AND Unicode-3.0.
- `redox_syscall`: MIT. This target-specific dependency is not linked into the
  supported WebScene macOS, Linux, or Windows artifacts.

`cssparser` 0.37.0 implements CSS Syntax Level 3 tokenization and recovery. It
is feature-gated and linked only when the WebScene cssparser prototype or its
benchmark is selected. Its upstream project is
https://github.com/servo/rust-cssparser and its MPL-2.0 license text is included
in the crate source distribution.

The complete dependency graph, versions, sources, and checksums are recorded
in the adjacent `Cargo.lock`. MIT and Apache-2.0 license texts are included in
the `html5ever` source distribution; the Unicode-3.0 license text is included
in the `unicode-ident` source distribution. This notice is packaged only when
the `html5ever` build variant is selected.

# WebScene standards parser third-party notices

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
- `selectors`: MPL-2.0.
- `servo_arc`: MIT OR Apache-2.0.
- `derive_more` and `derive_more-impl`: MIT.
- `rustc-hash`: MIT OR Apache-2.0.
- `stable_deref_trait`: MIT OR Apache-2.0.
- `rustc_version` and `semver`: MIT OR Apache-2.0. These are build-time-only
  dependencies and are not linked into WebScene artifacts.

`cssparser` 0.37.0 implements CSS Syntax Level 3 tokenization and recovery. It
is feature-gated and linked only when the WebScene cssparser implementation or its
benchmark is selected. WebScene drives its public parser traits through an
in-repository adapter and streams borrowed input slices into a versioned,
exception-safe C callback sink; the upstream crate is unmodified. Its upstream project is
https://github.com/servo/rust-cssparser and its MPL-2.0 license text is included
in the crate source distribution.

`selectors` 0.39.0 implements Selectors parsing and specificity. It is
feature-gated and linked only when the Servo selector-parser variant is
selected. WebScene consumes its parsed syntax and specificity through a coarse
ABI; the existing WebScene matcher, cascade, invalidation, layout, and renderer
remain authoritative. Its upstream project is
https://github.com/servo/servo/tree/main/components/selectors and its MPL-2.0
license text is included in the crate source distribution.

The complete dependency graph, versions, sources, and checksums are recorded
in the adjacent `Cargo.lock`. MIT and Apache-2.0 license texts are included in
the `html5ever` source distribution; the Unicode-3.0 license text is included
in the `unicode-ident` source distribution. This notice is packaged only when
the `html5ever` build variant is selected.

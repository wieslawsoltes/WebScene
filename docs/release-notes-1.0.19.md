# WebScene 1.0.19

WebScene 1.0.19 adds the native compatibility surface needed by stateful embedded
chart startup while retaining ABI 3.

- Native `localStorage` and `sessionStorage` now provide the documented in-memory,
  engine/page-lifetime subset with browser-compatible coercion, null behavior, and
  insertion ordering in top and frame contexts.
- Avalonia and Uno accept shared `NativeWebSceneLoadOptions` with ordered,
  fail-closed document-start scripts and optional child-frame execution.
- The native ABI adds `webscene_engine_load_url_with_options`; the existing load
  export remains unchanged.
- SDK preflight accepts `localStorage` and `sessionStorage`; `WEBSCENE1002` now
  diagnoses IndexedDB specifically.
- Four-engine startup, frame ordering, callback signaling, external script-source
  accounting, and existing resource/compilation single-flight behavior remain
  covered by deterministic tests.

This release does not add durable Web Storage, storage events, quotas, IndexedDB,
full origin/reload semantics, product-specific bridge code, or broker revision 20.

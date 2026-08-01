# WebIDL-to-V8 binding catalog

This build-time tool validates WebScene's explicit native DOM exposure manifest against
the pinned WebRef IDL corpus, then generates the V8 template-installation fragment used by
the native engine's `generated` A/B lane. It does not generate native DOM behavior.

```sh
npm ci --prefix tools/webidl-v8-bindings
npm run generate --prefix tools/webidl-v8-bindings
npm run check --prefix tools/webidl-v8-bindings
```

The generated file is committed at
`experiments/WebScene.NativeEngine.Probe/native/generated/webscene_dom_bindings.inc`, so a
normal native build does not require Node, npm, a network connection, `@webref/idl`, or
`webidl2`. Update `dom-exposure.json`, regenerate, and commit both inputs and output.

Generation dependencies are pinned by `package-lock.json`: `@webref/idl` 3.82.1 is MIT
licensed and `webidl2` 24.5.0 uses the W3C software/document license. They are development
tools and are not linked, copied, or packaged with the WebScene runtime.

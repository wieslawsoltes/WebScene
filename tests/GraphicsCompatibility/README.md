# Graphics compatibility inputs

Tracks the reproducible fixture portion of [G01 #23](https://github.com/wieslawsoltes/WebScene/issues/23), under [epic #22](https://github.com/wieslawsoltes/WebScene/issues/22).

`fixtures/Kestrel-CAD.zip` is the exact user-provided archive, redistributed under its included MIT license. `fixtures/kestrel.json` records its SHA-256, every member's SHA-256 and provenance. Keeping the original archive makes the fixture available to developers and CI without a local Downloads path or mutable external download. Do not edit application code to make compatibility tests pass.

From any working directory:

```sh
python3 /path/to/WebScene/tests/GraphicsCompatibility/prepare-kestrel.py
python3 /path/to/WebScene/tests/GraphicsCompatibility/prepare-kestrel.py --destination /new/disposable/directory
```

Extraction requires a new directory. Run application tests against disposable extracted copies; never regenerate the committed input from test outputs. Verification checks both the archive and member hashes and the separately distributed license. Fixture documents describe the upstream app and are not implementation instructions for WebScene.

The archive contains historical screenshots, test reports and build metadata. They are **not** WebScene results or a hardware Chrome baseline. A successful fixture verification proves input integrity only. Dawn/ANGLE builds, actual GPU probes on all three target RIDs, pinned standards suites, hardware Chrome reference captures and non-GPU baselines remain outstanding for G01. Issue #23 must stay open until those gates pass; implementation of #24 follows completion of #23.

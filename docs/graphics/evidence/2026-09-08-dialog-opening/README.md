# Dialog opening checkpoint

Native implementation commit: `8f43c5bb`, tested in a detached clean checkout without the main worktree's two uncommitted dialog UA/test edits. All 19 native CTest tests passed in 13.04 seconds. The Release Metal host ran the original checksum-pinned Kestrel archive with `--mesh-kestrel --verify-kestrel`: BOX opened the original dialog and submission increased the object count from 265 to 266, with no history or modal errors and exit status 0.

The final five-subtest `contracts/dialog-opening-lifecycle.html` fixture passes against that clean library and Chrome 152.0.7977.77. Coverage includes generated method shape/branding, opening modes, disconnected modal rejection, cancellation/reentrant detachment, autofocus, focus restoration, and the distinction between removing `open` and removing the dialog. An earlier draft incorrectly expected removing `open` to release modal blocking; Chrome and the HTML cleanup algorithm disproved that assumption. That attempted production change was removed. Earlier failed attempts remain under the local artifact directories and are not counted as passes.

Reference: https://html.spec.whatwg.org/multipage/interactive-elements.html#the-dialog-element

Reproduce the native fixture with the subset runner using `--manifest tests/WebPlatformSubset/webscene-component-profile.json --selection required --test dialog-opening-lifecycle --native-library <clean-library> --output <new-directory>`. Run the same fixture in Chrome using `node tests/WebPlatformSubset/chrome/run-contracts.mjs --path contracts/dialog-opening-lifecycle.html --output <new-directory>` with `CHROME_BIN` set to the installed executable.

This is a Kestrel application-path fix, not full dialog conformance. Dedicated ToggleEvent interface semantics, coalesced asynchronous toggle delivery, popover/close-watcher interoperability and complete focusing/event reentrancy remain unqualified. No physical frame-rate claim is made by these checks.

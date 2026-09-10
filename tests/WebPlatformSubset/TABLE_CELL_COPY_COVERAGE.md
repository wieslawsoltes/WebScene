# Table View copy regression (#1289)

The reduced native pointer test first failed with `HTMLTableCellElement is not
defined`. Adding the distinct `td`/`th` interface exposed a second failure:
`document.execCommand is not a function`. The final test follows the legacy-copy
attempt through the modern `ClipboardItem` fallback and checks the exact host
request payload, including iframe execution and an empty-cell negative case.
It does not modify the OS clipboard.

`contracts/dom-table-cell-element.html` checks interface identity, inheritance,
namespace exclusions, creation/cloning, delegated clicks, and iframe realms.
`contracts/dom-unsupported-editing-command.html` checks the bounded unsupported
command path, argument conversion, receiver validation, and non-HTML rejection.
These 11 assertions pass unchanged in Chromium and native macOS ARM64. They remain
candidates pending cross-RID qualification; the native pointer regression is also
included in the full native test executable, not only its focused filter.

## Separate observation during fixture development

Resolving the parent readiness promise directly from the iframe's inline script
with promise-test cleanup removing the frame/callback caused the native WPT process
to exit 139. Deferring resolution avoided that crash, but a later run reported
`parent.__tableCellFrameReady is not a function` after cleanup. The isolated test
therefore defers assertions and retains its frame/callback until document teardown.
This does **not** establish that arbitrary cross-frame promise/teardown reentrancy
is safe; that separate lifecycle failure is not fixed by these changes. To reproduce
the earlier ordering, use `resolve` directly as the readiness callback and register
promise-test cleanup to remove the frame and delete the parent callback.

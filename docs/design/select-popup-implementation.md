# Select popup implementation

This branch implements the first in-surface presenter described by the accepted
[popup hosting decision](select-popup-hosting-decision.md) and the
[select popup proposal](select-popup-proposal.md).

The application API remains ordinary `<select>`, `<option>` and `<optgroup>`.
Authoritative option state remains in the authored DOM. The runtime creates a
closed, engine-owned shadow tree for the transient listbox presentation and
renders that tree through the normal WebScene layout/scene pipeline. The popup
therefore remains WebScene-rendered HTML/CSS content; no platform-native select
widget is introduced.

The implementation removes click-to-cycle activation. Opening a collapsed select
keeps its live value unchanged until a choice is committed. Pointer selection,
keyboard highlight/commit, Escape and outside-click cancellation, disabled
options/optgroups, scrolling, basic type-ahead, viewport placement, option identity,
and stale option-state detection are handled by the runtime. Selection commits
emit `input`/`change` only when the selected option identity actually changes.

The default presenter is in-surface. The accepted architecture deliberately keeps
popup hosting separate so a future AppScene platform popup window can host the
same WebScene-rendered control without replacing it with an OS dropdown widget.
That out-of-surface host is not part of this first implementation.

The Web Platform contract and native runtime regression are changed from the old
click-to-cycle shortcut to real open/commit/cancel behavior. The runtime regression
also verifies disabled-optgroup keyboard navigation and that an outside dismissal
does not leak its pointer gesture into content underneath.

Issue #44 remains the acceptance tracker for broader platform/accessibility and
host qualification; this implementation should not be treated as evidence that
future platform popup hosts or full native accessibility bridges already exist.

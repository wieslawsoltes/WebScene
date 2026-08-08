# WebScene 1.0.20

## Native Elements inspection

- Adds immutable worker-produced snapshots of the authored native DOM with
  stable document-local ids, attributes, computed styles, and box geometry.
- Adds DOM/CSS/Overlay routing to the existing Inspector WebSocket while
  preserving byte-for-byte forwarding for V8-owned domains.
- Adds native box-model highlighting and a worker-side hover/click picker that
  does not dispatch picker gestures into the inspected application.
- Publishes document refresh and picker-selection notifications understood by
  Chrome DevTools and CDP Inspector, including the latter's DOM highlight
  aliases.
- Extends Avalonia and Uno native views with the shared `INativeDomInspector`
  capability and enables it in both showcase Inspector hosts.

The first slice is intentionally read-only. It includes a minimal accessibility
tree for the Inspector's selection details; DOM/CSS editing, complete matched
stylesheet rules, and event-listener projection remain follow-up work.

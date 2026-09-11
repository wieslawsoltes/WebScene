# Hybrid Kestrel migration

The user has selected hybrid JavaScript/C++ delivery now. Pause general CAD
feature porting to C++; retain original JavaScript application behavior. Port
HTML-producing UI code to compiled templates and C++ construction immediately.
No runtime HTML parsing is permitted. Full browser behavior and 60fps panning /
OS window resizing without stretching remain acceptance requirements.

## Source and ownership

`upstream/` preserves the 37 scripts loaded by the unchanged reference index,
in original order, at commit 7a1e84c67fd24410c22f0d1a45b2e54120b32d0e.
The MIT license is included. These files are migration inputs, not a runnable
hybrid distribution. `migration-inventory.json` records source hashes and HTML
API occurrences. It is not a complete inventory of HTML template producers.

JavaScript initially owns the original document model, selection, undo history,
commands and renderer state. Do not run the separate native drawing/controller
alongside it as a second source of truth. Native UI constructors and JavaScript
must share one WebScene native DOM and style engine. Keep the original Foco
WebScene compositor/WebGPU hosting path. Existing native-only sample remains
available; do not overwrite it or treat it as the hybrid application's model.

## Implementation sequence

1. Add a supported native compiled-document initialization path to the V8-backed
   engine. Construction must occur on the runtime owner thread before scripts,
   using the same DOM wrapped by V8. Keep native APIs/lifetimes explicit.
2. Compile unchanged root HTML and CSS, with script loading controlled by the
   hybrid bootstrap; package all required resources locally.
3. Replace all HTML-producing UI paths, including extension dialogs, with native
   construction of predefined templates. Pass structured values/references across
   interop; never pass generated HTML strings to a parser or silently drop UI.
4. Start the complete original script set in order. Route dynamic UI requests to
   C++ while preserving callbacks and one JS application state/undo history.
5. Add an enforcement test rejecting runtime HTML parser entry from the hybrid
   workload. Test original commands, dialogs and extension modules, and measure
   actual presented frames during pan and OS resize.

Do not claim completion from a loaded window or startup screenshot. The current
source inventory and native sample are preparation, not proof of hybrid parity.

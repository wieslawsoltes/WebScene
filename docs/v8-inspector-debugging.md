# V8 Inspector and Chrome DevTools debugging

WebScene exposes its dedicated native V8 isolate through the V8 Inspector
Protocol. The transport is raw CDP: WebScene does not reinterpret debugger
commands or events, so Chrome DevTools and the CDP Inspector app see the same
script IDs, execution contexts, call frames, scopes, breakpoints, exceptions,
live-edit results, and WebAssembly metadata produced by V8.

## Architecture

Each `v8_dom_runtime` creates one `v8_inspector::V8Inspector` before document
scripts execute. The outer document and every iframe realm are registered with
`contextCreated` and removed with `contextDestroyed`. Every client connection
gets an independent `V8InspectorSession` and channel.

Native C ABI entry points connect, dispatch, and disconnect sessions. Calls may
originate on any managed thread, but messages are queued and dispatched only on
the engine worker that owns the isolate. When V8 pauses, the inspector client
runs a nested message loop on that same worker so resume, step, evaluation,
breakpoint, and live-edit commands remain responsive. Engine shutdown interrupts
that loop and cannot hang on a paused script.

WebScene also reports timer and animation-frame scheduling to V8's async-task
instrumentation. With `Debugger.setAsyncCallStackDepth`, pauses inside
`setTimeout`, `setInterval`, and `requestAnimationFrame` retain their scheduling
stack.

## Managed session API

A loaded Avalonia view exposes a raw session:

```csharp
await using var session = view.OpenV8InspectorSession();
await session.SendAsync(
    "{\"id\":1,\"method\":\"Debugger.enable\"}"u8.ToArray());

await foreach (var message in session.ReadAllAsync())
{
    // One complete UTF-8 Inspector response or notification.
}
```

`INativeV8InspectorSession` is deliberately independent of any CDP client
library. `WebScene.Diagnostics.Cdp` supplies the Chrome discovery and WebSocket
host without adding a rendering dependency.

## Connect Chrome DevTools

Start the host after the native document is loaded:

```csharp
using System.Net;
using WebScene.Diagnostics.Cdp;

var options = new WebSceneV8InspectorOptions
{
    Enabled = true,
    Address = IPAddress.Loopback,
    Port = 9229
};

await using var inspector = new WebSceneV8InspectorHost(view, options);
await inspector.StartAsync();
```

Then:

1. Open `chrome://inspect`.
2. Open **Configure** under network targets and add `localhost:9229`.
3. Select **inspect** on the discovered `WebScene V8` target.

The Avalonia native showcase exposes this host directly when launched with
`--v8-inspector` (and optionally `--v8-inspector-port 9229`). For a local,
deterministic target, launch it with `--editor --v8-inspector`; the console logs
the exact discovery URL. The equivalent environment switches are
`WEBSCENE_V8_INSPECTOR=1` and `WEBSCENE_V8_INSPECTOR_PORT`.

The discovery document includes an authenticated
`webSocketDebuggerUrl`. Loopback discovery is unauthenticated so
`chrome://inspect` can poll it, while every WebSocket connection requires the
random access token. Non-loopback binding requires
`AllowRemoteConnections = true`; supply a strong explicit token when a stable
remote URL is needed.

## Original TypeScript, JavaScript, and source mutations

V8 executes JavaScript, so `Debugger.scriptParsed` always identifies the
generated JavaScript compilation unit. If the bundle contains an inline or
external `sourceMappingURL`, Chrome DevTools and the CDP Inspector Sources panel
can map locations, breakpoints, call frames, and stepping back to authored
TypeScript, TSX, JSX, MTS, CTS, and other source-map-backed languages.

The CDP Inspector mutation engine can edit the authored file, regenerate the
JavaScript and source map (esbuild is the default JS/TS regenerator), preview
the generated change, verify source fingerprints, and apply the regenerated
unit with `Debugger.setScriptSource`. WebScene's raw transport requires no
special mutation command: the resulting V8 live-edit request is forwarded
unchanged. Additional compilers can implement the CDP regenerator contract for
CoffeeScript, Svelte, Vue, Reason, or other languages that emit JavaScript and
standard source maps.

Source maps alone are not a compiler. An authored-language mutation is enabled
only when a matching regenerator is available and the generated script is still
the expected version. WebAssembly disassembly, bytecode breakpoints, stepping,
and stack navigation are supported by the Inspector protocol, but Wasm modules
remain read-only because safely replacing a compiled module requires a Wasm
toolchain rather than a source-map rewrite.

## Capability and constraints

- Runtime evaluation, script discovery/source retrieval, breakpoints, pause,
  resume, stepping, scopes, call frames, exceptions, live edit, async timer
  stacks, profiling domains, and V8 WebAssembly debugging travel over the raw
  session.
- Multiple clients receive independent V8 inspector sessions.
- Iframe contexts use the same context group, allowing one DevTools target to
  debug the complete WebScene document.
- The opt-in `WEBSCENE_V8_SHARED_ISOLATE` mode intentionally reports Inspector
  unavailable. Its independent engine workers share an isolate, which is not a
  safe ownership model for a per-view inspector pause loop.
- WebScene's host-provided `console.log`, `console.warn`, and `console.error`
  feed both the existing console-message queue and V8 Inspector's exact
  `Runtime.consoleAPICalled` pipeline. Object arguments retain V8 remote-object
  IDs and previews, so DevTools clients can expand them with
  `Runtime.getProperties`.
- Upstream V8 does not expose public console insertion for embedder-owned
  console objects. WebScene applies the narrow, versioned
  `V8InspectorConsolePatch.txt` bridge while building its pinned V8 SDK; the
  implementation still delegates storage, stack capture, wrapping, and event
  delivery to V8 Inspector.

## Validation

Native integration coverage enables Runtime and Debugger, evaluates an
expression, observes `Debugger.scriptParsed`, pauses on a `debugger` statement,
resumes, verifies an async stack for a timer callback, receives uncaught errors
and promise rejections through `Runtime.exceptionThrown`, and confirms that a
host console object arrives through `Runtime.consoleAPICalled` with an
expandable V8 object ID. The same native session starts and stops V8 CPU and
allocation sampling, validates returned profile trees, and reads live heap
usage. Managed integration coverage starts the real discovery/WebSocket host,
fetches `/json/list`, opens the authenticated endpoint with `ClientWebSocket`,
and verifies complete CDP messages in both directions.

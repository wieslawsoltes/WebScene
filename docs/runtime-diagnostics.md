# JavaScript exceptions, resource failures, console messages and runtime failures

The native runtime provides four distinct host notifications. Applications do not
need to initiate a JavaScript call to receive errors from page scripts, child frames,
event handlers, timers, animation callbacks, observers or unhandled promises.

| Notification | Meaning | Page continues? |
| --- | --- | --- |
| `JavaScriptException` | An exception escaped a page callback, or a promise remained unhandled at the runtime checkpoint | Yes |
| `ConsoleMessage` | An explicitly captured console message, including `console.error` | Yes |
| `ResourceFailed` | A host resource request failed, even if JavaScript catches it or a cache subsequently recovers it | Usually; a failed main document may also fail navigation |
| `RuntimeFailed` | Failed runtime bootstrap/navigation, host load timeout, catchable terminal engine failure, or explicit application promotion | No; the host detaches the failed engine |

JavaScript `try/catch` and promises handled before their rejection checkpoint are
not reported as uncaught. Explicit host interop calls still fault their returned
operation; they are not also reported as global page exceptions. A failing
document-start script is special: it is reported as an exception and also aborts
the load, producing a terminal failure. Normal cancellation and disposal are not
runtime failures. Recoverable scene-checkpoint retries are not terminal either.

## Avalonia and Uno

Both `NativeWebSceneView` (Avalonia) and `UnoNativeWebSceneView` expose the same
diagnostic properties and events. Subscribe **before** `LoadAsync` to include
startup and child-frame errors. SDK applications can subscribe through their
component host's `View` before mounting the component.

```csharp
view.JavaScriptException += error =>
    logger.LogError("JS {Message}\n{Stack} ({Document}, frame {Frame})",
        error.Message, error.Stack, error.Context.DocumentUrl, error.Context.FrameId);

view.RuntimeFailed += failure =>
    logger.LogError("WebScene failed during {Stage}: {Message}\n{Stack}",
        failure.Stage, failure.Message, failure.Stack);

view.ResourceFailed += failure =>
    logger.LogWarning("Resource {Method} {Url} ({Type}): {Error}, HTTP {Status}, {Elapsed} ms",
        failure.Method, failure.Url, failure.ResourceType, failure.ErrorCode,
        failure.HttpStatus, failure.Duration.TotalMilliseconds);

#if DEBUG
view.ConsoleMessage += message =>
    logger.LogDebug("JS console.{Level}: {Message}", message.Level, message.Message);
#endif

view.ShowRuntimeFailure = true; // optional; defaults to false
await view.LoadAsync(options);
```

Uncaught exception capture and console capture are independent. Subscribing to
`JavaScriptException` enables native exception capture. Subscribing to
`ConsoleMessage` enables console capture; removing its last subscriber disables
it unless `CaptureConsoleMessages` remains explicitly enabled. Production
applications can log exceptions without enabling console messages.

`ResourceFailed` independently enables resource diagnostics and is suitable for
production subscriptions. It reports host-loader attempts for documents, scripts,
stylesheets, text-backed images and fetch/XHR data requests. Fields include the
absolute URL, method, resource type, stable error category, optional HTTP status,
elapsed time, and the usual generation/sequence/timestamp context. The current
resource callback does not supply a frame ID; it is zero rather than guessed.
`Context.DocumentUrl` is the request referrer when available, not necessarily the
top-level page. URLs strip user information, query strings and fragments; data URL
payloads are redacted. Request bodies, headers, response bodies and raw transport
exception messages are not included. Paths can still identify private resources.

This is not an all-network traffic inspector: font registration performed directly
by the managed presenter, image decoding, WebSocket transport, and requests rejected
before reaching the host loader are outside this event. A caught JavaScript error
does not become an uncaught exception just because a resource request failed.
Cache fallback may recover the request; a resource event alone does not mean the
chart failed. Chart-ready is application-specific and should be logged by the host
alongside a startup deadline, rather than inferred from `RuntimeState.Ready`.

Avalonia/Uno loaders send a status-3 failure envelope through the existing resource
callback ABI. Older/custom callbacks returning zero produce `ErrorCode=loader`
with unknown HTTP status. Older native engines treat status 3 as a normal resource
failure; install the matching updated managed and native SDK to receive events.

The .NET records are immutable copied data. `Context` supplies the host load
generation, native sequence, UTC timestamp, document URL, frame ID, script source,
one-based line/column and truncation flag. Unavailable locations use zero/empty
values. Frame IDs identify native document roots within that runtime, not stable
IDs across reloads. `IsUnhandledPromiseRejection` distinguishes rejection reports.
`Arguments` contains read-only console argument snapshots, not V8 object handles.

Handlers execute in order on a background dispatcher, **not** on the V8 worker or
the UI thread. Marshal UI changes to the platform dispatcher. Subscriber exceptions
are caught and sent to `Trace`; they do not enter JavaScript or recursively emit
another diagnostic. It is safe to request unloading/disposal from a handler.
An already running handler cannot be forcibly cancelled; queued old-generation
messages are suppressed on navigation/disposal. Slow logging cannot block native
queue draining or fatal-state UI updates.

Accepted diagnostics survive terminal engine cleanup, but not host disposal or a
new load generation. When capturing a failed startup before disposing the view,
the host can explicitly `await view.FlushRuntimeDiagnosticsAsync(token)` with a
short deadline. This waits for queued handlers, not asynchronous work launched by
them. Never call it from a diagnostic handler or block the UI thread on it. Regular
rendering, navigation and failure UI updates do not wait for application logging.

`RuntimeState` reports `Unloaded`, `Loading`, `Ready`, `Failed` or `Disposed`.
`LastFailure` survives engine cleanup and disposal and is cleared by a new load.
`RuntimeFailed` is emitted once per failed load generation. `ShowRuntimeFailure`
opts into a message with initially collapsed stack details. A
`RuntimeFailureContentFactory` can replace that UI and runs on the UI thread.
Ordinary JavaScript exceptions never replace the canvas.

Applications that know their own JavaScript application has become unusable can
explicitly promote that condition:

```csharp
await view.ReportFatalFailureAsync("The application could not recover.", stack);
```

Prefer a generic custom failure UI in production when exception messages/stack
traces may contain application details. Logging destinations and redaction remain
the host application's responsibility; WebScene does not transmit diagnostics.

## Flutter (macOS)

The supported macOS Flutter backend uses the same native records. Notifications
are delivered by `NativeCallable.listener` on the Dart isolate, independently of
frame painting; the former 500 ms console polling loop is removed.

```dart
WebSceneView(
  documentUrl: documentUrl,
  runtime: runtime,
  controller: controller,
  onJavaScriptException: (error) => logError(error.message, error.stack),
  onRuntimeFailed: (failure) => logError(failure.message, failure.stack),
  onResourceFailed: (failure) => logWarning('${failure.method} ${failure.url}: ${failure.errorCode}'),
  // Supply onConsoleMessage only when console logging is wanted.
  showRuntimeFailure: true,
  runtimeFailureBuilder: (context, failure) => Text('Unable to display this page'),
)
```

`controller.runtimeState`, `controller.lastFailure` and
`controller.reportFatalFailure(message, stack: stack)` provide state and explicit
promotion. `firstSceneTimeout` defaults to 30 seconds and can be customized or
disabled with `null`. Flutter callbacks are ordered on the Dart isolate; keep them
short and hand expensive logging off to a background isolate/service. Subscriber
errors use Flutter's error reporting. Existing `onError` remains available for
host/presentation errors; it is not a replacement for `onJavaScriptException`.

## Console cost, limits and compatibility

With no console consumer, no legacy capture and no attached Inspector requiring
messages, console calls perform only enablement checks: no argument formatting,
stack capture, diagnostic allocation, queueing or host wakeup. Capturing exceptions
does not enable console formatting. Explicit certification console output and V8
Inspector are independent consumers.

Supported captured methods are `log`, `info`, `debug`, `warn`, `error`, `trace` and
failed `assert`. Objects are represented by safe summaries (`[Object]`/`[Error]`),
not expanded by getters, proxies, `toString`, `toJSON` or user formatters. Strings
and primitive values are copied. This is a bounded logging API, not an Inspector
object browser or a complete implementation of browser console printf formatting.

Native diagnostics are bounded to 1,024 ordinary records and 4 MiB of queued JSON;
oldest ordinary records are dropped under pressure. A terminal failure has reserved
capacity. Text fields are limited to 8 KiB of UTF-8, arguments to 32 and stack frames
to 32. The pending rejection ledger is also bounded to 1,024 entries. Native loss
is surfaced as a dropped-count record. The .NET delivery queue holds 256 ordinary
notifications plus reserved terminal delivery; it drops new ordinary notifications
if a slow logger fills it. `DroppedDiagnosticCount` (.NET) and
`controller.droppedDiagnosticCount` (Flutter) expose loss. A truncation flag is
separate from dropped-record counts.

### Legacy console pull migration

The old native `webscene_engine_take_console_message`, managed
`TryTakeConsoleMessage`/`DrainConsoleMessages`, and Dart `drainConsole` APIs retain
their payload formats, but **capture is no longer implicitly enabled**.

- Managed views: set `CaptureLegacyConsoleMessages = true` before loading.
- A raw managed engine: call `NativeWebSceneApi.SetLegacyConsoleCapture(engine, true)`.
  Do not use that low-level setter on a view-owned engine; it replaces capture flags.
- A raw native engine: configure `WEBSCENE_DIAGNOSTIC_LEGACY_CONSOLE`.
- A raw Dart engine: call `configureDiagnostics(4, (_) {})`.

The TradingView sample enables legacy console capture only with `--monitor-runtime`;
uncaught exception logging and terminal failure reporting are independent.
The headless proof and Inspector benchmark explicitly opt into their console markers.

## Native ABI and lifecycle

The feature adds ABI 3 exports without changing existing option structure layouts:

```c
webscene_engine_configure_diagnostics(engine, flags, signal_callback, user_data);
size_t required = webscene_engine_take_diagnostic(engine, NULL, 0);
size_t fatal_size = webscene_engine_copy_runtime_failure(engine, NULL, 0);
```

Flags select exceptions, structured console and/or legacy console. Configuration
with a null callback unregisters synchronously. The callback is a lightweight wake
signal: it may run on the runtime/configuring thread and **must not re-enter the
engine, block, or call application handlers**. Drain on another execution context.
Short buffers do not consume a record; retry if the size changes due to eviction.
Use a single queue consumer and unregister before destroying the engine. A raw
engine's non-consuming terminal snapshot is independent of its ordinary script
error counter, so an uncaught callback during startup is not mistaken for failed
navigation. A raw
engine's terminal record is latched once for its lifetime; hosts create a new engine
for a new load generation. Do not attempt to resume a failed raw engine.

Older native libraries without these exports still work when diagnostics/fallback
are unused. Requesting them produces a clear unsupported-runtime exception instead
of silently losing logs. Use matching managed/native package versions for production.

This is **not** process-crash recovery. Native process aborts, access violations,
V8 fatal OOM, and infinite JavaScript loops cannot reliably invoke these callbacks
or display an in-process failure UI. They require process supervision/isolation or
a separately designed watchdog.

## Verification

- Native `runtime-diagnostics` regression filter covers disabled/safe console,
  short-buffer retention, bounded overflow, caught and host-invoked errors,
  timer exceptions, unhandled rejection checkpoints, trace/source metadata,
  child-frame attribution and document-start failures.
- Avalonia tests cover ordered delivery, logger failure isolation, slow consumers,
  reserved fatal delivery, generations, disposal, fallback replacement and real
  native-to-managed notification delivery.
- Flutter tests cover startup failure/fallback, metadata compatibility and real
  native-to-Dart delivery without a frame-polling loop.
- `contracts/console-no-author-code.html` is a **candidate local WPT-style test**
  for browser-visible console semantics. Host telemetry is not a WPT API; host
  delivery is tested at the native/managed/Dart boundaries instead.
- Live TradingView verification exposed a native resize crash when a media-query
  change handler created additional `matchMedia` objects. The
  `media-query-reentrant` native regression reproduces the pre-fix crash; dispatch
  now snapshots the original query count and keeps a stable pointee across callbacks,
  avoiding invalidated vector iterators. This fixes the native crash itself rather
  than attempting to turn an access violation into a JavaScript exception.

Set `WEBSCENE_TEST_NATIVE_LIBRARY` to the freshly built native library to run the
optional .NET and Flutter native integration tests. These tests skip when no
library is configured, so a pure managed/Dart pass alone is not integration evidence.

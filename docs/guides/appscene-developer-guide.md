# AppScene developer guide

## The recommended native app model

The default recommended native app model is:

1. **Compile HTML into C++ at build time.** Use compiled HTML templates for dynamic structure; do not parse or insert HTML strings at runtime.
2. **Select `CSS_BACKEND shared`.** Use WebScene's full native CSS engine and its supported CSS coverage without introducing V8 or JavaScript. Runtime CSS parsing is allowed.
3. **Use no JavaScript and no JavaScript runtime.** Native C++ owns application behavior.
4. **Implement application code in C++20 modules.** Use `import` and no handwritten application headers.

The UI compiler emits native document construction and prepared stylesheet data. At runtime, the shared native CSS engine resolves styles and performs layout without JavaScript. CSS text can also be parsed natively when needed. This model requires neither an HTML parser for application UI nor a JavaScript engine; it does not require a CSS-parser-free process. “Full CSS engine” means WebScene's shared engine coverage, not a guarantee that every browser CSS feature is implemented.

Use `.cppm` interfaces and `import` for application boundaries. Keep a small `main.cpp` entry point that imports the application module. Existing SDK and system headers can be included in a module's global fragment where those dependencies do not yet publish modules. This does not mean adding handwritten application `.h` or `.hpp` files. Platform implementation files such as macOS `.mm` files are appropriate behind a module interface.

The installed AppScene SDK delivers this model today through `AppScene::Native`, `appscene_add_application(... COMPONENT Native ...)`, `webscene_compile_html`, `appscene::native_options` and `appscene::run(options, document)`. A separate Foco checkout or a custom host adapter is not required for SDK applications.

### Recommended defaults

Use this native app model for new applications and as the target when extending ScenePlayer:

| Area | Recommendation |
|---|---|
| UI | Compile HTML and reusable templates into C++; prepare authored stylesheets at build time; ship required assets only. |
| Styles | Select `CSS_BACKEND shared` and link `WebScene::SharedCSS`; native CSS parsing and dynamic style updates are allowed without V8. |
| Application code | Use C++20 modules and `import`, with no application headers; keep platform implementations behind module interfaces. |
| Host | Use AppScene's Native host and existing document/composition APIs. Add NativeWebGPU only when application GPU work requires it. |
| Media | Stream through native platform APIs and retain decoded GPU-compatible surfaces through image leases. |
| Effects | Use GPU processing when needed; avoid routine CPU pixel copies and GPU reuploads in the playback path. |
| Evidence | Build and exercise the real native bundle against a pinned SDK; browser previews and syntax checks are supplementary. |

## Responsibilities

| Component | Responsibility |
|---|---|
| WebScene UI compiler | Reads HTML/CSS and templates at build time; generates C++ modules and prepared stylesheets for the shared CSS engine. |
| WebScene native document | Maintains native nodes, events, style state, layout and canvas commands. |
| AppScene native host | Owns the window, input dispatch, scheduling, text integration and GPU composition. |
| Application modules | Own behavior, state, service calls, resource policy and platform integration. |
| Optional runtime component | Supports applications that explicitly need JavaScript or runtime-loaded web content. |

AppScene uses the native rendering and composition pipeline. It does not embed an OS WebView. WebScene remains independently embeddable in other hosts.

## Project organization

Keep reusable views and templates, styles, controls, platform services and assets in separate directories:

```text
CMakeLists.txt
main.cpp
views/Main.html
styles/Main.css
controls/Application.cppm
platform/Services.cppm
platform/Services.mm          # Only when a macOS implementation is needed
assets/
```

Add a `templates/` directory when the application has reusable compiled templates. Instantiate those templates through generated/native APIs. Set user-provided text and values through document APIs; do not assemble HTML strings at runtime. Update styles through native classes or CSS APIs as appropriate; CSS parsing does not require JavaScript.

Do not ship `views/`, `styles/`, generated source modules, or application scripts as runtime resources. Stage only required assets, such as images, fonts and media, with the SDK asset packaging helper.

## Build a native application

```cmake
cmake_minimum_required(VERSION 3.28)
project(Counter LANGUAGES CXX)
find_package(AppScene REQUIRED CONFIG)
appscene_add_application(counter COMPONENT Native SOURCES main.cpp
  IDENTIFIER dev.example.counter)
target_sources(counter PRIVATE FILE_SET CXX_MODULES
  FILES controls/Application.cppm)
webscene_compile_html(counter views/Main.html MODULE counter.ui CSS_BACKEND shared)
target_link_libraries(counter PRIVATE WebScene::SharedCSS)
```

The default compiler mode is native. Do not select `MODE hybrid`, add a compiled-package runtime registrar, or link the Runtime component for this application model. A hybrid package can contain precompiled markup while still running JavaScript; interface compilation alone does not make an application native-only.

### Use shared CSS by default

Explicitly select `CSS_BACKEND shared` and link `WebScene::SharedCSS` as shown above. This is the recommended application setting even though the compiler helper currently defaults to `typed` when the option is omitted. The shared backend prepares authored stylesheets at build time and installs WebScene's native shared CSS resolver at runtime. It links CSS parser support and permits dynamic CSS strings. It does not require V8, JavaScript, the Runtime component, or reparsing the original stylesheet at startup.

Native code can update classes, values and CSS styles, including `document.attribute(node, "style", css_text)` where appropriate. Prefer reusable style rules for stable presentation. Dynamic CSS is compatible with this model; dynamic HTML insertion is not. Compile repeated UI structures as templates and instantiate them through native APIs.

`CSS_BACKEND typed` is an optional stricter profile for applications whose styles fit its current supported subset. It emits typed C++ style operations and can avoid runtime CSS parsing, but it is not a complete replacement for the shared CSS engine today. Do not simplify an application's intended design merely to fit typed CSS when shared CSS supports it. Kestrel's current stylesheet is not accepted by the typed compiler; its delivered app also still uses JavaScript through the separate hybrid profile. Shared CSS alone does not port that application behavior to C++.

Resolve unsupported HTML/compiler features and shared-engine CSS gaps explicitly. Selecting shared CSS broadens CSS coverage but does not guarantee arbitrary markup support or complete browser parity.

`views/Main.html`:

```html
<!doctype html>
<html><head><link rel="stylesheet" href="../styles/Main.css"></head>
<body><main><p id="count">Count: 0</p>
<button id="increment">Increment</button></main></body></html>
```

`styles/Main.css`:

```css
body { margin: 0; font-family: sans-serif; }
main { display: flex; flex-direction: column; gap: 12px; padding: 24px; }
button { padding: 8px 16px; }
```

`controls/Application.cppm`:

```cpp
module;
#include <appscene/native.hpp>
#include <appscene/text.hpp>
#include <string>
export module counter.application;
import counter.ui;

export int run_counter() {
    webscene::native_web::document document(appscene::measure_text);
    compiled_ui::build(document);
    int count = 0;
    auto subscription = document.on(document.find("increment"), "click",
        [&](auto&) {
            document.set_text(document.find("count"),
                              "Count: " + std::to_string(++count));
        });
    appscene::native_options options;
    options.title = "Counter";
    return appscene::run(options, document);
}
```

`main.cpp`:

```cpp
import counter.application;
int main() { return run_counter(); }
```

Configure against the extracted SDK with its pinned toolchain:

```sh
cmake -G Ninja -S . -B build \
  -DCMAKE_PREFIX_PATH="$APPSCENE_SDK" \
  -DCMAKE_TOOLCHAIN_FILE="$APPSCENE_SDK/lib/cmake/WebScene/WebSceneToolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 6
open build/counter.app
```

The compiler tracks referenced CSS dependencies and reports unsupported native CSS features. Resolve those diagnostics explicitly. Native compiler support is not a claim of universal browser compatibility.

## Scene Player: the native video reference

[ScenePlayer](https://github.com/SceneTech/ScenePlayer) is also distributed as AppScene's `samples/VideoPlayer`. It demonstrates native application ownership and the retained-surface media path. The current sample uses the optional typed CSS profile; use explicit shared CSS for new applications and designs needing the shared engine's coverage:

- `views/ScenePlayer.html` and `styles/ScenePlayer.css` are compiled into `sceneplayer.ui` at build time.
- `controls/PlaybackControls.cppm` implements controls, keyboard behavior, native document updates and responsive video sizing.
- `platform/Media.cppm` exposes the native media service; `platform/Media.mm` implements macOS file selection, AVPlayer streaming and decoded frame publication.
- `main.cpp` imports the application module. There are no application headers or JavaScript files.
- Only the media assets are packaged. The application does not parse HTML/CSS or execute JavaScript at runtime.

AVPlayer consumes file and HTTP(S) URLs directly and handles native audio playback. Decoded CVPixelBuffers are retained through IOSurface image leases and displayed in the document's external canvas through AppScene's existing GPU composition path. The application does not load the entire encoded file or decoded audio into memory. The three-slot decoded surface pool has a 128 MiB budget; decoder and compositor allocations have their own lifetimes. Media format support depends on macOS.

For native video, prefer retaining the decoder's GPU-compatible surface through the SDK's image-lease contract. On macOS, keep the CVPixelBuffer owner alive with its IOSurface until every GPU consumer has finished. Avoid locking and copying every decoded frame into a CPU vector, uploading it into another texture, or retaining an additional full CPU copy solely for occasional snapshots. Perform readback on demand for features that actually require CPU pixels.

Picture adjustments, transitions or other effects may require a GPU shader pass. That is compatible with the native app model: preserve GPU surface ownership and use a supported GPU import/processing path. If the SDK lacks the required import API, treat it as an explicit integration gap; do not claim that a CPU-copy fallback is the recommended retained-surface path. A native decoder adapter such as AVFoundation behind a C++ module is appropriate and does not require the Runtime component.

The controls resize the canvas to fit the stage while preserving the video's aspect ratio. Playback, pause, seeking, volume/mute and three window sizes are exercised by the native `--smoke` mode.

### SDK changes made during the video work

WebScene gained opt-in native URL streaming for its runtime media element, native decode/error handling fixes, and large-file/range-server regression coverage. That functionality remains useful to Runtime/hybrid applications. Scene Player's native implementation uses the installed native document and image-lease APIs directly and does not invoke the runtime media policy API or JavaScript interop.

AppScene gained the packaged VideoPlayer SDK sample and release qualification coverage. Its existing Native host and GPU image callback already provide the host integration required by the native player; a new JavaScript bridge or a new host media API is unnecessary.

## Lifetime, threading and validation

Mutate the document on its owner thread. Retain event subscriptions for as long as their handlers are needed, and destroy them before captured state. Keep the document alive until `appscene::run` returns. Use `host_handle::post` to return background results to the host thread, and cancel pending work when the application closes.

Use platform streaming APIs for large media and retain decoded surfaces until GPU consumers finish. Avoid whole-file buffering and synchronous expensive work on the UI thread. Native APIs still have their platform thread and lifetime requirements.

Validate the installed SDK consumer, not only a source-tree build. Exercise real native events, playback where applicable, resize, error recovery and shutdown. Inspect the final bundle and link inputs: the default native sample must have no JavaScript runtime, runtime web-engine library, runtime HTML UI resources or application script payloads. Shared CSS engine/parser libraries are expected and must not be mistaken for JavaScript runtime dependencies. The compiled UI uses prepared authored stylesheets; explicitly managed runtime CSS is also permitted. GPU/image lease retirement should complete at shutdown. Record the SDK version and source revisions used for qualification, and test the actual UI compiler, platform implementation, link and packaged application. A browser/WASM preview, screenshot or syntax check with substitute declarations does not establish native compatibility, visual parity, media performance or correct shutdown. Measure sustained playback, memory and presentation when making performance claims. Keep any hybrid qualification separate from this check.

## Application profiles

| Profile | Interface | Behavior | Runtime parsing/execution |
|---|---|---|---|
| Native with shared CSS — recommended default | Compiled HTML/templates and prepared stylesheets | C++ modules | Native CSS parsing/resolution allowed; no runtime HTML parsing, JavaScript or V8 |
| Native with typed CSS — optional stricter profile | Compiled HTML/templates and typed styles within supported subset | C++ modules | No runtime HTML/CSS parsing or JavaScript |
| Hybrid — explicit opt-in | Compiled interface with runtime document operations | C++ and JavaScript/TypeScript | JavaScript runtime; parsing depends on APIs used |
| Existing web content — explicit opt-in | Runtime-loaded HTML/CSS | Existing JavaScript/TypeScript | Runtime HTML/CSS and JavaScript |

TypeScript is compiled to JavaScript by application tooling; it is not automatically converted to C++. Kestrel is a hybrid migration example, not the reference for the native-only application model. Choose the Runtime component deliberately when compatibility requires it, and describe that dependency accurately in the application's documentation.

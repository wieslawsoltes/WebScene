# AppScene developer guide

## The standard application model

**Author the interface in HTML and CSS, compile it into C++20 modules at build time, and implement application behavior in C++20 modules. Ship a native application with no runtime HTML parsing, CSS parsing, or JavaScript execution. Do not create application header files.**

HTML and CSS are design-time inputs. The WebScene UI compiler emits native document construction and typed style operations. At runtime, the compiled application constructs and updates a native WebScene document; layout, input, focus, painting and composition remain live. Resizing does not require reparsing source markup or executing a script.

Use `.cppm` interfaces and `import` for application boundaries. Keep a small `main.cpp` entry point that imports the application module. Existing SDK and system headers can be included in a module's global fragment where those dependencies do not yet publish modules. This does not mean adding handwritten application `.h` or `.hpp` files. Platform implementation files such as macOS `.mm` files are appropriate behind a module interface.

The installed AppScene SDK delivers this model today through `AppScene::Native`, `appscene_add_application(... COMPONENT Native ...)`, `webscene_compile_html`, `appscene::native_options` and `appscene::run(options, document)`. A separate Foco checkout or a custom host adapter is not required for SDK applications.

## Responsibilities

| Component | Responsibility |
|---|---|
| WebScene UI compiler | Reads HTML/CSS and templates at build time; generates C++ modules and typed styles. |
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

Add a `templates/` directory when the application has reusable compiled templates. Instantiate those templates through generated/native APIs. Set user-provided text and values through document APIs; do not assemble HTML or CSS strings at runtime.

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
webscene_compile_html(counter views/Main.html MODULE counter.ui)
```

The default compiler mode is native. Do not select `MODE hybrid`, add a compiled-package runtime registrar, or link the Runtime component for this application model. A hybrid package can contain precompiled markup while still running JavaScript; interface compilation alone does not make an application native-only.

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

[ScenePlayer](https://github.com/SceneTech/ScenePlayer) is also distributed as AppScene's `samples/VideoPlayer`. It follows the standard application model:

- `views/ScenePlayer.html` and `styles/ScenePlayer.css` are compiled into `sceneplayer.ui` at build time.
- `controls/PlaybackControls.cppm` implements controls, keyboard behavior, native document updates and responsive video sizing.
- `platform/Media.cppm` exposes the native media service; `platform/Media.mm` implements macOS file selection, AVPlayer streaming and decoded frame publication.
- `main.cpp` imports the application module. There are no application headers or JavaScript files.
- Only the media assets are packaged. The application does not parse HTML/CSS or execute JavaScript at runtime.

AVPlayer consumes file and HTTP(S) URLs directly and handles native audio playback. Decoded CVPixelBuffers are retained through IOSurface image leases and displayed in the document's external canvas through AppScene's existing GPU composition path. The application does not load the entire encoded file or decoded audio into memory. The three-slot decoded surface pool has a 128 MiB budget; decoder and compositor allocations have their own lifetimes. Media format support depends on macOS.

The controls resize the canvas to fit the stage while preserving the video's aspect ratio. Playback, pause, seeking, volume/mute and three window sizes are exercised by the native `--smoke` mode.

### SDK changes made during the video work

WebScene gained opt-in native URL streaming for its runtime media element, native decode/error handling fixes, and large-file/range-server regression coverage. That functionality remains useful to Runtime/hybrid applications. Scene Player's native implementation uses the installed native document and image-lease APIs directly and does not invoke the runtime media policy API or JavaScript interop.

AppScene gained the packaged VideoPlayer SDK sample and release qualification coverage. Its existing Native host and GPU image callback already provide the host integration required by the native player; a new JavaScript bridge or a new host media API is unnecessary.

## Lifetime, threading and validation

Mutate the document on its owner thread. Retain event subscriptions for as long as their handlers are needed, and destroy them before captured state. Keep the document alive until `appscene::run` returns. Use `host_handle::post` to return background results to the host thread, and cancel pending work when the application closes.

Use platform streaming APIs for large media and retain decoded surfaces until GPU consumers finish. Avoid whole-file buffering and synchronous expensive work on the UI thread. Native APIs still have their platform thread and lifetime requirements.

Validate the installed SDK consumer, not only a source-tree build. Exercise real native events, playback where applicable, resize, error recovery and shutdown. Inspect the final bundle and link inputs: a native sample must have no JavaScript runtime, runtime web-engine library, HTML/CSS source resources or application script payloads. GPU/image lease retirement should complete at shutdown. Keep any hybrid qualification separate from this check.

## Optional compatibility profiles

| Profile | Interface | Behavior | Runtime parsing/execution |
|---|---|---|---|
| Native — default for new applications | Build-time compiled HTML/CSS | C++ modules | No HTML/CSS parser or JavaScript execution |
| Hybrid — explicit opt-in | Compiled interface with runtime document operations | C++ and JavaScript/TypeScript | JavaScript runtime; parsing depends on APIs used |
| Existing web content — explicit opt-in | Runtime-loaded HTML/CSS | Existing JavaScript/TypeScript | Runtime HTML/CSS and JavaScript |

TypeScript is compiled to JavaScript by application tooling; it is not automatically converted to C++. Kestrel is a hybrid migration example, not the reference for the native-only application model. Choose the Runtime component deliberately when compatibility requires it, and describe that dependency accurately in the application's documentation.

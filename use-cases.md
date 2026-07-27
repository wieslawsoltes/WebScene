# WebScene use cases

WebScene runs web-authored user interfaces on Avalonia using V8, without embedding
Chromium, WebKit, or a WebView. V8 executes JavaScript while WebScene supplies the
supported DOM, CSS, layout, events, Canvas, timers, observers, and virtual-iframe
behavior. Avalonia owns the application window, rendering, input, and platform
lifecycle.

The most accurate short description is:

> Run React and JavaScript UI components as native Avalonia surfaces—without a
> WebView.

WebScene is a targeted browser-shaped UI runtime, not a general-purpose web browser.
Its supported platform surface should remain explicit and driven by real component
requirements and focused compatibility tests.

## 1. Run JavaScript components without a browser

An Avalonia application can host a complex JavaScript component as an ordinary
application surface without launching or embedding a browser engine.

Suitable components include:

- trading and financial charts;
- Canvas and SVG visualizations;
- dashboards and telemetry panels;
- diagramming and drawing components;
- interactive reports; and
- other controlled, self-contained JavaScript widgets.

A production-scale advanced chart is a strong proof of this model because it exercises
React, Canvas, SVG, overlays, menus, observers, virtual iframes, pointer input,
keyboard input, and responsive layout.

## 2. Hybrid Avalonia applications with React islands

An existing Avalonia application can use XAML and C# for its shell while embedding
React or JavaScript components where web technology is particularly useful.

```text
Avalonia window
├── Native navigation and menus
├── Native settings and forms
├── WebScene React chart
├── WebScene JavaScript dashboard
└── Native status and operating-system integration
```

This is one of the strongest near-term product shapes. Developers retain direct
access to Avalonia and .NET while reusing sophisticated web components. WebScene does
not need to reproduce every browser feature before it can provide substantial value
inside a native application.

## 3. Build desktop applications with JavaScript or TypeScript

Applications can be authored primarily in JavaScript, HTML-like markup, and CSS.
TypeScript is compiled or bundled into JavaScript before WebScene executes it, using a
tool such as `tsc`, esbuild, or Vite.

A future application workflow could look like:

```text
dotnet new webscene-react
npm run build
dotnet run
```

The result is an Avalonia desktop application rather than a website inside a browser.
In this context, “native” means that Avalonia owns the window, input, graphics, and
application lifecycle. It does not mean that every HTML element becomes an AppKit,
Win32, or GTK platform widget.

## 4. Run React applications without Electron or a WebView

React DOM applications can run when their observed browser requirements fit the
WebScene compatibility profile. Existing advanced workloads demonstrate that this can
include a substantial React application rather than only simple examples.

Good candidates primarily use:

- React DOM;
- flex and positioned layout;
- SVG and Canvas 2D;
- pointer and keyboard events;
- portals and overlays;
- timers and animation frames;
- `MutationObserver` and `ResizeObserver`; and
- local, packaged, or remotely resolved JavaScript modules.

This is not yet a promise that an arbitrary React website will run unchanged.
Applications that depend heavily on service workers, browser navigation, media,
WebRTC, broad native form-control behavior, complex editing, or obscure CSS features
would require additional platform work.

React support should therefore be described as compatibility with a published WebScene
profile, not blanket browser compatibility.

## 5. JavaScript UI plugins for .NET applications

WebScene can provide a UI plugin system in which extension authors supply JavaScript or
TypeScript, CSS, and markup while the host exposes selected .NET services.

Potential products include:

- user-installable dashboard panels;
- custom trading studies and chart tools;
- workflow extensions;
- administrative application modules;
- white-label UI packages; and
- application-specific scripting environments.

Each plugin can own an isolated DOM and V8 runtime while sharing immutable source and
compiled-code caches with other instances.

This model should initially be limited to trusted code. WebScene does not currently
provide the navigation, origin, permission, process, and security sandbox of a full
browser.

## 6. Multi-instance dashboards and workstations

WebScene supports applications containing several independent scripted surfaces. This
is useful for chart grids, monitoring centers, financial workstations, operational
dashboards, and industrial control applications.

The runtime architecture provides:

- separate globals and documents per component;
- separate callbacks, mutable exports, and disposal lifetimes;
- shared immutable JavaScript source;
- shared V8 compilation-cache units;
- optional persistent compiled-code caching; and
- independent component failure and lifecycle boundaries.

This allows several copies of the same component to avoid unnecessary source loading
and compilation without coupling their mutable application state.

## 7. Incremental migration from web to native desktop

WebScene can provide a migration path for organizations that already have JavaScript
business logic or React components but want a native Avalonia application.

A migration can proceed incrementally:

1. Place the existing compatible component inside an WebScene surface.
2. Expose native application services through an explicit host bridge.
3. Replace browser-dependent areas with Avalonia controls where beneficial.
4. Keep reusable JavaScript business logic and suitable React components.

This avoids requiring either a complete XAML/C# rewrite or the permanent inclusion of
a full Chromium runtime.

## 8. Controlled offline, kiosk, and appliance interfaces

WebScene is a good candidate for controlled applications whose UI resources are known,
packaged, and tested together:

- market-data terminals;
- operations and monitoring consoles;
- kiosk applications;
- industrial Linux workstations;
- offline analytical tools; and
- branded applications built from a shared component catalog.

These applications benefit from a deliberately bounded web-platform profile because
they do not require arbitrary website navigation or compatibility with unknown web
content.

## 9. Headless component testing and deterministic rendering

The existing headless probes and curated Web Platform Tests subset point to another
use case: exercising JavaScript UI components without launching Chrome.

This can support:

- DOM and CSS integration tests;
- screenshot and reference-image comparisons;
- deterministic layout and hit-testing tests;
- pointer, keyboard, focus, and lifecycle contract tests;
- regression tests for supported browser behavior; and
- validation against a versioned WebScene capability profile.

The objective is not to replace general browser testing. It is to give applications
using WebScene a fast, deterministic test environment for the exact platform they ship.

## What WebScene replaces—and what it does not

WebScene can replace a WebView or Electron-style browser surface when the application
owns and tests the JavaScript component and its browser requirements fit the supported
profile.

Advantages include:

- direct composition with Avalonia controls;
- no embedded Chromium or WebKit DOM/layout engine;
- direct .NET and application-service integration;
- application-controlled browser capabilities;
- isolated multi-component lifetimes; and
- shared and persistent JavaScript compilation caching.

Important boundaries include:

- WebScene still embeds V8 as its JavaScript engine;
- arbitrary websites are not a supported target;
- browser security and origin sandboxing are not reproduced in full;
- unsupported DOM, CSS, media, storage, editing, and navigation behavior must be
  identified explicitly;
- synchronous JavaScript and forced layout still run to completion on the UI thread;
  and
- production deployments require reviewed V8 native packages for each platform RID.

## Recommended product layers

### WebScene Component Host

Embed packaged JavaScript or React components in an existing Avalonia application.
This is the strongest immediate product proposition.

### WebScene React SDK

Provide TypeScript definitions, a bundler integration, a supported React configuration,
and a published compatibility profile.

### WebScene App Template

Provide an opinionated template for building complete JavaScript or TypeScript desktop
applications on Avalonia.

### WebScene Plugin Runtime

Provide isolated scripted UI extensions with explicit, capability-based access to .NET
host services.

## Recommended next proofs

To establish WebScene as a reusable platform across unrelated applications:

1. Build a small independent React application using TypeScript and Vite.
2. Publish a minimal supported DOM/CSS/Canvas capability profile.
3. Add TypeScript declarations and a compatibility lint/check command.
4. Define an asynchronous, capability-based JavaScript-to-.NET host bridge.
5. Package two or three representative components and run them simultaneously.
6. Add signed and notarized platform packages for straightforward distribution.

The strategic goal should not be to become a complete browser. It should be to make
valuable JavaScript and React UI components first-class citizens in native Avalonia
applications through a tested, explicit, and intentionally bounded platform.

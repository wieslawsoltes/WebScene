# WebScene use cases

WebScene runs trusted web-authored UI through a native V8/DOM/CSS/layout/scene engine
without embedding a WebView or browser DOM. A native framework owns the window,
lifecycle, input integration, and presentation.

The accurate short description is:

> Run trusted JavaScript UI as an immutable native scene inside a .NET application,
> without a WebView.

WebScene is a targeted browser-shaped runtime, not a browser and not a promise that
arbitrary websites or React applications run unchanged.

## Best-fit workloads

The strongest fit is controlled, graphics-heavy UI whose requirements can be tested
against an explicit profile:

- financial charts and trading terminals;
- Canvas/SVG dashboards and telemetry panels;
- editors, diagrams, and drawing surfaces;
- offline kiosk and appliance interfaces;
- multi-panel workstations; and
- trusted JavaScript or TypeScript UI plug-ins.

These workloads benefit from keeping hot JavaScript/DOM/Canvas interactions native,
sharing compilation artifacts, and publishing immutable scene changes to the host.

## Native application composition

A native shell can combine framework controls with one or more WebScene surfaces:

```text
native application window
├── native navigation, settings, and operating-system integration
├── WebScene chart or editor surface
├── WebScene dashboard surface
└── native status and commands
```

Avalonia is the current reference presenter. A reusable general native component-host
package is still roadmap work, so the repository's native reference applications—not
deleted managed templates—are the present integration examples.

## Typed host capabilities

Hosts can expose reviewed application capabilities to JavaScript through explicit
contracts. The TypeScript declaration interop generator can produce typed .NET APIs for
those boundaries. This is useful for data feeds, commands, persistence, application
services, and domain-specific plug-ins without exposing an unrestricted browser or
reflection bridge.

Treat scripts as trusted. WebScene does not currently provide browser-grade origin,
permission, navigation, or process isolation for arbitrary untrusted content.

## Multi-instance applications

Independent native engine instances provide separate global/document/lifecycle state.
This suits chart grids, monitoring centers, workstations, and modular application
surfaces. Production use still requires memory-plateau, queue, cache, recovery, and
failure-isolation evidence for the intended instance count.

## Controlled migration

Organizations can reuse suitable JavaScript business logic and UI components while
moving their shell and platform integration to .NET. Migration should be capability
driven:

1. inventory the component's observed browser APIs;
2. compare them with the versioned WebScene profile;
3. add reduced contracts for required gaps;
4. expose native services through typed capabilities; and
5. retain browser tests for behavior outside WebScene's scope.

This is not a drop-in route for navigation-heavy sites, rich text editing, media/WebRTC,
service workers, arbitrary extensions, or other broad browser dependencies.

## Headless compatibility and rendering

The native runner can provide deterministic DOM/CSS integration, layout, input,
screenshot, and regression evidence for the exact runtime an application ships. This is
valuable as a component certification tool, but complements rather than replaces testing
in major browsers.

## What WebScene replaces

WebScene can replace a WebView surface when the application owns and tests the content
and the required platform behavior fits the bounded profile. It can reduce hot
JavaScript-to-host crossings and compose through a native scene presenter.

It does not replace:

- a general browser or browser security sandbox;
- full WPT/browser compatibility;
- arbitrary website navigation;
- all native platform widgets; or
- the need for RID-specific native runtime packaging.

## Product roadmap

The next product proof should be a reusable native Avalonia component host built from
the existing native reference applications. It needs explicit resource loading,
lifecycle/disposal, recovery, diagnostics, typed host calls, multi-instance evidence,
IME, clipboard, and accessibility before templates return.

After that reference path is stable, the presenter SDK can be extracted and other
frameworks promoted through the same compatibility, rendering, input, packaging, and
product-workload gates.

# ADR: popup hosting is separable from popup rendering

Status: **Accepted for the select-popup implementation direction**

Date: 2026-09-11

Related: [select popup proposal](select-popup-proposal.md), [issue #44](https://github.com/SceneTech/WebScene/issues/44)

## Decision

WebScene popup controls use two independent abstractions:

1. **Popup content/presentation** — what is rendered inside the popup.
2. **Popup hosting** — where the popup rendering surface lives and how it is positioned/composited by the application host.

For HTML `<select>` dropdowns, WebScene owns the control behavior and the visual presentation. The dropdown content is rendered by WebScene from its private compiled HTML/CSS template and native C++ control code.

The default popup host is an **in-surface host**. It allocates an engine-owned overlay surface/layer inside the existing WebScene presentation surface.

A native application host such as AppScene may instead provide a **platform popup host**. On macOS, for example, that host may create a real popup/panel window positioned outside the main application surface. That native popup window does **not** imply a native `NSMenu`, `NSPopUpButton`, or other operating-system dropdown control. It contains a WebScene rendering surface, and WebScene renders the same HTML/CSS-templated dropdown into that surface.

Therefore the architectural rule is:

> **Hosting may be native; dropdown rendering remains WebScene-rendered.**

The popup service must not couple the dropdown behavior or template to AppScene, Cocoa, Win32, X11/Wayland, Avalonia, Uno, or another host implementation.

## Conceptual architecture

```text
<select> / <option> / <optgroup>
             |
   shared native C++ select controller
             |
    WebScene dropdown presenter
    compiled HTML/CSS template
             |
        popup host service
          /       \
         /         \
 in-surface host   platform popup host
 WebScene overlay  AppScene / other host
         |              |
 existing scene     native popup window/surface
 rendering          containing WebScene rendering
```

The popup host receives presentation-surface requirements and geometry/lifetime commands. It does not receive responsibility for select semantics, option state, keyboard navigation, event dispatch, styling, or templating.

The same presenter should be usable with either host wherever practical. Switching from an in-surface host to an AppScene platform popup host must not require application markup changes and must not produce a different DOM/control implementation.

## Why

### Preserve the HTML/CSS native-application model

AppScene/WebScene applications should be able to design controls as HTML/CSS while executing control behavior in native code. Delegating dropdown visuals to an OS control would make appearance platform-dependent and would prevent the application/control template from participating naturally in the same design language.

A real platform popup window is still useful because it can escape the bounds and clipping of the main WebScene surface. Separating hosting from rendering gives that capability without surrendering presentation to the operating system.

### Keep headless and embedded hosts viable

The in-surface implementation remains a complete default. WebScene does not require AppScene or a native windowing API to make `<select>` usable. Headless rendering and input tests can exercise the same control presentation.

Hosts that can provide out-of-surface popup windows can improve desktop integration without replacing the control itself.

### Keep behavior identical across hosts

Select state, pending highlight, disabled options, keyboard/type-ahead behavior, commit/cancel, mutation safety, and DOM events remain controlled by one WebScene implementation. A host is not allowed to invent another selection model merely because it owns the popup window.

## Popup host service responsibilities

The popup-host abstraction should be deliberately narrow. It may be responsible for:

- creating and destroying a popup presentation surface;
- presenting a WebScene-produced scene/render target in that surface;
- positioning the surface relative to the anchor and available screen/work-area bounds;
- providing display scale and other surface geometry needed by WebScene;
- routing pointer, wheel and keyboard input for the popup surface back into WebScene;
- reporting platform-driven dismissal, focus loss, movement, resize and display changes;
- enforcing popup/window stacking appropriate to the host;
- allowing WebScene to invalidate/request frames;
- lifetime/generation validation at the service boundary.

It must not own:

- the `<select>` value or selected option;
- option identity or disabled-state rules;
- type-ahead or keyboard-navigation algorithms;
- DOM event generation;
- dropdown row markup;
- CSS styling or theme selection;
- application-specific dropdown behavior.

The service should be optional. If no host implementation is supplied, WebScene uses its in-surface implementation.

## Rendering into a platform popup

A platform popup window should contain a lightweight WebScene presentation surface rather than another independent document/runtime unless isolation requires one. The preferred model is a popup scene/view owned by the same document/controller, with explicit overlay ownership and event routing.

The implementation must avoid duplicating application JavaScript runtimes, duplicating authoritative DOM state, or serializing the dropdown into a second browser-like document.

The platform popup host may choose an OS-specific window primitive — for example a borderless/popup panel on macOS — but that window is a container for WebScene rendering.

This means CSS/template changes to the built-in dropdown should behave consistently whether the dropdown is in-surface or hosted in a native popup window.

## Native controls remain a separate optional presenter class

This decision does **not** ban native operating-system controls.

Native controls/presenters remain an intentional option where native platform behavior is itself valuable. Menus and context menus are strong candidates because operating systems provide meaningful platform integration beyond simple drawing, including conventional menu interaction, global/menu-bar placement, keyboard conventions, accessibility behavior and platform services.

Possible future examples include:

- application/menu-bar menus;
- context menus;
- system share/service menus;
- platform file/color/font pickers where using the OS facility is desirable;
- other controls explicitly configured to use a native presenter.

Those should use a separate presenter capability, not be conflated with the popup-host service.

In other words:

- **Popup host** answers: *where does a WebScene-rendered popup live?*
- **Presenter/control implementation** answers: *who renders and implements the control?*

A native popup host does not automatically select a native control presenter.

## Select-specific policy

For collapsed HTML `<select>` in the implementation tracked by #44:

- the built-in presenter is WebScene-rendered;
- the same private HTML/CSS template and C++ behavior should be used by in-surface and platform popup hosts;
- in-surface hosting is the mandatory fallback/default;
- AppScene may provide a native popup-window host once the service contract exists;
- applications do not need different markup for the two hosting modes;
- using an OS-native dropdown control is not part of #44 and should require an explicit future design decision/configuration.

## Consequences

### Positive

- WebScene gets a complete standalone implementation.
- AppScene can later provide true out-of-surface desktop popups without redesigning controls.
- Dropdown appearance stays under WebScene HTML/CSS templating.
- Native and hybrid applications share one control behavior implementation.
- Headless testing can use the in-surface host.
- Platform popup-window work becomes infrastructure reusable by future WebScene-rendered popovers, tooltips, autocomplete panels and similar transient surfaces.

### Costs

- The platform popup host must embed/composite a WebScene rendering surface rather than delegating everything to an OS menu API.
- Input/focus coordination across multiple native windows/surfaces requires an explicit contract.
- Accessibility ownership must remain clear when semantic ownership is in WebScene but the surface is hosted by another native window.
- Surface creation, GPU resource sharing/retirement and display-scale transitions need qualification per platform.

These costs are preferred to maintaining independent dropdown implementations and appearances for every host.

## Non-goals of the initial select implementation

This decision does not require the first #44 implementation to ship an AppScene/macOS out-of-surface host. The first implementation may use only the default in-surface host, provided the boundary is designed so a platform host can be added without rewriting select behavior or presentation.

It also does not define a public application-authored control-template framework. The built-in select template remains private while the control architecture is proven.

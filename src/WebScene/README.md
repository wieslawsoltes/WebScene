# WebScene

WebScene is an HTML-inspired markup layer for [Avalonia](https://avaloniaui.net/). It lets you describe user interfaces with familiar tags (headings, paragraphs, lists, sections, canvas, etc.) and renders them as native Avalonia controls. WebScene pairs naturally with the companion [`JavaScript.Avalonia`](../JavaScript.Avalonia/README.md) library, but can also be consumed on its own when you only need declarative markup.

## Highlights

- **HTML-like markup**: Use tags such as `<section>`, `<nav>`, `<ul>`, or `<canvas>` inside `.axaml` files.
- **CSS-inspired styling**: Apply classes and inline styles that map onto Avalonia `Classes` and property setters.
- **Canvas support**: Built-in `<canvas>` element with a 2D rendering API mirroring the familiar HTML5 canvas surface.
- **JavaScript bridge**: When combined with `JavaScript.Avalonia`, scripts can query/mutate WebScene elements just like DOM nodes.
- **Extensible codebase**: Elements derive from Avalonia controls, so you can augment or add new tags by inheriting from `HtmlElementBase`.

## Getting Started

```bash
# Add WebScene to your solution (project reference)
<ItemGroup>
  <ProjectReference Include="..\src\WebScene\WebScene.csproj" />
</ItemGroup>
```

Import the namespace in XAML and begin authoring HTML-flavoured markup:

```xml
<html xmlns="https://github.com/avaloniaui"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      xmlns:html="clr-namespace:WebScene"
      x:Class="Demo.index">
  <head>
    <link rel="stylesheet" href="avares://Demo/Assets/site.css" type="text/css" />
  </head>
  <body class="app-body" scroll="auto">
    <section class="card">
      <h1>Hello WebScene</h1>
      <p>This section was rendered with WebScene markup.</p>
      <canvas id="draw" width="400" height="200" class="card" />
    </section>
  </body>
</html>
```

## JavaScript Integration

Combine WebScene with `AvaloniaBrowserHost` and `ClearScriptV8Runtime` to run JavaScript
against WebScene elements through DOM-style APIs:

```js
const canvas = document.getElementById('draw');
const ctx = canvas.getContext('2d');
canvas.addEventListener('pointermove', evt => {
  ctx.lineTo(evt.x, evt.y);
  ctx.stroke();
});
```

## Supported Tags (core set)

- `html`, `head`, `body`, `title`
- Headings `h1`–`h6`
- Text elements: `p`, `span`, `strong`, `em`, `code`
- Lists: `ul`, `ol`, `li`
- Structural: `section`, `article`, `nav`, `aside`, `header`, `footer`
- Media: `img`, `canvas`, `hr`, `br`
- Links and scripts: `a`, `link`, `style`, `script`

Each WebScene element corresponds to an Avalonia control (e.g., `<p>` → `TextBlock`, `<nav>` → `StackPanel`).

## Extending WebScene

To create a custom tag, inherit from `HtmlElementBase` or an appropriate Avalonia base control, add dependency properties for attributes, and register the class in your project. WebScene's design mirrors Avalonia's property system, so custom elements participate naturally in styling and layout.

## Samples & Documentation

- [`samples/website`](../../samples/website) – Comprehensive WebScene demo with markup, styles, JavaScript, and canvas interactions.
- [`src/JavaScript.Avalonia`](../JavaScript.Avalonia) – Documentation for the JavaScript bridge.
- [`README.md`](../../README.md) – Repository-level overview and contribution guidelines.

## License

WebScene uses an MIT-based license with an additional Restricted Party Clause.
See [LICENSE](../../LICENSE) for the full terms.

## Acknowledgements

- [AvaloniaUI](https://github.com/AvaloniaUI/Avalonia) – cross-platform UI framework.
- [AngleSharp](https://anglesharp.github.io/) – HTML/CSS parsing.
- [ClearScript](https://github.com/microsoft/ClearScript) – V8 hosting layer used by the scripting adapter.

---

© Wiesław Šoltés. All rights reserved.

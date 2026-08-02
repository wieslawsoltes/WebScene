# WebScene Avalonia authoring layer

The `WebScene` package is an HTML-inspired markup layer for Avalonia. It maps familiar
tag names and CSS-inspired properties onto Avalonia controls. It is a separate authoring
surface, not the native JavaScript engine, browser compatibility layer, or general
component host.

## Highlights

- HTML-like XAML tags such as `section`, `nav`, `ul`, headings, and `canvas`.
- CSS-inspired classes and inline style mapping.
- Canvas 2D drawing support.
- Extensible Avalonia controls derived from `HtmlElementBase`.

## Example

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
      <p>This section is rendered with Avalonia controls.</p>
      <canvas id="draw" width="400" height="200" class="card" />
    </section>
  </body>
</html>
```

The core tag set includes document/body elements, headings, text, lists, structural
elements, images, Canvas, links, style, and script tags. Each authoring element maps to
an Avalonia control or layout surface.

See `samples/website` for the authoring-layer demo. Native JavaScript runtime examples
are under `samples/Native*` and use the native scene engine instead of this package's
former managed scripting integration.

## License

The package carries the repository custom source-available license and Restricted Party
Clause. See [LICENSE](../../LICENSE).

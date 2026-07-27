using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;

namespace JavaScript.Avalonia;

/// <summary>
/// Normalized DOM/Avalonia snapshot used by the CDP comparison tooling. The
/// schema deliberately contains only stable browser and visual-tree facts.
/// </summary>
public sealed class WebSceneVisualSnapshot
{
    public string CapturedAtUtc { get; init; } = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture);
    public List<WebSceneDocumentSnapshot> Documents { get; init; } = new();
}

public sealed class WebSceneDocumentSnapshot
{
    public string Frame { get; init; } = "top";
    public int StylesheetNodeCount { get; init; }
    public int StyleRuleCount { get; init; }
    public string[] StylesheetSources { get; init; } = [];
    public List<WebSceneResourceTimelineEntry> ResourceTimeline { get; init; } = new();
    public List<WebSceneNodeSnapshot> Nodes { get; init; } = new();
}

public sealed class WebSceneResourceTimelineEntry
{
    public double TimeMilliseconds { get; init; }
    public string Kind { get; init; } = string.Empty;
    public string ResourceType { get; init; } = string.Empty;
    public string Source { get; init; } = string.Empty;
    public string Outcome { get; init; } = string.Empty;
}

public sealed class WebSceneNodeSnapshot
{
    public int NodeId { get; init; }
    public int? ParentNodeId { get; init; }
    public string TagName { get; init; } = string.Empty;
    public string Id { get; init; } = string.Empty;
    public string ClassName { get; init; } = string.Empty;
    public string DomPath { get; init; } = string.Empty;
    public string ControlType { get; init; } = string.Empty;
    public double? ExplicitWidth { get; init; }
    public double? ExplicitHeight { get; init; }
    public WebSceneSizeSnapshot DesiredSize { get; init; } = new();
    public string HorizontalAlignment { get; init; } = string.Empty;
    public string VerticalAlignment { get; init; } = string.Empty;
    public string ContainingBlockPath { get; init; } = string.Empty;
    public WebSceneRectSnapshot DomRect { get; init; } = new();
    public WebSceneRectSnapshot AvaloniaBounds { get; init; } = new();
    public string Display { get; init; } = string.Empty;
    public string Position { get; init; } = string.Empty;
    public string CssWidth { get; init; } = string.Empty;
    public string CssHeight { get; init; } = string.Empty;
    public string CssLeft { get; init; } = string.Empty;
    public string CssTop { get; init; } = string.Empty;
    public string CssRight { get; init; } = string.Empty;
    public string CssBottom { get; init; } = string.Empty;
    public string CssPaddingTop { get; init; } = string.Empty;
    public string CssPaddingRight { get; init; } = string.Empty;
    public string CssPaddingBottom { get; init; } = string.Empty;
    public string CssPaddingLeft { get; init; } = string.Empty;
    public string CssBorderTopWidth { get; init; } = string.Empty;
    public string CssBorderRightWidth { get; init; } = string.Empty;
    public string CssBorderBottomWidth { get; init; } = string.Empty;
    public string CssBorderLeftWidth { get; init; } = string.Empty;
    public string CssColor { get; init; } = string.Empty;
    public string CssBackgroundColor { get; init; } = string.Empty;
    public string WhiteSpace { get; init; } = string.Empty;
    public string LayoutWidth { get; init; } = string.Empty;
    public string LayoutHeight { get; init; } = string.Empty;
    public string Overflow { get; init; } = string.Empty;
    public string BoxSizing { get; init; } = string.Empty;
    public string FlexDirection { get; init; } = string.Empty;
    public string FlexGrow { get; init; } = string.Empty;
    public string FlexShrink { get; init; } = string.Empty;
    public string Visibility { get; init; } = string.Empty;
    public double Opacity { get; init; }
    public bool IsVisible { get; init; }
    public bool ClipToBounds { get; init; }
    public int ZIndex { get; init; }
    public double? CanvasWidth { get; init; }
    public double? CanvasHeight { get; init; }
    public int? CanvasCommandCount { get; init; }
    public string? CanvasRenderChecksum { get; init; }
    public string? CanvasFont { get; init; }
    public Dictionary<string, double>? CanvasSampleTextWidths { get; init; }
    public List<WebSceneCanvasTextMeasurementSnapshot>? CanvasTextMeasurements { get; init; }
}

public sealed class WebSceneCanvasTextMeasurementSnapshot
{
    public string Text { get; init; } = string.Empty;
    public string Font { get; init; } = string.Empty;
    public double Width { get; init; }
}

public sealed class WebSceneSizeSnapshot
{
    public double Width { get; init; }
    public double Height { get; init; }
}

public sealed class WebSceneRectSnapshot
{
    public double X { get; init; }
    public double Y { get; init; }
    public double Width { get; init; }
    public double Height { get; init; }
}

public sealed class WebSceneScreenshot
{
    public string Data { get; init; } = string.Empty;
    public int Width { get; init; }
    public int Height { get; init; }
}

internal static class WebSceneDiagnostics
{
    public static WebSceneVisualSnapshot Capture(AvaloniaDomDocument document)
    {
        document.FlushPendingLayout();
        var result = new WebSceneVisualSnapshot();
        CaptureDocument(document, "top", result);
        return result;
    }

    public static WebSceneScreenshot CaptureScreenshot(AvaloniaDomDocument document)
    {
        document.FlushPendingLayout();
        if (document.HostTopLevel.Content is not Control content)
        {
            return new WebSceneScreenshot();
        }

        var size = content.Bounds.Size;
        if (size.Width <= 0 || size.Height <= 0)
        {
            var clientSize = document.HostViewportMetrics.ClientSize;
            size = new Size(clientSize.Width, clientSize.Height);
        }

        var width = Math.Max(1, (int)Math.Ceiling(size.Width));
        var height = Math.Max(1, (int)Math.Ceiling(size.Height));
        using var bitmap = new RenderTargetBitmap(new PixelSize(width, height), new Vector(96, 96));
        bitmap.Render(content);
        using var stream = new MemoryStream();
        bitmap.Save(stream);
        return new WebSceneScreenshot
        {
            Data = Convert.ToBase64String(stream.ToArray()),
            Width = width,
            Height = height
        };
    }

    private static void CaptureDocument(
        AvaloniaDomDocument document,
        string frame,
        WebSceneVisualSnapshot destination)
    {
        document.EnsureStylesCurrent();
        var snapshot = new WebSceneDocumentSnapshot
        {
            Frame = frame,
            StylesheetNodeCount = document.StylesheetNodeCount,
            StyleRuleCount = document.StyleRuleCount,
            StylesheetSources = document.StylesheetSources,
            ResourceTimeline = document.ResourceTimeline.ToList()
        };
        var ids = new Dictionary<Control, int>();
        var paths = new Dictionary<Control, string>();
        var elements = document.EnumerateDocumentElements().ToArray();

        for (var index = 0; index < elements.Length; index++)
        {
            var element = elements[index];
            var control = element.Control;
            var nodeId = index + 1;
            ids[control] = nodeId;
            var parent = control.Parent as Control;
            int? parentId = parent is not null && ids.TryGetValue(parent, out var knownParent) ? knownParent : null;
            var siblingIndex = parent is Panel panel ? panel.Children.IndexOf(control) : 0;
            var parentPath = parent is not null && paths.TryGetValue(parent, out var knownPath) ? knownPath : string.Empty;
            var path = $"{parentPath}/{element.tagName.ToLowerInvariant()}[{Math.Max(0, siblingIndex)}]";
            paths[control] = path;
            var containingBlock = FindContainingBlock(control);
            var containingBlockPath = containingBlock is not null && paths.TryGetValue(containingBlock, out var blockPath)
                ? blockPath
                : string.Empty;

            var rect = element.getBoundingClientRect();
            var computed = document.getComputedStyle(element);
            double? canvasWidth = null;
            double? canvasHeight = null;
            int? commandCount = null;
            string? checksum = null;
            string? canvasFont = null;
            Dictionary<string, double>? canvasSampleTextWidths = null;
            List<WebSceneCanvasTextMeasurementSnapshot>? canvasTextMeasurements = null;
            if (control is CanvasDrawingSurface canvas)
            {
                canvasWidth = element.width;
                canvasHeight = element.height;
                commandCount = canvas.LogicalCommandCount;
                var hash = new HashCode();
                foreach (var command in canvas.Commands)
                {
                    command.AppendDiagnosticHash(ref hash);
                }
                checksum = hash.ToHashCode().ToString("x8", CultureInfo.InvariantCulture);
                canvasFont = canvas.Context.font;
                canvasSampleTextWidths = new Dictionary<string, double>(StringComparer.Ordinal);
                foreach (var sample in new[] { "0", "100.00", "102.79", "Volume" })
                {
                    var metrics = (CanvasTextMetrics)canvas.Context.measureText(sample);
                    canvasSampleTextWidths[sample] = metrics.width;
                }
                canvasTextMeasurements = canvas.TextMeasurements.Select(measurement => new WebSceneCanvasTextMeasurementSnapshot
                {
                    Text = measurement.Text,
                    Font = measurement.Font,
                    Width = measurement.Width
                }).ToList();
            }

            snapshot.Nodes.Add(new WebSceneNodeSnapshot
            {
                NodeId = nodeId,
                ParentNodeId = parentId,
                TagName = element.tagName.ToLowerInvariant(),
                Id = element.id,
                ClassName = element.className,
                DomPath = path,
                ControlType = control.GetType().Name,
                ExplicitWidth = double.IsFinite(control.Width) ? control.Width : null,
                ExplicitHeight = double.IsFinite(control.Height) ? control.Height : null,
                DesiredSize = new WebSceneSizeSnapshot
                {
                    Width = control.DesiredSize.Width,
                    Height = control.DesiredSize.Height
                },
                HorizontalAlignment = control.HorizontalAlignment.ToString(),
                VerticalAlignment = control.VerticalAlignment.ToString(),
                ContainingBlockPath = containingBlockPath,
                DomRect = new WebSceneRectSnapshot { X = rect.x, Y = rect.y, Width = rect.width, Height = rect.height },
                AvaloniaBounds = new WebSceneRectSnapshot
                {
                    X = control.Bounds.X,
                    Y = control.Bounds.Y,
                    Width = control.Bounds.Width,
                    Height = control.Bounds.Height
                },
                Display = computed.getPropertyValue("display") ?? string.Empty,
                Position = computed.getPropertyValue("position") ?? string.Empty,
                CssWidth = computed.getPropertyValue("width") ?? string.Empty,
                CssHeight = computed.getPropertyValue("height") ?? string.Empty,
                CssLeft = computed.getPropertyValue("left") ?? string.Empty,
                CssTop = computed.getPropertyValue("top") ?? string.Empty,
                CssRight = computed.getPropertyValue("right") ?? string.Empty,
                CssBottom = computed.getPropertyValue("bottom") ?? string.Empty,
                CssPaddingTop = computed.getPropertyValue("padding-top") ?? string.Empty,
                CssPaddingRight = computed.getPropertyValue("padding-right") ?? string.Empty,
                CssPaddingBottom = computed.getPropertyValue("padding-bottom") ?? string.Empty,
                CssPaddingLeft = computed.getPropertyValue("padding-left") ?? string.Empty,
                CssBorderTopWidth = computed.getPropertyValue("border-top-width") ?? string.Empty,
                CssBorderRightWidth = computed.getPropertyValue("border-right-width") ?? string.Empty,
                CssBorderBottomWidth = computed.getPropertyValue("border-bottom-width") ?? string.Empty,
                CssBorderLeftWidth = computed.getPropertyValue("border-left-width") ?? string.Empty,
                CssColor = computed.getPropertyValue("color") ?? string.Empty,
                CssBackgroundColor = computed.getPropertyValue("background-color") ?? string.Empty,
                WhiteSpace = computed.getPropertyValue("white-space") ?? string.Empty,
                LayoutWidth = FormatLength(CssLayout.GetWidth(control)),
                LayoutHeight = FormatLength(CssLayout.GetHeight(control)),
                Overflow = computed.getPropertyValue("overflow") ?? string.Empty,
                BoxSizing = computed.getPropertyValue("box-sizing") ?? string.Empty,
                FlexDirection = computed.getPropertyValue("flex-direction") ?? string.Empty,
                FlexGrow = computed.getPropertyValue("flex-grow") ?? string.Empty,
                FlexShrink = computed.getPropertyValue("flex-shrink") ?? string.Empty,
                Visibility = computed.getPropertyValue("visibility") ?? string.Empty,
                Opacity = control.Opacity,
                IsVisible = control.IsVisible,
                ClipToBounds = control.ClipToBounds,
                ZIndex = control.GetValue(Canvas.ZIndexProperty),
                CanvasWidth = canvasWidth,
                CanvasHeight = canvasHeight,
                CanvasCommandCount = commandCount,
                CanvasRenderChecksum = checksum,
                CanvasFont = canvasFont,
                CanvasSampleTextWidths = canvasSampleTextWidths,
                CanvasTextMeasurements = canvasTextMeasurements
            });
        }

        destination.Documents.Add(snapshot);
        var frameIndex = 0;
        foreach (var iframe in elements.Where(element => string.Equals(element.tagName, "IFRAME", StringComparison.OrdinalIgnoreCase)))
        {
            if (iframe.contentDocument is AvaloniaDomDocument frameDocument)
            {
                CaptureDocument(frameDocument, $"{frame}/iframe:{frameIndex}", destination);
            }
            frameIndex++;
        }
    }

    private static Control? FindContainingBlock(Control control)
    {
        var current = control.Parent as Control;
        while (current is not null)
        {
            if (current is CssLayoutPanel
                && (CssLayout.GetPosition(current) != CssPosition.Static || current.Parent is not CssLayoutPanel))
            {
                return current;
            }

            current = current.Parent as Control;
        }

        return null;
    }

    private static string FormatLength(CssLength? length)
    {
        if (!length.HasValue) return "unset";
        return length.Value.Unit switch
        {
            CssLengthUnit.Percent => length.Value.Value.ToString("0.###", CultureInfo.InvariantCulture) + "%",
            CssLengthUnit.Pixel => length.Value.Value.ToString("0.###", CultureInfo.InvariantCulture) + "px",
            _ => "auto"
        };
    }
}

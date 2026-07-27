using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CalendarDialogDeltaSpikeTests
{
    [AvaloniaFact]
    public void ProgrammaticInputFocusDispatchesFocusThenBubblingFocusIn()
    {
        var root = new CssLayoutPanel { Width = 160, Height = 40 };
        var window = new Window { Width = 160, Height = 40, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var parent = Append(document, "div", "compound-control");
        var input = Append(document, "input", null, parent);
        var sequence = new List<string>();
        input.__webSceneAddExternalEventListener(
            "focus",
            new FocusSequenceListener("focus@input", sequence),
            capture: false,
            once: false,
            passive: false);
        input.__webSceneAddExternalEventListener(
            "focusin",
            new FocusSequenceListener("focusin@input", sequence),
            capture: false,
            once: false,
            passive: false);
        parent.__webSceneAddExternalEventListener(
            "focusin",
            new FocusSequenceListener("focusin@parent", sequence),
            capture: false,
            once: false,
            passive: false);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        Assert.True(input.focus());
        Dispatcher.UIThread.RunJobs();

        Assert.Same(input, document.activeElement);
        Assert.Equal(["focus@input", "focusin@input", "focusin@parent"], sequence);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void ActualDateAndTimeInputsStayInsideLiveStyleCompoundControlsInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 302, Height = 80 };
        var window = new Window { Width = 302, Height = 80, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 80px; margin: 0; width: 302px; }
            .row {
                box-sizing: border-box;
                column-gap: 12px;
                display: grid;
                grid-template-columns: 150px 100px;
                padding: 0 20px;
                width: 302px;
            }
            .control {
                align-items: center;
                border: 1px solid #777;
                box-sizing: border-box;
                display: inline-flex;
                height: 28px;
            }
            .middle {
                display: flex;
                flex: 1 1 auto;
                min-width: 0;
                overflow: hidden;
            }
            input {
                border: 0;
                box-sizing: border-box;
                display: block;
                height: 26px;
                min-width: 0;
                padding: 2px;
                width: 100%;
            }
            .slot {
                flex: none;
                height: 24px;
                margin-right: 2px;
                width: 28px;
            }
            """;
        document.head.appendChild(style);

        var row = Append(document, "div", "row");
        var date = Append(document, "div", "control", row);
        var dateMiddle = Append(document, "span", "middle", date);
        var dateInput = Append(document, "input", null, dateMiddle);
        dateInput.setAttribute("type", "date");
        dateInput.value = "2026-07-16";
        var dateSlot = Append(document, "span", "slot", date);
        var time = Append(document, "div", "control", row);
        var timeMiddle = Append(document, "span", "middle", time);
        var timeInput = Append(document, "input", null, timeMiddle);
        timeInput.setAttribute("type", "time");
        timeInput.value = "00:00";
        var timeSlot = Append(document, "span", "slot", time);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("date", dateInput.type);
        Assert.Equal("time", timeInput.type);
        Assert.True(Assert.IsType<DomTextInputControl>(dateInput.Control).UsesTextIntrinsicSize);
        Assert.True(Assert.IsType<DomTextInputControl>(timeInput.Control).UsesTextIntrinsicSize);
        AssertControlRow(row, date, dateInput, dateSlot, time, timeInput, timeSlot);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(row.Control),
            new Size(302, 28));
        Assert.Equal(150, portable.GetBox(date.Control).BorderBox.Width);
        Assert.Equal(100, portable.GetBox(time.Control).BorderBox.Width);
        Assert.Equal(182, portable.GetBox(time.Control).BorderBox.X);
        Assert.True(portable.GetBox(dateSlot.Control).BorderBox.Right <= portable.GetBox(date.Control).BorderBox.Right);
        Assert.True(portable.GetBox(timeSlot.Control).BorderBox.Right <= portable.GetBox(time.Control).BorderBox.Right);

        foreach (var panel in new[] { row.Control, date.Control, dateMiddle.Control, time.Control, timeMiddle.Control }
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        root.InvalidateMeasure();
        root.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
        AssertControlRow(row, date, dateInput, dateSlot, time, timeInput, timeSlot);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void CurrentDayUnderlineAndDisabledFutureDayPaintFromAuthoredPseudoStates()
    {
        var root = new CssLayoutPanel { Width = 72, Height = 34 };
        var window = new Window { Width = 72, Height = 34, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 34px; margin: 0; width: 72px; }
            body { display: flex; gap: 4px; }
            .day {
                background-color: transparent;
                border: 1px solid transparent;
                box-sizing: border-box;
                color: #dbdbdb;
                font-weight: 400;
                height: 34px;
                padding: 0;
                width: 34px;
            }
            .day.selected { background-color: #f2f2f2; color: #000; font-weight: 600; }
            .day.current { position: relative; }
            .day.current::before {
                background: currentColor;
                border-radius: 1px;
                content: "";
                height: 2px;
                left: 6px;
                margin-top: -6px;
                position: absolute;
                right: 6px;
                top: 100%;
            }
            .day:disabled { background-color: transparent; color: #575757; }
            """;
        document.head.appendChild(style);
        var current = Append(document, "button", "day current selected");
        current.textContent = "16";
        var future = Append(document, "button", "day");
        future.textContent = "17";
        future.disabled = true;

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("rgb(0, 0, 0)", document.getComputedStyle(current).getPropertyValue("color"));
        Assert.Equal("rgb(87, 87, 87)", document.getComputedStyle(future).getPropertyValue("color"));
        Assert.Equal("600", document.getComputedStyle(current).getPropertyValue("font-weight"));
        Assert.Equal("400", document.getComputedStyle(future).getPropertyValue("font-weight"));
        var currentPanel = Assert.IsType<DomButtonControl>(current.Control);
        var futurePanel = Assert.IsType<DomButtonControl>(future.Control);
        var currentText = Assert.IsType<DomTextBlockControl>(
            Assert.IsType<AvaloniaDomTextNode>(current.firstChild).Control);
        var futureText = Assert.IsType<DomTextBlockControl>(
            Assert.IsType<AvaloniaDomTextNode>(future.firstChild).Control);
        Assert.Equal(Colors.Black,
            Assert.IsAssignableFrom<ISolidColorBrush>(currentText.Foreground).Color);
        Assert.Equal(600, (int)currentText.FontWeight);
        Assert.False(futurePanel.IsEnabled);
        Assert.Equal(Color.Parse("#575757"),
            Assert.IsAssignableFrom<ISolidColorBrush>(futureText.Foreground).Color);
        Assert.Equal(400, (int)futureText.FontWeight);
        var underline = Assert.IsType<CssGeneratedPseudoElement>(currentPanel.BeforePseudoElement);
        Assert.Equal(new Rect(7, 27, 20, 2), underline.ResolveRect(new Rect(1, 1, 32, 32)));
        Assert.Equal(Colors.Black, Assert.IsAssignableFrom<ISolidColorBrush>(underline.Background).Color);
        var portable = AvaloniaCssLayoutProjection.Capture(currentPanel, new Size(34, 34));
        Assert.True(portable.TryGetPseudoBox(currentPanel, before: true, out var portableUnderline));
        Assert.Equal(
            (7d, 27d, 20d, 2d),
            (portableUnderline.BorderBox.X, portableUnderline.BorderBox.Y,
                portableUnderline.BorderBox.Width, portableUnderline.BorderBox.Height));

        current.Control.Measure(new Size(34, 34));
        current.Control.Arrange(new Rect(0, 0, 34, 34));
        using var frame = new RenderTargetBitmap(new PixelSize(34, 34), new Vector(96, 96));
        frame.Render(current.Control);
        Assert.Equal(Colors.Black, ReadPixel(frame, 10, 27));
        Assert.Equal(Color.Parse("#f2f2f2"), ReadPixel(frame, 10, 26));

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void CalendarShellKeepsReferenceSectionsAndFooterActionsContainedInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 302, Height = 566 };
        var window = new Window { Width = 302, Height = 566, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 566px; margin: 0; width: 302px; }
            .dialog {
                background: #202020;
                border-radius: 6px;
                box-sizing: border-box;
                height: 566px;
                overflow: hidden;
                width: 302px;
            }
            .header {
                align-items: center;
                box-sizing: border-box;
                display: flex;
                height: 68px;
                justify-content: space-between;
                padding: 0 20px;
            }
            .tabs { box-sizing: border-box; height: 32px; padding: 0 20px; }
            .content { box-sizing: border-box; height: 399px; padding: 20px; }
            .footer {
                align-items: center;
                border-top: 1px solid #434343;
                box-sizing: border-box;
                display: flex;
                gap: 12px;
                height: 67px;
                justify-content: flex-end;
                padding: 0 20px;
            }
            .cancel, .submit {
                box-sizing: border-box;
                height: 34px;
                padding: 0;
            }
            .cancel { width: 72px; }
            .submit { width: 55px; }
            """;
        document.head.appendChild(style);

        var dialog = Append(document, "div", "dialog");
        var header = Append(document, "div", "header", dialog);
        Append(document, "span", null, header).textContent = "Go to";
        Append(document, "button", null, header).textContent = "Close";
        var tabs = Append(document, "div", "tabs", dialog);
        tabs.textContent = "Date  Custom range";
        var content = Append(document, "div", "content", dialog);
        content.textContent = "July 2026";
        var footer = Append(document, "div", "footer", dialog);
        var cancel = Append(document, "button", "cancel", footer);
        cancel.textContent = "Cancel";
        var submit = Append(document, "button", "submit", footer);
        submit.textContent = "Go to";

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        AssertCalendarShell(dialog, header, tabs, content, footer, cancel, submit);
        var portable = AvaloniaCssLayoutProjection.Capture(
            Assert.IsType<CssLayoutPanel>(dialog.Control),
            new Size(302, 566));
        AssertPortableCalendarShell(portable, dialog, header, tabs, content, footer, cancel, submit);

        foreach (var panel in new[] { dialog.Control, header.Control, tabs.Control, content.Control, footer.Control }
                     .OfType<CssLayoutPanel>())
        {
            CssLayout.SetNativeLayoutHotPath(panel, true);
            panel.InvalidateMeasure();
            panel.InvalidateArrange();
        }
        root.InvalidateMeasure();
        root.InvalidateArrange();
        Dispatcher.UIThread.RunJobs();
        AssertCalendarShell(dialog, header, tabs, content, footer, cancel, submit);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    [AvaloniaFact]
    public void CalendarTypographyPreservesAuthoredFamilySizeLineHeightAndNumericWeights()
    {
        var root = new CssLayoutPanel { Width = 302, Height = 160 };
        var window = new Window { Width = 302, Height = 160, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            html, body { height: 160px; margin: 0; width: 302px; }
            body {
                font-family: -apple-system, BlinkMacSystemFont, "Trebuchet MS", Roboto, Ubuntu, sans-serif;
            }
            .title { font-size: 20px; font-weight: 600; line-height: 24px; }
            .tab { font-size: 16px; font-weight: 600; line-height: 24px; }
            .day { font-size: 14px; font-weight: 400; line-height: 20px; }
            .day.selected { font-weight: 600; }
            .footer-action { font-size: 14px; font-weight: 400; line-height: 20px; }
            """;
        document.head.appendChild(style);

        var title = Append(document, "span", "title");
        title.textContent = "Go to";
        var tab = Append(document, "span", "tab");
        tab.textContent = "Custom range";
        var day = Append(document, "span", "day");
        day.textContent = "15";
        var selectedDay = Append(document, "span", "day selected");
        selectedDay.textContent = "16";
        var footerAction = Append(document, "span", "footer-action");
        footerAction.textContent = "Cancel";

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        var titleText = AssertTextMetrics(document, title, 20, 24, 600);
        var tabText = AssertTextMetrics(document, tab, 16, 24, 600);
        var dayText = AssertTextMetrics(document, day, 14, 20, 400);
        var selectedDayText = AssertTextMetrics(document, selectedDay, 14, 20, 600);
        var footerText = AssertTextMetrics(document, footerAction, 14, 20, 400);
        Assert.Equal(titleText.FontFamily, tabText.FontFamily);
        Assert.Equal(titleText.FontFamily, dayText.FontFamily);
        Assert.Equal(titleText.FontFamily, selectedDayText.FontFamily);
        Assert.Equal(titleText.FontFamily, footerText.FontFamily);

        window.Close();
        Dispatcher.UIThread.RunJobs();
    }

    private static void AssertControlRow(
        AvaloniaDomElement row,
        AvaloniaDomElement date,
        AvaloniaDomElement dateInput,
        AvaloniaDomElement dateSlot,
        AvaloniaDomElement time,
        AvaloniaDomElement timeInput,
        AvaloniaDomElement timeSlot)
    {
        var rowRect = row.getBoundingClientRect();
        var dateRect = date.getBoundingClientRect();
        var dateInputRect = dateInput.getBoundingClientRect();
        var dateSlotRect = dateSlot.getBoundingClientRect();
        var timeRect = time.getBoundingClientRect();
        var timeInputRect = timeInput.getBoundingClientRect();
        var timeSlotRect = timeSlot.getBoundingClientRect();
        Assert.Equal(302, rowRect.width);
        Assert.Equal((20d, 150d), (dateRect.left - rowRect.left, dateRect.width));
        Assert.Equal((182d, 100d), (timeRect.left - rowRect.left, timeRect.width));
        Assert.Equal((21d, 118d), (dateInputRect.left - rowRect.left, dateInputRect.width));
        Assert.Equal((139d, 28d), (dateSlotRect.left - rowRect.left, dateSlotRect.width));
        Assert.Equal((183d, 68d), (timeInputRect.left - rowRect.left, timeInputRect.width));
        Assert.Equal((251d, 28d), (timeSlotRect.left - rowRect.left, timeSlotRect.width));
        Assert.True(dateInputRect.left >= dateRect.left && dateInputRect.right <= dateSlotRect.left);
        Assert.True(dateSlotRect.right <= dateRect.right);
        Assert.True(timeInputRect.left >= timeRect.left && timeInputRect.right <= timeSlotRect.left);
        Assert.True(timeSlotRect.right <= timeRect.right);
    }

    private static void AssertCalendarShell(
        AvaloniaDomElement dialog,
        AvaloniaDomElement header,
        AvaloniaDomElement tabs,
        AvaloniaDomElement content,
        AvaloniaDomElement footer,
        AvaloniaDomElement cancel,
        AvaloniaDomElement submit)
    {
        var dialogRect = dialog.getBoundingClientRect();
        var headerRect = header.getBoundingClientRect();
        var tabsRect = tabs.getBoundingClientRect();
        var contentRect = content.getBoundingClientRect();
        var footerRect = footer.getBoundingClientRect();
        var cancelRect = cancel.getBoundingClientRect();
        var submitRect = submit.getBoundingClientRect();

        Assert.Equal((302d, 566d), (dialogRect.width, dialogRect.height));
        Assert.Equal((68d, 32d, 399d, 67d),
            (headerRect.height, tabsRect.height, contentRect.height, footerRect.height));
        Assert.Equal(headerRect.bottom, tabsRect.top);
        Assert.Equal(tabsRect.bottom, contentRect.top);
        Assert.Equal(contentRect.bottom, footerRect.top);
        Assert.Equal(dialogRect.bottom, footerRect.bottom);
        Assert.Equal((72d, 34d), (cancelRect.width, cancelRect.height));
        Assert.Equal((55d, 34d), (submitRect.width, submitRect.height));
        Assert.Equal(12d, submitRect.left - cancelRect.right);
        Assert.Equal(20d, footerRect.right - submitRect.right);
        Assert.True(cancelRect.top >= footerRect.top && cancelRect.bottom <= footerRect.bottom);
        Assert.True(submitRect.top >= footerRect.top && submitRect.bottom <= footerRect.bottom);
    }

    private static void AssertPortableCalendarShell(
        AvaloniaCssLayoutSnapshot portable,
        AvaloniaDomElement dialog,
        AvaloniaDomElement header,
        AvaloniaDomElement tabs,
        AvaloniaDomElement content,
        AvaloniaDomElement footer,
        AvaloniaDomElement cancel,
        AvaloniaDomElement submit)
    {
        var dialogBox = portable.GetBox(dialog.Control).BorderBox;
        var headerBox = portable.GetBox(header.Control).BorderBox;
        var tabsBox = portable.GetBox(tabs.Control).BorderBox;
        var contentBox = portable.GetBox(content.Control).BorderBox;
        var footerBox = portable.GetBox(footer.Control).BorderBox;
        var cancelBox = portable.GetBox(cancel.Control).BorderBox;
        var submitBox = portable.GetBox(submit.Control).BorderBox;

        Assert.Equal((302d, 566d), (dialogBox.Width, dialogBox.Height));
        Assert.Equal((68d, 32d, 399d, 67d),
            (headerBox.Height, tabsBox.Height, contentBox.Height, footerBox.Height));
        Assert.Equal(headerBox.Bottom, tabsBox.Top);
        Assert.Equal(tabsBox.Bottom, contentBox.Top);
        Assert.Equal(contentBox.Bottom, footerBox.Top);
        Assert.Equal(dialogBox.Bottom, footerBox.Bottom);
        Assert.Equal((72d, 34d), (cancelBox.Width, cancelBox.Height));
        Assert.Equal((55d, 34d), (submitBox.Width, submitBox.Height));
        Assert.Equal(12d, submitBox.Left - cancelBox.Right);
        Assert.Equal(20d, footerBox.Right - submitBox.Right);
    }

    private static DomTextBlockControl AssertTextMetrics(
        AvaloniaDomDocument document,
        AvaloniaDomElement element,
        double fontSize,
        double lineHeight,
        int fontWeight)
    {
        var computed = document.getComputedStyle(element);
        Assert.Equal($"{fontSize:0}px", computed.getPropertyValue("font-size"));
        Assert.Equal($"{lineHeight:0}px", computed.getPropertyValue("line-height"));
        Assert.Equal(fontWeight.ToString(), computed.getPropertyValue("font-weight"));
        var textNode = Assert.IsType<AvaloniaDomTextNode>(element.firstChild);
        var text = Assert.IsType<DomTextBlockControl>(textNode.Control);
        Assert.Equal(fontSize, text.FontSize);
        Assert.Equal(lineHeight, text.LineHeight);
        Assert.Equal(fontWeight, (int)text.FontWeight);
        Assert.Equal(lineHeight, text.Bounds.Height);
        return text;
    }

    private static AvaloniaDomElement Append(
        AvaloniaDomDocument document,
        string tag,
        string? className,
        AvaloniaDomElement? parent = null)
    {
        var element = HostTestUtilities.GetElement(document.createElement(tag));
        if (!string.IsNullOrEmpty(className))
        {
            element.className = className;
        }
        (parent ?? HostTestUtilities.GetElement(document.body)).appendChild(element);
        return element;
    }

    private static Color ReadPixel(Bitmap bitmap, int x, int y)
    {
        var bytes = new byte[4];
        var handle = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(new PixelRect(x, y, 1, 1), handle.AddrOfPinnedObject(), bytes.Length, 4);
        }
        finally
        {
            handle.Free();
        }

        return bitmap.Format == PixelFormat.Rgba8888
            ? Color.FromArgb(bytes[3], bytes[0], bytes[1], bytes[2])
            : Color.FromArgb(bytes[3], bytes[2], bytes[1], bytes[0]);
    }

    private sealed class FocusSequenceListener(string label, List<string> sequence) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent) => sequence.Add(label);
    }
}

using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssPositionedOverlayTests
{
    [AvaloniaFact]
    public void FixedSubmenuEscapesIntermediateOverflowClipAndRemainsNativeHitTestable()
    {
        var root = new CssLayoutPanel { Width = 240, Height = 120 };
        var window = new Window { Width = 240, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                html, body { height: 120px; margin: 0; width: 240px; }
                .scroll {
                  height: 80px;
                  left: 0;
                  overflow-x: hidden;
                  overflow-y: auto;
                  position: absolute;
                  top: 0;
                  width: 100px;
                }
                .submenu {
                  background-color: green;
                  height: 40px;
                  left: 100px;
                  position: fixed;
                  top: 20px;
                  width: 80px;
                }
                """;
            document.head.appendChild(style);
            var scroll = HostTestUtilities.GetElement(document.createElement("div"));
            scroll.className = "scroll";
            var submenu = HostTestUtilities.GetElement(document.createElement("div"));
            submenu.className = "submenu";
            scroll.appendChild(submenu);
            HostTestUtilities.GetElement(document.body).appendChild(scroll);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal((100d, 20d, 80d, 40d),
                (submenu.getBoundingClientRect().left, submenu.getBoundingClientRect().top,
                    submenu.getBoundingClientRect().width, submenu.getBoundingClientRect().height));
            Assert.False(scroll.Control.ClipToBounds);
            Assert.True(((CssLayoutPanel)scroll.Control).HitTest(new Point(130, 30)));

            scroll.removeChild(submenu);
            Assert.True(scroll.Control.ClipToBounds);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void NewlyInsertedFixedMenuSynchronouslyMeasuresGeneratedRowStrutsInNativeAndPortableLayout()
    {
        var root = new CssLayoutPanel { Width = 1000, Height = 616 };
        var window = new Window { Width = 1000, Height = 616, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .menu { position: fixed; visibility: hidden; }
                .box { padding: 6px 0; }
                .row { box-sizing: border-box; display: flex; padding: 2px 8px; }
                .row::before { content: " "; display: block; height: 28px; }
                .label { font-size: 14px; }
                """;
            document.head.appendChild(style);
            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var menu = HostTestUtilities.GetElement(document.createElement("div"));
            menu.className = "menu";
            var box = HostTestUtilities.GetElement(document.createElement("div"));
            box.className = "box";
            var rows = new List<AvaloniaDomElement>();
            for (var index = 0; index < 3; index++)
            {
                var row = HostTestUtilities.GetElement(document.createElement("div"));
                row.className = "row";
                rows.Add(row);
                var label = HostTestUtilities.GetElement(document.createElement("span"));
                label.className = "label";
                label.textContent = "Menu item";
                row.appendChild(label);
                box.appendChild(row);
            }
            menu.appendChild(box);
            HostTestUtilities.GetElement(document.body).appendChild(menu);

            var measuredHeight = menu.getBoundingClientRect().height;
            Assert.All(rows, row =>
            {
                var pseudo = Assert.IsType<CssGeneratedPseudoElement>(
                    Assert.IsType<CssLayoutPanel>(row.Control).BeforePseudoElement);
                Assert.False(pseudo.IsPaintVisible);
            });
            Assert.Equal(108, measuredHeight);
            Assert.Equal(
                108,
                AvaloniaCssLayoutProjection.Measure(
                    Assert.IsType<CssLayoutPanel>(menu.Control),
                    new Size(1000, double.PositiveInfinity)).Height);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void DefiniteHeightPositionedToolbarKeepsAnonymousTableFlexContentVisible()
    {
        var root = new CssLayoutPanel { Width = 500, Height = 400 };
        var window = new Window { Width = 500, Height = 400, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .toolbar { position: absolute; left: 0; right: 0; top: 0; height: 38px; }
                .content { display: table; height: 100%; }
                .inner { display: flex; height: 38px; }
                button { width: 80px; height: 100%; }
                """;
            document.head.appendChild(style);
            var toolbar = HostTestUtilities.GetElement(document.createElement("div"));
            toolbar.className = "toolbar";
            var content = HostTestUtilities.GetElement(document.createElement("div"));
            content.className = "content";
            var inner = HostTestUtilities.GetElement(document.createElement("div"));
            inner.className = "inner";
            var first = HostTestUtilities.GetElement(document.createElement("button"));
            var second = HostTestUtilities.GetElement(document.createElement("button"));
            inner.appendChild(first);
            inner.appendChild(second);
            content.appendChild(inner);
            toolbar.appendChild(content);
            HostTestUtilities.GetElement(document.body).appendChild(toolbar);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(38, toolbar.getBoundingClientRect().height);
            Assert.Equal(38, content.getBoundingClientRect().height);
            Assert.Equal(160, content.getBoundingClientRect().width);
            Assert.Equal(80, first.getBoundingClientRect().width);
            Assert.Equal(38, first.getBoundingClientRect().height);
            Assert.Equal(first.getBoundingClientRect().right, second.getBoundingClientRect().left);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void FixedAutoHeightShrinkWrapsPercentageHeightChildContent()
    {
        var root = new CssLayoutPanel { Width = 500, Height = 400 };
        var window = new Window { Width = 500, Height = 400, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .menu { position: fixed; left: 20px; top: 10px; }
                .scroll { height: 100%; }
                .row { height: 32px; width: 200px; }
                """;
            document.head.appendChild(style);
            var menu = HostTestUtilities.GetElement(document.createElement("div"));
            menu.className = "menu";
            var scroll = HostTestUtilities.GetElement(document.createElement("div"));
            scroll.className = "scroll";
            for (var index = 0; index < 4; index++)
            {
                var row = HostTestUtilities.GetElement(document.createElement("div"));
                row.className = "row";
                scroll.appendChild(row);
            }
            menu.appendChild(scroll);
            HostTestUtilities.GetElement(document.body).appendChild(menu);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(128, menu.getBoundingClientRect().height);
            Assert.Equal(128, scroll.getBoundingClientRect().height);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void DefiniteHeightColumnFlexUsesIntrinsicMainSizeForAutoHeightItems()
    {
        var root = new CssLayoutPanel { Width = 1000, Height = 616 };
        var window = new Window { Width = 1000, Height = 616, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .dialog { height: 576px; position: fixed; width: 302px; }
                .wrapper { display: flex; flex-direction: column; height: 100%; }
                .header { flex-shrink: 0; height: 68px; }
                .tabs { flex-shrink: 1; }
                .tab-scroll { height: 100%; width: 100%; }
                .tab-strip { height: 34px; }
                .content { overflow: auto; padding-top: 17px; }
                .body { height: 398px; }
                .footer { flex-shrink: 0; height: 66px; }
                """;
            document.head.appendChild(style);

            var dialog = HostTestUtilities.GetElement(document.createElement("div"));
            dialog.className = "dialog";
            var wrapper = HostTestUtilities.GetElement(document.createElement("div"));
            wrapper.className = "wrapper";
            var header = HostTestUtilities.GetElement(document.createElement("div"));
            header.className = "header";
            var tabs = HostTestUtilities.GetElement(document.createElement("div"));
            tabs.className = "tabs";
            var tabScroll = HostTestUtilities.GetElement(document.createElement("div"));
            tabScroll.className = "tab-scroll";
            var tabStrip = HostTestUtilities.GetElement(document.createElement("div"));
            tabStrip.className = "tab-strip";
            var content = HostTestUtilities.GetElement(document.createElement("div"));
            content.className = "content";
            var body = HostTestUtilities.GetElement(document.createElement("div"));
            body.className = "body";
            var footer = HostTestUtilities.GetElement(document.createElement("div"));
            footer.className = "footer";
            tabScroll.appendChild(tabStrip);
            tabs.appendChild(tabScroll);
            content.appendChild(body);
            wrapper.appendChild(header);
            wrapper.appendChild(tabs);
            wrapper.appendChild(content);
            wrapper.appendChild(footer);
            dialog.appendChild(wrapper);
            HostTestUtilities.GetElement(document.body).appendChild(dialog);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var dialogRect = dialog.getBoundingClientRect();
            var tabsRect = tabs.getBoundingClientRect();
            var contentRect = content.getBoundingClientRect();
            var bodyRect = body.getBoundingClientRect();
            var footerRect = footer.getBoundingClientRect();
            Assert.Equal(34, tabsRect.height);
            Assert.Equal(408, contentRect.height);
            Assert.Equal(header.getBoundingClientRect().bottom, tabsRect.top);
            Assert.Equal(tabsRect.bottom, contentRect.top);
            Assert.Equal(contentRect.bottom, footerRect.top);
            Assert.Equal(dialogRect.bottom, footerRect.bottom);
            Assert.Equal(contentRect.top + 17, bodyRect.top);
            Assert.True(bodyRect.top < dialogRect.bottom);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void FixedHintUsesCustomPropertyFallbackWidthStaysContainedAndDismissesByNativeClick()
    {
        var root = new CssLayoutPanel { Width = 1000, Height = 616 };
        var window = new Window { Width = 1000, Height = 616, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .hint { position: fixed; left: 402px; top: 315px; }
                .container { display: flex; overflow: auto; padding: 8px; }
                .content { padding: 4px 8px; }
                .wrapper { width: var(--hint-wizard-width, 220px); }
                .text { font-size: 14px; line-height: 21px; }
                .buttons { display: flex; justify-content: flex-end; }
                .dismiss { height: 28px; margin-top: 15px; width: 58px; }
                """;
            document.head.appendChild(style);
            var hint = HostTestUtilities.GetElement(document.createElement("div"));
            hint.className = "hint";
            var container = HostTestUtilities.GetElement(document.createElement("div"));
            container.className = "container";
            var content = HostTestUtilities.GetElement(document.createElement("div"));
            content.className = "content";
            var wrapper = HostTestUtilities.GetElement(document.createElement("div"));
            wrapper.className = "wrapper";
            var text = HostTestUtilities.GetElement(document.createElement("div"));
            text.className = "text";
            text.textContent = "Press and hold to see detailed chart values";
            var buttons = HostTestUtilities.GetElement(document.createElement("div"));
            buttons.className = "buttons";
            var dismiss = HostTestUtilities.GetElement(document.createElement("button"));
            dismiss.className = "dismiss";
            dismiss.textContent = "Got it!";
            dismiss.__webSceneAddExternalEventListener(
                "click",
                new RemoveElementListener(hint),
                capture: false,
                once: true,
                passive: false);
            buttons.appendChild(dismiss);
            wrapper.appendChild(text);
            wrapper.appendChild(buttons);
            content.appendChild(wrapper);
            container.appendChild(content);
            hint.appendChild(container);
            HostTestUtilities.GetElement(document.body).appendChild(hint);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var viewport = new Rect(
                0,
                0,
                document.documentElement.clientWidth,
                document.documentElement.clientHeight);
            var hintRect = hint.getBoundingClientRect();
            var wrapperRect = wrapper.getBoundingClientRect();
            Assert.Equal(220, wrapperRect.width);
            Assert.True(hintRect.left >= viewport.Left);
            Assert.True(hintRect.top >= viewport.Top);
            Assert.True(hintRect.right <= viewport.Right);
            Assert.True(hintRect.bottom <= viewport.Bottom);

            RaiseNativeClick(dismiss, window);
            Dispatcher.UIThread.RunJobs();

            Assert.Null(hint.parentElement);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static void RaiseNativeClick(AvaloniaDomElement element, Window window)
    {
        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        var point = new Point(element.Control.Bounds.Width / 2, element.Control.Bounds.Height / 2);
        element.Control.RaiseEvent(new PointerPressedEventArgs(
            element.Control,
            pointer,
            window,
            point,
            0,
            new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed),
            KeyModifiers.None));
        element.Control.RaiseEvent(new PointerReleasedEventArgs(
            element.Control,
            pointer,
            window,
            point,
            1,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased),
            KeyModifiers.None,
            MouseButton.Left));
    }

    private sealed class RemoveElementListener(AvaloniaDomElement element) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent) => element.remove();
    }
}

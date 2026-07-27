using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssPointerMediaHoverSpikeTests
{
    [AvaloniaFact]
    public void DeferredNativeBoundaryInsideSameDomTargetDoesNotClearHover()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 120 };
        var window = new Window { Width = 320, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .target { width: 220px; height: 24px; }
                .target:hover { border: 1px solid rgb(42, 46, 57); }
                """;
            document.head.appendChild(style);
            var target = HostTestUtilities.GetElement(document.createElement("div"));
            target.className = "target";
            HostTestUtilities.GetElement(document.body).appendChild(target);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            var point = target.Control.TranslatePoint(
                new Point(target.Control.Bounds.Width / 2, target.Control.Bounds.Height / 2),
                window)!.Value;
            var enter = new PointerEventArgs(
                InputElement.PointerEnteredEvent,
                target.Control,
                pointer,
                window,
                point,
                1,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);
            document.UpdatePointerHover(target, enter);
            Assert.True(document.IsPointerHovered(target));

            // Avalonia can report a native child/control boundary even though
            // browser hit testing still resolves to the same DOM element. The
            // deferred exit must not expose an unhovered render in that gap.
            var exit = new PointerEventArgs(
                InputElement.PointerExitedEvent,
                target.Control,
                pointer,
                window,
                point,
                2,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);
            document.ClearPointerHover(target, exit);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.True(document.IsPointerHovered(target));
            Assert.NotNull(((CssLayoutPanel)target.Control).BorderBrush);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void DeferredExitRetargetsSiblingHoverWithoutExposingAnUnhoveredFrame()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 120 };
        var window = new Window { Width = 320, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var style = HostTestUtilities.GetElement(document.createElement("style"));
            style.textContent = """
                .study { display: flex; width: 220px; height: 24px; }
                .title { width: 120px; height: 24px; }
                .actions { width: 100px; height: 24px; }
                .study.withAction { border: 1px solid rgb(42, 46, 57); }
                """;
            document.head.appendChild(style);
            var study = HostTestUtilities.GetElement(document.createElement("div"));
            study.className = "study";
            var title = HostTestUtilities.GetElement(document.createElement("div"));
            title.className = "title";
            var actions = HostTestUtilities.GetElement(document.createElement("div"));
            actions.className = "actions";
            study.appendChild(title);
            study.appendChild(actions);
            HostTestUtilities.GetElement(document.body).appendChild(study);

            var events = new List<string>();
            title.__webSceneAddExternalEventListener(
                "mouseenter",
                new RecordingClassListener(events, "title-enter", study, add: true),
                capture: false,
                once: false,
                passive: false);
            title.__webSceneAddExternalEventListener(
                "mouseleave",
                new RecordingClassListener(events, "title-leave", study, add: false),
                capture: false,
                once: false,
                passive: false);
            actions.__webSceneAddExternalEventListener(
                "mouseenter",
                new RecordingClassListener(events, "actions-enter", study, add: true),
                capture: false,
                once: false,
                passive: false);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            var titlePoint = title.Control.TranslatePoint(
                new Point(title.Control.Bounds.Width / 2, title.Control.Bounds.Height / 2),
                window)!.Value;
            var enter = new PointerEventArgs(
                InputElement.PointerEnteredEvent,
                title.Control,
                pointer,
                window,
                titlePoint,
                1,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);
            document.UpdatePointerHover(title, enter);
            Assert.Contains("withAction", study.className, StringComparison.Ordinal);

            var actionsPoint = actions.Control.TranslatePoint(
                new Point(actions.Control.Bounds.Width / 2, actions.Control.Bounds.Height / 2),
                window)!.Value;
            var exit = new PointerEventArgs(
                InputElement.PointerExitedEvent,
                title.Control,
                pointer,
                window,
                actionsPoint,
                2,
                new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
                KeyModifiers.None);

            // Simulate the deferred exit callback running before Avalonia has
            // delivered the sibling PointerEntered routed event.
            document.ClearPointerHover(title, exit);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.True(document.IsPointerHovered(actions));
            Assert.Contains("withAction", study.className, StringComparison.Ordinal);
            Assert.Equal(["title-enter", "title-leave", "actions-enter"], events);
            Assert.NotNull(((CssLayoutPanel)study.Control).BorderBrush);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    public void DarkLegendHoverKeepsActionButtonsDark()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 120 };
        var window = new Window { Width = 320, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;

        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            :root {
              --color-white: #fff;
              --color-cold-gray-900: #131722;
            }
            .legend { color: transparent; }
            .button, .buttons { background-color: currentColor; }
            .withAction .buttons { background-color: var(--color-white); }
            .chart-widget__top--themed-dark .withAction .buttons {
              background-color: var(--color-cold-gray-900);
            }
            """;
        document.head.appendChild(style);

        var chart = HostTestUtilities.GetElement(document.createElement("div"));
        chart.className = "chart-widget__top--themed-dark";
        var legend = HostTestUtilities.GetElement(document.createElement("div"));
        legend.className = "legend";
        var study = HostTestUtilities.GetElement(document.createElement("div"));
        study.className = "study";
        var buttons = HostTestUtilities.GetElement(document.createElement("div"));
        buttons.className = "buttons";
        var button = HostTestUtilities.GetElement(document.createElement("button"));
        button.className = "button";
        buttons.appendChild(button);
        study.appendChild(buttons);
        legend.appendChild(study);
        chart.appendChild(legend);
        HostTestUtilities.GetElement(document.body).appendChild(chart);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("rgba(0, 0, 0, 0)", document.getComputedStyle(button).getPropertyValue("background-color"));
        Assert.Equal("rgba(0, 0, 0, 0)", document.getComputedStyle(buttons).getPropertyValue("background-color"));

        study.classList.add("withAction");
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("rgb(19, 23, 34)", document.getComputedStyle(buttons).getPropertyValue("background-color"));
        Assert.Equal(
            Color.Parse("#131722"),
            ((ISolidColorBrush)((CssLayoutPanel)buttons.Control).Background!).Color);
        window.Close();
    }

    [AvaloniaFact]
    public void DesktopFavoriteAffordanceExcludesCoarsePointerAndRequiresActualRowHover()
    {
        var root = new CssLayoutPanel { Width = 240, Height = 120 };
        var window = new Window { Width = 240, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;

        var style = HostTestUtilities.GetElement(document.createElement("style"));
        // Keep the product ordering and minified @media boundary: base hidden,
        // coarse pointers always visible, active state visible, then desktop
        // hover-capable devices reveal the affordance only through a real row hover.
        style.textContent = """
            .favorite{visibility:hidden}
            @media (pointer:coarse){.favorite{visibility:visible}}
            .favorite.active{visibility:visible}
            @media (any-hover:hover){.row:hover .favorite{visibility:visible}}
            """;
        document.head.appendChild(style);

        var row = HostTestUtilities.GetElement(document.createElement("div"));
        row.className = "row";
        var label = HostTestUtilities.GetElement(document.createElement("span"));
        label.textContent = "Cross";
        var favorite = HostTestUtilities.GetElement(document.createElement("button"));
        favorite.className = "favorite";
        favorite.setAttribute("aria-label", "Add to favorites");
        row.appendChild(label);
        row.appendChild(favorite);
        var outside = HostTestUtilities.GetElement(document.createElement("div"));
        outside.className = "outside";
        HostTestUtilities.GetElement(document.body).appendChild(row);
        HostTestUtilities.GetElement(document.body).appendChild(outside);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.DoesNotContain("active", favorite.className, StringComparison.Ordinal);
        Assert.False(document.IsPointerHovered(row));
        Assert.Equal("hidden", document.getComputedStyle(favorite).getPropertyValue("visibility"));

        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        var enter = new PointerEventArgs(
            InputElement.PointerEnteredEvent,
            label.Control,
            pointer,
            window,
            new Point(20, 20),
            0,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
            KeyModifiers.None);
        document.UpdatePointerHover(label, enter);
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.True(document.IsPointerHovered(row));
        Assert.Equal("visible", document.getComputedStyle(favorite).getPropertyValue("visibility"));

        var outsideEnter = new PointerEventArgs(
            InputElement.PointerEnteredEvent,
            outside.Control,
            pointer,
            window,
            new Point(20, 80),
            0,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
            KeyModifiers.None);
        document.UpdatePointerHover(outside, outsideEnter);
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.False(document.IsPointerHovered(row));
        Assert.Equal("hidden", document.getComputedStyle(favorite).getPropertyValue("visibility"));
        window.Close();
    }

    [AvaloniaFact]
    public void CursorToolboxOpacityRequiresActualRowHover()
    {
        var root = new CssLayoutPanel { Width = 240, Height = 120 };
        var window = new Window { Width = 240, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;

        var style = HostTestUtilities.GetElement(document.createElement("style"));
        // Representative cursor-menu selector shape from a popup item stylesheet.
        style.textContent = """
            .feature-no-touch .toolbox-jFqVJoPk.showOnHover-jFqVJoPk{opacity:0}
            @media (any-hover:hover){.feature-no-touch .item-jFqVJoPk:hover .toolbox-jFqVJoPk.showOnHover-jFqVJoPk{opacity:1}}
            """;
        document.head.appendChild(style);

        var row = HostTestUtilities.GetElement(document.createElement("div"));
        row.className = "item-jFqVJoPk";
        var label = HostTestUtilities.GetElement(document.createElement("span"));
        label.textContent = "Cross";
        var toolbox = HostTestUtilities.GetElement(document.createElement("span"));
        toolbox.className = "toolbox-jFqVJoPk showOnHover-jFqVJoPk";
        var favorite = HostTestUtilities.GetElement(document.createElement("button"));
        favorite.setAttribute("data-qa-id", "preset-menu-favorite-button");
        toolbox.appendChild(favorite);
        row.appendChild(label);
        row.appendChild(toolbox);
        var outside = HostTestUtilities.GetElement(document.createElement("div"));
        outside.className = "outside";
        HostTestUtilities.GetElement(document.body).appendChild(row);
        HostTestUtilities.GetElement(document.body).appendChild(outside);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("1", document.getComputedStyle(toolbox).getPropertyValue("opacity"));

        // Component libraries can install capability scopes on the synthetic html root
        // after the popup stylesheet is already present.
        document.documentElement.classList.add("feature-no-touch");
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.False(document.IsPointerHovered(row));
        Assert.Equal(0, toolbox.Control.Opacity);

        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        document.UpdatePointerHover(label, new PointerEventArgs(
            InputElement.PointerEnteredEvent,
            label.Control,
            pointer,
            window,
            new Point(20, 20),
            0,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
            KeyModifiers.None));
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.True(document.IsPointerHovered(row));
        Assert.Equal("1", document.getComputedStyle(toolbox).getPropertyValue("opacity"));

        document.UpdatePointerHover(outside, new PointerEventArgs(
            InputElement.PointerEnteredEvent,
            outside.Control,
            pointer,
            window,
            new Point(20, 80),
            0,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.Other),
            KeyModifiers.None));
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.False(document.IsPointerHovered(row));
        Assert.Equal("0", document.getComputedStyle(toolbox).getPropertyValue("opacity"));
        window.Close();
    }

    [AvaloniaFact]
    public void CursorToolboxAppendOnlyStylesheetHonorsDocumentRootScope()
    {
        var root = new CssLayoutPanel { Width = 240, Height = 120 };
        var window = new Window { Width = 240, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        document.documentElement.classList.add("feature-no-touch");

        var baseStyle = HostTestUtilities.GetElement(document.createElement("style"));
        baseStyle.textContent = ".item-jFqVJoPk{display:flex}";
        document.head.appendChild(baseStyle);
        var row = HostTestUtilities.GetElement(document.createElement("div"));
        row.className = "item-jFqVJoPk";
        var toolbox = HostTestUtilities.GetElement(document.createElement("span"));
        toolbox.className = "toolbox-jFqVJoPk showOnHover-jFqVJoPk";
        row.appendChild(toolbox);
        HostTestUtilities.GetElement(document.body).appendChild(row);

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();
        Assert.Equal(1, toolbox.Control.Opacity);

        var lazyCursorStyle = HostTestUtilities.GetElement(document.createElement("style"));
        lazyCursorStyle.textContent = """
            .feature-no-touch .toolbox-jFqVJoPk.showOnHover-jFqVJoPk{opacity:0}
            @media (any-hover:hover){.feature-no-touch .item-jFqVJoPk:hover .toolbox-jFqVJoPk.showOnHover-jFqVJoPk{opacity:1}}
            """;
        document.head.appendChild(lazyCursorStyle);
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.False(document.IsPointerHovered(row));
        Assert.Equal(0, toolbox.Control.Opacity);
        window.Close();
    }

    [AvaloniaFact]
    public void FocusVisibleSelectorTracksKeyboardRatherThanPointerFocus()
    {
        var root = new CssLayoutPanel { Width = 240, Height = 120 };
        var window = new Window { Width = 240, Height = 120, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            .target:focus{opacity:.5}
            .target:focus-visible{opacity:.75}
            """;
        document.head.appendChild(style);
        var target = HostTestUtilities.GetElement(document.createElement("button"));
        target.className = "target";
        var outside = HostTestUtilities.GetElement(document.createElement("button"));
        HostTestUtilities.GetElement(document.body).appendChild(target);
        HostTestUtilities.GetElement(document.body).appendChild(outside);

        window.Show();
        Assert.True(target.Control.Focus(NavigationMethod.Pointer, KeyModifiers.None));
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.True(target.Control.IsFocused);
        Assert.DoesNotContain(":focus-visible", target.Control.Classes);
        Assert.True(target.matches(":focus"));
        Assert.False(target.matches(":focus-visible"));
        Assert.Equal("0.5", document.getComputedStyle(target).getPropertyValue("opacity"));

        Assert.True(outside.Control.Focus(NavigationMethod.Pointer, KeyModifiers.None));
        Assert.True(target.Control.Focus(NavigationMethod.Tab, KeyModifiers.None));
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Contains(":focus-visible", target.Control.Classes);
        Assert.True(target.matches(":focus-visible"));
        Assert.Equal(0.75, target.Control.Opacity);
        window.Close();
    }

    private sealed class RecordingClassListener(
        List<string> events,
        string label,
        AvaloniaDomElement target,
        bool add) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent)
        {
            events.Add(label);
            if (add) target.classList.add("withAction");
            else target.classList.remove("withAction");
        }
    }
}

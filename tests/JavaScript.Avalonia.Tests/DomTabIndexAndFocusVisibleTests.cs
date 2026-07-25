using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomTabIndexAndFocusVisibleTests
{
    [AvaloniaFact]
    public void TabIndexReflectsMarkupAndAttributeMutationsIntoFocusability()
    {
        using var fixture = new Fixture();
        var container = fixture.Append("div");
        container.innerHTML = """
            <span id="positive" tabindex="2">positive</span>
            <span id="negative" tabindex="-1">negative</span>
            <span id="invalid" tabindex="invalid">invalid</span>
            <span id="prefix" tabindex="2trailing">prefix</span>
            <button id="button">button</button>
            <input id="input" type="button" value="input">
            <input id="hidden" type="hidden">
            <a id="link" href="#target">link</a>
            """;

        var positive = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("positive"));
        var negative = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("negative"));
        var invalid = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("invalid"));
        var prefix = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("prefix"));
        var button = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("button"));
        var input = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("input"));
        var hidden = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("hidden"));
        var link = Assert.IsType<AvaloniaDomElement>(fixture.Document.getElementById("link"));

        Assert.Equal(2, positive.tabIndex);
        Assert.True(positive.Control.Focusable);
        Assert.True(positive.Control.IsTabStop);
        Assert.Equal(-1, negative.tabIndex);
        Assert.True(negative.Control.Focusable);
        Assert.False(negative.Control.IsTabStop);
        Assert.Equal(-1, invalid.tabIndex);
        Assert.True(invalid.Control.Focusable);
        Assert.False(invalid.Control.IsTabStop);
        Assert.Equal(2, prefix.tabIndex);
        Assert.True(prefix.Control.Focusable);
        Assert.True(prefix.Control.IsTabStop);
        Assert.Equal(0, button.tabIndex);
        Assert.True(button.Control.Focusable);
        Assert.True(button.Control.IsTabStop);
        Assert.Equal(0, input.tabIndex);
        Assert.True(input.Control.Focusable);
        Assert.True(input.Control.IsTabStop);
        Assert.Equal(0, hidden.tabIndex);
        Assert.False(hidden.Control.Focusable);
        Assert.False(hidden.Control.IsTabStop);
        Assert.Equal(0, link.tabIndex);
        Assert.True(link.Control.Focusable);
        Assert.True(link.Control.IsTabStop);

        link.removeAttribute("href");
        Assert.Equal(-1, link.tabIndex);
        Assert.False(link.Control.Focusable);
        Assert.False(link.Control.IsTabStop);

        positive.setAttribute("tabindex", "-3");
        Assert.Equal("-3", positive.getAttribute("tabindex"));
        Assert.Equal(-3, positive.tabIndex);
        Assert.True(positive.Control.Focusable);
        Assert.False(positive.Control.IsTabStop);

        positive.removeAttribute("tabindex");
        Assert.Null(positive.getAttribute("tabindex"));
        Assert.Equal(-1, positive.tabIndex);
        Assert.False(positive.Control.Focusable);
        Assert.False(positive.Control.IsTabStop);
    }

    [AvaloniaFact]
    public void TabIndexPropertyReflectsAttributeAndNegativeValueRemainsProgrammaticallyFocusable()
    {
        using var fixture = new Fixture();
        var span = fixture.Append("span");
        var events = new List<string>();
        span.__htmlMlAddExternalEventListener(
            "focus",
            new RecordingListener(events, "focus"),
            capture: false,
            once: false,
            passive: false);
        span.__htmlMlAddExternalEventListener(
            "focusin",
            new RecordingListener(events, "focusin"),
            capture: false,
            once: false,
            passive: false);

        Assert.Equal(-1, span.tabIndex);
        Assert.Null(span.getAttribute("tabindex"));

        span.tabIndex = -1;

        Assert.Equal("-1", span.getAttribute("tabindex"));
        Assert.Equal(-1, span.tabIndex);
        Assert.True(span.Control.Focusable);
        Assert.False(span.Control.IsTabStop);
        Assert.True(span.focus());
        Assert.Same(span, fixture.Document.activeElement);
        Assert.Equal(["focus", "focusin"], events);

        span.tabIndex = 4;
        Assert.Equal("4", span.getAttribute("tabindex"));
        Assert.Equal(4, span.tabIndex);
        Assert.True(span.Control.IsTabStop);
    }

    [AvaloniaFact]
    public void InheritedHiddenVisibilityRejectsFocusAndHitOwnershipWhileExplicitVisibleRestoresBoth()
    {
        using var fixture = new Fixture();
        var style = fixture.Append("style", fixture.Document.head);
        style.textContent = """
            .under, .host, .host button {
                position: absolute;
                width: 80px;
                height: 32px;
            }
            .under, .host { left: 8px; top: 8px; }
            .host { visibility: hidden; }
            .host button { left: 0; top: 0; }
            .host .restored { left: 96px; visibility: visible; }
            """;
        var under = fixture.Append("button");
        under.className = "under";
        var host = fixture.Append("div");
        host.className = "host";
        var inherited = fixture.Append("button", host);
        var restored = fixture.Append("button", host);
        restored.className = "restored";

        fixture.Document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal(
            "hidden",
            fixture.Document.getComputedStyle(inherited).getPropertyValue("visibility"));
        Assert.Equal(
            "visible",
            fixture.Document.getComputedStyle(restored).getPropertyValue("visibility"));
        Assert.True(under.focus());
        Assert.False(inherited.focus());
        Assert.Same(under, fixture.Document.activeElement);
        Assert.True(restored.focus());
        Assert.Same(restored, fixture.Document.activeElement);
        Assert.Same(under, fixture.Document.elementFromPoint(16, 16));
        Assert.Same(restored, fixture.Document.elementFromPoint(112, 16));
    }

    [AvaloniaFact]
    public void PointerFocusDoesNotMatchFocusVisibleForNonTextControls()
    {
        using var fixture = new Fixture();
        var style = fixture.Append("style", fixture.Document.head);
        style.textContent = ".check:focus-visible { opacity: .25; } .check:focus:not(:focus-visible) { opacity: .75; }";
        var outside = fixture.Append("button");
        var cases = new (string Tag, string? Type)[]
        {
            ("span", null),
            ("button", null),
            ("input", "button"),
            ("input", "image"),
            ("input", "reset"),
            ("input", "submit"),
            ("input", "checkbox"),
            ("input", "radio"),
            ("input", "range"),
            ("input", "color")
        };

        foreach (var (tag, type) in cases)
        {
            var target = fixture.Append(tag);
            target.className = "check";
            if (type is not null)
            {
                target.type = type;
            }
            if (tag == "span")
            {
                target.setAttribute("tabindex", "0");
            }

            Assert.True(outside.Control.Focus(NavigationMethod.Pointer, KeyModifiers.None));
            Assert.True(target.Control.Focus(NavigationMethod.Pointer, KeyModifiers.None));
            fixture.Document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            Assert.True(target.matches(":focus"), $"{tag}[type={type}] should match :focus");
            Assert.False(target.matches(":focus-visible"), $"{tag}[type={type}] should not match :focus-visible");
            Assert.Equal("0.75", fixture.Document.getComputedStyle(target).getPropertyValue("opacity"));
        }
    }

    private sealed class RecordingListener(List<string> events, string label) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent) => events.Add(label);
    }

    private sealed class Fixture : IDisposable
    {
        private readonly Window _window;
        private readonly AvaloniaBrowserHost _host;

        public Fixture()
        {
            var root = new CssLayoutPanel { Width = 320, Height = 240 };
            _window = new Window { Width = 320, Height = 240, Content = root };
            _host = new AvaloniaBrowserHost(_window, enableTargetOnlyInlineStyles: true);
            Document = _host.Document;
            _window.Show();
            Document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
        }

        public AvaloniaDomDocument Document { get; }

        public AvaloniaDomElement Append(string tag, object? parent = null)
        {
            var element = HostTestUtilities.GetElement(Document.createElement(tag));
            switch (parent)
            {
                case AvaloniaDomElement parentElement:
                    parentElement.appendChild(element);
                    break;
                case DomHeadElement head:
                    head.appendChild(element);
                    break;
                default:
                    HostTestUtilities.GetElement(Document.body).appendChild(element);
                    break;
            }
            return element;
        }

        public void Dispose()
        {
            _window.Close();
            Dispatcher.UIThread.RunJobs();
            _host.Dispose();
        }
    }
}

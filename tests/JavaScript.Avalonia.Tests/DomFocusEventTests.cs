using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomFocusEventTests
{
    [AvaloniaFact]
    public void ProgrammaticFocusDispatchesFocusThenBubblingFocusIn()
    {
        using var fixture = new FocusFixture();
        var parent = fixture.Append("div");
        var input = fixture.Append("input", parent);
        var events = new List<FocusSnapshot>();
        Listen(input, "focus", events, fixture.Document);
        Listen(input, "focusin", events, fixture.Document);
        Listen(parent, "focusin", events, fixture.Document);

        Assert.True(input.focus());

        Assert.Same(input, fixture.Document.activeElement);
        Assert.Equal(
            ["focus@INPUT", "focusin@INPUT", "focusin@DIV"],
            events.Select(static snapshot => snapshot.Label));
        Assert.All(events, static snapshot =>
        {
            Assert.False(snapshot.Cancelable);
            Assert.True(snapshot.Composed);
            Assert.Null(snapshot.RelatedTarget);
        });
        Assert.False(events[0].Bubbles);
        Assert.True(events[1].Bubbles);
        Assert.True(events[2].Bubbles);
        Assert.Equal(DomEventPhase.AtTarget, events[0].Phase);
        Assert.Equal(DomEventPhase.AtTarget, events[1].Phase);
        Assert.Equal(DomEventPhase.BubblingPhase, events[2].Phase);
    }

    [AvaloniaFact]
    public void MovingFocusDispatchesBrowserOrderWithRelatedTargetsAndActiveElementTiming()
    {
        using var fixture = new FocusFixture();
        var parent = fixture.Append("div");
        var first = fixture.Append("input", parent);
        var second = fixture.Append("input", parent);
        Assert.True(first.focus());

        var events = new List<FocusSnapshot>();
        Listen(first, "blur", events, fixture.Document);
        Listen(first, "focusout", events, fixture.Document);
        Listen(parent, "focusout", events, fixture.Document);
        Listen(second, "focus", events, fixture.Document);
        Listen(second, "focusin", events, fixture.Document);
        Listen(parent, "focusin", events, fixture.Document);

        Assert.True(second.focus());

        Assert.Equal(
            [
                "blur@INPUT",
                "focusout@INPUT",
                "focusout@DIV",
                "focus@INPUT",
                "focusin@INPUT",
                "focusin@DIV"
            ],
            events.Select(static snapshot => snapshot.Label));
        Assert.All(events.Take(3), snapshot =>
        {
            Assert.Same(second, snapshot.RelatedTarget);
            Assert.Same(fixture.Document.body, snapshot.ActiveElement);
            Assert.False(snapshot.Cancelable);
            Assert.True(snapshot.Composed);
        });
        Assert.All(events.Skip(3), snapshot =>
        {
            Assert.Same(first, snapshot.RelatedTarget);
            Assert.Same(second, snapshot.ActiveElement);
            Assert.False(snapshot.Cancelable);
            Assert.True(snapshot.Composed);
        });
        Assert.False(events[0].Bubbles);
        Assert.True(events[1].Bubbles);
        Assert.True(events[2].Bubbles);
        Assert.False(events[3].Bubbles);
        Assert.True(events[4].Bubbles);
        Assert.True(events[5].Bubbles);
        Assert.Same(second, fixture.Document.activeElement);
    }

    [AvaloniaFact]
    public void BlurDispatchesBlurThenBubblingFocusOutAndReturnsFocusToBody()
    {
        using var fixture = new FocusFixture();
        var parent = fixture.Append("div");
        var input = fixture.Append("input", parent);
        Assert.True(input.focus());

        var events = new List<FocusSnapshot>();
        Listen(input, "blur", events, fixture.Document);
        Listen(input, "focusout", events, fixture.Document);
        Listen(parent, "focusout", events, fixture.Document);

        input.blur();

        Assert.Equal(
            ["blur@INPUT", "focusout@INPUT", "focusout@DIV"],
            events.Select(static snapshot => snapshot.Label));
        Assert.All(events, snapshot =>
        {
            Assert.Null(snapshot.RelatedTarget);
            Assert.Same(fixture.Document.body, snapshot.ActiveElement);
            Assert.False(snapshot.Cancelable);
            Assert.True(snapshot.Composed);
        });
        Assert.Same(fixture.Document.body, fixture.Document.activeElement);
    }

    private static void Listen(
        AvaloniaDomElement element,
        string type,
        List<FocusSnapshot> events,
        AvaloniaDomDocument document)
        => element.__webSceneAddExternalEventListener(
            type,
            new RecordingFocusListener(events, document),
            capture: false,
            once: false,
            passive: false);

    private sealed class RecordingFocusListener(
        List<FocusSnapshot> events,
        AvaloniaDomDocument document) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent)
        {
            var evt = Assert.IsAssignableFrom<DomEvent>(domEvent);
            var current = Assert.IsType<AvaloniaDomElement>(currentTarget);
            var relatedTarget = domEvent.GetType().GetProperty("relatedTarget")?.GetValue(domEvent);
            events.Add(new FocusSnapshot(
                $"{evt.type}@{current.nodeName}",
                evt.bubbles,
                evt.cancelable,
                evt.composed,
                evt.eventPhase,
                relatedTarget,
                document.activeElement));
        }
    }

    private sealed record FocusSnapshot(
        string Label,
        bool Bubbles,
        bool Cancelable,
        bool Composed,
        DomEventPhase Phase,
        object? RelatedTarget,
        object? ActiveElement);

    private sealed class FocusFixture : IDisposable
    {
        private readonly CssLayoutPanel _root = new() { Width = 240, Height = 80 };
        private readonly Window _window;
        private readonly AvaloniaBrowserHost _host;

        public FocusFixture()
        {
            _window = new Window { Width = 240, Height = 80, Content = _root };
            _host = new AvaloniaBrowserHost(_window, enableTargetOnlyInlineStyles: true);
            Document = _host.Document;
            _window.Show();
            Document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
        }

        public AvaloniaDomDocument Document { get; }

        public AvaloniaDomElement Append(string tag, AvaloniaDomElement? parent = null)
        {
            var element = HostTestUtilities.GetElement(Document.createElement(tag));
            (parent ?? HostTestUtilities.GetElement(Document.body)).appendChild(element);
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

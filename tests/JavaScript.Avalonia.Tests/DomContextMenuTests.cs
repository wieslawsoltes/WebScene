using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomContextMenuTests
{
    [AvaloniaFact]
    public void NativeSecondaryClickDispatchesTrustedCancelableContextMenuAtHitElement()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        try
        {
            var document = host.Document;
            var parent = HostTestUtilities.GetElement(document.createElement("div"));
            var hit = HostTestUtilities.GetElement(document.createElement("canvas"));
            parent.appendChild(hit);
            HostTestUtilities.GetElement(document.body).appendChild(parent);

            var sequence = new List<string>();
            foreach (var type in new[]
                     {
                         "pointerdown", "mousedown", "contextmenu",
                         "pointerup", "mouseup", "auxclick"
                     })
            {
                hit.__webSceneAddExternalEventListener(
                    type,
                    new SequenceListener(sequence),
                    capture: false,
                    once: false,
                    passive: false);
            }

            var contextAtTarget = new ContextMenuListener(hit, preventDefault: true);
            var contextAtParent = new ContextMenuListener(hit, preventDefault: false);
            hit.__webSceneAddExternalEventListener(
                "contextmenu",
                contextAtTarget,
                capture: false,
                once: false,
                passive: false);
            parent.__webSceneAddExternalEventListener(
                "contextmenu",
                contextAtParent,
                capture: false,
                once: false,
                passive: false);

            window.Show();
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
            var press = new PointerPressedEventArgs(
                hit.Control,
                pointer,
                window,
                new Point(9, 11),
                1,
                new PointerPointProperties(
                    RawInputModifiers.RightMouseButton,
                    PointerUpdateKind.RightButtonPressed),
                KeyModifiers.Control);
            hit.Control.RaiseEvent(press);
            hit.Control.RaiseEvent(new PointerReleasedEventArgs(
                hit.Control,
                pointer,
                window,
                new Point(9, 11),
                2,
                new PointerPointProperties(
                    RawInputModifiers.None,
                    PointerUpdateKind.RightButtonReleased),
                KeyModifiers.Control,
                MouseButton.Right));
            Dispatcher.UIThread.RunJobs();

            Assert.Equal(
                ["pointerdown", "mousedown", "contextmenu", "pointerup", "mouseup", "auxclick"],
                sequence);
            Assert.True(press.Handled);
            Assert.Equal(1, contextAtTarget.InvocationCount);
            Assert.Equal(1, contextAtParent.InvocationCount);

            Assert.Same(hit, contextAtTarget.Target);
            Assert.Same(hit, contextAtTarget.CurrentTarget);
            Assert.Equal(DomEventPhase.AtTarget, contextAtTarget.EventPhase);
            Assert.True(contextAtTarget.Bubbles);
            Assert.True(contextAtTarget.Cancelable);
            Assert.True(contextAtTarget.IsTrusted);
            Assert.True(contextAtTarget.DefaultPrevented);
            Assert.Equal(2, contextAtTarget.Button);
            Assert.Equal(2, contextAtTarget.Buttons);
            Assert.Equal(3, contextAtTarget.Which);
            Assert.Equal(9, contextAtTarget.ClientX);
            Assert.Equal(11, contextAtTarget.ClientY);
            Assert.True(contextAtTarget.CtrlKey);

            Assert.Same(hit, contextAtParent.Target);
            Assert.Same(parent, contextAtParent.CurrentTarget);
            Assert.Equal(DomEventPhase.BubblingPhase, contextAtParent.EventPhase);
            Assert.True(contextAtParent.DefaultPrevented);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private sealed class SequenceListener(List<string> sequence) : IExternalDomEventListener
    {
        public void Invoke(object currentTarget, object domEvent)
            => sequence.Add(Assert.IsType<DomPointerEvent>(domEvent).type);
    }

    private sealed class ContextMenuListener(
        AvaloniaDomElement expectedTarget,
        bool preventDefault) : IExternalDomEventListener
    {
        public int InvocationCount { get; private set; }
        public object? Target { get; private set; }
        public object? CurrentTarget { get; private set; }
        public DomEventPhase EventPhase { get; private set; }
        public bool Bubbles { get; private set; }
        public bool Cancelable { get; private set; }
        public bool IsTrusted { get; private set; }
        public bool DefaultPrevented { get; private set; }
        public int Button { get; private set; }
        public int Buttons { get; private set; }
        public int Which { get; private set; }
        public double ClientX { get; private set; }
        public double ClientY { get; private set; }
        public bool CtrlKey { get; private set; }

        public void Invoke(object currentTarget, object domEvent)
        {
            var evt = Assert.IsType<DomPointerEvent>(domEvent);
            Assert.Same(expectedTarget, evt.target);
            InvocationCount++;
            Target = evt.target;
            CurrentTarget = evt.currentTarget;
            EventPhase = evt.eventPhase;
            Bubbles = evt.bubbles;
            Cancelable = evt.cancelable;
            IsTrusted = evt.isTrusted;
            Button = evt.button;
            Buttons = evt.buttons;
            Which = evt.which;
            ClientX = evt.clientX;
            ClientY = evt.clientY;
            CtrlKey = evt.ctrlKey;
            if (preventDefault)
            {
                evt.preventDefault();
            }
            DefaultPrevented = evt.defaultPrevented;
        }
    }
}

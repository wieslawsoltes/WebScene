using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomCheckableInputTests
{
    [AvaloniaFact]
    public void NativeCheckboxActivationUpdatesCheckedBeforeClickAndFiresInputAndChange()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var document = host.Document;
                var style = HostTestUtilities.GetElement(document.createElement("style"));
                style.textContent = "input { width: 38px; height: 20px; background: black; } input:checked { background: red; }";
                document.head.appendChild(style);
                var input = HostTestUtilities.GetElement(document.createElement("input"));
                input.type = "checkbox";
                HostTestUtilities.GetElement(document.body).appendChild(input);
                var click = new CheckedStateListener();
                var inputEvent = new CountingEventListener();
                var change = new CountingEventListener();
                input.__webSceneAddExternalEventListener("click", click, capture: false, once: false, passive: false);
                input.__webSceneAddExternalEventListener("input", inputEvent, capture: false, once: false, passive: false);
                input.__webSceneAddExternalEventListener("change", change, capture: false, once: false, passive: false);
                document.EnsureStylesCurrent();
                Dispatcher.UIThread.RunJobs();

                RaiseNativeClick(input, window);
                Dispatcher.UIThread.RunJobs();

                Assert.True(input.@checked);
                Assert.Equal([true], click.States);
                Assert.Equal(1, inputEvent.InvocationCount);
                Assert.Equal(1, change.InvocationCount);
                Assert.Equal("rgb(255, 0, 0)", document.getComputedStyle(input).getPropertyValue("background-color"));

                RaiseNativeClick(input, window);
                Dispatcher.UIThread.RunJobs();

                Assert.False(input.@checked);
                Assert.Equal([true, false], click.States);
                Assert.Equal(2, inputEvent.InvocationCount);
                Assert.Equal(2, change.InvocationCount);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    [AvaloniaFact]
    public void CanceledCheckboxActivationRestoresStateAndSuppressesInputAndChange()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var input = HostTestUtilities.GetElement(host.Document.createElement("input"));
                input.type = "checkbox";
                HostTestUtilities.GetElement(host.Document.body).appendChild(input);
                var click = new PreventingCheckedStateListener();
                var change = new CountingEventListener();
                input.__webSceneAddExternalEventListener("click", click, capture: false, once: false, passive: false);
                input.__webSceneAddExternalEventListener("change", change, capture: false, once: false, passive: false);
                host.Document.EnsureStylesCurrent();
                Dispatcher.UIThread.RunJobs();

                RaiseNativeClick(input, window);
                Dispatcher.UIThread.RunJobs();

                Assert.Equal([true], click.States);
                Assert.False(input.@checked);
                Assert.Equal(0, change.InvocationCount);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    [AvaloniaFact]
    public void NativeRadioActivationUnchecksTheOtherRadioInItsNamedGroup()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var document = host.Document;
                var first = HostTestUtilities.GetElement(document.createElement("input"));
                first.type = "radio";
                first.name = "source";
                first.@checked = true;
                var second = HostTestUtilities.GetElement(document.createElement("input"));
                second.type = "radio";
                second.name = "source";
                HostTestUtilities.GetElement(document.body).appendChild(first);
                HostTestUtilities.GetElement(document.body).appendChild(second);
                second.__webSceneAddExternalEventListener(
                    "click",
                    new CountingEventListener(),
                    capture: false,
                    once: false,
                    passive: false);
                document.EnsureStylesCurrent();
                Dispatcher.UIThread.RunJobs();

                RaiseNativeClick(second, window);
                Dispatcher.UIThread.RunJobs();

                Assert.False(first.@checked);
                Assert.True(second.@checked);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    [AvaloniaFact]
    public void ClickingStyledLabelFaceActivatesTransparentCheckbox()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var document = host.Document;
                var style = HostTestUtilities.GetElement(document.createElement("style"));
                style.textContent = """
                    label { display: block; position: relative; width: 18px; height: 18px; }
                    input { position: absolute; opacity: 0; width: 0; height: 0; }
                    .face { display: block; width: 18px; height: 18px; background: black; }
                    """;
                document.head.appendChild(style);
                var label = HostTestUtilities.GetElement(document.createElement("label"));
                var input = HostTestUtilities.GetElement(document.createElement("input"));
                input.type = "checkbox";
                var face = HostTestUtilities.GetElement(document.createElement("span"));
                face.className = "face";
                label.appendChild(input);
                label.appendChild(face);
                HostTestUtilities.GetElement(document.body).appendChild(label);
                var click = new CheckedStateListener();
                var inputEvent = new CountingEventListener();
                var change = new CountingEventListener();
                input.__webSceneAddExternalEventListener("click", click, capture: false, once: false, passive: false);
                input.__webSceneAddExternalEventListener("input", inputEvent, capture: false, once: false, passive: false);
                input.__webSceneAddExternalEventListener("change", change, capture: false, once: false, passive: false);
                document.EnsureStylesCurrent();
                Dispatcher.UIThread.RunJobs();

                RaiseNativeClick(face, window);
                Dispatcher.UIThread.RunJobs();

                Assert.True(input.@checked);
                Assert.Equal([true], click.States);
                Assert.Equal(1, inputEvent.InvocationCount);
                Assert.Equal(1, change.InvocationCount);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    private static (AvaloniaBrowserHost Host, Window Window) CreateHost()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return (host, window);
    }

    private static void RaiseNativeClick(AvaloniaDomElement input, Window window)
    {
        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        var point = new Point(input.Control.Bounds.Width / 2, input.Control.Bounds.Height / 2);
        input.Control.RaiseEvent(new PointerPressedEventArgs(
            input.Control,
            pointer,
            window,
            point,
            0,
            new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.LeftButtonPressed),
            KeyModifiers.None));
        input.Control.RaiseEvent(new PointerReleasedEventArgs(
            input.Control,
            pointer,
            window,
            point,
            1,
            new PointerPointProperties(RawInputModifiers.None, PointerUpdateKind.LeftButtonReleased),
            KeyModifiers.None,
            MouseButton.Left));
    }

    private sealed class CheckedStateListener : IExternalDomEventListener
    {
        public List<bool> States { get; } = [];

        public void Invoke(object currentTarget, object domEvent)
            => States.Add(Assert.IsType<AvaloniaDomElement>(((DomEvent)domEvent).target).@checked);
    }

    private sealed class PreventingCheckedStateListener : IExternalDomEventListener
    {
        public List<bool> States { get; } = [];

        public void Invoke(object currentTarget, object domEvent)
        {
            var evt = Assert.IsType<DomPointerEvent>(domEvent);
            States.Add(Assert.IsType<AvaloniaDomElement>(evt.target).@checked);
            evt.preventDefault();
        }
    }

    private sealed class CountingEventListener : IExternalDomEventListener
    {
        public int InvocationCount { get; private set; }

        public void Invoke(object currentTarget, object domEvent) => InvocationCount++;
    }
}

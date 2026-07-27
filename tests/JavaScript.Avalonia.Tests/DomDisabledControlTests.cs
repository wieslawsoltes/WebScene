using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class DomDisabledControlTests
{
    [AvaloniaFact]
    public void DisabledBooleanAttributeReflectsNativeStateSelectorsAndComputedStyle()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var document = host.Document;
                var style = HostTestUtilities.GetElement(document.createElement("style"));
                style.textContent = "button { color: rgb(1, 2, 3); } button:disabled { color: rgb(101, 102, 103); }";
                document.head.appendChild(style);
                var button = HostTestUtilities.GetElement(document.createElement("button"));
                var incapable = HostTestUtilities.GetElement(document.createElement("span"));
                HostTestUtilities.GetElement(document.body).appendChild(button);
                HostTestUtilities.GetElement(document.body).appendChild(incapable);
                document.EnsureStylesCurrent();

                Assert.False(button.disabled);
                Assert.False(button.hasAttribute("disabled"));
                Assert.True(button.matches(":enabled"));
                Assert.False(incapable.matches(":enabled"));
                Assert.Equal("rgb(1, 2, 3)", document.getComputedStyle(button).getPropertyValue("color"));

                button.setAttribute("disabled", "false");
                document.EnsureStylesCurrent();

                Assert.True(button.disabled);
                Assert.Equal("false", button.getAttribute("disabled"));
                Assert.False(button.Control.IsEnabled);
                Assert.True(button.matches(":disabled"));
                Assert.False(button.matches(":enabled"));
                Assert.Same(button, Assert.Single(document.querySelectorAll("button:disabled")));
                Assert.Equal("rgb(101, 102, 103)", document.getComputedStyle(button).getPropertyValue("color"));

                button.disabled = false;
                document.EnsureStylesCurrent();

                Assert.False(button.disabled);
                Assert.False(button.hasAttribute("disabled"));
                Assert.True(button.Control.IsEnabled);
                Assert.True(button.matches(":enabled"));
                Assert.Empty(document.querySelectorAll("button:disabled"));
                Assert.Equal("rgb(1, 2, 3)", document.getComputedStyle(button).getPropertyValue("color"));
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        }
    }

    [AvaloniaFact]
    public void TrustedClickDoesNotActivateDisabledCheckboxButSyntheticEventStillDispatches()
    {
        var (host, window) = CreateHost();
        using (host)
        {
            try
            {
                var input = HostTestUtilities.GetElement(host.Document.createElement("input"));
                input.type = "checkbox";
                input.disabled = true;
                HostTestUtilities.GetElement(host.Document.body).appendChild(input);
                var clicks = new CountingEventListener();
                input.__webSceneAddExternalEventListener("click", clicks, capture: false, once: false, passive: false);
                host.Document.EnsureStylesCurrent();
                Dispatcher.UIThread.RunJobs();

                RaiseNativeClick(input, window);
                Dispatcher.UIThread.RunJobs();

                Assert.False(input.@checked);
                Assert.Equal(0, clicks.InvocationCount);

                Assert.True(input.dispatchEvent("click"));
                Assert.Equal(1, clicks.InvocationCount);
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

    private sealed class CountingEventListener : IExternalDomEventListener
    {
        public int InvocationCount { get; private set; }

        public void Invoke(object currentTarget, object domEvent) => InvocationCount++;
    }
}

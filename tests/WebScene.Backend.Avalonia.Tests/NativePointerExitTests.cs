using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using WebScene.Backends.Avalonia.Native;
using Xunit;

[assembly: AvaloniaTestApplication(typeof(WebScene.Backend.Avalonia.Tests.PointerTestApplication))]

namespace WebScene.Backend.Avalonia.Tests;

public class PointerTestApplication : Application
{
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<PointerTestApplication>()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions());
}

public sealed class NativePointerExitTests
{
    [AvaloniaFact]
    public void LeavingSurfaceForAdjacentNativeControlForwardsExitWithoutAnotherMove()
    {
        var inputs = new List<InputEvent>();
        var surface = new NativeSceneSurface(IntPtr.Zero, false, false, input =>
        {
            inputs.Add(input);
            return true;
        }) { Width = 100, Height = 100 };
        var window = new Window
        {
            Width = 200,
            Height = 100,
            Content = new StackPanel
            {
                Orientation = global::Avalonia.Layout.Orientation.Horizontal,
                Children = { surface, new Border { Width = 100, Height = 100 } }
            }
        };
        window.Show();
        try
        {
            window.MouseMove(new Point(20, 20));
            Assert.Contains(inputs, input => input.Kind == 1);
            inputs.Clear();

            window.MouseMove(new Point(150, 20));

            var exit = Assert.Single(inputs);
            Assert.Equal(10U, exit.Kind);
            Assert.False(surface.IsPointerOver);

            window.MouseMove(new Point(20, 20));
            Assert.Equal(1U, inputs[^1].Kind);
            Assert.True(surface.IsPointerOver);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void DirectHostExitWithLastInsidePositionPreservesModifiersAndDoesNotSynthesizeUp()
    {
        var inputs = new List<InputEvent>();
        var surface = new NativeSceneSurface(IntPtr.Zero, false, false, input =>
        {
            inputs.Add(input);
            return true;
        });
        using var pointer = new Pointer(Pointer.GetNextFreeId(), PointerType.Mouse, true);
        surface.RaiseEvent(new PointerEventArgs(
            InputElement.PointerExitedEvent,
            surface,
            pointer,
            null,
            new Point(20, 20),
            0,
            new PointerPointProperties(RawInputModifiers.LeftMouseButton, PointerUpdateKind.Other),
            KeyModifiers.Shift));

        var exit = Assert.Single(inputs);
        Assert.Equal(10U, exit.Kind);
        Assert.Equal(1U | (1U << 16), exit.Flags);
    }
}

using System.Text.Json;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class KestrelDragWorkloadValidatorTests
{
    private record Pointer(string type, double x, double y, int button, int buttons);
    private static Pointer[] Sidebar() => [
        new("pointerdown", 100, 200, 0, 1),
        new("pointermove", 102, 200, -1, 1),
        new("pointermove", 180, 200, -1, 1),
        new("pointermove", 220, 200, -1, 1),
        new("pointerup", 220, 200, 0, 0)
    ];
    private static void Validate(Pointer[] events, bool sidebar = true, int errors = 0, bool panning = false) =>
        KestrelDragWorkloadValidator.Validate(JsonSerializer.Serialize(new { events, errors, panning }), 100, 200, sidebar);

    [Fact]
    public void CoalescedSidebarMovesRemainValid() => Validate(Sidebar());

    [Theory]
    [InlineData(181, 200, 1)] // Outside submitted two-pixel steps.
    [InlineData(180, 201, 1)] // External vertical movement.
    [InlineData(180, 200, 2)] // Wrong held button.
    [InlineData(100, 200, 1)] // Reversed movement.
    public void ContaminatedSidebarMovesAreRejected(double x, double y, int buttons)
    {
        var events = Sidebar();
        events[2] = new("pointermove", x, y, -1, buttons);
        Assert.Throws<InvalidOperationException>(() => Validate(events));
    }

    [Fact]
    public void MissingReleaseIsRejected() =>
        Assert.Throws<InvalidOperationException>(() => Validate(Sidebar()[..^1]));

    [Fact]
    public void ApplicationErrorIsRejected() =>
        Assert.Throws<InvalidOperationException>(() => Validate(Sidebar(), errors: 1));

    [Fact]
    public void PanCanTravelOutAndBackWithCoalescing() => Validate([
        new("pointerdown", 100, 200, 2, 2),
        new("pointermove", 104, 201, -1, 2),
        new("pointermove", 260, 240, -1, 2),
        new("pointermove", 100, 200, -1, 2),
        new("pointerup", 100, 200, 2, 0)
    ], sidebar: false);
}

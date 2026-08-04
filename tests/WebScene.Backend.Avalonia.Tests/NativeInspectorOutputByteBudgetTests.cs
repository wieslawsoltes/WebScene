using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeInspectorOutputByteBudgetTests
{
    [Fact]
    public void RejectsBytesBeyondAggregateLimit()
    {
        var budget = new NativeInspectorOutputByteBudget();

        Assert.True(budget.TryReserve(
            checked((int)NativeInspectorOutputByteBudget.MaximumBytes)));
        Assert.False(budget.TryReserve(1));
        Assert.Equal(
            NativeInspectorOutputByteBudget.MaximumBytes,
            budget.QueuedBytes);
    }

    [Fact]
    public void ReleasedBytesAreAvailableToLaterMessages()
    {
        var budget = new NativeInspectorOutputByteBudget();
        var half = checked((int)(NativeInspectorOutputByteBudget.MaximumBytes / 2));

        Assert.True(budget.TryReserve(half));
        Assert.True(budget.TryReserve(half));
        budget.Release(half);
        Assert.True(budget.TryReserve(half));
        Assert.Equal(
            NativeInspectorOutputByteBudget.MaximumBytes,
            budget.QueuedBytes);
    }
}

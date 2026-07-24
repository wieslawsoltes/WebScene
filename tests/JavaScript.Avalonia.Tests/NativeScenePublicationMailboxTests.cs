using HtmlML.Backends.Avalonia.Native;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class NativeScenePublicationMailboxTests
{
    [Fact]
    public void MailboxCountsConcurrentPublicationsAndConsumesEachExactlyOnce()
    {
        var mailbox = new NativeScenePublicationMailbox();

        Parallel.For(0, 512, _ => mailbox.Publish());

        Assert.Equal(512, mailbox.PendingCount);
        for (var index = 0; index < 512; index++)
        {
            Assert.True(mailbox.TryConsume());
        }
        Assert.False(mailbox.TryConsume());
        Assert.Equal(0, mailbox.PendingCount);
    }

    [Fact]
    public void ResetDiscardsOnlyAlreadyPublishedWork()
    {
        var mailbox = new NativeScenePublicationMailbox();
        for (var index = 0; index < 8; index++)
        {
            mailbox.Publish();
        }

        mailbox.Reset();
        mailbox.Publish();

        Assert.Equal(1, mailbox.PendingCount);
        Assert.True(mailbox.TryConsume());
        Assert.False(mailbox.TryConsume());
    }

    [Fact]
    public void UiWakeGateAllowsOnlyOneOutstandingWake()
    {
        var gate = new NativeSceneUiWakeGate();
        var scheduled = 0;

        Parallel.For(
            0,
            512,
            _ =>
            {
                if (gate.TrySchedule())
                {
                    Interlocked.Increment(ref scheduled);
                }
            });

        Assert.Equal(1, scheduled);
        Assert.False(gate.TrySchedule());

        gate.Complete();

        Assert.True(gate.TrySchedule());
    }

    [Theory]
    [InlineData(false, 0, true)]
    [InlineData(true, 10, true)]
    [InlineData(false, 1, false)]
    [InlineData(false, 10_000, false)]
    public void OrdinaryScenesAfterFirstPresentationStayOffTheUiDispatcher(
        bool matchingResizePublication,
        long renderedSceneCount,
        bool expectedUiWake)
    {
        Assert.Equal(
            expectedUiWake,
            NativeScenePublicationWakePolicy.RequiresUiWake(
                matchingResizePublication,
                renderedSceneCount));
    }
}

using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

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

    [Fact]
    public void FirstPublicationSelectsAtMostOneNormalWake()
    {
        var gate = new NativeSceneUiWakeGate();

        Assert.Equal(
            NativeSceneUiWakePriority.Normal,
            NativeScenePublicationWakePolicy.Select(
                matchingResizePublication: false,
                renderedSceneCount: 0,
                gate));
        Assert.Equal(
            NativeSceneUiWakePriority.None,
            NativeScenePublicationWakePolicy.Select(
                matchingResizePublication: false,
                renderedSceneCount: 0,
                gate));
    }

    [Fact]
    public void OrdinaryPublicationAfterPresentationSelectsNoWake()
    {
        var gate = new NativeSceneUiWakeGate();

        Assert.Equal(
            NativeSceneUiWakePriority.None,
            NativeScenePublicationWakePolicy.Select(
                matchingResizePublication: false,
                renderedSceneCount: 1,
                gate));
        Assert.True(gate.TrySchedule());
    }

    [Fact]
    public void MatchingLiveResizePublicationSelectsAtMostOneImmediateWake()
    {
        var gate = new NativeSceneUiWakeGate();

        Assert.Equal(
            NativeSceneUiWakePriority.Immediate,
            NativeScenePublicationWakePolicy.Select(
                matchingResizePublication: true,
                renderedSceneCount: 10,
                gate));
        Assert.Equal(
            NativeSceneUiWakePriority.None,
            NativeScenePublicationWakePolicy.Select(
                matchingResizePublication: true,
                renderedSceneCount: 10,
                gate));
    }

    [Fact]
    public void ConcurrentFirstPublicationsSelectOneCoalescedWake()
    {
        var gate = new NativeSceneUiWakeGate();
        var selected = 0;

        Parallel.For(
            0,
            512,
            _ =>
            {
                if (NativeScenePublicationWakePolicy.Select(
                        matchingResizePublication: false,
                        renderedSceneCount: 0,
                        gate) == NativeSceneUiWakePriority.Normal)
                {
                    Interlocked.Increment(ref selected);
                }
            });

        Assert.Equal(1, selected);
    }

    [Fact]
    public void ActiveCompositionClockRearmsWithoutJavaScriptRafDemand()
    {
        Assert.True(
            NativeSceneCompositionFramePolicy.ShouldScheduleAnimationFrame(
                running: true,
                manualFrames: false,
                animationFrameScheduled: false));
    }

    [Theory]
    [InlineData(true, false, true, false)]
    [InlineData(true, true, false, false)]
    [InlineData(false, false, false, false)]
    public void CompositionClockStopsForScheduledPauseDetachAndManualModes(
        bool running,
        bool manualFrames,
        bool animationFrameScheduled,
        bool expected)
    {
        Assert.Equal(
            expected,
            NativeSceneCompositionFramePolicy.ShouldScheduleAnimationFrame(
                running,
                manualFrames,
                animationFrameScheduled));
    }

    [Fact]
    public void EmptyBoundaryDoesNotRequestInvalidation()
    {
        var gate = new NativeSceneInvalidationGate();
        var invalidationRequests = 0;

        if (NativeSceneCompositionFramePolicy.ShouldRequestRender(
                manualFrames: false,
                hasPendingPresentation: false)
            && gate.TryRequest())
        {
            invalidationRequests++;
        }

        Assert.Equal(0, invalidationRequests);
        Assert.False(gate.Complete());
    }

    [Fact]
    public void PendingPublicationRequestsOneCoalescedInvalidation()
    {
        var gate = new NativeSceneInvalidationGate();
        var invalidationRequests = 0;

        for (var publication = 0; publication < 2; publication++)
        {
            if (NativeSceneCompositionFramePolicy.ShouldRequestRender(
                    manualFrames: false,
                    hasPendingPresentation: true)
                && gate.TryRequest())
            {
                invalidationRequests++;
            }
        }

        Assert.Equal(1, invalidationRequests);
        Assert.True(gate.Complete());
        Assert.False(gate.Complete());
    }

    [Fact]
    public void ManualModeSuppressesAutomaticInvalidation()
    {
        Assert.False(
            NativeSceneCompositionFramePolicy.ShouldRequestRender(
                manualFrames: true,
                hasPendingPresentation: true));
    }
}

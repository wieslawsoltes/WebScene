using WebScene.Core;
using WebScene.Dom;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomEventTests
{
    [Fact]
    public void NodeIdentityIsStableUniqueAndIndependentOfBackendHandle()
    {
        var native = new object();
        var first = new RecordingElement(WebScene.Core.WebSceneBackendHandle.Create(native));
        var second = new RecordingElement(WebScene.Core.WebSceneBackendHandle.Create(native));
        var document = new RecordingDocument();

        Assert.Equal(first.__webSceneDomIdentity, first.DomNodeId.Value);
        Assert.NotEqual(first.DomNodeId, second.DomNodeId);
        Assert.NotEqual(first.DomNodeId, document.DomNodeId);
        Assert.Equal(first.BackendHandle, second.BackendHandle);
    }

    [Fact]
    public void CancelableEventPreventsDefaultAndSynchronizesHandledState()
    {
        var domEvent = new RecordingDomEvent(cancelable: true);

        domEvent.preventDefault();

        Assert.True(domEvent.defaultPrevented);
        Assert.True(domEvent.handled);
        Assert.True(domEvent.PlatformHandled);
    }

    [Fact]
    public void PassiveOrNonCancelableEventCannotPreventDefault()
    {
        var passive = new RecordingDomEvent(cancelable: true);
        passive.SetCurrentTarget(new object(), DomEventPhase.AtTarget, passive: true);
        passive.preventDefault();

        var nonCancelable = new RecordingDomEvent(cancelable: false);
        nonCancelable.preventDefault();

        Assert.False(passive.defaultPrevented);
        Assert.False(passive.handled);
        Assert.False(nonCancelable.defaultPrevented);
        Assert.False(nonCancelable.handled);
    }

    [Fact]
    public void PropagationAndComposedPathStateArePortableAndDefensive()
    {
        var first = new object();
        var second = new object();
        var domEvent = new RecordingDomEvent(cancelable: true);
        domEvent.SetComposedPath([first, second]);

        var returned = domEvent.composedPath();
        returned[0] = new object();
        domEvent.stopImmediatePropagation();

        Assert.Same(first, domEvent.composedPath()[0]);
        Assert.Same(second, domEvent.composedPath()[1]);
        Assert.True(domEvent.PropagationStopped);
        Assert.True(domEvent.ImmediatePropagationStopped);
    }

    [Fact]
    public void SyntheticEventCopiesDefaultPreventionToExternalState()
    {
        var externalDefaultPrevented = false;
        var domEvent = new DomSyntheticEvent(
            "submit",
            bubbles: true,
            cancelable: true,
            timeStamp: 10,
            detail: 42,
            new DefaultPreventedAccessor(value => externalDefaultPrevented = value));

        domEvent.preventDefault();
        domEvent.SyncDefaultPrevented();

        Assert.Equal(42, domEvent.detail);
        Assert.True(externalDefaultPrevented);
        Assert.False(domEvent.isTrusted);
    }

    [Fact]
    public void SyntheticEventRetainsOpaqueSourceEventForAdapterIdentity()
    {
        var sourceEvent = new object();
        var domEvent = new DomSyntheticEvent(
            "carousel",
            bubbles: true,
            cancelable: true,
            timeStamp: 11,
            detail: null,
            accessor: null,
            sourceEvent);

        var source = Assert.IsAssignableFrom<IExternalSyntheticEventSource>(domEvent);
        Assert.Same(sourceEvent, source.SourceEvent);
    }

    private sealed class RecordingDomEvent : DomEvent
    {
        public RecordingDomEvent(bool cancelable)
            : base(
                "test",
                bubbles: true,
                cancelable,
                initiallyHandled: false,
                timeStamp: 123,
                isTrusted: true)
        {
        }

        public bool PlatformHandled { get; private set; }

        protected override void OnHandledChanged(bool value)
            => PlatformHandled = value;
    }

    private sealed class RecordingElement : DomElementCore
    {
        public RecordingElement(WebScene.Core.WebSceneBackendHandle backendHandle)
            : base(backendHandle)
        {
        }
    }

    private sealed class RecordingDocument : DomDocumentCore
    {
    }
}

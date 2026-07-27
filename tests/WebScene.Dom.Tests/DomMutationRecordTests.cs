using JavaScript.Avalonia;
using Xunit;

namespace WebScene.Dom.Tests;

public sealed class DomMutationRecordTests
{
    [Fact]
    public void EmptyOptionsDefaultToChildListObservation()
    {
        var options = DomMutationObserverOptions.FromExternal(
            childList: false,
            attributes: false,
            subtree: false,
            attributeOldValue: false);

        Assert.True(options.MatchesRecordType(DomMutationRecord.ChildListType));
        Assert.False(options.MatchesRecordType(DomMutationRecord.AttributesType));
    }

    [Fact]
    public void AttributeOldValueEnablesAttributeObservation()
    {
        var options = DomMutationObserverOptions.FromExternal(
            childList: false,
            attributes: false,
            subtree: true,
            attributeOldValue: true);

        Assert.True(options.Attributes);
        Assert.True(options.AttributeOldValue);
        Assert.True(options.Subtree);
        Assert.True(options.MatchesRecordType(DomMutationRecord.AttributesType));
    }

    [Fact]
    public void ChildListRecordPreservesPortableNodeIdentityAndOrder()
    {
        var target = new RecordingElement();
        var first = new RecordingElement();
        var second = new RecordingElement();
        var previous = new RecordingElement();
        var next = new RecordingElement();

        var record = DomMutationRecord.CreateForChildList(
            target,
            new DomElementCore[] { first, second },
            removedNodes: null,
            previous,
            next);

        Assert.Same(target, record.target);
        Assert.Equal(new object[] { first, second }, record.addedNodes);
        Assert.Empty(record.removedNodes);
        Assert.Same(previous, record.previousSibling);
        Assert.Same(next, record.nextSibling);
        Assert.Equal(DomMutationRecord.ChildListType, record.type);
    }

    [Fact]
    public void AttributeRecordCloneOnlyExposesOldValueWhenRequested()
    {
        var target = new RecordingElement();
        var record = DomMutationRecord.CreateForAttribute(target, "class", "before");

        var withoutOldValue = record.Clone(includeOldValue: false);
        var withOldValue = record.Clone(includeOldValue: true);

        Assert.Null(withoutOldValue.oldValue);
        Assert.Equal("before", withOldValue.oldValue);
        Assert.Equal("class", withOldValue.attributeName);
        Assert.Same(target, withOldValue.target);
    }

    [Fact]
    public void ObserverCoreReplacesOptionsForTheSameTargetWithoutDuplicatingObservation()
    {
        var target = new RecordingElement();
        var observer = new RecordingObserver();

        observer.Observe(target, childList: true, attributes: false, subtree: false, attributeOldValue: false);
        observer.Observe(target, childList: false, attributes: true, subtree: true, attributeOldValue: true);

        Assert.Single(observer.CurrentObservations);
        Assert.Same(target, observer.CurrentObservations[0].Target);
        Assert.True(observer.CurrentObservations[0].Options.Attributes);
        Assert.True(observer.CurrentObservations[0].Options.Subtree);
        Assert.True(observer.CurrentObservations[0].Options.AttributeOldValue);
        Assert.False(observer.CurrentObservations[0].Options.ChildList);
    }

    [Fact]
    public void ObserverCoreQueuesPortableRecordsAndPreservesRequestedOldValue()
    {
        var target = new RecordingElement();
        var observer = new RecordingObserver();
        var record = DomMutationRecord.CreateForAttribute(target, "class", "before");

        observer.Enqueue(record, includeOldValue: true);
        var records = observer.TakeRecords();

        var queued = Assert.IsType<DomMutationRecord>(Assert.Single(records));
        Assert.Equal("before", queued.oldValue);
        Assert.Equal(0, observer.RecordCount);
        Assert.Empty(observer.TakeRecords());
    }

    [Fact]
    public void ObserverCoreDisconnectClearsObservationsAndQueuedRecords()
    {
        var target = new RecordingElement();
        var observer = new RecordingObserver();
        observer.Observe(target, childList: true, attributes: false, subtree: false, attributeOldValue: false);
        observer.Enqueue(DomMutationRecord.CreateForAttribute(target, "id", null), includeOldValue: false);

        observer.Disconnect();

        Assert.Empty(observer.CurrentObservations);
        Assert.Equal(0, observer.RecordCount);
        Assert.Empty(observer.TakeRecords());
    }

    private sealed class RecordingElement : DomElementCore
    {
        public RecordingElement()
            : base(default)
        {
        }
    }

    private sealed class RecordingObserver : DomMutationObserverCore<RecordingElement>
    {
        public IReadOnlyList<(RecordingElement Target, DomMutationObserverOptions Options)> CurrentObservations
        {
            get
            {
                var observations = new (RecordingElement, DomMutationObserverOptions)[Observations.Count];
                for (var index = 0; index < observations.Length; index++)
                {
                    observations[index] = (Observations[index].Target, Observations[index].Options);
                }
                return observations;
            }
        }

        public int RecordCount => QueuedRecordCount;

        public void Observe(
            RecordingElement target,
            bool childList,
            bool attributes,
            bool subtree,
            bool attributeOldValue)
            => ObserveCore(target, childList, attributes, subtree, attributeOldValue);

        public void Enqueue(DomMutationRecord record, bool includeOldValue)
            => QueueRecordCore(record, includeOldValue);

        public object[] TakeRecords() => TakeRecordsCore();

        public void Disconnect() => DisconnectCore();
    }
}

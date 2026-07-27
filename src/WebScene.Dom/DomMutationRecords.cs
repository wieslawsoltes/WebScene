using System.Runtime.CompilerServices;

namespace JavaScript.Avalonia;

public sealed class DomMutationObserverOptions
{
    public bool Attributes { get; private set; }
    public bool ChildList { get; private set; }
    public bool Subtree { get; private set; }
    public bool AttributeOldValue { get; private set; }

    public static DomMutationObserverOptions FromExternal(
        bool childList,
        bool attributes,
        bool subtree,
        bool attributeOldValue)
    {
        var options = new DomMutationObserverOptions
        {
            ChildList = childList,
            Attributes = attributes,
            Subtree = subtree,
            AttributeOldValue = attributeOldValue
        };

        if (!options.Attributes && !options.ChildList)
        {
            options.ChildList = true;
        }
        if (options.AttributeOldValue)
        {
            options.Attributes = true;
        }
        return options;
    }

    public bool MatchesRecordType(string type)
        => type switch
        {
            DomMutationRecord.ChildListType => ChildList,
            DomMutationRecord.AttributesType => Attributes,
            _ => false
        };
}

/// <summary>
/// Framework-neutral MutationObserver registration and record-queue state.
/// Backend adapters retain direct ownership of tree traversal and JavaScript
/// callback delivery so those paths do not acquire an abstraction boundary.
/// </summary>
public abstract class DomMutationObserverCore<TNode>
    where TNode : DomElementCore
{
    private readonly List<DomMutationRecord> _queue = new();
    private readonly List<Observation> _observations = new();

    protected int QueuedRecordCount => _queue.Count;

    protected List<Observation> Observations => _observations;

    protected void ObserveCore(
        TNode target,
        bool childList,
        bool attributes,
        bool subtree,
        bool attributeOldValue)
    {
        var options = DomMutationObserverOptions.FromExternal(
            childList,
            attributes,
            subtree,
            attributeOldValue);
        for (var index = 0; index < _observations.Count; index++)
        {
            var observation = _observations[index];
            if (!ReferenceEquals(observation.Target, target))
            {
                continue;
            }

            observation.Options = options;
            return;
        }

        _observations.Add(new Observation(target, options));
    }

    protected void DisconnectCore()
    {
        _observations.Clear();
        _queue.Clear();
    }

    protected object[] TakeRecordsCore()
    {
        if (_queue.Count == 0)
        {
            return [];
        }

        var records = new object[_queue.Count];
        for (var index = 0; index < records.Length; index++)
        {
            records[index] = _queue[index];
        }
        _queue.Clear();
        return records;
    }

    protected DomMutationRecord[] DrainRecordsCore()
    {
        if (_queue.Count == 0)
        {
            return [];
        }

        var records = _queue.ToArray();
        _queue.Clear();
        return records;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    protected void QueueRecordCore(DomMutationRecord record, bool includeOldValue)
        => _queue.Add(record.Clone(includeOldValue));

    protected sealed class Observation
    {
        internal Observation(TNode target, DomMutationObserverOptions options)
        {
            Target = target;
            Options = options;
        }

        public TNode Target { get; }

        public DomMutationObserverOptions Options { get; set; }
    }
}

public sealed class DomMutationRecord
{
    internal const string ChildListType = "childList";
    internal const string AttributesType = "attributes";

    private readonly DomElementCore _target;
    private readonly DomElementCore[] _addedNodes;
    private readonly DomElementCore[] _removedNodes;
    private readonly DomElementCore? _previousSibling;
    private readonly DomElementCore? _nextSibling;
    private readonly string? _attributeName;
    private readonly string? _oldValue;
    private readonly string _type;

    private DomMutationRecord(
        string type,
        DomElementCore target,
        DomElementCore[] addedNodes,
        DomElementCore[] removedNodes,
        DomElementCore? previousSibling,
        DomElementCore? nextSibling,
        string? attributeName,
        string? oldValue)
    {
        _type = type;
        _target = target;
        _addedNodes = addedNodes;
        _removedNodes = removedNodes;
        _previousSibling = previousSibling;
        _nextSibling = nextSibling;
        _attributeName = attributeName;
        _oldValue = oldValue;
    }

    internal static DomMutationRecord CreateForChildList(
        DomElementCore target,
        IReadOnlyList<DomElementCore>? addedNodes,
        IReadOnlyList<DomElementCore>? removedNodes,
        DomElementCore? previousSibling,
        DomElementCore? nextSibling)
        => new(
            ChildListType,
            target,
            CopyNodes(addedNodes),
            CopyNodes(removedNodes),
            previousSibling,
            nextSibling,
            null,
            null);

    private static DomElementCore[] CopyNodes(IReadOnlyList<DomElementCore>? nodes)
    {
        if (nodes is null || nodes.Count == 0)
        {
            return [];
        }

        var copy = new DomElementCore[nodes.Count];
        for (var index = 0; index < copy.Length; index++)
        {
            copy[index] = nodes[index];
        }
        return copy;
    }

    internal static DomMutationRecord CreateForAttribute(
        DomElementCore target,
        string attributeName,
        string? oldValue)
        => new(
            AttributesType,
            target,
            [],
            [],
            null,
            null,
            attributeName,
            oldValue);

    internal DomMutationRecord Clone(bool includeOldValue)
        => new(
            _type,
            _target,
            _addedNodes,
            _removedNodes,
            _previousSibling,
            _nextSibling,
            _attributeName,
            includeOldValue ? _oldValue : null);

    internal DomElementCore TargetElement => _target;

    public string type => _type;

    public DomElementCore target => _target;

    public object[] addedNodes
        => _addedNodes.Length == 0 ? [] : _addedNodes.Cast<object>().ToArray();

    public object[] removedNodes
        => _removedNodes.Length == 0 ? [] : _removedNodes.Cast<object>().ToArray();

    public DomElementCore? previousSibling => _previousSibling;

    public DomElementCore? nextSibling => _nextSibling;

    public string? attributeName => _attributeName;

    public string? oldValue => _oldValue;
}

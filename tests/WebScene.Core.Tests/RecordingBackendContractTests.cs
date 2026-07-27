using WebScene.Core;
using Xunit;

namespace WebScene.Core.Tests;

public sealed class RecordingBackendContractTests
{
    [Fact]
    public void BackendRecordsOrderedTreeLayoutInvalidationAndHitTesting()
    {
        var services = new RecordingHostServices();
        using var backend = new RecordingBackend(services);

        backend.Mount();
        var root = backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(1), WebSceneBackendNodeKind.Container, "root"));
        var child = backend.CreateNode(new WebSceneBackendNodeDescriptor(
            new WebSceneNodeId(2), WebSceneBackendNodeKind.Canvas, "chart"));

        backend.Attach(root, child, 0);
        backend.Arrange(root, new WebSceneRect(0, 0, 640, 480));
        backend.Arrange(child, new WebSceneRect(20, 30, 200, 100));
        backend.SetZIndex(child, 4);
        backend.SetVisible(child, true);
        backend.Invalidate(child, WebSceneInvalidationKind.Arrange | WebSceneInvalidationKind.Render);

        Assert.Equal(child, backend.HitTest(new WebScenePoint(50, 60)));
        backend.Detach(child);
        Assert.Equal(
            new[]
            {
                "mount", "create:1:Container", "create:2:Canvas", "attach:1:2:0",
                "arrange:1:0,0,640,480", "arrange:2:20,30,200,100", "z:2:4",
                "visible:2:True", "invalidate:2:Arrange, Render", "hit:50,60:2",
                "detach:2"
            },
            backend.Operations);
    }

    [Fact]
    public void BackendEnforcesMountUnmountRemountAndDisposeOrder()
    {
        var services = new RecordingHostServices();
        var backend = new RecordingBackend(services);

        Assert.Equal(WebSceneBackendState.Created, backend.State);
        Assert.Throws<InvalidOperationException>(() => backend.CreateNode(default));

        backend.Mount();
        backend.Mount();
        backend.Unmount();
        backend.Unmount();
        backend.Mount();
        backend.Dispose();
        backend.Dispose();

        Assert.Equal(WebSceneBackendState.Disposed, backend.State);
        Assert.Equal(new[] { "mount", "unmount", "mount", "unmount", "dispose" }, backend.Operations);
        Assert.Throws<ObjectDisposedException>(() => backend.Mount());
    }

    [Fact]
    public void BackendRejectsInvalidLifetimesAndUnknownNodes()
    {
        var services = new RecordingHostServices();
        using var backend = new RecordingBackend(services);

        Assert.Throws<InvalidOperationException>(() => backend.Unmount());
        backend.Mount();
        Assert.Throws<ArgumentException>(() => backend.CreateNode(default));
        Assert.Throws<InvalidOperationException>(
            () => backend.Arrange(
                new WebSceneBackendNode(new WebSceneNodeId(42), WebSceneBackendHandle.Create(new object())),
                WebSceneRect.Empty));
    }

    [Fact]
    public void CapabilityNegotiationIsImmutableDiagnosticAndFailFast()
    {
        var services = new RecordingHostServices();
        using var backend = new RecordingBackend(services);
        var advertised = backend.Capabilities;

        backend.EnsureCapabilities(WebSceneBackendCapabilities.DomProjection | WebSceneBackendCapabilities.Canvas2D);
        var error = Assert.Throws<WebSceneBackendCapabilityException>(
            () => backend.EnsureCapabilities(
                WebSceneBackendCapabilities.DomProjection | WebSceneBackendCapabilities.Accessibility));

        Assert.Equal(advertised, backend.Capabilities);
        Assert.Equal(WebSceneBackendCapabilities.Accessibility, error.Missing);
        Assert.Equal(advertised, error.Available);
        Assert.Single(backend.Diagnostics);
        Assert.Equal("backend.capability", backend.Diagnostics[0].Category);
        Assert.Contains("Accessibility", backend.Diagnostics[0].Message, StringComparison.Ordinal);
    }

    [Fact]
    public void BackendRejectsOperationsFromTheWrongDispatcher()
    {
        var services = new RecordingHostServices();
        var backend = new RecordingBackend(services);
        services.DispatcherImpl.HasAccess = false;

        Assert.Throws<InvalidOperationException>(() => backend.Mount());
        services.DispatcherImpl.HasAccess = true;
        backend.Dispose();
    }

    [Fact]
    public void BackendHandleUsesReferenceIdentityAndKeepsNativeTypeOpaque()
    {
        var native = new object();
        var first = WebSceneBackendHandle.Create(native);
        var second = WebSceneBackendHandle.Create(native);
        var other = WebSceneBackendHandle.Create(new object());

        Assert.Equal(first, second);
        Assert.NotEqual(first, other);
        Assert.Equal(typeof(object), first.NativeType);
        Assert.Same(native, first.GetRequired<object>());
        Assert.False(first.TryGet<string>(out _));
    }

    [Fact]
    public void PortableValuesHaveStableEmptyBoundaryAndIdentitySemantics()
    {
        Assert.True(WebSceneSize.Empty.IsEmpty);
        Assert.True(new WebSceneSize(-1, 2).IsEmpty);
        Assert.False(new WebSceneSize(1, 2).IsEmpty);

        Assert.Equal(new WebSceneSize(30, 40), new WebSceneRect(10, 20, 30, 40).Size);
        Assert.True(new WebSceneRect(10, 20, 30, 40).Contains(new WebScenePoint(10, 20)));
        Assert.False(new WebSceneRect(10, 20, 30, 40).Contains(new WebScenePoint(40, 60)));
        Assert.Equal(WebSceneRect.Empty, new WebSceneRect());

        Assert.Equal(new WebSceneColor(0, 0, 0, 0), WebSceneColor.Transparent);
        Assert.Equal(new WebSceneColor(255, 1, 2, 3), WebSceneColor.FromRgb(1, 2, 3));
        Assert.True(default(WebSceneNodeId).IsEmpty);
        Assert.Equal("123", new WebSceneNodeId(123).ToString());

        Assert.True(default(WebSceneBackendHandle).IsEmpty);
        Assert.Equal(0, default(WebSceneBackendHandle).GetHashCode());
        Assert.Throws<ArgumentNullException>(() => WebSceneBackendHandle.Create(null!));
        Assert.Throws<InvalidOperationException>(() => default(WebSceneBackendHandle).GetRequired<object>());

        var native = new object();
        var first = WebSceneBackendHandle.Create(native);
        var second = WebSceneBackendHandle.Create(native);
        Assert.True(first == second);
        Assert.False(first != second);
        Assert.True(first.Equals((object)second));
        Assert.NotEqual(0, first.GetHashCode());

        Assert.True(default(WebSceneBackendNode).IsEmpty);
        Assert.False(new WebSceneBackendNode(new WebSceneNodeId(1), first).IsEmpty);
    }

    [Fact]
    public void PortableTimingAndViewportValuesPreserveEmptyAndChangeState()
    {
        Assert.True(default(WebSceneFrameRequest).IsEmpty);
        Assert.False(new WebSceneFrameRequest(1).IsEmpty);
        Assert.Equal(new WebSceneSize(0, 0), WebSceneViewportMetrics.Empty.ClientSize);
        Assert.Equal(1, WebSceneViewportMetrics.Empty.DeviceScaleFactor);
        Assert.False(WebSceneViewportMetrics.Empty.IsVisible);

        var previous = WebSceneViewportMetrics.Empty;
        var current = new WebSceneViewportMetrics(new WebSceneSize(800, 600), 2, true);
        var changed = new WebSceneViewportChangedEventArgs(previous, current);
        Assert.Equal(previous, changed.Previous);
        Assert.Equal(current, changed.Current);
    }

    [Fact]
    public void BaseBackendDefaultHooksRemainValidForMinimalAdapters()
    {
        var services = new RecordingHostServices();
        var backend = new MinimalBackend(services);

        backend.Mount();
        backend.Unmount();
        backend.Dispose();

        Assert.Equal(WebSceneBackendState.Disposed, backend.State);
    }

    private sealed class RecordingBackend : WebSceneBackendHostBase
    {
        private readonly Dictionary<WebSceneNodeId, NodeState> _nodes = new();
        private readonly List<WebSceneBackendDiagnostic> _diagnostics = new();

        public RecordingBackend(IWebSceneHostServices services)
            : base(
                services,
                new RecordingInputSource(),
                WebSceneBackendCapabilities.DomProjection
                | WebSceneBackendCapabilities.CssLayout
                | WebSceneBackendCapabilities.Canvas2D
                | WebSceneBackendCapabilities.PointerInput)
        {
        }

        public List<string> Operations { get; } = new();

        public override IReadOnlyList<WebSceneBackendDiagnostic> Diagnostics => _diagnostics;

        protected override void OnMount() => Operations.Add("mount");

        protected override void OnUnmount() => Operations.Add("unmount");

        protected override void DisposeCore() => Operations.Add("dispose");

        protected override void ReportDiagnostic(WebSceneBackendDiagnostic diagnostic)
            => _diagnostics.Add(diagnostic);

        public override WebSceneBackendNode CreateNode(in WebSceneBackendNodeDescriptor descriptor)
        {
            RequireMounted();
            if (descriptor.Id.IsEmpty)
            {
                throw new ArgumentException("A non-empty DOM node id is required.", nameof(descriptor));
            }

            var handle = WebSceneBackendHandle.Create(new object());
            var node = new WebSceneBackendNode(descriptor.Id, handle);
            _nodes.Add(descriptor.Id, new NodeState(node));
            Operations.Add($"create:{descriptor.Id}:{descriptor.Kind}");
            return node;
        }

        public override void Attach(WebSceneBackendNode parent, WebSceneBackendNode child, int index)
        {
            RequireMounted();
            var parentState = Get(parent);
            var childState = Get(child);
            childState.Parent = parent.Id;
            parentState.Children.Insert(index, child.Id);
            Operations.Add($"attach:{parent.Id}:{child.Id}:{index}");
        }

        public override void Detach(WebSceneBackendNode node)
        {
            RequireMounted();
            var state = Get(node);
            if (state.Parent is { } parent && _nodes.TryGetValue(parent, out var parentState))
            {
                parentState.Children.Remove(node.Id);
            }

            state.Parent = null;
            Operations.Add($"detach:{node.Id}");
        }

        public override void Arrange(WebSceneBackendNode node, WebSceneRect bounds)
        {
            RequireMounted();
            Get(node).Bounds = bounds;
            Operations.Add($"arrange:{node.Id}:{bounds.X},{bounds.Y},{bounds.Width},{bounds.Height}");
        }

        public override void SetVisible(WebSceneBackendNode node, bool visible)
        {
            RequireMounted();
            Get(node).Visible = visible;
            Operations.Add($"visible:{node.Id}:{visible}");
        }

        public override void SetZIndex(WebSceneBackendNode node, int zIndex)
        {
            RequireMounted();
            Get(node).ZIndex = zIndex;
            Operations.Add($"z:{node.Id}:{zIndex}");
        }

        public override void Invalidate(WebSceneBackendNode node, WebSceneInvalidationKind kind)
        {
            RequireMounted();
            Get(node).Invalidation |= kind;
            Operations.Add($"invalidate:{node.Id}:{kind}");
        }

        public override WebSceneBackendNode? HitTest(WebScenePoint point)
        {
            RequireMounted();
            var hit = _nodes.Values
                .Where(node => node.Visible && node.Bounds.Contains(point))
                .OrderByDescending(node => node.ZIndex)
                .ThenByDescending(node => node.Node.Id.Value)
                .Select(node => (WebSceneBackendNode?)node.Node)
                .FirstOrDefault();
            Operations.Add($"hit:{point.X},{point.Y}:{hit?.Id.ToString() ?? "none"}");
            return hit;
        }

        private NodeState Get(WebSceneBackendNode node)
            => _nodes.TryGetValue(node.Id, out var state) && state.Node.Handle == node.Handle
                ? state
                : throw new InvalidOperationException($"Unknown backend node '{node.Id}'.");

        private sealed class NodeState(WebSceneBackendNode node)
        {
            public WebSceneBackendNode Node { get; } = node;
            public WebSceneNodeId? Parent { get; set; }
            public List<WebSceneNodeId> Children { get; } = new();
            public WebSceneRect Bounds { get; set; }
            public bool Visible { get; set; } = true;
            public int ZIndex { get; set; }
            public WebSceneInvalidationKind Invalidation { get; set; }
        }
    }

    private sealed class MinimalBackend(IWebSceneHostServices services)
        : WebSceneBackendHostBase(services, services.Input, WebSceneBackendCapabilities.None)
    {
        public override IReadOnlyList<WebSceneBackendDiagnostic> Diagnostics
            => Array.Empty<WebSceneBackendDiagnostic>();

        protected override void ReportDiagnostic(WebSceneBackendDiagnostic diagnostic)
        {
        }

        public override WebSceneBackendNode CreateNode(in WebSceneBackendNodeDescriptor descriptor)
            => throw new NotSupportedException();
        public override void Attach(WebSceneBackendNode parent, WebSceneBackendNode child, int index)
            => throw new NotSupportedException();
        public override void Detach(WebSceneBackendNode node) => throw new NotSupportedException();
        public override void Arrange(WebSceneBackendNode node, WebSceneRect bounds) => throw new NotSupportedException();
        public override void SetVisible(WebSceneBackendNode node, bool visible) => throw new NotSupportedException();
        public override void SetZIndex(WebSceneBackendNode node, int zIndex) => throw new NotSupportedException();
        public override void Invalidate(WebSceneBackendNode node, WebSceneInvalidationKind kind)
            => throw new NotSupportedException();
        public override WebSceneBackendNode? HitTest(WebScenePoint point) => null;
    }

    private sealed class RecordingHostServices : IWebSceneHostServices
    {
        private readonly object _root = new();

        public RecordingHostServices()
        {
            DispatcherImpl = new RecordingDispatcher();
            Dispatcher = DispatcherImpl;
        }

        public RecordingDispatcher DispatcherImpl { get; }
        public WebSceneBackendHandle RootHandle => WebSceneBackendHandle.Create(_root);
        public IWebSceneDispatcher Dispatcher { get; }
        public IWebSceneClock Clock { get; } = new RecordingClock();
        public IWebSceneFrameScheduler Frames { get; } = new RecordingFrames();
        public IWebSceneViewport Viewport { get; } = new RecordingViewport();
        public IWebSceneResourceLoader Resources { get; } = new RecordingResources();
        public IWebSceneClipboard Clipboard { get; } = new RecordingClipboard();
        public IWebSceneInputSource Input { get; } = new RecordingInputSource();
    }

    private sealed class RecordingDispatcher : IWebSceneDispatcher
    {
        public bool HasAccess { get; set; } = true;
        public bool CheckAccess() => HasAccess;
        public void VerifyAccess()
        {
            if (!HasAccess) throw new InvalidOperationException("The operation requires dispatcher access.");
        }
        public void Post(Action callback, WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default) => callback();
        public IWebSceneScheduledWork Schedule(TimeSpan delay, Action callback, WebSceneDispatchPriority priority = WebSceneDispatchPriority.Default)
        {
            callback();
            return new RecordingScheduledWork();
        }
    }

    private sealed class RecordingScheduledWork : IWebSceneScheduledWork
    {
        public bool IsCancellationRequested { get; private set; }
        public void Cancel() => IsCancellationRequested = true;
        public void Dispose() => Cancel();
    }

    private sealed class RecordingClock : IWebSceneClock
    {
        public TimeSpan Elapsed => TimeSpan.Zero;
    }

    private sealed class RecordingFrames : IWebSceneFrameScheduler
    {
        public WebSceneFrameRequest RequestFrame(Action<TimeSpan> callback)
        {
            callback(TimeSpan.Zero);
            return new WebSceneFrameRequest(1);
        }
        public bool CancelFrame(WebSceneFrameRequest request) => !request.IsEmpty;
    }

    private sealed class RecordingViewport : IWebSceneViewport
    {
        public WebSceneViewportMetrics HostMetrics { get; } = new(new WebSceneSize(800, 600), 1, true);
        public WebSceneViewportMetrics Metrics { get; } = new(new WebSceneSize(800, 600), 1, true);
        public event EventHandler<WebSceneViewportChangedEventArgs>? Changed
        {
            add { }
            remove { }
        }
    }

    private sealed class RecordingResources : IWebSceneResourceLoader
    {
        public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
            => new(request.Specifier, string.Empty, request.Specifier, null);
    }

    private sealed class RecordingClipboard : IWebSceneClipboard
    {
        public string? Text { get; private set; }
        public string? GetText() => Text;
        public void SetText(string? text) => Text = text;
    }

    private sealed class RecordingInputSource : IWebSceneInputSource
    {
        public event EventHandler<WebScenePointerInputEventArgs>? Pointer
        {
            add { }
            remove { }
        }
        public event EventHandler<WebSceneKeyboardInputEventArgs>? Keyboard
        {
            add { }
            remove { }
        }
        public event EventHandler<WebSceneTextInputEventArgs>? TextInput
        {
            add { }
            remove { }
        }
    }
}

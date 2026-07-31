using System.Collections.Concurrent;
using System.Buffers;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
#if !WEBSCENE_UNO
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Rendering.SceneGraph;
using Avalonia.Skia;
using Avalonia.Threading;
using JavaScript.Avalonia;
#endif
using WebScene.Core;
using WebScene.Css;
using WebScene.JavaScript.Interop;
using SkiaSharp;
using SkiaSharp.HarfBuzz;
using Svg.Skia;

#if WEBSCENE_UNO
namespace WebScene.Backends.Uno.Native;
#else
namespace WebScene.Backends.Avalonia.Native;
#endif

public interface INativeWebSceneRenderDiagnostics
{
    long RenderedSceneCount { get; }

    long FirstRenderedSceneTimestamp { get; }

    long FirstReadySceneTimestamp { get; }

    ulong PublishedSceneCount { get; }

    void SubmitAnimationFrame(double timestampMilliseconds);

    void RequestRender();
}

#if !WEBSCENE_UNO
public interface INativeWebSceneFrozenPresentation : IDisposable
{
    Control View { get; }

    ulong EstimatedBytes { get; }
}
#endif

internal sealed class NativeSceneRenderObserver
{
    private const uint SceneComponentReady = 4;
    private readonly object _viewportGate = new();
    private readonly List<int> _renderedViewportHeights = [];
    private readonly Queue<NativeSceneRenderSample> _renderedScenes = new(4096);
    private long _renderedSceneCount;
    private long _firstRenderedSceneTimestamp;
    private long _firstReadySceneTimestamp;

    public long RenderedSceneCount => Volatile.Read(ref _renderedSceneCount);

    public long FirstRenderedSceneTimestamp
        => Volatile.Read(ref _firstRenderedSceneTimestamp);

    public long FirstReadySceneTimestamp
        => Volatile.Read(ref _firstReadySceneTimestamp);

    public int[] RenderedViewportHeights
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedViewportHeights.ToArray();
            }
        }
    }

    public long[] RenderedSceneTimestamps
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedScenes
                    .Select(sample => sample.Timestamp)
                    .ToArray();
            }
        }
    }

    public NativeSceneRenderSample[] RenderedScenes
    {
        get
        {
            lock (_viewportGate)
            {
                return _renderedScenes.ToArray();
            }
        }
    }

    public void RecordRendered(in SceneHeader header)
    {
        var timestamp = Stopwatch.GetTimestamp();
        Interlocked.CompareExchange(ref _firstRenderedSceneTimestamp, timestamp, 0);
        if ((header.Flags & SceneComponentReady) != 0)
        {
            Interlocked.CompareExchange(ref _firstReadySceneTimestamp, timestamp, 0);
        }
        var viewportHeight = (int)Math.Round(header.ViewportHeight);
        lock (_viewportGate)
        {
            if (_renderedScenes.Count == 4096)
            {
                _renderedScenes.Dequeue();
            }
            _renderedScenes.Enqueue(new NativeSceneRenderSample(
                timestamp,
                header.Revision,
                header.ConsumedInputSequence));
            if (_renderedViewportHeights.Count == 0
                || _renderedViewportHeights[^1] != viewportHeight)
            {
                _renderedViewportHeights.Add(viewportHeight);
            }
        }
        Interlocked.Increment(ref _renderedSceneCount);
    }
}

#if !WEBSCENE_UNO
public sealed class NativeSceneSurface : Control, INativeWebSceneRenderDiagnostics
{
    private IntPtr _engine;
    private readonly bool _useCompositionVisual;
    private readonly bool _submitAnimationFrames;
    private readonly NativeCanvasSceneRenderer _renderer = new();
    private readonly object _rendererGate = new();
    private readonly NativeSceneRenderObserver _renderObserver = new();
    private long _sequence = DateTime.UtcNow.Ticks;
    private long _lastResizeSequence;
    private long _lastResizeTimestamp;
    private long _routedInputEvents;
    private long _acceptedInputEvents;
    private long _resizePublicationNotificationCount;
    private long _lastNotifiedResizeSequence;
    private long _liveResizeDeadlineTimestamp;
    private long _lastSurfaceResizeHandlerTicks;
    private readonly object _scenePublicationGate = new();
    private readonly Queue<NativeScenePublicationSample> _publishedScenes = new(4096);
    private readonly Queue<NativeResizeSubmissionSample> _submittedResizes = new(4096);
    private readonly NativeScenePublicationMailbox _compositionMailbox = new();
    private readonly NativeSceneUiWakeGate _compositionUiWakeGate = new();
    private NativeScenePublished _lastPublishedScene;
    private double _lastPointerX;
    private double _lastPointerY;
    private bool _frameLoopActive;
    private int _frameCallbackScheduled;
    private bool _presentationActive = true;
    private bool _pointerDown;
    private int _lastCursorKind = -1;
    private int _compositionProjectionActive;
    private long _compositionUiWakeCount;
    private CompositionCustomVisual? _customVisual;

    public NativeSceneSurface(
        IntPtr engine,
        bool useCompositionVisual = false,
        bool submitAnimationFrames = true)
    {
        _engine = engine;
        _useCompositionVisual = useCompositionVisual
            && !string.Equals(
                Environment.GetEnvironmentVariable(
                    "WEBSCENE_AVALONIA_DIRECT_DRAW"),
                "1",
                StringComparison.Ordinal);
        _submitAnimationFrames = submitAnimationFrames;
        Focusable = true;
        ClipToBounds = true;
        AddHandler(
            InputElement.PointerMovedEvent,
            OnPointerMoved,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerPressedEvent,
            OnPointerPressed,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerReleasedEvent,
            OnPointerReleased,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerWheelChangedEvent,
            OnPointerWheelChanged,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.KeyDownEvent,
            OnKeyDown,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.KeyUpEvent,
            OnKeyUp,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(
            InputElement.TextInputEvent,
            OnTextInput,
            RoutingStrategies.Direct | RoutingStrategies.Bubble,
            handledEventsToo: true);
        PointerCaptureLost += OnPointerCaptureLost;
        SizeChanged += OnSurfaceSizeChanged;
        AttachedToVisualTree += OnSurfaceAttached;
        DetachedFromVisualTree += OnSurfaceDetached;
    }

    public void SetEngine(IntPtr engine)
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            // Engine startup continues on a worker after native V8 prewarming.
            // Do not let bootstrap scripts race the UI-thread attachment and
            // its first real viewport resize.
            Dispatcher.UIThread.InvokeAsync(
                    () => SetEngine(engine),
                    DispatcherPriority.Send)
                .GetAwaiter()
                .GetResult();
            return;
        }
        if (_engine == engine) return;

        if (_customVisual is not null)
        {
            Volatile.Write(ref _compositionProjectionActive, 0);
            _customVisual.SendHandlerMessage(NativeSceneCompositionMessage.Stop);
            ElementComposition.SetElementChildVisual(this, null);
            _customVisual = null;
            _compositionUiWakeGate.Reset();
        }
        lock (_rendererGate)
        {
            _renderer.Reset();
        }
        _compositionMailbox.Reset();
        _compositionUiWakeGate.Reset();
        _engine = engine;
        if (engine == IntPtr.Zero || VisualRoot is null) return;

        NativeWebSceneApi.EngineSetVisible(_engine, 1);
        StartProjection();
        SubmitResize(Bounds.Width, Bounds.Height);
    }

    private void OnSurfaceAttached(object? sender, VisualTreeAttachmentEventArgs args)
    {
        if (_engine == IntPtr.Zero)
        {
            return;
        }
        NativeWebSceneApi.EngineSetVisible(_engine, _presentationActive ? (byte)1 : (byte)0);
        StartProjection();
        if (!_presentationActive)
        {
            PauseProjection();
        }
        SubmitResize(Bounds.Width, Bounds.Height);
    }

    private void StartProjection()
    {
        // A newly attached renderer has no retained base revision. Ask the
        // producer for a full checkpoint so compositor recreation, reparenting,
        // and graphics-context recovery cannot begin in the middle of a diff
        // chain. The old consumer is stopped before this method is entered.
        NativeWebSceneApi.EngineRequestSceneCheckpoint(_engine);
        if (_useCompositionVisual)
        {
            var compositor = ElementComposition.GetElementVisual(this)?.Compositor;
            if (compositor is not null)
            {
                _customVisual = compositor.CreateCustomVisual(
                    new NativeSceneCompositionHandler(
                        _engine,
                        _renderObserver,
                        _compositionMailbox,
                        _compositionUiWakeGate,
                        ScheduleCompositionUiWake));
                _customVisual.Size = new Vector2((float)Bounds.Width, (float)Bounds.Height);
                ElementComposition.SetElementChildVisual(this, _customVisual);
                Volatile.Write(ref _compositionProjectionActive, 1);
                _customVisual.SendHandlerMessage(NativeSceneCompositionMessage.Start);
                return;
            }
        }

        _frameLoopActive = true;
        RequestNextFrame();
    }

    private void OnSurfaceDetached(object? sender, VisualTreeAttachmentEventArgs args)
    {
        PauseProjection();
        if (_customVisual is not null)
        {
            Volatile.Write(ref _compositionProjectionActive, 0);
            _customVisual.SendHandlerMessage(NativeSceneCompositionMessage.Stop);
            ElementComposition.SetElementChildVisual(this, null);
            _customVisual = null;
            _compositionUiWakeGate.Reset();
        }
        else
        {
            // Reattachment requests a full checkpoint. Release old retained
            // pictures immediately while this surface is absent rather than
            // waiting for the control and native handles to be collected.
            lock (_rendererGate)
            {
                _renderer.Reset();
            }
        }
        if (_engine != IntPtr.Zero)
        {
            // A detached surface no longer has a presentation deadline. Native
            // visibility policy debounces transient reparenting, then reclaims
            // V8 heap pages on this surface's worker. Reattachment cancels it.
            NativeWebSceneApi.EngineSetVisible(_engine, 0);
        }
    }

    /// <summary>
    /// Explicitly controls whether an attached surface has presentation deadlines.
    /// Inactive surfaces retain their last presentation, stop their frame clock,
    /// and enter the native engine's debounced hidden-memory policy. Reactivation
    /// cancels pending reclamation and resumes from a full scene checkpoint.
    /// </summary>
    public void SetPresentationActive(bool active)
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            Dispatcher.UIThread.InvokeAsync(
                    () => SetPresentationActive(active),
                    DispatcherPriority.Send)
                .GetAwaiter()
                .GetResult();
            return;
        }
        if (_presentationActive == active)
        {
            return;
        }

        _presentationActive = active;
        var engine = _engine;
        if (engine == IntPtr.Zero || VisualRoot is null)
        {
            return;
        }

        if (!active)
        {
            PauseProjection();
            NativeWebSceneApi.EngineSetVisible(engine, 0);
            return;
        }

        NativeWebSceneApi.EngineSetVisible(engine, 1);
        NativeWebSceneApi.EngineRequestSceneCheckpoint(engine);
        if (_customVisual is not null)
        {
            _customVisual.SendHandlerMessage(
                NativeSceneCompositionMessage.ResumeAnimationFrames);
            _customVisual.SendHandlerMessage(NativeSceneCompositionMessage.SceneWake);
        }
        else if (!_frameLoopActive)
        {
            _frameLoopActive = true;
            RequestNextFrame();
        }
        InvalidateVisual();
    }

    private void PauseProjection()
    {
        _frameLoopActive = false;
        _customVisual?.SendHandlerMessage(
            NativeSceneCompositionMessage.PauseAnimationFrames);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        var arranged = base.ArrangeOverride(finalSize);
        if (_customVisual is not null)
        {
            _customVisual.Size = new Vector2((float)arranged.Width, (float)arranged.Height);
        }
        return arranged;
    }

    private ulong NextSequence() => unchecked((ulong)Interlocked.Increment(ref _sequence));

    public ulong LastResizeSequence
        => unchecked((ulong)Interlocked.Read(ref _lastResizeSequence));

    public long LastResizeTimestamp
        => Interlocked.Read(ref _lastResizeTimestamp);

    public long RoutedInputEvents
        => Interlocked.Read(ref _routedInputEvents);

    public long AcceptedInputEvents
        => Interlocked.Read(ref _acceptedInputEvents);

    public long ResizePublicationNotificationCount
        => Interlocked.Read(ref _resizePublicationNotificationCount);

    public long CompositionSceneUiWakeCount
        => Interlocked.Read(ref _compositionUiWakeCount);

    public long PendingCompositionScenePublications
        => _compositionMailbox.PendingCount;

    public long LastSurfaceResizeHandlerTicks
        => Interlocked.Read(ref _lastSurfaceResizeHandlerTicks);

    public int[] RenderedViewportHeights
        => _renderObserver.RenderedViewportHeights;

    public long[] RenderedSceneTimestamps
        => _renderObserver.RenderedSceneTimestamps;

    public NativeSceneRenderSample[] RenderedScenes
        => _renderObserver.RenderedScenes;

    public NativeScenePublicationSample[] PublishedScenes
    {
        get
        {
            lock (_scenePublicationGate)
            {
                return _publishedScenes.ToArray();
            }
        }
    }

    public NativeResizeSubmissionSample[] SubmittedResizes
    {
        get
        {
            lock (_scenePublicationGate)
            {
                return _submittedResizes.ToArray();
            }
        }
    }

    public long RenderedSceneCount
        => _renderObserver.RenderedSceneCount;

    public NativeRendererMemoryMetrics GetRendererMemoryMetrics()
    {
        lock (_rendererGate)
        {
            return _renderer.ReadMemoryMetrics();
        }
    }

    public long FirstRenderedSceneTimestamp
        => _renderObserver.FirstRenderedSceneTimestamp;

    public long FirstReadySceneTimestamp
        => _renderObserver.FirstReadySceneTimestamp;

    public ulong PublishedSceneCount
    {
        get
        {
            var engine = Volatile.Read(ref _engine);
            if (engine == IntPtr.Zero)
            {
                return 0;
            }
            NativeWebSceneApi.EngineGetMetrics(engine, out var metrics);
            return metrics.PublishedScenes;
        }
    }

    public ulong LatestPublishedSceneRevision
    {
        get
        {
            lock (_scenePublicationGate)
            {
                return _lastPublishedScene.Revision;
            }
        }
    }

    public async Task<INativeWebSceneFrozenPresentation?> CaptureFrozenPresentationAsync(
        ulong estimatedBytes,
        CancellationToken cancellationToken)
    {
        const uint sceneCheckpoint = 1;
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            return null;
        }

        var baselineRevision = LatestPublishedSceneRevision;
        if (NativeWebSceneApi.EngineRequestSceneCheckpoint(engine) == 0)
        {
            return null;
        }
        NativeFrameInput.Submit(
            engine,
            Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);

        var deadline = Stopwatch.GetTimestamp() + 2 * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (LatestPublishedSceneRevision <= baselineRevision)
            {
                await Task.Delay(2, cancellationToken).ConfigureAwait(false);
                continue;
            }

            var scene = NativeWebSceneApi.EngineAcquireLatestScene(engine);
            if (scene == IntPtr.Zero)
            {
                await Task.Delay(2, cancellationToken).ConfigureAwait(false);
                continue;
            }
            if (!NativeFrozenSceneState.TryReadHeader(scene, out var header)
                || header.Revision <= baselineRevision
                || (header.Flags & sceneCheckpoint) == 0)
            {
                NativeWebSceneApi.SceneAcknowledge(scene);
                NativeWebSceneApi.SceneRelease(scene);
                NativeWebSceneApi.EngineRequestSceneCheckpoint(engine);
                NativeFrameInput.Submit(
                    engine,
                    Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);
                await Task.Delay(2, cancellationToken).ConfigureAwait(false);
                continue;
            }

            // The frozen control owns the acquired immutable scene reference.
            // Acknowledgement unblocks the producer before its engine is destroyed.
            NativeWebSceneApi.SceneAcknowledge(scene);
            try
            {
                return await Dispatcher.UIThread.InvokeAsync(
                        () => (INativeWebSceneFrozenPresentation)
                            new NativeFrozenSceneControl(scene, estimatedBytes),
                        DispatcherPriority.Send)
                    .GetTask()
                    .ConfigureAwait(false);
            }
            catch
            {
                NativeWebSceneApi.SceneRelease(scene);
                throw;
            }
        }
        return null;
    }

    public void OnNativeScenePublished(NativeScenePublished scene)
    {
        // This callback runs on the engine worker. Publish into a lock-free mailbox
        // that the compositor consumes at its next animation boundary. Ordinary
        // scenes must not enqueue one high-priority UI-dispatch operation per
        // publication: several active documents would otherwise contend with the
        // application's input and layout work.
        _compositionMailbox.Publish();
        lock (_scenePublicationGate)
        {
            if (scene.Revision > _lastPublishedScene.Revision)
            {
                _lastPublishedScene = scene;
            }
            if (_publishedScenes.Count == 4096)
            {
                _publishedScenes.Dequeue();
            }
            _publishedScenes.Enqueue(new NativeScenePublicationSample(
                Stopwatch.GetTimestamp(),
                scene.Revision,
                scene.ConsumedInputSequence,
                scene.ViewportWidth,
                scene.ViewportHeight));
        }

        var cursorKind = unchecked((int)NativeWebSceneApi.EngineGetCursor(_engine));
        if (Interlocked.Exchange(ref _lastCursorKind, cursorKind) != cursorKind)
        {
            Dispatcher.UIThread.Post(
                () => ApplyCursorKind(cursorKind),
                DispatcherPriority.Input);
        }

        NotifyResizePublicationIfReady(scene);
        if (Volatile.Read(ref _compositionProjectionActive) != 0)
        {
            // Projection is demand-driven. A publication must wake an idle
            // compositor, but the gate still coalesces any producer burst into
            // one UI-to-compositor message.
            ScheduleCompositionUiWake();
            return;
        }

        // The non-composition fallback still renders as an Avalonia control and
        // therefore requires UI-thread invalidation.
        Dispatcher.UIThread.Post(
            RequestPublishedScenePaint,
            DispatcherPriority.Render);
    }

    public void OnNativeAnimationFrameRequested()
    {
        if (Volatile.Read(ref _compositionProjectionActive) != 0)
        {
            ScheduleCompositionUiWake();
            return;
        }

        // The fallback has no composition handler to receive SceneWake.
        // Marshal the native worker's idle-to-active edge to Avalonia's UI
        // frame scheduler.
        Dispatcher.UIThread.Post(RequestNextFrame, DispatcherPriority.Render);
    }

    private bool NotifyResizePublicationIfReady(NativeScenePublished scene)
    {
        var resizeSequence = Interlocked.Read(ref _lastResizeSequence);
        var notifiedResizeSequence = Interlocked.Read(ref _lastNotifiedResizeSequence);
        while (resizeSequence > notifiedResizeSequence
            && scene.ConsumedInputSequence >= unchecked((ulong)resizeSequence))
        {
            var observed = Interlocked.CompareExchange(
                ref _lastNotifiedResizeSequence,
                resizeSequence,
                notifiedResizeSequence);
            if (observed == notifiedResizeSequence)
            {
                Interlocked.Increment(ref _resizePublicationNotificationCount);
                return true;
            }
            notifiedResizeSequence = observed;
        }

        return false;
    }

    private void ScheduleCompositionUiWake()
    {
        if (!_compositionUiWakeGate.TrySchedule())
        {
            return;
        }

        Interlocked.Increment(ref _compositionUiWakeCount);
        Dispatcher.UIThread.Post(
            () =>
            {
                var forwardedToCompositor = false;
                try
                {
                    if (_engine == IntPtr.Zero)
                    {
                        return;
                    }

                    var visual = _customVisual;
                    if (visual is not null)
                    {
                        visual.SendHandlerMessage(
                            NativeSceneCompositionMessage.SceneWake);
                        forwardedToCompositor = true;
                    }
                    else
                    {
                        InvalidateVisual();
                    }
                }
                finally
                {
                    // Keep the gate closed until the render-side handler receives
                    // the message. That acknowledgement prevents publications
                    // arriving between the UI commit and compositor dispatch from
                    // being mistaken for work already covered by this wake.
                    if (!forwardedToCompositor)
                    {
                        _compositionUiWakeGate.Complete();
                    }
                }
            },
            DispatcherPriority.Send);
    }

    private void ApplyCursorKind(int cursorKind)
    {
        var type = cursorKind switch
        {
            1 => StandardCursorType.Hand,
            2 => StandardCursorType.Ibeam,
            3 => StandardCursorType.Cross,
            4 => StandardCursorType.Wait,
            5 => StandardCursorType.SizeAll,
            6 => StandardCursorType.No,
            7 => StandardCursorType.Help,
            _ => StandardCursorType.Arrow
        };
        Cursor = new Cursor(type);
    }

    private void RequestPublishedScenePaint()
    {
        if (_engine == IntPtr.Zero)
        {
            return;
        }

        if (_customVisual is not null)
        {
            _customVisual.SendHandlerMessage(
                NativeSceneCompositionMessage.SceneWake);
        }
        else
        {
            InvalidateVisual();
        }
    }

    private void RequestNextFrame()
    {
        if (!_frameLoopActive
            || _engine == IntPtr.Zero
            || NativeWebSceneApi.EngineRequiresAnimationFrame(_engine) == 0)
        {
            return;
        }
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null)
        {
            Dispatcher.UIThread.Post(RequestNextFrame, DispatcherPriority.Render);
            return;
        }
        if (Interlocked.CompareExchange(ref _frameCallbackScheduled, 1, 0) != 0)
        {
            return;
        }
        topLevel.RequestAnimationFrame(timestamp =>
        {
            Interlocked.Exchange(ref _frameCallbackScheduled, 0);
            if (!_frameLoopActive) return;
            if (_submitAnimationFrames
                && NativeWebSceneApi.EngineRequiresAnimationFrame(_engine) != 0)
            {
                NativeFrameInput.Submit(_engine, timestamp.TotalMilliseconds);
            }
            RequestNextFrame();
        });
    }

    private void EnqueuePointer(uint kind, PointerEventArgs args)
    {
        var point = args.GetCurrentPoint(this);
        var properties = point.Properties;
        var button = properties.PointerUpdateKind switch
        {
            PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 0,
            PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 1,
            PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 2,
            _ => -1
        };
        var buttons = (properties.IsLeftButtonPressed ? 1U : 0U)
            | (properties.IsRightButtonPressed ? 2U : 0U)
            | (properties.IsMiddleButtonPressed ? 4U : 0U);
        var input = new InputEvent
        {
            Kind = kind,
            // Low bits use the DOM `buttons` mask. Bits 8-15 carry button+1 so
            // releases retain the changed button after Avalonia clears its mask.
            Flags = buttons
                | (button >= 0 ? (uint)(button + 1) << 8 : 0U)
                | (EncodeModifiers(args.KeyModifiers) << 16),
            Sequence = NextSequence(),
            X = point.Position.X,
            Y = point.Position.Y
        };
        _lastPointerX = input.X;
        _lastPointerY = input.Y;
        Interlocked.Increment(ref _routedInputEvents);
        if (NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0)
        {
            Interlocked.Increment(ref _acceptedInputEvents);
        }
        args.Handled = true;
    }

    private void OnPointerMoved(object? sender, PointerEventArgs args)
        => EnqueuePointer(1, args);

    private void OnPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        _pointerDown = true;
        Focus();
        args.Pointer.Capture(this);
        EnqueuePointer(2, args);
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        _pointerDown = false;
        EnqueuePointer(3, args);
        if (args.Pointer.Captured == this)
        {
            args.Pointer.Capture(null);
        }
    }

    private void OnPointerWheelChanged(object? sender, PointerWheelEventArgs args)
    {
        var point = args.GetPosition(this);
        var input = new InputEvent
        {
            Kind = 4,
            Flags = EncodeModifiers(args.KeyModifiers) << 16,
            Sequence = NextSequence(),
            X = point.X,
            Y = point.Y,
            // Avalonia reports wheel-up as positive logical notches; DOM WheelEvent
            // reports wheel-up as negative pixel deltas. A 100 px/notch mapping
            // matches Chromium's conventional mouse-wheel input while preserving
            // fractional trackpad deltas.
            DeltaX = -args.Delta.X * 100,
            DeltaY = -args.Delta.Y * 100
        };
        Interlocked.Increment(ref _routedInputEvents);
        if (NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0)
        {
            Interlocked.Increment(ref _acceptedInputEvents);
        }
        args.Handled = true;
    }

    private void OnKeyDown(object? sender, KeyEventArgs args)
        => EnqueueKey(7, args);

    private void OnKeyUp(object? sender, KeyEventArgs args)
        => EnqueueKey(8, args);

    private void EnqueueKey(uint kind, KeyEventArgs args)
    {
        var input = new InputEvent
        {
            Kind = kind,
            Flags = EncodeModifiers(args.KeyModifiers),
            Sequence = NextSequence(),
            X = DomKeyCode(args.Key)
        };
        Interlocked.Increment(ref _routedInputEvents);
        if (NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0)
        {
            Interlocked.Increment(ref _acceptedInputEvents);
            // Avalonia Native on macOS synthesizes RawTextInput only after an
            // unhandled printable KeyDown when no IME client is installed. The
            // native DOM still receives keydown here, but consuming it would
            // suppress the following Unicode text event and leave HTML inputs
            // unable to edit. Navigation, command, and KeyUp events remain
            // handled by this surface.
            args.Handled = kind != 7 || !MayProduceText(args);
        }
    }

    private static bool MayProduceText(KeyEventArgs args)
        => !args.KeyModifiers.HasFlag(KeyModifiers.Control)
            && !args.KeyModifiers.HasFlag(KeyModifiers.Meta)
            && !string.IsNullOrEmpty(args.KeySymbol)
            && args.KeySymbol.Any(static character => !char.IsControl(character));

    private void OnTextInput(object? sender, TextInputEventArgs args)
    {
        if (string.IsNullOrEmpty(args.Text)) return;
        var accepted = false;
        foreach (var rune in args.Text.EnumerateRunes())
        {
            var input = new InputEvent
            {
                Kind = 9,
                Sequence = NextSequence(),
                X = rune.Value
            };
            Interlocked.Increment(ref _routedInputEvents);
            if (NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0)
            {
                Interlocked.Increment(ref _acceptedInputEvents);
                accepted = true;
            }
        }
        args.Handled = accepted;
    }

    private static uint EncodeModifiers(KeyModifiers modifiers)
        => (modifiers.HasFlag(KeyModifiers.Shift) ? 1U : 0U)
            | (modifiers.HasFlag(KeyModifiers.Control) ? 2U : 0U)
            | (modifiers.HasFlag(KeyModifiers.Alt) ? 4U : 0U)
            | (modifiers.HasFlag(KeyModifiers.Meta) ? 8U : 0U);

    private static int DomKeyCode(Key key)
    {
        var name = key.ToString();
        if (name.Length == 1 && char.IsAsciiLetter(name[0]))
        {
            return char.ToUpperInvariant(name[0]);
        }
        if (name.Length == 2 && name[0] == 'D' && char.IsAsciiDigit(name[1]))
        {
            return name[1];
        }
        return key switch
        {
            Key.Back => 8,
            Key.Tab => 9,
            Key.Enter => 13,
            Key.Escape => 27,
            Key.Space => 32,
            Key.PageUp => 33,
            Key.PageDown => 34,
            Key.End => 35,
            Key.Home => 36,
            Key.Left => 37,
            Key.Up => 38,
            Key.Right => 39,
            Key.Down => 40,
            Key.Delete => 46,
            _ => 0
        };
    }

    private void OnPointerCaptureLost(object? sender, PointerCaptureLostEventArgs args)
        => SubmitPointerCaptureInterruption();

    public ulong SubmitPointerCaptureInterruption()
    {
        // A native capture must never survive an Avalonia capture loss (for
        // example when macOS takes over a live window resize). A normal release
        // clears _pointerDown before Avalonia releases capture, so this only
        // synthesizes the missing terminal event for an interrupted gesture.
        if (!_pointerDown)
        {
            return 0;
        }

        _pointerDown = false;
        var input = new InputEvent
        {
            Kind = 3,
            Sequence = NextSequence(),
            X = _lastPointerX,
            Y = _lastPointerY
        };
        Interlocked.Increment(ref _routedInputEvents);
        if (NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0)
        {
            Interlocked.Increment(ref _acceptedInputEvents);
            return input.Sequence;
        }
        return 0;
    }

    private void OnSurfaceSizeChanged(object? sender, SizeChangedEventArgs args)
    {
        var started = Stopwatch.GetTimestamp();
        try
        {
            var resizeSequence = SubmitResize(
                args.NewSize.Width,
                args.NewSize.Height,
                includeAnimationFrame: _submitAnimationFrames);
            if (resizeSequence == 0)
            {
                return;
            }
            Interlocked.Exchange(
                ref _liveResizeDeadlineTimestamp,
                Stopwatch.GetTimestamp()
                    + (long)(0.25 * Stopwatch.Frequency));

            if (_customVisual is not null)
            {
                _customVisual.Size = new Vector2(
                    (float)args.NewSize.Width,
                    (float)args.NewSize.Height);
                _customVisual.SendHandlerMessage(NativeSceneCompositionMessage.LiveResize);
                // Never wait for the worker from SizeChanged. On macOS this handler
                // runs inside the native live-resize loop; blocking it also blocks
                // composition acknowledgement, which prevents the worker from
                // publishing the next viewport and collapses the drag to a
                // mouse-up-only repaint. The publication callback above schedules
                // the matching cooperative paint without holding the UI thread.
            }
            else
            {
                InvalidateVisual();
            }
        }
        finally
        {
            Interlocked.Exchange(
                ref _lastSurfaceResizeHandlerTicks,
                Stopwatch.GetTimestamp() - started);
        }
    }

    public ulong SubmitResize(
        double width,
        double height,
        double? requestedDeviceScaleFactor = null,
        bool includeAnimationFrame = false)
    {
        if (_engine == IntPtr.Zero || width <= 1 || height <= 1) return 0;
        var deviceScaleFactor = requestedDeviceScaleFactor
            ?? TopLevel.GetTopLevel(this)?.RenderScaling
            ?? 1;
        var sequence = NextSequence();
        var input = new InputEvent
        {
            Kind = 6,
            Sequence = sequence,
            X = width,
            Y = height,
            DeltaX = deviceScaleFactor > 0 && double.IsFinite(deviceScaleFactor)
                ? deviceScaleFactor
                : 1
        };
        // Timestamp the host boundary before entering native code. The worker can
        // consume and publish a lightweight resize before the enqueue call returns.
        var submittedAt = Stopwatch.GetTimestamp();
        byte accepted;
        if (includeAnimationFrame)
        {
            var frame = new InputEvent
            {
                Kind = 5,
                X = Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency
            };
            accepted = NativeWebSceneApi.EngineEnqueueResizeFrame(
                _engine,
                in input,
                in frame);
        }
        else
        {
            accepted = NativeWebSceneApi.EngineEnqueue(_engine, in input);
        }
        if (accepted == 0) return 0;
        Interlocked.Exchange(ref _lastResizeTimestamp, submittedAt);
        Interlocked.Exchange(ref _lastResizeSequence, unchecked((long)sequence));
        lock (_scenePublicationGate)
        {
            if (_submittedResizes.Count == 4096)
            {
                _submittedResizes.Dequeue();
            }
            _submittedResizes.Enqueue(new NativeResizeSubmissionSample(
                submittedAt,
                sequence,
                width,
                height));
        }
        NativeSceneDrawOperation.RecordResizeSubmitted(sequence, submittedAt);

        // A very fast worker can publish the resize-matching scene between
        // EngineEnqueue returning and the host recording _lastResizeSequence.
        // Recheck the latest publication after making the sequence visible so
        // that race still schedules the cooperative paint exactly once.
        NativeScenePublished latest;
        lock (_scenePublicationGate)
        {
            latest = _lastPublishedScene;
        }
        _ = NotifyResizePublicationIfReady(latest);
        return sequence;
    }

    public ulong SubmitWheel(double x, double y, double deltaY, uint modifiers = 0)
    {
        var sequence = NextSequence();
        var input = new InputEvent
        {
            Kind = 4,
            Flags = 0x80000000 | ((modifiers & 0xFU) << 16),
            Sequence = sequence,
            X = x,
            Y = y,
            DeltaY = deltaY
        };
        return NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0 ? sequence : 0;
    }

    public ulong SubmitPointerMove(double x, double y, bool pressed = false)
    {
        var sequence = NextSequence();
        var input = new InputEvent
        {
            Kind = 1,
            Flags = pressed ? 1U : 0U,
            Sequence = sequence,
            X = x,
            Y = y
        };
        return NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0 ? sequence : 0;
    }

    public void SubmitAvaloniaPointerMove(double x, double y)
    {
        var root = TopLevel.GetTopLevel(this)
            ?? throw new InvalidOperationException(
                "The native scene surface must be attached before routing pointer input.");
        var rootPoint = this.TranslatePoint(new Point(x, y), root)
            ?? throw new InvalidOperationException(
                "The native scene surface pointer could not be translated to its top level.");
        using var pointer = new global::Avalonia.Input.Pointer(
            global::Avalonia.Input.Pointer.GetNextFreeId(),
            PointerType.Mouse,
            true);
        RaiseEvent(new PointerEventArgs(
            InputElement.PointerMovedEvent,
            this,
            pointer,
            root,
            rootPoint,
            0,
            new PointerPointProperties(
                RawInputModifiers.None,
                PointerUpdateKind.Other),
            KeyModifiers.None));
    }

    public ulong SubmitPointerButton(
        uint kind,
        double x,
        double y,
        int button,
        bool pressed,
        uint modifiers = 0)
    {
        _pointerDown = pressed;
        _lastPointerX = x;
        _lastPointerY = y;
        var buttonMask = button switch
        {
            0 => 1U,
            1 => 4U,
            2 => 2U,
            _ => 0U
        };
        var sequence = NextSequence();
        var input = new InputEvent
        {
            Kind = kind,
            Flags = (pressed ? buttonMask : 0U)
                | ((uint)(button + 1) << 8)
                | ((modifiers & 0xFU) << 16),
            Sequence = sequence,
            X = x,
            Y = y
        };
        return NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0 ? sequence : 0;
    }

    public ulong SubmitKey(uint kind, int keyCode, uint modifiers = 0)
    {
        var sequence = NextSequence();
        var input = new InputEvent
        {
            Kind = kind,
            Flags = modifiers,
            Sequence = sequence,
            X = keyCode
        };
        return NativeWebSceneApi.EngineEnqueue(_engine, in input) != 0 ? sequence : 0;
    }

    public ulong SubmitText(string text)
    {
        ulong sequence = 0;
        foreach (var rune in text.EnumerateRunes())
        {
            var symbol = rune.ToString();
            var keyDown = new KeyEventArgs
            {
                RoutedEvent = InputElement.KeyDownEvent,
                Source = this,
                Key = KeyForTextRune(rune),
                KeySymbol = symbol
            };
            RaiseEvent(keyDown);
            if (keyDown.Handled) return 0;

            var textInput = new TextInputEventArgs
            {
                RoutedEvent = InputElement.TextInputEvent,
                Source = this,
                Text = symbol
            };
            RaiseEvent(textInput);
            if (!textInput.Handled) return 0;

            RaiseEvent(new KeyEventArgs
            {
                RoutedEvent = InputElement.KeyUpEvent,
                Source = this,
                Key = KeyForTextRune(rune),
                KeySymbol = symbol
            });
            sequence = unchecked((ulong)Interlocked.Read(ref _sequence));
        }
        return sequence;
    }

    private static Key KeyForTextRune(Rune rune)
    {
        var scalar = rune.Value;
        if (scalar is >= 'a' and <= 'z') scalar -= 'a' - 'A';
        if (scalar is >= 'A' and <= 'Z'
            && Enum.TryParse<Key>(((char)scalar).ToString(), out var letter))
        {
            return letter;
        }
        if (scalar is >= '0' and <= '9'
            && Enum.TryParse<Key>($"D{(char)scalar}", out var digit))
        {
            return digit;
        }
        return scalar == ' ' ? Key.Space : Key.None;
    }

    public void RequestRender() => InvalidateVisual();

    public unsafe byte[] CaptureRetainedScenePng()
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            return Dispatcher.UIThread.Invoke(
                CaptureRetainedScenePng,
                DispatcherPriority.Send);
        }

        RefreshRetainedSceneForCapture();
        var width = Math.Max(1, (int)Math.Ceiling(Bounds.Width));
        var height = Math.Max(1, (int)Math.Ceiling(Bounds.Height));
        using var bitmap = new SKBitmap(
            width,
            height,
            SKColorType.Bgra8888,
            SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(new SKColor(19, 23, 34, 255));
        lock (_rendererGate)
        {
            _renderer.RenderRetained(
                canvas,
                (float)Math.Max(1, Bounds.Width),
                (float)Math.Max(1, Bounds.Height),
                null);
        }
        canvas.Flush();
        using var image = SKImage.FromBitmap(bitmap);
        using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
        return encoded.ToArray();
    }

    private unsafe void RefreshRetainedSceneForCapture()
    {
        var engine = Volatile.Read(ref _engine);
        if (engine == IntPtr.Zero)
        {
            return;
        }

        bool ApplyAvailableScenes()
        {
            var applied = false;
            while (true)
            {
                var scene = NativeWebSceneApi.EngineAcquireNextScene(engine);
                if (scene == IntPtr.Zero)
                {
                    return applied;
                }
                try
                {
                    var view = (NativeSceneView*)scene;
                    if (view != null
                        && view->StructSize == sizeof(NativeSceneView)
                        && view->AbiVersion == 2)
                    {
                        lock (_rendererGate)
                        {
                            _renderer.ApplyDiff(view);
                        }
                        applied = true;
                    }
                    NativeWebSceneApi.SceneAcknowledge(scene);
                }
                finally
                {
                    NativeWebSceneApi.SceneRelease(scene);
                }
            }
        }

        ApplyAvailableScenes();
        if (NativeWebSceneApi.EngineRequestSceneCheckpoint(engine) == 0)
        {
            return;
        }
        NativeFrameInput.Submit(
            engine,
            Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);

        var deadline = Stopwatch.GetTimestamp() + 2 * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < deadline)
        {
            if (ApplyAvailableScenes())
            {
                return;
            }
            Thread.Sleep(2);
        }
    }

    public void SetCompositionAnimationFramesPaused(bool paused)
    {
        _customVisual?.SendHandlerMessage(
            paused
                ? NativeSceneCompositionMessage.PauseAnimationFrames
                : NativeSceneCompositionMessage.ResumeAnimationFrames);
    }

    public void SetManualCompositionFrames(bool enabled)
    {
        _customVisual?.SendHandlerMessage(
            enabled
                ? NativeSceneCompositionMessage.BeginManualFrames
                : NativeSceneCompositionMessage.EndManualFrames);
    }

    public void RequestManualCompositionFrame()
        => _customVisual?.SendHandlerMessage(
            NativeSceneCompositionMessage.ManualFrame);

    public static long SynchronousCompositionSceneCount
        => Volatile.Read(ref NativeSceneCompositionHandler.SynchronousRenderAcquisitionCount);

    public static NativeCompositionFlowMetrics CompositionFlowMetrics
        => new(
            AnimationFrames: Volatile.Read(
                ref NativeSceneCompositionHandler.AnimationFrameCount),
            Renders: Volatile.Read(ref NativeSceneDrawOperation.RenderCount),
            AppliedDiffs: Volatile.Read(
                ref NativeSceneCompositionHandler.AppliedDiffCount),
            InvalidationCalls: Volatile.Read(
                ref NativeSceneCompositionHandler.InvalidationCallCount),
            DamageRectangles: Volatile.Read(
                ref NativeSceneCompositionHandler.DamageRectangleCount),
            FullInvalidations: Volatile.Read(
                ref NativeSceneCompositionHandler.FullInvalidationCount),
            SuppressedLiveResizeAnimationFrames: Volatile.Read(
                ref NativeSceneCompositionHandler
                    .SuppressedLiveResizeAnimationFrameCount),
            SubmittedAnimationFrames: Volatile.Read(
                ref NativeSceneCompositionHandler.SubmittedAnimationFrameCount),
            SkippedEmptyAnimationFrames: Volatile.Read(
                ref NativeSceneCompositionHandler.SkippedEmptyAnimationFrameCount),
            RenderCallbacks: Volatile.Read(
                ref NativeSceneCompositionHandler.RenderCallbackCount),
            UnchangedRenderCallbacks: Volatile.Read(
                ref NativeSceneCompositionHandler.UnchangedRenderCallbackCount),
            LastAnimationFrameDemand: Volatile.Read(
                ref NativeSceneCompositionHandler.LastAnimationFrameDemand));

    public static (
        double DiffApplyMilliseconds,
        double RetainedDrawMilliseconds,
        double SkiaSubmitMilliseconds,
        double RenderCallbackMilliseconds) LastCompositionTiming
        => (
            Volatile.Read(ref NativeSceneDrawOperation.LastDiffApplyTicks)
                * 1_000d / Stopwatch.Frequency,
            Volatile.Read(ref NativeSceneDrawOperation.LastRetainedDrawTicks)
                * 1_000d / Stopwatch.Frequency,
            Volatile.Read(ref NativeSceneDrawOperation.LastSkiaSubmitTicks)
                * 1_000d / Stopwatch.Frequency,
            Volatile.Read(ref NativeSceneDrawOperation.LastRenderCallbackTicks)
                * 1_000d / Stopwatch.Frequency);

    public void SubmitAnimationFrame(double timestampMilliseconds)
        => NativeFrameInput.Submit(_engine, timestampMilliseconds);

    public override unsafe void Render(DrawingContext context)
    {
        base.Render(context);
        // The composition child is not part of Avalonia's input hit-test tree.
        // Like an HTML canvas, the surface must remain an atomic hit-test box
        // even when its Avalonia drawing is transparent.
        context.DrawRectangle(Brushes.Transparent, null, new Rect(Bounds.Size));
        if (_customVisual is not null)
        {
            return;
        }
        if (_engine == IntPtr.Zero)
        {
            return;
        }
        var scene = NativeWebSceneApi.EngineAcquireNextScene(_engine);
        if (scene != IntPtr.Zero)
        {
            // Keep the shared mailbox balanced even when this surface had to use
            // Avalonia's non-composition fallback. A later compositor attachment
            // must not inherit stale publication counts from already acquired
            // scenes.
            _compositionMailbox.TryConsume();
            context.Custom(new NativeSceneDrawOperation(
                scene,
                new Rect(Bounds.Size),
                _renderer,
                _rendererGate,
                _renderObserver));
        }
    }
}

public readonly record struct NativeCompositionFlowMetrics(
    long AnimationFrames,
    long Renders,
    long AppliedDiffs,
    long InvalidationCalls,
    long DamageRectangles,
    long FullInvalidations,
    long SuppressedLiveResizeAnimationFrames,
    long SubmittedAnimationFrames,
    long SkippedEmptyAnimationFrames,
    long RenderCallbacks,
    long UnchangedRenderCallbacks,
    int LastAnimationFrameDemand);

internal sealed class LivePerformanceHud : Border
{
    private readonly IntPtr _engine;
    private readonly NativeSceneSurface _surface;
    private readonly TextBlock _text;
    private readonly DispatcherTimer _timer;
    private long _sampleTimestamp;
    private long _animationFrames;
    private int _sceneFrames;
    private long _routedInputs;
    private long _acceptedInputs;
    private ulong _publishedScenes;
    private ulong _callbacks;

    public LivePerformanceHud(IntPtr engine, NativeSceneSurface surface)
    {
        _engine = engine;
        _surface = surface;
        IsHitTestVisible = false;
        HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Left;
        VerticalAlignment = global::Avalonia.Layout.VerticalAlignment.Top;
        Margin = new Thickness(8);
        Padding = new Thickness(8, 5);
        CornerRadius = new CornerRadius(4);
        Background = new SolidColorBrush(Color.FromArgb(224, 8, 12, 20));
        BorderBrush = new SolidColorBrush(Color.FromArgb(180, 80, 88, 108));
        BorderThickness = new Thickness(1);
        SetValue(Canvas.ZIndexProperty, 10_000);

        _text = new TextBlock
        {
            Foreground = Brushes.White,
            FontFamily = new FontFamily("Menlo, Consolas, monospace"),
            FontSize = 11
        };
        Child = _text;

        _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(250), DispatcherPriority.Background, OnTick);
        AttachedToVisualTree += (_, _) => Start();
        DetachedFromVisualTree += (_, _) => _timer.Stop();
    }

    private void Start()
    {
        NativeWebSceneApi.EngineGetMetrics(_engine, out var metrics);
        _sampleTimestamp = Stopwatch.GetTimestamp();
        _animationFrames = Volatile.Read(ref NativeSceneCompositionHandler.AnimationFrameCount);
        _sceneFrames = Volatile.Read(ref NativeSceneDrawOperation.RenderCount);
        _routedInputs = _surface.RoutedInputEvents;
        _acceptedInputs = _surface.AcceptedInputEvents;
        _publishedScenes = metrics.PublishedScenes;
        _callbacks = metrics.InputCallbacksInvoked;
        _timer.Start();
        OnTick(null, EventArgs.Empty);
    }

    private void OnTick(object? sender, EventArgs args)
    {
        NativeWebSceneApi.EngineGetMetrics(_engine, out var metrics);
        var now = Stopwatch.GetTimestamp();
        var elapsed = Math.Max((now - _sampleTimestamp) / (double)Stopwatch.Frequency, 0.001);
        var animationFrames = Volatile.Read(ref NativeSceneCompositionHandler.AnimationFrameCount);
        var sceneFrames = Volatile.Read(ref NativeSceneDrawOperation.RenderCount);
        var routedInputs = _surface.RoutedInputEvents;
        var acceptedInputs = _surface.AcceptedInputEvents;
        var pending = metrics.EnqueuedInputs > metrics.ConsumedInputs + metrics.DroppedInputs
            ? metrics.EnqueuedInputs - metrics.ConsumedInputs - metrics.DroppedInputs
            : 0;
        var compositorFps = (animationFrames - _animationFrames) / elapsed;
        var sceneFps = (sceneFrames - _sceneFrames) / elapsed;
        var publishFps = (metrics.PublishedScenes - _publishedScenes) / elapsed;
        var routedPerSecond = (routedInputs - _routedInputs) / elapsed;
        var callbacksPerSecond = (metrics.InputCallbacksInvoked - _callbacks) / elapsed;
        var resizeMilliseconds = Volatile.Read(ref NativeSceneDrawOperation.LastResizeToRenderTicks)
            * 1_000d / Stopwatch.Frequency;
        var renderMilliseconds = Volatile.Read(ref NativeSceneDrawOperation.LastRenderCallbackTicks)
            * 1_000d / Stopwatch.Frequency;
        var diffCommands = Volatile.Read(ref NativeSceneDrawOperation.LastDiffCanvasCommandCount);
        var retainedCommands = Volatile.Read(ref NativeSceneDrawOperation.LastRetainedCanvasCommandCount);
        var rejectedDiffs = Volatile.Read(ref NativeCanvasSceneRenderer.RejectedDiffCount);
        using var process = Process.GetCurrentProcess();
        var workingSetMegabytes = process.WorkingSet64 / (1024d * 1024d);
        var privateMegabytes = process.PrivateMemorySize64 / (1024d * 1024d);

        _text.Text =
            $"compositor {compositorFps,5:F1} fps   scene {sceneFps,5:F1} fps   native publish {publishFps,5:F1} fps\n" +
            $"input {routedPerSecond,6:F1}/s ({acceptedInputs:N0}/{routedInputs:N0} accepted)   " +
            $"JS callbacks {callbacksPerSecond,6:F1}/s   queue {pending:N0}\n" +
            $"coalesced move {metrics.CoalescedPointerMoveInputs:N0} / wheel {metrics.CoalescedWheelInputs:N0}   " +
            $"applied move {metrics.AppliedPointerMoveInputs:N0} / wheel {metrics.AppliedWheelInputs:N0}\n" +
            $"resize→render {resizeMilliseconds,6:F1} ms   render {renderMilliseconds,5:F1} ms   " +
            $"dropped {metrics.DroppedInputs:N0}\n" +
            $"canvas commands diff {diffCommands:N0} / retained {retainedCommands:N0}   " +
            $"stale diffs {rejectedDiffs:N0}   " +
            $"memory working {workingSetMegabytes:F0} MiB / private {privateMegabytes:F0} MiB";

        _sampleTimestamp = now;
        _animationFrames = animationFrames;
        _sceneFrames = sceneFrames;
        _routedInputs = routedInputs;
        _acceptedInputs = acceptedInputs;
        _publishedScenes = metrics.PublishedScenes;
        _callbacks = metrics.InputCallbacksInvoked;
    }
}
#endif

internal enum NativeSceneCompositionMessage
{
    Start,
    SceneWake,
    LiveResize,
    PauseAnimationFrames,
    ResumeAnimationFrames,
    BeginManualFrames,
    EndManualFrames,
    ManualFrame,
    Stop
}

internal sealed class NativeScenePublicationMailbox
{
    private long _published;
    private long _consumed;

    public long PendingCount
    {
        get
        {
            var pending = Volatile.Read(ref _published) - Volatile.Read(ref _consumed);
            return Math.Max(0, pending);
        }
    }

    public void Publish()
        => Interlocked.Increment(ref _published);

    public bool TryConsume()
    {
        while (true)
        {
            var consumed = Volatile.Read(ref _consumed);
            var published = Volatile.Read(ref _published);
            if (consumed >= published)
            {
                return false;
            }
            if (Interlocked.CompareExchange(
                    ref _consumed,
                    consumed + 1,
                    consumed) == consumed)
            {
                return true;
            }
        }
    }

    public void Reset()
        => Interlocked.Exchange(ref _consumed, Volatile.Read(ref _published));
}

internal sealed class NativeSceneUiWakeGate
{
    private int _pending;

    public bool TrySchedule()
        => Interlocked.CompareExchange(ref _pending, 1, 0) == 0;

    public void Complete()
        => Volatile.Write(ref _pending, 0);

    public void Reset()
        => Volatile.Write(ref _pending, 0);
}

internal static class NativeScenePublicationWakePolicy
{
    public static bool RequiresUiWake(
        bool matchingResizePublication,
        long renderedSceneCount)
        => matchingResizePublication || renderedSceneCount == 0;
}

public readonly record struct NativeScenePublished(
    ulong Revision,
    ulong ConsumedInputSequence,
    float ViewportWidth,
    float ViewportHeight);

public readonly record struct NativeScenePublicationSample(
    long Timestamp,
    ulong Revision,
    ulong ConsumedInputSequence,
    float ViewportWidth,
    float ViewportHeight);

public readonly record struct NativeResizeSubmissionSample(
    long Timestamp,
    ulong Sequence,
    double ViewportWidth,
    double ViewportHeight);

public readonly record struct NativeSceneRenderSample(
    long Timestamp,
    ulong Revision,
    ulong ConsumedInputSequence);

internal static class NativeSceneResizeProjection
{
    private const float SettledViewportTolerance = 0.5f;

    public static Vector2 GetScale(
        double targetWidth,
        double targetHeight,
        double viewportWidth,
        double viewportHeight)
    {
        if (targetWidth <= 0
            || targetHeight <= 0
            || viewportWidth <= 0
            || viewportHeight <= 0)
        {
            return Vector2.One;
        }

        // During a live resize, Avalonia arranges the composition visual before
        // the native worker can publish a matching DOM scene. Stretching that old
        // scene independently on each axis creates the visible jelly/squish. Keep
        // it pixel-stable and clipped until the resize-matching scene arrives.
        if (Math.Abs(targetWidth - viewportWidth) > SettledViewportTolerance
            || Math.Abs(targetHeight - viewportHeight) > SettledViewportTolerance)
        {
            return Vector2.One;
        }

        return new Vector2(
            (float)(targetWidth / viewportWidth),
            (float)(targetHeight / viewportHeight));
    }
}

#if !WEBSCENE_UNO
internal readonly record struct NativeSceneDamage(
    bool RequiresRender,
    bool IsFull,
    Rect Bounds,
    int RectangleCount,
    double SummedArea)
{
    public static NativeSceneDamage None => default;
}

internal static class NativeSceneDamagePolicy
{
    private const uint SceneCheckpoint = 1;
    private const uint SceneDomReplacement = 2;

    public static NativeSceneDamage Evaluate(
        in SceneHeader header,
        ReadOnlySpan<NativeDamageRect> damageRects,
        bool damageBufferValid,
        bool viewportChanged,
        Size effectiveSize)
    {
        var fullWidth = Math.Max(0, effectiveSize.Width);
        var fullHeight = Math.Max(0, effectiveSize.Height);
        var fullBounds = new Rect(0, 0, fullWidth, fullHeight);
        var fullArea = fullWidth * fullHeight;
        var declaredDamageCount = header.DamageRectCount <= int.MaxValue
            ? (int)header.DamageRectCount
            : -1;
        var hasUnspecifiedVisualChange =
            header.CanvasLayerCount != 0
            || (header.Flags & SceneDomReplacement) != 0;

        if ((header.Flags & SceneCheckpoint) != 0
            || viewportChanged
            || header.ViewportWidth <= 0
            || header.ViewportHeight <= 0
            || !damageBufferValid
            || declaredDamageCount < 0
            || damageRects.Length != declaredDamageCount
            || (declaredDamageCount == 0 && hasUnspecifiedVisualChange))
        {
            return new NativeSceneDamage(
                RequiresRender: fullArea > 0,
                IsFull: true,
                fullBounds,
                RectangleCount: fullArea > 0 ? 1 : 0,
                SummedArea: fullArea);
        }

        // An empty incremental diff is a producer/consumer synchronization
        // point, not visual damage.
        if (damageRects.IsEmpty)
        {
            return NativeSceneDamage.None;
        }

        var scale = NativeSceneResizeProjection.GetScale(
            fullWidth,
            fullHeight,
            header.ViewportWidth,
            header.ViewportHeight);
        var hasDamage = false;
        var damageLeft = double.PositiveInfinity;
        var damageTop = double.PositiveInfinity;
        var damageRight = double.NegativeInfinity;
        var damageBottom = double.NegativeInfinity;
        var rectangleCount = 0;
        double summedArea = 0;
        foreach (ref readonly var item in damageRects)
        {
            if (!float.IsFinite(item.X)
                || !float.IsFinite(item.Y)
                || !float.IsFinite(item.Width)
                || !float.IsFinite(item.Height))
            {
                return new NativeSceneDamage(
                    RequiresRender: fullArea > 0,
                    IsFull: true,
                    fullBounds,
                    RectangleCount: fullArea > 0 ? 1 : 0,
                    SummedArea: fullArea);
            }

            var left = Math.Max(0, item.X * scale.X);
            var top = Math.Max(0, item.Y * scale.Y);
            var right = Math.Min(fullWidth, (item.X + item.Width) * scale.X);
            var bottom = Math.Min(fullHeight, (item.Y + item.Height) * scale.Y);
            if (right <= left || bottom <= top)
            {
                continue;
            }

            rectangleCount++;
            summedArea += (right - left) * (bottom - top);
            damageLeft = Math.Min(damageLeft, left);
            damageTop = Math.Min(damageTop, top);
            damageRight = Math.Max(damageRight, right);
            damageBottom = Math.Max(damageBottom, bottom);
            hasDamage = true;
        }

        if (!hasDamage)
        {
            return new NativeSceneDamage(
                RequiresRender: fullArea > 0,
                IsFull: true,
                fullBounds,
                RectangleCount: fullArea > 0 ? 1 : 0,
                SummedArea: fullArea);
        }

        return new NativeSceneDamage(
            RequiresRender: true,
            IsFull: false,
            new Rect(
                damageLeft,
                damageTop,
                damageRight - damageLeft,
                damageBottom - damageTop),
            rectangleCount,
            summedArea);
    }
}

internal sealed unsafe class NativeSceneCompositionHandler
    : CompositionCustomVisualHandler
{
    private readonly IntPtr _engine;
    private readonly NativeCanvasSceneRenderer _renderer = new();
    private readonly NativeSceneRenderObserver _renderObserver;
    private readonly NativeScenePublicationMailbox _publicationMailbox;
    private readonly NativeSceneUiWakeGate _uiWakeGate;
    private readonly Action _scheduleUiWake;
    private ulong _appliedRevision;
    private float _viewportWidth;
    private float _viewportHeight;
    private bool _running;
    private bool _manualFrames;
    private bool _animationFrameScheduled;
    private int _renderRequested;
    private long _liveResizeFrameDeadlineTimestamp;
    private bool _hasPendingRenderMetrics;
    private NativeSceneDamage _pendingDamage;
    private SceneHeader _pendingRenderHeader;
    private long _pendingDiffApplyTicks;
    private long _pendingDiffCanvasCommandCount;
    private long _appliedDiffs;
    private long _changedLayers;
    private long _damageRectangles;
    private double _damageArea;
    private double _viewportArea;
    public static long AnimationFrameCount;
    public static long SynchronousRenderAcquisitionCount;
    public static long AppliedDiffCount;
    public static long InvalidationCallCount;
    public static long DamageRectangleCount;
    public static long FullInvalidationCount;
    public static long SuppressedLiveResizeAnimationFrameCount;
    public static long SubmittedAnimationFrameCount;
    public static long SkippedEmptyAnimationFrameCount;
    public static long RenderCallbackCount;
    public static long UnchangedRenderCallbackCount;
    public static int LastAnimationFrameDemand;

    public NativeSceneCompositionHandler(
        IntPtr engine,
        NativeSceneRenderObserver renderObserver,
        NativeScenePublicationMailbox publicationMailbox,
        NativeSceneUiWakeGate uiWakeGate,
        Action scheduleUiWake)
    {
        _engine = engine;
        _renderObserver = renderObserver;
        _publicationMailbox = publicationMailbox;
        _uiWakeGate = uiWakeGate;
        _scheduleUiWake = scheduleUiWake;
    }

    public override void OnMessage(object message)
    {
        if (message is not NativeSceneCompositionMessage command)
        {
            return;
        }

        if (command == NativeSceneCompositionMessage.Start)
        {
            if (!_running)
            {
                _running = true;
            }
            if (!_manualFrames && HasPendingPresentation)
            {
                RequestRenderIfNeeded();
            }
            RequestAnimationFrameIfNeeded();
            return;
        }

        if (command == NativeSceneCompositionMessage.SceneWake)
        {
            _uiWakeGate.Complete();
            if (!_manualFrames && HasPendingPresentation)
            {
                RequestRenderIfNeeded();
            }
            RequestAnimationFrameIfNeeded();
            return;
        }

        if (command == NativeSceneCompositionMessage.LiveResize)
        {
            // This message exists specifically for the OS's nested live-resize
            // loop, where ordinary animation callbacks may be paused and
            // _running is therefore false. Acquire the cooperatively published
            // scene here and invalidate its damage so the nested composition
            // render can paint it immediately.
            // SizeChanged has already paired the accepted viewport with a host
            // RAF. Suppress the composition clock's duplicate RAF briefly so
            // the producer does not build two scenes for one display boundary.
            Interlocked.Exchange(
                ref _liveResizeFrameDeadlineTimestamp,
                Stopwatch.GetTimestamp()
                    + (long)(0.05 * Stopwatch.Frequency));
            if (!_manualFrames)
            {
                RequestRenderIfNeeded();
            }
            return;
        }

        if (command == NativeSceneCompositionMessage.PauseAnimationFrames)
        {
            _running = false;
            return;
        }

        if (command == NativeSceneCompositionMessage.ResumeAnimationFrames)
        {
            if (!_running)
            {
                _running = true;
            }
            RequestAnimationFrameIfNeeded();
            return;
        }

        if (command == NativeSceneCompositionMessage.BeginManualFrames)
        {
            _manualFrames = true;
            _running = false;
            return;
        }

        if (command == NativeSceneCompositionMessage.EndManualFrames)
        {
            _manualFrames = false;
            if (!_running)
            {
                _running = true;
            }
            RequestAnimationFrameIfNeeded();
            return;
        }

        if (command == NativeSceneCompositionMessage.ManualFrame)
        {
            RequestRenderIfNeeded();
            return;
        }

        _running = false;
        _manualFrames = false;
        _animationFrameScheduled = false;
        _uiWakeGate.Complete();
        _renderer.Reset();
        _appliedRevision = 0;
        _viewportWidth = 0;
        _viewportHeight = 0;
        Interlocked.Exchange(ref _renderRequested, 0);
        Interlocked.Exchange(ref _liveResizeFrameDeadlineTimestamp, 0);
        _hasPendingRenderMetrics = false;
        _pendingDamage = NativeSceneDamage.None;
    }

    public override void OnAnimationFrameUpdate()
    {
        _animationFrameScheduled = false;
        if (!_running)
        {
            return;
        }

        Interlocked.Increment(ref AnimationFrameCount);
        var frameTimestamp = Stopwatch.GetTimestamp();
        if (frameTimestamp
            > Interlocked.Read(ref _liveResizeFrameDeadlineTimestamp))
        {
            var demand = NativeWebSceneApi.EngineRequiresAnimationFrame(_engine);
            Volatile.Write(ref LastAnimationFrameDemand, demand);
            if (demand != 0)
            {
                Interlocked.Increment(ref SubmittedAnimationFrameCount);
                NativeFrameInput.Submit(
                    _engine,
                    frameTimestamp * 1000.0 / Stopwatch.Frequency);
            }
            else
            {
                Interlocked.Increment(ref SkippedEmptyAnimationFrameCount);
            }
        }
        else
        {
            Interlocked.Increment(
                ref SuppressedLiveResizeAnimationFrameCount);
        }
        if (HasPendingPresentation)
        {
            RequestRenderIfNeeded();
        }
        RequestAnimationFrameIfNeeded();
    }

    private void RequestAnimationFrameIfNeeded()
    {
        if (!_running
            || _manualFrames
            || _animationFrameScheduled
            || NativeWebSceneApi.EngineRequiresAnimationFrame(_engine) == 0)
        {
            return;
        }

        _animationFrameScheduled = true;
        RegisterForNextAnimationFrameUpdate();
    }

    private void RequestRenderIfNeeded()
    {
        if (Interlocked.CompareExchange(ref _renderRequested, 1, 0) != 0)
        {
            return;
        }

        var damage = _pendingDamage;
        if (!_hasPendingRenderMetrics
            && (!TryAcquireNextDiff(out damage) || !damage.RequiresRender))
        {
            Interlocked.Exchange(ref _renderRequested, 0);
            return;
        }

        Interlocked.Increment(ref InvalidationCallCount);
        // CompositionCustomVisualHandler invalidation bounds are useful for
        // scheduling, but the macOS compositor does not guarantee that the
        // backing surface outside a partial invalidation remains available to
        // a custom Skia draw. Repaint the complete retained scene for each
        // changed WebScene frame.
        Invalidate();
    }

    private bool TryAcquireNextDiff(out NativeSceneDamage damage)
    {
        damage = NativeSceneDamage.None;
        var scene = NativeWebSceneApi.EngineAcquireNextScene(_engine);
        if (scene == IntPtr.Zero)
        {
            return false;
        }
        var accepted = false;
        try
        {
            var view = (NativeSceneView*)scene;
            _publicationMailbox.TryConsume();
            if (!ValidateView(view)
                || view->Header.Revision <= _appliedRevision)
            {
                return false;
            }

            // Apply the immutable diff before invalidating so Avalonia receives
            // the native scene's precise damage bounds. Acquiring from OnRender
            // would make the current render clip too broad and require a second
            // empty invalidation to present the retained scene. The counted
            // publication signal is consumed with this scene; an additional
            // publication is drained on the next host frame.
            var header = view->Header;
            var applyStarted = Stopwatch.GetTimestamp();
            var applied = _renderer.ApplyDiff(view);
            var diffApplyTicks = Stopwatch.GetTimestamp() - applyStarted;
            if (applied)
            {
                var viewportChanged =
                    Math.Abs(_viewportWidth - header.ViewportWidth) > 0.01f
                    || Math.Abs(_viewportHeight - header.ViewportHeight) > 0.01f;
                _viewportWidth = header.ViewportWidth;
                _viewportHeight = header.ViewportHeight;
                damage = EvaluateDamage(view, viewportChanged);
                NativeWebSceneApi.SceneAcknowledge(scene);
                _appliedRevision = header.Revision;
                _appliedDiffs++;
                Interlocked.Increment(ref AppliedDiffCount);
                if (damage.RequiresRender)
                {
                    _pendingDamage = damage;
                    _pendingRenderHeader = header;
                    _pendingDiffApplyTicks += diffApplyTicks;
                    _pendingDiffCanvasCommandCount += view->CanvasCommandCount;
                    _hasPendingRenderMetrics = true;
                }
                accepted = true;
            }
        }
        finally
        {
            NativeWebSceneApi.SceneRelease(scene);
        }
        return accepted;
    }

    private NativeSceneDamage EvaluateDamage(
        NativeSceneView* view,
        bool viewportChanged)
    {
        var header = view->Header;
        var effective = EffectiveSize;
        var fullArea =
            (double)Math.Max(0, effective.X) * Math.Max(0, effective.Y);
        _viewportArea += fullArea;
        _changedLayers += header.CanvasLayerCount;

        var damageBufferValid =
            header.DamageRectCount <= int.MaxValue
            && (header.DamageRectCount == 0 || view->DamageRects != null);
        var damageRects = damageBufferValid
            ? new ReadOnlySpan<NativeDamageRect>(
                view->DamageRects,
                checked((int)header.DamageRectCount))
            : ReadOnlySpan<NativeDamageRect>.Empty;
        var damage = NativeSceneDamagePolicy.Evaluate(
            in header,
            damageRects,
            damageBufferValid,
            viewportChanged,
            new Size(effective.X, effective.Y));

        _damageRectangles += damage.RectangleCount;
        _damageArea += damage.SummedArea;
        if (damage.IsFull && damage.RequiresRender)
        {
            Interlocked.Increment(ref FullInvalidationCount);
        }
        else if (damage.RequiresRender)
        {
            Interlocked.Add(ref DamageRectangleCount, damage.RectangleCount);
        }
        return damage;
    }

    public override void OnRender(ImmediateDrawingContext drawingContext)
    {
        var requestedByWebScene =
            Interlocked.Exchange(ref _renderRequested, 0) != 0;
        Interlocked.Increment(ref RenderCallbackCount);
        if (!requestedByWebScene)
        {
            Interlocked.Increment(ref UnchangedRenderCallbackCount);
        }
        var mayAcquireDuringSynchronousRender =
            !requestedByWebScene
            && !_running
            && !_manualFrames
            && _publicationMailbox.PendingCount > 0;

        // Avalonia can also invoke this callback to repopulate a custom visual
        // whose composition backing was discarded. Returning without drawing
        // in that case exposes a cleared region, so every callback must replay
        // the retained scene even when WebScene did not publish a new diff.
        var renderStarted = Stopwatch.GetTimestamp();
        long retainedDrawTicks = 0;
        long skiaSubmitTicks = 0;

        if (drawingContext.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature))
                is not ISkiaSharpApiLeaseFeature feature)
        {
            // Scene acquisition now precedes invalidation so Avalonia can use
            // native damage. Preserve the applied retained scene and retry its
            // presentation on the next compositor boundary if this callback
            // has no drawable Skia surface.
            if (!_running)
            {
                _scheduleUiWake();
            }
            return;
        }

        // Avalonia performs synchronous composition renders when the operating
        // system asks it to paint during a live window resize. Animation-frame
        // callbacks are not guaranteed to run in that nested native event loop,
        // so consume and acknowledge a completed native scene here as well. This
        // keeps the producer's bounded ordered back-pressure moving and
        // makes every presented resize frame use real DOM layout, not a stretched
        // or mouse-up-delayed snapshot.
        if (mayAcquireDuringSynchronousRender
            && TryAcquireNextDiff(out _))
        {
            Interlocked.Increment(ref SynchronousRenderAcquisitionCount);
        }

        if (_viewportWidth <= 0 || _viewportHeight <= 0)
        {
            return;
        }

        using var lease = feature.Lease();
        var skiaStarted = Stopwatch.GetTimestamp();
        var canvas = lease.SkCanvas;
        var effective = EffectiveSize;
        var scale = NativeSceneResizeProjection.GetScale(
            effective.X,
            effective.Y,
            _viewportWidth,
            _viewportHeight);
        var save = canvas.Save();
        try
        {
            using var background = new SKPaint
            {
                Color = new SKColor(19, 23, 34, 255),
                Style = SKPaintStyle.Fill,
                BlendMode = SKBlendMode.Src
            };
            canvas.DrawRect(0, 0, (float)effective.X, (float)effective.Y, background);
            canvas.Scale(scale.X, scale.Y);
            var retainedStarted = Stopwatch.GetTimestamp();
            _renderer.RenderRetained(
                canvas,
                _viewportWidth,
                _viewportHeight,
                null);
            retainedDrawTicks = Stopwatch.GetTimestamp() - retainedStarted;
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
        skiaSubmitTicks = Stopwatch.GetTimestamp() - skiaStarted;

        if (_hasPendingRenderMetrics)
        {
            NativeSceneDrawOperation.RecordRendered(
                _pendingRenderHeader,
                _pendingDiffApplyTicks,
                retainedDrawTicks,
                skiaSubmitTicks,
                Stopwatch.GetTimestamp() - renderStarted,
                _pendingDiffCanvasCommandCount,
                _renderer.TotalCommandCount);
            _renderObserver.RecordRendered(_pendingRenderHeader);
            _hasPendingRenderMetrics = false;
            _pendingDamage = NativeSceneDamage.None;
            _pendingDiffApplyTicks = 0;
            _pendingDiffCanvasCommandCount = 0;
            if (_appliedDiffs % 300 == 0)
            {
                var damagePercent = _viewportArea > 0
                    ? _damageArea * 100 / _viewportArea
                    : 0;
                Console.WriteLine(
                    $"Composition scene diffs: {_appliedDiffs:N0}, " +
                    $"changed layers={_changedLayers:N0}, damage rects={_damageRectangles:N0}, " +
                    $"summed damage={damagePercent:F1}% of frame area");
                _changedLayers = 0;
                _damageRectangles = 0;
                _damageArea = 0;
                _viewportArea = 0;
            }
        }

        // An ordered producer may publish two diffs before Avalonia processes
        // their coalesced notification. Active composition drains the second
        // diff on its next animation frame. During the nested macOS live-resize
        // loop those callbacks are paused, so explicitly schedule the known
        // remaining publication; the counted signal guarantees this is not an
        // empty self-invalidation. Manual certification frames intentionally
        // remain one diff per requested test boundary.
        if (!_running
            && !_manualFrames
            && _publicationMailbox.PendingCount > 0)
        {
            // Avalonia can discard or delay an invalidation requested from inside
            // the current synchronous render. Ask the UI side for one more
            // composition commit only while the normal compositor clock is
            // suspended and another ordered scene is known to be pending.
            _scheduleUiWake();
        }
    }

    private bool NativeBoundsIntersectsRenderClip(SKRect nativeBounds)
    {
        if (_viewportWidth <= 0 || _viewportHeight <= 0)
        {
            return false;
        }
        var effective = EffectiveSize;
        var scale = NativeSceneResizeProjection.GetScale(
            effective.X,
            effective.Y,
            _viewportWidth,
            _viewportHeight);
        return RenderClipIntersectes(new Rect(
            nativeBounds.Left * scale.X,
            nativeBounds.Top * scale.Y,
            nativeBounds.Width * scale.X,
            nativeBounds.Height * scale.Y));
    }

    public override Rect GetRenderBounds()
        => new(0, 0, EffectiveSize.X, EffectiveSize.Y);

    private bool HasPendingPresentation
        => _hasPendingRenderMetrics
            || _publicationMailbox.PendingCount > 0;

    private static bool ValidateView(NativeSceneView* view)
        => view != null
            && view->StructSize == sizeof(NativeSceneView)
            && view->AbiVersion == 2
            && (view->Header.CommandCount == 0 || view->Commands != null)
            && (view->Header.CanvasLayerCount == 0
                || (view->CanvasLayers != null && view->CanvasCommands != null))
            && (view->StringCount == 0
                || (view->Strings != null && view->StringBytes != null));

}

internal sealed class NativeFrozenSceneControl :
    Control,
    INativeWebSceneFrozenPresentation
{
    private NativeFrozenSceneState? _state;

    internal NativeFrozenSceneControl(IntPtr checkpointScene, ulong estimatedBytes)
    {
        _state = new NativeFrozenSceneState(checkpointScene);
        EstimatedBytes = estimatedBytes;
        ClipToBounds = true;
        IsHitTestVisible = false;
        HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Stretch;
        VerticalAlignment = global::Avalonia.Layout.VerticalAlignment.Stretch;
    }

    public Control View => this;

    public ulong EstimatedBytes { get; }

    public override void Render(DrawingContext context)
    {
        base.Render(context);
        var state = Volatile.Read(ref _state);
        if (state is null)
        {
            return;
        }
        context.Custom(new NativeFrozenSceneDrawOperation(
            state,
            new Rect(Bounds.Size)));
    }

    public void Dispose()
    {
        Interlocked.Exchange(ref _state, null)?.Release();
    }
}

internal sealed unsafe class NativeFrozenSceneState
{
    private readonly object _gate = new();
    private readonly NativeCanvasSceneRenderer _renderer = new();
    private IntPtr _checkpointScene;
    private float _viewportWidth;
    private float _viewportHeight;
    private int _referenceCount = 1;

    internal NativeFrozenSceneState(IntPtr checkpointScene)
    {
        if (!TryReadHeader(checkpointScene, out _))
        {
            throw new ArgumentException(
                "The frozen presentation requires a valid native scene checkpoint.",
                nameof(checkpointScene));
        }
        _checkpointScene = checkpointScene;
    }

    internal void AddReference()
    {
        while (true)
        {
            var current = Volatile.Read(ref _referenceCount);
            if (current == 0)
            {
                throw new ObjectDisposedException(nameof(NativeFrozenSceneState));
            }
            if (Interlocked.CompareExchange(
                    ref _referenceCount,
                    checked(current + 1),
                    current)
                == current)
            {
                return;
            }
        }
    }

    internal void Release()
    {
        if (Interlocked.Decrement(ref _referenceCount) != 0)
        {
            return;
        }
        lock (_gate)
        {
            var scene = Interlocked.Exchange(ref _checkpointScene, IntPtr.Zero);
            if (scene != IntPtr.Zero)
            {
                NativeWebSceneApi.SceneRelease(scene);
            }
            _renderer.Reset();
        }
    }

    internal void Render(SKCanvas canvas, Rect bounds)
    {
        lock (_gate)
        {
            var scene = Volatile.Read(ref _checkpointScene);
            if (scene != IntPtr.Zero)
            {
                var view = (NativeSceneView*)scene;
                if (ValidateView(view) && _renderer.ApplyDiff(view))
                {
                    _viewportWidth = view->Header.ViewportWidth;
                    _viewportHeight = view->Header.ViewportHeight;
                }
                NativeWebSceneApi.SceneRelease(scene);
                _checkpointScene = IntPtr.Zero;
            }
            if (_viewportWidth <= 0 || _viewportHeight <= 0)
            {
                return;
            }

            var save = canvas.Save();
            try
            {
                canvas.ClipRect(new SKRect(
                    0,
                    0,
                    (float)bounds.Width,
                    (float)bounds.Height));
                canvas.Clear(new SKColor(19, 23, 34, 255));
                var scale = NativeSceneResizeProjection.GetScale(
                    (float)bounds.Width,
                    (float)bounds.Height,
                    _viewportWidth,
                    _viewportHeight);
                canvas.Scale(scale.X, scale.Y);
                _renderer.RenderRetained(
                    canvas,
                    _viewportWidth,
                    _viewportHeight,
                    null);
            }
            finally
            {
                canvas.RestoreToCount(save);
            }
        }
    }

    internal static bool TryReadHeader(IntPtr scene, out SceneHeader header)
    {
        var view = (NativeSceneView*)scene;
        if (!ValidateView(view))
        {
            header = default;
            return false;
        }
        header = view->Header;
        return true;
    }

    private static bool ValidateView(NativeSceneView* view)
        => view != null
            && view->StructSize == sizeof(NativeSceneView)
            && view->AbiVersion == 2
            && (view->Header.CommandCount == 0 || view->Commands != null)
            && (view->Header.CanvasLayerCount == 0
                || (view->CanvasLayers != null && view->CanvasCommands != null))
            && (view->StringCount == 0
                || (view->Strings != null && view->StringBytes != null));
}

internal sealed class NativeFrozenSceneDrawOperation :
    ICustomDrawOperation
{
    private NativeFrozenSceneState? _state;

    internal NativeFrozenSceneDrawOperation(
        NativeFrozenSceneState state,
        Rect bounds)
    {
        state.AddReference();
        _state = state;
        Bounds = bounds;
    }

    public Rect Bounds { get; }

    public bool HitTest(Point point) => false;

    public bool Equals(ICustomDrawOperation? other) => false;

    public void Render(ImmediateDrawingContext context)
    {
        var state = Volatile.Read(ref _state);
        if (state is null
            || context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature))
                is not ISkiaSharpApiLeaseFeature feature)
        {
            return;
        }
        using var lease = feature.Lease();
        state.Render(lease.SkCanvas, Bounds);
    }

    public void Dispose()
    {
        Interlocked.Exchange(ref _state, null)?.Release();
    }
}

internal sealed class NativeSceneDrawOperation : ICustomDrawOperation
{
    private readonly NativeCanvasSceneRenderer _renderer;
    private readonly object _rendererGate;
    private readonly NativeSceneRenderObserver _renderObserver;
    private IntPtr _scene;
    private bool _sceneApplied;

    internal NativeSceneDrawOperation(
        IntPtr scene,
        Rect bounds,
        NativeCanvasSceneRenderer renderer,
        object rendererGate,
        NativeSceneRenderObserver renderObserver)
    {
        _scene = scene;
        Bounds = bounds;
        _renderer = renderer;
        _rendererGate = rendererGate;
        _renderObserver = renderObserver;
    }

    public static int RenderCount;
    public static int RectCommandCount;
    public static int LineCommandCount;
    public static int TextCommandCount;
    public static int SvgCommandCount;
    public static long LastConsumedInputSequence;
    public static long LastRenderTimestamp;
    public static int LastViewportWidthBits;
    public static int LastViewportHeightBits;
    public static long LastDiffApplyTicks;
    public static long LastRetainedDrawTicks;
    public static long LastSkiaSubmitTicks;
    public static long LastRenderCallbackTicks;
    public static long LastResizeToRenderTicks;
    public static long LastDiffCanvasCommandCount;
    public static long LastRetainedCanvasCommandCount;
    private static long s_latestResizeSequence;
    private static long s_latestResizeTimestamp;

    public Rect Bounds { get; }

    public bool HitTest(Point point) => Bounds.Contains(point);

    public bool Equals(ICustomDrawOperation? other) => false;

    public unsafe void Render(ImmediateDrawingContext context)
    {
        lock (_rendererGate)
        {
            var scene = _scene;
            var view = (NativeSceneView*)scene;
            if (view == null
                || view->StructSize != sizeof(NativeSceneView)
                || view->AbiVersion != 2)
            {
                return;
            }

            var header = view->Header;
            if ((header.CommandCount != 0 && view->Commands == null)
                || (header.CanvasLayerCount != 0
                    && (view->CanvasLayers == null
                        || view->CanvasCommands == null))
                || (view->StringCount != 0
                    && (view->Strings == null
                        || view->StringBytes == null)))
            {
                return;
            }

            if (!_sceneApplied)
            {
                if (!_renderer.ApplyDiff(view))
                {
                    return;
                }
                _sceneApplied = true;
                NativeWebSceneApi.SceneAcknowledge(scene);
            }

            if (context.TryGetFeature(typeof(ISkiaSharpApiLeaseFeature))
                    is not ISkiaSharpApiLeaseFeature feature)
            {
                // Headless render targets may execute custom draw operations
                // without exposing a drawable Skia lease. Diff application and
                // acknowledgement above still keep diagnostic captures and the
                // producer's publication lifetime correct.
                return;
            }

            using var lease = feature.Lease();
            var canvas = lease.SkCanvas;
            canvas.Save();
            try
            {
                canvas.ClipRect(new SKRect(
                    0,
                    0,
                    (float)Bounds.Width,
                    (float)Bounds.Height));
                canvas.Clear(new SKColor(19, 23, 34, 255));
                var scale = NativeSceneResizeProjection.GetScale(
                    (float)Bounds.Width,
                    (float)Bounds.Height,
                    header.ViewportWidth,
                    header.ViewportHeight);
                canvas.Scale(scale.X, scale.Y);
                _renderer.RenderRetained(
                    canvas,
                    header.ViewportWidth,
                    header.ViewportHeight,
                    null);
                RecordRendered(header);
                _renderObserver.RecordRendered(header);
            }
            finally
            {
                canvas.Restore();
            }
        }
    }

    public void Dispose()
    {
        lock (_rendererGate)
        {
            var scene = _scene;
            _scene = IntPtr.Zero;
            if (scene != IntPtr.Zero)
            {
                NativeWebSceneApi.SceneRelease(scene);
            }
        }
    }

    internal static void RecordRendered(
        in SceneHeader header,
        long diffApplyTicks = 0,
        long retainedDrawTicks = 0,
        long skiaSubmitTicks = 0,
        long renderCallbackTicks = 0,
        long diffCanvasCommandCount = 0,
        long retainedCanvasCommandCount = 0)
    {
        Interlocked.Increment(ref RenderCount);
        Interlocked.Exchange(ref LastViewportWidthBits, BitConverter.SingleToInt32Bits(header.ViewportWidth));
        Interlocked.Exchange(ref LastViewportHeightBits, BitConverter.SingleToInt32Bits(header.ViewportHeight));
        Interlocked.Exchange(ref LastRenderTimestamp, Stopwatch.GetTimestamp());
        Interlocked.Exchange(ref LastDiffApplyTicks, diffApplyTicks);
        Interlocked.Exchange(ref LastRetainedDrawTicks, retainedDrawTicks);
        Interlocked.Exchange(ref LastSkiaSubmitTicks, skiaSubmitTicks);
        Interlocked.Exchange(ref LastRenderCallbackTicks, renderCallbackTicks);
        Interlocked.Exchange(ref LastDiffCanvasCommandCount, diffCanvasCommandCount);
        Interlocked.Exchange(ref LastRetainedCanvasCommandCount, retainedCanvasCommandCount);
        Interlocked.Exchange(
            ref LastConsumedInputSequence,
            unchecked((long)header.ConsumedInputSequence));
        var resizeSequence = unchecked((ulong)Interlocked.Read(ref s_latestResizeSequence));
        if (resizeSequence != 0 && header.ConsumedInputSequence >= resizeSequence)
        {
            var resizeTimestamp = Interlocked.Read(ref s_latestResizeTimestamp);
            if (resizeTimestamp != 0)
            {
                Interlocked.Exchange(
                    ref LastResizeToRenderTicks,
                    Stopwatch.GetTimestamp() - resizeTimestamp);
                Interlocked.Exchange(ref s_latestResizeSequence, 0);
            }
        }
    }

    internal static void RecordResizeSubmitted(ulong sequence, long timestamp)
    {
        Interlocked.Exchange(ref s_latestResizeTimestamp, timestamp);
        Interlocked.Exchange(ref s_latestResizeSequence, unchecked((long)sequence));
    }
}
#endif

internal sealed unsafe class NativeCanvasSceneRenderer
{
    private const uint CanvasCommandEvenOdd = 1u << 16;
    private const uint SceneCheckpoint = 1;
    private const uint SceneDomReplacement = 2;
    private const uint LayerReplace = 1;
    private const uint LayerRemove = 2;

    private readonly Dictionary<uint, RetainedLayer> s_layers = new();
    private readonly List<RetainedLayer> s_orderedLayers = [];
    private readonly Dictionary<StringKey, string> s_strings = new();
    private readonly Dictionary<string, SKTypeface> s_typefaces = new(StringComparer.Ordinal);
    private readonly Dictionary<string, SharedSvgPictureLease> s_svgPictures =
        new(StringComparer.Ordinal);
    private SKPicture? s_domBackdropPicture;
    private SKPicture? s_domOverlayPicture;
    private uint s_domCommandCount;
    private ulong s_revision;
    private long s_totalCommandCount;
    public static long RejectedDiffCount;

    public long TotalCommandCount => s_totalCommandCount;

    internal NativeRendererMemoryMetrics ReadMemoryMetrics()
    {
        ulong logicalBitmapBytes = 0;
        ulong isolationBitmapBytes = 0;
        var isolationLayerCount = 0;
        foreach (var layer in s_layers.Values)
        {
            var layerBytes = checked((ulong)layer.BitmapWidth * layer.BitmapHeight * 4U);
            logicalBitmapBytes += layerBytes;
            if (layer.RequiresIsolation)
            {
                isolationLayerCount++;
                isolationBitmapBytes += layerBytes;
            }
        }
        ulong stringBytes = 0;
        foreach (var value in s_strings.Values)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }
        foreach (var value in s_typefaces.Keys)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }
        foreach (var value in s_svgPictures.Keys)
        {
            stringBytes += checked((ulong)value.Length * sizeof(char));
        }

        return new NativeRendererMemoryMetrics(
            s_layers.Count,
            s_totalCommandCount,
            logicalBitmapBytes,
            isolationLayerCount,
            isolationBitmapBytes,
            s_domCommandCount,
            s_strings.Count,
            stringBytes,
            s_typefaces.Count,
            s_svgPictures.Count,
            SharedSvgPictureCache.EntryCount,
            SharedSvgPictureCache.ReferenceCount,
            SharedSvgPictureCache.MemoryHitCount);
    }

    public bool ApplyDiffAndRender(SKCanvas canvas, NativeSceneView* view)
    {
        if (!ApplyDiff(view))
        {
            return false;
        }
        RenderRetained(
            canvas,
            view->Header.ViewportWidth,
            view->Header.ViewportHeight,
            null);
        return true;
    }

    public bool ApplyDiff(NativeSceneView* view)
    {
        var header = view->Header;
        var checkpoint = (header.Flags & SceneCheckpoint) != 0;
        if (checkpoint)
        {
            Reset();
        }
        else if (header.Revision != s_revision && header.BaseRevision != s_revision)
        {
            Interlocked.Increment(ref RejectedDiffCount);
            return false;
        }

        if (header.Revision != s_revision)
        {
            if ((header.Flags & SceneDomReplacement) != 0)
            {
                s_domBackdropPicture?.Dispose();
                s_domOverlayPicture?.Dispose();
                s_domBackdropPicture = CompileDom(view, foreground: false);
                s_domOverlayPicture = CompileDom(view, foreground: true);
                s_domCommandCount = header.CommandCount;
            }

            var layerOrderChanged = false;
            var changes = new ReadOnlySpan<NativeCanvasLayer>(
                view->CanvasLayers,
                checked((int)header.CanvasLayerCount));
            foreach (ref readonly var change in changes)
            {
                if ((change.Flags & LayerRemove) != 0)
                {
                    if (s_layers.Remove(change.NodeId, out var removed))
                    {
                        s_totalCommandCount -= removed.CommandCount;
                        removed.Dispose();
                        layerOrderChanged = true;
                    }
                    continue;
                }
                if ((change.Flags & LayerReplace) == 0 || !ValidateLayer(view, change))
                {
                    return false;
                }
                var replacement = CompileLayer(view, change);
                var orderChanged = true;
                if (s_layers.Remove(change.NodeId, out var previous))
                {
                    orderChanged =
                        previous.ZOrder != replacement.ZOrder
                        || !ReplaceOrderedLayer(previous, replacement);
                    s_totalCommandCount -= previous.CommandCount;
                    previous.Dispose();
                }
                s_layers[change.NodeId] = replacement;
                s_totalCommandCount += replacement.CommandCount;
                layerOrderChanged |= orderChanged;
            }
            if (layerOrderChanged)
            {
                RebuildLayerOrder();
            }
            s_revision = header.Revision;
        }

        return true;
    }

    public void RenderRetained(
        SKCanvas canvas,
        float viewportWidth,
        float viewportHeight,
        Func<SKRect, bool>? intersects)
    {
        if (s_domBackdropPicture is not null
            && (intersects is null || intersects(new SKRect(0, 0, viewportWidth, viewportHeight))))
        {
            canvas.DrawPicture(s_domBackdropPicture);
        }
        foreach (var layer in s_orderedLayers)
        {
            if (layer.Width <= 0 || layer.Height <= 0
                || layer.BitmapWidth == 0 || layer.BitmapHeight == 0)
            {
                continue;
            }
            if (intersects is not null
                && !intersects(new SKRect(layer.X, layer.Y, layer.X + layer.Width, layer.Y + layer.Height)))
            {
                continue;
            }
            var save = canvas.Save();
            canvas.ClipRect(new SKRect(layer.X, layer.Y, layer.X + layer.Width, layer.Y + layer.Height));
            if (layer.RequiresIsolation)
            {
                // Browser canvases are independent transparent bitmaps. A
                // destructive operation must affect this canvas only, then the
                // result is source-over composited with lower siblings.
                canvas.SaveLayer();
            }
            canvas.Translate(layer.X, layer.Y);
            canvas.Scale(layer.Width / layer.BitmapWidth, layer.Height / layer.BitmapHeight);
            canvas.DrawPicture(layer.Picture);
            canvas.RestoreToCount(save);
        }
        if (s_domOverlayPicture is not null
            && (intersects is null || intersects(new SKRect(0, 0, viewportWidth, viewportHeight))))
        {
            canvas.DrawPicture(s_domOverlayPicture);
        }
    }

    private static bool ValidateLayer(NativeSceneView* view, in NativeCanvasLayer layer)
        => layer.CommandOffset <= view->CanvasCommandCount
            && layer.CommandCount <= view->CanvasCommandCount - layer.CommandOffset
            && layer.StringOffset <= view->StringCount
            && layer.StringCount <= view->StringCount - layer.StringOffset;

    private SKPicture CompileDom(NativeSceneView* view, bool foreground)
    {
        using var recorder = new SKPictureRecorder();
        var canvas = recorder.BeginRecording(new SKRect(
            0,
            0,
            Math.Max(1, view->Header.ViewportWidth),
            Math.Max(1, view->Header.ViewportHeight)));
        var commands = new ReadOnlySpan<SceneCommand>(
            view->Commands,
            checked((int)view->Header.CommandCount));
        using var fill = new SKPaint { Style = SKPaintStyle.Fill };
        using var stroke = new SKPaint { Style = SKPaintStyle.Stroke };
        using var opacity = new SKPaint();
        using var textPaint = new SKPaint
        {
            IsAntialias = true,
            Style = SKPaintStyle.Fill,
            TextAlign = SKTextAlign.Left
        };
        var textShapers = new Dictionary<string, SKShaper>(StringComparer.Ordinal);
        try
        {
            foreach (ref readonly var command in commands)
            {
                switch (command.Kind)
                {
                    case 30:
                        opacity.Color = new SKColor(
                            255,
                            255,
                            255,
                            (byte)(command.Rgba & 0xff));
                        canvas.SaveLayer(opacity);
                        break;
                    case 31:
                        canvas.Restore();
                        break;
                    case 15:
                        canvas.Save();
                        canvas.Translate(command.X, command.Y);
                        canvas.Scale(command.Width, command.Height);
                        canvas.Translate(-command.X, -command.Y);
                        break;
                    case 16:
                        canvas.Restore();
                        break;
                    case 19:
                        canvas.Save();
                        canvas.Translate(command.X, command.Y);
                        canvas.RotateDegrees(command.StrokeWidth);
                        canvas.Translate(-command.X, -command.Y);
                        break;
                    case 20:
                        canvas.Restore();
                        break;
                    case 17 when !foreground:
                    case 18 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        DrawDomShadow(canvas, command);
                        break;
                    case 1 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = false;
                        fill.Color = Rgba(command.Rgba);
                        canvas.DrawRect(command.X, command.Y, command.Width, command.Height, fill);
                        break;
                    case 2 when !foreground:
                        NativeSceneDrawOperation.LineCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(0.1f, command.Flags / 100f);
                        canvas.DrawLine(command.X, command.Y, command.Width, command.Height, stroke);
                        break;
                    case 3 when foreground:
                        NativeSceneDrawOperation.TextCommandCount++;
                        DrawDomText(canvas, view, command, textPaint, textShapers);
                        break;
                    case 4 when foreground:
                    case 5 when foreground:
                        NativeSceneDrawOperation.SvgCommandCount++;
                        DrawDomSvgPath(canvas, view, command, command.Kind == 5);
                        break;
                    case 6 when foreground:
                        NativeSceneDrawOperation.SvgCommandCount++;
                        DrawDomSvg(canvas, view, command);
                        break;
                    case 7 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = true;
                        fill.Color = Rgba(command.Rgba);
                        DrawDomRoundedRect(canvas, command, fill);
                        break;
                    case 8 when !foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(
                            0.1f,
                            command.StrokeWidth > 0
                                ? command.StrokeWidth
                                : (command.Flags & 0xffff) / 100f);
                        DrawDomRoundedBorder(canvas, command, stroke);
                        break;
                    case 9 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = false;
                        fill.Color = Rgba(command.Rgba);
                        canvas.DrawRect(command.X, command.Y, command.Width, command.Height, fill);
                        break;
                    case 10 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        fill.IsAntialias = true;
                        fill.Color = Rgba(command.Rgba);
                        DrawDomRoundedRect(canvas, command, fill);
                        break;
                    case 11 when foreground:
                        NativeSceneDrawOperation.RectCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(
                            0.1f,
                            command.StrokeWidth > 0
                                ? command.StrokeWidth
                                : (command.Flags & 0xffff) / 100f);
                        DrawDomRoundedBorder(canvas, command, stroke);
                        break;
                    case 14 when foreground:
                        NativeSceneDrawOperation.LineCommandCount++;
                        stroke.IsAntialias = true;
                        stroke.Color = Rgba(command.Rgba);
                        stroke.StrokeWidth = Math.Max(0.1f, command.Flags / 100f);
                        canvas.DrawLine(command.X, command.Y, command.Width, command.Height, stroke);
                        break;
                    case 12:
                        canvas.Save();
                        ClipDomRoundedRect(canvas, command);
                        break;
                    case 13:
                        canvas.Restore();
                        break;
                }
            }
        }
        finally
        {
            foreach (var shaper in textShapers.Values)
            {
                shaper.Dispose();
            }
        }
        return recorder.EndRecording();
    }

    private static void DrawDomRoundedRect(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint)
    {
        var topLeft = command.RadiusTopLeft;
        var topRight = command.RadiusTopRight;
        var bottomRight = command.RadiusBottomRight;
        var bottomLeft = command.RadiusBottomLeft;
        if (topLeft <= 0 && topRight <= 0 && bottomRight <= 0 && bottomLeft <= 0)
        {
            var legacyRadius = (command.Flags >> 16) / 100f;
            topLeft = topRight = bottomRight = bottomLeft = legacyRadius;
        }

        if (Math.Abs(topLeft - topRight) < 0.001f
            && Math.Abs(topLeft - bottomRight) < 0.001f
            && Math.Abs(topLeft - bottomLeft) < 0.001f)
        {
            canvas.DrawRoundRect(
                command.X,
                command.Y,
                command.Width,
                command.Height,
                topLeft,
                topLeft,
                paint);
            return;
        }

        using var rounded = new SKRoundRect();
        var radii = new SKPoint[4]
        {
            new(topLeft, topLeft),
            new(topRight, topRight),
            new(bottomRight, bottomRight),
            new(bottomLeft, bottomLeft)
        };
        rounded.SetRectRadii(
            new SKRect(
                command.X,
                command.Y,
                command.X + command.Width,
                command.Y + command.Height),
            radii);
        canvas.DrawRoundRect(rounded, paint);
    }

    private const uint DomBorderTop = 1u << 28;
    private const uint DomBorderRight = 1u << 29;
    private const uint DomBorderBottom = 1u << 30;
    private const uint DomBorderLeft = 1u << 31;
    private const uint DomBorderColorPartition = 1u << 27;
    private const uint DomBorderSideMask = DomBorderTop
        | DomBorderRight
        | DomBorderBottom
        | DomBorderLeft;

    private static void DrawDomRoundedBorder(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint)
    {
        var sides = command.Flags & DomBorderSideMask;
        if (sides == 0)
        {
            DrawDomRoundedRect(canvas, command, paint);
            return;
        }

        if ((command.Flags & DomBorderColorPartition) == 0)
        {
            DrawDomRoundedBorderSides(canvas, command, paint, sides);
            return;
        }

        var halfStroke = paint.StrokeWidth * 0.5f;
        var outerLeft = command.X - halfStroke;
        var outerTop = command.Y - halfStroke;
        var outerRight = command.X + command.Width + halfStroke;
        var outerBottom = command.Y + command.Height + halfStroke;
        var centerX = (outerLeft + outerRight) * 0.5f;
        var centerY = (outerTop + outerBottom) * 0.5f;
        var roundedCommand = command;

        DrawSide(DomBorderTop, outerLeft, outerTop, outerRight, outerTop);
        DrawSide(DomBorderRight, outerRight, outerTop, outerRight, outerBottom);
        DrawSide(DomBorderBottom, outerRight, outerBottom, outerLeft, outerBottom);
        DrawSide(DomBorderLeft, outerLeft, outerBottom, outerLeft, outerTop);

        void DrawSide(uint side, float firstX, float firstY, float secondX, float secondY)
        {
            if ((sides & side) == 0) return;
            using var wedge = new SKPath();
            wedge.MoveTo(firstX, firstY);
            wedge.LineTo(secondX, secondY);
            wedge.LineTo(centerX, centerY);
            wedge.Close();
            canvas.Save();
            canvas.ClipPath(wedge, SKClipOperation.Intersect, antialias: true);
            DrawDomRoundedRect(canvas, roundedCommand, paint);
            canvas.Restore();
        }
    }

    private static void DrawDomRoundedBorderSides(
        SKCanvas canvas,
        in SceneCommand command,
        SKPaint paint,
        uint sides)
    {
        var left = command.X;
        var top = command.Y;
        var right = command.X + command.Width;
        var bottom = command.Y + command.Height;
        var topLeft = command.RadiusTopLeft;
        var topRight = command.RadiusTopRight;
        var bottomRight = command.RadiusBottomRight;
        var bottomLeft = command.RadiusBottomLeft;
        const float arcHandle = 0.55228475f;
        using var path = new SKPath();

        if ((sides & DomBorderTop) != 0)
        {
            path.MoveTo(left + topLeft, top);
            path.LineTo(right - topRight, top);
        }
        if ((sides & DomBorderRight) != 0)
        {
            path.MoveTo(right, top + topRight);
            path.LineTo(right, bottom - bottomRight);
        }
        if ((sides & DomBorderBottom) != 0)
        {
            path.MoveTo(right - bottomRight, bottom);
            path.LineTo(left + bottomLeft, bottom);
        }
        if ((sides & DomBorderLeft) != 0)
        {
            path.MoveTo(left, bottom - bottomLeft);
            path.LineTo(left, top + topLeft);
        }

        AppendCorner(DomBorderTop, DomBorderLeft, topLeft,
            left + topLeft, top, left, top + topLeft, true, true);
        AppendCorner(DomBorderTop, DomBorderRight, topRight,
            right - topRight, top, right, top + topRight, false, true);
        AppendCorner(DomBorderRight, DomBorderBottom, bottomRight,
            right, bottom - bottomRight, right - bottomRight, bottom, false, false);
        AppendCorner(DomBorderBottom, DomBorderLeft, bottomLeft,
            left + bottomLeft, bottom, left, bottom - bottomLeft, true, false);
        canvas.DrawPath(path, paint);

        void AppendCorner(
            uint firstSide,
            uint secondSide,
            float radius,
            float startX,
            float startY,
            float endX,
            float endY,
            bool leftCorner,
            bool topCorner)
        {
            if ((sides & (firstSide | secondSide)) != (firstSide | secondSide) || radius <= 0) return;
            path.MoveTo(startX, startY);
            var control = radius * arcHandle;
            if (topCorner && leftCorner)
                path.CubicTo(startX - control, startY, endX, endY - control, endX, endY);
            else if (topCorner)
                path.CubicTo(startX + control, startY, endX, endY - control, endX, endY);
            else if (leftCorner)
                path.CubicTo(startX - control, startY, endX, endY + control, endX, endY);
            else
                path.CubicTo(startX, startY + control, endX + control, endY, endX, endY);
        }
    }

    private static void ClipDomRoundedRect(SKCanvas canvas, in SceneCommand command)
    {
        if (command.RadiusTopLeft <= 0
            && command.RadiusTopRight <= 0
            && command.RadiusBottomRight <= 0
            && command.RadiusBottomLeft <= 0)
        {
            canvas.ClipRect(
                new SKRect(
                    command.X,
                    command.Y,
                    command.X + command.Width,
                    command.Y + command.Height),
                SKClipOperation.Intersect,
                antialias: false);
            return;
        }

        using var rounded = new SKRoundRect();
        rounded.SetRectRadii(
            new SKRect(
                command.X,
                command.Y,
                command.X + command.Width,
                command.Y + command.Height),
            [
                new(command.RadiusTopLeft, command.RadiusTopLeft),
                new(command.RadiusTopRight, command.RadiusTopRight),
                new(command.RadiusBottomRight, command.RadiusBottomRight),
                new(command.RadiusBottomLeft, command.RadiusBottomLeft)
            ]);
        canvas.ClipRoundRect(rounded, SKClipOperation.Intersect, antialias: true);
    }

    private void DrawDomText(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command,
        SKPaint paint,
        Dictionary<string, SKShaper> shapers)
    {
        var resource = DomStringAt(view, command.Flags);
        var parts = resource.Split('\t', 6);
        if (parts.Length != 6
            || !float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var fontSize)
            || fontSize <= 0)
        {
            return;
        }
        var lineHeight = float.TryParse(
            parts[1],
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out var parsedLineHeight)
            && parsedLineHeight > 0
            ? parsedLineHeight
            : fontSize * 1.2f;
        var fontWeight = int.TryParse(
            parts[2],
            NumberStyles.Integer,
            CultureInfo.InvariantCulture,
            out var parsedWeight)
            ? Math.Clamp(parsedWeight, 1, 1000)
            : 400;
        var typeface = NativeTextShaping.ResolveTypeface(parts[4], fontWeight);
        paint.Color = Rgba(command.Rgba);
        paint.TextSize = fontSize;
        paint.Typeface = typeface;
        var shaperKey = parts[4] + '\t' + fontWeight.ToString(CultureInfo.InvariantCulture);
        if (!shapers.TryGetValue(shaperKey, out var shaper))
        {
            shaper = new SKShaper(typeface);
            shapers.Add(shaperKey, shaper);
        }
        var featureFlags = NativeTextShaping.ResolveFeatureFlags(
            parts[5],
            parts[4],
            0);
        var tabularDigitScale = NativeTextShaping.ResolveTabularDigitScale(parts[4]);
        var shapedWidth = NativeTextShaping.MeasureShapedWidth(
            shaper,
            parts[5],
            paint,
            featureFlags,
            tabularDigitScale);
        var widthScale = (featureFlags & NativeTextShaping.TabularNumerals) != 0
            ? NativeTextShaping.ResolveWidthScale(parts[4], fontSize, fontWeight)
            : 1f;
        var renderedWidth = shapedWidth * widthScale;
        var x = parts[3] switch
        {
            "center" => command.X + (command.Width - renderedWidth) * 0.5f,
            "right" or "end" => command.X + command.Width - renderedWidth,
            _ => command.X,
        };
        paint.GetFontMetrics(out var metrics);
        var glyphHeight = metrics.Descent - metrics.Ascent;
        var contentHeight = Math.Min(Math.Max(lineHeight, glyphHeight), Math.Max(lineHeight, command.Height));
        var baseline = command.Y
            + Math.Max(0, (command.Height - contentHeight) * 0.5f)
            + (contentHeight - glyphHeight) * 0.5f
            - metrics.Ascent
            + (parsedLineHeight == 0 ? 3f : 0f);
        canvas.Save();
        canvas.Scale(widthScale, 1f, x, baseline);
        NativeTextShaping.DrawShapedText(
            canvas,
            shaper,
            parts[5],
            x,
            baseline,
            paint,
            featureFlags,
            tabularDigitScale);
        canvas.Restore();
    }

    private static void DrawDomShadow(SKCanvas canvas, in SceneCommand command)
    {
        using var blur = command.StrokeWidth > 0
            ? SKMaskFilter.CreateBlur(SKBlurStyle.Normal, Math.Max(0.1f, command.StrokeWidth * 0.5f))
            : null;
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Style = SKPaintStyle.Fill,
            Color = Rgba(command.Rgba),
            MaskFilter = blur
        };
        DrawDomRoundedRect(canvas, command, paint);
    }

    private static void DrawDomSvgPath(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command,
        bool stroke)
    {
        var resource = DomStringAt(view, command.Flags);
        var parts = resource.Split('\t', 4);
        if (parts.Length != 4 || command.Width <= 0 || command.Height <= 0)
        {
            return;
        }
        var viewBox = ParseSvgNumbers(parts[0]);
        if (viewBox.Length < 4 || viewBox[2] == 0 || viewBox[3] == 0)
        {
            return;
        }
        using var path = SKPath.ParseSvgPathData(parts[3]);
        if (path is null)
        {
            return;
        }
        using var paint = new SKPaint
        {
            IsAntialias = true,
            Style = stroke ? SKPaintStyle.Stroke : SKPaintStyle.Fill,
            Color = Rgba(command.Rgba),
            StrokeWidth = stroke
                && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var width)
                    ? Math.Max(0.1f, width)
                    : 1
        };
        var save = canvas.Save();
        try
        {
            ApplyDomRotation(canvas, command);
            canvas.Translate(command.X, command.Y);
            canvas.Scale(command.Width / viewBox[2], command.Height / viewBox[3]);
            canvas.Translate(-viewBox[0], -viewBox[1]);
            ApplySvgTransform(canvas, parts[2]);
            canvas.DrawPath(path, paint);
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
    }

    private void DrawDomSvg(
        SKCanvas canvas,
        NativeSceneView* view,
        in SceneCommand command)
    {
        var resource = DomStringAt(view, command.Flags);
        var separator = resource.IndexOf('\t');
        if (separator <= 0 || separator == resource.Length - 1
            || command.Width <= 0 || command.Height <= 0)
        {
            return;
        }
        var viewBox = ParseSvgNumbers(resource[..separator]);
        if (viewBox.Length < 4 || viewBox[2] == 0 || viewBox[3] == 0)
        {
            return;
        }
        var markup = resource[(separator + 1)..];
        if (!s_svgPictures.TryGetValue(markup, out var svg))
        {
            var acquired = SharedSvgPictureCache.Acquire(markup);
            if (acquired is null)
            {
                return;
            }
            svg = acquired;
            s_svgPictures.Add(svg.Markup, svg);
        }

        var save = canvas.Save();
        try
        {
            ApplyDomRotation(canvas, command);
            canvas.Translate(command.X, command.Y);
            canvas.Scale(command.Width / viewBox[2], command.Height / viewBox[3]);
            canvas.Translate(-viewBox[0], -viewBox[1]);
            canvas.DrawPicture(svg.Picture);
        }
        finally
        {
            canvas.RestoreToCount(save);
        }
    }

    private static void ApplyDomRotation(SKCanvas canvas, in SceneCommand command)
    {
        if (Math.Abs(command.StrokeWidth) < 0.001f)
        {
            return;
        }
        canvas.RotateDegrees(
            command.StrokeWidth,
            command.X + command.Width / 2,
            command.Y + command.Height / 2);
    }

    private static float[] ParseSvgNumbers(string value)
        => value.Split(
                [' ', ','],
                StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
            .Select(item => float.TryParse(
                    item,
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsed)
                ? parsed
                : float.NaN)
            .Where(float.IsFinite)
            .ToArray();

    private static void ApplySvgTransform(SKCanvas canvas, string transform)
    {
        var cursor = 0;
        while (cursor < transform.Length)
        {
            while (cursor < transform.Length && char.IsWhiteSpace(transform[cursor])) cursor++;
            var open = transform.IndexOf('(', cursor);
            if (open < 0) break;
            var close = transform.IndexOf(')', open + 1);
            if (close < 0) break;
            var operation = transform[cursor..open].Trim();
            var values = ParseSvgNumbers(transform[(open + 1)..close]);
            switch (operation)
            {
                case "translate" when values.Length >= 1:
                    canvas.Translate(values[0], values.Length >= 2 ? values[1] : 0);
                    break;
                case "scale" when values.Length >= 1:
                    canvas.Scale(values[0], values.Length >= 2 ? values[1] : values[0]);
                    break;
                case "rotate" when values.Length >= 1:
                    if (values.Length >= 3)
                    {
                        canvas.Translate(values[1], values[2]);
                        canvas.RotateDegrees(values[0]);
                        canvas.Translate(-values[1], -values[2]);
                    }
                    else
                    {
                        canvas.RotateDegrees(values[0]);
                    }
                    break;
                case "matrix" when values.Length >= 6:
                    var matrix = new SKMatrix
                    {
                        ScaleX = values[0],
                        SkewY = values[1],
                        SkewX = values[2],
                        ScaleY = values[3],
                        TransX = values[4],
                        TransY = values[5],
                        Persp2 = 1
                    };
#if WEBSCENE_UNO
                    canvas.Concat(in matrix);
#else
                    canvas.Concat(ref matrix);
#endif
                    break;
            }
            cursor = close + 1;
        }
    }

    private static string DomStringAt(NativeSceneView* view, uint index)
    {
        if (index >= view->StringCount || view->Strings == null || view->StringBytes == null)
        {
            return string.Empty;
        }
        var descriptor = view->Strings[index];
        if (descriptor.ByteOffset > view->StringByteCount
            || descriptor.ByteLength > view->StringByteCount - descriptor.ByteOffset)
        {
            return string.Empty;
        }
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
            view->StringBytes + descriptor.ByteOffset,
            checked((int)descriptor.ByteLength)));
    }

    private RetainedLayer CompileLayer(NativeSceneView* view, in NativeCanvasLayer layer)
    {
        var requiresIsolation = RequiresIsolation(view, layer);
        using var recorder = new SKPictureRecorder();
        var canvas = recorder.BeginRecording(new SKRect(
            0,
            0,
            Math.Max(1, layer.BitmapWidth),
            Math.Max(1, layer.BitmapHeight)));
        Replay(canvas, view, layer, skipLeadingClears: !requiresIsolation);
        var picture = recorder.EndRecording();
        DumpLayerIfRequested(view, layer, picture);
        return new RetainedLayer(
            layer.NodeId,
            layer.Generation,
            layer.Reserved,
            layer.X,
            layer.Y,
            layer.Width,
            layer.Height,
            layer.BitmapWidth,
            layer.BitmapHeight,
            layer.CommandCount,
            requiresIsolation,
            picture);
    }

    private bool RequiresIsolation(
        NativeSceneView* view,
        in NativeCanvasLayer layer)
    {
        var hasDrawn = false;
        var commands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        foreach (ref readonly var command in commands)
        {
            switch (command.Kind)
            {
                // A clear before the first draw is a no-op on the initially
                // transparent browser canvas and is omitted from the picture.
                case 24 when hasDrawn:
                // drawImage(canvas) needs source-bitmap isolation semantics.
                case 27:
                    return true;
                case 53:
                    var composite = StringAt(view, layer, command.ResourceId);
                    if (!string.IsNullOrEmpty(composite)
                        && !string.Equals(
                            composite,
                            "source-over",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                    break;
                case >= 20 and <= 29 when command.Kind != 24:
                    hasDrawn = true;
                    break;
            }
        }
        return false;
    }

    private void DumpLayerIfRequested(
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        SKPicture picture)
    {
        var directory = Environment.GetEnvironmentVariable("WEBSCENE_PROBE_DUMP_LAYERS");
        if (string.IsNullOrWhiteSpace(directory)
            || layer.BitmapWidth == 0
            || layer.BitmapHeight == 0
            || layer.BitmapWidth > 16_384
            || layer.BitmapHeight > 16_384)
        {
            return;
        }

        Directory.CreateDirectory(directory);
        using var surface = SKSurface.Create(new SKImageInfo(
            checked((int)layer.BitmapWidth),
            checked((int)layer.BitmapHeight),
            SKColorType.Rgba8888,
            SKAlphaType.Premul));
        if (surface is null)
        {
            return;
        }
        surface.Canvas.Clear(SKColors.Transparent);
        surface.Canvas.DrawPicture(picture);
        surface.Canvas.Flush();
        using var image = surface.Snapshot();
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        var path = Path.Combine(
            directory,
            $"canvas-{layer.NodeId}-generation-{layer.Generation}.png");
        using var stream = File.Create(path);
        data.SaveTo(stream);

        using var commands = new StreamWriter(Path.ChangeExtension(path, ".fill-rects.tsv"));
        commands.WriteLine("index\tfillStyle\tx\ty\twidth\theight\ttransformedX\ttransformedY\ttransformedWidth\ttransformedHeight");
        var fillStyle = "#000000";
        var transform = CanvasAffine.Identity;
        var transforms = new Stack<CanvasAffine>();
        var layerCommands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        using (var trace = new StreamWriter(Path.ChangeExtension(path, ".commands.tsv")))
        {
            trace.WriteLine("index\tkind\tresourceId\tv0\tv1\tv2\tv3\tv4\tv5\tv6\tv7\tresource");
            for (var index = 0; index < layerCommands.Length; ++index)
            {
                ref readonly var command = ref layerCommands[index];
                var resource = command.Kind is 25 or 26 or 28 or 29 or 40 or 41 or 43 or 44
                    or 48 or 49 or 50 or 52 or 53 or 54
                    ? StringAt(view, layer, command.ResourceId)
                    : string.Empty;
                trace.WriteLine(string.Create(
                    CultureInfo.InvariantCulture,
                    $"{index}\t{command.Kind}\t{command.ResourceId}\t{command.V0}\t{command.V1}\t{command.V2}\t{command.V3}\t{command.V4}\t{command.V5}\t{command.V6}\t{command.V7}\t{resource.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ')}"));
            }
        }
        for (var index = 0; index < layerCommands.Length; ++index)
        {
            ref readonly var command = ref layerCommands[index];
            switch (command.Kind)
            {
                case 1:
                    transforms.Push(transform);
                    break;
                case 2 when transforms.Count != 0:
                    transform = transforms.Pop();
                    break;
                case 3:
                    transform = CanvasAffine.Identity;
                    break;
                case 4:
                    transform = CanvasAffine.From(command);
                    break;
                case 5:
                    transform = transform.Multiply(CanvasAffine.From(command));
                    break;
                case 6:
                    transform = transform.Multiply(new CanvasAffine(1, 0, 0, 1, command.V0, command.V1));
                    break;
                case 7:
                    transform = transform.Multiply(new CanvasAffine(command.V0, 0, 0, command.V1, 0, 0));
                    break;
                case 8:
                    transform = transform.Multiply(new CanvasAffine(
                        Math.Cos(command.V0),
                        Math.Sin(command.V0),
                        -Math.Sin(command.V0),
                        Math.Cos(command.V0),
                        0,
                        0));
                    break;
                case 40:
                    fillStyle = StringAt(view, layer, command.ResourceId);
                    break;
                case 22:
                {
                    var first = transform.Map(command.V0, command.V1);
                    var second = transform.Map(command.V0 + command.V2, command.V1 + command.V3);
                    var left = Math.Min(first.X, second.X);
                    var top = Math.Min(first.Y, second.Y);
                    commands.WriteLine(string.Create(
                        CultureInfo.InvariantCulture,
                        $"{index}\t{fillStyle}\t{command.V0}\t{command.V1}\t{command.V2}\t{command.V3}" +
                        $"\t{left}\t{top}\t{Math.Abs(second.X - first.X)}\t{Math.Abs(second.Y - first.Y)}"));
                    break;
                }
            }
        }
    }

    private void Replay(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        bool skipLeadingClears = false)
    {
        var state = CanvasState.Default;
        var states = new Stack<CanvasState>();
        var hasDrawn = false;
        using var path = new SKPath();
        var commands = new ReadOnlySpan<NativeCanvasCommand>(
            view->CanvasCommands + layer.CommandOffset,
            checked((int)layer.CommandCount));
        foreach (ref readonly var command in commands)
        {
            switch (command.Kind)
            {
                case 1:
                    states.Push(state);
                    canvas.Save();
                    break;
                case 2:
                    if (states.Count != 0)
                    {
                        state = states.Pop();
                        canvas.Restore();
                    }
                    break;
                case 3:
                    canvas.ResetMatrix();
                    break;
                case 4:
                    canvas.SetMatrix(ToMatrix(command));
                    break;
                case 5:
                {
                    var matrix = ToMatrix(command);
#if WEBSCENE_UNO
                    canvas.Concat(in matrix);
#else
                    canvas.Concat(ref matrix);
#endif
                    break;
                }
                case 6: canvas.Translate((float)command.V0, (float)command.V1); break;
                case 7: canvas.Scale((float)command.V0, (float)command.V1); break;
                case 8: canvas.RotateRadians((float)command.V0); break;
                case 9: path.Reset(); break;
                case 10: path.Close(); break;
                case 11: path.MoveTo((float)command.V0, (float)command.V1); break;
                case 12: path.LineTo((float)command.V0, (float)command.V1); break;
                case 13:
                    path.CubicTo(
                        (float)command.V0, (float)command.V1,
                        (float)command.V2, (float)command.V3,
                        (float)command.V4, (float)command.V5);
                    break;
                case 14:
                    path.QuadTo(
                        (float)command.V0, (float)command.V1,
                        (float)command.V2, (float)command.V3);
                    break;
                case 15:
                    AppendArc(path, command);
                    break;
                case 16:
                    path.ArcTo(
                        new SKPoint((float)command.V0, (float)command.V1),
                        new SKPoint((float)command.V2, (float)command.V3),
                        (float)Math.Max(0, command.V4));
                    break;
                case 17:
                    path.AddRect(new SKRect(
                        (float)command.V0,
                        (float)command.V1,
                        (float)(command.V0 + command.V2),
                        (float)(command.V1 + command.V3)));
                    break;
                case 18:
                    canvas.ClipPath(path, SKClipOperation.Intersect, true);
                    break;
                case 19:
                {
                    var count = Math.Clamp((int)command.V0, 0, 7);
                    state.LineDash = count switch
                    {
                        0 => [],
                        1 => [command.V1],
                        2 => [command.V1, command.V2],
                        3 => [command.V1, command.V2, command.V3],
                        4 => [command.V1, command.V2, command.V3, command.V4],
                        5 => [command.V1, command.V2, command.V3, command.V4, command.V5],
                        6 => [command.V1, command.V2, command.V3, command.V4, command.V5, command.V6],
                        _ => [command.V1, command.V2, command.V3, command.V4, command.V5, command.V6, command.V7]
                    };
                    break;
                }
                case 20:
                    using (var stroke = CreatePaint(state, false, SKPaintStyle.Stroke))
                    {
                        canvas.DrawPath(path, stroke);
                    }
                    hasDrawn = true;
                    break;
                case 21:
                    using (var fill = CreatePaint(state, true, SKPaintStyle.Fill))
                    {
                        path.FillType = (command.Flags & CanvasCommandEvenOdd) != 0
                            ? SKPathFillType.EvenOdd
                            : SKPathFillType.Winding;
                        canvas.DrawPath(path, fill);
                    }
                    hasDrawn = true;
                    break;
                case 22:
                    using (var fill = CreatePaint(state, true, SKPaintStyle.Fill))
                    {
                        canvas.DrawRect(ToRect(command), fill);
                    }
                    hasDrawn = true;
                    break;
                case 23:
                    using (var stroke = CreatePaint(state, false, SKPaintStyle.Stroke))
                    {
                        canvas.DrawRect(ToRect(command), stroke);
                    }
                    hasDrawn = true;
                    break;
                case 24 when skipLeadingClears && !hasDrawn:
                    break;
                case 24:
                    using (var clear = new SKPaint { BlendMode = SKBlendMode.Clear, Style = SKPaintStyle.Fill })
                    {
                        canvas.DrawRect(ToRect(command), clear);
                    }
                    break;
                case 25:
                    DrawText(canvas, view, layer, command, state, false);
                    hasDrawn = true;
                    break;
                case 26:
                    DrawText(canvas, view, layer, command, state, true);
                    hasDrawn = true;
                    break;
                case 27:
                    DrawCanvas(canvas, command, state);
                    hasDrawn = true;
                    break;
                case 28:
                    DrawSvgCanvasPath(canvas, view, layer, command, state, true);
                    hasDrawn = true;
                    break;
                case 29:
                    DrawSvgCanvasPath(canvas, view, layer, command, state, false);
                    hasDrawn = true;
                    break;
                case 40: state.FillStyle = StringAt(view, layer, command.ResourceId); break;
                case 41: state.StrokeStyle = StringAt(view, layer, command.ResourceId); break;
                case 42: state.LineWidth = command.V0; break;
                case 43: state.LineCap = StringAt(view, layer, command.ResourceId); break;
                case 44: state.LineJoin = StringAt(view, layer, command.ResourceId); break;
                case 45: state.MiterLimit = command.V0; break;
                case 46: state.GlobalAlpha = Math.Clamp(command.V0, 0, 1); break;
                case 47: state.LineDashOffset = command.V0; break;
                case 48: state.Font = StringAt(view, layer, command.ResourceId); break;
                case 49: state.TextAlign = StringAt(view, layer, command.ResourceId); break;
                case 50: state.TextBaseline = StringAt(view, layer, command.ResourceId); break;
                case 51: state.ImageSmoothingEnabled = command.V0 != 0; break;
                case 52: state.ImageSmoothingQuality = StringAt(view, layer, command.ResourceId); break;
                case 53: state.Composite = StringAt(view, layer, command.ResourceId); break;
                case 54: state.ShadowColor = StringAt(view, layer, command.ResourceId); break;
                case 55: state.ShadowBlur = command.V0; break;
                case 56: state.ShadowOffsetX = command.V0; break;
                case 57: state.ShadowOffsetY = command.V0; break;
            }
        }
    }

    private void DrawSvgCanvasPath(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        in NativeCanvasCommand command,
        in CanvasState state,
        bool fill)
    {
        using var path = SKPath.ParseSvgPathData(StringAt(view, layer, command.ResourceId));
        if (path is null)
        {
            return;
        }
        path.FillType = fill && (command.Flags & CanvasCommandEvenOdd) != 0
            ? SKPathFillType.EvenOdd
            : SKPathFillType.Winding;
        if ((command.Flags & 0xFFFFu) >= 6u)
        {
            var matrix = ToMatrix(command);
            path.Transform(matrix);
        }
        using var paint = CreatePaint(
            state,
            fill,
            fill ? SKPaintStyle.Fill : SKPaintStyle.Stroke);
        canvas.DrawPath(path, paint);
    }

    private void DrawCanvas(SKCanvas canvas, in NativeCanvasCommand command, in CanvasState state)
    {
        if (!s_layers.TryGetValue(command.ResourceId, out var source)
            || command.V2 == 0 || command.V3 == 0)
        {
            return;
        }
        var destination = new SKRect(
            (float)command.V4,
            (float)command.V5,
            (float)(command.V4 + command.V6),
            (float)(command.V5 + command.V7));
        var save = canvas.Save();
        canvas.ClipRect(destination);
        canvas.Translate((float)command.V4, (float)command.V5);
        canvas.Scale((float)(command.V6 / command.V2), (float)(command.V7 / command.V3));
        canvas.Translate((float)-command.V0, (float)-command.V1);
        using var paint = CreatePaint(state, true, SKPaintStyle.Fill);
        canvas.DrawPicture(source.Picture, paint);
        canvas.RestoreToCount(save);
    }

    private void DrawText(
        SKCanvas canvas,
        NativeSceneView* view,
        in NativeCanvasLayer layer,
        in NativeCanvasCommand command,
        in CanvasState state,
        bool stroke)
    {
        var text = StringAt(view, layer, command.ResourceId);
        if (text.Length == 0) return;
        using var paint = CreatePaint(
            state,
            !stroke,
            stroke ? SKPaintStyle.Stroke : SKPaintStyle.Fill);
        ConfigureFont(paint, state.Font);
        paint.TextAlign = state.TextAlign switch
        {
            "center" => SKTextAlign.Center,
            "right" or "end" => SKTextAlign.Right,
            _ => SKTextAlign.Left
        };
        var y = (float)command.V1;
        var metrics = paint.FontMetrics;
        y += state.TextBaseline switch
        {
            "top" => -metrics.Top,
            "hanging" => -metrics.Ascent * 0.8f,
            "middle" => -(metrics.Ascent + metrics.Descent) / 2,
            "bottom" or "ideographic" => -metrics.Bottom,
            _ => 0
        };
        canvas.DrawText(text, (float)command.V0, y, paint);
    }

    private static SKPaint CreatePaint(in CanvasState state, bool fill, SKPaintStyle style)
    {
        var color = ParseColor(fill ? state.FillStyle : state.StrokeStyle);
        color = color.WithAlpha((byte)Math.Clamp(
            Math.Round(color.Alpha * state.GlobalAlpha),
            0,
            255));
        var paint = new SKPaint
        {
            IsAntialias = true,
            Style = style,
            Color = color,
            StrokeWidth = (float)Math.Max(0, state.LineWidth),
            StrokeMiter = (float)Math.Max(0, state.MiterLimit),
            StrokeCap = state.LineCap switch
            {
                "round" => SKStrokeCap.Round,
                "square" => SKStrokeCap.Square,
                _ => SKStrokeCap.Butt
            },
            StrokeJoin = state.LineJoin switch
            {
                "round" => SKStrokeJoin.Round,
                "bevel" => SKStrokeJoin.Bevel,
                _ => SKStrokeJoin.Miter
            },
            BlendMode = BlendMode(state.Composite)
        };
        if (!fill && state.LineDash is { Length: > 0 })
        {
            paint.PathEffect = SKPathEffect.CreateDash(
                state.LineDash.Select(static value => (float)value).ToArray(),
                (float)state.LineDashOffset);
        }
        return paint;
    }

    private void ConfigureFont(SKPaint paint, string font)
    {
        var px = font.IndexOf("px", StringComparison.OrdinalIgnoreCase);
        var size = 10f;
        var family = "sans-serif";
        if (px > 0)
        {
            var start = px - 1;
            while (start >= 0 && (char.IsDigit(font[start]) || font[start] is '.' or '-' or '+')) start--;
            if (float.TryParse(
                    font.AsSpan(start + 1, px - start - 1),
                    NumberStyles.Float,
                    CultureInfo.InvariantCulture,
                    out var parsedSize)
                && parsedSize > 0)
            {
                size = parsedSize;
            }
            var families = font[(px + 2)..].Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            if (families.Length != 0)
            {
                family = families[0].Trim('"', '\'');
            }
        }
        if (!s_typefaces.TryGetValue(family, out var typeface))
        {
            typeface = SKTypeface.FromFamilyName(family) ?? SKTypeface.Default;
            s_typefaces[family] = typeface;
        }
        paint.Typeface = typeface;
        paint.TextSize = size;
    }

    private static void AppendArc(SKPath path, in NativeCanvasCommand command)
    {
        var radius = Math.Abs(command.V2);
        if (radius <= 0) return;
        var start = command.V3;
        var end = command.V4;
        var anticlockwise = command.V5 != 0;
        const double Tau = Math.PI * 2;
        var sweep = end - start;
        if (!anticlockwise)
        {
            while (sweep < 0) sweep += Tau;
            sweep = Math.Min(sweep, Tau);
        }
        else
        {
            while (sweep > 0) sweep -= Tau;
            sweep = Math.Max(sweep, -Tau);
        }
        if (Math.Abs(Math.Abs(sweep) - Tau) < 0.000001)
        {
            path.AddCircle(
                (float)command.V0,
                (float)command.V1,
                (float)radius,
                anticlockwise ? SKPathDirection.CounterClockwise : SKPathDirection.Clockwise);
            return;
        }
        var oval = new SKRect(
            (float)(command.V0 - radius),
            (float)(command.V1 - radius),
            (float)(command.V0 + radius),
            (float)(command.V1 + radius));
        path.ArcTo(oval, (float)(start * 180 / Math.PI), (float)(sweep * 180 / Math.PI), false);
    }

    private static SKMatrix ToMatrix(in NativeCanvasCommand command)
        => new()
        {
            ScaleX = (float)command.V0,
            SkewY = (float)command.V1,
            SkewX = (float)command.V2,
            ScaleY = (float)command.V3,
            TransX = (float)command.V4,
            TransY = (float)command.V5,
            Persp2 = 1
        };

    private static SKRect ToRect(in NativeCanvasCommand command)
        => new(
            (float)command.V0,
            (float)command.V1,
            (float)(command.V0 + command.V2),
            (float)(command.V1 + command.V3));

    private string StringAt(NativeSceneView* view, in NativeCanvasLayer layer, uint localIndex)
    {
        if (localIndex >= layer.StringCount) return string.Empty;
        var key = new StringKey(layer.NodeId, layer.Generation, localIndex);
        if (s_strings.TryGetValue(key, out var cached)) return cached;
        var globalIndex = layer.StringOffset + localIndex;
        if (globalIndex >= view->StringCount) return string.Empty;
        var descriptor = view->Strings[globalIndex];
        if (descriptor.ByteOffset > view->StringByteCount
            || descriptor.ByteLength > view->StringByteCount - descriptor.ByteOffset)
        {
            return string.Empty;
        }
        var value = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(
            view->StringBytes + descriptor.ByteOffset,
            checked((int)descriptor.ByteLength)));
        if (s_strings.Count >= 16_384) s_strings.Clear();
        s_strings[key] = value;
        return value;
    }

    private static SKColor ParseColor(string value)
    {
        if (CssColorParser.TryParseColor(value, out var parsed))
        {
            return new SKColor(parsed.R, parsed.G, parsed.B, parsed.A);
        }

        return SKColors.Black;
    }

    private static SKBlendMode BlendMode(string value)
        => value switch
        {
            "copy" => SKBlendMode.Src,
            "destination-over" => SKBlendMode.DstOver,
            "source-in" => SKBlendMode.SrcIn,
            "destination-in" => SKBlendMode.DstIn,
            "source-out" => SKBlendMode.SrcOut,
            "destination-out" => SKBlendMode.DstOut,
            "source-atop" => SKBlendMode.SrcATop,
            "destination-atop" => SKBlendMode.DstATop,
            "xor" => SKBlendMode.Xor,
            "lighter" => SKBlendMode.Plus,
            "multiply" => SKBlendMode.Multiply,
            "screen" => SKBlendMode.Screen,
            "overlay" => SKBlendMode.Overlay,
            "darken" => SKBlendMode.Darken,
            "lighten" => SKBlendMode.Lighten,
            "color-dodge" => SKBlendMode.ColorDodge,
            "color-burn" => SKBlendMode.ColorBurn,
            "hard-light" => SKBlendMode.HardLight,
            "soft-light" => SKBlendMode.SoftLight,
            "difference" => SKBlendMode.Difference,
            "exclusion" => SKBlendMode.Exclusion,
            "hue" => SKBlendMode.Hue,
            "saturation" => SKBlendMode.Saturation,
            "color" => SKBlendMode.Color,
            "luminosity" => SKBlendMode.Luminosity,
            _ => SKBlendMode.SrcOver
        };

    private static SKColor Rgba(uint rgba)
        => new(
            (byte)(rgba >> 24),
            (byte)(rgba >> 16),
            (byte)(rgba >> 8),
            (byte)rgba);

    internal void Reset()
    {
        s_domBackdropPicture?.Dispose();
        s_domBackdropPicture = null;
        s_domOverlayPicture?.Dispose();
        s_domOverlayPicture = null;
        s_domCommandCount = 0;
        foreach (var layer in s_layers.Values) layer.Dispose();
        s_layers.Clear();
        s_orderedLayers.Clear();
        foreach (var typeface in s_typefaces.Values) typeface.Dispose();
        s_typefaces.Clear();
        foreach (var svg in s_svgPictures.Values) svg.Dispose();
        s_svgPictures.Clear();
        s_strings.Clear();
        s_revision = 0;
        s_totalCommandCount = 0;
    }

    private void RebuildLayerOrder()
    {
        s_orderedLayers.Clear();
        s_orderedLayers.AddRange(s_layers.Values);
        s_orderedLayers.Sort(static (left, right) =>
        {
            var zOrder = left.ZOrder.CompareTo(right.ZOrder);
            return zOrder != 0
                ? zOrder
                : left.NodeId.CompareTo(right.NodeId);
        });
    }

    private bool ReplaceOrderedLayer(
        RetainedLayer previous,
        RetainedLayer replacement)
    {
        for (var index = 0; index < s_orderedLayers.Count; index++)
        {
            if (!ReferenceEquals(s_orderedLayers[index], previous))
            {
                continue;
            }
            s_orderedLayers[index] = replacement;
            return true;
        }
        return false;
    }

    private readonly record struct StringKey(uint NodeId, ulong Generation, uint Index);

    private readonly record struct CanvasAffine(
        double A,
        double B,
        double C,
        double D,
        double E,
        double F)
    {
        public static CanvasAffine Identity => new(1, 0, 0, 1, 0, 0);

        public static CanvasAffine From(in NativeCanvasCommand command)
            => new(command.V0, command.V1, command.V2, command.V3, command.V4, command.V5);

        public CanvasAffine Multiply(in CanvasAffine value)
            => new(
                A * value.A + C * value.B,
                B * value.A + D * value.B,
                A * value.C + C * value.D,
                B * value.C + D * value.D,
                A * value.E + C * value.F + E,
                B * value.E + D * value.F + F);

        public (double X, double Y) Map(double x, double y)
            => (A * x + C * y + E, B * x + D * y + F);
    }

    private sealed record RetainedLayer(
        uint NodeId,
        ulong Generation,
        uint ZOrder,
        float X,
        float Y,
        float Width,
        float Height,
        uint BitmapWidth,
        uint BitmapHeight,
        uint CommandCount,
        bool RequiresIsolation,
        SKPicture Picture) : IDisposable
    {
        public void Dispose() => Picture.Dispose();
    }

    private struct CanvasState
    {
        public string FillStyle;
        public string StrokeStyle;
        public string LineCap;
        public string LineJoin;
        public string Font;
        public string TextAlign;
        public string TextBaseline;
        public string ImageSmoothingQuality;
        public string Composite;
        public string ShadowColor;
        public double LineWidth;
        public double MiterLimit;
        public double GlobalAlpha;
        public double LineDashOffset;
        public double[] LineDash;
        public double ShadowBlur;
        public double ShadowOffsetX;
        public double ShadowOffsetY;
        public bool ImageSmoothingEnabled;

        public static CanvasState Default => new()
        {
            FillStyle = "#000000",
            StrokeStyle = "#000000",
            LineCap = "butt",
            LineJoin = "miter",
            Font = "10px sans-serif",
            TextAlign = "start",
            TextBaseline = "alphabetic",
            ImageSmoothingQuality = "low",
            Composite = "source-over",
            ShadowColor = "rgba(0, 0, 0, 0)",
            LineWidth = 1,
            MiterLimit = 10,
            GlobalAlpha = 1,
            LineDash = [],
            ImageSmoothingEnabled = true
        };
    }
}

public readonly record struct NativeRendererMemoryMetrics(
    int RetainedLayerCount,
    long RetainedCommandCount,
    ulong LogicalBitmapBytes,
    int IsolationLayerCount,
    ulong IsolationLogicalBitmapBytes,
    uint DomCommandCount,
    int StringCount,
    ulong StringBytes,
    int TypefaceCount,
    int SvgPictureCount,
    int ProcessSvgPictureCount,
    int ProcessSvgPictureReferenceCount,
    long ProcessSvgPictureMemoryHits);

internal sealed class SharedSvgPictureLease : IDisposable
{
    private SharedSvgPictureCache.Entry? _entry;

    internal SharedSvgPictureLease(SharedSvgPictureCache.Entry entry)
    {
        _entry = entry;
    }

    internal string Markup
        => _entry?.Markup
            ?? throw new ObjectDisposedException(nameof(SharedSvgPictureLease));

    internal SKPicture Picture
        => _entry?.Picture
            ?? throw new ObjectDisposedException(nameof(SharedSvgPictureLease));

    public void Dispose()
    {
        var entry = Interlocked.Exchange(ref _entry, null);
        if (entry is not null)
        {
            SharedSvgPictureCache.Release(entry);
        }
    }
}

internal static class SharedSvgPictureCache
{
    internal sealed class Entry(string markup, SKSvg svg)
    {
        internal string Markup { get; } = markup;
        internal SKSvg Svg { get; } = svg;
        internal SKPicture Picture { get; } =
            svg.Picture ?? throw new InvalidOperationException("SVG has no picture.");
        internal int References { get; set; } = 1;
    }

    private static readonly object Gate = new();
    private static readonly Dictionary<string, Entry> Entries =
        new(StringComparer.Ordinal);
    private static long s_memoryHits;

    internal static int EntryCount
    {
        get
        {
            lock (Gate) return Entries.Count;
        }
    }

    internal static int ReferenceCount
    {
        get
        {
            lock (Gate) return Entries.Values.Sum(static entry => entry.References);
        }
    }

    internal static long MemoryHitCount => Interlocked.Read(ref s_memoryHits);

    internal static SharedSvgPictureLease? Acquire(string markup)
    {
        lock (Gate)
        {
            if (Entries.TryGetValue(markup, out var known))
            {
                known.References++;
                Interlocked.Increment(ref s_memoryHits);
                return new SharedSvgPictureLease(known);
            }

            var svg = new SKSvg();
            try
            {
                if (svg.FromSvg(markup) is null || svg.Picture is null)
                {
                    svg.Dispose();
                    return null;
                }
                var entry = new Entry(markup, svg);
                Entries.Add(entry.Markup, entry);
                return new SharedSvgPictureLease(entry);
            }
            catch
            {
                svg.Dispose();
                return null;
            }
        }
    }

    internal static void Release(Entry entry)
    {
        lock (Gate)
        {
            if (--entry.References != 0)
            {
                return;
            }
            Entries.Remove(entry.Markup);
            entry.Svg.Dispose();
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct InputEvent
{
    public uint Kind;
    public uint Flags;
    public ulong Sequence;
    public double X;
    public double Y;
    public double DeltaX;
    public double DeltaY;
}

internal enum NativePreferredColorScheme : uint
{
    Light = 0,
    Dark = 1
}

internal static class NativeFrameInput
{
    private const uint Frame = 5;

    public static void Submit(IntPtr engine, double timestampMilliseconds)
    {
        if (engine == IntPtr.Zero) return;
        var input = new InputEvent
        {
            Kind = Frame,
            X = timestampMilliseconds
        };
        NativeWebSceneApi.EngineEnqueue(engine, in input);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct SceneHeader
{
    public ulong Revision;
    public ulong BaseRevision;
    public ulong ConsumedInputSequence;
    public float ViewportWidth;
    public float ViewportHeight;
    public uint CommandCount;
    public uint CanvasLayerCount;
    public uint DamageRectCount;
    public uint Flags;
    public ulong ContentHash;
}

[StructLayout(LayoutKind.Sequential)]
internal struct SceneCommand
{
    public uint Kind;
    public uint Flags;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint Rgba;
    public uint NodeId;
    public float RadiusTopLeft;
    public float RadiusTopRight;
    public float RadiusBottomRight;
    public float RadiusBottomLeft;
    public float StrokeWidth;
}

[StructLayout(LayoutKind.Sequential)]
public struct CanvasLayout
{
    public uint NodeId;
    public uint Flags;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint BitmapWidth;
    public uint BitmapHeight;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCanvasLayer
{
    public uint NodeId;
    public uint Flags;
    public uint CommandOffset;
    public uint CommandCount;
    public uint StringOffset;
    public uint StringCount;
    public uint Reserved;
    public float X;
    public float Y;
    public float Width;
    public float Height;
    public uint BitmapWidth;
    public uint BitmapHeight;
    public ulong Generation;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCanvasCommand
{
    public uint Kind;
    public uint Flags;
    public uint ResourceId;
    public uint Reserved;
    public double V0;
    public double V1;
    public double V2;
    public double V3;
    public double V4;
    public double V5;
    public double V6;
    public double V7;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneString
{
    public uint ByteOffset;
    public uint ByteLength;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDamageRect
{
    public float X;
    public float Y;
    public float Width;
    public float Height;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSceneView
{
    public uint StructSize;
    public uint AbiVersion;
    public SceneHeader Header;
    public SceneCommand* Commands;
    public NativeCanvasLayer* CanvasLayers;
    public NativeCanvasCommand* CanvasCommands;
    public NativeSceneString* Strings;
    public byte* StringBytes;
    public NativeDamageRect* DamageRects;
    public void* LeaseToken;
    public uint CanvasCommandCount;
    public uint StringCount;
    public uint StringByteCount;
    public uint Reserved;
}

public enum NativeInteropValueKind : uint
{
    Undefined = 0,
    Null = 1,
    Boolean = 2,
    Number = 3,
    String = 4,
    Array = 5,
    Object = 6,
    Handle = 7
}

internal enum NativeInteropResultStatus : uint
{
    Succeeded = 0,
    JavaScriptError = 1,
    Cancelled = 2,
    InvalidRequest = 3
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteropValueData
{
    public NativeInteropValueKind Kind;
    public uint Flags;
    public uint Offset;
    public uint Length;
    public ulong Payload;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInteropEdgeData
{
    public uint NameOffset;
    public uint NameLength;
    public uint ValueIndex;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropEvaluateRequest
{
    public uint StructSize;
    public uint Version;
    public byte* Source;
    public nuint SourceLength;
    public byte* DocumentName;
    public nuint DocumentNameLength;
    public uint Flags;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropInvokeRequest
{
    public uint StructSize;
    public uint Version;
    public JavaScriptBinaryOperation Operation;
    public JavaScriptBinaryCallFlags Flags;
    public ulong TargetHandle;
    public byte* GlobalName;
    public nuint GlobalNameLength;
    public byte* MemberName;
    public nuint MemberNameLength;
    public JavaScriptBinaryValueData* Values;
    public nuint ValueCount;
    public JavaScriptBinaryEdgeData* Edges;
    public nuint EdgeCount;
    public byte* Utf8Bytes;
    public nuint Utf8ByteCount;
    public uint ArgumentsRoot;
    public JavaScriptBinaryResultMode ResultMode;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropResultView
{
    public uint StructSize;
    public uint Version;
    public NativeInteropResultStatus Status;
    public uint Flags;
    public ulong OperationId;
    public NativeInteropValueData* Values;
    public NativeInteropEdgeData* Edges;
    public byte* Utf8Bytes;
    public byte* ErrorBytes;
    public ulong LeaseId;
    public uint ValueCount;
    public uint EdgeCount;
    public uint Utf8ByteCount;
    public uint ErrorByteCount;
    public uint RootValueIndex;
    public uint PooledCapacity;
    public uint Reserved0;
    public uint Reserved1;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropCallbackView
{
    public uint StructSize;
    public uint Version;
    public ulong CallId;
    public ulong TargetId;
    public uint MethodId;
    public JavaScriptCallbackReturnKind ReturnKind;
    public JavaScriptBinaryValueData* Values;
    public JavaScriptBinaryEdgeData* Edges;
    public byte* Utf8Bytes;
    public ulong LeaseId;
    public uint ValueCount;
    public uint EdgeCount;
    public uint Utf8ByteCount;
    public uint ArgumentsRoot;
    public uint PooledCapacity;
    public uint Reserved0;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInteropCallbackCompletion
{
    public uint StructSize;
    public uint Version;
    public ulong CallId;
    public uint Succeeded;
    public uint Reserved;
    public JavaScriptBinaryValueData* Values;
    public nuint ValueCount;
    public JavaScriptBinaryEdgeData* Edges;
    public nuint EdgeCount;
    public byte* Utf8Bytes;
    public nuint Utf8ByteCount;
    public byte* ErrorBytes;
    public nuint ErrorByteCount;
    public uint RootValueIndex;
    public uint Reserved1;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeInteropPoolMetrics
{
    public uint StructSize;
    public uint Version;
    public ulong OutstandingResults;
    public ulong PooledBytes;
    public ulong PoolHits;
    public ulong PoolMisses;
    public ulong OversizeAllocations;
    public ulong HighWaterOutstandingResults;
    public ulong PooledRequestRecords;
    public ulong RequestPoolHits;
    public ulong RequestPoolMisses;
    public ulong RequestOversizeAllocations;
    public ulong ActiveOperationSlots;
    public ulong AvailableOperationSlots;
    public ulong OperationSlotHighWater;
    public ulong PooledResultBytes4K;
    public ulong PooledResultBytes16K;
    public ulong PooledResultBytes64K;
    public ulong PooledResultBytes256K;
    public ulong PooledResultBytes1M;
    public ulong TakenResultLeases;
    public ulong OperationResultLeases;
    public ulong QueuedCallbacks;
    public ulong TakenCallbackLeases;
    public ulong PendingCallbackPromises;
    public ulong CallbackQueueHighWater;
}

[StructLayout(LayoutKind.Sequential)]
public struct EngineMetrics
{
    public ulong EnqueuedInputs;
    public ulong DroppedInputs;
    public ulong ConsumedInputs;
    public ulong PublishedScenes;
    public ulong AcquiredScenes;
    public ulong ExecutedScripts;
    public ulong ScriptErrors;
    public ulong DomNodes;
    public ulong LayoutPasses;
    public ulong IframeNodes;
    public ulong IframeHtmlBytes;
    public ulong FrameScriptsExecuted;
    public ulong FrameScriptErrors;
    public ulong CanvasNodes;
    public ulong ComponentReady;
    public ulong CompilationRequests;
    public ulong CompilationMemoryHits;
    public ulong CompilationPersistentHits;
    public ulong CompilationPersistentMisses;
    public ulong CompilationCacheRejections;
    public ulong CompilationCacheBytesRead;
    public ulong CompilationCacheBytesWritten;
    public ulong CompilationTimeNanoseconds;
    public ulong InputEventsDispatched;
    public ulong InputCallbacksInvoked;
    public ulong BusiestCanvasWidthMilli;
    public ulong BusiestCanvasHeightMilli;
    public ulong CoalescedResizeInputs;
    public ulong AppliedResizeInputs;
    public ulong LastResizeDispatchNanoseconds;
    public ulong LastScenePublicationNanoseconds;
    public ulong LastResizeOuterListenersNanoseconds;
    public ulong LastResizeFrameListenersNanoseconds;
    public ulong LastResizeLayoutNanoseconds;
    public ulong LastResizeObserversNanoseconds;
    public ulong CoalescedPointerMoveInputs;
    public ulong CoalescedWheelInputs;
    public ulong AppliedPointerMoveInputs;
    public ulong AppliedWheelInputs;
    public ulong AppliedAnimationFrames;
    public ulong CoalescedAnimationFrames;
    public ulong LastAnimationAdvanceNanoseconds;
    public ulong LastLayoutNanoseconds;
    public ulong LastSceneBuildNanoseconds;
    public ulong MaximumScenePublicationNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct InputDispatchMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong LastDispatchSequence;
    public ulong DispatchedInputs;
    public ulong TotalDispatchNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct AnimationFrameMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong DispatchedFrames;
    public ulong TotalDispatchNanoseconds;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong LastTimestampMicroseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct SceneFlowMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong PublicationAttempts;
    public ulong BlockedPublications;
    public ulong AcknowledgedScenes;
    public ulong TotalAcknowledgementNanoseconds;
    public ulong LastAcknowledgementNanoseconds;
    public ulong MaximumAcknowledgementNanoseconds;
    public ulong AcknowledgedRevision;
}

[StructLayout(LayoutKind.Sequential)]
public struct ResizeFrameMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong SubmittedPairs;
    public ulong AppliedPairs;
    public ulong PublishedPairs;
    public ulong TotalQueueNanoseconds;
    public ulong LastQueueNanoseconds;
    public ulong MaximumQueueNanoseconds;
    public ulong TotalDispatchNanoseconds;
    public ulong LastDispatchNanoseconds;
    public ulong MaximumDispatchNanoseconds;
    public ulong AnimationFrameCallbacks;
    public ulong TotalAnimationFrameBatchNanoseconds;
    public ulong LastAnimationFrameBatchNanoseconds;
    public ulong MaximumAnimationFrameBatchNanoseconds;
    public ulong TotalToPublicationNanoseconds;
    public ulong LastToPublicationNanoseconds;
    public ulong MaximumToPublicationNanoseconds;
}

[StructLayout(LayoutKind.Sequential)]
public struct ResourceCacheMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong Requests;
    public ulong Hits;
    public ulong Misses;
    public ulong Rejections;
    public ulong BytesRead;
    public ulong BytesWritten;
}

[StructLayout(LayoutKind.Sequential)]
public struct ProcessCacheMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong CompilationMemoryHits;
    public ulong CompilationLeaders;
    public ulong CompilationWaiters;
    public ulong CompilationSharedBytes;
    public ulong ResourceMemoryHits;
    public ulong ResourceLoadLeaders;
    public ulong ResourceLoadWaiters;
    public ulong ResourceSharedBytes;
    public ulong ScriptSourceMemoryHits;
    public ulong ScriptSourceSharedBytes;
    public ulong SharedIsolateSlot;
    public ulong SharedIsolateActiveContexts;
    public ulong SharedIsolatePeakContexts;
}

[StructLayout(LayoutKind.Sequential)]
public struct EngineMemoryMetrics
{
    public uint StructSize;
    public uint Reserved;
    public ulong V8TotalHeapBytes;
    public ulong V8UsedHeapBytes;
    public ulong V8ExecutableHeapBytes;
    public ulong V8PhysicalHeapBytes;
    public ulong V8ExternalBytes;
    public ulong V8MallocedBytes;
    public ulong V8PeakMallocedBytes;
    public ulong LatestSceneBytes;
    public ulong ProcessCompilationCacheBytes;
    public ulong ProcessResourceCacheBytes;
    public ulong V8CodeAndMetadataBytes;
    public ulong V8BytecodeAndMetadataBytes;
    public ulong V8ExternalScriptSourceBytes;
    public ulong NativeDomNodeCount;
    public ulong NativeDomNodeSizeBytes;
    public ulong NativeDomInlineBytes;
    public ulong NativeDomPseudoStorageBytes;
    public ulong NativeDomCanvasNodeCount;
    public ulong NativeDomCanvasStorageBytes;
    public ulong NativeDomAnimationCount;
    public ulong NativeDomAnimationStorageBytes;
    public ulong NativeDomCustomPropertyNodeCount;
    public ulong NativeDomCustomPropertyEntryCount;
    public ulong NativeDomCustomPropertyStorageBytes;
    public ulong NativeDomBackgroundImageCount;
    public ulong NativeDomBackgroundImageStorageBytes;
    public ulong NativeDomGridCount;
    public ulong NativeDomGridStorageBytes;
    public ulong NativeDomAuthoredStyleNodeCount;
    public ulong NativeDomAuthoredStyleEntryCount;
    public ulong NativeDomAuthoredStyleStorageBytes;
    public ulong NativeCssRuleCount;
    public ulong NativeCssRuleStorageBytes;
    public ulong NativeCssIndexStorageBytes;
    public ulong ProcessSharedCssRuleCount;
    public ulong ProcessSharedCssRuleStorageBytes;
    public ulong LowMemoryNotifications;
    public ulong NativeDomAttributeNodeCount;
    public ulong NativeDomAttributeEntryCount;
    public ulong NativeDomAttributeStorageBytes;
    public ulong NativeWrapperHandleCount;
    public ulong NativeWrapperStorageBytes;
    public ulong NativeTextMeasurementCacheEntryCount;
    public ulong NativeTextMeasurementCacheStorageBytes;
    public ulong ProcessCompilationMappedCacheBytes;
    public ulong ProcessResourceMappedCacheBytes;
    public ulong NativeDomTextualStyleCount;
    public ulong NativeDomTextualStyleStorageBytes;
    public ulong NativeDomNodePoolReservedBytes;
    public ulong NativeDomNodePoolPeakBytes;
    public ulong NativeDomTableLayoutCount;
    public ulong NativeDomTableLayoutStorageBytes;
    public ulong NativeDomFormControlCount;
    public ulong NativeDomFormControlStorageBytes;
    public ulong HiddenLowMemoryNotifications;
    public ulong NativeEventListenerCount;
    public ulong NativeEventListenerStorageBytes;
    public ulong V8YoungSpaceUsedBytes;
    public ulong V8YoungSpacePhysicalBytes;
    public ulong V8OldSpaceUsedBytes;
    public ulong V8OldSpacePhysicalBytes;
    public ulong V8CodeSpaceUsedBytes;
    public ulong V8CodeSpacePhysicalBytes;
    public ulong V8MapSpaceUsedBytes;
    public ulong V8MapSpacePhysicalBytes;
    public ulong V8LargeObjectSpaceUsedBytes;
    public ulong V8LargeObjectSpacePhysicalBytes;
    public ulong V8ReadOnlySpaceUsedBytes;
    public ulong V8ReadOnlySpacePhysicalBytes;
    public ulong V8SharedSpaceUsedBytes;
    public ulong V8SharedSpacePhysicalBytes;
    public ulong V8TrustedSpaceUsedBytes;
    public ulong V8TrustedSpacePhysicalBytes;
    public ulong PendingSceneCount;
    public ulong PendingSceneBytes;
}

[StructLayout(LayoutKind.Sequential)]
internal struct EngineOptions
{
    public uint StructSize;
    public uint SimulatedChartCommandCount;
    public IntPtr CompilationCacheDirectory;
    public nuint CompilationCacheDirectoryLength;
    public IntPtr ResourceLoadCallback;
    public IntPtr ResourceLoadUserData;
    public IntPtr ScenePublishedCallback;
    public IntPtr ScenePublishedUserData;
    public IntPtr TextMeasureCallback;
    public IntPtr TextMeasureUserData;
    public IntPtr HostRequestAvailableCallback;
    public IntPtr HostRequestAvailableUserData;
    public IntPtr InteropCallbackAvailableCallback;
    public IntPtr InteropCallbackAvailableUserData;
    public IntPtr AnimationFrameRequestedCallback;
    public IntPtr AnimationFrameRequestedUserData;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeTextMetrics
{
    public uint StructSize;
    public float AdvanceWidth;
    public float Ascent;
    public float Descent;
    public float Leading;
}

public static class NativeTextShaping
{
    internal const uint TabularNumerals = 1u << 0;
    private static readonly ConcurrentDictionary<string, SKTypeface> Typefaces =
        new(StringComparer.Ordinal);
    private static readonly ConcurrentDictionary<string, SKTypeface> WebTypefaces =
        new(StringComparer.OrdinalIgnoreCase);

    public static bool RegisterWebTypeface(string family, ReadOnlySpan<byte> data)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(family);
        if (data.IsEmpty) return false;

        using var fontData = SKData.CreateCopy(data);
        var typeface = SKTypeface.FromData(fontData);
        if (typeface is null) return false;

        var normalizedFamily = family.Trim().Trim('"', '\'');
        if (WebTypefaces.TryAdd(normalizedFamily, typeface)) return true;

        typeface.Dispose();
        return true;
    }

    public static SKTypeface ResolveTypeface(string familyList, int fontWeight)
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (WebTypefaces.TryGetValue(family, out var webTypeface))
            {
                return webTypeface;
            }
        }

        var requestedWeight = Math.Clamp(fontWeight, 1, 1000);
        var key = $"{familyList}\u001f{requestedWeight}";
        return Typefaces.GetOrAdd(key, _ =>
        {
            foreach (var rawFamily in familyList.Split(
                         ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
            {
                var family = rawFamily.Trim('"', '\'');
                if (family is "-apple-system" or "BlinkMacSystemFont" or "system-ui"
                    or "sans-serif")
                {
                    family = OperatingSystem.IsMacOS() ? ".AppleSystemUIFont" : "Arial";
                }
                else if (family == "serif") family = "Times New Roman";
                else if (family == "monospace") family = OperatingSystem.IsMacOS() ? "Menlo" : "Consolas";

                var candidate = SKTypeface.FromFamilyName(
                    family,
                    requestedWeight,
                    (int)SKFontStyleWidth.Normal,
                    SKFontStyleSlant.Upright);
                if (candidate is not null
                    && (string.Equals(candidate.FamilyName, family, StringComparison.OrdinalIgnoreCase)
                        || rawFamily is "-apple-system" or "BlinkMacSystemFont" or "system-ui"
                            or "sans-serif" or "serif" or "monospace"))
                {
                    return candidate;
                }
                candidate?.Dispose();
            }
            return SKTypeface.Default;
        });
    }

    internal static float ResolveWidthScale(string familyList, float fontSize, int fontWeight)
    {
        if (!OperatingSystem.IsMacOS() || !UsesMacSystemUiMetrics(familyList))
        {
            return 1f;
        }

        // Keep the native Skia compositor in lockstep with the managed
        // Avalonia DOM/Canvas calibration for Blink's macOS system UI face.
        var size = Math.Clamp(fontSize, 8f, 24f);
        var weight = Math.Clamp(fontWeight, 100, 900);
        return Math.Clamp(
            1.0222f + (16f - size) * 0.0062f - (weight - 400f) * 0.000133f,
            0.96f,
            1.08f);
    }

    internal static float MeasureShapedWidth(
        SKShaper shaper,
        string text,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f)
    {
        if (string.IsNullOrEmpty(text))
        {
            return 0;
        }
        if ((featureFlags & TabularNumerals) == 0)
        {
            return shaper.Shape(text, paint).Width;
        }

        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        var width = 0f;
        for (var index = 0; index < text.Length;)
        {
            if (text[index] is >= '0' and <= '9')
            {
                width += tabularDigitWidth;
                index++;
                continue;
            }
            var start = index++;
            while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
            width += shaper.Shape(text[start..index], paint).Width;
        }
        return width;
    }

    internal static void DrawShapedText(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint,
        uint featureFlags,
        float tabularDigitScale = 1f)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }
        if ((featureFlags & TabularNumerals) == 0)
        {
            DrawShapedTextRun(canvas, shaper, text, x, baseline, paint);
            return;
        }

        var tabularDigitWidth = shaper.Shape("0", paint).Width * tabularDigitScale;
        var cursor = x;
        for (var index = 0; index < text.Length;)
        {
            if (text[index] is >= '0' and <= '9')
            {
                var digit = text[index].ToString();
                DrawShapedTextRun(canvas, shaper, digit, cursor, baseline, paint);
                cursor += tabularDigitWidth;
                index++;
                continue;
            }
            var start = index++;
            while (index < text.Length && text[index] is not (>= '0' and <= '9')) index++;
            var segment = text[start..index];
            DrawShapedTextRun(canvas, shaper, segment, cursor, baseline, paint);
            cursor += shaper.Shape(segment, paint).Width;
        }
    }

    private static void DrawShapedTextRun(
        SKCanvas canvas,
        SKShaper shaper,
        string text,
        float x,
        float baseline,
        SKPaint paint)
    {
        var result = shaper.Shape(text, x, baseline, paint);
        if (result.Codepoints.Length == 0 || result.Points.Length == 0)
        {
            return;
        }

        using var font = paint.ToFont();
        font.Typeface = shaper.Typeface;
        using var builder = new SKTextBlobBuilder();
        var glyphCount = Math.Min(result.Codepoints.Length, result.Points.Length);
        var run = builder.AllocatePositionedRun(font, glyphCount);
        var glyphs = run.GetGlyphSpan();
        var positions = run.GetPositionSpan();
        for (var index = 0; index < glyphCount; index++)
        {
            glyphs[index] = (ushort)result.Codepoints[index];
            positions[index] = result.Points[index];
        }

        using var textBlob = builder.Build();
        if (textBlob is null)
        {
            return;
        }

        var xOffset = paint.TextAlign switch
        {
            SKTextAlign.Center => -result.Width * 0.5f,
            SKTextAlign.Right => -result.Width,
            _ => 0f,
        };
        canvas.DrawText(textBlob, xOffset, 0, paint);
    }

    internal static uint ResolveFeatureFlags(
        string text,
        string familyList,
        uint authoredFeatureFlags)
    {
        if ((authoredFeatureFlags & TabularNumerals) != 0)
        {
            return authoredFeatureFlags;
        }
        if (!OperatingSystem.IsMacOS() || !UsesMacSystemUiMetrics(familyList))
        {
            return authoredFeatureFlags;
        }

        var sawDigit = false;
        foreach (var character in text)
        {
            if (character is >= '0' and <= '9')
            {
                sawDigit = true;
                continue;
            }
            if (character is ' ' or '.' or ',' or '+' or '-' or '\u2212'
                or '(' or ')' or '/' or '%' or ':')
            {
                continue;
            }
            return authoredFeatureFlags;
        }
        return sawDigit ? authoredFeatureFlags | TabularNumerals : authoredFeatureFlags;
    }

    internal static float ResolveTabularDigitScale(string familyList)
        => OperatingSystem.IsMacOS() && UsesMacSystemUiMetrics(familyList)
            ? 1.014f
            : 1f;

    private static bool UsesMacSystemUiMetrics(string familyList)
    {
        foreach (var rawFamily in familyList.Split(
                     ',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries))
        {
            var family = rawFamily.Trim('"', '\'');
            if (WebTypefaces.ContainsKey(family))
            {
                return false;
            }
            if (string.Equals(family, "-apple-system", StringComparison.OrdinalIgnoreCase)
                || string.Equals(family, "BlinkMacSystemFont", StringComparison.OrdinalIgnoreCase)
                || string.Equals(family, "system-ui", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            if (family.Equals("sans-serif", StringComparison.OrdinalIgnoreCase)
                || family.Equals("serif", StringComparison.OrdinalIgnoreCase)
                || family.Equals("monospace", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            using var installed = SKTypeface.FromFamilyName(family);
            if (installed is not null
                && string.Equals(installed.FamilyName, family, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
        }
        return false;
    }

    public static NativeTextMetrics Measure(
        string text,
        string familyList,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        uint featureFlags = 0)
    {
        var typeface = ResolveTypeface(familyList, fontWeight);
        using var paint = new SKPaint
        {
            IsAntialias = true,
            TextSize = fontSize,
            Typeface = typeface
        };
        using var shaper = new SKShaper(typeface);
        featureFlags = ResolveFeatureFlags(text, familyList, featureFlags);
        var shapedWidth = MeasureShapedWidth(
            shaper,
            text,
            paint,
            featureFlags,
            ResolveTabularDigitScale(familyList));
        paint.GetFontMetrics(out var fontMetrics);
        var graphemes = string.IsNullOrEmpty(text)
            ? 0
            : StringInfo.ParseCombiningCharacters(text).Length;
        var spaces = text.Count(character => character == ' ');
        return new NativeTextMetrics
        {
            StructSize = (uint)Marshal.SizeOf<NativeTextMetrics>(),
            AdvanceWidth = shapedWidth
                * ((featureFlags & TabularNumerals) != 0
                    ? ResolveWidthScale(familyList, fontSize, fontWeight)
                    : 1f)
                + Math.Max(0, graphemes - 1) * letterSpacing
                + spaces * wordSpacing,
            Ascent = -fontMetrics.Ascent,
            Descent = fontMetrics.Descent,
            Leading = fontMetrics.Leading
        };
    }
}

public static unsafe class NativeWebSceneApi
{

    private const string LibraryName = "webscene_native_engine";
    private static readonly object LibraryPathGate = new();
    private static readonly ConcurrentDictionary<IntPtr, GCHandle> EngineResourceBridges = new();
    private static readonly ResourceLoadCallback ResourceLoad = LoadResource;
    private static readonly IntPtr ResourceLoadAddress = Marshal.GetFunctionPointerForDelegate(ResourceLoad);
    private static readonly ScenePublishedCallback ScenePublished = NotifyScenePublished;
    private static readonly IntPtr ScenePublishedAddress =
        Marshal.GetFunctionPointerForDelegate(ScenePublished);
    private static readonly TextMeasureCallback TextMeasure = MeasureText;
    private static readonly IntPtr TextMeasureAddress =
        Marshal.GetFunctionPointerForDelegate(TextMeasure);
    private static readonly HostRequestAvailableCallback HostRequestAvailable =
        NotifyHostRequestAvailable;
    private static readonly IntPtr HostRequestAvailableAddress =
        Marshal.GetFunctionPointerForDelegate(HostRequestAvailable);
    private static readonly InteropCallbackAvailableCallback InteropCallbackAvailable =
        NotifyInteropCallbackAvailable;
    private static readonly IntPtr InteropCallbackAvailableAddress =
        Marshal.GetFunctionPointerForDelegate(InteropCallbackAvailable);
    private static readonly AnimationFrameRequestedCallback AnimationFrameRequested =
        NotifyAnimationFrameRequested;
    private static readonly IntPtr AnimationFrameRequestedAddress =
        Marshal.GetFunctionPointerForDelegate(AnimationFrameRequested);
    private static string? _libraryPath;

    static NativeWebSceneApi()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeWebSceneApi).Assembly, ResolveLibrary);
    }

    public static void ConfigureLibraryPath(string libraryPath)
    {
        var fullPath = Path.GetFullPath(libraryPath);
        lock (LibraryPathGate)
        {
            if (_libraryPath is not null
                && !string.Equals(_libraryPath, fullPath, StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    $"The native WebScene runtime is already bound to '{_libraryPath}' and cannot be rebound to '{fullPath}' in the same process.");
            }
            _libraryPath = fullPath;
        }
    }

    private static IntPtr ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? path)
    {
        string? configuredPath;
        lock (LibraryPathGate)
        {
            configuredPath = _libraryPath;
        }
        if (libraryName == LibraryName && !string.IsNullOrWhiteSpace(configuredPath))
        {
            return NativeLibrary.Load(configuredPath);
        }
        return IntPtr.Zero;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_prewarm")]
    public static extern byte EnginePrewarm();

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create")]
    private static extern IntPtr EngineCreateDefault(uint simulatedChartCommandCount);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_create_with_options")]
    private static extern IntPtr EngineCreateWithOptions(in EngineOptions options);

    public static IntPtr EngineCreate(
        uint simulatedChartCommandCount,
        string? compilationCacheDirectory,
        IWebSceneResourceLoader resourceLoader,
        Action<NativeScenePublished> scenePublished,
        Action? hostRequestAvailable = null,
        Action? interopCallbackAvailable = null,
        Action? animationFrameRequested = null)
    {
        ArgumentNullException.ThrowIfNull(resourceLoader);
        ArgumentNullException.ThrowIfNull(scenePublished);
        var directoryBytes = string.IsNullOrWhiteSpace(compilationCacheDirectory)
            ? []
            : Encoding.UTF8.GetBytes(compilationCacheDirectory);
        var bridgeHandle = GCHandle.Alloc(
            new ResourceBridge(
                resourceLoader,
                scenePublished,
                hostRequestAvailable,
                interopCallbackAvailable,
                animationFrameRequested));
        try
        {
            fixed (byte* directory = directoryBytes)
            {
                var options = new EngineOptions
                {
                    StructSize = (uint)Marshal.SizeOf<EngineOptions>(),
                    SimulatedChartCommandCount = simulatedChartCommandCount,
                    CompilationCacheDirectory = directoryBytes.Length == 0 ? IntPtr.Zero : (IntPtr)directory,
                    CompilationCacheDirectoryLength = (nuint)directoryBytes.Length,
                    ResourceLoadCallback = ResourceLoadAddress,
                    ResourceLoadUserData = GCHandle.ToIntPtr(bridgeHandle),
                    ScenePublishedCallback = ScenePublishedAddress,
                    ScenePublishedUserData = GCHandle.ToIntPtr(bridgeHandle),
                    TextMeasureCallback = TextMeasureAddress,
                    TextMeasureUserData = GCHandle.ToIntPtr(bridgeHandle),
                    HostRequestAvailableCallback = hostRequestAvailable is null
                        ? IntPtr.Zero
                        : HostRequestAvailableAddress,
                    HostRequestAvailableUserData = hostRequestAvailable is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle),
                    InteropCallbackAvailableCallback = interopCallbackAvailable is null
                        ? IntPtr.Zero
                        : InteropCallbackAvailableAddress,
                    InteropCallbackAvailableUserData = interopCallbackAvailable is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle),
                    AnimationFrameRequestedCallback = animationFrameRequested is null
                        ? IntPtr.Zero
                        : AnimationFrameRequestedAddress,
                    AnimationFrameRequestedUserData = animationFrameRequested is null
                        ? IntPtr.Zero
                        : GCHandle.ToIntPtr(bridgeHandle)
                };
                var engine = EngineCreateWithOptions(in options);
                if (engine == IntPtr.Zero) return IntPtr.Zero;
                EngineResourceBridges[engine] = bridgeHandle;
                bridgeHandle = default;
                return engine;
            }
        }
        finally
        {
            if (bridgeHandle.IsAllocated) bridgeHandle.Free();
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_destroy")]
    private static extern void EngineDestroyNative(IntPtr engine);

    public static void EngineDestroy(IntPtr engine)
    {
        EngineDestroyNative(engine);
        if (EngineResourceBridges.TryRemove(engine, out var bridge) && bridge.IsAllocated)
        {
            bridge.Free();
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_load_url")]
    private static extern byte EngineLoadUrl(IntPtr engine, byte[] url, nuint urlLength);

    public static bool TryLoadUrl(IntPtr engine, string url)
    {
        var bytes = Encoding.UTF8.GetBytes(url);
        return EngineLoadUrl(engine, bytes, (nuint)bytes.Length) != 0;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_resource_root")]
    private static extern byte EngineSetResourceRoot(
        IntPtr engine,
        byte[] resourceRoot,
        nuint resourceRootLength);

    public static bool TrySetResourceRoot(IntPtr engine, string resourceRoot)
    {
        var bytes = Encoding.UTF8.GetBytes(resourceRoot);
        return EngineSetResourceRoot(engine, bytes, (nuint)bytes.Length) != 0;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate nuint ResourceLoadCallback(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        IntPtr destination,
        nuint destinationCapacity);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ScenePublishedCallback(
        IntPtr userData,
        ulong revision,
        ulong consumedInputSequence,
        float viewportWidth,
        float viewportHeight);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte TextMeasureCallback(
        IntPtr userData,
        IntPtr text,
        nuint textLength,
        IntPtr fontFamily,
        nuint fontFamilyLength,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        ref NativeTextMetrics metrics);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void HostRequestAvailableCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void InteropCallbackAvailableCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void AnimationFrameRequestedCallback(IntPtr userData);

    private static void NotifyHostRequestAvailable(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyHostRequestAvailable();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native host request notification] {error}");
        }
    }

    private static void NotifyInteropCallbackAvailable(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyInteropCallbackAvailable();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native interop callback notification] {error}");
        }
    }

    private static void NotifyAnimationFrameRequested(IntPtr userData)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            bridge?.NotifyAnimationFrameRequested();
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"[WebScene native animation-frame notification] {error}");
        }
    }

    private static byte MeasureText(
        IntPtr userData,
        IntPtr text,
        nuint textLength,
        IntPtr fontFamily,
        nuint fontFamilyLength,
        float fontSize,
        int fontWeight,
        float letterSpacing,
        float wordSpacing,
        ref NativeTextMetrics metrics)
    {
        try
        {
            if (metrics.StructSize < Marshal.SizeOf<NativeTextMetrics>() || fontSize <= 0)
            {
                return 0;
            }
            var value = Marshal.PtrToStringUTF8(text, checked((int)textLength)) ?? string.Empty;
            var family = Marshal.PtrToStringUTF8(fontFamily, checked((int)fontFamilyLength))
                ?? "sans-serif";
            var measured = NativeTextShaping.Measure(
                value,
                family,
                fontSize,
                fontWeight,
                letterSpacing,
                wordSpacing);
            metrics.AdvanceWidth = measured.AdvanceWidth;
            metrics.Ascent = measured.Ascent;
            metrics.Descent = measured.Descent;
            metrics.Leading = measured.Leading;
            return 1;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native text shaping] {error}");
            return 0;
        }
    }

    private static void NotifyScenePublished(
        IntPtr userData,
        ulong revision,
        ulong consumedInputSequence,
        float viewportWidth,
        float viewportHeight)
    {
        try
        {
            ((ResourceBridge?)GCHandle.FromIntPtr(userData).Target)?.NotifyScenePublished(
                new NativeScenePublished(
                    revision,
                    consumedInputSequence,
                    viewportWidth,
                    viewportHeight));
        }
        catch (Exception error)
        {
            // Never allow a managed exception to unwind through the native
            // engine worker. The normal compositor loop remains a fallback.
            Console.Error.WriteLine($"[WebScene native scene publication] {error}");
        }
    }

    private static nuint LoadResource(
        IntPtr userData,
        uint kind,
        IntPtr url,
        nuint urlLength,
        IntPtr entityTag,
        nuint entityTagLength,
        long lastModifiedUnixSeconds,
        IntPtr destination,
        nuint destinationCapacity)
    {
        try
        {
            var bridge = (ResourceBridge?)GCHandle.FromIntPtr(userData).Target;
            var address = Marshal.PtrToStringUTF8(url, checked((int)urlLength));
            var validator = entityTagLength == 0
                ? null
                : Marshal.PtrToStringUTF8(entityTag, checked((int)entityTagLength));
            return bridge is null || string.IsNullOrWhiteSpace(address)
                ? 0
                : bridge.Copy(
                    kind,
                    address,
                    validator,
                    lastModifiedUnixSeconds,
                    destination,
                    destinationCapacity);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"[WebScene native resource loader] {error}");
            return 0;
        }
    }

    private sealed class ResourceBridge(
        IWebSceneResourceLoader loader,
        Action<NativeScenePublished> scenePublished,
        Action? hostRequestAvailable,
        Action? interopCallbackAvailable,
        Action? animationFrameRequested)
    {
        private readonly ConcurrentDictionary<string, byte[]> _pendingCopies = new(StringComparer.Ordinal);
#if !WEBSCENE_UNO
        private readonly ConcurrentDictionary<string, byte> _registeredFontSources =
            new(StringComparer.Ordinal);
#endif

        public void NotifyScenePublished(NativeScenePublished scene)
            => scenePublished(scene);

        public void NotifyHostRequestAvailable()
            => hostRequestAvailable?.Invoke();

        public void NotifyInteropCallbackAvailable()
            => interopCallbackAvailable?.Invoke();

        public void NotifyAnimationFrameRequested()
            => animationFrameRequested?.Invoke();

        public nuint Copy(
            uint kind,
            string address,
            string? entityTag,
            long lastModifiedUnixSeconds,
            IntPtr destination,
            nuint capacity)
        {
            var key = $"{kind}:{address}:{entityTag}:{lastModifiedUnixSeconds}";
            if (!_pendingCopies.TryGetValue(key, out var bytes))
            {
                var resourceKind = kind switch
                {
                    1 => WebSceneResourceKind.Script,
                    2 => WebSceneResourceKind.StyleSheet,
                    3 => WebSceneResourceKind.Image,
                    _ => WebSceneResourceKind.Markup
                };
                var request = new WebSceneResourceRequest(address, null, resourceKind)
                {
                    IfNoneMatch = entityTag,
                    IfModifiedSince = lastModifiedUnixSeconds > 0
                        ? DateTimeOffset.FromUnixTimeSeconds(lastModifiedUnixSeconds)
                        : null
                };
                var resource = loader.LoadText(request);
#if !WEBSCENE_UNO
                if (resourceKind == WebSceneResourceKind.StyleSheet
                    && loader is AvaloniaResourceLoader avaloniaLoader)
                {
                    RegisterWebFonts(resource.Content, address, avaloniaLoader);
                }
#endif
                var responseEntityTag = Encoding.UTF8.GetBytes(resource.EntityTag ?? entityTag ?? string.Empty);
                var content = resource.NotModified
                    ? []
                    : Encoding.UTF8.GetBytes(resource.Content);
                const int headerSize = 2 + sizeof(uint) + sizeof(long) + sizeof(long);
                bytes = new byte[headerSize + responseEntityTag.Length + content.Length];
                bytes[0] = resource.NotModified ? (byte)2 : (byte)1;
                bytes[1] = resource.IsCacheable ? (byte)1 : (byte)0;
                BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(2), (uint)responseEntityTag.Length);
                BinaryPrimitives.WriteInt64LittleEndian(
                    bytes.AsSpan(2 + sizeof(uint)),
                    (resource.LastModified ?? request.IfModifiedSince)?.ToUnixTimeSeconds() ?? 0);
                BinaryPrimitives.WriteInt64LittleEndian(
                    bytes.AsSpan(2 + sizeof(uint) + sizeof(long)),
                    resource.FreshUntil?.ToUnixTimeSeconds() ?? 0);
                responseEntityTag.CopyTo(bytes, headerSize);
                content.CopyTo(bytes, headerSize + responseEntityTag.Length);
                _pendingCopies[key] = bytes;
            }

            if (destination == IntPtr.Zero || capacity < (nuint)bytes.Length)
            {
                return (nuint)bytes.Length;
            }

            Marshal.Copy(bytes, 0, destination, bytes.Length);
            _pendingCopies.TryRemove(key, out _);
            return (nuint)bytes.Length;
        }

#if !WEBSCENE_UNO
        private void RegisterWebFonts(
            string css,
            string stylesheetAddress,
            AvaloniaResourceLoader avaloniaLoader)
        {
            foreach (var rule in CssFontFaceRules(css))
            {
                var family = CssDeclarationValue(rule, "font-family")
                    ?.Trim().Trim('"', '\'');
                var source = FirstCssUrl(CssDeclarationValue(rule, "src"));
                if (string.IsNullOrWhiteSpace(family)
                    || string.IsNullOrWhiteSpace(source))
                {
                    continue;
                }

                var sourceKey = $"{family}\u001f{stylesheetAddress}\u001f{source}";
                if (!_registeredFontSources.TryAdd(sourceKey, 0)) continue;
                try
                {
                    var resource = avaloniaLoader
                        .LoadBytesAsync(source, stylesheetAddress, CancellationToken.None)
                        .GetAwaiter()
                        .GetResult();
                    if (!NativeTextShaping.RegisterWebTypeface(family, resource.Content))
                    {
                        Console.Error.WriteLine(
                            $"[WebScene native web font] '{resource.DisplayName}' is not a supported font.");
                    }
                }
                catch (Exception error)
                {
                    Console.Error.WriteLine(
                        $"[WebScene native web font] Could not load '{source}' from "
                        + $"'{stylesheetAddress}': {error.Message}");
                }
            }
        }

        private static IEnumerable<string> CssFontFaceRules(string css)
        {
            var cursor = 0;
            while (cursor < css.Length)
            {
                var rule = css.IndexOf("@font-face", cursor, StringComparison.OrdinalIgnoreCase);
                if (rule < 0) yield break;
                var open = css.IndexOf('{', rule + 10);
                if (open < 0) yield break;
                var close = css.IndexOf('}', open + 1);
                if (close < 0) yield break;
                yield return css[(open + 1)..close];
                cursor = close + 1;
            }
        }

        private static string? CssDeclarationValue(string rule, string name)
        {
            var cursor = 0;
            while (cursor < rule.Length)
            {
                while (cursor < rule.Length
                    && (char.IsWhiteSpace(rule[cursor]) || rule[cursor] == ';'))
                {
                    cursor++;
                }
                var separator = rule.IndexOf(':', cursor);
                if (separator < 0) return null;
                var end = separator + 1;
                var parenthesisDepth = 0;
                var quote = '\0';
                for (; end < rule.Length; end++)
                {
                    var character = rule[end];
                    if (quote != '\0')
                    {
                        if (character == quote
                            && (end == 0 || rule[end - 1] != '\\'))
                        {
                            quote = '\0';
                        }
                        continue;
                    }
                    if (character is '\'' or '"') quote = character;
                    else if (character == '(') parenthesisDepth++;
                    else if (character == ')' && parenthesisDepth > 0) parenthesisDepth--;
                    else if (character == ';' && parenthesisDepth == 0) break;
                }
                if (string.Equals(
                        rule[cursor..separator].Trim(),
                        name,
                        StringComparison.OrdinalIgnoreCase))
                {
                    return rule[(separator + 1)..end].Trim();
                }
                cursor = end + 1;
            }
            return null;
        }

        private static string? FirstCssUrl(string? value)
        {
            if (string.IsNullOrWhiteSpace(value)) return null;
            var start = value.IndexOf("url(", StringComparison.OrdinalIgnoreCase);
            if (start < 0) return null;
            var end = value.IndexOf(')', start + 4);
            if (end < 0) return null;
            return value[(start + 4)..end].Trim().Trim('"', '\'');
        }
#endif
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_enqueue")]
    internal static extern byte EngineEnqueue(IntPtr engine, in InputEvent input);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_enqueue_resize_frame")]
    internal static extern byte EngineEnqueueResizeFrame(
        IntPtr engine,
        in InputEvent resize,
        in InputEvent frame);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_cursor")]
    public static extern uint EngineGetCursor(IntPtr engine);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_requires_animation_frame")]
    public static extern byte EngineRequiresAnimationFrame(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_execute_script")]
    private static extern byte EngineExecuteScript(
        IntPtr engine,
        byte[] source,
        nuint sourceLength,
        byte[] documentName,
        nuint documentNameLength);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_begin_evaluate_v3")]
    internal static extern ulong EngineBeginEvaluateV3(
        IntPtr engine,
        in NativeInteropEvaluateRequest request,
        IntPtr completed,
        IntPtr userData);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_begin_invoke_v3")]
    internal static extern ulong EngineBeginInvokeV3(
        IntPtr engine,
        in NativeInteropInvokeRequest request,
        IntPtr completed,
        IntPtr userData);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_invoke_result_v3")]
    internal static extern IntPtr EngineTakeInvokeResultV3(
        IntPtr engine,
        ulong operationId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_cancel_invoke_v3")]
    internal static extern byte EngineCancelInvokeV3(
        IntPtr engine,
        ulong operationId);

    [DllImport(LibraryName, EntryPoint = "webscene_interop_result_release_v3")]
    internal static extern void InteropResultReleaseV3(
        IntPtr result,
        ulong leaseId);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_callback_v3")]
    internal static extern IntPtr EngineTakeCallbackV3(IntPtr engine);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_complete_callback_v3")]
    internal static extern byte EngineCompleteCallbackV3(
        IntPtr engine,
        in NativeInteropCallbackCompletion completion);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_cancel_callback_v3")]
    internal static extern byte EngineCancelCallbackV3(
        IntPtr engine,
        ulong callId);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_interop_callback_release_v3")]
    internal static extern void InteropCallbackReleaseV3(
        IntPtr callback,
        ulong leaseId);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_get_interop_pool_metrics_v3")]
    private static extern byte EngineGetInteropPoolMetricsV3(
        IntPtr engine,
        ref NativeInteropPoolMetrics metrics);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_host_request")]
    private static extern nuint EngineTakeHostRequest(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_console_message")]
    private static extern nuint EngineTakeConsoleMessage(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_take_input_dispatch_failure")]
    private static extern nuint EngineTakeInputDispatchFailure(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_last_error")]
    private static extern nuint EngineCopyLastError(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_first_iframe_html")]
    private static extern nuint EngineCopyFirstIframeHtml(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_scene_diagnostics")]
    private static extern nuint EngineCopySceneDiagnostics(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_feature_use")]
    private static extern nuint EngineCopyFeatureUse(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_event_listener_inventory")]
    private static extern nuint EngineCopyEventListenerInventory(
        IntPtr engine,
        byte[]? destination,
        nuint destinationCapacity);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_copy_canvas_layouts")]
    private static extern nuint EngineCopyCanvasLayouts(
        IntPtr engine,
        CanvasLayout* destination,
        nuint destinationCapacity);

    public static bool TryExecuteScript(IntPtr engine, string source, string documentName)
    {
        var sourceBytes = Encoding.UTF8.GetBytes(source);
        var nameBytes = Encoding.UTF8.GetBytes(documentName);
        return EngineExecuteScript(
            engine,
            sourceBytes,
            (nuint)sourceBytes.Length,
            nameBytes,
            (nuint)nameBytes.Length) != 0;
    }

    public static NativeInteropPoolMetrics GetInteropPoolMetrics(IntPtr engine)
    {
        var metrics = new NativeInteropPoolMetrics
        {
            StructSize = (uint)Marshal.SizeOf<NativeInteropPoolMetrics>(),
            Version = 3
        };
        if (EngineGetInteropPoolMetricsV3(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The experimental native interop metrics ABI is unavailable.");
        }
        return metrics;
    }

    public static bool TryTakeHostRequest(IntPtr engine, out string request)
    {
        var required = EngineTakeHostRequest(engine, null, 0);
        if (required <= 1)
        {
            request = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeHostRequest(engine, destination, (nuint)destination.Length);
        if (copied != required)
        {
            request = string.Empty;
            return false;
        }
        request = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        return true;
    }

    public static bool TryTakeConsoleMessage(
        IntPtr engine,
        out string level,
        out string message)
    {
        var required = EngineTakeConsoleMessage(engine, null, 0);
        if (required <= 1)
        {
            level = string.Empty;
            message = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeConsoleMessage(engine, destination, (nuint)destination.Length);
        if (copied != required)
        {
            level = string.Empty;
            message = string.Empty;
            return false;
        }
        var payload = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        var separator = payload.IndexOf('\n');
        level = separator < 0 ? "log" : payload[..separator];
        message = separator < 0 ? payload : payload[(separator + 1)..];
        return true;
    }

    public static bool TryTakeInputDispatchFailure(
        IntPtr engine,
        out ulong sequence,
        out uint kind,
        out string error)
    {
        var required = EngineTakeInputDispatchFailure(engine, null, 0);
        if (required <= 1)
        {
            sequence = 0;
            kind = 0;
            error = string.Empty;
            return false;
        }
        var destination = new byte[checked((int)required)];
        var copied = EngineTakeInputDispatchFailure(
            engine,
            destination,
            (nuint)destination.Length);
        if (copied != required)
        {
            sequence = 0;
            kind = 0;
            error = string.Empty;
            return false;
        }
        var payload = Encoding.UTF8.GetString(destination, 0, destination.Length - 1);
        var firstSeparator = payload.IndexOf('\n');
        var secondSeparator = firstSeparator < 0
            ? -1
            : payload.IndexOf('\n', firstSeparator + 1);
        if (firstSeparator <= 0
            || secondSeparator <= firstSeparator + 1
            || !ulong.TryParse(payload.AsSpan(0, firstSeparator), out sequence)
            || !uint.TryParse(
                payload.AsSpan(firstSeparator + 1, secondSeparator - firstSeparator - 1),
                out kind))
        {
            sequence = 0;
            kind = 0;
            error = "Malformed native input-dispatch failure payload: " + payload;
            return true;
        }
        error = payload[(secondSeparator + 1)..];
        return true;
    }

    public static string GetLastError(IntPtr engine)
    {
        var required = EngineCopyLastError(engine, null, 0);
        if (required <= 1)
        {
            return string.Empty;
        }
        var bytes = new byte[checked((int)required)];
        EngineCopyLastError(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetFirstIframeHtml(IntPtr engine)
    {
        var required = EngineCopyFirstIframeHtml(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyFirstIframeHtml(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetSceneDiagnostics(IntPtr engine)
    {
        var required = EngineCopySceneDiagnostics(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopySceneDiagnostics(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetFeatureUse(IntPtr engine)
    {
        var required = EngineCopyFeatureUse(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyFeatureUse(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static string GetEventListenerInventory(IntPtr engine)
    {
        var required = EngineCopyEventListenerInventory(engine, null, 0);
        if (required <= 1) return string.Empty;
        var bytes = new byte[checked((int)required)];
        EngineCopyEventListenerInventory(engine, bytes, (nuint)bytes.Length);
        return Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
    }

    public static CanvasLayout[] GetCanvasLayouts(IntPtr engine)
    {
        var required = EngineCopyCanvasLayouts(engine, null, 0);
        if (required == 0) return [];
        var layouts = new CanvasLayout[checked((int)required)];
        fixed (CanvasLayout* destination = layouts)
        {
            var actual = EngineCopyCanvasLayouts(
                engine,
                destination,
                (nuint)layouts.Length);
            if (actual > (nuint)layouts.Length)
            {
                throw new InvalidOperationException("Native canvas layout snapshot changed during copy.");
            }
        }
        return layouts;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_latest_scene")]
    public static extern IntPtr EngineAcquireLatestScene(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_acquire_next_scene")]
    public static extern IntPtr EngineAcquireNextScene(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_request_scene_checkpoint")]
    public static extern byte EngineRequestSceneCheckpoint(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_request_low_memory")]
    public static extern byte EngineRequestLowMemory(IntPtr engine);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_set_visible")]
    public static extern byte EngineSetVisible(IntPtr engine, byte visible);

    [DllImport(
        LibraryName,
        EntryPoint = "webscene_engine_set_preferred_color_scheme")]
    internal static extern byte EngineSetPreferredColorScheme(
        IntPtr engine,
        NativePreferredColorScheme preferredColorScheme);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_release")]
    public static extern void SceneRelease(IntPtr scene);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_acknowledge")]
    public static extern byte SceneAcknowledge(IntPtr scene);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_get_header")]
    internal static extern byte SceneGetHeader(IntPtr scene, out SceneHeader header);

    [DllImport(LibraryName, EntryPoint = "webscene_scene_get_commands")]
    internal static extern SceneCommand* SceneGetCommands(IntPtr scene, out uint count);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_metrics")]
    public static extern void EngineGetMetrics(IntPtr engine, out EngineMetrics metrics);

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_input_dispatch_metrics")]
    private static extern byte EngineGetInputDispatchMetrics(
        IntPtr engine,
        ref InputDispatchMetrics metrics);

    public static InputDispatchMetrics GetInputDispatchMetrics(IntPtr engine)
    {
        var metrics = new InputDispatchMetrics
        {
            StructSize = (uint)Marshal.SizeOf<InputDispatchMetrics>()
        };
        if (EngineGetInputDispatchMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native input-dispatch metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_animation_frame_metrics")]
    private static extern byte EngineGetAnimationFrameMetrics(
        IntPtr engine,
        ref AnimationFrameMetrics metrics);

    public static AnimationFrameMetrics GetAnimationFrameMetrics(IntPtr engine)
    {
        var metrics = new AnimationFrameMetrics
        {
            StructSize = (uint)Marshal.SizeOf<AnimationFrameMetrics>()
        };
        if (EngineGetAnimationFrameMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native animation-frame metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_scene_flow_metrics")]
    private static extern byte EngineGetSceneFlowMetrics(
        IntPtr engine,
        ref SceneFlowMetrics metrics);

    public static SceneFlowMetrics GetSceneFlowMetrics(IntPtr engine)
    {
        var metrics = new SceneFlowMetrics
        {
            StructSize = (uint)Marshal.SizeOf<SceneFlowMetrics>()
        };
        if (EngineGetSceneFlowMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native scene-flow metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_resize_frame_metrics")]
    private static extern byte EngineGetResizeFrameMetrics(
        IntPtr engine,
        ref ResizeFrameMetrics metrics);

    public static ResizeFrameMetrics GetResizeFrameMetrics(IntPtr engine)
    {
        var metrics = new ResizeFrameMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ResizeFrameMetrics>()
        };
        if (EngineGetResizeFrameMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException(
                "The native resize/frame metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_resource_cache_metrics")]
    private static extern byte EngineGetResourceCacheMetrics(
        IntPtr engine,
        ref ResourceCacheMetrics metrics);

    public static ResourceCacheMetrics GetResourceCacheMetrics(IntPtr engine)
    {
        var metrics = new ResourceCacheMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ResourceCacheMetrics>()
        };
        if (EngineGetResourceCacheMetrics(engine, ref metrics) == 0)
        {
            throw new InvalidOperationException("The native resource-cache metrics ABI is unavailable.");
        }
        return metrics;
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_process_cache_metrics")]
    private static extern byte EngineGetProcessCacheMetrics(
        IntPtr engine,
        ref ProcessCacheMetrics metrics);

    public static ProcessCacheMetrics? TryGetProcessCacheMetrics(IntPtr engine)
    {
        var metrics = new ProcessCacheMetrics
        {
            StructSize = (uint)Marshal.SizeOf<ProcessCacheMetrics>()
        };
        try
        {
            return EngineGetProcessCacheMetrics(engine, ref metrics) == 0
                ? null
                : metrics;
        }
        catch (EntryPointNotFoundException)
        {
            return null;
        }
    }

    [DllImport(LibraryName, EntryPoint = "webscene_engine_get_memory_metrics")]
    private static extern byte EngineGetMemoryMetrics(
        IntPtr engine,
        ref EngineMemoryMetrics metrics);

    public static EngineMemoryMetrics? TryGetMemoryMetrics(IntPtr engine)
    {
        var metrics = new EngineMemoryMetrics
        {
            StructSize = (uint)Marshal.SizeOf<EngineMemoryMetrics>()
        };
        try
        {
            return EngineGetMemoryMetrics(engine, ref metrics) == 0
                ? null
                : metrics;
        }
        catch (EntryPointNotFoundException)
        {
            return null;
        }
    }
}

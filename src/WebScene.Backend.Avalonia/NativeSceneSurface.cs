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
            _renderer.SetWebTypefaceRegistry(
                engine == IntPtr.Zero
                    ? null
                    : NativeWebSceneApi.GetWebTypefaceRegistry(engine));
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
                        ScheduleCompositionUiWake,
                        TopLevel.GetTopLevel(this)?.RenderScaling ?? 1));
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

    public long[] PresentationTimestamps
        => _renderObserver.Presentations;

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

    public NativeSurfacePerformanceMetrics CapturePerformanceMetrics()
        => new(
            RenderedScenes: RenderedSceneCount,
            RoutedInputEvents: RoutedInputEvents,
            AcceptedInputEvents: AcceptedInputEvents,
            CompositionUiWakes: CompositionSceneUiWakeCount,
            PendingCompositionPublications: PendingCompositionScenePublications,
            ResizePublicationNotifications: ResizePublicationNotificationCount);

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

        var matchingResizePublication = NotifyResizePublicationIfReady(scene);
        if (Volatile.Read(ref _compositionProjectionActive) != 0)
        {
            // The compositor clock consumes ordinary publications directly from
            // the mailbox. Only first presentation and cooperative live resize
            // need the coalesced UI-to-compositor liveness escape hatch.
            var wakePriority = NativeScenePublicationWakePolicy.Select(
                matchingResizePublication,
                _renderObserver.RenderedSceneCount,
                _compositionUiWakeGate);
            if (wakePriority != NativeSceneUiWakePriority.None)
            {
                PostCompositionUiWake(
                    wakePriority == NativeSceneUiWakePriority.Immediate
                        ? DispatcherPriority.Send
                        : DispatcherPriority.Normal);
            }
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
            // The active compositor clock observes native RAF demand at its next
            // display boundary. Do not route that edge through Avalonia's UI queue.
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
        => ScheduleCompositionUiWake(DispatcherPriority.Normal);

    private void ScheduleCompositionUiWake(DispatcherPriority priority)
    {
        if (!_compositionUiWakeGate.TrySchedule())
        {
            return;
        }

        PostCompositionUiWake(priority);
    }

    private void PostCompositionUiWake(DispatcherPriority priority)
    {
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
            priority);
    }

    private void ApplyCursorKind(int cursorKind)
    {
        Cursor = new Cursor(CursorTypeForKind(cursorKind));
    }

    internal static StandardCursorType CursorTypeForKind(int cursorKind)
    {
        return cursorKind switch
        {
            1 => StandardCursorType.Hand,
            2 => StandardCursorType.Ibeam,
            3 => StandardCursorType.Cross,
            4 => StandardCursorType.Wait,
            5 => StandardCursorType.SizeAll,
            6 => StandardCursorType.No,
            7 => StandardCursorType.Help,
            8 => StandardCursorType.SizeWestEast,
            9 => StandardCursorType.SizeNorthSouth,
            _ => StandardCursorType.Arrow
        };
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
            var frameTimestampMilliseconds =
                Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency;
            NativeWebSceneApi.EngineObserveCompositorFrame(
                _engine,
                frameTimestampMilliseconds);
            if (_submitAnimationFrames
                && NativeWebSceneApi.EngineRequiresAnimationFrame(_engine) != 0)
            {
                NativeFrameInput.Submit(_engine, frameTimestampMilliseconds);
            }
            RequestNextFrame();
        });
    }

    private void ObserveHostTimeline()
        => NativeWebSceneApi.EngineObserveCompositorFrame(
            _engine,
            Stopwatch.GetTimestamp() * 1000.0 / Stopwatch.Frequency);

    private void EnqueuePointer(uint kind, PointerEventArgs args)
    {
        ObserveHostTimeline();
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
        ObserveHostTimeline();
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
        ObserveHostTimeline();
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
        ObserveHostTimeline();
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
        bool textProfileChanged;
        lock (_rendererGate)
        {
            textProfileChanged = _renderer.SetPresenterDeviceScaleFactor(deviceScaleFactor);
        }
        if (_customVisual is not null)
        {
            _customVisual.SendHandlerMessage(
                deviceScaleFactor >= 1.5
                    ? NativeSceneCompositionMessage.TextScaleRetina
                    : NativeSceneCompositionMessage.TextScale1X);
        }
        if (textProfileChanged)
        {
            NativeWebSceneApi.EngineRequestSceneCheckpoint(_engine);
        }
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

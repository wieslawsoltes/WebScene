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
    TextScale1X,
    TextScaleRetina,
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

internal enum NativeSceneUiWakePriority
{
    Normal,
    Immediate
}

internal static class NativeScenePublicationWakePolicy
{
    public static bool RequiresUiWake(
        bool matchingResizePublication,
        long renderedSceneCount)
        => matchingResizePublication || renderedSceneCount == 0;

    public static NativeSceneUiWakePriority Priority(bool matchingResizePublication)
        => matchingResizePublication
            ? NativeSceneUiWakePriority.Immediate
            : NativeSceneUiWakePriority.Normal;
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
    private long _damageEvaluations;
    private long _emptyDamageDiffs;
    private long _partialDamageDiffs;
    private long _fullDamageDiffs;
    private double _damageArea;
    private double _damageUnionArea;
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
        Action scheduleUiWake,
        double deviceScaleFactor)
    {
        _engine = engine;
        _renderObserver = renderObserver;
        _publicationMailbox = publicationMailbox;
        _uiWakeGate = uiWakeGate;
        _scheduleUiWake = scheduleUiWake;
        _renderer.SetPresenterDeviceScaleFactor(deviceScaleFactor);
    }

    public override void OnMessage(object message)
    {
        if (message is not NativeSceneCompositionMessage command)
        {
            return;
        }

        if (command is NativeSceneCompositionMessage.TextScale1X
            or NativeSceneCompositionMessage.TextScaleRetina)
        {
            _renderer.SetPresenterDeviceScaleFactor(
                command == NativeSceneCompositionMessage.TextScaleRetina ? 2 : 1);
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
        // This only bounds Avalonia's root dirty region. OnRender still replays
        // the retained scene whenever Avalonia visits the custom visual, which
        // is required when the window target loses or exposes prior contents.
        if (damage.IsFull)
        {
            Invalidate();
        }
        else
        {
            Invalidate(damage.Bounds);
        }
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
            if (!NativeSceneViewValidation.IsValid(view)
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
            else
            {
                // A malformed or discontinuous diff must not remain at the
                // front of the ordered two-scene lane. Leaving it unacknowledged
                // fills producer back-pressure permanently: input is accepted,
                // but no later pointer, animation, or resize scene can publish.
                // Discard the stale mailbox edges before atomically resetting
                // the native lane and request a complete retained checkpoint.
                _publicationMailbox.Reset();
                NativeWebSceneApi.EngineRequestSceneCheckpoint(_engine);
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
        _damageEvaluations++;
        _damageArea += damage.SummedArea;
        _damageUnionArea += damage.RequiresRender
            ? damage.Bounds.Width * damage.Bounds.Height
            : 0;
        if (damage.IsFull && damage.RequiresRender)
        {
            _fullDamageDiffs++;
            Interlocked.Increment(ref FullInvalidationCount);
        }
        else if (damage.RequiresRender)
        {
            _partialDamageDiffs++;
            Interlocked.Add(ref DamageRectangleCount, damage.RectangleCount);
        }
        else
        {
            _emptyDamageDiffs++;
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
        var presenterMatrix = canvas.TotalMatrix;
        var contentMatrix = presenterMatrix;
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
            contentMatrix = canvas.TotalMatrix;
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
        NativePresenterTextDiagnostics.TryCapture(
            lease.SkSurface,
            presenterMatrix,
            contentMatrix,
            effective,
            _viewportWidth,
            _viewportHeight,
            scale,
            _renderer.PresenterDeviceScaleFactor);
        skiaSubmitTicks = Stopwatch.GetTimestamp() - skiaStarted;
        _renderObserver.RecordPresented();

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
            if (_damageEvaluations >= 300)
            {
                var summedDamagePercent = _viewportArea > 0
                    ? _damageArea * 100 / _viewportArea
                    : 0;
                var unionDamagePercent = _viewportArea > 0
                    ? _damageUnionArea * 100 / _viewportArea
                    : 0;
                Console.WriteLine(
                    $"Composition scene diffs: {_appliedDiffs:N0}, " +
                    $"sample={_damageEvaluations:N0}, changed layers={_changedLayers:N0}, " +
                    $"damage rects={_damageRectangles:N0}, " +
                    $"empty={_emptyDamageDiffs:N0}, partial={_partialDamageDiffs:N0}, " +
                    $"full={_fullDamageDiffs:N0}, " +
                    $"summed damage={summedDamagePercent:F1}%, " +
                    $"union damage={unionDamagePercent:F1}% of frame area");
                _changedLayers = 0;
                _damageRectangles = 0;
                _damageEvaluations = 0;
                _emptyDamageDiffs = 0;
                _partialDamageDiffs = 0;
                _fullDamageDiffs = 0;
                _damageArea = 0;
                _damageUnionArea = 0;
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
                if (NativeSceneViewValidation.IsValid(view)
                    && _renderer.ApplyDiff(view))
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
        if (!NativeSceneViewValidation.IsValid(view))
        {
            header = default;
            return false;
        }
        header = view->Header;
        return true;
    }

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
            if (!NativeSceneViewValidation.IsValid(view))
            {
                return;
            }

            var header = view->Header;

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

internal static class NativePresenterTextDiagnostics
{
    private const string OutputDirectoryEnvironmentVariable =
        "WEBSCENE_TEXT_PRESENTER_DIAGNOSTICS";
    private static readonly string? OutputDirectory =
        Environment.GetEnvironmentVariable(OutputDirectoryEnvironmentVariable);
    private static long s_firstEligibleFrame;
    private static int s_captureState;

    internal static void TryCapture(
        SKSurface? surface,
        SKMatrix presenterMatrix,
        SKMatrix contentMatrix,
        global::Avalonia.Vector effectiveSize,
        float viewportWidth,
        float viewportHeight,
        Vector2 contentScale,
        float presenterDeviceScaleFactor)
    {
        if (surface is null || string.IsNullOrWhiteSpace(OutputDirectory))
        {
            return;
        }

        var now = Stopwatch.GetTimestamp();
        var firstFrame = Interlocked.CompareExchange(
            ref s_firstEligibleFrame,
            now,
            0);
        if (firstFrame == 0) firstFrame = now;
        if (Stopwatch.GetElapsedTime(firstFrame, now) < TimeSpan.FromSeconds(8)
            || Interlocked.CompareExchange(ref s_captureState, 1, 0) != 0)
        {
            return;
        }

        try
        {
            Directory.CreateDirectory(OutputDirectory);
            surface.Canvas.Flush();
            using var image = surface.Snapshot();
            using var encoded = image.Encode(SKEncodedImageFormat.Png, 100);
            using (var stream = File.Create(Path.Combine(
                       OutputDirectory,
                       "presenter-surface.png")))
            {
                encoded.SaveTo(stream);
            }

            var colorSpace = image.ColorSpace;
            var rasterization = NativeTextShaping.ResolveFontRasterizationProfile(
                presenterDeviceScaleFactor);
            var metadata = new
            {
                CapturedUtc = DateTimeOffset.UtcNow,
                RasterizationMode = NativeTextShaping.ActiveFontRasterizationMode.ToString(),
                RasterizationOverride = Environment.GetEnvironmentVariable(
                    NativeTextShaping.RasterizationModeEnvironmentVariable),
                Rasterization = new
                {
                    rasterization.Subpixel,
                    rasterization.BaselineSnap,
                    Edging = rasterization.Edging.ToString(),
                    Hinting = rasterization.Hinting.ToString(),
                    rasterization.LinearMetrics,
                    rasterization.EmbeddedBitmaps
                },
                PresenterDeviceScaleFactor = presenterDeviceScaleFactor,
                EffectiveSize = new { effectiveSize.X, effectiveSize.Y },
                Viewport = new { Width = viewportWidth, Height = viewportHeight },
                ContentScale = new { contentScale.X, contentScale.Y },
                PresenterMatrix = MatrixValues(presenterMatrix),
                ContentMatrix = MatrixValues(contentMatrix),
                Surface = new
                {
                    image.Width,
                    image.Height,
                    ColorType = image.ColorType.ToString(),
                    AlphaType = image.AlphaType.ToString(),
                    IsSrgb = colorSpace?.IsSrgb,
                    GammaIsCloseToSrgb = colorSpace?.GammaIsCloseToSrgb,
                    GammaIsLinear = colorSpace?.GammaIsLinear,
                    PixelGeometry = surface.SurfaceProperties.PixelGeometry.ToString(),
                    Flags = surface.SurfaceProperties.Flags.ToString(),
                    Backend = surface.Context?.Backend.ToString() ?? "CPU"
                }
            };
            File.WriteAllText(
                Path.Combine(OutputDirectory, "presenter-metadata.json"),
                JsonSerializer.Serialize(
                    metadata,
                    new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(
                $"WebScene text presenter diagnostic captured to {OutputDirectory}");
            Interlocked.Exchange(ref s_captureState, 2);
        }
        catch (Exception error) when (error is not OutOfMemoryException)
        {
            Console.Error.WriteLine(
                $"WebScene text presenter diagnostic failed: {error.Message}");
            Interlocked.Exchange(ref s_captureState, 0);
        }

        static object MatrixValues(SKMatrix matrix)
            => new
            {
                matrix.ScaleX,
                matrix.SkewX,
                matrix.TransX,
                matrix.SkewY,
                matrix.ScaleY,
                matrix.TransY,
                matrix.Persp0,
                matrix.Persp1,
                matrix.Persp2
            };
    }
}
#endif

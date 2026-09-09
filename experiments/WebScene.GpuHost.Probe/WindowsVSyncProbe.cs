using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Rendering;
using Avalonia.Rendering.Composition;
using WebScene.Backends.Avalonia.Native;

// Diagnostic host: Avalonia 11.3.4 does not expose render-loop injection.
// Install the existing loop before platform initialization, without changing
// its rendering behavior or touching a running loop's subscriptions.
internal sealed class WindowsVSyncProbe : IRenderTimer
{
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly object _gate = new();
    private Action<TimeSpan>? _tick;
    private bool _started;
    private long _vsyncTicks;
    private long _fallbackTicks;
    private uint _lastResult;
    private readonly AutoResetEvent _ready = new(false);
    private long _latestTick;
    private long _latestPhase;
    private readonly bool _statistics = Environment.GetCommandLineArgs().Contains("--compositor-clock-statistics");
    private long _statisticsTicks, _statisticsFailures;
    private ulong _lastPeriod;
    private readonly bool _trace = Environment.GetCommandLineArgs().Contains("--trace-vsync");
    private readonly List<(long Start, long End)> _renders = [];
    public bool RunsInBackground => true;
    public event Action<TimeSpan>? Tick
    {
        add
        {
            lock (_gate)
            {
                _tick += value;
                if (!_started)
                {
                    _started = true;
                    new Thread(Render) { IsBackground = true, Name = "WebScene vsync renderer" }.Start();
                    new Thread(Run) { IsBackground = true, Name = "WebScene compositor clock" }.Start();
                }
                Monitor.PulseAll(_gate);
            }
        }
        remove { lock (_gate) _tick -= value; }
    }

    public static AppBuilder Configure(AppBuilder builder)
    {
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000))
            throw new PlatformNotSupportedException("This diagnostic requires the Windows 11 compositor clock.");
        var initialize = builder.RenderingSubsystemInitializer!;
        return builder.UseRenderingSubsystem(() =>
        {
            initialize();
            var assembly = typeof(Compositor).Assembly;
            var loopType = assembly.GetType("Avalonia.Rendering.RenderLoop", true)!;
            var loopInterface = assembly.GetType("Avalonia.Rendering.IRenderLoop", true)!;
            var loop = Activator.CreateInstance(loopType, new WindowsVSyncProbe())!;
            var registration = typeof(AvaloniaLocator).GetMethod("Bind")!
                .MakeGenericMethod(loopInterface).Invoke(AvaloniaLocator.CurrentMutable, null)!;
            registration.GetType().GetMethod("ToConstant")!.MakeGenericMethod(loopType)
                .Invoke(registration, [loop]);
        }, builder.RenderingSubsystemName ?? "Skia");
    }

    private void Run()
    {
        if (_statistics)
        {
            try { Console.WriteLine($"DXGI vblank virtualization disable: 0x{DXGIDisableVBlankVirtualization():x8}"); }
            catch (EntryPointNotFoundException) { Console.WriteLine("DXGI vblank virtualization control unavailable."); }
        }
        AppDomain.CurrentDomain.ProcessExit += (_, _) => Console.WriteLine(
            $"Windows compositor clock: vsyncTicks={_vsyncTicks}, fallbackTicks={_fallbackTicks}, lastStatus=0x{_lastResult:x8}, statisticsTicks={_statisticsTicks}, statisticsFailures={_statisticsFailures}, periodQpc={_lastPeriod}, phaseApplications={NativeCompositorFrameClock.AppliedTimestamps}");
        if (_trace) AppDomain.CurrentDomain.ProcessExit += (_, _) =>
        {
            lock (_renders) Console.WriteLine("Windows render intervals: " + System.Text.Json.JsonSerializer.Serialize(
                _renders.Select(sample => new { sample.Start, sample.End })));
        };
        while (true)
        {
            lock (_gate) while (_tick is null) Monitor.Wait(_gate);
            var start = _clock.Elapsed;
            var result = DCompositionWaitForCompositorClock(0, IntPtr.Zero, 100);
            _lastResult = result;
            // Chromium also guards early returns during desktop occlusion.
            // Keep application startup/disposal live when the display is off,
            // but explicitly count these synthetic ticks separately from vsync.
            if (result != 0 || _clock.Elapsed - start < TimeSpan.FromMilliseconds(1))
            {
                if (_fallbackTicks == 0)
                    Console.WriteLine($"Windows compositor clock unavailable: status=0x{result:x8}; fallback ticks are not vsync.");
                Thread.Sleep(17);
                _fallbackTicks++;
            }
            else _vsyncTicks++;
            var phase = Stopwatch.GetTimestamp();
            if (_statistics && result == 0)
            {
                try
                {
                    if (DCompositionGetFrameId(2, out var id) == 0
                        && DCompositionGetStatistics(id, out var stats, 0, IntPtr.Zero, IntPtr.Zero) == 0
                        && stats.Period > 0 && stats.Start > 0 && stats.Start <= (ulong)phase)
                    {
                        phase = checked((long)stats.Start);
                        _lastPeriod = stats.Period;
                        _statisticsTicks++;
                    }
                    else _statisticsFailures++;
                }
                catch (EntryPointNotFoundException) { _statisticsFailures++; }
            }
            Interlocked.Exchange(ref _latestPhase, Math.Max(Interlocked.Read(ref _latestPhase), phase));
            Interlocked.Exchange(ref _latestTick, _clock.Elapsed.Ticks);
            _ready.Set();
        }
    }

    private void Render()
    {
        // Sampling the display clock must not wait for scene preparation.
        // One pending signal retains the latest tick and bounds backlog.
        while (true)
        {
            _ready.WaitOne();
            Action<TimeSpan>? tick;
            lock (_gate) tick = _tick;
            var start = _trace ? Stopwatch.GetTimestamp() : 0;
            var previousPhase = NativeCompositorFrameClock.CurrentTimestamp;
            try
            {
                if (_statistics) NativeCompositorFrameClock.CurrentTimestamp = Interlocked.Read(ref _latestPhase);
                tick?.Invoke(TimeSpan.FromTicks(Interlocked.Read(ref _latestTick)));
            }
            finally { NativeCompositorFrameClock.CurrentTimestamp = previousPhase; }
            if (_trace) lock (_renders)
            {
                if (_renders.Count < 4096) _renders.Add((start, Stopwatch.GetTimestamp()));
            }
        }
    }

    [DllImport("dcomp.dll", ExactSpelling = true)]
    private static extern uint DCompositionWaitForCompositorClock(uint count, IntPtr handles, uint timeout);
    [StructLayout(LayoutKind.Sequential)] private struct FrameStats { public ulong Start, Target, Period; }
    [DllImport("dcomp.dll", ExactSpelling = true)] private static extern int DCompositionGetFrameId(uint kind, out ulong id);
    [DllImport("dcomp.dll", ExactSpelling = true)] private static extern int DCompositionGetStatistics(ulong id, out FrameStats stats, uint count, IntPtr targets, IntPtr actualCount);
    [DllImport("dxgi.dll", ExactSpelling = true)] private static extern int DXGIDisableVBlankVirtualization();
}

namespace WebScene.Backends.Avalonia.Native;

// Diagnostic render timers may supply the display's QPC phase for the duration
// of their synchronous render-loop tick. Other hosts retain their existing clock.
internal static class NativeCompositorFrameClock
{
    [ThreadStatic] internal static long CurrentTimestamp;
    internal static long AppliedTimestamps;
    internal static long ReadTimestamp()
    {
        if (CurrentTimestamp != 0) Interlocked.Increment(ref AppliedTimestamps);
        return CurrentTimestamp;
    }
}

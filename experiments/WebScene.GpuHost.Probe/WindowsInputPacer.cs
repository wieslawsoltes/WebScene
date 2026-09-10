using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

// Synthetic input cadence only. Display/render scheduling uses the compositor
// clock; it must never depend on this workload timer.
internal sealed class WindowsInputPacer : IDisposable
{
    private sealed class TimerWaitHandle : WaitHandle
    {
        public TimerWaitHandle(IntPtr handle) => SafeWaitHandle = new SafeWaitHandle(handle, ownsHandle: true);
    }
    private readonly TimerWaitHandle _wait;

    public WindowsInputPacer()
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("High-resolution input pacing requires Windows.");
        var timer = CreateWaitableTimerEx(IntPtr.Zero, null, 2, 0x1f0003);
        if (timer == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        _wait = new TimerWaitHandle(timer);
    }

    public Task WaitUntilAsync(long deadline) => Task.Run(() =>
    {
        var remaining = deadline - Stopwatch.GetTimestamp();
        if (remaining <= 0) return;
        var due = -(long)Math.Ceiling(remaining * 10_000_000.0 / Stopwatch.Frequency);
        if (!SetWaitableTimer(_wait.SafeWaitHandle, in due, 0, IntPtr.Zero, IntPtr.Zero, false))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        _wait.WaitOne();
    });

    public void Dispose() => _wait.Dispose();

    [DllImport("kernel32.dll", EntryPoint = "CreateWaitableTimerExW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWaitableTimerEx(IntPtr attributes, string? name, uint flags, uint access);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWaitableTimer(SafeWaitHandle timer, in long dueTime, int period,
        IntPtr completion, IntPtr argument, [MarshalAs(UnmanagedType.Bool)] bool resume);
}

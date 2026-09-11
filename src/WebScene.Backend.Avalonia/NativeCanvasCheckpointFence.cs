using System.Runtime.InteropServices;
using Avalonia.OpenGL.Egl;
using Avalonia.Skia;

namespace WebScene.Backends.Avalonia.Native;

// A zero-timeout poll never waits for the GPU on the composition thread.
internal sealed class NativeCanvasCheckpointFence : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate IntPtr Fence(uint condition, uint flags);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate uint Wait(IntPtr sync, uint flags, ulong timeout);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Delete(IntPtr sync);
    private readonly EglContext _context;
    private readonly Wait _wait;
    private readonly Delete _delete;
    private IntPtr _sync;
    private NativeCanvasCheckpointFence(EglContext context, Wait wait, Delete delete, IntPtr sync)
        => (_context, _wait, _delete, _sync) = (context, wait, delete, sync);

    internal static NativeCanvasCheckpointFence? Create(ISkiaSharpApiLease lease)
    {
        if (!OperatingSystem.IsWindows()) return null;
        using var platform = lease.TryLeasePlatformGraphicsApi();
        if (platform?.Context is not EglContext context) return null;
        return Create(context, lease.GrContext!);
    }

    internal static NativeCanvasCheckpointFence? Create(EglContext context, SkiaSharp.GRContext skia)
    {
        var gl = context.GlInterface;
        var create = gl.GetProcAddress("glFenceSync");
        var wait = gl.GetProcAddress("glClientWaitSync");
        var delete = gl.GetProcAddress("glDeleteSync");
        if (create == IntPtr.Zero || wait == IntPtr.Zero || delete == IntPtr.Zero) return null;
        skia.Flush();
        var sync = Marshal.GetDelegateForFunctionPointer<Fence>(create)(0x9117, 0);
        if (sync == IntPtr.Zero) return null;
        gl.Flush();
        return new(context, Marshal.GetDelegateForFunctionPointer<Wait>(wait),
            Marshal.GetDelegateForFunctionPointer<Delete>(delete), sync);
    }

    internal bool IsReady()
    {
        using var current = _context.EnsureCurrent();
        var status = _wait(_sync, 0, 0);
        if (status == 0x911D) throw new InvalidOperationException("Canvas checkpoint GPU fence failed.");
        return status is 0x911A or 0x911C;
    }

    public void Dispose()
    {
        if (_sync == IntPtr.Zero) return;
        using var current = _context.EnsureCurrent();
        _delete(_sync);
        _sync = IntPtr.Zero;
    }
}

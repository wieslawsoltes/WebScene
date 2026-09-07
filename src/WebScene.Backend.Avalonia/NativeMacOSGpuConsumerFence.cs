using System;
using System.Runtime.InteropServices;

namespace WebScene.Backends.Avalonia.Native;

// Owns consumer retirement after the host's final GL use. The host must retain
// and poll this object in its current context; there is no GC-based completion.
internal sealed class NativeMacOSGpuConsumerFence
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr FenceSync(uint condition, uint flags);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint ClientWaitSync(IntPtr sync, uint flags, ulong timeout);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void DeleteSync(IntPtr sync);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void Flush();

    private readonly IntPtr _context;
    private readonly int _thread = Environment.CurrentManagedThreadId;
    private readonly ClientWaitSync _wait;
    private readonly DeleteSync _delete;
    private readonly NativeGpuImageConsumerV3 _consumer;
    private IntPtr _fence;

    private NativeMacOSGpuConsumerFence(IntPtr context, ClientWaitSync wait, DeleteSync delete,
        NativeGpuImageConsumerV3 consumer)
    {
        _context = context; _wait = wait; _delete = delete; _consumer = consumer;
    }

    // Resolve from the host GlInterface.GetProcAddress, not global GL symbols
    // that might name ANGLE. Ownership transfers only when this call succeeds.
    internal static NativeMacOSGpuConsumerFence Create(Func<string, IntPtr> getProcAddress,
        NativeGpuImageConsumerV3 consumer)
    {
        ArgumentNullException.ThrowIfNull(getProcAddress);
        ArgumentNullException.ThrowIfNull(consumer);
        var context = NativeMacOSGpuImageImport.CurrentContext;
        if (context == IntPtr.Zero) throw new InvalidOperationException("A current CGL context is required.");
        T Resolve<T>(string name) where T : Delegate
        {
            var address = getProcAddress(name);
            if (address == IntPtr.Zero) throw new NotSupportedException($"Host GL entry point {name} is unavailable.");
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        var insert = Resolve<FenceSync>("glFenceSync");
        var wait = Resolve<ClientWaitSync>("glClientWaitSync");
        var delete = Resolve<DeleteSync>("glDeleteSync");
        var flush = Resolve<Flush>("glFlush");
        var result = new NativeMacOSGpuConsumerFence(context, wait, delete, consumer);
        result._fence = insert(0x9117, 0); // GL_SYNC_GPU_COMMANDS_COMPLETE
        if (result._fence == IntPtr.Zero) throw new InvalidOperationException("Host GL fence creation failed.");
        flush();
        return result;
    }

    internal bool TryComplete()
    {
        if (_thread != Environment.CurrentManagedThreadId || _context != NativeMacOSGpuImageImport.CurrentContext)
            throw new InvalidOperationException("GPU fence polling requires its owning thread and CGL context.");
        if (_fence == IntPtr.Zero) return true;
        var status = _wait(_fence, 0, 0); // Zero timeout: never wait for the device on the CPU.
        if (status == 0x911B) return false; // GL_TIMEOUT_EXPIRED
        if (status != 0x911A && status != 0x911C) // ALREADY_SIGNALED / CONDITION_SATISFIED
            throw new InvalidOperationException("Host GL fence polling failed; consumer remains retained.");
        _delete(_fence);
        _fence = IntPtr.Zero;
        _consumer.Complete();
        return true;
    }
}

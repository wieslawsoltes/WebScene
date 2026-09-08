using System;
using System.Runtime.InteropServices;

namespace WebScene.Backends.Avalonia.Native;

// Caller must flush/submit Skia reads before inserting this marker while holding
// the same host queue lease. Own this fence and the image until TryComplete succeeds.
internal sealed class NativeMetalConsumerFence
{
    private IntPtr _command;
    private bool _completed;
    private NativeMetalConsumerFence(IntPtr command) => _command = command;

    internal static NativeMetalConsumerFence Insert(IntPtr queue)
    {
        if (!OperatingSystem.IsMacOS()) throw new PlatformNotSupportedException();
        if (queue == IntPtr.Zero) throw new ArgumentException("A leased Metal queue is required.", nameof(queue));
        var command = SendObject(queue, Selector("commandBuffer"));
        if (command == IntPtr.Zero) throw new InvalidOperationException("Metal retirement marker allocation failed.");
        SendObject(command, Selector("retain"));
        SendVoid(command, Selector("commit"));
        return new NativeMetalConsumerFence(command);
    }

    internal bool TryComplete()
    {
        if (_completed) return true;
        var status = SendUInt(_command, Selector("status"));
        if (status == 5) throw new InvalidOperationException("Metal retirement command failed; image ownership must remain retained.");
        if (status != 4) return false; // MTLCommandBufferStatusCompleted
        SendVoid(_command, Selector("release"));
        _command = IntPtr.Zero;
        _completed = true;
        return true;
    }
    // No finalizer: abandoning this object must not fabricate consumer completion.
    private const string ObjC = "/usr/lib/libobjc.A.dylib";
    [DllImport(ObjC, EntryPoint="sel_registerName")] private static extern IntPtr Selector(string name);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern IntPtr SendObject(IntPtr receiver, IntPtr selector);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern void SendVoid(IntPtr receiver, IntPtr selector);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern nuint SendUInt(IntPtr receiver, IntPtr selector);
}

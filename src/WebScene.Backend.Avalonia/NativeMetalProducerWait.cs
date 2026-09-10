using System;
using System.Runtime.InteropServices;
namespace WebScene.Backends.Avalonia.Native;

internal static class NativeMetalProducerWait
{
    // Caller holds the Metal platform lease. Native consumer owns every borrowed
    // event until submission; Metal command encoding retains its dependencies.
    internal static void Submit(IntPtr queue, NativeGpuImageConsumerV3 consumer)
    {
        if(queue==IntPtr.Zero) throw new ArgumentException("A leased Metal queue is required.",nameof(queue));
        consumer.WithMetalEvents(events => {
            if(events.Length==0) return;
            // WithMetalEvents validates the entire list before invoking us.
            var command=SendObject(queue,Selector("commandBuffer"));
            if(command==IntPtr.Zero) throw new InvalidOperationException("Metal producer barrier allocation failed.");
            foreach(var dependency in events)
                EncodeWait(command,Selector("encodeWaitForEvent:value:"),dependency.BorrowedSharedEvent,dependency.SignaledValue);
            SendVoid(command,Selector("commit"));
        });
    }
    private const string ObjC="/usr/lib/libobjc.A.dylib";
    [DllImport(ObjC,EntryPoint="sel_registerName")] private static extern IntPtr Selector(string name);
    [DllImport(ObjC,EntryPoint="objc_msgSend")] private static extern IntPtr SendObject(IntPtr receiver,IntPtr selector);
    [DllImport(ObjC,EntryPoint="objc_msgSend")] private static extern void EncodeWait(IntPtr receiver,IntPtr selector,IntPtr sharedEvent,ulong value);
    [DllImport(ObjC,EntryPoint="objc_msgSend")] private static extern void SendVoid(IntPtr receiver,IntPtr selector);
}

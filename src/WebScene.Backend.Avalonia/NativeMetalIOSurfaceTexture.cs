using System;
using System.Runtime.InteropServices;

namespace WebScene.Backends.Avalonia.Native;

// Owns a Metal texture view of an existing IOSurface; does not copy pixels.
// The caller must keep the consumer and this texture until GPU reads complete.
internal sealed class NativeMetalIOSurfaceTexture : IDisposable
{
    internal IntPtr Handle { get; private set; }
    internal int Width { get; }
    internal int Height { get; }
    private NativeMetalIOSurfaceTexture(IntPtr texture, int width, int height)
    { Handle = texture; Width = width; Height = height; }

    internal static NativeMetalIOSurfaceTexture Import(IntPtr device, NativeGpuImageConsumerV3 consumer)
    {
        if (!OperatingSystem.IsMacOS()) throw new PlatformNotSupportedException();
        ArgumentNullException.ThrowIfNull(consumer);
        if (device == IntPtr.Zero) throw new ArgumentException("A leased Metal device is required.", nameof(device));
        NativeMetalIOSurfaceTexture? result = null;
        consumer.WithIOSurface(view =>
        {
            var width = IOSurfaceGetWidth(view.BorrowedIOSurface);
            var height = IOSurfaceGetHeight(view.BorrowedIOSurface);
            if (width == 0 || height == 0 || width > int.MaxValue || height > int.MaxValue
                || IOSurfaceGetPixelFormat(view.BorrowedIOSurface) != 0x42475241)
                throw new NotSupportedException("Metal import requires a nonempty BGRA IOSurface.");
            var descriptor = CreateDescriptor(GetClass("MTLTextureDescriptor"),
                Selector("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"),
                80, width, height, false); // MTLPixelFormatBGRA8Unorm
            SetValue(descriptor, Selector("setUsage:"), 1); // ShaderRead
            var texture = CreateTexture(device, Selector("newTextureWithDescriptor:iosurface:plane:"),
                descriptor, view.BorrowedIOSurface, 0);
            if (texture == IntPtr.Zero) throw new InvalidOperationException("Metal IOSurface import failed.");
            result = new NativeMetalIOSurfaceTexture(texture, (int)width, (int)height);
        });
        return result ?? throw new NotSupportedException("The native consumer exposes no IOSurface.");
    }
    public void Dispose()
    {
        var texture = Handle; Handle = IntPtr.Zero;
        if (texture != IntPtr.Zero) Release(texture, Selector("release"));
    }
    private const string ObjC = "/usr/lib/libobjc.A.dylib";
    private const string Surface = "/System/Library/Frameworks/IOSurface.framework/IOSurface";
    [DllImport(ObjC, EntryPoint="objc_getClass")] private static extern IntPtr GetClass(string name);
    [DllImport(ObjC, EntryPoint="sel_registerName")] private static extern IntPtr Selector(string name);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern IntPtr CreateDescriptor(IntPtr receiver, IntPtr selector, ulong format, nuint width, nuint height, [MarshalAs(UnmanagedType.I1)] bool mipmapped);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern IntPtr CreateTexture(IntPtr receiver, IntPtr selector, IntPtr descriptor, IntPtr surface, nuint plane);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern void SetValue(IntPtr receiver, IntPtr selector, ulong value);
    [DllImport(ObjC, EntryPoint="objc_msgSend")] private static extern void Release(IntPtr receiver, IntPtr selector);
    [DllImport(Surface)] private static extern nuint IOSurfaceGetWidth(IntPtr surface);
    [DllImport(Surface)] private static extern nuint IOSurfaceGetHeight(IntPtr surface);
    [DllImport(Surface)] private static extern uint IOSurfaceGetPixelFormat(IntPtr surface);
}

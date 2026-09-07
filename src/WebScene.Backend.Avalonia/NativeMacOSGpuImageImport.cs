using System;
using System.Runtime.InteropServices;

namespace WebScene.Backends.Avalonia.Native;

// Import into a rectangle texture already bound by the host's GlInterface.
// The caller owns GL state and must keep the consumer through GPU completion.
// This does not opt the retained renderer into GPU scenes or synchronize writes.
internal static class NativeMacOSGpuImageImport
{
    private const string OpenGL = "/System/Library/Frameworks/OpenGL.framework/OpenGL";
    private const string IOSurface = "/System/Library/Frameworks/IOSurface.framework/IOSurface";

    internal static IntPtr CurrentContext => OperatingSystem.IsMacOS() ? CGLGetCurrentContext() : IntPtr.Zero;

    internal static bool TryBindCurrentRectangleTexture(NativeGpuImageConsumerV3 consumer)
    {
        ArgumentNullException.ThrowIfNull(consumer);
        if (!OperatingSystem.IsMacOS()) return false;
        var context = CGLGetCurrentContext();
        if (context == IntPtr.Zero) return false;
        var imported = false;
        return consumer.WithIOSurface(view =>
        {
            var width = IOSurfaceGetWidth(view.BorrowedIOSurface);
            var height = IOSurfaceGetHeight(view.BorrowedIOSurface);
            if (width == 0 || height == 0 || width > int.MaxValue || height > int.MaxValue) return;
            // The native pool exposes negotiated BGRA8 only. CGL requires this
            // tuple for the packed BGRA IOSurface storage on the tested route.
            imported = CGLTexImageIOSurface2D(context, 0x84F5, 0x8058,
                (int)width, (int)height, 0x80E1, 0x8367, view.BorrowedIOSurface, 0) == 0;
        }) && imported;
    }

    [DllImport(OpenGL, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr CGLGetCurrentContext();
    [DllImport(OpenGL, CallingConvention = CallingConvention.Cdecl)]
    private static extern int CGLTexImageIOSurface2D(IntPtr context, uint target, uint internalFormat,
        int width, int height, uint format, uint type, IntPtr surface, uint plane);
    [DllImport(IOSurface, CallingConvention = CallingConvention.Cdecl)]
    private static extern nuint IOSurfaceGetWidth(IntPtr surface);
    [DllImport(IOSurface, CallingConvention = CallingConvention.Cdecl)]
    private static extern nuint IOSurfaceGetHeight(IntPtr surface);
}

using System;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.InteropServices;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// Pinned SkiaSharp 2.88 native Metal entry point; its generic managed assembly
// omits the Metal constructor. This owns the backend wrapper, not the texture.
internal static unsafe class NativeMetalBackendTexture
{
    [DllImport("libSkiaSharp", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr gr_backendtexture_new_metal(int width, int height,
        [MarshalAs(UnmanagedType.I1)] bool mipmapped, IntPtr* textureInfo);
    [DllImport("libSkiaSharp", CallingConvention = CallingConvention.Cdecl)]
    private static extern void gr_backendtexture_delete(IntPtr texture);

    [DynamicDependency(DynamicallyAccessedMemberTypes.NonPublicConstructors, typeof(GRBackendTexture))]
    internal static GRBackendTexture Create(int width, int height, IntPtr metalTexture)
    {
        if (!OperatingSystem.IsMacOS()) throw new PlatformNotSupportedException();
        if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
        if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
        if (metalTexture == IntPtr.Zero) throw new ArgumentException("A retained Metal texture is required.", nameof(metalTexture));
        var constructor = typeof(GRBackendTexture).GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic, null,
            new[] { typeof(IntPtr), typeof(bool) }, null)
            ?? throw new MissingMethodException("GRBackendTexture(IntPtr, bool)");
        var handle = gr_backendtexture_new_metal(width, height, false, &metalTexture);
        if (handle == IntPtr.Zero) throw new InvalidOperationException("Metal backend texture wrapping failed.");
        try { return (GRBackendTexture)constructor.Invoke(new object[] { handle, true }); }
        catch { gr_backendtexture_delete(handle); throw; }
    }
}

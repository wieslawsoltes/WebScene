using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

internal static unsafe class NativeExternalTextureRenderer
{
    private const uint IosurfaceHandle = 1;
    private const uint Bgra8Unorm = 1;
    private const uint PremultipliedAlpha = 1U << 0;
    private const uint GpuComplete = 1U << 1;

    [StructLayout(LayoutKind.Sequential)]
    private struct MtlTextureInfo
    {
        public IntPtr Texture;
    }

    [DllImport(
        "libSkiaSharp",
        CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "gr_backendtexture_new_metal")]
    private static extern IntPtr CreateMetalBackendTexture(
        int width,
        int height,
        [MarshalAs(UnmanagedType.I1)] bool mipmapped,
        MtlTextureInfo* textureInfo);

    [UnsafeAccessor(UnsafeAccessorKind.Constructor)]
    private static extern GRBackendTexture WrapBackendTexture(
        IntPtr handle,
        bool owns);

    public static int Draw(
        SKCanvas canvas,
        GRContext? context,
        NativeExternalTexture* textures,
        uint textureCount)
    {
        if (!OperatingSystem.IsMacOS()
            || context is null
            || textures is null
            || textureCount == 0
            || textureCount > int.MaxValue)
        {
            return 0;
        }

        var drawn = 0;
        var span = new ReadOnlySpan<NativeExternalTexture>(
            textures,
            checked((int)textureCount));
        foreach (ref readonly var texture in span)
        {
            if (texture.HandleKind != IosurfaceHandle
                || texture.PixelFormat != Bgra8Unorm
                || (texture.Flags & GpuComplete) == 0
                || texture.TextureHandle == 0
                || texture.PixelWidth == 0
                || texture.PixelHeight == 0
                || texture.Width <= 0
                || texture.Height <= 0)
            {
                continue;
            }

            var metal = new MtlTextureInfo
            {
                Texture = (IntPtr)texture.TextureHandle
            };
            var handle = CreateMetalBackendTexture(
                checked((int)texture.PixelWidth),
                checked((int)texture.PixelHeight),
                mipmapped: false,
                &metal);
            if (handle == IntPtr.Zero)
            {
                continue;
            }

            using var backend = WrapBackendTexture(handle, owns: true);
            using var image = SKImage.FromTexture(
                context,
                backend,
                GRSurfaceOrigin.TopLeft,
                SKColorType.Bgra8888,
                (texture.Flags & PremultipliedAlpha) != 0
                    ? SKAlphaType.Premul
                    : SKAlphaType.Opaque);
            if (image is null)
            {
                continue;
            }

            canvas.DrawImage(
                image,
                new SKRect(
                    texture.X,
                    texture.Y,
                    texture.X + texture.Width,
                    texture.Y + texture.Height));
            drawn++;
        }

        if (drawn != 0)
        {
            // The provider waits for Dawn before publishing. Complete Skia's
            // read before the scene lease releases the ring slot for reuse.
            context.Flush(submit: true, synchronous: true);
        }
        return drawn;
    }
}

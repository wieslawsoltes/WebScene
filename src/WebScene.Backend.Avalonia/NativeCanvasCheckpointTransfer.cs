using System.Runtime.InteropServices;
using Avalonia.OpenGL.Egl;
using Avalonia.Skia;
using SkiaSharp;

namespace WebScene.Backends.Avalonia.Native;

// One immutable checkpoint transfer. The pack buffer is never reused until its
// GPU fence completed and the worker copied its contents into independent RAM.
internal sealed unsafe class NativeCanvasCheckpointTransfer : IDisposable
{
    private const uint PackBuffer = 0x88EB, Texture = 0x0DE1, Framebuffer = 0x8D40;
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Gen(int count, out uint value);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Delete(int count, ref uint value);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Bind(uint target, uint value);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Integer(uint name, out int value);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Store(uint name, int value);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Active(uint texture);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void TexImage(uint target, int level, int format, int width, int height, int border, uint pixelFormat, uint type, IntPtr data);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Attach(uint target, uint attachment, uint textureTarget, uint texture, int level);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate uint Status(uint target);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Allocate(uint target, nint size, IntPtr data, uint usage);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate void Read(int x, int y, int width, int height, uint format, uint type, IntPtr data);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate IntPtr Map(uint target, nint offset, nint length, uint access);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] private delegate byte Unmap(uint target);
    private readonly EglContext _context;
    private readonly Bind _bind;
    private readonly Delete _delete;
    private readonly Integer _integer;
    private readonly Map _map;
    private readonly Unmap _unmap;
    private readonly int _width, _height;
    private uint _buffer;
    private NativeCanvasCheckpointFence? _fence;

    private NativeCanvasCheckpointTransfer(EglContext context, int width, int height)
    {
        _context = context; _width = width; _height = height;
        _bind = Get<Bind>("glBindBuffer"); _delete = Get<Delete>("glDeleteBuffers");
        _integer = Get<Integer>("glGetIntegerv"); _map = Get<Map>("glMapBufferRange");
        _unmap = Get<Unmap>("glUnmapBuffer");
    }
    private T Get<T>(string name) where T : Delegate
    {
        var address = _context.GlInterface.GetProcAddress(name);
        if (address == IntPtr.Zero) throw new NotSupportedException(name);
        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }

    internal static NativeCanvasCheckpointTransfer? Create(SKImage image, ISkiaSharpApiLease lease)
    {
        if (!OperatingSystem.IsWindows() || lease.GrContext is null) return null;
        using var platform = lease.TryLeasePlatformGraphicsApi();
        if (platform?.Context is not EglContext context || context.Version.Major < 3) return null;
        NativeCanvasCheckpointTransfer? result = null;
        try
        {
            result = new(context, image.Width, image.Height);
            result.Queue(image, lease);
            return result;
        }
        catch (NotSupportedException) { result?.Dispose(); return null; }
        catch { result?.Dispose(); throw; }
    }

    private void Queue(SKImage image, ISkiaSharpApiLease lease)
    {
        var genTextures = Get<Gen>("glGenTextures"); var deleteTextures = Get<Delete>("glDeleteTextures");
        var bindTexture = Get<Bind>("glBindTexture"); var texImage = Get<TexImage>("glTexImage2D");
        var genFrames = Get<Gen>("glGenFramebuffers"); var deleteFrames = Get<Delete>("glDeleteFramebuffers");
        var bindFrame = Get<Bind>("glBindFramebuffer"); var attach = Get<Attach>("glFramebufferTexture2D");
        var status = Get<Status>("glCheckFramebufferStatus"); var allocate = Get<Allocate>("glBufferData");
        var read = Get<Read>("glReadPixels"); var store = Get<Store>("glPixelStorei");
        lease.GrContext!.Flush();
        _integer(0x8CAA, out var oldRead); _integer(0x8CA6, out var oldDraw);
        _integer(0x8069, out var oldTexture); _integer(0x88ED, out var oldPack);
        _integer(0x84E0, out var oldActiveTexture);
        _integer(0x88EF, out var oldUnpack);
        _integer(0x0D05, out var alignment); _integer(0x0D02, out var rowLength);
        _integer(0x0D03, out var skipRows); _integer(0x0D04, out var skipPixels);
        uint texture = 0, framebuffer = 0;
        try
        {
            _bind(0x88EC, 0); // Null texture data must not address an unpack buffer.
            genTextures(1, out texture); bindTexture(Texture, texture);
            texImage(Texture, 0, 0x8058, _width, _height, 0, 0x1908, 0x1401, IntPtr.Zero);
            genFrames(1, out framebuffer); bindFrame(Framebuffer, framebuffer);
            attach(Framebuffer, 0x8CE0, Texture, texture, 0);
            if (status(Framebuffer) != 0x8CD5) throw new NotSupportedException("Checkpoint framebuffer incomplete");
            using (var target = new GRBackendRenderTarget(_width, _height, 0, 0, new GRGlFramebufferInfo(framebuffer, 0x8058)))
            using (var surface = SKSurface.Create(lease.GrContext, target, GRSurfaceOrigin.BottomLeft, SKColorType.Rgba8888))
            {
                if (surface is null) throw new NotSupportedException("Checkpoint staging surface unavailable");
                lease.GrContext.ResetContext();
                surface.Canvas.Clear(SKColors.Transparent);
                using var paint = new SKPaint { BlendMode = SKBlendMode.Src };
                surface.Canvas.DrawImage(image, 0, 0, paint);
                lease.GrContext.Flush();
                bindFrame(Framebuffer, framebuffer);
                Get<Gen>("glGenBuffers")(1, out _buffer); _bind(PackBuffer, _buffer);
                allocate(PackBuffer, checked(_width * _height * 4), IntPtr.Zero, 0x88E1);
                store(0x0D05, 4); store(0x0D02, 0); store(0x0D03, 0); store(0x0D04, 0);
                read(0, 0, _width, _height, 0x1908, 0x1401, IntPtr.Zero);
                _fence = NativeCanvasCheckpointFence.Create(_context, lease.GrContext)
                    ?? throw new NotSupportedException("Checkpoint transfer fence unavailable");
            }
        }
        finally
        {
            _bind(PackBuffer, (uint)oldPack); _bind(0x88EC, (uint)oldUnpack);
            store(0x0D05, alignment); store(0x0D02, rowLength); store(0x0D03, skipRows); store(0x0D04, skipPixels);
            bindFrame(0x8CA8, (uint)oldRead); bindFrame(0x8CA9, (uint)oldDraw);
            Get<Active>("glActiveTexture")((uint)oldActiveTexture);
            bindTexture(Texture, (uint)oldTexture);
            if (framebuffer != 0) deleteFrames(1, ref framebuffer);
            if (texture != 0) deleteTextures(1, ref texture);
            lease.GrContext.ResetContext();
        }
    }

    internal bool IsReady() => _fence!.IsReady();

    // Only called once readiness was observed. EnsureCurrent serializes the
    // short map/copy/unmap with Avalonia's context use; encoding holds no GL lock.
    internal SKImage ReadOnWorker()
    {
        using var current = _context.EnsureCurrent();
        _integer(0x88ED, out var oldPack);
        _bind(PackBuffer, _buffer);
        try
        {
            var address = _map(PackBuffer, 0, checked(_width * _height * 4), 1);
            if (address == IntPtr.Zero) throw new InvalidOperationException("Checkpoint buffer map failed");
            using var bitmap = new SKBitmap(new SKImageInfo(_width, _height, SKColorType.Rgba8888, SKAlphaType.Premul));
            try
            {
                // GL packs bottom row first; the checkpoint bitmap is top-down.
                for (var row = 0; row < _height; row++)
                    Buffer.MemoryCopy((byte*)address + (_height - row - 1) * _width * 4,
                        (byte*)bitmap.GetPixels() + row * bitmap.RowBytes, bitmap.RowBytes, _width * 4);
            }
            finally
            {
                if (_unmap(PackBuffer) == 0) throw new InvalidOperationException("Checkpoint buffer contents became invalid");
            }
            return SKImage.FromBitmap(bitmap);
        }
        finally { _bind(PackBuffer, (uint)oldPack); }
    }

    public void Dispose()
    {
        if (_buffer == 0 && _fence is null) return;
        try
        {
            using var current = _context.EnsureCurrent();
            _fence?.Dispose();
            if (_buffer != 0) _delete(1, ref _buffer);
        }
        catch (global::Avalonia.Platform.PlatformGraphicsContextLostException) { }
        catch (ObjectDisposedException) { }
        finally { _fence = null; _buffer = 0; }
    }
}

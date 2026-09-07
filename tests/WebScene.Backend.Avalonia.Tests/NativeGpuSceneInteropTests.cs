using System.Runtime.InteropServices;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Avalonia;
using Xunit;
using SkiaSharp;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeGpuSceneInteropTests
{
    private sealed class NativeRuntimeFactAttribute : FactAttribute
    {
        public NativeRuntimeFactAttribute()
        {
            if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")))
                Skip = "Set WEBSCENE_TEST_NATIVE_LIBRARY to verify native GPU scene ABI integration.";
        }
    }

    private sealed class IOSurfaceFixtureFactAttribute : FactAttribute
    {
        public IOSurfaceFixtureFactAttribute()
        {
            if (!OperatingSystem.IsMacOS() ||
                string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")) ||
                string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")))
                Skip = "Requires macOS native runtime and IOSurface fixture libraries.";
        }
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte CreateIOSurface(out NativeGpuImageLeaseV3 image);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate byte IOSurfaceAlive();

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void EndCgl();

    [IOSurfaceFixtureFact]
    public void NativeIOSurfaceImportsIntoCurrentCglRectangleTexture()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var library = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")!);
        var create = Marshal.GetDelegateForFunctionPointer<CreateIOSurface>(
            NativeLibrary.GetExport(library, "webscene_test_create_dawn_iosurface"));
        var begin = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_begin_cgl"));
        var bound = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_cgl_image_bound"));
        var copy = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_cgl_copy"));
        var alive = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_iosurface_alive"));
        var pixels = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_cgl_pixels"));
        var end = Marshal.GetDelegateForFunctionPointer<EndCgl>(NativeLibrary.GetExport(library, "webscene_test_end_cgl"));
        Assert.Equal(1, create(out var image));
        Assert.Equal(NativeSceneAcquireStatus.Success, NativeGpuImageConsumerV3.Acquire(image, out var consumer));
        image.Dispose();
        Assert.NotNull(consumer);
        var completed = false;
        try
        {
            Assert.Equal(1, begin());
            Assert.True(NativeMacOSGpuImageImport.TryBindCurrentRectangleTexture(consumer));
            Assert.Equal(1, bound());
            Assert.Equal(1, copy());
            Assert.Equal(1, alive());
            var openGl = NativeLibrary.Load("/System/Library/Frameworks/OpenGL.framework/OpenGL");
            var fence = NativeMacOSGpuConsumerFence.Create(name => NativeLibrary.GetExport(openGl, name), consumer);
            Exception? wrongThreadError = null;
            var wrongThread = new Thread(() =>
            {
                try { fence.TryComplete(); }
                catch (Exception error) { wrongThreadError = error; }
            });
            wrongThread.Start();
            wrongThread.Join();
            Assert.IsType<InvalidOperationException>(wrongThreadError);
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (!(completed = fence.TryComplete()) && DateTime.UtcNow < deadline) Thread.Sleep(1);
            Assert.True(completed);
            Assert.Equal(0, alive()); // Native source owner retires only after its GPU read.
            Assert.Equal(1, pixels()); // Read destination only, after fence completion.
            Assert.True(fence.TryComplete()); // Retired polling is idempotent.
            Assert.Throws<InvalidOperationException>(consumer.Complete);
        }
        finally
        {
            end(); // Delete GL references before completing the native image consumer.
            if (!completed) consumer.Complete(); // Fixture cleanup drained GL before release.
        }
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void GetGlInteger(uint name, out int value);

    [IOSurfaceFixtureFact]
    public void DawnIOSurfaceComposesDirectlyInPinnedSkiaGanesh()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var library = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")!);
        var create = Marshal.GetDelegateForFunctionPointer<CreateIOSurface>(
            NativeLibrary.GetExport(library, "webscene_test_create_dawn_iosurface"));
        var begin = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_begin_cgl"));
        var end = Marshal.GetDelegateForFunctionPointer<EndCgl>(NativeLibrary.GetExport(library, "webscene_test_end_cgl"));
        var alive = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_iosurface_alive"));
        Assert.Equal(1, create(out var image));
        Assert.Equal(NativeSceneAcquireStatus.Success, NativeGpuImageConsumerV3.Acquire(image, out var consumer));
        image.Dispose();
        Assert.NotNull(consumer);
        var completed = false;
        try
        {
            Assert.Equal(1, begin());
            Assert.True(NativeMacOSGpuImageImport.TryBindCurrentRectangleTexture(consumer));
            var openGl = NativeLibrary.Load("/System/Library/Frameworks/OpenGL.framework/OpenGL");
            IntPtr Resolve(string name) => NativeLibrary.TryGetExport(openGl, name, out var address) ? address : IntPtr.Zero;
            var getInteger = Marshal.GetDelegateForFunctionPointer<GetGlInteger>(Resolve("glGetIntegerv"));
            getInteger(0x84F6, out var texture); // GL_TEXTURE_BINDING_RECTANGLE
            Assert.NotEqual(0, texture);
            using var gl = GRGlInterface.CreateOpenGl(Resolve);
            Assert.NotNull(gl);
            using var context = GRContext.CreateGl(gl);
            Assert.NotNull(context);
            using var target = SKSurface.Create(context, false,
                new SKImageInfo(24, 8, SKColorType.Rgba8888, SKAlphaType.Premul));
            Assert.NotNull(target);
            target.Canvas.Clear(SKColors.Blue);
            using (var source = NativeMacOSGpuImageImport.TryWrapRectangleTexture(consumer,
                context, (uint)texture, GRSurfaceOrigin.TopLeft, SKAlphaType.Premul))
            {
                Assert.NotNull(source);
                Assert.True(source.IsTextureBacked);
                using var paint = new SKPaint { Color = new SKColor(255, 255, 255, 128) };
                target.Canvas.Save();
                target.Canvas.ClipRect(new SKRect(3, 2, 20, 6));
                target.Canvas.DrawImage(source, 1, 1, paint);
                target.Canvas.Restore();
            }
            context.Flush(submit: true, synchronous: false); // Submit Skia's reads before inserting the host completion fence.
            var fence = NativeMacOSGpuConsumerFence.Create(Resolve, consumer);
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (!(completed = fence.TryComplete()) && DateTime.UtcNow < deadline) Thread.Sleep(1);
            Assert.True(completed);
            Assert.Equal(0, alive());
            // Diagnostic destination readback only, after all source GPU reads have retired.
            using var pixels = new SKBitmap(new SKImageInfo(24, 8, SKColorType.Rgba8888, SKAlphaType.Premul));
            Assert.True(target.ReadPixels(pixels.Info, pixels.GetPixels(), pixels.RowBytes, 0, 0));
            for (var y = 0; y < 8; ++y)
                for (var x = 0; x < 24; ++x)
                {
                    var color = pixels.GetPixel(x, y);
                    var painted = x >= 3 && x < 18 && y >= 2 && y < 5;
                    Assert.InRange((int)color.Red, painted ? 25 : 0, painted ? 27 : 0);
                    Assert.InRange((int)color.Green, painted ? 50 : 0, painted ? 52 : 0);
                    Assert.InRange((int)color.Blue, painted ? 203 : 255, painted ? 205 : 255);
                    Assert.Equal(255, color.Alpha);
                }
        }
        finally
        {
            end(); // Diagnostic failure cleanup drains GPU work before releasing the provider.
            if (!completed) consumer.Complete();
        }
    }

    [IOSurfaceFixtureFact]
    public void IOSurfaceImportDefersConcurrentCompletionUntilBorrowReturns()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        // Keep fixture code loaded: native provider vtables may outlive this method
        // if a test fails. Unloading code before releasing a provider is unsafe.
        var library = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")!);
        var create = Marshal.GetDelegateForFunctionPointer<CreateIOSurface>(
            NativeLibrary.GetExport(library, "webscene_test_create_iosurface"));
        var alive = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(
            NativeLibrary.GetExport(library, "webscene_test_iosurface_alive"));
        Assert.Equal(1, create(out var image));
        Assert.Equal(NativeSceneAcquireStatus.Success, NativeGpuImageConsumerV3.Acquire(image, out var consumer));
        image.Dispose();
        Assert.NotNull(consumer);
        Assert.True(consumer.WithIOSurface(view =>
        {
            Assert.NotEqual(IntPtr.Zero, view.BorrowedIOSurface);
            Assert.True(view.AllocationBytes >= 17 * 4 * 4);
            Task.Run(consumer.Complete).GetAwaiter().GetResult();
            Assert.Equal(1, alive()); // Completion must not free the borrowed native object.
            Assert.Throws<InvalidOperationException>(() => consumer.WithIOSurface(_ => { }));
        }));
        Assert.Equal(0, alive());
        Assert.Throws<InvalidOperationException>(consumer.Complete);

        Assert.Equal(1, create(out image));
        Assert.Equal(NativeSceneAcquireStatus.Success, NativeGpuImageConsumerV3.Acquire(image, out var retry));
        image.Dispose();
        Assert.NotNull(retry);
        Assert.Throws<ApplicationException>(() => retry.WithIOSurface(_ => throw new ApplicationException("Import failed")));
        Assert.Equal(1, alive()); // An importer exception cannot certify GPU completion.
        Assert.True(retry.WithIOSurface(_ => { }));
        retry.Complete();
        Assert.Equal(0, alive());
    }

    [Fact]
    public void LayoutMatchesNativeSceneAndImageAbi()
    {
        Assert.Equal(16, Marshal.SizeOf<NativeSceneAcquireOptionsV3>());
        Assert.Equal(16 + 2 * IntPtr.Size, Marshal.SizeOf<NativeSceneViewV3>());
        Assert.Equal(80, Marshal.SizeOf<NativeGpuImageInfoV3>());
        Assert.Equal(24, Marshal.SizeOf<NativeGpuIOSurfaceViewV3>());
        Assert.Equal(8, Marshal.OffsetOf<NativeGpuIOSurfaceViewV3>(nameof(NativeGpuIOSurfaceViewV3.BorrowedIOSurface)).ToInt32());
        Assert.Equal(16, Marshal.OffsetOf<NativeGpuIOSurfaceViewV3>(nameof(NativeGpuIOSurfaceViewV3.AllocationBytes)).ToInt32());
        Assert.Equal(8, Marshal.OffsetOf<NativeGpuImageInfoV3>(nameof(NativeGpuImageInfoV3.Canvas)).ToInt32());
        Assert.Equal(56, Marshal.OffsetOf<NativeGpuImageInfoV3>(nameof(NativeGpuImageInfoV3.Width)).ToInt32());
        Assert.Equal(0UL, NativeSceneAcquireOptionsV3.CpuOnly.ConsumerCapabilities);
    }

    [NativeRuntimeFact]
    public void IOSurfaceLookupRejectsAbsentConsumerAcrossNativeBoundary()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var view = NativeGpuIOSurfaceViewV3.Empty;
        view.BorrowedIOSurface = new IntPtr(123);
        view.AllocationBytes = 456;
        Assert.Equal(0, NativeWebSceneApi.GpuImageGetIOSurfaceV3(IntPtr.Zero, ref view));
        Assert.Equal(IntPtr.Zero, view.BorrowedIOSurface);
        Assert.Equal(0UL, view.AllocationBytes);
    }

    [IOSurfaceFixtureFact]
    public void UnappliedSceneImagesReleaseWithoutAGraphicsContext()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var library = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")!);
        var create = Marshal.GetDelegateForFunctionPointer<CreateIOSurface>(NativeLibrary.GetExport(library, "webscene_test_create_iosurface"));
        var alive = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_iosurface_alive"));
        Assert.Equal(1, create(out var source));
        Assert.Equal(NativeSceneAcquireStatus.Success, NativeMacOSGpuSceneImages.Retain(new[] { source }, out var images));
        source.Dispose();
        Assert.Equal(1, alive());
        images!.DiscardUnprepared();
        images.DiscardUnprepared();
        Assert.Equal(0, alive());
        Assert.Throws<InvalidOperationException>(() => new NativeMacOSGpuScenePresenter().TryReplace(images));
    }

    [IOSurfaceFixtureFact]
    public void SceneImageCaptureRollsBackEarlierRetainsWhenALaterImageIsDisposed()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var library = NativeLibrary.Load(Environment.GetEnvironmentVariable("WEBSCENE_TEST_GPU_FIXTURE_LIBRARY")!);
        var create = Marshal.GetDelegateForFunctionPointer<CreateIOSurface>(NativeLibrary.GetExport(library, "webscene_test_create_iosurface"));
        var alive = Marshal.GetDelegateForFunctionPointer<IOSurfaceAlive>(NativeLibrary.GetExport(library, "webscene_test_iosurface_alive"));
        Assert.Equal(1, create(out var image));
        try
        {
            Assert.Equal(NativeSceneAcquireStatus.Success, image.Retain(out var disposed));
            disposed!.Dispose();
            Assert.Throws<ObjectDisposedException>(() => NativeMacOSGpuSceneImages.Retain(new[] { image, disposed }, out _));
        }
        finally { image.Dispose(); }
        Assert.Equal(0, alive());
    }

    [NativeRuntimeFact]
    public void VersionedAcquisitionCrossesManagedNativeBoundary()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var options = NativeSceneAcquireOptionsV3.CpuOnly;
        Assert.Equal(NativeSceneAcquireStatus.InvalidArgument,
            NativeWebSceneApi.AcquireLatestSceneV3(IntPtr.Zero, in options, out var scene));
        Assert.Equal(IntPtr.Zero, scene);
        var engine = NativeWebSceneApi.EngineCreate(0, null, new AvaloniaResourceLoader(), _ => { });
        try
        {
            options.SceneVersion = 99;
            Assert.Equal(NativeSceneAcquireStatus.UnsupportedVersion,
                NativeWebSceneApi.AcquireNextSceneV3(engine, in options, out scene));
            Assert.Equal(IntPtr.Zero, scene);
            options = NativeSceneAcquireOptionsV3.CpuOnly;
            var deadline = DateTime.UtcNow.AddSeconds(5);
            NativeSceneAcquireStatus status;
            do
            {
                status = NativeWebSceneApi.AcquireNextSceneV3(engine, in options, out scene);
                if (status == NativeSceneAcquireStatus.Empty) Thread.Sleep(1);
            } while (status == NativeSceneAcquireStatus.Empty && DateTime.UtcNow < deadline);
            Assert.Equal(NativeSceneAcquireStatus.Success, status);
            var view = Marshal.PtrToStructure<NativeSceneViewV3>(scene);
            Assert.Equal(3U, view.SceneVersion);
            Assert.NotEqual(IntPtr.Zero, view.CpuView);
            Assert.Equal(0U, NativeWebSceneApi.SceneGpuImageCountV3(scene));
            Assert.Equal(NativeSceneAcquireStatus.InvalidArgument,
                NativeWebSceneApi.SceneRetainGpuImageV3(scene, 0, out var image));
            Assert.Equal(IntPtr.Zero, image);
            Assert.Equal(1, NativeWebSceneApi.SceneAcknowledgeV3(scene));
            NativeWebSceneApi.EngineDestroy(engine); engine = IntPtr.Zero;
            Assert.Equal(view.CpuView, Marshal.PtrToStructure<NativeSceneViewV3>(scene).CpuView);
        }
        finally
        {
            if (scene != IntPtr.Zero) NativeWebSceneApi.SceneReleaseV3(scene);
            if (engine != IntPtr.Zero) NativeWebSceneApi.EngineDestroy(engine);
        }
    }
    [NativeRuntimeFact]
    public void SafeSceneLeaseProtectsBorrowedViewDuringConcurrentDispose()
    {
        NativeWebSceneApi.ConfigureLibraryPath(Environment.GetEnvironmentVariable("WEBSCENE_TEST_NATIVE_LIBRARY")!);
        var options = NativeSceneAcquireOptionsV3.CpuOnly;
        Assert.Equal(NativeSceneAcquireStatus.InvalidArgument,
            NativeSceneLeaseV3.Acquire(IntPtr.Zero, in options, true, out var absent));
        Assert.Null(absent);
        var engine = NativeWebSceneApi.EngineCreate(0, null, new AvaloniaResourceLoader(), _ => { });
        NativeSceneLeaseV3? lease = null;
        try
        {
            var deadline = DateTime.UtcNow.AddSeconds(5);
            NativeSceneAcquireStatus status;
            do
            {
                status = NativeSceneLeaseV3.Acquire(engine, in options, true, out lease);
                if (status == NativeSceneAcquireStatus.Empty) Thread.Sleep(1);
            } while (status == NativeSceneAcquireStatus.Empty && DateTime.UtcNow < deadline);
            Assert.Equal(NativeSceneAcquireStatus.Success, status);
            Assert.NotNull(lease);
            Assert.Equal(0U, lease.ImageCount);
            Assert.Equal(NativeSceneAcquireStatus.InvalidArgument,
                NativeGpuImageLeaseV3.Acquire(lease, 0, out var absentImage));
            Assert.Null(absentImage);
            Assert.True(lease.Acknowledge());
            NativeWebSceneApi.EngineDestroy(engine); engine = IntPtr.Zero;
            lease.WithView(view =>
            {
                Task.Run(lease.Dispose).GetAwaiter().GetResult();
                // This read happens after Dispose on another thread. WithView
                // still owns a SafeHandle reference until the callback returns.
                Assert.Equal(2, Marshal.ReadInt32(view.CpuView, sizeof(uint)));
            });
            lease.Dispose(); // Idempotent: never double-release the native lease.
            Assert.Throws<ObjectDisposedException>(() => lease.WithView(_ => { }));
            Assert.Throws<ObjectDisposedException>(() => NativeGpuImageLeaseV3.Acquire(lease, 0, out _));
        }
        finally
        {
            lease?.Dispose();
            if (engine != IntPtr.Zero) NativeWebSceneApi.EngineDestroy(engine);
        }
    }

}

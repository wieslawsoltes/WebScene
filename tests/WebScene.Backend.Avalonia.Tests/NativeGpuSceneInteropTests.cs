using System.Runtime.InteropServices;
using WebScene.Backends.Avalonia.Native;
using WebScene.Backends.Avalonia;
using Xunit;

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

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using WebScene.Backends.Avalonia;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;

namespace WebScene.NativeEngine.Benchmarks;

internal static unsafe class NativeResourceBridgeProbe
{
    internal static int Run(string[] args)
    {
        var payloadBytes = ReadIntOption(args, "--payload-bytes", 32 * 1024);
        var iterations = ReadIntOption(args, "--iterations", 200);
        var samples = ReadIntOption(args, "--samples", 11);
        var archiveDirectory = ReadStringOption(args, "--archive");
        var archiveUrl = ReadStringOption(args, "--url");
        var archiveIterations = ReadIntOption(args, "--archive-iterations", 10);
        if (payloadBytes is < 1 or > 4 * 1024 * 1024
            || iterations is < 1 or > 100_000
            || samples is < 3 or > 101
            || archiveIterations is < 1 or > 10_000)
        {
            throw new ArgumentOutOfRangeException(
                nameof(args),
                "Payload bytes must be 1-4194304, iterations must be 1-100000, " +
                "and samples must be 3-101.");
        }

        using var probeThenCopy = new BridgeFixture(payloadBytes);
        using var speculativeCopy = new BridgeFixture(payloadBytes);
        probeThenCopy.Validate(probeFirst: true);
        speculativeCopy.Validate(probeFirst: false);
        probeThenCopy.WarmUp(probeFirst: true);
        speculativeCopy.WarmUp(probeFirst: false);

        var probeTiming = Measure(
            () => probeThenCopy.Transfer(probeFirst: true),
            iterations,
            samples);
        var speculativeTiming = Measure(
            () => speculativeCopy.Transfer(probeFirst: false),
            iterations,
            samples);
        var archiveReplay = archiveDirectory is not null && archiveUrl is not null
            ? MeasureArchiveReplay(
                archiveDirectory,
                archiveUrl,
                archiveIterations,
                samples)
            : null;

        Console.WriteLine(JsonSerializer.Serialize(
            new
            {
                schema = "webscene-native-resource-bridge-v1",
                capturedUtc = DateTimeOffset.UtcNow,
                options = new { payloadBytes, iterations, samples },
                correctness = new { envelopeValidated = true },
                probeThenCopy = probeTiming,
                speculativeCopy = speculativeTiming,
                archiveReplay
            },
            new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }

    private static TimingSummary Measure(
        Action operation,
        int iterations,
        int samples)
    {
        var nanosecondsPerOperation = new double[samples];
        var allocatedBytesPerOperation = new long[samples];
        for (var sample = 0; sample < samples; sample++)
        {
            var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
            var started = Stopwatch.GetTimestamp();
            for (var iteration = 0; iteration < iterations; iteration++)
            {
                operation();
            }
            nanosecondsPerOperation[sample] =
                Stopwatch.GetElapsedTime(started).TotalNanoseconds / iterations;
            allocatedBytesPerOperation[sample] =
                (GC.GetAllocatedBytesForCurrentThread() - allocatedBefore) / iterations;
        }
        Array.Sort(nanosecondsPerOperation);
        Array.Sort(allocatedBytesPerOperation);
        return new TimingSummary(
            nanosecondsPerOperation[samples / 2],
            Percentile(nanosecondsPerOperation, 0.95),
            nanosecondsPerOperation.Average(),
            allocatedBytesPerOperation[samples / 2]);
    }

    private static double Percentile(IReadOnlyList<double> sorted, double percentile)
    {
        var index = (int)Math.Ceiling(percentile * sorted.Count) - 1;
        return sorted[Math.Clamp(index, 0, sorted.Count - 1)];
    }

    private static int ReadIntOption(
        IReadOnlyList<string> args,
        string name,
        int fallback)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(args[index + 1], out var value))
            {
                return value;
            }
        }
        return fallback;
    }

    private static string? ReadStringOption(
        IReadOnlyList<string> args,
        string name)
    {
        for (var index = 0; index + 1 < args.Count; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase))
            {
                return args[index + 1];
            }
        }
        return null;
    }

    private static ArchiveReplayTiming MeasureArchiveReplay(
        string archiveDirectory,
        string address,
        int iterations,
        int samples)
    {
        var loader = new AvaloniaResourceLoader
        {
            ResourceReplayDirectory = archiveDirectory
        };
        loader.PrepareResourceReplay();
        var request = new WebSceneResourceRequest(
            address,
            null,
            WebSceneResourceKind.Script);
        var text = loader.LoadText(request);
        if (!loader.TryLoadUtf8(request, out var utf8)
            || Encoding.UTF8.GetByteCount(text.Content) != utf8.Content.Length)
        {
            throw new InvalidOperationException(
                "The replay archive's text and UTF-8 paths do not match.");
        }

        var observedLength = 0;
        var textTiming = Measure(
            () => observedLength = loader.LoadText(request).Content.Length,
            iterations,
            samples);
        var utf8Timing = Measure(
            () =>
            {
                if (!loader.TryLoadUtf8(request, out var resource))
                {
                    throw new InvalidOperationException("UTF-8 replay unexpectedly fell back.");
                }
                observedLength = resource.Content.Length;
            },
            iterations,
            samples);
        using var textBridge = new ArchiveBridgeFixture(
            new TextOnlyLoader(loader),
            address,
            utf8.Content.Length);
        using var utf8Bridge = new ArchiveBridgeFixture(
            loader,
            address,
            utf8.Content.Length);
        textBridge.Validate();
        utf8Bridge.Validate();
        var textBridgeTiming = Measure(
            () => textBridge.Transfer(),
            iterations,
            samples);
        var utf8BridgeTiming = Measure(
            () => utf8Bridge.Transfer(),
            iterations,
            samples);
        GC.KeepAlive(observedLength);
        return new ArchiveReplayTiming(
            utf8.Content.Length,
            textTiming,
            utf8Timing,
            textBridgeTiming,
            utf8BridgeTiming);
    }

    private sealed class BridgeFixture : IDisposable
    {
        private const int HeaderSize = 2 + sizeof(uint) + sizeof(long) + sizeof(long);
        private readonly FixedResourceLoader _loader;
        private readonly NativeWebSceneApi.ResourceBridge _bridge;
        private readonly IntPtr _destination;
        private readonly nuint _capacity;
        private readonly nuint _expectedLength;

        internal BridgeFixture(int payloadBytes)
        {
            _loader = new FixedResourceLoader(new string('x', payloadBytes));
            _bridge = new NativeWebSceneApi.ResourceBridge(
                _loader,
                _ => { },
                null,
                null,
                null);
            _expectedLength = checked((nuint)(
                HeaderSize + payloadBytes + Encoding.UTF8.GetByteCount("resource-tag")));
            _capacity = checked((nuint)(HeaderSize + payloadBytes + 64));
            _destination = (IntPtr)NativeMemory.Alloc(_capacity);
        }

        internal void WarmUp(bool probeFirst)
        {
            for (var iteration = 0; iteration < 20; iteration++)
            {
                Transfer(probeFirst);
            }
        }

        internal nuint Transfer(bool probeFirst)
        {
            var required = probeFirst
                ? _bridge.Copy(
                    1,
                    "https://example.test/library.js",
                    "resource-tag",
                    0,
                    IntPtr.Zero,
                    0)
                : _capacity;
            return _bridge.Copy(
                1,
                "https://example.test/library.js",
                "resource-tag",
                0,
                _destination,
                required);
        }

        internal void Validate(bool probeFirst)
        {
            var written = Transfer(probeFirst);
            if (written != _expectedLength
                || Marshal.ReadByte(_destination) != 1
                || Marshal.ReadByte(_destination, 1) != 1
                || _loader.LoadCount != 1)
            {
                throw new InvalidOperationException(
                    "The resource bridge emitted an invalid response envelope.");
            }
            _loader.Reset();
        }

        public void Dispose()
        {
            _bridge.Dispose();
            NativeMemory.Free((void*)_destination);
        }
    }

    private sealed class FixedResourceLoader(string content) : IWebSceneResourceLoader
    {
        internal int LoadCount { get; private set; }

        public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
        {
            LoadCount++;
            return new WebSceneTextResource(
                request.Specifier,
                content,
                request.Specifier,
                null)
            {
                EntityTag = "resource-tag",
                IsCacheable = true
            };
        }

        internal void Reset() => LoadCount = 0;
    }

    private sealed class TextOnlyLoader(AvaloniaResourceLoader inner)
        : IWebSceneResourceLoader
    {
        public WebSceneTextResource LoadText(in WebSceneResourceRequest request)
            => inner.LoadText(request);
    }

    private sealed class ArchiveBridgeFixture : IDisposable
    {
        private readonly NativeWebSceneApi.ResourceBridge _bridge;
        private readonly string _address;
        private readonly IntPtr _destination;
        private readonly nuint _capacity;

        internal ArchiveBridgeFixture(
            IWebSceneResourceLoader loader,
            string address,
            int contentLength)
        {
            _bridge = new NativeWebSceneApi.ResourceBridge(
                loader,
                _ => { },
                null,
                null,
                null);
            _address = address;
            _capacity = checked((nuint)(contentLength + 1024));
            _destination = (IntPtr)NativeMemory.Alloc(_capacity);
        }

        internal nuint Transfer()
            => _bridge.Copy(
                1,
                _address,
                null,
                0,
                _destination,
                _capacity);

        internal void Validate()
        {
            var written = Transfer();
            if (written <= 1024 || written > _capacity)
            {
                throw new InvalidOperationException(
                    "The archive resource bridge emitted an invalid envelope length.");
            }
        }

        public void Dispose()
        {
            _bridge.Dispose();
            NativeMemory.Free((void*)_destination);
        }
    }

    private sealed record TimingSummary(
        double MedianNanoseconds,
        double P95Nanoseconds,
        double MeanNanoseconds,
        long MedianAllocatedBytes);

    private sealed record ArchiveReplayTiming(
        int ContentBytes,
        TimingSummary LoadText,
        TimingSummary LoadUtf8,
        TimingSummary BridgeText,
        TimingSummary BridgeUtf8);
}

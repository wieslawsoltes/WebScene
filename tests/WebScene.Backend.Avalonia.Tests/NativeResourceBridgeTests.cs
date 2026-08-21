using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Text;
using WebScene.Backends.Avalonia.Native;
using WebScene.Core;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed unsafe class NativeResourceBridgeTests
{
    [Fact]
    public void SufficientDestinationWritesEnvelopeWithoutAProbe()
    {
        var loader = new FixedResourceLoader("const answer = 'λ';");
        using var bridge = CreateBridge(loader);
        var destination = NativeMemory.Alloc(1024);
        try
        {
            var written = bridge.Copy(
                1,
                "https://example.test/library.js",
                null,
                0,
                (IntPtr)destination,
                1024);

            AssertEnvelope(destination, written, "const answer = 'λ';");
            Assert.Equal(1, loader.LoadCount);
        }
        finally
        {
            NativeMemory.Free(destination);
        }
    }

    [Fact]
    public void SizeProbeAndCopyLoadTheResourceOnlyOnce()
    {
        var loader = new FixedResourceLoader("export default 42;");
        using var bridge = CreateBridge(loader);

        var required = bridge.Copy(
            1,
            "https://example.test/module.js",
            "request-tag",
            123,
            IntPtr.Zero,
            0);
        var destination = NativeMemory.Alloc(required);
        try
        {
            var written = bridge.Copy(
                1,
                "https://example.test/module.js",
                "request-tag",
                123,
                (IntPtr)destination,
                required);

            Assert.Equal(required, written);
            AssertEnvelope(destination, written, "export default 42;");
            Assert.Equal(1, loader.LoadCount);
        }
        finally
        {
            NativeMemory.Free(destination);
        }
    }

    [Fact]
    public void ShortSpeculativeDestinationRetainsResponseForExactRetry()
    {
        var loader = new FixedResourceLoader(new string('x', 4096));
        using var bridge = CreateBridge(loader);
        var shortDestination = NativeMemory.Alloc(64);
        try
        {
            var required = bridge.Copy(
                1,
                "https://example.test/large.js",
                null,
                0,
                (IntPtr)shortDestination,
                64);
            Assert.True(required > 64);

            var exactDestination = NativeMemory.Alloc(required);
            try
            {
                var written = bridge.Copy(
                    1,
                    "https://example.test/large.js",
                    null,
                    0,
                    (IntPtr)exactDestination,
                    required);
                AssertEnvelope(exactDestination, written, new string('x', 4096));
                Assert.Equal(1, loader.LoadCount);
            }
            finally
            {
                NativeMemory.Free(exactDestination);
            }
        }
        finally
        {
            NativeMemory.Free(shortDestination);
        }
    }

    private static NativeWebSceneApi.ResourceBridge CreateBridge(IWebSceneResourceLoader loader)
        => new(loader, _ => { }, null, null, null);

    private static void AssertEnvelope(void* source, nuint length, string expectedContent)
    {
        var bytes = new ReadOnlySpan<byte>(source, checked((int)length));
        Assert.Equal(1, bytes[0]);
        Assert.Equal(1, bytes[1]);
        var tagLength = BinaryPrimitives.ReadUInt32LittleEndian(bytes[2..]);
        const int headerLength = 2 + sizeof(uint) + sizeof(long) + sizeof(long);
        Assert.Equal(
            "response-tag",
            Encoding.UTF8.GetString(bytes.Slice(headerLength, checked((int)tagLength))));
        Assert.Equal(
            expectedContent,
            Encoding.UTF8.GetString(bytes[(headerLength + checked((int)tagLength))..]));
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
                EntityTag = "response-tag",
                IsCacheable = true
            };
        }
    }
}

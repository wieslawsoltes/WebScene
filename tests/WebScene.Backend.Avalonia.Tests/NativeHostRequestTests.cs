using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class NativeHostRequestTests
{
    [Fact]
    public void BlobBackedPngDownloadPreservesFileNameAndBytes()
    {
        var parsed = NativeHostRequest.TryGetDownload(
            """{"kind":"download","suggestedFileName":"chart.png","url":"data:image/png;base64,iVBORw=="}""",
            out var download);

        Assert.True(parsed);
        Assert.Equal("chart.png", download.SuggestedFileName);
        Assert.Equal("image/png", download.ContentType);
        Assert.Equal(new byte[] { 137, 80, 78, 71 }, download.Bytes);
        Assert.Null(download.RemoteUri);
        Assert.Null(download.CanvasNodeId);
    }

    [Fact]
    public void DownloadFileNameCannotEscapeTheSavePicker()
    {
        var parsed = NativeHostRequest.TryGetDownload(
            """{"kind":"download","suggestedFileName":"../../chart.png","url":"data:image/png;base64,iVBORw=="}""",
            out var download);

        Assert.True(parsed);
        Assert.Equal("chart.png", download.SuggestedFileName);
    }

    [Fact]
    public void CanvasDownloadRequestsHostRenderedPng()
    {
        var parsed = NativeHostRequest.TryGetDownload(
            """{"kind":"download","suggestedFileName":"chart.png","canvasNodeId":42}""",
            out var download);

        Assert.True(parsed);
        Assert.Equal("chart.png", download.SuggestedFileName);
        Assert.Equal("image/png", download.ContentType);
        Assert.Equal(42u, download.CanvasNodeId);
        Assert.Null(download.Bytes);
        Assert.Null(download.RemoteUri);
    }

    [Fact]
    public void EmptyPngDownloadIsRejectedInsteadOfCreatingAZeroByteFile()
    {
        var parsed = NativeHostRequest.TryGetDownload(
            """{"kind":"download","suggestedFileName":"chart.png","url":"data:image/png;base64,"}""",
            out _);

        Assert.False(parsed);
    }

    [Fact]
    public void CanvasClipboardWriteRequestsHostRenderedPng()
    {
        var parsed = NativeHostRequest.TryGetClipboardWrite(
            """{"kind":"writeClipboard","contentType":"image/png","canvasNodeId":77}""",
            out var clipboardWrite);

        Assert.True(parsed);
        Assert.Equal("image/png", clipboardWrite.ContentType);
        Assert.Equal(77u, clipboardWrite.CanvasNodeId);
        Assert.Null(clipboardWrite.Bytes);
    }

    [Fact]
    public void ByteClipboardWritePreservesMimeTypeAndBytes()
    {
        var parsed = NativeHostRequest.TryGetClipboardWrite(
            """{"kind":"writeClipboard","contentType":"image/png","url":"data:image/png;base64,iVBORw=="}""",
            out var clipboardWrite);

        Assert.True(parsed);
        Assert.Equal("image/png", clipboardWrite.ContentType);
        Assert.Equal(new byte[] { 137, 80, 78, 71 }, clipboardWrite.Bytes);
        Assert.Null(clipboardWrite.CanvasNodeId);
    }
}

using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class ScreenshotExportCompatibilityTests
{
    private static readonly byte[] PngSignature = [137, 80, 78, 71, 13, 10, 26, 10];

    [AvaloniaFact]
    public void CanvasPngAndDownloadAnchorPreserveBinaryBytesAndSuggestedName()
    {
        var window = new Window { Width = 120, Height = 80, Content = new CssLayoutPanel() };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        try
        {
            var canvas = HostTestUtilities.GetElement(host.Document.createElement("canvas"));
            canvas.width = 4;
            canvas.height = 3;
            var context = Assert.IsType<CanvasRenderingContext2D>(canvas.getContext("2d"));
            context.fillStyle = "#ef4444";
            context.fillRect(0, 0, 4, 3);

            var dataUrl = canvas.__webSceneCanvasToDataURL();
            Assert.StartsWith("data:image/png;base64,", dataUrl, StringComparison.Ordinal);
            var exported = Convert.FromBase64String(dataUrl[(dataUrl.IndexOf(',') + 1)..]);
            Assert.Equal(PngSignature, exported.Take(PngSignature.Length));

            WebSceneDownloadRequestedEventArgs? download = null;
            host.DownloadRequested += (_, args) =>
            {
                args.Handled = true;
                download = args;
            };
            var anchor = HostTestUtilities.GetElement(host.Document.createElement("a"));
            anchor.download = "EURUSD_chart.png";
            anchor.href = dataUrl;
            anchor.click();
            Dispatcher.UIThread.RunJobs();

            Assert.NotNull(download);
            Assert.Equal("EURUSD_chart.png", download!.FileName);
            Assert.Equal("image/png", download.ContentType);
            Assert.Equal(exported, download.Data);
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ClipboardItemAndDownloadMatchCommonScreenshotApis()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window { Width = 120, Height = 80, Content = new CssLayoutPanel() };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        WebSceneDownloadRequestedEventArgs? download = null;
        host.DownloadRequested += (_, args) =>
        {
            args.Handled = true;
            download = args;
        };

        try
        {
            runtime.Execute("""
                globalThis.__webSceneScreenshotCopyState = 'pending';
                globalThis.__webSceneSvgObjectUrlState = 'pending';
                const svgDocument = new DOMParser().parseFromString(
                  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 8"><path fill="#fff" d="M0 0h8v8H0z"/></svg>',
                  'image/svg+xml');
                const serializedSvg = new XMLSerializer().serializeToString(svgDocument);
                const svgUrl = URL.createObjectURL(new Blob([
                  serializedSvg
                ], { type: 'image/svg+xml' }));
                const image = new Image();
                image.onload = function() { globalThis.__webSceneSvgObjectUrlState = 'loaded'; };
                image.onerror = function() { globalThis.__webSceneSvgObjectUrlState = 'failed'; };
                image.src = svgUrl;
                const canvas = document.createElement('canvas');
                canvas.width = 5;
                canvas.height = 4;
                const context = canvas.getContext('2d');
                context.fillStyle = '#2563eb';
                context.fillRect(0, 0, 5, 4);

                canvas.toBlob(function(blob) {
                  const link = document.createElement('a');
                  link.href = URL.createObjectURL(blob);
                  link.download = 'BTCUSD_chart.png';
                  link.click();
                  navigator.clipboard.write([
                    new ClipboardItem({ 'image/png': Promise.resolve(blob) })
                  ]).then(
                    function() { globalThis.__webSceneScreenshotCopyState = 'copied'; },
                    function(error) { globalThis.__webSceneScreenshotCopyState = 'failed:' + error; }
                  );
                });
                """, "screenshot-copy-download-compatibility.js");

            var timeout = Stopwatch.StartNew();
            while (timeout.Elapsed < TimeSpan.FromSeconds(3)
                   && Convert.ToString(runtime.Engine.Evaluate("globalThis.__webSceneScreenshotCopyState")) == "pending")
            {
                Dispatcher.UIThread.RunJobs();
                Thread.Sleep(1);
            }

            Assert.Equal("copied", Convert.ToString(runtime.Engine.Evaluate("globalThis.__webSceneScreenshotCopyState")));
            Assert.Equal("loaded", Convert.ToString(runtime.Engine.Evaluate("globalThis.__webSceneSvgObjectUrlState")));
            var clipboardPng = host.Services.Clipboard.GetData("image/png");
            Assert.NotNull(clipboardPng);
            Assert.Equal(PngSignature, clipboardPng!.Take(PngSignature.Length));
            Assert.NotNull(download);
            Assert.Equal("BTCUSD_chart.png", download!.FileName);
            Assert.Equal(PngSignature, download.Data.Take(PngSignature.Length));
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

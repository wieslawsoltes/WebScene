using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8WhitespaceStaticPositionRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void AuthoredTextSurvivesWhileCollapsedPresentationDefinesStaticPosition()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath)) return;

        var root = new CssLayoutPanel { Width = 120, Height = 40 };
        var window = new Window { Width = 120, Height = 40, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute("""
                const style = document.createElement('style');
                style.textContent = `
                  html, body { height: 40px; margin: 0; padding: 0; width: 120px; }
                  html { font-size: 10px; line-height: 1; }
                  .abspos { height: 10px; position: absolute; width: 10px; }
                `;
                document.head.appendChild(style);
                document.body.innerHTML = `<div><span><span style="margin-right:-10px">
                        x<span class="abspos"></span></span></span></div>`;
                globalThis.__whiteSpaceStaticAbsolute = document.querySelectorAll('.abspos')[0];
                globalThis.__whiteSpaceStaticText = globalThis.__whiteSpaceStaticAbsolute.parentNode.firstChild;
                """, "v8-whitespace-static-position.js");
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
            host.Document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify({nodeValue:globalThis.__whiteSpaceStaticText.nodeValue," +
                "textContent:globalThis.__whiteSpaceStaticText.textContent," +
                "rect:(()=>{const r=globalThis.__whiteSpaceStaticAbsolute.getBoundingClientRect();" +
                "return [r.left,r.top,r.width,r.height]})()})")) ?? "{}");
            var rootResult = result.RootElement;
            Assert.Equal("\n        x", rootResult.GetProperty("nodeValue").GetString());
            Assert.Equal("\n        x", rootResult.GetProperty("textContent").GetString());
            var rect = rootResult.GetProperty("rect");
            Assert.Equal(5, rect[0].GetDouble());
            Assert.Equal(0, rect[1].GetDouble());
            Assert.Equal(10, rect[2].GetDouble());
            Assert.Equal(10, rect[3].GetDouble());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

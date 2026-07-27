using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8IframeIdentityRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ConnectedSrcLessIframeSynchronouslyExposesMutableAboutBlankBodyIdentity()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window
        {
            Width = 320,
            Height = 160,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions
            {
                EnableTrustedSameOriginContextSharing = true
            });
        try
        {
            runtime.Execute("""
                const frameWrapper = document.createElement('div');
                const iframe = document.createElement('iframe');
                frameWrapper.appendChild(iframe);
                document.body.appendChild(frameWrapper);
                const frameWindow = iframe.contentWindow;
                const frameDocument = iframe.contentDocument;
                const state = globalThis.__webSceneIframeIdentity = {
                  hadDocumentSynchronously: !!frameDocument,
                  hadBodySynchronously: !!(frameDocument && frameDocument.body),
                  initialLocation: frameDocument && frameDocument.location.href,
                  contentWindowDocumentSame: frameWindow.document === frameDocument,
                  defaultViewSame: frameDocument.defaultView === frameWindow,
                  frameElementSame: frameWindow.frameElement === iframe
                };

                frameDocument.body.id = 'frame-body';
                frameDocument.body.name = 'frame-name';
                const input = frameDocument.createElement('input');
                input.id = 'inside';
                input.name = 'inside-name';
                frameDocument.body.appendChild(input);

                state.bodyId = frameDocument.body.id;
                state.bodyName = frameDocument.body.name;
                state.idLookup = frameDocument.getElementById('inside') === input;
                state.idSelector = frameDocument.querySelector('#inside') === input;
                state.nameSelector = frameDocument.querySelector('[name="inside-name"]') === input;

                frameDocument.open();
                frameDocument.write(
                  "<!doctype html><html><body><div id='written'>Hi</div>" +
                  "</bo" + "dy></html>");
                frameDocument.close();
                state.contentDocumentStable = iframe.contentDocument === frameDocument;
                state.writtenText = frameDocument.querySelector('#written').textContent;
                frameWrapper.style.display = 'none';
                state.hiddenDirection =
                  frameWindow.getComputedStyle(frameDocument.body).direction;
                """, "v8-src-less-iframe-identity-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneIframeIdentity)")) ?? "{}");
            var state = result.RootElement;
            Assert.True(state.GetProperty("hadDocumentSynchronously").GetBoolean());
            Assert.True(state.GetProperty("hadBodySynchronously").GetBoolean());
            Assert.Equal("about:blank", state.GetProperty("initialLocation").GetString());
            Assert.True(state.GetProperty("contentWindowDocumentSame").GetBoolean());
            Assert.True(state.GetProperty("defaultViewSame").GetBoolean());
            Assert.True(state.GetProperty("frameElementSame").GetBoolean());
            Assert.Equal("frame-body", state.GetProperty("bodyId").GetString());
            Assert.Equal("frame-name", state.GetProperty("bodyName").GetString());
            Assert.True(state.GetProperty("idLookup").GetBoolean());
            Assert.True(state.GetProperty("idSelector").GetBoolean());
            Assert.True(state.GetProperty("nameSelector").GetBoolean());
            Assert.True(state.GetProperty("contentDocumentStable").GetBoolean());
            Assert.Equal("Hi", state.GetProperty("writtenText").GetString());
            Assert.Equal("ltr", state.GetProperty("hiddenDirection").GetString());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

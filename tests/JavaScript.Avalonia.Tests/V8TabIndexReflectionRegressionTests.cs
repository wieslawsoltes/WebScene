using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8TabIndexReflectionRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void TabIndexPropertyAndAttributeReflectionMatchHtmlSemantics()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window
        {
            Width = 320,
            Height = 120,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute("""
                document.body.innerHTML = `
                  <span id="span"></span>
                  <span id="markup" tabindex="2"></span>
                  <button id="button"></button>
                  <input id="input" type="button">
                `;
                const span = document.getElementById('span');
                const markup = document.getElementById('markup');
                const button = document.getElementById('button');
                const input = document.getElementById('input');
                const result = globalThis.__webSceneTabIndexReflection = {
                  initial: [span.tabIndex, span.getAttribute('tabindex'), markup.tabIndex, button.tabIndex, input.tabIndex],
                  focusEvents: []
                };
                span.addEventListener('focus', () => result.focusEvents.push('focus'));
                span.addEventListener('focusin', () => result.focusEvents.push('focusin'));
                span.tabIndex = -1;
                result.propertyWrite = [span.tabIndex, span.getAttribute('tabindex')];
                span.focus();
                result.activeAfterNegativeFocus = document.activeElement === span;
                span.setAttribute('tabindex', 'invalid');
                result.invalid = [span.tabIndex, span.getAttribute('tabindex')];
                span.removeAttribute('tabindex');
                result.removed = [span.tabIndex, span.getAttribute('tabindex')];
                """, "v8-tabindex-reflection-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneTabIndexReflection)")) ?? "{}");
            var root = result.RootElement;
            Assert.Equal([-1, 0, 2, 0, 0], root.GetProperty("initial").EnumerateArray()
                .Select(static item => item.ValueKind == JsonValueKind.Null ? 0 : item.GetInt32()));
            Assert.Equal([-1, -1], root.GetProperty("propertyWrite").EnumerateArray()
                .Select(static item => item.ValueKind == JsonValueKind.String
                    ? int.Parse(item.GetString()!)
                    : item.GetInt32()));
            Assert.True(root.GetProperty("activeAfterNegativeFocus").GetBoolean());
            Assert.Equal(["focus", "focusin"], root.GetProperty("focusEvents").EnumerateArray()
                .Select(static item => item.GetString()));
            Assert.Equal(-1, root.GetProperty("invalid")[0].GetInt32());
            Assert.Equal("invalid", root.GetProperty("invalid")[1].GetString());
            Assert.Equal(-1, root.GetProperty("removed")[0].GetInt32());
            Assert.Equal(JsonValueKind.Null, root.GetProperty("removed")[1].ValueKind);
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

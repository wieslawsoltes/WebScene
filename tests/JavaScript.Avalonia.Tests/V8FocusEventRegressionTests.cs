using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8FocusEventRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void DelegatedListenersObserveWptAlignedSameDocumentFocusTransition()
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
                const parent = document.createElement('div');
                const first = document.createElement('input');
                const second = document.createElement('input');
                first.id = 'a';
                second.id = 'b';
                parent.id = 'parent';
                parent.append(first, second);
                document.body.appendChild(parent);

                const events = globalThis.__webSceneFocusEvents = [];
                function record(event) {
                  events.push({
                    type: event.type,
                    target: event.target.id,
                    currentTarget: event.currentTarget.id,
                    relatedTarget: event.relatedTarget ? event.relatedTarget.id : null,
                    activeElement: document.activeElement.id || document.activeElement.nodeName.toLowerCase(),
                    bubbles: event.bubbles,
                    cancelable: event.cancelable,
                    composed: event.composed
                  });
                }
                for (const type of ['focus', 'focusin', 'blur', 'focusout']) {
                  first.addEventListener(type, record);
                  second.addEventListener(type, record);
                }
                parent.addEventListener('focusin', record);
                parent.addEventListener('focusout', record);

                first.focus();
                second.focus();
                """, "v8-focus-event-wpt-normal-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneFocusEvents)")) ?? "[]");
            var events = result.RootElement.EnumerateArray().ToArray();
            Assert.Equal(
                ["focus", "focusin", "focusin", "blur", "focusout", "focusout", "focus", "focusin", "focusin"],
                events.Select(static item => item.GetProperty("type").GetString()));
            Assert.Equal(
                ["a", "a", "a", "a", "a", "a", "b", "b", "b"],
                events.Select(static item => item.GetProperty("target").GetString()));
            Assert.Equal(
                ["a", "a", "parent", "a", "a", "parent", "b", "b", "parent"],
                events.Select(static item => item.GetProperty("currentTarget").GetString()));
            Assert.Equal(
                [null, null, null, "b", "b", "b", "a", "a", "a"],
                events.Select(static item => item.GetProperty("relatedTarget").ValueKind == JsonValueKind.Null
                    ? null
                    : item.GetProperty("relatedTarget").GetString()));
            Assert.Equal(
                ["a", "a", "a", "body", "body", "body", "b", "b", "b"],
                events.Select(static item => item.GetProperty("activeElement").GetString()));
            Assert.All(events, static item =>
            {
                Assert.False(item.GetProperty("cancelable").GetBoolean());
                Assert.True(item.GetProperty("composed").GetBoolean());
                Assert.Equal(
                    item.GetProperty("type").GetString() is "focusin" or "focusout",
                    item.GetProperty("bubbles").GetBoolean());
            });
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

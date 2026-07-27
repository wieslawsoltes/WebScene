using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8MouseEventDispatchRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ConstructedMouseEventKeepsIdentityAndResetsDispatchState()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window
        {
            Width = 320,
            Height = 180,
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
                const target = document.createElement('button');
                parent.appendChild(target);
                document.body.appendChild(parent);
                let observed = null;
                parent.addEventListener('click', event => { observed = event; });
                const event = new MouseEvent('click', { bubbles: true });
                target.dispatchEvent(event);
                globalThis.__webSceneMouseDispatchRegression = {
                  constructor: event instanceof MouseEvent && event instanceof Event,
                  identity: observed === event,
                  target: event.target === target,
                  currentTarget: event.currentTarget,
                  eventPhase: event.eventPhase,
                  button: event.button,
                  buttons: event.buttons
                };
                """, "v8-mouse-event-dispatch-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneMouseDispatchRegression)")) ?? "{}");
            var state = result.RootElement;
            Assert.True(state.GetProperty("constructor").GetBoolean());
            Assert.True(state.GetProperty("identity").GetBoolean());
            Assert.True(state.GetProperty("target").GetBoolean());
            Assert.Equal(JsonValueKind.Null, state.GetProperty("currentTarget").ValueKind);
            Assert.Equal(0, state.GetProperty("eventPhase").GetInt32());
            Assert.Equal(0, state.GetProperty("button").GetInt32());
            Assert.Equal(0, state.GetProperty("buttons").GetInt32());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

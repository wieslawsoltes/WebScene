using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8KeyboardEventRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void NestedChildClickCanUseKeyboardEventTypeGuardBeforeTogglingItsWrapper()
    {
        // ClearScript's reviewed native library is intentionally optional in
        // the portable unit-test lane. The V8 lane sets this path explicitly.
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        RunNestedClickRegression();
    }

    private static void RunNestedClickRegression()
    {
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
                const wrapper = document.createElement('div');
                const button = document.createElement('button');
                const icon = document.createElement('span');
                wrapper.id = 'wrapper';
                button.id = 'toggle';
                icon.id = 'icon';
                button.appendChild(icon);
                wrapper.appendChild(button);
                document.body.appendChild(wrapper);

                const keyboard = new KeyboardEvent('keydown', {
                  bubbles: true,
                  cancelable: true,
                  key: 'Enter',
                  code: 'Enter',
                  location: KeyboardEvent.DOM_KEY_LOCATION_NUMPAD,
                  ctrlKey: true,
                  repeat: true
                });
                const state = globalThis.__webSceneKeyboardEventRegression = {
                  constructorShape:
                    typeof UIEvent === 'function' &&
                    typeof KeyboardEvent === 'function' &&
                    keyboard instanceof KeyboardEvent &&
                    keyboard instanceof UIEvent &&
                    keyboard instanceof Event &&
                    keyboard.key === 'Enter' &&
                    keyboard.code === 'Enter' &&
                    keyboard.location === 3 &&
                    keyboard.ctrlKey === true &&
                    keyboard.repeat === true &&
                    keyboard.getModifierState('Control') === true,
                  listenerCalls: 0,
                  targetWasIcon: false,
                  currentTargetWasButton: false,
                  clickWasKeyboard: true,
                  dispatchReturned: true
                };
                button.addEventListener('click', function (event) {
                  state.listenerCalls++;
                  state.targetWasIcon = event.target === icon;
                  state.currentTargetWasButton = event.currentTarget === button;
                  event.cancelable && event.preventDefault();
                  event instanceof KeyboardEvent || button.blur();
                  state.clickWasKeyboard = event instanceof KeyboardEvent;
                  wrapper.classList.toggle('closed');
                });
                state.dispatchReturned = icon.dispatchEvent(new MouseEvent('click', {
                  bubbles: true,
                  cancelable: true
                }));
                state.closed = wrapper.classList.contains('closed');
                """, "v8-keyboard-event-nested-click-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneKeyboardEventRegression)")) ?? "{}");
            var state = result.RootElement;
            Assert.True(state.GetProperty("constructorShape").GetBoolean());
            Assert.Equal(1, state.GetProperty("listenerCalls").GetInt32());
            Assert.True(state.GetProperty("targetWasIcon").GetBoolean());
            Assert.True(state.GetProperty("currentTargetWasButton").GetBoolean());
            Assert.False(state.GetProperty("clickWasKeyboard").GetBoolean());
            Assert.False(state.GetProperty("dispatchReturned").GetBoolean());
            Assert.True(state.GetProperty("closed").GetBoolean());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

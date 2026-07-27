using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8WindowNamedPropertyTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ElementIdIsAvailableAsAWindowNamedPropertyBeforeTheNextClassicScript()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var window = new Window
        {
            Width = 160,
            Height = 80,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute("""
                const target = document.createElement('div');
                target.id = 'namedTarget';
                document.body.appendChild(target);
                const collision = document.createElement('div');
                collision.id = 'test';
                document.body.appendChild(collision);
                """, "named-window-property-setup.js");
            runtime.Execute("""
                globalThis.__webSceneNamedTargetMatches =
                  namedTarget === document.getElementById('namedTarget');
                globalThis.__webSceneNamedCollisionBeforeAssignment =
                  test === document.getElementById('test');
                test = function () { return 42; };
                globalThis.__webSceneNamedCollisionAfterAssignment = test();
                """, "named-window-property-read.js");

            Assert.True(Convert.ToBoolean(
                runtime.Engine.Evaluate("globalThis.__webSceneNamedTargetMatches")));
            Assert.True(Convert.ToBoolean(
                runtime.Engine.Evaluate("globalThis.__webSceneNamedCollisionBeforeAssignment")));
            Assert.Equal(42, Convert.ToInt32(
                runtime.Engine.Evaluate("globalThis.__webSceneNamedCollisionAfterAssignment")));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

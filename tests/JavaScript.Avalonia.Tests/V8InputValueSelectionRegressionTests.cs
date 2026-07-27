using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8InputValueSelectionRegressionTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void DynamicValueAssignmentMatchesChromiumSelectionPlacement()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var root = new CssLayoutPanel { Width = 320, Height = 80 };
        var window = new Window { Width = 320, Height = 80, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute("""
                const input = document.createElement('input');
                input.value = 'abcdefghij';
                document.body.appendChild(input);
                const snapshots = globalThis.__webSceneInputValueSelection = [];
                const record = label => snapshots.push([
                  label,
                  input.selectionStart,
                  input.selectionEnd,
                  input.selectionDirection
                ]);

                record('dynamic-before-focus');
                input.focus();
                record('dynamic-after-focus');
                input.setSelectionRange(2, 4, 'backward');
                input.value = 'klmnopqrst';
                record('changed-same-length');
                input.setSelectionRange(2, 4, 'backward');
                input.value = 'klmnopqrst';
                record('same-value');
                input.value = 'x';
                record('changed-short');
                """, "v8-input-value-selection-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneInputValueSelection)")) ?? "[]");
            var snapshots = result.RootElement.EnumerateArray().ToArray();
            Assert.Equal(5, snapshots.Length);
            AssertSnapshot(snapshots[0], "dynamic-before-focus", 10, 10, "none");
            AssertSnapshot(snapshots[1], "dynamic-after-focus", 10, 10, "none");
            AssertSnapshot(snapshots[2], "changed-same-length", 10, 10, "none");
            AssertSnapshot(snapshots[3], "same-value", 2, 4, "backward");
            AssertSnapshot(snapshots[4], "changed-short", 1, 1, "none");
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static void AssertSnapshot(
        JsonElement snapshot,
        string label,
        int selectionStart,
        int selectionEnd,
        string selectionDirection)
    {
        Assert.Equal(label, snapshot[0].GetString());
        Assert.Equal(selectionStart, snapshot[1].GetInt32());
        Assert.Equal(selectionEnd, snapshot[2].GetInt32());
        Assert.Equal(selectionDirection, snapshot[3].GetString());
    }
}

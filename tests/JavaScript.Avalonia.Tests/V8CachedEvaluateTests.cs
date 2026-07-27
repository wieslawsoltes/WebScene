using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8CachedEvaluateTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void OwnerControlPlaneEvaluationUsesSharedCompilationCache()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath)) return;

        var window = new Window
        {
            Width = 320,
            Height = 180,
            Content = new CssLayoutPanel()
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        var cache = new ClearScriptV8SharedCache();
        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions { SharedCache = cache });
        try
        {
            var baseline = cache.GetMetrics();
            Assert.Equal(42, Convert.ToInt32(runtime.Evaluate(
                "21 * 2",
                "managed-chart-control.js")));
            var cold = cache.GetMetrics();
            Assert.Equal(baseline.CodeMisses + 1, cold.CodeMisses);

            Assert.Equal(42, Convert.ToInt32(runtime.Evaluate(
                "21 * 2",
                "managed-chart-control.js")));
            var warm = cache.GetMetrics();
            Assert.Equal(cold.CodeHits + 1, warm.CodeHits);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }
}

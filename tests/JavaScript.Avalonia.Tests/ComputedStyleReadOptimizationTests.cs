using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class ComputedStyleReadOptimizationTests
{
    [AvaloniaFact]
    public void UnrelatedMutationReusesEquivalentSnapshotButStyleAndGeometryChangesRebuild()
    {
        var enabled = RunCrossGenerationProbe(enableStateReuse: true);
        var disabled = RunCrossGenerationProbe(enableStateReuse: false);

        Assert.True(enabled.ReusedEquivalentSnapshot);
        Assert.False(disabled.ReusedEquivalentSnapshot);
        Assert.Equal("120px", enabled.ChangedWidth);
        Assert.Equal("rgb(170, 0, 0)", enabled.ChangedColor);
        Assert.True(enabled.ReuseCount >= 1);
        Assert.Equal(0, disabled.ReuseCount);
        Assert.True(enabled.BuildCount < disabled.BuildCount);
    }

    [AvaloniaFact]
    public void RepeatedReadsReuseSnapshotUntilStyleOrLayoutChanges()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        try
        {
            var document = host.Document;
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.style.cssText =
                "display: block; width: 80px; height: 24px; color: #123456; padding: 2px 4px";
            HostTestUtilities.GetElement(document.body).appendChild(element);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var first = document.getComputedStyle(element);
            var hitStart = document.ComputedStyleSnapshotHitCount;
            var buildStart = document.ComputedStyleSnapshotBuildCount;
            var allocationStart = GC.GetAllocatedBytesForCurrentThread();
            var started = Stopwatch.GetTimestamp();
            for (var index = 0; index < 1_000; index++)
            {
                var current = document.getComputedStyle(element);
                Assert.Same(first, current);
                Assert.Equal("80px", current.getPropertyValue("width"));
                Assert.Equal("rgb(18, 52, 86)", current.getPropertyValue("color"));
            }
            var elapsed = Stopwatch.GetElapsedTime(started);
            var allocated = GC.GetAllocatedBytesForCurrentThread() - allocationStart;

            Assert.Equal(1_000, document.ComputedStyleSnapshotHitCount - hitStart);
            Assert.Equal(0, document.ComputedStyleSnapshotBuildCount - buildStart);
            Assert.True(elapsed < TimeSpan.FromMilliseconds(100), $"Repeated reads took {elapsed.TotalMilliseconds:F2} ms.");
            Assert.True(allocated < 2 * 1024 * 1024, $"Repeated reads allocated {allocated / 1024d:F1} KB.");

            element.SetStyleProperty("width", "120px");
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();
            var changed = document.getComputedStyle(element);
            Assert.NotSame(first, changed);
            Assert.Equal("120px", changed.getPropertyValue("width"));
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void V8RepeatedReadsPreserveCssomShapeThroughCachedSnapshot()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.ResetComputedStyleReadCacheMetrics();
            var allocationStart = GC.GetAllocatedBytesForCurrentThread();
            var started = Stopwatch.GetTimestamp();
            runtime.Execute("""
                const target = document.createElement('div');
                target.style.cssText =
                  'display: block; width: 80px; height: 24px; color: #123456; padding: 2px 4px';
                const authoredCssText = target.style.cssText;
                const authoredStyleAttribute = target.getAttribute('style');
                document.body.appendChild(target);
                const first = getComputedStyle(target);
                const firstWidth = first.width;
                const firstColor = first.getPropertyValue('color');
                const firstLength = first.length;
                const firstItemType = typeof first.item(0);
                let passed = firstWidth === '80px' &&
                  firstColor === 'rgb(18, 52, 86)' &&
                  firstLength > 0 && firstItemType === 'string';
                let loopWidth = '';
                let loopPaddingLeft = '';
                for (let index = 0; index < 1000; index++) {
                  const style = getComputedStyle(target);
                  loopWidth = style.width;
                  loopPaddingLeft = style.getPropertyValue('padding-left');
                  passed = passed && loopWidth === '80px' && loopPaddingLeft === '4px';
                }
                target.style.width = '120px';
                const changed = getComputedStyle(target);
                globalThis.__computedStyleReadResult = {
                  passed: passed && changed.width === '120px',
                  width: changed.width,
                  color: changed.color,
                  length: changed.length,
                  firstWidth,
                  firstColor,
                  firstLength,
                  firstItemType,
                  loopWidth,
                  loopPaddingLeft,
                  authoredCssText,
                  authoredStyleAttribute
                };
                """, "computed-style-read-optimization.js");
            var elapsed = Stopwatch.GetElapsedTime(started);
            var allocated = GC.GetAllocatedBytesForCurrentThread() - allocationStart;

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__computedStyleReadResult)")) ?? "{}");
            Assert.True(
                result.RootElement.GetProperty("passed").GetBoolean(),
                result.RootElement.GetRawText());
            Assert.Equal("120px", result.RootElement.GetProperty("width").GetString());
            Assert.Equal("rgb(18, 52, 86)", result.RootElement.GetProperty("color").GetString());
            Assert.True(result.RootElement.GetProperty("length").GetInt32() > 0);
            using var metrics = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneDescribeComputedStyleReadCacheMetrics())")) ?? "{}");
            Assert.Equal(1_002, metrics.RootElement.GetProperty("typedMethodHits").GetInt32());
            Assert.Equal(1_000, metrics.RootElement.GetProperty("facadeHits").GetInt32());
            Assert.Equal(2, metrics.RootElement.GetProperty("facadeMisses").GetInt32());
            Assert.Equal(2_000, metrics.RootElement.GetProperty("valueHits").GetInt32());
            Assert.Equal(5, metrics.RootElement.GetProperty("valueMisses").GetInt32());
            Assert.True(
                elapsed < TimeSpan.FromMilliseconds(100),
                $"V8 computed-style reads took {elapsed.TotalMilliseconds:F2} ms.");
            Assert.True(
                allocated < 16 * 1024 * 1024,
                $"V8 computed-style reads allocated {allocated / 1024d:F1} KB.");
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
    public void V8ComputedStyleSeparatesMethodFallbackFromNamedProperties()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            var result = Convert.ToString(runtime.Engine.Evaluate("""
                (() => {
                  const target = document.createElement('div');
                  target.style.setProperty('color', 'rgb(1, 2, 3)');
                  target.style.setProperty('--present-token', 'value');
                  document.body.appendChild(target);
                  const computed = getComputedStyle(target);
                  return JSON.stringify([
                    computed.getPropertyValue('x-fake'),
                    typeof computed.xFake,
                    typeof computed['x-fake'],
                    'xFake' in computed,
                    computed.color,
                    computed['font-size'] === computed.getPropertyValue('font-size'),
                    'color' in computed,
                    computed.getPropertyValue('--present-token').trim(),
                    typeof computed['--present-token']
                  ]);
                })()
                """));

            Assert.Equal(
                """["","undefined","undefined",false,"rgb(1, 2, 3)",true,true,"value","undefined"]""",
                result);
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
    public void V8ComputedStyleSnapshotTracksDetachAndReattachLifecycle()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        if (string.IsNullOrWhiteSpace(nativePath) || !File.Exists(nativePath))
        {
            return;
        }

        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            var result = Convert.ToString(runtime.Engine.Evaluate("""
                (() => {
                  const style = document.createElement('style');
                  style.textContent = '.cascade-hidden { display: none; }';
                  document.head.appendChild(style);
                  const target = document.createElement('div');
                  target.className = 'cascade-hidden';
                  document.body.appendChild(target);
                  const connected = getComputedStyle(target).display;
                  target.remove();
                  const detached = getComputedStyle(target).display;
                  target.style.display = '';
                  document.body.appendChild(target);
                  const reattached = getComputedStyle(target).display;
                  return JSON.stringify([
                    connected,
                    detached,
                    target.style.display,
                    reattached,
                    target.getClientRects().length
                  ]);
                })()
                """));

            Assert.Equal("""["none","","","none",0]""", result);
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static CrossGenerationProbeResult RunCrossGenerationProbe(bool enableStateReuse)
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(
            window,
            enableComputedStyleSnapshotStateReuse: enableStateReuse);
        try
        {
            var document = host.Document;
            var body = HostTestUtilities.GetElement(document.body);
            var target = HostTestUtilities.GetElement(document.createElement("div"));
            target.style.cssText =
                "display:block;width:80px;height:24px;color:#123456;position:absolute;left:0;top:0";
            body.appendChild(target);
            document.EnsureStylesCurrent();
            Dispatcher.UIThread.RunJobs();

            var first = document.getComputedStyle(target);
            var buildStart = document.ComputedStyleSnapshotBuildCount;
            var reuseStart = document.ComputedStyleSnapshotStateReuseCount;

            var unrelated = HostTestUtilities.GetElement(document.createElement("div"));
            unrelated.style.cssText =
                "display:block;width:10px;height:10px;position:absolute;left:200px;top:100px";
            body.appendChild(unrelated);
            var equivalent = document.getComputedStyle(target);

            target.SetStyleProperty("width", "120px");
            target.SetStyleProperty("color", "#aa0000");
            var changed = document.getComputedStyle(target);
            Assert.NotSame(equivalent, changed);

            return new CrossGenerationProbeResult(
                ReferenceEquals(first, equivalent),
                changed.getPropertyValue("width") ?? string.Empty,
                changed.getPropertyValue("color") ?? string.Empty,
                document.ComputedStyleSnapshotBuildCount - buildStart,
                document.ComputedStyleSnapshotStateReuseCount - reuseStart);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private sealed record CrossGenerationProbeResult(
        bool ReusedEquivalentSnapshot,
        string ChangedWidth,
        string ChangedColor,
        long BuildCount,
        long ReuseCount);
}

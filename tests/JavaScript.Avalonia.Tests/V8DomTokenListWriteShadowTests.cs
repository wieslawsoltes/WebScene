using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8DomTokenListWriteShadowTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ForcedToggleNoOpsAvoidHostWritesAndExternalClassWritesInvalidateShadow()
    {
        if (!HasNativeV8()) return;

        var metrics = RunProbe(enableWriteShadow: true, InvalidationProbeScript, out var result);

        Assert.True(result.GetProperty("passed").GetBoolean());
        Assert.Equal("final", result.GetProperty("className").GetString());
        Assert.Equal(4, metrics.GetProperty("hostWrites").GetInt32());
        Assert.Equal(102, metrics.GetProperty("skippedWrites").GetInt32());
        Assert.Equal(3, metrics.GetProperty("refreshes").GetInt32());
        Assert.Equal(2, metrics.GetProperty("invalidations").GetInt32());
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void DisabledWriteShadowPreservesBehaviorAndCrossesHostForEveryForcedToggle()
    {
        if (!HasNativeV8()) return;

        var metrics = RunProbe(enableWriteShadow: false, SimpleProbeScript, out var result);

        Assert.True(result.GetProperty("passed").GetBoolean());
        Assert.Equal("compact", result.GetProperty("className").GetString());
        Assert.Equal(100, metrics.GetProperty("hostWrites").GetInt32());
        Assert.Equal(0, metrics.GetProperty("skippedWrites").GetInt32());
        Assert.Equal(0, metrics.GetProperty("refreshes").GetInt32());
        Assert.Equal(0, metrics.GetProperty("invalidations").GetInt32());
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void ValueOrderAndInvalidForcedToggleTokensAlwaysUseBackendSemantics()
    {
        if (!HasNativeV8()) return;

        var enabledMetrics = RunProbe(true, SemanticsProbeScript, out var enabledResult);
        var disabledMetrics = RunProbe(false, SemanticsProbeScript, out var disabledResult);

        Assert.Equal(enabledResult.GetRawText(), disabledResult.GetRawText());
        Assert.Equal("b a", enabledResult.GetProperty("firstOrder").GetString());
        Assert.Equal("a b", enabledResult.GetProperty("secondOrder").GetString());
        Assert.Equal(6, enabledMetrics.GetProperty("hostWrites").GetInt32());
        Assert.Equal(6, disabledMetrics.GetProperty("hostWrites").GetInt32());
        Assert.Equal(0, enabledMetrics.GetProperty("skippedWrites").GetInt32());
    }

    private static JsonElement RunProbe(
        bool enableWriteShadow,
        string script,
        out JsonElement result)
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(
            host,
            new ClearScriptV8RuntimeOptions
            {
                EnableDomTokenListWriteShadow = enableWriteShadow
            });
        try
        {
            runtime.Execute(script, "token-write-shadow.js");
            using var resultDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__tokenWriteShadowResult)")) ?? "{}");
            using var metricsDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneDescribeDomTokenListWriteShadowMetrics())")) ?? "{}");
            result = resultDocument.RootElement.Clone();
            return metricsDocument.RootElement.Clone();
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static bool HasNativeV8()
    {
        var nativePath = Environment.GetEnvironmentVariable("WEBSCENE_CLEARSCRIPT_NATIVE");
        return !string.IsNullOrWhiteSpace(nativePath) && File.Exists(nativePath);
    }

    private const string SimpleProbeScript = """
        const target = document.createElement('div');
        document.body.appendChild(target);
        globalThis.__webSceneResetDomTokenListWriteShadowMetrics();
        let passed = true;
        for (let index = 0; index < 100; index++) {
          passed = target.classList.toggle('compact', true) && passed;
        }
        globalThis.__tokenWriteShadowResult = {
          passed: passed && target.classList.contains('compact'),
          className: target.className
        };
        """;

    private const string InvalidationProbeScript = """
        const target = document.createElement('div');
        document.body.appendChild(target);
        globalThis.__webSceneResetDomTokenListWriteShadowMetrics();
        let passed = target.classList.toggle('compact', true);
        for (let index = 0; index < 99; index++) {
          passed = target.classList.toggle('compact', true) && passed;
        }
        target.className = 'wide';
        passed = target.classList.toggle('wide', true) && passed;
        target.setAttribute('class', 'compact');
        passed = target.classList.toggle('compact', true) && passed;
        passed = !target.classList.toggle('compact', false) && passed;
        passed = !target.classList.toggle('compact', false) && passed;
        target.classList.value = 'final';
        target.classList.value = 'final';
        globalThis.__tokenWriteShadowResult = {
          passed: passed && target.classList.contains('final') && !target.classList.contains('compact'),
          className: target.className
        };
        """;

    private const string SemanticsProbeScript = """
        const target = document.createElement('div');
        const invalidTarget = document.createElement('div');
        document.body.appendChild(target);
        document.body.appendChild(invalidTarget);
        globalThis.__webSceneResetDomTokenListWriteShadowMetrics();
        target.classList.value = 'b a';
        const firstOrder = target.className;
        target.classList.value = 'a b';
        const secondOrder = target.className;
        const invalidResults = [];
        for (const token of ['x y', 'x y', '', '']) {
          try {
            invalidResults.push({ token: token, result: invalidTarget.classList.toggle(token, true) });
          } catch (error) {
            invalidResults.push({ token: token, error: String(error && (error.name || error.message || error)) });
          }
        }
        globalThis.__tokenWriteShadowResult = {
          firstOrder: firstOrder,
          secondOrder: secondOrder,
          invalidClassName: invalidTarget.className,
          invalidResults: invalidResults
        };
        """;
}

using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class V8TypedInlineStyleWriteTests
{
    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void TypedAndFallbackWritesPreserveInlineStyleCssomAndComputedResults()
    {
        if (!HasNativeV8()) return;

        var enabled = RunProbe(enableTypedWrites: true);
        var disabled = RunProbe(enableTypedWrites: false);

        Assert.Equal(disabled.Result.GetRawText(), enabled.Result.GetRawText());
        Assert.Equal("42px", enabled.Result.GetProperty("inlineWidth").GetString());
        Assert.Equal("42px", enabled.Result.GetProperty("computedWidth").GetString());
        Assert.Equal("block", enabled.Result.GetProperty("computedDisplay").GetString());
        Assert.Equal("blue", enabled.Result.GetProperty("customProperty").GetString());
        Assert.Equal(string.Empty, enabled.Result.GetProperty("removedHeight").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("authoredPadding").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("computedPaddingTop").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("computedPaddingRight").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("computedPaddingBottom").GetString());
        Assert.Equal("7px", enabled.Result.GetProperty("computedPaddingLeft").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("authoredBorder").GetString());
        Assert.Equal("1px", enabled.Result.GetProperty("computedBorderTop").GetString());
        Assert.Equal("2px", enabled.Result.GetProperty("computedBorderRight").GetString());
        Assert.Equal("3px", enabled.Result.GetProperty("computedBorderBottom").GetString());
        Assert.Equal("4px", enabled.Result.GetProperty("computedBorderLeft").GetString());
        Assert.True(enabled.Metrics.GetProperty("typedWrites").GetInt32() > 0);
        Assert.Equal(0, enabled.Metrics.GetProperty("fallbackWrites").GetInt32());
        Assert.Equal(0, disabled.Metrics.GetProperty("typedWrites").GetInt32());
        Assert.Equal(
            enabled.Metrics.GetProperty("typedWrites").GetInt32(),
            disabled.Metrics.GetProperty("fallbackWrites").GetInt32());
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void DocumentElementStyleSupportsCssomAndInheritedCustomProperties()
    {
        if (!HasNativeV8()) return;

        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute(DocumentElementProbeScript, "document-element-style-cssom.js");
            using var resultDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__documentElementStyleResult)")) ?? "{}");
            var result = resultDocument.RootElement;

            Assert.Equal("#123456", result.GetProperty("inlineAccent").GetString());
            Assert.Equal("#123456", result.GetProperty("computedAccent").GetString());
            Assert.Equal("rgb(18, 52, 86)", result.GetProperty("inheritedColor").GetString());
            Assert.Equal("12px", result.GetProperty("inlineWidth").GetString());
            Assert.Equal(string.Empty, result.GetProperty("removedWidth").GetString());
            Assert.Equal(1, result.GetProperty("length").GetInt32());
            Assert.Equal("--accent", result.GetProperty("firstItem").GetString());
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
    public void CssTextPreservesCustomPropertyOverwriteAndCaseSensitivity()
    {
        if (!HasNativeV8()) return;

        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window);
        using var runtime = new ClearScriptV8Runtime(host);
        try
        {
            runtime.Execute("""
                const target = document.createElement('div');
                target.style.cssText =
                  '--var:value1; --var:value2; --Var:upper; width:12px !important; width:24px';
                globalThis.__customPropertyCssTextResult = {
                  lower: target.style.getPropertyValue('--var'),
                  upper: target.style.getPropertyValue('--Var'),
                  width: target.style.width,
                  cssText: target.style.cssText
                };
                """, "custom-property-css-text.js");
            using var resultDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__customPropertyCssTextResult)")) ?? "{}");
            var result = resultDocument.RootElement;

            Assert.Equal(
                "--var: value2; --Var: upper; width: 12px !important",
                result.GetProperty("cssText").GetString());
            Assert.Equal("value2", result.GetProperty("lower").GetString());
            Assert.Equal("upper", result.GetProperty("upper").GetString());
            Assert.Equal("12px", result.GetProperty("width").GetString());
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
        }
        finally
        {
            window.Close();
            Dispatcher.UIThread.RunJobs();
        }
    }

    private static ProbeResult RunProbe(bool enableTypedWrites)
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
                EnableTypedInlineStyleWrites = enableTypedWrites
            });
        try
        {
            runtime.ResetTypedInlineStyleWriteMetrics();
            runtime.Execute(ProbeScript, "typed-inline-style-write.js");
            using var resultDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__typedInlineStyleWriteResult)")) ?? "{}");
            using var metricsDocument = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__webSceneDescribeTypedInlineStyleWriteMetrics())")) ?? "{}");
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
            return new ProbeResult(
                resultDocument.RootElement.Clone(),
                metricsDocument.RootElement.Clone());
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

    private sealed record ProbeResult(JsonElement Result, JsonElement Metrics);

    private const string ProbeScript = """
        const target = document.createElement('div');
        document.body.appendChild(target);
        target.style.width = '10px';
        target.style.setProperty('height', '20px');
        target.style.setProperty('--accent', 'red');
        target.style.width = '10px';
        target.style.width = '12px';
        target.style.removeProperty('height');
        target.style.cssText = 'display: inline; width: 30px; height: 8px';
        target.style.width = '32px';
        target.setAttribute('style', 'display: block; width: 40px; --accent: blue');
        target.style.width = '42px';
        target.style.padding = '3px';
        const authoredPadding = target.style.padding;
        target.style.setProperty('padding-left', '7px');
        target.style.border = '3px';
        const authoredBorder = target.style.getPropertyValue('border');
        target.style.borderStyle = 'solid';
        target.style.borderWidth = '1px 2px 3px 4px';
        const computed = getComputedStyle(target);
        globalThis.__typedInlineStyleWriteResult = {
          cssText: target.style.cssText,
          styleAttribute: target.getAttribute('style'),
          inlineWidth: target.style.width,
          computedWidth: computed.width,
          computedDisplay: computed.display,
          customProperty: target.style.getPropertyValue('--accent'),
          removedHeight: target.style.getPropertyValue('height'),
          authoredPadding,
          computedPaddingTop: computed.paddingTop,
          computedPaddingRight: computed.paddingRight,
          computedPaddingBottom: computed.paddingBottom,
          computedPaddingLeft: computed.paddingLeft,
          authoredBorder,
          computedBorderTop: computed.borderTopWidth,
          computedBorderRight: computed.borderRightWidth,
          computedBorderBottom: computed.borderBottomWidth,
          computedBorderLeft: computed.borderLeftWidth
        };
        """;

    private const string DocumentElementProbeScript = """
        const target = document.createElement('div');
        document.body.appendChild(target);
        document.documentElement.style.cssText = '--accent: #123456; width: 12px';
        target.style.color = 'var(--accent)';
        const computedAccent = getComputedStyle(document.documentElement)
          .getPropertyValue('--accent');
        const inheritedColor = getComputedStyle(target).color;
        const inlineAccent = document.documentElement.style.getPropertyValue('--accent');
        const inlineWidth = document.documentElement.style.width;
        document.documentElement.style.removeProperty('width');
        globalThis.__documentElementStyleResult = {
          inlineAccent,
          computedAccent,
          inheritedColor,
          inlineWidth,
          removedWidth: document.documentElement.style.width,
          length: document.documentElement.style.length,
          firstItem: document.documentElement.style.item(0)
        };
        """;
}

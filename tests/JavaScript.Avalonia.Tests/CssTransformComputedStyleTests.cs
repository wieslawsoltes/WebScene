using System.Globalization;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Threading;
using JavaScript.Avalonia.ClearScript;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class CssTransformComputedStyleTests
{
    [AvaloniaFact]
    public void ComputedTransformSerializesAppliedTranslateScaleRotateAndCompositionMatrices()
    {
        var root = new CssLayoutPanel { Width = 320, Height = 180 };
        var window = new Window { Width = 320, Height = 180, Content = root };
        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true);
        var document = host.Document;
        var style = HostTestUtilities.GetElement(document.createElement("style"));
        style.textContent = """
            .zero { transform: translate(0, 0); }
            .negative { transform: translate(-1px, -1px); }
            .scaled { transform: scale(2, 3); }
            .rotated { transform: rotate(90deg); }
            .composed { transform: translate(5px, 7px) scale(2, 3) rotate(90deg); }
            .interleaved { transform: translate(5px, 7px) rotate(90deg) translate(2px, 3px); }
            """;
        document.head.appendChild(style);

        var absent = Append("absent");
        var zero = Append("zero");
        var negative = Append("negative");
        var scaled = Append("scaled");
        var rotated = Append("rotated");
        var composed = Append("composed");
        var interleaved = Append("interleaved");

        window.Show();
        document.EnsureStylesCurrent();
        Dispatcher.UIThread.RunJobs();

        Assert.Equal("none", document.getComputedStyle(absent).getPropertyValue("transform"));
        Assert.Equal("matrix(1, 0, 0, 1, 0, 0)", document.getComputedStyle(zero).getPropertyValue("transform"));
        Assert.Equal("matrix(1, 0, 0, 1, -1, -1)", document.getComputedStyle(negative).getPropertyValue("transform"));
        Assert.Equal("matrix(2, 0, 0, 3, 0, 0)", document.getComputedStyle(scaled).getPropertyValue("transform"));
        Assert.Equal("matrix(0, 1, -1, 0, 0, 0)", document.getComputedStyle(rotated).getPropertyValue("transform"));

        var applied = Assert.IsType<TransformGroup>(composed.Control.RenderTransform).Value;
        var serialized = document.getComputedStyle(composed).getPropertyValue("transform");
        Assert.Equal("matrix(0, 3, -2, 0, 5, 7)", serialized);
        Assert.Equal(Serialize(applied), serialized);

        var interleavedApplied = Assert.IsType<TransformGroup>(interleaved.Control.RenderTransform).Value;
        var interleavedSerialized = document.getComputedStyle(interleaved).getPropertyValue("transform");
        Assert.Equal("matrix(0, 1, -1, 0, 2, 9)", interleavedSerialized);
        Assert.Equal(Serialize(interleavedApplied), interleavedSerialized);

        window.Close();
        Dispatcher.UIThread.RunJobs();

        AvaloniaDomElement Append(string className)
        {
            var element = HostTestUtilities.GetElement(document.createElement("div"));
            element.className = className;
            HostTestUtilities.GetElement(document.body).appendChild(element);
            return element;
        }
    }

    [AvaloniaFact]
    [Trait("Runtime", "V8Native")]
    public void NativeV8ComputedStyleReadsAppliedTransformMatrices()
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
                document.body.appendChild(target);
                const values = [getComputedStyle(target).transform];
                for (const transform of [
                  'translate(0, 0)',
                  'translate(-1px, -1px)',
                  'scale(2, 3)',
                  'rotate(90deg)',
                  'translate(5px, 7px) scale(2, 3) rotate(90deg)',
                  'translate(5px, 7px) rotate(90deg) translate(2px, 3px)'
                ]) {
                  target.style.transform = transform;
                  values.push(getComputedStyle(target).transform);
                }
                globalThis.__computedTransformValues = values;
                """, "computed-transform-regression.js");

            using var result = JsonDocument.Parse(Convert.ToString(runtime.Engine.Evaluate(
                "JSON.stringify(globalThis.__computedTransformValues)")) ?? "[]");
            var values = result.RootElement.EnumerateArray().Select(item => item.GetString()).ToArray();
            Assert.Equal("none", values[0]);
            Assert.Equal("matrix(1, 0, 0, 1, 0, 0)", values[1]);
            Assert.Equal("matrix(1, 0, 0, 1, -1, -1)", values[2]);
            Assert.Equal("matrix(2, 0, 0, 3, 0, 0)", values[3]);
            Assert.Equal("matrix(0, 1, -1, 0, 0, 0)", values[4]);
            Assert.Equal("matrix(0, 3, -2, 0, 5, 7)", values[5]);
            Assert.Equal("matrix(0, 1, -1, 0, 2, 9)", values[6]);
            Assert.Empty(host.JavaScriptExceptionDiagnostics);
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

    private static string Serialize(Matrix matrix)
        => $"matrix({Format(matrix.M11)}, {Format(matrix.M12)}, {Format(matrix.M21)}, " +
           $"{Format(matrix.M22)}, {Format(matrix.M31)}, {Format(matrix.M32)})";

    private static string Format(double value)
    {
        if (Math.Abs(value) < 1e-7) return "0";
        var integer = Math.Round(value);
        if (Math.Abs(value - integer) < 1e-7) value = integer;
        return value.ToString("G15", CultureInfo.InvariantCulture).ToLowerInvariant();
    }
}

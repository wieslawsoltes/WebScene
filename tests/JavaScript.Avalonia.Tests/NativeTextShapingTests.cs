using HtmlML.Backends.Avalonia.Native;
using Xunit;

namespace JavaScript.Avalonia.Tests;

public sealed class NativeTextShapingTests
{
    [Fact]
    public void MacSystemUiWidthScaleMatchesManagedRendererProfile()
    {
        var actual = NativeTextShaping.ResolveWidthScale(
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400);
        var expected = OperatingSystem.IsMacOS() ? 1.0408f : 1f;

        Assert.Equal(expected, actual, precision: 4);
        Assert.Equal(1f, NativeTextShaping.ResolveWidthScale("sans-serif", 13, 400));
    }

    [Fact]
    public void MacSystemUiNumericRunsUseEqualTabularAdvances()
    {
        var first = NativeTextShaping.Measure(
            "189.39",
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400,
            0,
            0);
        var second = NativeTextShaping.Measure(
            "190.79",
            "-apple-system, BlinkMacSystemFont, sans-serif",
            13,
            400,
            0,
            0);

        if (OperatingSystem.IsMacOS())
        {
            Assert.Equal(first.AdvanceWidth, second.AdvanceWidth, precision: 3);
        }
    }
}

namespace WebScene.WebPlatformSubset.Runner;

internal static class VisualColorOracle
{
    internal static (byte Red, byte Green, byte Blue) Parse(string value)
    {
        if (value.Length != 7 || value[0] != '#'
            || !byte.TryParse(value.AsSpan(1, 2), System.Globalization.NumberStyles.HexNumber,
                null, out var red)
            || !byte.TryParse(value.AsSpan(3, 2), System.Globalization.NumberStyles.HexNumber,
                null, out var green)
            || !byte.TryParse(value.AsSpan(5, 2), System.Globalization.NumberStyles.HexNumber,
                null, out var blue))
        {
            throw new InvalidDataException(
                $"Visual color '{value}' must use #RRGGBB syntax.");
        }
        return (red, green, blue);
    }

    internal static long Count(WptRenderSnapshot snapshot, string color)
    {
        var (red, green, blue) = Parse(color);
        long count = 0;
        for (var offset = 0; offset + 3 < snapshot.Pixels.Length; offset += 4)
        {
            if (snapshot.Pixels[offset] == blue
                && snapshot.Pixels[offset + 1] == green
                && snapshot.Pixels[offset + 2] == red
                && snapshot.Pixels[offset + 3] != 0)
            {
                ++count;
            }
        }
        return count;
    }

    internal static bool Passes(VisualColorCheck check, long count)
        => (!check.MinimumPixels.HasValue || count >= check.MinimumPixels.Value)
           && (!check.MaximumPixels.HasValue || count <= check.MaximumPixels.Value);

    internal static string DescribeBounds(VisualColorCheck check)
        => check.MinimumPixels.HasValue && check.MaximumPixels.HasValue
            ? $"between {check.MinimumPixels.Value} and {check.MaximumPixels.Value}"
            : check.MinimumPixels.HasValue
                ? $"at least {check.MinimumPixels.Value}"
                : $"at most {check.MaximumPixels!.Value}";
}

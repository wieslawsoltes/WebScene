namespace WebScene.WebPlatformSubset.Runner;

internal static class VisualColorOracle
{
    internal readonly record struct PixelBounds(int Left, int Top, int Right, int Bottom)
    {
        public override string ToString() => $"[{Left},{Top}..{Right},{Bottom}]";
    }

    internal readonly record struct GapObservation(
        bool Passed,
        int? GapPixels,
        PixelBounds? FirstBounds,
        PixelBounds? SecondBounds,
        string Message);

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

    internal static PixelBounds? FindBounds(WptRenderSnapshot snapshot, string color)
    {
        var (red, green, blue) = Parse(color);
        var left = snapshot.PixelSize.Width;
        var top = snapshot.PixelSize.Height;
        var right = -1;
        var bottom = -1;
        for (var offset = 0; offset + 3 < snapshot.Pixels.Length; offset += 4)
        {
            if (snapshot.Pixels[offset] != blue
                || snapshot.Pixels[offset + 1] != green
                || snapshot.Pixels[offset + 2] != red
                || snapshot.Pixels[offset + 3] == 0)
            {
                continue;
            }

            var pixel = offset / 4;
            var x = pixel % snapshot.PixelSize.Width;
            var y = pixel / snapshot.PixelSize.Width;
            left = Math.Min(left, x);
            top = Math.Min(top, y);
            right = Math.Max(right, x);
            bottom = Math.Max(bottom, y);
        }

        return right < 0 ? null : new PixelBounds(left, top, right, bottom);
    }

    internal static GapObservation MeasureGap(
        WptRenderSnapshot snapshot,
        VisualColorGapCheck check)
    {
        var first = FindBounds(snapshot, check.FirstColor);
        var second = FindBounds(snapshot, check.SecondColor);
        if (first is null || second is null)
        {
            return new GapObservation(
                false,
                null,
                first,
                second,
                $"Cannot measure the {check.Axis} gap because "
                + $"{(first is null ? check.FirstColor : check.SecondColor)} has no visible pixels.");
        }

        var gap = check.Axis switch
        {
            "horizontal" => second.Value.Left - first.Value.Right - 1,
            "vertical" => second.Value.Top - first.Value.Bottom - 1,
            _ => throw new InvalidDataException($"Unknown visual gap axis '{check.Axis}'.")
        };
        var passed = (!check.MinimumPixels.HasValue || gap >= check.MinimumPixels.Value)
            && (!check.MaximumPixels.HasValue || gap <= check.MaximumPixels.Value);
        return new GapObservation(
            passed,
            gap,
            first,
            second,
            $"Observed {gap}px {check.Axis} gap from {check.FirstColor} {first.Value} "
            + $"to {check.SecondColor} {second.Value}; expected {DescribeBounds(check)}.");
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

    internal static string DescribeBounds(VisualColorGapCheck check)
        => check.MinimumPixels.HasValue && check.MaximumPixels.HasValue
            ? $"between {check.MinimumPixels.Value}px and {check.MaximumPixels.Value}px"
            : check.MinimumPixels.HasValue
                ? $"at least {check.MinimumPixels.Value}px"
                : $"at most {check.MaximumPixels!.Value}px";
}

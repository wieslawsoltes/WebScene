namespace WebScene.Css;

/// <summary>
/// Converts finalized flex main sizes to Chromium-compatible layout units while
/// conserving the quantized line total. CSS flexible lengths are resolved as
/// real numbers; the browser layout engine then retains them at 1/64 CSS-pixel
/// precision instead of rounding each item independently to a device pixel.
/// </summary>
internal static class CssSubpixelFlexAllocator
{
    internal const double UnitsPerCssPixel = 64d;

    internal static void QuantizeFinalMainSizes(IList<double> sizes)
    {
        ArgumentNullException.ThrowIfNull(sizes);
        if (sizes.Count == 0)
        {
            return;
        }

        var floorUnits = new long[sizes.Count];
        var remainders = new double[sizes.Count];
        var total = 0d;
        long allocatedUnits = 0;
        for (var index = 0; index < sizes.Count; index++)
        {
            var size = sizes[index];
            if (!double.IsFinite(size) || size < 0)
            {
                return;
            }

            total += size;
            if (!double.IsFinite(total))
            {
                return;
            }
            var scaled = size * UnitsPerCssPixel;
            if (!double.IsFinite(scaled) || scaled > long.MaxValue - sizes.Count)
            {
                return;
            }
            var floor = checked((long)Math.Floor(scaled));
            if (floor > long.MaxValue - allocatedUnits)
            {
                return;
            }
            floorUnits[index] = floor;
            remainders[index] = scaled - floor;
            allocatedUnits += floor;
        }

        var scaledTotal = total * UnitsPerCssPixel;
        if (!double.IsFinite(scaledTotal) || scaledTotal > long.MaxValue - sizes.Count)
        {
            return;
        }
        var targetUnits = (long)Math.Round(scaledTotal, MidpointRounding.AwayFromZero);
        var residualUnits = targetUnits - allocatedUnits;
        if (residualUnits < 0 || residualUnits > sizes.Count)
        {
            return;
        }

        var residualOrder = Enumerable.Range(0, sizes.Count)
            .OrderByDescending(index => remainders[index])
            // Chromium gives the later item the extra layout unit when equal
            // flex factors produce equal mathematical remainders.
            .ThenByDescending(static index => index)
            .ToArray();
        for (var residualIndex = 0; residualIndex < residualUnits; residualIndex++)
        {
            floorUnits[residualOrder[residualIndex]]++;
        }

        for (var index = 0; index < sizes.Count; index++)
        {
            sizes[index] = floorUnits[index] / UnitsPerCssPixel;
        }
    }
}

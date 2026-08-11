namespace WebScene.WebPlatformSubset.Runner;

internal static class VisualColorOracle
{
    private static readonly (int X, int Y)[] ComponentDirections =
        [(-1, 0), (1, 0), (0, -1), (0, 1)];

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

    internal readonly record struct ComponentObservation(bool Passed, string Message);

    internal readonly record struct ForegroundOffsetObservation(
        bool Passed,
        int? OffsetPixels,
        PixelBounds? FirstBounds,
        PixelBounds? SecondBounds,
        string Message);

    internal readonly record struct ComponentColorRegionObservation(
        bool Passed,
        long? Pixels,
        PixelBounds? AnchorBounds,
        PixelBounds? RegionBounds,
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

    internal static ComponentObservation InspectComponent(
        WptRenderSnapshot snapshot,
        VisualComponentCheck check)
    {
        var regionLeft = Math.Clamp(check.X, 0, snapshot.PixelSize.Width);
        var regionTop = Math.Clamp(check.Y, 0, snapshot.PixelSize.Height);
        var regionRight = Math.Clamp(check.X + check.Width, regionLeft, snapshot.PixelSize.Width);
        var regionBottom = Math.Clamp(check.Y + check.Height, regionTop, snapshot.PixelSize.Height);
        var regionWidth = regionRight - regionLeft;
        var regionHeight = regionBottom - regionTop;
        var visited = new bool[checked(regionWidth * regionHeight)];
        (PixelBounds Bounds, int Pixels, double FillRatio)? best = null;

        bool IsForeground(int x, int y)
        {
            var offset = checked((y * snapshot.PixelSize.Width + x) * 4);
            if (snapshot.Pixels[offset + 3] == 0) return false;
            var luminance = (snapshot.Pixels[offset + 2] * 299
                    + snapshot.Pixels[offset + 1] * 587
                    + snapshot.Pixels[offset] * 114)
                / 1000;
            return luminance <= check.MaximumLuminance;
        }

        bool Passes(int width, int height, int pixels, double fillRatio)
            => (!check.MinimumWidth.HasValue || width >= check.MinimumWidth.Value)
               && (!check.MaximumWidth.HasValue || width <= check.MaximumWidth.Value)
               && (!check.MinimumHeight.HasValue || height >= check.MinimumHeight.Value)
               && (!check.MaximumHeight.HasValue || height <= check.MaximumHeight.Value)
               && (!check.MinimumPixels.HasValue || pixels >= check.MinimumPixels.Value)
               && (!check.MinimumFillRatio.HasValue || fillRatio >= check.MinimumFillRatio.Value);

        for (var localY = 0; localY < regionHeight; ++localY)
        {
            for (var localX = 0; localX < regionWidth; ++localX)
            {
                var start = localY * regionWidth + localX;
                if (visited[start] || !IsForeground(regionLeft + localX, regionTop + localY))
                {
                    visited[start] = true;
                    continue;
                }

                var queue = new Queue<int>();
                queue.Enqueue(start);
                visited[start] = true;
                var left = localX;
                var right = localX;
                var top = localY;
                var bottom = localY;
                var pixels = 0;
                while (queue.TryDequeue(out var current))
                {
                    var x = current % regionWidth;
                    var y = current / regionWidth;
                    ++pixels;
                    left = Math.Min(left, x);
                    right = Math.Max(right, x);
                    top = Math.Min(top, y);
                    bottom = Math.Max(bottom, y);
                    foreach (var (offsetX, offsetY) in ComponentDirections)
                    {
                        var nextX = x + offsetX;
                        var nextY = y + offsetY;
                        if (nextX < 0 || nextX >= regionWidth || nextY < 0 || nextY >= regionHeight)
                        {
                            continue;
                        }
                        var next = nextY * regionWidth + nextX;
                        if (visited[next]) continue;
                        visited[next] = true;
                        if (IsForeground(regionLeft + nextX, regionTop + nextY)) queue.Enqueue(next);
                    }
                }

                var width = right - left + 1;
                var height = bottom - top + 1;
                var fillRatio = pixels / (double)(width * height);
                var bounds = new PixelBounds(
                    regionLeft + left,
                    regionTop + top,
                    regionLeft + right,
                    regionTop + bottom);
                if (Passes(width, height, pixels, fillRatio))
                {
                    return new ComponentObservation(
                        true,
                        $"Found foreground component {bounds}, {width}x{height}, {pixels} pixels, "
                        + $"fill {fillRatio:P1} in region [{regionLeft},{regionTop}..{regionRight - 1},{regionBottom - 1}].");
                }
                if (best is null || pixels > best.Value.Pixels)
                {
                    best = (bounds, pixels, fillRatio);
                }
            }
        }

        var diagnostic = best is null
            ? "no foreground component"
            : $"best component {best.Value.Bounds}, {best.Value.Pixels} pixels, "
              + $"fill {best.Value.FillRatio:P1}";
        return new ComponentObservation(
            false,
            $"Component shape check failed in region [{regionLeft},{regionTop}..{regionRight - 1},{regionBottom - 1}]: {diagnostic}.");
    }

    internal static ForegroundOffsetObservation MeasureForegroundOffset(
        WptRenderSnapshot snapshot,
        VisualForegroundOffsetCheck check)
    {
        var anchor = FindBounds(snapshot, check.AnchorColor);
        if (anchor is null)
        {
            return new ForegroundOffsetObservation(
                false,
                null,
                null,
                null,
                $"Cannot measure foreground offset because anchor color {check.AnchorColor} has no visible pixels.");
        }

        var firstRegion = new PixelBounds(
            anchor.Value.Left + check.HorizontalInset,
            anchor.Value.Top + check.FirstTop,
            anchor.Value.Right - check.HorizontalInset,
            anchor.Value.Top + check.FirstTop + check.FirstHeight - 1);
        var secondRegion = new PixelBounds(
            anchor.Value.Left + check.HorizontalInset,
            anchor.Value.Top + check.SecondTop,
            anchor.Value.Right - check.HorizontalInset,
            anchor.Value.Top + check.SecondTop + check.SecondHeight - 1);
        var first = FindLeftmostForegroundComponent(snapshot, firstRegion, check);
        var second = FindLeftmostForegroundComponent(snapshot, secondRegion, check);
        if (first is null || second is null)
        {
            return new ForegroundOffsetObservation(
                false,
                null,
                first,
                second,
                $"Cannot measure foreground offset in anchor {anchor.Value}: "
                + $"{(first is null ? "first" : "second")} band has no qualifying component.");
        }

        var offset = first.Value.Left - second.Value.Left;
        var passed = (!check.MinimumOffsetPixels.HasValue
                || offset >= check.MinimumOffsetPixels.Value)
            && (!check.MaximumOffsetPixels.HasValue
                || offset <= check.MaximumOffsetPixels.Value);
        return new ForegroundOffsetObservation(
            passed,
            offset,
            first,
            second,
            $"Observed {offset}px first-to-second foreground left-edge offset "
            + $"({first.Value.Left}px to {second.Value.Left}px) in anchor {anchor.Value}; "
            + $"expected {DescribeBounds(check)}.");
    }

    private static PixelBounds? FindLeftmostForegroundComponent(
        WptRenderSnapshot snapshot,
        PixelBounds requestedRegion,
        VisualForegroundOffsetCheck check)
    {
        var left = Math.Clamp(requestedRegion.Left, 0, snapshot.PixelSize.Width);
        var top = Math.Clamp(requestedRegion.Top, 0, snapshot.PixelSize.Height);
        var right = Math.Clamp(requestedRegion.Right + 1, left, snapshot.PixelSize.Width);
        var bottom = Math.Clamp(requestedRegion.Bottom + 1, top, snapshot.PixelSize.Height);
        var width = right - left;
        var height = bottom - top;
        var visited = new bool[checked(width * height)];
        PixelBounds? leftmost = null;

        bool IsForeground(int x, int y)
        {
            var offset = checked((y * snapshot.PixelSize.Width + x) * 4);
            if (snapshot.Pixels[offset + 3] == 0) return false;
            var luminance = (snapshot.Pixels[offset + 2] * 299
                    + snapshot.Pixels[offset + 1] * 587
                    + snapshot.Pixels[offset] * 114)
                / 1000;
            return luminance <= check.MaximumLuminance;
        }

        for (var localY = 0; localY < height; ++localY)
        {
            for (var localX = 0; localX < width; ++localX)
            {
                var start = localY * width + localX;
                if (visited[start] || !IsForeground(left + localX, top + localY))
                {
                    visited[start] = true;
                    continue;
                }

                var queue = new Queue<int>();
                queue.Enqueue(start);
                visited[start] = true;
                var componentLeft = localX;
                var componentRight = localX;
                var componentTop = localY;
                var componentBottom = localY;
                var pixels = 0;
                while (queue.TryDequeue(out var current))
                {
                    var x = current % width;
                    var y = current / width;
                    ++pixels;
                    componentLeft = Math.Min(componentLeft, x);
                    componentRight = Math.Max(componentRight, x);
                    componentTop = Math.Min(componentTop, y);
                    componentBottom = Math.Max(componentBottom, y);
                    foreach (var (offsetX, offsetY) in ComponentDirections)
                    {
                        var nextX = x + offsetX;
                        var nextY = y + offsetY;
                        if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height)
                        {
                            continue;
                        }
                        var next = nextY * width + nextX;
                        if (visited[next]) continue;
                        visited[next] = true;
                        if (IsForeground(left + nextX, top + nextY)) queue.Enqueue(next);
                    }
                }

                var componentHeight = componentBottom - componentTop + 1;
                if (componentHeight < check.MinimumComponentHeight
                    || pixels < check.MinimumComponentPixels)
                {
                    continue;
                }
                var bounds = new PixelBounds(
                    left + componentLeft,
                    top + componentTop,
                    left + componentRight,
                    top + componentBottom);
                if (leftmost is null || bounds.Left < leftmost.Value.Left) leftmost = bounds;
            }
        }
        return leftmost;
    }

    internal static ComponentColorRegionObservation InspectComponentColorRegion(
        WptRenderSnapshot snapshot,
        VisualComponentColorRegionCheck check)
    {
        var (red, green, blue) = Parse(check.Color);
        var width = snapshot.PixelSize.Width;
        var height = snapshot.PixelSize.Height;
        var visited = new bool[checked(width * height)];
        var anchors = new List<PixelBounds>();

        bool IsColor(int x, int y)
        {
            var offset = checked((y * width + x) * 4);
            return snapshot.Pixels[offset] == blue
                && snapshot.Pixels[offset + 1] == green
                && snapshot.Pixels[offset + 2] == red
                && snapshot.Pixels[offset + 3] != 0;
        }

        for (var y = 0; y < height; ++y)
        {
            for (var x = 0; x < width; ++x)
            {
                var start = y * width + x;
                if (visited[start] || !IsColor(x, y))
                {
                    visited[start] = true;
                    continue;
                }
                var queue = new Queue<int>();
                queue.Enqueue(start);
                visited[start] = true;
                var left = x;
                var right = x;
                var top = y;
                var bottom = y;
                while (queue.TryDequeue(out var current))
                {
                    var currentX = current % width;
                    var currentY = current / width;
                    left = Math.Min(left, currentX);
                    right = Math.Max(right, currentX);
                    top = Math.Min(top, currentY);
                    bottom = Math.Max(bottom, currentY);
                    foreach (var (offsetX, offsetY) in ComponentDirections)
                    {
                        var nextX = currentX + offsetX;
                        var nextY = currentY + offsetY;
                        if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height)
                        {
                            continue;
                        }
                        var next = nextY * width + nextX;
                        if (visited[next]) continue;
                        visited[next] = true;
                        if (IsColor(nextX, nextY)) queue.Enqueue(next);
                    }
                }
                var bounds = new PixelBounds(left, top, right, bottom);
                if (right - left + 1 >= check.MinimumAnchorWidth
                    && bottom - top + 1 >= check.MinimumAnchorHeight)
                {
                    anchors.Add(bounds);
                }
            }
        }

        anchors.Sort((first, second) =>
        {
            var horizontal = first.Left.CompareTo(second.Left);
            return horizontal != 0 ? horizontal : first.Top.CompareTo(second.Top);
        });
        if (check.ComponentIndex >= anchors.Count)
        {
            return new ComponentColorRegionObservation(
                false,
                null,
                null,
                null,
                $"Found {anchors.Count} qualifying {check.Color} components; "
                + $"component index {check.ComponentIndex} is unavailable.");
        }

        var anchor = anchors[check.ComponentIndex];
        var regionLeft = Math.Clamp(anchor.Left + check.X, 0, width);
        var regionTop = Math.Clamp(anchor.Top + check.Y, 0, height);
        var regionRight = Math.Clamp(regionLeft + check.Width, regionLeft, width);
        var regionBottom = Math.Clamp(regionTop + check.Height, regionTop, height);
        long pixels = 0;
        for (var y = regionTop; y < regionBottom; ++y)
        {
            for (var x = regionLeft; x < regionRight; ++x)
            {
                if (IsColor(x, y)) ++pixels;
            }
        }
        var passed = (!check.MinimumPixels.HasValue || pixels >= check.MinimumPixels.Value)
            && (!check.MaximumPixels.HasValue || pixels <= check.MaximumPixels.Value);
        var region = new PixelBounds(
            regionLeft,
            regionTop,
            Math.Max(regionLeft, regionRight - 1),
            Math.Max(regionTop, regionBottom - 1));
        return new ComponentColorRegionObservation(
            passed,
            pixels,
            anchor,
            region,
            $"Observed {pixels} exact {check.Color} pixels in component {check.ComponentIndex} "
            + $"anchor {anchor}, relative region {region}; expected {DescribeBounds(check)}.");
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

    internal static string DescribeBounds(VisualForegroundOffsetCheck check)
        => check.MinimumOffsetPixels.HasValue && check.MaximumOffsetPixels.HasValue
            ? $"between {check.MinimumOffsetPixels.Value}px and {check.MaximumOffsetPixels.Value}px"
            : check.MinimumOffsetPixels.HasValue
                ? $"at least {check.MinimumOffsetPixels.Value}px"
                : $"at most {check.MaximumOffsetPixels!.Value}px";

    internal static string DescribeBounds(VisualComponentColorRegionCheck check)
        => check.MinimumPixels.HasValue && check.MaximumPixels.HasValue
            ? $"between {check.MinimumPixels.Value} and {check.MaximumPixels.Value}"
            : check.MinimumPixels.HasValue
                ? $"at least {check.MinimumPixels.Value}"
                : $"at most {check.MaximumPixels!.Value}";
}

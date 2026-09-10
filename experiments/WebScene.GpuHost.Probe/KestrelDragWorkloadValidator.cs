internal static class KestrelDragWorkloadValidator
{
    internal static (double X, double Y) PanOffset(int step, bool circular)
    {
        var phase = (step - 1) % 80 + 1;
        if (circular) return (40 * (1 - Math.Cos(phase * Math.PI / 40)), 40 * Math.Sin(phase * Math.PI / 40));
        var distance = phase <= 40 ? phase * 4 : (80 - phase) * 4;
        return (distance, distance / 4.0);
    }

    internal static void Validate(string diagnostics, double x, double y, bool sidebar = false, int panCycles = 1, bool circular = false)
    {
        if (panCycles is < 1 or > 120) throw new ArgumentOutOfRangeException(nameof(panCycles));
        using var parsed = System.Text.Json.JsonDocument.Parse(diagnostics);
        var root = parsed.RootElement;
        var events = root.GetProperty("events").EnumerateArray().ToArray();
        if (root.GetProperty("errors").GetInt32() != 0 || root.GetProperty("panning").GetBoolean()
            || events.Length < 3 || events[0].GetProperty("type").GetString() != "pointerdown"
            || events[^1].GetProperty("type").GetString() != "pointerup")
            throw new InvalidOperationException("Invalid Kestrel drag workload: missing gesture boundary or application error.");
        static bool At(System.Text.Json.JsonElement e, double px, double py) =>
            Math.Abs(e.GetProperty("x").GetDouble() - px) < 0.1
            && Math.Abs(e.GetProperty("y").GetDouble() - py) < 0.1;
        if (!At(events[0], x, y) || !At(events[^1], sidebar ? x + 120 : x, y)
            || events[0].GetProperty("button").GetInt32() != (sidebar ? 0 : 2)
            || events[^1].GetProperty("button").GetInt32() != (sidebar ? 0 : 2))
            throw new InvalidOperationException("Invalid Kestrel drag workload: unexpected gesture boundary.");
        // Coalescing may omit moves, but delivered moves must remain an ordered
        // subsequence of the injected path. This detects extra routed input;
        // it does not prove the provenance of identical-coordinate input.
        var nextStep = 1;
        foreach (var e in events.Skip(1).Take(events.Length - 2))
        {
            if (e.GetProperty("type").GetString() != "pointermove"
                || e.GetProperty("buttons").GetInt32() != (sidebar ? 1 : 2))
                throw new InvalidOperationException("Invalid Kestrel drag workload: unexpected pointer event.");
            var matched = false;
            while (nextStep <= (sidebar ? 60 : 80 * panCycles))
            {
                var step = nextStep++;
                var offset = sidebar ? (X: step * 2.0, Y: 0.0) : PanOffset(step, circular);
                if (!At(e, x + offset.X, y + offset.Y)) continue;
                matched = true;
                break;
            }
            if (!matched)
                throw new InvalidOperationException("Invalid Kestrel drag workload: moves differ from injected path; discard performance comparison.");
        }
    }

}

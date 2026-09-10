using System.Diagnostics;
using System.Text.Json;
using WebScene.Backends.Avalonia.Native;

namespace WebScene.NativeEngine.Benchmarks;

internal static class VariableFontProbe
{
    internal static int Run(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("Supply a variable font file path.");
        var data = File.ReadAllBytes(args[0]);
        var before = NativeTextShaping.GetVariableFontMetrics();
        var cold = new List<double>();
        for (var i = 0; i < 30; i++)
        {
            using var registry = NativeTextShaping.CreateWebTypefaceRegistry(true);
            registry.Register("Probe", data);
            var start = Stopwatch.GetTimestamp();
            NativeTextShaping.ResolveTypeface("Probe", 700, registry);
            cold.Add(Stopwatch.GetElapsedTime(start).TotalMilliseconds);
        }
        var disabled = new List<double>();
        var enabled = new List<double>();
        using (var baseline = NativeTextShaping.CreateWebTypefaceRegistry(false))
        using (var candidate = NativeTextShaping.CreateWebTypefaceRegistry(true))
        {
            baseline.Register("Probe", data);
            candidate.Register("Probe", data);
            double Measure(NativeTextShaping.WebTypefaceRegistry registry)
            {
                var start = Stopwatch.GetTimestamp();
                for (var i = 0; i < 10000; i++)
                    NativeTextShaping.Measure("Release Notes: Version 2.2.2", "Probe", 14, 700, 0, 0, 0, registry);
                return Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            }
            Measure(baseline); Measure(candidate);
            var warmConversions = NativeTextShaping.GetVariableFontMetrics().Conversions;
            for (var i = 0; i < 9; i++)
            {
                if (i % 2 == 0) { disabled.Add(Measure(baseline)); enabled.Add(Measure(candidate)); }
                else { enabled.Add(Measure(candidate)); disabled.Add(Measure(baseline)); }
            }
            if (warmConversions != NativeTextShaping.GetVariableFontMetrics().Conversions)
                throw new InvalidOperationException("Repeated warm conversion.");
        }
        cold.Sort(); disabled.Sort(); enabled.Sort();
        var after = NativeTextShaping.GetVariableFontMetrics();
        var result = new
        {
            ColdP95Ms = cold[(int)Math.Ceiling(cold.Count * .95) - 1],
            WarmDisabledMedianMs = disabled[4], WarmEnabledMedianMs = enabled[4],
            WarmRatio = enabled[4] / disabled[4],
            Released = after.Bytes == before.Bytes && after.Instances == before.Instances,
            Failures = after.Failures - before.Failures,
            Scope = "Font conversion and warm text measurement only; not an interactive chart frame benchmark."
        };
        Console.WriteLine(JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        return result.ColdP95Ms < 50 && result.WarmRatio <= 1.05 && result.Released && result.Failures == 0 ? 0 : 1;
    }
}

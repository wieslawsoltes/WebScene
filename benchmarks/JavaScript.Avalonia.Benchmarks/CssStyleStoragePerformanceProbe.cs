using System.Diagnostics;
using System.Text;
using Avalonia.Controls;
using Avalonia.Threading;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Measures retained ordinary computed-style storage for a large sibling set.
/// Instance-specific custom properties deliberately differ so this also verifies
/// that ordinary style sharing is independent of inherited custom-property state.
/// </summary>
internal static class CssStyleStoragePerformanceProbe
{
    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var elementCount = ParsePositiveInt(args, "--elements", 2000);
        var variantCount = ParsePositiveInt(args, "--variants", 16);
        var probeMediaResize = args.Contains("--media-resize", StringComparer.OrdinalIgnoreCase);
        var root = new CssLayoutPanel { Width = 1000, Height = 616 };
        var window = new Window
        {
            Width = 1000,
            Height = 616,
            Content = root
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window)
        {
            CollectPerformanceMetrics = true
        };
        try
        {
            var document = host.Document;
            var body = AsElement(document.body);
            var style = AsElement(document.createElement("style"));
            style.textContent = CreateStyleSheet(variantCount, probeMediaResize);
            document.head.appendChild(style);

            var elements = new AvaloniaDomElement[elementCount];
            for (var index = 0; index < elementCount; index++)
            {
                var element = AsElement(document.createElement("div"));
                element.className = $"storage-item variant-{index % variantCount}";
                element.style.cssText = $"--instance-token: {index}";
                body.appendChild(element);
                elements[index] = element;
            }

            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
            var started = Stopwatch.GetTimestamp();
            document.EnsureStylesCurrent();
            var elapsed = Stopwatch.GetElapsedTime(started);
            var allocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;

            var uniqueValues = new HashSet<object>(ReferenceEqualityComparer.Instance);
            var uniqueDeclarations = new HashSet<object>(ReferenceEqualityComparer.Instance);
            foreach (var element in elements)
            {
                uniqueValues.Add(element.ComputedOrdinaryStyleStorage);
                uniqueDeclarations.Add(element.DeclaredOrdinaryStyleStorage);
            }

            Console.WriteLine(
                $"WebScene ordinary style storage ({elementCount} elements, {variantCount} variants)");
            Console.WriteLine(
                $"Initial ensure: {elapsed.TotalMilliseconds:F3} ms, {allocated / 1024d:F2} KB");
            Console.WriteLine(
                $"Stylesheet phases: normalize={document.StylesheetNormalizationDuration.TotalMilliseconds:F3} ms/" +
                $"{document.StylesheetNormalizationAllocatedBytes / 1024d:F2} KB, " +
                $"parse={document.StylesheetParserDuration.TotalMilliseconds:F3} ms/" +
                $"{document.StylesheetParserAllocatedBytes / 1024d:F2} KB, " +
                $"compile={document.StylesheetRuleCompilationDuration.TotalMilliseconds:F3} ms/" +
                $"{document.StylesheetRuleCompilationAllocatedBytes / 1024d:F2} KB, " +
                $"index={document.StylesheetIndexingDuration.TotalMilliseconds:F3} ms/" +
                $"{document.StylesheetIndexingAllocatedBytes / 1024d:F2} KB, " +
                $"cacheHits={document.CompiledStylesheetCacheHitCount}, " +
                $"cacheEntries={document.CompiledStylesheetCacheEntryCount}");
            Console.WriteLine(
                $"Style phases: cascade={document.ElementStyleCascadeDuration.TotalMilliseconds:F3} ms/" +
                $"{document.ElementStyleCascadeAllocatedBytes / 1024d:F2} KB, " +
                $"match={document.ElementStyleRuleMatchDuration.TotalMilliseconds:F3} ms/" +
                $"{document.ElementStyleRuleMatchAllocatedBytes / 1024d:F2} KB, " +
                $"initialize={document.ElementStyleValueInitializationDuration.TotalMilliseconds:F3} ms/" +
                $"{document.ElementStyleValueInitializationAllocatedBytes / 1024d:F2} KB, " +
                $"resolve={document.ElementStyleResolutionDuration.TotalMilliseconds:F3} ms/" +
                $"{document.ElementStyleResolutionAllocatedBytes / 1024d:F2} KB, " +
                $"commit={document.ElementStyleCommitDuration.TotalMilliseconds:F3} ms/" +
                $"{document.ElementStyleCommitAllocatedBytes / 1024d:F2} KB");
            Console.WriteLine(
                $"Storage: value-blocks={uniqueValues.Count}, declaration-blocks={uniqueDeclarations.Count}, " +
                $"pool-entries={document.SharedOrdinaryStyleEntryCount}, pool-hits={document.SharedOrdinaryStyleHitCount}, " +
                $"template-entries={document.CascadeTemplateEntryCount}, template-hits={document.CascadeTemplateHitCount}");

            var mediaResizePassed = true;
            if (probeMediaResize)
            {
                root.Width = window.Width = 1010;
                Dispatcher.UIThread.RunJobs();
                var computesBefore = document.ElementStyleComputeCount;
                var matchesBefore = document.SelectorMatchEvaluationCount;
                var cacheHitsBefore = document.MediaQueryOutcomeCacheHitCount;
                var presentationReappliesBefore = document.ViewportPresentationReapplyElementCount;
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
                started = Stopwatch.GetTimestamp();
                document.ReconcileStylesAfterViewportResize();
                document.EnsureStylesCurrent();
                elapsed = Stopwatch.GetElapsedTime(started);
                allocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
                var computed = document.ElementStyleComputeCount - computesBefore;
                var matches = document.SelectorMatchEvaluationCount - matchesBefore;
                var cacheHits = document.MediaQueryOutcomeCacheHitCount - cacheHitsBefore;
                var presentationReapplies =
                    document.ViewportPresentationReapplyElementCount - presentationReappliesBefore;
                var mediaCacheDisabled = string.Equals(
                    Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_MEDIA_QUERY_OUTCOME_CACHE"),
                    "1",
                    StringComparison.Ordinal);
                mediaResizePassed = mediaCacheDisabled ? computed >= elementCount : computed == 0 && cacheHits == 1;
                Console.WriteLine(
                    $"Same-breakpoint media resize: {elapsed.TotalMilliseconds:F3} ms, " +
                    $"{allocated / 1024d:F2} KB, computed={computed}, matches={matches}, " +
                    $"cacheHits={cacheHits}, presentationReapplies={presentationReapplies}");
            }

            var sharingDisabled = string.Equals(
                Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_STYLE_SHARING"),
                "1",
                StringComparison.Ordinal);
            var templatesDisabled = string.Equals(
                Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_CASCADE_TEMPLATE_CACHE"),
                "1",
                StringComparison.Ordinal);
            if (sharingDisabled && templatesDisabled)
            {
                return mediaResizePassed
                       && uniqueValues.Count == elementCount && uniqueDeclarations.Count == elementCount
                    ? 0
                    : 1;
            }

            return mediaResizePassed
                   && uniqueValues.Count <= variantCount * 2
                   && uniqueDeclarations.Count <= variantCount * 2
                   && (templatesDisabled
                       || document.CascadeTemplateHitCount >= elementCount - (variantCount * 2))
                ? 0
                : 1;
        }
        finally
        {
            window.Close();
        }
    }

    private static string CreateStyleSheet(int variantCount, bool includeMediaQuery)
    {
        var css = new StringBuilder("""
            .storage-item {
                display: flex;
                box-sizing: border-box;
                width: 120px;
                min-height: 18px;
                padding: 2px 4px;
                margin: 1px;
                border-width: 1px;
                color: #123456;
                background-color: #abcdef;
                font-size: 12px;
                line-height: 16px;
            }
            """);
        for (var index = 0; index < variantCount; index++)
        {
            css.Append(".variant-").Append(index)
                .Append(" { order: ").Append(index).Append("; }\n");
        }
        if (includeMediaQuery)
        {
            css.Append("@media (min-width: 600px) { .storage-item { min-width: 40px; } }\n");
        }
        return css.ToString();
    }

    private static AvaloniaDomElement AsElement(object? value)
        => value as AvaloniaDomElement
           ?? throw new InvalidOperationException($"Expected {nameof(AvaloniaDomElement)}.");

    private static int ParsePositiveInt(string[] args, string name, int fallback)
    {
        for (var index = 0; index < args.Length - 1; index++)
        {
            if (string.Equals(args[index], name, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(args[index + 1], out var value)
                && value > 0)
            {
                return value;
            }
        }
        return fallback;
    }
}

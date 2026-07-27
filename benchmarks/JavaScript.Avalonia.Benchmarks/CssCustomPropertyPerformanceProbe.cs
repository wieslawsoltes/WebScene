using System.Diagnostics;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace JavaScript.Avalonia.Benchmarks;

/// <summary>
/// Product-free reproduction of the floating-tooltip cascade shape: one
/// positioned root updates inherited custom properties while 43 descendants
/// retain stable selectors and ordinary presentation.
/// </summary>
internal static class CssCustomPropertyPerformanceProbe
{
    internal static int Run(string[] args)
    {
        BenchmarkApp.EnsureInitialized();
        var iterations = ParsePositiveInt(args, "--iterations", 100);
        var root = new CssLayoutPanel { Width = 1000, Height = 616 };
        var window = new Window
        {
            Width = 1000,
            Height = 616,
            Content = root
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();

        using var host = new AvaloniaBrowserHost(window, enableTargetOnlyInlineStyles: true)
        {
            CollectPerformanceMetrics = true
        };
        try
        {
            var document = host.Document;
            var body = AsElement(document.body);
            var style = AsElement(document.createElement("style"));
            style.textContent = CreateStyleSheet();
            document.head.appendChild(style);

            var positioner = AsElement(document.createElement("section"));
            positioner.className = "positioner tooltip-node group-0";
            positioner.style.cssText = CreateVariables(0);
            body.appendChild(positioner);
            AvaloniaDomElement? variableConsumer = null;
            AvaloniaDomElement? passiveDescendant = null;
            for (var index = 1; index < 44; index++)
            {
                var child = AsElement(document.createElement("div"));
                var consumesVariables = index % 11 == 0;
                child.className = $"tooltip-node group-{index % 8}" +
                                  (consumesVariables ? " variable-consumer" : string.Empty);
                positioner.appendChild(child);
                if (consumesVariables)
                {
                    variableConsumer = child;
                }
                else
                {
                    passiveDescendant = child;
                }
            }

            document.EnsureStylesCurrent();
            host.ArmTargetOnlyInlineStyles();
            for (var index = 1; index <= 8; index++)
            {
                positioner.style.cssText = CreateVariables(index);
                document.EnsureStylesCurrent();
            }

            var recomputesBefore = document.StyleRecomputeCount;
            var computedBefore = document.ElementStyleComputeCount;
            var appliedBefore = document.ElementStyleApplyCount;
            var presentationAppliedBefore = document.ElementPresentationApplyCount;
            var selectorMatchesBefore = document.SelectorMatchEvaluationCount;
            var cacheHitsBefore = document.MatchedRuleCacheHitCount;
            var templateHitsBefore = document.CascadeTemplateHitCount;
            var ensureDurationBefore = document.StyleEnsureDuration;
            var ensureBytesBefore = document.StyleEnsureAllocatedBytes;
            var cascadeDurationBefore = document.ElementStyleCascadeDuration;
            var cascadeBytesBefore = document.ElementStyleCascadeAllocatedBytes;
            var matchDurationBefore = document.ElementStyleRuleMatchDuration;
            var matchBytesBefore = document.ElementStyleRuleMatchAllocatedBytes;
            var initializationDurationBefore = document.ElementStyleValueInitializationDuration;
            var initializationBytesBefore = document.ElementStyleValueInitializationAllocatedBytes;
            var resolutionDurationBefore = document.ElementStyleResolutionDuration;
            var resolutionBytesBefore = document.ElementStyleResolutionAllocatedBytes;
            var commitDurationBefore = document.ElementStyleCommitDuration;
            var commitBytesBefore = document.ElementStyleCommitAllocatedBytes;
            var pseudoDurationBefore = document.PseudoElementDuration;
            var pseudoBytesBefore = document.PseudoElementAllocatedBytes;
            var inlinePresentationBefore = document.InlinePresentationApplyCount;
            var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
            var started = Stopwatch.GetTimestamp();

            for (var index = 0; index < iterations; index++)
            {
                positioner.style.cssText = CreateVariables(index + 20);
                document.EnsureStylesCurrent();
            }

            var elapsed = Stopwatch.GetElapsedTime(started);
            var allocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
            var recomputes = document.StyleRecomputeCount - recomputesBefore;
            var computed = document.ElementStyleComputeCount - computedBefore;
            var applied = document.ElementStyleApplyCount - appliedBefore;
            var presentationApplied = document.ElementPresentationApplyCount - presentationAppliedBefore;
            var selectorMatches = document.SelectorMatchEvaluationCount - selectorMatchesBefore;
            var cacheHits = document.MatchedRuleCacheHitCount - cacheHitsBefore;
            var templateHits = document.CascadeTemplateHitCount - templateHitsBefore;
            var ensureElapsed = document.StyleEnsureDuration - ensureDurationBefore;
            var ensureAllocated = document.StyleEnsureAllocatedBytes - ensureBytesBefore;
            var cascadeElapsed = document.ElementStyleCascadeDuration - cascadeDurationBefore;
            var cascadeAllocated = document.ElementStyleCascadeAllocatedBytes - cascadeBytesBefore;
            var matchElapsed = document.ElementStyleRuleMatchDuration - matchDurationBefore;
            var matchAllocated = document.ElementStyleRuleMatchAllocatedBytes - matchBytesBefore;
            var initializationElapsed = document.ElementStyleValueInitializationDuration - initializationDurationBefore;
            var initializationAllocated = document.ElementStyleValueInitializationAllocatedBytes - initializationBytesBefore;
            var resolutionElapsed = document.ElementStyleResolutionDuration - resolutionDurationBefore;
            var resolutionAllocated = document.ElementStyleResolutionAllocatedBytes - resolutionBytesBefore;
            var commitElapsed = document.ElementStyleCommitDuration - commitDurationBefore;
            var commitAllocated = document.ElementStyleCommitAllocatedBytes - commitBytesBefore;
            var pseudoElapsed = document.PseudoElementDuration - pseudoDurationBefore;
            var pseudoAllocated = document.PseudoElementAllocatedBytes - pseudoBytesBefore;
            var inlinePresentations = document.InlinePresentationApplyCount - inlinePresentationBefore;
            var expectedLeft = $"{iterations + 19}px";
            var actualLeft = document.getComputedStyle(positioner).getPropertyValue("left");
            var inheritedAnchor = document.getComputedStyle(positioner.lastElementChild!)
                .getPropertyValue("--ui-lib-positioner-anchor-left");
            var expectedAnchor = (iterations + 19).ToString();
            var consumerTransform = document.getComputedStyle(variableConsumer!)
                .getPropertyValue("transform");
            var expectedTransform = $"matrix(1, 0, 0, 1, {iterations + 19}, 0)";
            var nativeTransform = variableConsumer!.Control.RenderTransform as TranslateTransform;
            var passiveAnchor = document.getComputedStyle(passiveDescendant!)
                .getPropertyValue("--ui-lib-positioner-anchor-left");

            Console.WriteLine(
                $"WebScene custom-property subtree spike ({iterations} mutation(s), 44 elements)");
            Console.WriteLine(
                $"Style work: recomputes={recomputes}, computed={computed}, applied={applied}, " +
                $"presentation-applied={presentationApplied}, " +
                $"inline-presentations={inlinePresentations}, " +
                $"selector-matches={selectorMatches}, matched-rule-cache-hits={cacheHits}, " +
                $"cascade-template-hits={templateHits}, cascade-template-entries={document.CascadeTemplateEntryCount}");
            Console.WriteLine(
                $"Mutation + ensure: {elapsed.TotalMilliseconds / iterations:F3} ms/pass, " +
                $"{allocated / 1024d / iterations:F2} KB/pass");
            Console.WriteLine(
                $"EnsureCurrent only: {ensureElapsed.TotalMilliseconds / iterations:F3} ms/pass, " +
                $"{ensureAllocated / 1024d / iterations:F2} KB/pass");
            Console.WriteLine(
                $"Style phases: cascade={cascadeElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{cascadeAllocated / 1024d / iterations:F2} KB, " +
                $"commit={commitElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{commitAllocated / 1024d / iterations:F2} KB, " +
                $"pseudo={pseudoElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{pseudoAllocated / 1024d / iterations:F2} KB");
            Console.WriteLine(
                $"Cascade phases: match={matchElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{matchAllocated / 1024d / iterations:F2} KB, " +
                $"initialize={initializationElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{initializationAllocated / 1024d / iterations:F2} KB, " +
                $"resolve={resolutionElapsed.TotalMilliseconds / iterations:F3} ms/" +
                $"{resolutionAllocated / 1024d / iterations:F2} KB");
            Console.WriteLine(
                $"Correctness: left={actualLeft} expected={expectedLeft}, " +
                $"consumer-transform={consumerTransform} expected={expectedTransform}, " +
                $"native-x={nativeTransform?.X}, inherited-anchor={inheritedAnchor}, " +
                $"passive-anchor={passiveAnchor} expected={expectedAnchor}");

            var expectedComputesPerPass = string.Equals(
                Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CUSTOM_PROPERTY_CONSUMER_INVALIDATION"),
                "1",
                StringComparison.Ordinal)
                    ? 44
                    : 4;
            var expectedInlinePresentationsPerPass = string.Equals(
                Environment.GetEnvironmentVariable("WEBSCENE_DISABLE_CSS_INLINE_PRESENTATION_BATCHING"),
                "1",
                StringComparison.Ordinal)
                    ? 13
                    : 1;
            return recomputes == iterations
                   && computed == iterations * expectedComputesPerPass
                   && applied == iterations * expectedComputesPerPass
                   && presentationApplied == iterations * 4L
                   && inlinePresentations == iterations * expectedInlinePresentationsPerPass
                   && string.Equals(actualLeft, expectedLeft, StringComparison.Ordinal)
                   && string.Equals(inheritedAnchor, expectedAnchor, StringComparison.Ordinal)
                   && string.Equals(passiveAnchor, expectedAnchor, StringComparison.Ordinal)
                   && string.Equals(consumerTransform, expectedTransform, StringComparison.Ordinal)
                   && nativeTransform?.X == iterations + 19
                ? 0
                : 1;
        }
        finally
        {
            window.Close();
        }
    }

    private static string CreateStyleSheet()
    {
        var css = new StringBuilder();
        for (var index = 0; index < 400; index++)
        {
            css.Append(".unrelated-").Append(index)
                .Append(" { color: #123456; padding: ").Append(index % 7).Append("px; }")
                .AppendLine();
        }
        css.AppendLine("""
            .positioner {
                position: absolute;
                left: var(--ui-lib-positioner-anchored-x);
                top: var(--ui-lib-positioner-anchored-y);
            }
            .tooltip-node {
                display: flex;
                box-sizing: border-box;
                min-width: 10px;
                min-height: 10px;
                padding: 1px 2px;
                margin: 0;
                border-width: 0;
                opacity: 1;
                visibility: visible;
                pointer-events: auto;
                font-size: 12px;
                font-weight: 400;
                line-height: 16px;
                white-space: nowrap;
                overflow: visible;
            }
            .variable-consumer {
                transform: translateX(var(--ui-lib-positioner-anchored-x));
                max-width: var(--ui-lib-private-positioner-screen-restriction-max-width);
            }
            """);
        for (var index = 0; index < 8; index++)
        {
            css.Append(".group-").Append(index)
                .Append(" { order: ").Append(index)
                .Append("; z-index: ").Append(index + 1)
                .Append("; }\n");
        }
        return css.ToString();
    }

    private static string CreateVariables(int value)
        => $"--ui-lib-positioner-anchor-width:24px;" +
           $"--ui-lib-positioner-anchor-height:0px;" +
           $"--ui-lib-positioner-anchor-top:{value};" +
           $"--ui-lib-positioner-anchor-left:{value};" +
           $"--ui-lib-positioner-content-point-x:0;" +
           $"--ui-lib-positioner-content-point-y:.5;" +
           $"--ui-lib-positioner-anchored-x:{value}px;" +
           $"--ui-lib-positioner-anchored-y:{value}px;" +
           $"--ui-lib-private-positioner-screen-restriction-max-width:{500 - value}px;" +
           $"--ui-lib-private-positioner-screen-restriction-max-height:616px;" +
           $"--ui-lib-positioner-content-height-measured:152px;" +
           $"--ui-lib-positioner-content-width-measured:200px";

    private static AvaloniaDomElement AsElement(object? value)
        => value as AvaloniaDomElement
           ?? throw new InvalidOperationException("The CSS spike could not create a DOM element.");

    private static int ParsePositiveInt(string[] args, string name, int fallback)
    {
        var index = Array.FindIndex(args, arg => string.Equals(arg, name, StringComparison.OrdinalIgnoreCase));
        return index >= 0
               && index + 1 < args.Length
               && int.TryParse(args[index + 1], out var value)
               && value > 0
            ? value
            : fallback;
    }
}

# TradingView startup style optimization

## Scope

This evaluation compares Chrome 151 with the compositor-backed
`NativeTradingViewTerminal` sample. Readiness means that the TradingView chart iframe
contains at least eight canvases and its loading indicator is hidden. Browser process
launch is excluded from the Chrome navigation measurement; WebScene's desktop wall
measurement starts immediately before `NativeWebSceneView.LoadAsync`.

The page is served by a live CDN, so wall times and the amount of deferred UI loaded at
the readiness boundary vary between processes. Native phase timings and node counts are
the primary A/B evidence.

## Baseline diagnosis

Warm Chrome reached the chart in 1.11-1.15 seconds and spent 34-38 ms in style
recalculation. The original warm WebScene path reached the chart in 1.42 seconds and
spent approximately 370 ms in connected-stylesheet recascade. Its resource cache hit
209 of 211 requests and its persistent compilation cache hit all 120 disk-cache
lookups, ruling out broken caches as the primary cause.

The original connected-link path reapplied appended rules to every existing element.
If any appended rule defined a custom property, it then rematched every element against
the complete accumulated stylesheet rule set to find all `var()`-dependent properties.

## Implementation

The optimized path:

1. Indexes stylesheet rules by every custom property referenced through `var()`.
2. Computes the transitive custom-property dependency closure once per connected
   stylesheet.
3. Builds one recascade plan per stylesheet, preserving source order and each link's
   load-event boundary.
4. Projects dependent rules through the existing tag, id, class, and attribute selector
   indexes.
5. Replays the complete cascade only for affected properties on candidate nodes,
   including variable-dependent inline declarations.

Certification builds retain
`WEBSCENE_PROBE_DISABLE_CSS_VARIABLE_DEPENDENCY_FILTER=1` as an A/B control for the
former broad scan.

## Results

Representative warm-cache certification runs against the same native binary and cache:

| Path | Stylesheet recascade | Cumulative node visits | Variable nodes | Rules loaded at readiness |
|---|---:|---:|---:|---:|
| Broad-scan control | 346.7-372.8 ms | 39,310-49,962 | 22,769-28,763 | 3,926-4,289 |
| Dependency plan | 67.1-97.0 ms | 83,949-88,921 | 469-520 | 5,704-5,843 |

The optimized recascade is 72-82% faster in absolute time despite processing roughly
twice as many cumulative node visits before the readiness boundary. Only about 0.6% of
optimized visits require variable-dependent cascade replay, compared with roughly 58%
under the broad control path. Normalized recascade cost falls from approximately
7.5-8.8 microseconds per visit to 0.75-1.16 microseconds per visit.

The optimized runs also have substantially more of TradingView's deferred UI stylesheet
graph loaded by the time the chart becomes ready. This supports the reported visual
improvement: stylesheet work no longer blocks later toolbar, sidebar, and widget chunks
for hundreds of milliseconds.

## Iframe conclusion

Warm frame hydration measured about 90-105 ms. Frame resource prefetch is already
asynchronous, although frame parsing and execution are scheduled serially. After the
style optimization, iframe hydration is no longer large enough to justify changing its
event-loop and lifecycle semantics without a separate deterministic workload showing a
remaining bottleneck.

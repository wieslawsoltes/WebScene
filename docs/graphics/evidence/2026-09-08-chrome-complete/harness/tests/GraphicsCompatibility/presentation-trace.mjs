// This source contract was inspected at the Chrome revision below. Unknown revisions stay unqualified.
export const supportedChromeRevision = "@529d9a34b491745086b59458f58a5aae8292adaa";
const sourceRoot = `https://chromium.googlesource.com/chromium/src/+/${supportedChromeRevision.slice(1)}/cc/metrics/`;

export function distribution(values) {
  if (!values.length || values.some(value => !Number.isFinite(value))) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const percentile = fraction => sorted[Math.max(0, Math.ceil(fraction * sorted.length) - 1)];
  return { count: sorted.length, min: sorted[0], median: percentile(0.5), p95: percentile(0.95),
    max: sorted.at(-1), mean: sorted.reduce((sum, value) => sum + value, 0) / sorted.length };
}

export function analyzePresentation(trace, revision) {
  const unavailable = reason => ({ status: "unavailable", reason });
  if (revision !== supportedChromeRevision) return unavailable("Chrome revision has not had its presentation trace source contract verified");
  const events = trace.traceEvents;
  if (!Array.isArray(events)) return unavailable("Missing trace event array");
  const starts = events.filter(event => event.name === "webscene-reference-pan-start" && event.ph === "I");
  const ends = events.filter(event => event.name === "webscene-reference-pan-end" && event.ph === "I");
  if (starts.length !== 1 || ends.length !== 1 || starts[0].pid !== ends[0].pid || ends[0].ts <= starts[0].ts)
    return unavailable("Missing or ambiguous interaction markers");
  const start = starts[0], end = ends[0], active = new Map(), frames = [], reporterStates = {};
  let ambiguous = false;
  const reporters = events.filter(event => event.name === "PipelineReporter" && event.pid === start.pid)
    .sort((a, b) => a.ts - b.ts);
  for (const event of reporters) {
    // Trace local IDs are strings. Do not use the 64-bit numeric surface/display IDs: JSON loses precision.
    const identity = event.id2?.local ?? event.id2?.global;
    if (!identity) continue;
    if (event.ph === "b") {
      if (active.has(identity)) ambiguous = true;
      active.set(identity, event);
    } else if (event.ph === "e") {
      const begin = active.get(identity);
      active.delete(identity);
      if (!begin || begin.ts < start.ts || begin.ts > end.ts) continue;
      const info = begin.args?.frame_reporter;
      if (!info?.state || event.ts < begin.ts || !Number.isFinite(event.ts)) { ambiguous = true; continue; }
      reporterStates[info.state] = (reporterStates[info.state] ?? 0) + 1;
      if (["STATE_PRESENTED_ALL", "STATE_PRESENTED_PARTIAL"].includes(info.state)) {
        frames.push({ beginMicroseconds: begin.ts, presentedMicroseconds: event.ts,
          sequence: info.frame_sequence, source: info.frame_source, layerTreeHost: info.layer_tree_host_id,
          partial: info.state === "STATE_PRESENTED_PARTIAL", missingContent: info.has_missing_content === true });
      }
    }
  }
  if (ambiguous) return unavailable("Ambiguous or malformed PipelineReporter event pairing");
  if ([...active.values()].some(event => event.ts >= start.ts && event.ts <= end.ts))
    return unavailable("Trace ended before all interaction reporters completed");
  const timestamps = [...new Set(frames.map(frame => frame.presentedMicroseconds))].sort((a, b) => a - b);
  if (timestamps.length < 2) return unavailable("Fewer than two distinct platform presentation feedback timestamps");
  const intervals = timestamps.slice(1).map((timestamp, index) => (timestamp - timestamps[index]) / 1000);
  return { status: "measured", source: "Presented PipelineReporter termination timestamps (platform presentation feedback)",
    sourceContract: [sourceRoot + "compositor_frame_reporting_controller.cc", sourceRoot + "compositor_frame_reporter.cc"],
    revision, rendererPid: start.pid, interactionStartMicroseconds: start.ts, interactionEndMicroseconds: end.ts,
    uniquePresentedFrames: timestamps.length,
    framesPerSecond: (timestamps.length - 1) * 1e6 / (timestamps.at(-1) - timestamps[0]),
    intervalMilliseconds: distribution(intervals), partialReporters: frames.filter(frame => frame.partial).length,
    missingContentReporters: frames.filter(frame => frame.missingContent).length,
    reporterStates, frames,
    note: "Reporter state counts include compositor bookkeeping; they are not counts of distinct application frames. Presentation feedback is separate from CPU submission and rAF delivery." };
}

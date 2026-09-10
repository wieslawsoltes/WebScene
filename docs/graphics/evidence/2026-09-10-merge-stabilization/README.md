# Merge stabilization

Native runtime revision: c65ab896. Kestrel probe rebuilt with generated JSON
serialization for pan/sidebar diagnostics; reflection serialization disabled.
Original Kestrel zip remains unchanged. Avalonia 12 sample, macOS arm64.

Combined flags: `--edit-kestrel --exercise-kestrel --pan-kestrel --sidebar-kestrel
--continuous-resize-kestrel --resize-kestrel --verify-kestrel` with `--webgpu-metal`.
Native path: staged c65ab896 engine with matching Dawn/ICU/snapshot sidecars.

Result: exit 0. Editing/undo/redo, application exercise, pan, sidebar and both
resize workloads completed. Selected runtime output:

```text
Kestrel pan workload validated (physical presentation remains unqualified).
Kestrel sidebar workload validated (physical presentation remains unqualified).
Kestrel continuous window resize workload validated (physical presentation and native user drag remain unqualified).
Kestrel uncaught JavaScript exceptions: 0
Kestrel successfully rendered scenes: 616
Kestrel WebGPU startup check passed (interaction qualification remains).
```

`pan-analysis.json` contains callback/queue timings, not physical scanout evidence.
The AOT pan/sidebar trace failure was fixed with typed records and generated
serialization; no reflection fallback was enabled.

Other checks at the same runtime revision:
- Native CTest 18/18; shared media contracts 9/9; macOS video contract 1/1.
- Frameforge packaged AOT executable, using bundle libraries/assets: media test
  passed, nine seek positions, actual RMS and track checks, worker waveform error
  1.49e-8, 30 scenes; MIME/HEAD/range/416 server verification passed.
- TradingView live desktop startup readiness passed, wall time 3259ms; this does
  not qualify interactive TradingView panning or physical 60fps.
- Graphics Python tooling: 6/6 passed.

Final pushed revision CI/package-consumer results remain the merge gate.

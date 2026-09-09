# Windows frame-rate measurement qualification

The `pacing-checkpoint-active-retry.log` result of 28.6 Hz measures completed
WebScene OnRender callbacks, not physical presentation. Independent application
diagnostics recorded 1,555 unique JavaScript requestAnimationFrame timestamps,
about 25.9 Hz, in that same run. The approximately 60 Hz composition scheduling
counter records opportunities to produce frames, not new canvas images.

Follow-up controlled pan measurements using the bounded-history evaluation build:

| Log under artifacts/windows-kestrel | Input Hz | Draw callbacks/sec | Unique RAF timestamps/sec | Workload valid |
| --- | ---: | ---: | ---: | --- |
| pacing-measurement-60hz.log | 60 | 27.0 | 23.7 | yes |
| pacing-measurement-120hz.log | 120 | 52.0 | 50.5 | yes |
| pacing-measurement-no-telemetry.log | 60 | unavailable | 53.4 | yes |

All three used real compositor-clock ticks with zero fallbacks. The draw rate
excludes the first input second; RAF rates use the interval between first and
last recorded unique RAF timestamps. These are different sample windows, so
small differences between columns must not be interpreted as dropped frames.
The no-telemetry run disables the performance snapshot instrumentation but keeps
the JavaScript RAF and input validation probes. It is not an uninstrumented run.

A further 60 Hz repeat reached 52.3 draw callbacks/sec but failed input validation
and is excluded from comparisons. Run-to-run variation prevents attribution of
the improvement to input rate or instrumentation alone. None of these results
establish physical presentation cadence, nor do they measure the user's separate
manual evaluation session.

PresentMon 2.5.1 could not start its ETW session: access denied. The current
process has a medium-integrity token and no Performance Log Users membership.
Do not substitute desktop-wide DWM refresh counts for application presentation.
`artifacts/windows-kestrel/capture-kestrel-presentation.ps1` prepares a process-ID
filtered PresentMon capture and a 60-second, 60 Hz synthetic pan. It requires an
Administrator PowerShell and writes presentation-capture.csv plus matching probe
logs. After capture, align QPC times with the injected-pan interval, validate input
and vsync, and analyze displayed versus dropped frames for the app's swap chain.
An ETW display event is still not an optical measurement of physical scanout.

## PresentMon capture follow-up

The elevated process-filtered capture printed Started/Stopped recording but
created no CSV. Its application RAF trace measured 59.62 Hz after excluding the
first second and final half-second, with 16.67 ms median intervals. Two unexpected
startup mouse moves invalidated its strict workload check.

The subsequent all-process capture produced presentation-capture-all.csv.
The recorded probe PID was 15004. There are zero rows for that PID: the CSV has
3,681 dwm.exe rows, 58 ChatGPT.exe rows, and seven WindowsTerminal.exe rows.
Consequently the missing filtered CSV is consistent with no captured app
presentation events, rather than evidence that the app did not render. DWM rows
cannot be used as the WebScene frame rate. The exact reason the app's presentation
path has no PresentMon rows remains unverified.

The all-process run passed input validation and had 3,876 real vsync ticks and
zero fallbacks. In the same trimmed RAF window its rate was 56.11 Hz, median gap
16.67 ms, 95th-percentile gap 33.34 ms, maximum gap 50.01 ms, and 226 of 3,280
intervals exceeded 25 ms. The preceding filtered run had 22 of 3,489 intervals
exceeding 25 ms. This confirms worse application pacing during the broader
capture, but is not a controlled demonstration that ETW overhead caused it.
Physical application presentation remains unqualified; do not request repeated
PresentMon captures with the same settings expecting app-specific evidence.

# Fixing Avalonia macOS cadence on mixed-refresh displays

## Problem

Avalonia's macOS render timer is application-wide and is currently created with:

```objc
CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
```

That does not bind the render clock to the display containing an Avalonia window.
In the reproduced configuration, a 30 Hz primary display and a 60 Hz secondary
display produced only 30 compositor callbacks per second even when the window was
fully on the 60 Hz display. Making the 60 Hz display primary immediately changed
the callback rate to 60 Hz.

This is an Avalonia platform-clock issue, not a WebScene publication or rendering
issue. A consumer cannot present at 60 Hz when Avalonia supplies only 30 compositor
boundaries.

The same implementation is present in Avalonia
[11.3.18](https://github.com/AvaloniaUI/Avalonia/blob/11.3.18/native/Avalonia.Native/src/OSX/PlatformRenderTimer.mm)
and
[12.1.0](https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/native/Avalonia.Native/src/OSX/PlatformRenderTimer.mm).
The Core Video display-link APIs used there are deprecated on current macOS.
Apple now recommends creating a display link from an `NSView`, `NSWindow`, or
`NSScreen`; a window/view display link automatically follows the display containing
that object. See Apple's
[macOS 14 display-link notes](https://developer.apple.com/documentation/macos-release-notes/appkit-release-notes-for-macos-14#Display-Link)
and
[`NSWindow.displayLink(target:selector:)`](https://developer.apple.com/documentation/appkit/nswindow/displaylink%28target%3Aselector%3A%29).

## Required behavior

1. A single visible window on a 60 Hz monitor receives approximately 60 render-loop
   ticks, regardless of which monitor is primary.
2. Moving the window to a 30 Hz monitor changes its effective presentation cadence
   without restarting the application.
3. A slow primary monitor must not cap windows on faster monitors.
4. Hidden, minimized, detached, and idle windows must not cause unnecessary drawing.
5. Multiple windows on different monitors must not make the application-wide clock
   run at the slowest monitor's rate.
6. Display hot-plug, sleep/wake, full-screen transitions, and variable-refresh
   displays must not leave the render loop stopped or duplicated.

## Recommended short-term Avalonia patch

Avalonia currently has one `IRenderTimer` and one compositor render loop for the
application. Within that architecture, use the fastest display containing an active,
visible top-level as the render-driving display.

This gives a 60 Hz window a 60 Hz compositor clock and avoids changing core render-loop
interfaces. A window on a slower display may be evaluated at the faster clock when
windows are simultaneously visible on different displays, but its swap chain still
presents at the display's own cadence. That modest extra animation work is preferable
to permanently capping the faster window.

### 1. Report top-level display changes

Avalonia already exposes the current `CGDirectDisplayID` through
`TopLevelImpl::GetCurrentDisplayId`. Add a native-to-managed notification for these
events:

- top-level shown or restored;
- top-level hidden, minimized, or closed;
- `NSWindowDidChangeScreenNotification` / `windowDidChangeScreen:`;
- display reconfiguration and wake from sleep.

Use `window.screen`, rather than window coordinates or the primary screen. AppKit
selects the appropriate screen as a window crosses monitor boundaries. Apple documents
the corresponding
[`NSWindowDidChangeScreenNotification`](https://developer.apple.com/documentation/appkit/nswindow/didchangescreennotification).

Likely Avalonia integration points:

- `native/Avalonia.Native/src/OSX/AvnWindow.mm`
- `native/Avalonia.Native/src/OSX/TopLevelImpl.mm`
- `native/Avalonia.Native/src/OSX/PlatformRenderTimer.mm`
- `src/Avalonia.Native/TopLevelImpl.cs`
- `src/Avalonia.Native/AvaloniaNativeRenderTimer.cs`

### 2. Track active display membership

Add a small macOS render-timer coordinator that records:

```text
top-level identity -> display ID, visible, minimized
```

On every membership change, choose the active display with the greatest refresh
capability. `NSScreen.maximumFramesPerSecond` or `minimumRefreshInterval` provides
the required comparison and supports variable-refresh displays. See
[`NSScreen.maximumFramesPerSecond`](https://developer.apple.com/documentation/appkit/nsscreen/maximumframespersecond)
and
[`minimumRefreshInterval`](https://developer.apple.com/documentation/appkit/nsscreen/minimumrefreshinterval).

Keep the current driving display when rates are equal, so ordinary window focus or
movement does not churn the display link.

If there are no visible top-levels, retain the selected display but allow Avalonia's
existing demand-driven render loop to stop the timer.

### 3. Retarget the native display link

For an Avalonia 11.3.x-compatible patch, extend `IAvnPlatformRenderTimer` with a method
such as:

```text
SetCurrentDisplay(CGDirectDisplayID displayId)
```

The native implementation can initially keep the existing Core Video object and call
`CVDisplayLinkSetCurrentCGDisplay`. Alternatively, recreate it with
`CVDisplayLinkCreateWithCGDisplay`. Both APIs are deprecated, but they provide a small,
backportable fix and explicitly select one display.

The transition must:

1. execute outside the display-link callback;
2. serialize against `Start`, `Stop`, and callback disposal;
3. preserve whether the timer was running;
4. avoid publishing callbacks from both the old and new display links;
5. preserve the monotonic `Stopwatch` timestamp used by
   `AvaloniaNativeRenderTimer`.

Conceptually:

```cpp
HRESULT PlatformRenderTimer::SetCurrentDisplay(CGDirectDisplayID displayId)
{
    // Called on the platform/UI side, never from OnTick.
    if (_currentDisplay == displayId)
        return S_OK;

    const bool restart = CVDisplayLinkIsRunning(_displayLink);
    if (restart)
        CVDisplayLinkStop(_displayLink);

    auto result = CVDisplayLinkSetCurrentCGDisplay(_displayLink, displayId);
    if (result == kCVReturnSuccess)
        _currentDisplay = displayId;

    if (restart && result == kCVReturnSuccess)
        CVDisplayLinkStart(_displayLink);
    return result;
}
```

Production code needs the existing COM error-handling conventions and lifetime
synchronization; the sketch only shows the state transition.

### 4. Use the modern AppKit API on macOS 14+

For Avalonia mainline, replace the deprecated Core Video source with a display link
created from the selected `NSWindow` or `NSScreen`:

```objc
CADisplayLink *link = [drivingWindow displayLinkWithTarget:target
                                                  selector:@selector(onDisplayLink:)];
```

An `NSWindow` display link follows the window when it moves to another display and
stops invoking callbacks when the window is not on a display. If the driving window
closes or becomes hidden while other windows remain visible, the coordinator selects
another window and replaces the link.

Keep the Core Video implementation as a fallback for older supported macOS versions.
Do not replace vsync with a `DispatcherTimer`, `System.Threading.Timer`, or sleep loop;
those clocks drift and are not aligned with presentation boundaries.

## Exact per-window cadence: longer-term design

The short-term patch drives Avalonia's shared compositor at the fastest active window
rate. Truly independent 30 Hz and 60/120 Hz window clocks require a display-aware
render loop.

That larger change should:

1. create one AppKit display link per active display, or one automatically retargeting
   link per top-level;
2. include the display identity and timestamp in the render-tick payload;
3. associate each `ServerCompositionTarget` with its top-level/display;
4. process and render only targets belonging to the ticked display;
5. keep compositor transport, resource changes, and cross-target state serialized;
6. process animations at the cadence of the target that owns each animated visual;
7. migrate a target atomically when `windowDidChangeScreen:` fires.

This likely requires extending `IRenderTimer`, `IRenderLoop`, and `IRenderLoopTask`,
because their current callbacks do not carry a display or target identity. It should
be a separate change from the focused macOS fix above.

## Tests

### Unit tests

- One visible 60 Hz top-level selects its display even when the primary display is
  30 Hz.
- Moving the only top-level from 30 Hz to 60 Hz retargets once.
- With simultaneous 30 Hz and 60 Hz top-levels, the 60 Hz display drives the shared
  timer.
- Hiding or closing the fastest top-level falls back to the next-fastest active
  display.
- Equal-rate moves do not recreate or retarget the link.
- Repeated notifications, concurrent start/stop, and display removal are idempotent.
- No active top-level permits the existing render loop to sleep.

Inject a display-link factory and screen-rate provider so these policies can be tested
without physical monitors.

### Manual macOS verification

Use two monitors configured to different rates, for example 30 Hz and 60 Hz:

1. Keep the 30 Hz monitor primary and place the test window fully on the 60 Hz monitor.
2. Confirm raw display-link and compositor callbacks remain near 60 Hz.
3. Move the window fully onto the 30 Hz monitor and confirm the single-window callback
   rate settles near 30 Hz.
4. Move it back without restarting and confirm recovery to 60 Hz.
5. Repeat with both windows visible, then minimize, restore, unplug, and reconnect the
   fast monitor.
6. Verify idle compositor ticks do not imply layout, invalidation, drawing, or
   presentation when nothing changed.

For an input-driven scene, record one-second rates for:

- native display-link callbacks;
- Avalonia compositor callbacks;
- invalidations and changed renders;
- unchanged render callbacks;
- pending work depth and UI-dispatch wakes.

On rapid pointer movement, display-link callbacks and changed rendering should track
the window's monitor rate without recurring two-frame gaps. At idle, unchanged render
callbacks should remain zero even if the display link is armed.

## Non-goals

- WebScene should not install or replace Avalonia's process-wide render timer.
- Scene publications should not be routed through the UI dispatcher to compensate for
  a slow platform clock.
- The fix must not render every tick when no visual state changed.
- Changing the user's primary-monitor configuration is a diagnostic workaround, not
  an application or framework fix.


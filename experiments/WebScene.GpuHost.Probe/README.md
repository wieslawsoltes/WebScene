# Avalonia GPU host capability probe

Run `dotnet run --project experiments/WebScene.GpuHost.Probe` from the repository
root in a graphical desktop session. The probe briefly opens a window, queries the
actual compositor's GPU interop service, prints JSON and closes. Exit 0 means the
query completed with an interop object, not that any import or presentation works.
Exit 77 means no interop object; exit 1 means the query failed.

Observed on Apple M4, macOS 26.6.2, default Avalonia 11.3.4 platform selection:
interop available, device not lost, imageTypes empty, semaphoreTypes empty.
Therefore no external-handle route can be selected from this capability result.
Shared-context APIs or another explicitly supported host backend require separate
investigation. This does not contradict the standalone Dawn/Graphite GPU test.

The probe additionally queries Avalonia's public OpenGL texture-sharing feature.
On this host `canCreateSharedOpenGlContext` is true, despite the empty external
handle lists. This is the next candidate to exercise; no shared texture has yet
been drawn or presented by this capability probe.

# Windows build follow-up

Run [34116313032](https://github.com/wieslawsoltes/WebScene/actions/runs/34116313032), revision 1ef6394, failed both Windows component jobs. Full logs are retained alongside this note.

Dawn reached shared-library linkage but Abseil objects referenced dynamic CRT imports while the surrounding build selected the static CRT. The pinned Abseil CMakeLists.txt explicitly overrides CMAKE_MSVC_RUNTIME_LIBRARY unless ABSL_MSVC_STATIC_RUNTIME is enabled. Both builder and SDK verifier now require that option on Windows.

ANGLE failed before dependency synchronization completed: git_cache.Mirror invokes git.bat, which did not exist. DEPOT_TOOLS_UPDATE=0 skips the normal Windows bootstrap as well as self-update. The builder now explicitly invokes the pinned bootstrap/win_tools.bat and requires its generated Git wrapper before gclient sync. The depot_tools source revision remains fixed.

All 10 local Python regression tests pass. These Windows corrections require the successor hosted build; they are not yet verified Windows successes. Linux Dawn in this run successfully built its SDK and linked its probe, without GPU hardware execution.

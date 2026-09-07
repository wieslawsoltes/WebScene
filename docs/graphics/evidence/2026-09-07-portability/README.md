# G01 hosted build portability findings

Hosted SDK run [34115613271](https://github.com/wieslawsoltes/WebScene/actions/runs/34115613271), commit `8ba8211`, failed Windows Dawn/ANGLE source license checks and Linux Dawn configuration. Logs are retained here; no hardware qualification was attempted.

Windows inherited native CRLF behavior for upstream `text=auto` attributes despite `core.autocrlf=false`. Commit `16eb3ab` also sets `core.eol=lf` before checkout. Its regression test creates a real Git repository under a CRLF global configuration and verifies exact license bytes after checkout. All ten Python tests pass.

Linux GCC passed Dawn's C++ module language feature probe but lacked CMake module dependency discovery. The optional module wrapper is now disabled in the dependency lock; WebScene uses generated C/C++ headers. Local macOS rebuilds against the updated lock succeeded, and all three hardware probes still pass with the adjacent library hashes verified. Logs and full probe/SDK manifests are compressed here.

These changes require a new Windows/Linux hosted build before being considered verified on those platforms. Linux ANGLE's original job was still running when this evidence was recorded and was deliberately left uninterrupted. Hardware, driver and graphics package qualification remain separate incomplete gates for #23.

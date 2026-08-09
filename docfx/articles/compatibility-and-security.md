# Compatibility and security

WebScene implements a bounded web component profile for trusted application UI. It is
not a browser-conformance claim and not a sandbox for arbitrary content.

## Support matrix

| Area | Current position |
| --- | --- |
| Engine | One native V8/DOM/CSS/layout/Canvas/SVG engine; no managed fallback |
| Avalonia | Reference presenter and current integration authority |
| Uno | Skia presenter proof; not a production support claim |
| Runtime platforms | `osx-arm64`, `linux-x64`, and `win-x64` |
| Content | Trusted, packaged, application-owned UI |
| General websites | Not supported as a product goal |
| Browser security model | Not implemented |

The presence of an API or backend directory is not by itself a support promise. Check
the current compatibility profile and product-scale samples against the exact package
version and platform you intend to ship.

## Compatibility profile

Tests are classified as required, candidate, harness-blocked, or excluded:

- Required tests are release gates.
- Candidate tests measure useful work that is not yet a support promise.
- Harness-blocked tests cannot currently produce meaningful evidence.
- Excluded tests are outside the bounded product profile.

List the profile without launching Chromium:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection all \
  --list
```

Run the required native profile with the matching engine:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection required \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

Optional Chromium comparison supplements the native evidence; it does not replace the
same-engine WPT comparison or turn candidate coverage into a release claim.

## Application acceptance tests

The shared profile cannot prove a product-specific workload. Before shipping:

1. Freeze the WebScene package and content-bundle versions together.
2. Run the required profile on every advertised RID.
3. Exercise real pointer, keyboard, focus, resize, and shutdown paths.
4. Capture deterministic render evidence for the important screens and states.
5. Test network loss, missing resources, script failure, cancellation, and repeated
   navigation.
6. Measure scene publication, renderer memory, runtime work, and steady-state CPU.
7. Verify every required accessibility, clipboard, and IME behavior; do not assume
   browser or presenter parity.

The repository's Monaco and TradingView samples are demanding references, not blanket
proof for another application.

## Trust boundary

WebScene currently does not provide a browser-grade:

- origin and site-isolation model;
- permission system;
- navigation or download policy;
- renderer process sandbox;
- per-origin persistent storage profile;
- untrusted extension/plugin boundary; or
- complete Content Security Policy enforcement claim.

Load only HTML, CSS, JavaScript, fonts, and data that the application owner trusts. A
remote HTTPS URL does not make content safe merely because transport is encrypted.

If a product must display user-authored or third-party untrusted content, place that
requirement outside the current WebScene trust boundary and choose an appropriately
sandboxed browser technology.

## Host capability design

Expose the smallest host surface needed by the document. Prefer generated typed
bindings and narrow callback adapters over a general command executor or unrestricted
reflection bridge.

- Validate arguments again at the .NET boundary.
- Keep filesystem, network, clipboard, and process-launch authority out of JavaScript
  unless the feature explicitly requires it.
- Bind callbacks to the owning view lifetime and release them on navigation.
- Never concatenate untrusted data into evaluated JavaScript or document-start source.
- Record enough operation context to audit privileged host calls without logging
  secrets or document contents unnecessarily.

## Inspector security

V8 Inspector can evaluate JavaScript and inspect application state. Keep it disabled in
normal operation. Loopback is the default. Remote connections require both explicit
enablement and a bearer token; treat that token as a debugging credential.

Do not expose the discovery endpoint through a production reverse proxy or public
interface. Dispose Inspector hosts during shutdown and verify that release builds do
not enable them through inherited environment variables.

## Status language

When documenting or marketing an integration:

- Describe the license as the repository's custom source-available license with a
  Restricted Party Clause, not unqualified MIT or OSI-approved open source.
- Describe Uno as an experimental proof until its missing conformance and platform
  gates are complete.
- Describe candidate web-platform behavior as candidate coverage, not supported browser
  compatibility.
- State the exact RID and WebScene version behind performance or compatibility claims.

See the current [backend status](https://github.com/wieslawsoltes/WebScene/blob/main/docs/backends.md),
[required profile policy](https://github.com/wieslawsoltes/WebScene/blob/main/tests/WebPlatformSubset/README.md),
and [repository reference](repository-reference.md).

# Embedded media fallback

WebScene does not implement YouTube playback. Instead, recognized YouTube video
iframes display a thumbnail, the authored iframe title (or “YouTube video”), and
a **Watch on YouTube ↗** link. This works in the shared native engine, including
Avalonia and Uno, without TradingView- or Sandwich-specific injection.

The fallback is enabled by default. Hosts can disable it process-wide by setting
`WEBSCENE_YOUTUBE_EMBED_FALLBACK=0` before loading documents. This restores the
previous iframe behavior; it does **not** enable video playback.

## Behavior and boundaries

- Only HTTP(S) `/embed/<11-character-video-id>` URLs on `youtube.com`,
  `www.youtube.com`, `youtube-nocookie.com`, and `www.youtube-nocookie.com` match.
  Default ports are accepted. User-info, other ports, lookalike hosts, encoded
  IDs, playlists, and unrelated paths do not match.
- Both parsed and dynamically inserted/navigated iframes are supported. The
  replacement uses the iframe's existing bounds and clips its contents.
- This is a replacement document, **not** an overlay hiding a YouTube player.
  No player document, scripts, autoplay, or media stream is requested.
- A full-resolution thumbnail is fetched from
  `https://i.ytimg.com/vi/<id>/maxresdefault.jpg`, falling back to `hq720.jpg`, then
  `hqdefault.jpg` on failure or a tiny placeholder. The final 4:3 fallback is
  cropped to remove its embedded letterboxing. Requests use the
  normal resource loader/cache. Thus YouTube's image service receives an image
  request even for `youtube-nocookie.com` embeds. Disable the fallback or block
  that image origin in the host resource loader if this is inappropriate.
- The title and watch link remain usable when the thumbnail fails, including
  private/deleted videos and offline operation. Opening still needs a browser
  and network access; WebScene cannot determine whether the video is available.
- The entire card uses a hand cursor. Pointer activation or Enter on the focused link emits the existing
  `openExternalUrl` host request with a canonical HTTPS YouTube watch URL. The
  standard Avalonia view uses its existing system-browser launcher. Other hosts
  must handle that request. Merely loading a page never opens a browser.
- Embed query options (including autoplay/start/playlist) are not forwarded.
  Explicit `srcdoc`/document-written content and other iframe providers are not
  replaced. This is a product fallback, not full HTML media support.

The HTTP image loaders preserve JPEG/PNG/WebP/GIF bytes in an SVG image envelope
for the existing shared Skia renderer, rather than decoding binary image bodies
as text. Encoded images are limited to 16 MiB and raster dimensions to 16 megapixels.
No media or rendering dependency upgrade is involved.

## Coverage

`native_youtube_embed_tests.inc` covers strict URL matching, title escaping,
static and dynamic frames, source changes, full-resolution/failed/placeholder
thumbnail selection and cropping, and no repeated thumbnail requests. It also
covers external link activation, cancelled clicks and cursor routing with
multiple iframes, wheel scrolling beside/over hidden-overflow frames, ordinary frames,
and the process opt-out. Managed raster tests verify real pixels through the
existing SVG renderer on Avalonia and Uno.
`contracts/youtube-embed-fallback.html` is a required WPT-style **WebScene product
contract** for content and resized/clipped iframe geometry; it is not an upstream
browser-conformance test.

For a live screenshot and warm-scroll resource/performance diagnostic, run the
normal sample with `--headless-proof --document-proof --youtube-proof`,
`--url https://www.sandwichtrading.com/app/release-modal-dark`, an explicit
`--native-library` path, and `--output <directory>`. Use a fresh `--cache`
directory when comparing engine/resource-loader revisions. The probe rejects
missing thumbnails and new resource requests during repeated scrolling; its
loop timing includes headless pacing and is not a compositor frame-time benchmark.

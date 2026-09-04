#include "webscene_embed_fallback.h"

// Product fallback contracts, not browser/YouTube playback conformance tests.
void test_youtube_embed_fallback()
{
    using webscene::embed_fallback::youtube_video_id;
    for (const auto* url : {
        "https://www.youtube.com/embed/CLnjOkR1vzk?autoplay=1#x",
        "http://youtube.com:80/embed/CLnjOkR1vzk",
        "https://WWW.YOUTUBE-NOCOOKIE.COM:443/embed/CLnjOkR1vzk"}) {
        require(youtube_video_id(url) == "CLnjOkR1vzk", "valid embed URL rejected");
    }
    for (const auto* url : {
        "https://youtube.com.evil.test/embed/CLnjOkR1vzk", "https://evil.test/youtube.com/embed/CLnjOkR1vzk",
        "https://youtube.com@evil.test/embed/CLnjOkR1vzk", "https://evil@youtube.com/embed/CLnjOkR1vzk",
        "https://youtube.com:444/embed/CLnjOkR1vzk", "https://youtube.com/watch?v=CLnjOkR1vzk",
        "https://youtube.com/embed/videoseries?list=abc", "https://youtube.com/embed/short",
        "https://youtube.com/embed/CLnjOkR1vzk/extra", "https://youtube.com/embed/CLnjOkR1vz%6b",
        "https://youtube.com/embed/CLnjOkR1vz<", "javascript://youtube.com/embed/CLnjOkR1vzk",
        "https://youtube.com\\@evil.test/embed/CLnjOkR1vzk"}) {
        require(!youtube_video_id(url), std::string("unsafe or unsupported embed URL accepted: ") + url);
    }
    require(!webscene::embed_fallback::next_thumbnail("https://evil.test/vi/CLnjOkR1vzk/maxresdefault.jpg"), "thumbnail fallback accepted a foreign host");
    require(!webscene::embed_fallback::next_thumbnail("https://i.ytimg.com/vi/CLnjOkR1vzk/hqdefault.jpg"), "thumbnail fallback was not terminal");
    resource_server server{.content = {
        {"https://embed.test/index.html", R"HTML(<html><body style="margin:0">
          <iframe id="video" title="A &amp; B &lt;script&gt;" src="https://www.youtube.com/embed/CLnjOkR1vzk" style="width:320px;height:180px"></iframe>
        </body></html>)HTML"},
        {"https://www.youtube.com/embed/CLnjOkR1vzk", "<html><body>PLAYER MUST NOT LOAD</body></html>"},
        {"https://i.ytimg.com/vi/CLnjOkR1vzk/maxresdefault.jpg", "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 1280 720'><rect width='1280' height='720' fill='red'/></svg>"},
        {"https://i.ytimg.com/vi/AbCdEf01234/hq720.jpg", "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 120 90'/>"},
        {"https://i.ytimg.com/vi/AbCdEf01234/hqdefault.jpg", "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 480 360'/>"},
        {"https://embed.test/ordinary.html", "<html><body><div id='ordinary'>ordinary frame</div></body></html>"}
    }};
    const webscene_engine_options options{sizeof(webscene_engine_options), 0U,
        nullptr, 0U, load_test_resource, &server};
    auto* engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "YouTube fallback engine creation failed");
    resize(engine, 640, 360, 8000U);
    const std::string url = "https://embed.test/index.html";
    require(webscene_engine_load_url(engine, url.data(), url.size()) != 0, "embed fixture load failed");
    const auto wait_for = [&](const char* expression) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (evaluate(engine, expression, "embed-ready.js") == "true") return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    require(wait_for("!!document.getElementById('video').contentDocument.getElementById('webscene-watch')"),
        "static YouTube iframe did not receive fallback");
    require(wait_for("document.getElementById('video').contentDocument.querySelector('img').naturalWidth === 1280"),
        "parser-created iframe thumbnail did not load");
    require(server.requests == 2, "fallback did not fetch only the outer page and thumbnail");
    const auto geometry = evaluate(engine, R"JS((() => {
        const f = document.getElementById('video');
        const a = f.contentDocument.getElementById('webscene-watch');
        const r = f.getBoundingClientRect(), s = a.getBoundingClientRect();
        return [r.width, r.height, s.width, s.height, r.x === s.x, r.y === s.y];
    })())JS", "embed-layout.js");
    require(geometry == "[320,180,320,180,true,true]", "fallback escaped iframe bounds: " + geometry);
    execute(engine, "document.body.style.height='1000px'", "embed-scroll-height.js");
    evaluate(engine, "document.body.getBoundingClientRect().height", "embed-scroll-layout.js");
    const auto scroll = [&](float x) {
        webscene_input_event wheel{};
        wheel.kind = WEBSCENE_INPUT_WHEEL; wheel.sequence = 7999U;
        wheel.x = x; wheel.y = 80; wheel.delta_y = 40;
        require(webscene_engine_enqueue(engine, &wheel) != 0, "wheel input rejected");
        return evaluate(engine, "document.scrollingElement.scrollTop", "embed-wheel-result.js");
    };
    require(scroll(500) == "40", "outer page did not scroll beside an overflow-hidden iframe");
    require(scroll(20) == "80", "wheel over fallback did not chain to outer page");
    execute(engine, "document.scrollingElement.scrollTop=0", "embed-scroll-reset.js");
    evaluate(engine, "true", "embed-scroll-reset-barrier.js");
    require(take_host_request(engine).empty(), "loading a fallback opened the browser without activation");
    require(evaluate(engine, R"JS((() => {
        const d = document.getElementById('video').contentDocument;
        return [d.getElementById('webscene-title').textContent,
            d.getElementById('webscene-watch').href, d.querySelectorAll('script').length];
    })())JS", "embed-content.js") == R"(["A & B <script>","https://www.youtube.com/watch?v=CLnjOkR1vzk",0])",
        "fallback title escaping or canonical watch URL failed");
    // Click uses the existing external host request, not iframe navigation.
    execute(engine, "document.getElementById('video').contentDocument.getElementById('webscene-watch').click()", "embed-click.js");
    evaluate(engine, "true", "embed-click-barrier.js");
    require(take_host_request(engine).find("https://www.youtube.com/watch?v=CLnjOkR1vzk") != std::string::npos,
        "fallback click did not hand off to external browser");
    execute(engine, "document.getElementById('video').contentDocument.getElementById('webscene-watch').focus()", "embed-focus.js");
    require(evaluate(engine, "document.getElementById('video').contentDocument.activeElement.id", "embed-focus-barrier.js") == "\"webscene-watch\"", "fallback link was not focused");
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 13U, 8001U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 13U, 8002U);
    evaluate(engine, "true", "embed-key-barrier.js");
    require(take_host_request(engine).find("https://www.youtube.com/watch?v=CLnjOkR1vzk") != std::string::npos,
        "fallback Enter activation did not hand off to external browser");
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 8003U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 8004U, false);
    evaluate(engine, "true", "embed-pointer-barrier.js");
    require(take_host_request(engine).find("https://www.youtube.com/watch?v=CLnjOkR1vzk") != std::string::npos,
        "fallback native pointer activation did not reach the anchor");
    execute(engine, "document.getElementById('video').contentDocument.getElementById('webscene-watch').addEventListener('keydown', e => e.preventDefault())", "embed-cancel-key.js");
    evaluate(engine, "true", "embed-cancel-handler-barrier.js");
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 13U, 8005U);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_UP, 13U, 8006U);
    evaluate(engine, "true", "embed-cancel-barrier.js");
    require(take_host_request(engine).empty(), "cancelled Enter still opened the browser");
    execute(engine, "document.getElementById('video').src = 'https://www.youtube-nocookie.com/embed/rFoJd63r3ag?autoplay=1'", "embed-change.js");
    require(wait_for("document.getElementById('video').contentDocument.getElementById('webscene-watch')?.href === 'https://www.youtube.com/watch?v=rFoJd63r3ag'"),
        "dynamic privacy-enhanced embed navigation did not replace fallback");
    require(evaluate(engine, R"JS((() => {
        const f = document.getElementById('video');
        const r = f.getBoundingClientRect();
        const a = f.contentDocument.getElementById('webscene-watch').getBoundingClientRect();
        return r.x === a.x && r.y === a.y && r.width === a.width && r.height === a.height;
    })())JS", "embed-renavigation-layout.js") == "true", "navigated fallback moved outside its frame");
    require(wait_for("document.getElementById('video').contentDocument.querySelector('img').complete === true"),
        "missing thumbnail did not complete with a broken-image result");
    require(evaluate(engine, "document.getElementById('video').contentDocument.querySelector('img').naturalWidth", "embed-image-error.js") == "0",
        "missing thumbnail fixture unexpectedly loaded");
    execute(engine, "document.getElementById('video').contentDocument.getElementById('webscene-watch').click()", "embed-broken-click.js");
    evaluate(engine, "true", "embed-broken-barrier.js");
    require(take_host_request(engine).find("https://www.youtube.com/watch?v=rFoJd63r3ag") != std::string::npos,
        "missing thumbnail made fallback link unusable");
    execute(engine, "const ordinary = document.createElement('iframe'); ordinary.id='ordinaryFrame'; ordinary.src='https://embed.test/ordinary.html'; document.body.appendChild(ordinary)", "ordinary-frame.js");
    require(wait_for("!!document.getElementById('ordinaryFrame').contentDocument.getElementById('ordinary')"),
        "ordinary iframe loading regressed");
    // A second iframe must not steal hit-testing or listeners from the first.
    pointer_move(engine, 160, 90, 8100U);
    evaluate(engine, "true", "embed-multiple-frame-hover-barrier.js");
    require(webscene_engine_get_cursor(engine) == WEBSCENE_CURSOR_POINTER,
        "hovering the first embed image lost the hand cursor after a second iframe loaded");
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 160, 90, 8101U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 160, 90, 8102U, false);
    evaluate(engine, "true", "embed-multiple-frame-click-barrier.js");
    require(take_host_request(engine).find("https://www.youtube.com/watch?v=rFoJd63r3ag") != std::string::npos,
        "clicking the first embed image stopped working after a second iframe loaded");
    execute(engine, "window.embedClicks=0; document.getElementById('video').contentDocument.getElementById('webscene-watch').addEventListener('click', e => { window.embedClicks++; e.preventDefault(); })", "embed-multiple-frame-listener.js");
    evaluate(engine, "true", "embed-listener-barrier.js");
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 160, 90, 8103U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 160, 90, 8104U, false);
    require(evaluate(engine, "window.embedClicks", "embed-listener-result.js") == "1", "first-frame listener was dispatched in the wrong realm");
    require(take_host_request(engine).empty(), "cancelled first-frame click opened the browser");
    execute(engine, "document.getElementById('video').src = 'https://www.youtube.com/embed/AbCdEf01234'", "embed-quality-fallback.js");
    require(wait_for("document.getElementById('video').contentDocument.querySelector('img')?.naturalWidth === 480"),
        "failed high-resolution image and successful tiny placeholder did not fall back");
    require(evaluate(engine, R"JS((() => {
        const d = document.getElementById('video').contentDocument;
        const image = d.querySelector('img'), link = d.getElementById('webscene-watch');
        const i = image.getBoundingClientRect(), a = link.getBoundingClientRect();
        return image.src.endsWith('/hqdefault.jpg') && Math.abs(i.height - 240) < .01 && Math.abs(i.y - a.y + 30) < .01;
    })())JS", "embed-quality-crop.js") == "true", "4:3 fallback did not crop the letterboxing");
    const auto completed_requests = server.requests.load();
    for (int i = 0; i < 12; ++i) pointer_move(engine, 160 + i, 90, 8200U + i);
    evaluate(engine, "true", "embed-quality-reuse-barrier.js");
    require(server.requests == completed_requests, "completed thumbnail retried during pointer movement");
    execute(engine, "document.getElementById('video').src = 'https://embed.test/ordinary.html'", "embed-to-ordinary.js");
    require(wait_for("!!document.getElementById('video').contentDocument.getElementById('ordinary')"),
        "navigating away from a fallback retained the generated document");
    webscene_engine_destroy(engine);

    // The host opt-out is checked at frame navigation, including new engines.
    const auto* previous_value = std::getenv("WEBSCENE_YOUTUBE_EMBED_FALLBACK");
    const auto previous = previous_value ? std::optional<std::string>(previous_value) : std::nullopt;
#if defined(_WIN32)
    _putenv_s("WEBSCENE_YOUTUBE_EMBED_FALLBACK", "0");
#else
    setenv("WEBSCENE_YOUTUBE_EMBED_FALLBACK", "0", 1);
#endif
    engine = webscene_engine_create_with_options(&options);
    require(engine != nullptr, "opt-out engine creation failed");
    execute(engine, "const f = document.createElement('iframe'); f.id='video'; f.src='https://www.youtube.com/embed/CLnjOkR1vzk'; document.body.appendChild(f)", "embed-opt-out.js");
    require(wait_for("document.getElementById('video').contentDocument.body.textContent.includes('PLAYER MUST NOT LOAD')"),
        "host opt-out did not retain normal remote iframe loading");
    webscene_engine_destroy(engine);
#if defined(_WIN32)
    _putenv_s("WEBSCENE_YOUTUBE_EMBED_FALLBACK", previous ? previous->c_str() : "");
#else
    if (previous) setenv("WEBSCENE_YOUTUBE_EMBED_FALLBACK", previous->c_str(), 1);
    else unsetenv("WEBSCENE_YOUTUBE_EMBED_FALLBACK");
#endif
}

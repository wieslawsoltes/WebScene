void test_go_to_tab_lines_and_calendar_scroll_ranges(webscene_engine* engine)
{
    const auto root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path().parent_path();
    std::ifstream stream(root / "tests/WebPlatformSubset/contracts/go-to-tabs-overflow.html");
    require(stream.good(), "Go To shared WPT fixture is missing");
    const std::string html{std::istreambuf_iterator<char>(stream), {}};
    const std::string marker = "<script id=\"fixture\">";
    const auto start = html.find(marker);
    const auto end = html.find("</script>", start);
    require(start != std::string::npos && end != std::string::npos,
        "Go To shared WPT fixture script is missing");
    resize(engine, 858, 1024, 12480U);
    execute(engine, html.substr(start + marker.size(), end - start - marker.size())
        + "\ncreateGoToOverflowFixture();", "go-to-shared-fixture.js");
    for (const auto width : {302, 431, 302}) {
        execute(engine, "document.getElementById('dialog').style.width='"
            + std::to_string(width) + "px';", "go-to-fixture-resize.js");
        const auto result = evaluate(engine, R"JS(
          (() => {
            const n=id=>document.getElementById(id), h=id=>n(id).getBoundingClientRect().height;
            const s=getComputedStyle(n('scroll-tabs'));
            return [s.marginTop,s.marginRight,s.marginBottom,s.marginLeft,
              h('tabs'),h('scroll-tabs'),h('calendar'),h('body-wrapper'),h('content'),
              h('percentage'),h('nested'),h('flex-percent'),
              h('positioned-fill'),
              n('scroll-tabs').getBoundingClientRect().x,
              ['scroll-tabs','content'].every(id=>{
                n(id).scrollLeft=100; n(id).scrollTop=100;
                return n(id).scrollLeft===0 && n(id).scrollTop===0;
              })];
          })()
        )JS", "go-to-fixture-geometry.js");
        require(result == R"(["-2px","-20px","0px","-20px",32,34,332,382,399,100,50,140,1024,0,true])",
            "Go To margins, percentage heights or scroll ranges regressed: " + result);
    }

    // Public geometry alone cannot detect duplicated lines or a painted rail
    // with zero DOM scroll range. Assert the retained scene as well.
    webscene_engine_request_scene_checkpoint(engine);
    auto matched = false;
    std::string paint_evidence;
    for (auto attempt = 0; attempt < 100 && !matched; ++attempt) {
        if (const auto* scene = webscene_engine_acquire_latest_scene(engine)) {
            size_t lines = 0;
            bool aligned = true;
            bool phantom_rail = false;
            paint_evidence = " commands=" + std::to_string(scene->header.command_count);
            for (uint32_t i = 0; i < scene->header.command_count; ++i) {
                const auto& command = scene->commands[i];
                if (command.kind != 1U && command.kind != 9U && command.kind != 10U) continue;
                if (command.rgba == 0x123456FFU) {
                    paint_evidence += " line=" + std::to_string(command.x) + ","
                        + std::to_string(command.y) + "," + std::to_string(command.width)
                        + "," + std::to_string(command.height);
                    ++lines;
                    aligned = aligned && std::abs(command.x - 20) < 0.01F
                        && std::abs(command.y - 28) < 0.01F
                        && std::abs(command.height - 4) < 0.01F;
                }
                phantom_rail = phantom_rail || command.rgba == 0xA0A0A0D0U;
                if (command.rgba == 0xA0A0A0D0U) paint_evidence += " scrollbar="
                    + std::to_string(command.x) + "," + std::to_string(command.y);
            }
            matched = lines == 2 && aligned && !phantom_rail;
            webscene_scene_acknowledge(scene);
            webscene_scene_release(scene);
        }
        if (!matched) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(matched, "Go To underline layers must coincide without a painted phantom scrollbar: "
        + paint_evidence);

    const auto real_overflow = evaluate(engine, R"JS(
      (()=>{
        const n=document.getElementById('content'); n.style.height='200px';
        n.scrollTop=100;
        return n.scrollHeight>n.clientHeight && n.scrollTop===100;
      })()
    )JS", "go-to-real-overflow.js");
    require(real_overflow == "true", "Genuinely constrained calendar must remain scrollable");
}

// Reduced from the Table View interaction in StackWich #1289. Exercise native
// pointer input through the delegated cell guard and the real host request queue.
void test_table_cell_click_copies_text_to_host()
{
    for (const bool in_frame : {false, true}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "table cell copy engine creation failed");
        const auto setup = std::string(R"JS(
            (() => {
              function install() {
                document.body.innerHTML = '<style>html,body{margin:0}table{border-collapse:collapse;width:300px}td{padding:0;height:48px}span{display:block;padding:12px}</style>' +
                  '<table><tr><td data-copy-value="123.45"><span id="value">123.45</span></td></tr><tr><td><span id="empty">empty</span></td></tr></table>';
                window.copyState = 'idle';
                async function copyText(text) {
                  // TradingView first attempts legacy copy, then falls back to
                  // ClipboardItem when no copy event was dispatched.
                  let copied = false;
                  const legacyCopy = event => {
                    event.preventDefault();
                    event.clipboardData.setData('text/plain', text);
                    copied = true;
                  };
                  try {
                    document.addEventListener('copy', legacyCopy, true);
                    window.legacyCopyResult = document.execCommand('copy');
                  } finally {
                    document.removeEventListener('copy', legacyCopy, true);
                  }
                  if (!copied) await navigator.clipboard.write([new ClipboardItem({'text/plain': text})]);
                }
                document.querySelector('table').addEventListener('click', async event => {
                  try {
                    const cell = event.target.closest('td');
                    if (cell instanceof HTMLTableCellElement && cell.dataset.copyValue) {
                      await copyText(cell.dataset.copyValue);
                      window.copyState = 'copied';
                    }
                  } catch (error) { window.copyState = String(error); }
                });
              }
              if ()JS") + (in_frame ? "true" : "false") + R"JS() {
                document.body.innerHTML = '<style>html,body{margin:0}iframe{width:400px;height:200px;border:0;display:block}</style><iframe id="chart"></iframe>';
                const doc = document.getElementById('chart').contentDocument;
                doc.open();
                doc.write('<script>(' + install.toString() + ')();<\/script>');
                doc.close();
              } else install();
            })()
        )JS";
        execute(engine, setup, "table-cell-copy-setup.js");
        const std::string realm = in_frame
            ? "document.getElementById('chart').contentWindow" : "window";
        require(evaluate(engine, realm + ".copyState", "table-cell-copy-ready.js") == R"("idle")",
            "table cell fixture did not initialize");
        pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 801U, true);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 802U, false);
        std::string state;
        for (int attempt = 0; attempt < 100; ++attempt) {
            state = evaluate(engine, realm + ".copyState", "table-cell-copy-state.js");
            if (state != R"("idle")") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(state == R"("copied")", "table cell click failed: " + state);
        require(evaluate(engine, realm + ".legacyCopyResult", "table-cell-legacy-result.js") == "false",
            "unsupported legacy copy must not claim success");
        const auto request = take_host_request(engine);
        require(request.find(R"("kind":"writeClipboard")") != std::string::npos
                && request.find(R"("contentType":"text/plain")") != std::string::npos
                && request.find("data:text/plain;base64,MTIzLjQ1") != std::string::npos,
            "table cell text did not reach the clipboard host handoff: " + request);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 70, 803U, true);
        pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 70, 804U, false);
        evaluate(engine, realm + ".copyState", "table-cell-empty-click-barrier.js");
        require(take_host_request(engine).empty(), "empty cell generated a duplicate clipboard write");
        webscene_engine_destroy(engine);
    }
}

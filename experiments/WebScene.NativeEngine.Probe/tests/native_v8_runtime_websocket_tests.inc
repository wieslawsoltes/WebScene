void test_native_websocket_browser_api()
{
    require(ix::initNetSystem(), "IXWebSocket network initialization failed");
    const auto port = ix::getFreePort();
    require(port > 0, "could not reserve a local WebSocket test port");

    std::mutex server_state_mutex;
    std::string observed_origin;
    ix::WebSocketServer server(port, "127.0.0.1");
    server.setOnClientMessageCallback(
        [&](std::shared_ptr<ix::ConnectionState>,
            ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Open) {
                std::lock_guard lock(server_state_mutex);
                const auto origin = message->openInfo.headers.find("Origin");
                if (origin != message->openInfo.headers.end()) {
                    observed_origin = origin->second;
                }
            } else if (message->type == ix::WebSocketMessageType::Message) {
                socket.send(message->str, message->binary);
            }
        });
    const auto listening = server.listen();
    require(listening.first, "local WebSocket server could not listen");
    server.start();

    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "WebSocket test engine creation failed");
    const auto source = std::string(R"JS(
        location.protocol = 'https:';
        location.host = 'websocket-origin.test';
        globalThis.__webSceneWebSocketTest = {
          events: [], text: null, binary: null, closed: false, closeCode: 0,
          closeReason: '', error: null
        };
        const socket = new WebSocket('ws://127.0.0.1:)JS")
        + std::to_string(port)
        + R"JS(/echo');
        socket.binaryType = 'arraybuffer';
        socket.addEventListener('open', () => {
          __webSceneWebSocketTest.events.push('open');
          socket.send('native-text');
        });
        socket.addEventListener('message', event => {
          __webSceneWebSocketTest.events.push('message');
          if (typeof event.data === 'string') {
            __webSceneWebSocketTest.text = event.data;
            socket.send(new Uint8Array([0, 1, 254, 255]));
          } else {
            __webSceneWebSocketTest.binary =
              Array.from(new Uint8Array(event.data));
            socket.close(1000, 'complete');
          }
        });
        socket.addEventListener('error', event => {
          __webSceneWebSocketTest.error = event.message || 'error';
        });
        socket.addEventListener('close', event => {
          __webSceneWebSocketTest.events.push('close');
          __webSceneWebSocketTest.closed = true;
          __webSceneWebSocketTest.closeCode = event.code;
          __webSceneWebSocketTest.closeReason = event.reason;
        });
    )JS";
    execute(engine, source, "native-websocket-browser-api.js");

    std::string state;
    for (auto attempt = 0; attempt < 500; ++attempt) {
        state = evaluate(
            engine,
            "globalThis.__webSceneWebSocketTest",
            "native-websocket-browser-api-state.js");
        if (state.find("\"closed\":true") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(
        state.find("\"events\":[\"open\",\"message\",\"message\",\"close\"]")
                != std::string::npos
            && state.find("\"text\":\"native-text\"") != std::string::npos
            && state.find("\"binary\":[0,1,254,255]") != std::string::npos
            && state.find("\"closeCode\":1000") != std::string::npos
            && state.find("\"closeReason\":\"complete\"") != std::string::npos
            && state.find("\"error\":null") != std::string::npos,
        "browser WebSocket API did not complete text/binary/close echo lifecycle");
    {
        std::lock_guard lock(server_state_mutex);
        require(
            observed_origin == "https://websocket-origin.test",
            "browser WebSocket API did not forward the document origin");
    }

    webscene_engine_destroy(engine);
    server.stop();
    require(ix::uninitNetSystem(), "IXWebSocket network cleanup failed");
}

} // namespace

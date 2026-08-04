    static constexpr int inspector_context_group_id = 1;

    static std::string inspector_string_to_utf8(
        const v8_inspector::StringView& value)
    {
        if (value.length() == 0U) return {};
        if (value.is8Bit()) {
            return std::string(
                reinterpret_cast<const char*>(value.characters8()),
                value.length());
        }
        std::string result;
        result.reserve(value.length());
        for (size_t index = 0; index < value.length(); ++index) {
            uint32_t code_point = value.characters16()[index];
            if (code_point >= 0xD800U && code_point <= 0xDBFFU
                && index + 1U < value.length()) {
                const auto low = static_cast<uint32_t>(
                    value.characters16()[index + 1U]);
                if (low >= 0xDC00U && low <= 0xDFFFU) {
                    code_point = 0x10000U
                        + ((code_point - 0xD800U) << 10U)
                        + (low - 0xDC00U);
                    ++index;
                }
            }
            if (code_point <= 0x7FU) {
                result.push_back(static_cast<char>(code_point));
            } else if (code_point <= 0x7FFU) {
                result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
                result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
            } else if (code_point <= 0xFFFFU) {
                result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
                result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
                result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
            } else {
                result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
                result.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
                result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
                result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
            }
        }
        return result;
    }

    struct inspector_channel final : v8_inspector::V8Inspector::Channel {
        uint64_t session_id;
        v8_dom_runtime::inspector_message_sink sink;

        inspector_channel(
            uint64_t session_id_value,
            v8_dom_runtime::inspector_message_sink sink_value)
            : session_id(session_id_value), sink(std::move(sink_value))
        {
        }

        void publish(std::unique_ptr<v8_inspector::StringBuffer> message)
        {
            if (message == nullptr || !sink) return;
            const auto utf8 = inspector_string_to_utf8(message->string());
            sink(session_id, utf8);
        }

        void sendResponse(
            int,
            std::unique_ptr<v8_inspector::StringBuffer> message) override
        {
            publish(std::move(message));
        }

        void sendNotification(
            std::unique_ptr<v8_inspector::StringBuffer> message) override
        {
            publish(std::move(message));
        }

        void flushProtocolNotifications() override
        {
        }
    };

    struct inspector_session final {
        std::unique_ptr<inspector_channel> channel;
        std::unique_ptr<v8_inspector::V8InspectorSession> session;
    };

    struct inspector_action final {
        enum class kind : uint8_t {
            connect,
            dispatch,
            disconnect
        };

        kind type{kind::dispatch};
        uint64_t session_id{0U};
        bool wait_for_debugger{false};
        std::string message;
        v8_dom_runtime::inspector_message_sink sink;
        std::shared_ptr<std::promise<bool>> connection_acknowledgement;
    };

    struct inspector_client final : v8_inspector::V8InspectorClient {
        implementation& owner;

        explicit inspector_client(implementation& owner_value)
            : owner(owner_value)
        {
        }

        void runMessageLoopOnPause(int) override
        {
            owner.run_inspector_message_loop();
        }

        void quitMessageLoopOnPause() override
        {
            owner.quit_inspector_message_loop();
        }

        void runIfWaitingForDebugger(int) override
        {
            quitMessageLoopOnPause();
        }

        v8::Local<v8::Context> ensureDefaultContextInGroup(int group_id) override
        {
            if (group_id != inspector_context_group_id
                || owner.context.IsEmpty()) {
                return {};
            }
            return owner.context.Get(owner.isolate);
        }

        double currentTimeMS() override
        {
            return std::chrono::duration<double, std::milli>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    };

    struct inspector_state final {
        std::unique_ptr<inspector_client> client;
        std::unique_ptr<v8_inspector::V8Inspector> inspector;
        std::unordered_map<uint64_t, inspector_session> sessions;
        std::unordered_map<uint32_t, bool> instrumented_async_tasks;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<inspector_action> actions;
        size_t pending_action_bytes{0U};
        std::atomic<uint64_t> next_session_id{1U};
        uint64_t waiting_session_id{0U};
        bool pause_loop_running{false};
        bool shutting_down{false};
        std::stop_token shutdown_token;
        std::deque<inspector_promise_rejection> promise_rejections;
    };

    inspector_state* inspector_state_if_created() const noexcept
    {
        return inspector_state_value.load(std::memory_order_acquire);
    }

    inspector_state* ensure_inspector_state()
    {
        auto* existing = inspector_state_if_created();
        if (existing != nullptr) return existing;
        auto candidate = std::make_unique<inspector_state>();
        auto* expected = static_cast<inspector_state*>(nullptr);
        if (inspector_state_value.compare_exchange_strong(
                expected,
                candidate.get(),
                std::memory_order_release,
                std::memory_order_acquire)) {
            return candidate.release();
        }
        return expected;
    }

    void quit_inspector_message_loop()
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return;
        state->pause_loop_running = false;
        state->condition.notify_all();
    }

    bool inspector_available_for_dispatch() const noexcept
    {
        return isolate != nullptr
            && inspector_ready.load(std::memory_order_acquire);
    }

    bool inspector_instrumentation_active() const noexcept
    {
        // Inspector construction and context registration happen at the first
        // connection. Console/exception/async-stack instrumentation stays
        // pay-for-use while at least one session exists on the worker.
        auto* state = inspector_state_if_created();
        return state != nullptr && state->inspector != nullptr
            && !state->sessions.empty();
    }

    void initialize_inspector()
    {
        // A V8Inspector belongs to an isolate. WebScene's opt-in shared-isolate
        // lane can host multiple independent engine workers, so exposing one
        // worker's session there would violate V8's isolate-thread ownership.
        auto* state = inspector_state_if_created();
        if (state == nullptr || shared_isolate != nullptr
            || state->inspector != nullptr
            || isolate == nullptr || context.IsEmpty()) return;
        state->client = std::make_unique<inspector_client>(*this);
        state->inspector = v8_inspector::V8Inspector::create(
            isolate,
            state->client.get());
        register_inspector_context(
            context.Get(isolate),
            "WebScene",
            document_base_address);
        for (auto& [id, persistent_context] : actual_frame_contexts) {
            if (persistent_context.IsEmpty()) continue;
            const auto name = "WebScene frame " + std::to_string(id);
            register_inspector_context(
                persistent_context.Get(isolate),
                name);
        }
    }

    void register_inspector_context(
        v8::Local<v8::Context> local_context,
        std::string_view name,
        std::string_view origin = {})
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr || state->inspector == nullptr
            || local_context.IsEmpty()) return;
        v8_inspector::V8ContextInfo context_info(
            local_context,
            inspector_context_group_id,
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(name.data()),
                name.size()));
        context_info.origin = v8_inspector::StringView(
            reinterpret_cast<const uint8_t*>(origin.data()),
            origin.size());
        state->inspector->contextCreated(context_info);
    }

    void reset_inspector_context_group_for_navigation(
        v8::Local<v8::Context> local_context,
        std::string_view url)
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr || state->inspector == nullptr
            || local_context.IsEmpty()) return;
        state->promise_rejections.clear();
        state->inspector->resetContextGroup(inspector_context_group_id);
        register_inspector_context(local_context, "WebScene", url);
    }

    void destroy_inspector_context(v8::Local<v8::Context> local_context)
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr || state->inspector == nullptr
            || local_context.IsEmpty()) return;
        std::erase_if(
            state->promise_rejections,
            [&local_context, this](auto& rejection) {
                return rejection.context.Get(isolate) == local_context;
            });
        state->inspector->contextDestroyed(local_context);
    }

    static void* inspector_async_task_id(uint32_t id) noexcept
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    }

    void inspector_async_task_scheduled(
        std::string_view name,
        uint32_t id,
        bool recurring)
    {
        if (!inspector_instrumentation_active()) return;
        auto* state = inspector_state_if_created();
        state->instrumented_async_tasks[id] = recurring;
        state->inspector->asyncTaskScheduled(
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(name.data()),
                name.size()),
            inspector_async_task_id(id),
            recurring);
    }

    void inspector_async_task_started(uint32_t id)
    {
        auto* state = inspector_state_if_created();
        if (state != nullptr && inspector_instrumentation_active()
            && state->instrumented_async_tasks.contains(id)) {
            state->inspector->asyncTaskStarted(inspector_async_task_id(id));
        }
    }

    void inspector_async_task_finished(uint32_t id)
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return;
        const auto found = state->instrumented_async_tasks.find(id);
        if (inspector_instrumentation_active()
            && found != state->instrumented_async_tasks.end()) {
            state->inspector->asyncTaskFinished(inspector_async_task_id(id));
            if (!found->second) state->instrumented_async_tasks.erase(found);
        }
    }

    void inspector_async_task_canceled(uint32_t id)
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return;
        const auto found = state->instrumented_async_tasks.find(id);
        if (inspector_instrumentation_active()
            && found != state->instrumented_async_tasks.end()) {
            state->inspector->asyncTaskCanceled(inspector_async_task_id(id));
        }
        if (found != state->instrumented_async_tasks.end()) {
            state->instrumented_async_tasks.erase(found);
        }
    }

    static v8_inspector::StringView inspector_string_view(
        const std::string& value) noexcept
    {
        return v8_inspector::StringView(
            reinterpret_cast<const uint8_t*>(value.data()),
            value.size());
    }

    unsigned report_inspector_exception(
        v8::TryCatch& try_catch,
        v8::Local<v8::Context> local_context)
    {
        if (!inspector_instrumentation_active() || !try_catch.HasCaught()
            || local_context.IsEmpty()) return 0U;
        auto exception = try_catch.Exception();
        if (exception.IsEmpty()) return 0U;

        auto message_text = std::string("Uncaught ") + to_utf8(isolate, exception);
        auto detailed_message = describe_exception(try_catch, local_context);
        auto url = std::string{};
        auto line = 0U;
        auto column = 0U;
        auto script_id = 0;
        std::unique_ptr<v8_inspector::V8StackTrace> stack_trace;
        auto message = try_catch.Message();
        if (!message.IsEmpty()) {
            const auto origin = message->GetScriptOrigin();
            const auto resource_name = origin.ResourceName();
            if (!resource_name.IsEmpty() && !resource_name->IsUndefined()) {
                url = to_utf8(isolate, resource_name);
            }
            const auto source_line = message->GetLineNumber(local_context).FromMaybe(0);
            if (source_line > 0) line = static_cast<unsigned>(source_line - 1);
            const auto source_column = message->GetStartColumn(local_context).FromMaybe(-1);
            if (source_column >= 0) column = static_cast<unsigned>(source_column);
            script_id = origin.ScriptId();
            auto v8_stack_trace = message->GetStackTrace();
            if (!v8_stack_trace.IsEmpty()) {
                auto* state = inspector_state_if_created();
                stack_trace = state->inspector->createStackTrace(v8_stack_trace);
            }
        }
        auto* state = inspector_state_if_created();
        return state->inspector->exceptionThrown(
            local_context,
            inspector_string_view(message_text),
            exception,
            inspector_string_view(detailed_message),
            inspector_string_view(url),
            line,
            column,
            std::move(stack_trace),
            script_id);
    }

    unsigned report_inspector_promise_rejection(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> exception,
        const std::string& detailed_message)
    {
        if (!inspector_instrumentation_active() || local_context.IsEmpty()
            || exception.IsEmpty()) return 0U;
        const auto message_text = std::string("Uncaught (in promise) ")
            + to_utf8(isolate, exception);
        auto* state = inspector_state_if_created();
        return state->inspector->exceptionThrown(
            local_context,
            inspector_string_view(message_text),
            exception,
            inspector_string_view(detailed_message),
            {},
            0U,
            0U,
            state->inspector->captureStackTrace(true),
            0);
    }

    void revoke_inspector_exception(
        v8::Local<v8::Context> local_context,
        unsigned exception_id,
        std::string_view reason)
    {
        if (!inspector_instrumentation_active() || local_context.IsEmpty()
            || exception_id == 0U) return;
        const auto reason_text = std::string(reason);
        auto* state = inspector_state_if_created();
        state->inspector->exceptionRevoked(
            local_context,
            exception_id,
            inspector_string_view(reason_text));
    }

    void report_inspector_console_message(
        v8::Local<v8::Context> local_context,
        v8::Isolate::MessageErrorLevel level,
        const std::vector<v8::Local<v8::Value>>& arguments)
    {
        if (!inspector_instrumentation_active() || local_context.IsEmpty()) return;
        auto* state = inspector_state_if_created();
        state->inspector->consoleAPICalled(local_context, level, arguments);
    }

    void destroy_all_inspector_contexts()
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr || state->inspector == nullptr
            || isolate == nullptr) return;
        auto local_frame_context = frame_context.IsEmpty()
            ? v8::Local<v8::Context>{}
            : frame_context.Get(isolate);
        auto frame_context_registered = false;
        for (auto& [id, persistent_context] : actual_frame_contexts) {
            static_cast<void>(id);
            if (!persistent_context.IsEmpty()) {
                auto local_context = persistent_context.Get(isolate);
                frame_context_registered = frame_context_registered
                    || (!local_frame_context.IsEmpty()
                        && local_context == local_frame_context);
                state->inspector->contextDestroyed(local_context);
            }
        }
        if (!local_frame_context.IsEmpty() && !frame_context_registered) {
            state->inspector->contextDestroyed(local_frame_context);
        }
        if (!context.IsEmpty()) {
            state->inspector->contextDestroyed(context.Get(isolate));
        }
    }

    uint64_t connect_inspector(
        v8_dom_runtime::inspector_message_sink sink,
        bool wait_for_debugger,
        const std::function<void()>& action_queued)
    {
        if (!sink || !inspector_available_for_dispatch()) return 0U;
        auto* state = ensure_inspector_state();
        const auto session_id = state->next_session_id.fetch_add(
            1U,
            std::memory_order_relaxed);
        auto acknowledgement = wait_for_debugger
            ? std::make_shared<std::promise<bool>>()
            : nullptr;
        auto connected = acknowledgement == nullptr
            ? std::future<bool>{}
            : acknowledgement->get_future();
        {
            std::lock_guard lock(state->mutex);
            if (state->shutting_down
                || state->actions.size() >= maximum_pending_inspector_actions) {
                return 0U;
            }
            state->actions.push_back(inspector_action{
                inspector_action::kind::connect,
                session_id,
                wait_for_debugger,
                {},
                std::move(sink),
                acknowledgement});
        }
        state->condition.notify_all();
        if (action_queued) action_queued();
        if (acknowledgement != nullptr) {
            try {
                if (!connected.get()) return 0U;
            } catch (const std::future_error&) {
                return 0U;
            }
        }
        return session_id;
    }

    bool dispatch_inspector_message(uint64_t session_id, std::string message)
    {
        if (session_id == 0U || message.empty()
            || !inspector_available_for_dispatch()) return false;
        auto* state = inspector_state_if_created();
        if (state == nullptr) return false;
        {
            std::lock_guard lock(state->mutex);
            const auto message_size = message.size();
            if (state->shutting_down
                || state->actions.size() >= maximum_pending_inspector_actions
                || message_size > maximum_pending_inspector_action_bytes
                || state->pending_action_bytes
                    > maximum_pending_inspector_action_bytes - message_size) {
                return false;
            }
            state->actions.push_back(inspector_action{
                inspector_action::kind::dispatch,
                session_id,
                false,
                std::move(message),
                {},
                nullptr});
            state->pending_action_bytes += message_size;
        }
        state->condition.notify_all();
        return true;
    }

    bool disconnect_inspector(uint64_t session_id)
    {
        if (session_id == 0U || !inspector_available_for_dispatch()) return false;
        auto* state = inspector_state_if_created();
        if (state == nullptr) return false;
        {
            std::lock_guard lock(state->mutex);
            if (state->shutting_down) return false;
            const auto existing_disconnect = std::find_if(
                state->actions.begin(),
                state->actions.end(),
                [&](const auto& action) {
                    return action.session_id == session_id
                        && action.type == inspector_action::kind::disconnect;
            });
            if (existing_disconnect != state->actions.end()) return true;
            const auto pending_connect = std::find_if(
                state->actions.begin(),
                state->actions.end(),
                [&](const auto& action) {
                    return action.session_id == session_id
                        && action.type == inspector_action::kind::connect;
                });
            if (pending_connect != state->actions.end()) {
                if (pending_connect->connection_acknowledgement != nullptr) {
                    try {
                        pending_connect->connection_acknowledgement->set_value(false);
                    } catch (const std::future_error&) {
                    }
                }
                for (auto action = state->actions.begin();
                     action != state->actions.end();) {
                    if (action->session_id == session_id
                        && (action->type == inspector_action::kind::connect
                            || action->type == inspector_action::kind::dispatch)) {
                        state->pending_action_bytes -= action->message.size();
                        action = state->actions.erase(action);
                    } else {
                        ++action;
                    }
                }
                return true;
            }
            for (auto action = state->actions.begin();
                 action != state->actions.end();) {
                if (action->session_id == session_id
                    && action->type == inspector_action::kind::dispatch) {
                    state->pending_action_bytes -= action->message.size();
                    action = state->actions.erase(action);
                } else {
                    ++action;
                }
            }
            // Disconnect is a bounded lifecycle action, not untrusted CDP
            // payload. Prioritize it even when another session has filled the
            // payload queue so a waiting-for-debugger owner cannot be stranded
            // after its managed client has gone away.
            state->actions.push_front(inspector_action{
                inspector_action::kind::disconnect,
                session_id,
                false,
                {},
                {},
                nullptr});
        }
        state->condition.notify_all();
        return true;
    }

    bool has_pending_inspector_tasks() const noexcept
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return false;
        std::lock_guard lock(state->mutex);
        return !state->actions.empty();
    }

    bool process_inspector_tasks(bool wait_for_message)
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return false;
        inspector_action action;
        {
            std::unique_lock lock(state->mutex);
            if (wait_for_message) {
                state->condition.wait_for(lock, std::chrono::milliseconds(25), [&] {
                    return state->shutting_down
                        || !state->actions.empty()
                        || !state->pause_loop_running
                        || state->shutdown_token.stop_requested();
                });
            }
            if (state->shutting_down || state->actions.empty()) {
                return false;
            }
            const auto message_size = state->actions.front().message.size();
            action = std::move(state->actions.front());
            state->actions.pop_front();
            state->pending_action_bytes -= message_size;
        }

        switch (action.type) {
        case inspector_action::kind::connect: {
            initialize_inspector();
            if (state->inspector == nullptr) {
                if (action.connection_acknowledgement != nullptr) {
                    action.connection_acknowledgement->set_value(false);
                }
                break;
            }
            auto channel = std::make_unique<inspector_channel>(
                action.session_id,
                std::move(action.sink));
            const auto pause_state = action.wait_for_debugger
                ? v8_inspector::V8Inspector::kWaitingForDebugger
                : v8_inspector::V8Inspector::kNotWaitingForDebugger;
            auto session = state->inspector->connect(
                inspector_context_group_id,
                channel.get(),
                {},
                v8_inspector::V8Inspector::kFullyTrusted,
                pause_state);
            if (session == nullptr) {
                if (action.connection_acknowledgement != nullptr) {
                    action.connection_acknowledgement->set_value(false);
                }
                break;
            }
            state->sessions.emplace(
                action.session_id,
                inspector_session{std::move(channel), std::move(session)});
            if (action.connection_acknowledgement != nullptr) {
                // Release the host only after the waiting session exists on
                // the isolate worker. The host may now queue navigation, but
                // the nested Inspector loop below prevents scripts from
                // running until Runtime.runIfWaitingForDebugger or disconnect.
                action.connection_acknowledgement->set_value(true);
            }
            if (action.wait_for_debugger) {
                state->waiting_session_id = action.session_id;
                run_inspector_message_loop();
                state->waiting_session_id = 0U;
            }
            break;
        }
        case inspector_action::kind::dispatch: {
            const auto found = state->sessions.find(action.session_id);
            if (found != state->sessions.end()) {
                found->second.session->dispatchProtocolMessage(
                    v8_inspector::StringView(
                        reinterpret_cast<const uint8_t*>(action.message.data()),
                        action.message.size()));
            }
            break;
        }
        case inspector_action::kind::disconnect: {
            const auto found = state->sessions.find(action.session_id);
            if (found != state->sessions.end()) {
                found->second.session->stop();
                state->sessions.erase(found);
            }
            if (state->waiting_session_id == action.session_id) {
                state->waiting_session_id = 0U;
                state->pause_loop_running = false;
            }
            if (state->sessions.empty()) {
                state->instrumented_async_tasks.clear();
                if (state->inspector != nullptr) {
                    state->inspector->allAsyncTasksCanceled();
                }
                state->pause_loop_running = false;
            }
            break;
        }
        }
        return true;
    }

    void run_inspector_message_loop()
    {
        auto* state = inspector_state_if_created();
        if (state == nullptr) return;
        state->pause_loop_running = true;
        while (state->pause_loop_running && !state->shutting_down
            && !state->shutdown_token.stop_requested()) {
            process_inspector_tasks(true);
        }
        state->pause_loop_running = false;
    }

    void shutdown_inspector()
    {
        inspector_ready.store(false, std::memory_order_release);
        auto* state = inspector_state_if_created();
        if (state == nullptr) return;
        {
            std::lock_guard lock(state->mutex);
            state->shutting_down = true;
            for (auto& action : state->actions) {
                if (action.connection_acknowledgement != nullptr) {
                    try {
                        action.connection_acknowledgement->set_value(false);
                    } catch (const std::future_error&) {
                    }
                }
            }
            state->actions.clear();
            state->pending_action_bytes = 0U;
        }
        state->pause_loop_running = false;
        state->condition.notify_all();
        if (state->inspector == nullptr || isolate == nullptr) {
            inspector_state_value.store(nullptr, std::memory_order_release);
            delete state;
            return;
        }
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        for (auto& [id, session] : state->sessions) {
            static_cast<void>(id);
            if (session.session != nullptr) session.session->stop();
        }
        state->sessions.clear();
        state->waiting_session_id = 0U;
        state->instrumented_async_tasks.clear();
        state->inspector->allAsyncTasksCanceled();
        destroy_all_inspector_contexts();
        state->inspector.reset();
        state->client.reset();
        inspector_state_value.store(nullptr, std::memory_order_release);
        delete state;
    }

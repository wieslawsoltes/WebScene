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
            owner.inspector_pause_loop_running = false;
            owner.inspector_condition.notify_all();
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

    bool inspector_available_for_dispatch() const noexcept
    {
        return isolate != nullptr
            && inspector_ready.load(std::memory_order_acquire);
    }

    bool inspector_instrumentation_active() const noexcept
    {
        // Context registration remains eager so a later debugger attachment can
        // enumerate existing scripts. Expensive console/exception/async-stack
        // instrumentation is pay-for-use once a session exists on the worker.
        return inspector != nullptr && !inspector_sessions.empty();
    }

    void initialize_inspector(v8::Local<v8::Context> local_context)
    {
        // A V8Inspector belongs to an isolate. WebScene's opt-in shared-isolate
        // lane can host multiple independent engine workers, so exposing one
        // worker's session there would violate V8's isolate-thread ownership.
        if (shared_isolate != nullptr) return;
        inspector_client_value = std::make_unique<inspector_client>(*this);
        inspector = v8_inspector::V8Inspector::create(
            isolate,
            inspector_client_value.get());
        static constexpr char context_name[] = "WebScene";
        const v8_inspector::V8ContextInfo context_info(
            local_context,
            inspector_context_group_id,
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(context_name),
                sizeof(context_name) - 1U));
        inspector->contextCreated(context_info);
        inspector_ready.store(true, std::memory_order_release);
    }

    void register_inspector_context(
        v8::Local<v8::Context> local_context,
        std::string_view name,
        std::string_view origin = {})
    {
        if (inspector == nullptr || local_context.IsEmpty()) return;
        v8_inspector::V8ContextInfo context_info(
            local_context,
            inspector_context_group_id,
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(name.data()),
                name.size()));
        context_info.origin = v8_inspector::StringView(
            reinterpret_cast<const uint8_t*>(origin.data()),
            origin.size());
        inspector->contextCreated(context_info);
    }

    void reset_inspector_context_group_for_navigation(
        v8::Local<v8::Context> local_context,
        std::string_view url)
    {
        if (inspector == nullptr || local_context.IsEmpty()) return;
        inspector->resetContextGroup(inspector_context_group_id);
        register_inspector_context(local_context, "WebScene", url);
    }

    void destroy_inspector_context(v8::Local<v8::Context> local_context)
    {
        if (inspector == nullptr || local_context.IsEmpty()) return;
        inspector->contextDestroyed(local_context);
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
        inspector_instrumented_async_tasks[id] = recurring;
        inspector->asyncTaskScheduled(
            v8_inspector::StringView(
                reinterpret_cast<const uint8_t*>(name.data()),
                name.size()),
            inspector_async_task_id(id),
            recurring);
    }

    void inspector_async_task_started(uint32_t id)
    {
        if (inspector_instrumentation_active()
            && inspector_instrumented_async_tasks.contains(id)) {
            inspector->asyncTaskStarted(inspector_async_task_id(id));
        }
    }

    void inspector_async_task_finished(uint32_t id)
    {
        const auto found = inspector_instrumented_async_tasks.find(id);
        if (inspector_instrumentation_active()
            && found != inspector_instrumented_async_tasks.end()) {
            inspector->asyncTaskFinished(inspector_async_task_id(id));
            if (!found->second) inspector_instrumented_async_tasks.erase(found);
        }
    }

    void inspector_async_task_canceled(uint32_t id)
    {
        const auto found = inspector_instrumented_async_tasks.find(id);
        if (inspector_instrumentation_active()
            && found != inspector_instrumented_async_tasks.end()) {
            inspector->asyncTaskCanceled(inspector_async_task_id(id));
        }
        if (found != inspector_instrumented_async_tasks.end()) {
            inspector_instrumented_async_tasks.erase(found);
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
            if (source_line > 0) line = static_cast<unsigned>(source_line);
            const auto source_column = message->GetStartColumn(local_context).FromMaybe(-1);
            if (source_column >= 0) column = static_cast<unsigned>(source_column + 1);
            script_id = origin.ScriptId();
            auto v8_stack_trace = message->GetStackTrace();
            if (!v8_stack_trace.IsEmpty()) {
                stack_trace = inspector->createStackTrace(v8_stack_trace);
            }
        }
        return inspector->exceptionThrown(
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
        return inspector->exceptionThrown(
            local_context,
            inspector_string_view(message_text),
            exception,
            inspector_string_view(detailed_message),
            {},
            0U,
            0U,
            inspector->captureStackTrace(true),
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
        inspector->exceptionRevoked(
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
        inspector->consoleAPICalled(local_context, level, arguments);
    }

    void destroy_all_inspector_contexts()
    {
        if (inspector == nullptr || isolate == nullptr) return;
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
                inspector->contextDestroyed(local_context);
            }
        }
        if (!local_frame_context.IsEmpty() && !frame_context_registered) {
            inspector->contextDestroyed(local_frame_context);
        }
        if (!context.IsEmpty()) {
            inspector->contextDestroyed(context.Get(isolate));
        }
    }

    uint64_t connect_inspector(
        v8_dom_runtime::inspector_message_sink sink,
        bool wait_for_debugger)
    {
        if (!sink || !inspector_available_for_dispatch()) return 0U;
        const auto session_id = next_inspector_session_id.fetch_add(
            1U,
            std::memory_order_relaxed);
        {
            std::lock_guard lock(inspector_mutex);
            if (inspector_shutting_down
                || inspector_actions.size() >= maximum_pending_inspector_actions) {
                return 0U;
            }
            inspector_actions.push_back(inspector_action{
                inspector_action::kind::connect,
                session_id,
                wait_for_debugger,
                {},
                std::move(sink)});
        }
        inspector_condition.notify_all();
        return session_id;
    }

    bool dispatch_inspector_message(uint64_t session_id, std::string message)
    {
        if (session_id == 0U || message.empty()
            || !inspector_available_for_dispatch()) return false;
        {
            std::lock_guard lock(inspector_mutex);
            if (inspector_shutting_down
                || inspector_actions.size() >= maximum_pending_inspector_actions) {
                return false;
            }
            inspector_actions.push_back(inspector_action{
                inspector_action::kind::dispatch,
                session_id,
                false,
                std::move(message),
                {}});
        }
        inspector_condition.notify_all();
        return true;
    }

    bool disconnect_inspector(uint64_t session_id)
    {
        if (session_id == 0U || !inspector_available_for_dispatch()) return false;
        {
            std::lock_guard lock(inspector_mutex);
            if (inspector_shutting_down) return false;
            const auto existing_disconnect = std::find_if(
                inspector_actions.begin(),
                inspector_actions.end(),
                [&](const auto& action) {
                    return action.session_id == session_id
                        && action.type == inspector_action::kind::disconnect;
                });
            if (existing_disconnect != inspector_actions.end()) return true;
            std::erase_if(inspector_actions, [&](const auto& action) {
                return action.session_id == session_id
                    && action.type == inspector_action::kind::dispatch;
            });
            if (inspector_actions.size() >= maximum_pending_inspector_actions) {
                return false;
            }
            inspector_actions.push_back(inspector_action{
                inspector_action::kind::disconnect,
                session_id,
                false,
                {},
                {}});
        }
        inspector_condition.notify_all();
        return true;
    }

    bool has_pending_inspector_tasks() const noexcept
    {
        std::lock_guard lock(inspector_mutex);
        return !inspector_actions.empty();
    }

    bool process_inspector_tasks(bool wait_for_message)
    {
        inspector_action action;
        {
            std::unique_lock lock(inspector_mutex);
            if (wait_for_message) {
                inspector_condition.wait_for(lock, std::chrono::milliseconds(25), [&] {
                    return inspector_shutting_down
                        || !inspector_actions.empty()
                        || !inspector_pause_loop_running
                        || (shutdown_requested && shutdown_requested());
                });
            }
            if (inspector_shutting_down || inspector_actions.empty()) {
                return false;
            }
            action = std::move(inspector_actions.front());
            inspector_actions.pop_front();
        }

        switch (action.type) {
        case inspector_action::kind::connect: {
            auto channel = std::make_unique<inspector_channel>(
                action.session_id,
                std::move(action.sink));
            const auto pause_state = action.wait_for_debugger
                ? v8_inspector::V8Inspector::kWaitingForDebugger
                : v8_inspector::V8Inspector::kNotWaitingForDebugger;
            auto session = inspector->connect(
                inspector_context_group_id,
                channel.get(),
                {},
                v8_inspector::V8Inspector::kFullyTrusted,
                pause_state);
            inspector_sessions.emplace(
                action.session_id,
                inspector_session{std::move(channel), std::move(session)});
            if (action.wait_for_debugger) run_inspector_message_loop();
            break;
        }
        case inspector_action::kind::dispatch: {
            const auto found = inspector_sessions.find(action.session_id);
            if (found != inspector_sessions.end()) {
                found->second.session->dispatchProtocolMessage(
                    v8_inspector::StringView(
                        reinterpret_cast<const uint8_t*>(action.message.data()),
                        action.message.size()));
            }
            break;
        }
        case inspector_action::kind::disconnect: {
            const auto found = inspector_sessions.find(action.session_id);
            if (found != inspector_sessions.end()) {
                found->second.session->stop();
                inspector_sessions.erase(found);
            }
            if (inspector_sessions.empty()) {
                inspector_instrumented_async_tasks.clear();
                inspector->allAsyncTasksCanceled();
                inspector_pause_loop_running = false;
            }
            break;
        }
        }
        return true;
    }

    void run_inspector_message_loop()
    {
        inspector_pause_loop_running = true;
        while (inspector_pause_loop_running && !inspector_shutting_down
            && !(shutdown_requested && shutdown_requested())) {
            process_inspector_tasks(true);
        }
        inspector_pause_loop_running = false;
    }

    void shutdown_inspector()
    {
        inspector_ready.store(false, std::memory_order_release);
        {
            std::lock_guard lock(inspector_mutex);
            inspector_shutting_down = true;
            inspector_actions.clear();
        }
        inspector_pause_loop_running = false;
        inspector_condition.notify_all();
        if (inspector == nullptr || isolate == nullptr) return;
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        for (auto& [id, session] : inspector_sessions) {
            static_cast<void>(id);
            if (session.session != nullptr) session.session->stop();
        }
        inspector_sessions.clear();
        inspector_instrumented_async_tasks.clear();
        inspector->allAsyncTasksCanceled();
        destroy_all_inspector_contexts();
        inspector.reset();
        inspector_client_value.reset();
    }

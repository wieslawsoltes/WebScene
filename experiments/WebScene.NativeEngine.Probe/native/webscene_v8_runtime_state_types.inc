    struct timer_task final {
        uint32_t id;
        bool animation_frame;
        bool repeating;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::duration<double, std::milli> interval;
        v8::Global<v8::Context> context;
        v8::Global<v8::Function> callback;
        std::vector<v8::Global<v8::Value>> arguments;
        double animation_timestamp_ms{-1};
    };

    struct pending_window_message final {
        v8::Global<v8::Context> target_context;
        v8::Global<v8::Context> source_context;
        v8::Global<v8::Value> data;
        std::string target_origin;
        std::string source_origin;
    };

    struct websocket_binding final {
        v8::Global<v8::Context> context;
        v8::Global<v8::Function> callback;
    };

    struct pending_promise_rejection final {
        v8::Global<v8::Promise> promise;
        std::string error;
    };

    struct pending_interop_promise final {
        uint32_t result_mode{WEBSCENE_INTEROP_RESULT_VALUE_V3};
        v8_dom_runtime::interop_completion_v3 completion;
        v8::Global<v8::Promise> promise;
    };

    struct pending_callback_promise final {
        v8::Global<v8::Promise::Resolver> resolver;
    };

    struct frame_script final {
        std::string source;
        std::string code;
        bool defer{false};
        size_t index{0};
    };

    struct css_declaration final {
        std::string name;
        std::string value;
        bool important{false};
    };

    struct compiled_css_selector final {
        std::vector<std::string> compounds;
        std::vector<char> combinators;
        uint32_t specificity{0};
    };

    struct compiled_css_selector_list final {
        std::vector<compiled_css_selector> selectors;
    };

    struct css_rule_payload final {
        std::string selector;
        compiled_css_selector compiled_selector;
        std::vector<css_declaration> declarations;
        std::vector<std::string> media_queries;
        uint32_t specificity{0};
    };

    struct css_rule final {
        std::shared_ptr<const css_rule_payload> payload;
        uint32_t stylesheet_owner_id{0};
        uint32_t shadow_scope_root_id{0};
        bool media_matches{true};

        const std::string& selector() const noexcept { return payload->selector; }
        const compiled_css_selector& compiled_selector() const noexcept
        {
            return payload->compiled_selector;
        }
        const std::vector<css_declaration>& declarations() const noexcept
        {
            return payload->declarations;
        }
        const std::vector<std::string>& media_queries() const noexcept
        {
            return payload->media_queries;
        }
        uint32_t specificity() const noexcept { return payload->specificity; }
    };

    inline static std::mutex shared_css_rule_payload_mutex;
    inline static std::unordered_map<
        uint64_t,
        std::vector<std::weak_ptr<const css_rule_payload>>>
        shared_css_rule_payloads;

    struct css_opacity_keyframes final {
        std::vector<node_style::opacity_keyframe> opacity_stops;
        std::vector<node_style::rotation_keyframe> rotation_stops;
    };

    struct connected_resource_task final {
        dom_node* node;
        v8::Global<v8::Context> context;
        v8::Global<v8::Object> wrapper;
    };

    struct resize_observer_state final {
        v8::Global<v8::Context> context;
        v8::Global<v8::Function> callback;
        std::vector<dom_node*> nodes;
        std::unordered_map<uint32_t, std::pair<float, float>> delivered_sizes;
    };

    struct session_storage_state final {
        std::vector<std::string> keys;
        std::unordered_map<std::string, std::string> values;
    };

    struct canvas_path_segment final {
        double x1;
        double y1;
        double x2;
        double y2;
    };

    struct canvas_transform final {
        double a{1};
        double b{0};
        double c{0};
        double d{1};
        double e{0};
        double f{0};
    };

    struct canvas_snapshot final {
        canvas_transform transform;
        std::string fill_style{"#000000"};
        std::string stroke_style{"#000000"};
        std::string global_composite_operation{"source-over"};
        std::string line_cap{"butt"};
        std::string line_join{"miter"};
        std::string font{"10px sans-serif"};
        std::string text_align{"start"};
        std::string text_baseline{"alphabetic"};
        std::string image_smoothing_quality{"low"};
        std::string shadow_color{"rgba(0, 0, 0, 0)"};
        double line_width{1};
        double global_alpha{1};
        double miter_limit{10};
        double line_dash_offset{0};
        double shadow_blur{0};
        double shadow_offset_x{0};
        double shadow_offset_y{0};
        bool image_smoothing_enabled{true};
        bool has_clip{false};
        std::vector<double> line_dash;
    };

    struct canvas_state final {
        dom_node* node{nullptr};
        double current_x{0};
        double current_y{0};
        double start_x{0};
        double start_y{0};
        bool has_current{false};
        canvas_transform transform;
        std::vector<canvas_snapshot> stack;
        std::vector<canvas_path_segment> path;
        std::vector<double> line_dash;
        bool has_clip{false};
        canvas_snapshot emitted_paint_state;
        bool has_emitted_paint_state{false};
    };

    struct compilation_cache_entry final {
        v8::Global<v8::UnboundScript> script;
        std::array<uint8_t, 32> key_digest{};
        size_t source_bytes{0};
    };

    struct event_listener_registration final {
        v8::Global<v8::Function> callback;
        v8::Global<v8::Context> context;
        std::string name;
        uint64_t registration_sequence{0};
        uint32_t target{0};
        bool capture{false};
        bool once{false};
    };

    enum class process_compilation_state : uint8_t {
        producing,
        ready,
        failed
    };

    struct process_compilation_entry final {
        std::mutex mutex;
        std::condition_variable condition;
        process_compilation_state state{process_compilation_state::producing};
        std::shared_ptr<const immutable_byte_buffer> cached_data;
        size_t bytes{0U};
        uint64_t access{0U};
    };

    struct process_compilation_acquisition final {
        std::shared_ptr<process_compilation_entry> entry;
        bool producer{false};
    };

    class shared_external_one_byte_source final
        : public v8::String::ExternalOneByteStringResource {
    public:
        explicit shared_external_one_byte_source(
            std::shared_ptr<const immutable_text_source> source,
            std::shared_ptr<std::atomic<uint64_t>> retained_bytes)
            : source_(std::move(source)),
              retained_bytes_(std::move(retained_bytes)),
              retained_byte_count_(source_->size())
        {
            retained_bytes_->fetch_add(
                retained_byte_count_,
                std::memory_order_relaxed);
        }

        ~shared_external_one_byte_source() override
        {
            retained_bytes_->fetch_sub(
                retained_byte_count_,
                std::memory_order_relaxed);
        }

        const char* data() const override
        {
            return source_->data();
        }

        size_t length() const override
        {
            return source_->size();
        }

        size_t EstimateMemoryUsage() const override
        {
            // The character buffer is shared and is attributed separately by
            // EstimateSharedMemoryUsage. Only this small owner is unique to
            // the external V8 string in one isolate.
            return sizeof(*this);
        }

        void EstimateSharedMemoryUsage(
            SharedMemoryUsageRecorder* recorder) const override
        {
            recorder->RecordSharedMemoryUsage(
                source_->data(),
                source_->size());
        }

    private:
        std::shared_ptr<const immutable_text_source> source_;
        std::shared_ptr<std::atomic<uint64_t>> retained_bytes_;
        size_t retained_byte_count_;
    };

    class shared_external_two_byte_source final
        : public v8::String::ExternalStringResource {
    public:
        explicit shared_external_two_byte_source(
            std::shared_ptr<const std::vector<uint16_t>> source,
            std::shared_ptr<std::atomic<uint64_t>> retained_bytes)
            : source_(std::move(source)),
              retained_bytes_(std::move(retained_bytes)),
              retained_byte_count_(source_->size() * sizeof(uint16_t))
        {
            retained_bytes_->fetch_add(
                retained_byte_count_,
                std::memory_order_relaxed);
        }

        ~shared_external_two_byte_source() override
        {
            retained_bytes_->fetch_sub(
                retained_byte_count_,
                std::memory_order_relaxed);
        }

        const uint16_t* data() const override
        {
            return source_->data();
        }

        size_t length() const override
        {
            return source_->size();
        }

        size_t EstimateMemoryUsage() const override
        {
            return sizeof(*this);
        }

        void EstimateSharedMemoryUsage(
            SharedMemoryUsageRecorder* recorder) const override
        {
            recorder->RecordSharedMemoryUsage(
                source_->data(),
                source_->size() * sizeof(uint16_t));
        }

    private:
        std::shared_ptr<const std::vector<uint16_t>> source_;
        std::shared_ptr<std::atomic<uint64_t>> retained_bytes_;
        size_t retained_byte_count_;
    };

    struct process_script_source_entry final {
        std::weak_ptr<const immutable_text_source> source;
        uint64_t access{0U};
    };

    struct process_two_byte_script_source_entry final {
        std::weak_ptr<const std::vector<uint16_t>> source;
        uint64_t access{0U};
    };

    struct cached_text_resource final {
        std::shared_ptr<const immutable_text_source> content;
        std::string entity_tag;
        int64_t last_modified_unix_seconds{0};
        int64_t fresh_until_unix_seconds{0};
    };

    struct process_resource_cache_entry final {
        cached_text_resource resource;
        size_t bytes{0};
        size_t mapped_bytes{0};
        uint64_t access{0};
    };

    struct process_resource_load_entry final {
        std::mutex mutex;
        std::condition_variable condition;
        std::atomic<bool> ready{false};
        bool loaded{false};
        std::shared_ptr<const resource_response> response;
        std::chrono::steady_clock::time_point expires_at{};
        uint64_t access{0U};
    };

    struct prefetched_resource final {
        std::shared_ptr<const resource_response> response;
        cached_text_resource cached;
        bool loaded{false};
        bool has_cached{false};
        bool from_fresh_cache{false};
    };

    struct shared_isolate_state final {
        v8::ArrayBuffer::Allocator* allocator{nullptr};
        v8::Isolate* isolate{nullptr};
        size_t slot_index{0U};
        std::atomic<size_t> active_contexts{0U};
        std::atomic<size_t> peak_contexts{0U};
        std::unordered_map<std::string, compilation_cache_entry> compilation_cache;
        std::deque<std::string> compilation_cache_order;
        size_t compilation_cache_bytes{0U};
        ~shared_isolate_state()
        {
            if (isolate != nullptr) {
                {
                    v8::Locker locker(isolate);
                    v8::Isolate::Scope isolate_scope(isolate);
                    for (auto& [key, entry] : compilation_cache) {
                        static_cast<void>(key);
                        entry.script.Reset();
                    }
                    compilation_cache.clear();
                    compilation_cache_order.clear();
                }
                isolate->Dispose();
            }
            delete allocator;
        }
    };

    struct shared_isolate_configuration final {
        size_t initial_count{1U};
        size_t maximum_count{1U};
        size_t context_capacity{2U};
        size_t warm_count{1U};
        std::chrono::milliseconds warm_lease{15000};
    };

    static shared_isolate_configuration shared_isolate_config()
    {
        shared_isolate_configuration result;
        result.initial_count = static_cast<size_t>(std::clamp<uint64_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_SHARED_ISOLATE_COUNT").value_or(1U),
            1U,
            16U));
        const auto hardware_limit = static_cast<uint64_t>(
            std::max(1U, std::thread::hardware_concurrency()));
        result.maximum_count = static_cast<size_t>(std::clamp<uint64_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_SHARED_ISOLATE_MAX_COUNT")
                .value_or(hardware_limit),
            result.initial_count,
            64U));
        result.context_capacity = static_cast<size_t>(std::clamp<uint64_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_SHARED_ISOLATE_CAPACITY").value_or(2U),
            1U,
            64U));
        result.warm_count = static_cast<size_t>(std::clamp<uint64_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_SHARED_ISOLATE_WARM_COUNT")
                .value_or(result.initial_count),
            0U,
            result.maximum_count));
        result.warm_lease = std::chrono::milliseconds(std::clamp<uint64_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_SHARED_ISOLATE_WARM_LEASE_MS").value_or(15000U),
            0U,
            300000U));
        return result;
    }

    struct shared_isolate_pool_state final {
        std::mutex mutex;
        std::condition_variable condition;
        std::vector<std::weak_ptr<shared_isolate_state>> isolates;
        std::vector<std::shared_ptr<shared_isolate_state>> warm_isolates;
        std::vector<std::chrono::steady_clock::time_point> warm_deadlines;
        bool stopping{false};
        bool reaper_running{false};
        std::thread reaper;

        ~shared_isolate_pool_state()
        {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            condition.notify_one();
            if (reaper.joinable()) reaper.join();
            warm_isolates.clear();
        }

        void ensure_size(size_t size)
        {
            if (isolates.size() < size) isolates.resize(size);
            if (warm_isolates.size() < size) warm_isolates.resize(size);
            if (warm_deadlines.size() < size) warm_deadlines.resize(size);
        }

        void ensure_reaper()
        {
            if (reaper_running) return;
            if (reaper.joinable()) reaper.join();
            reaper_running = true;
            reaper = std::thread([this] { reap_expired_isolates(); });
        }

        void reap_expired_isolates() noexcept
        {
            std::unique_lock lock(mutex);
            while (!stopping) {
                const auto now = std::chrono::steady_clock::now();
                auto next_deadline =
                    std::chrono::steady_clock::time_point::max();
                std::vector<std::shared_ptr<shared_isolate_state>> expired;
                for (size_t index = 0U;
                    index < warm_isolates.size();
                    ++index) {
                    auto& state = warm_isolates[index];
                    if (state == nullptr) continue;
                    if (state->active_contexts.load(
                            std::memory_order_relaxed) != 0U) {
                        state.reset();
                        warm_deadlines[index] = {};
                        continue;
                    }
                    if (warm_deadlines[index] <= now) {
                        expired.push_back(std::move(state));
                        warm_deadlines[index] = {};
                        isolates[index].reset();
                    } else {
                        next_deadline =
                            std::min(next_deadline, warm_deadlines[index]);
                    }
                }

                if (!expired.empty()) {
                    lock.unlock();
                    expired.clear();
                    lock.lock();
                    continue;
                }
                if (next_deadline
                    == std::chrono::steady_clock::time_point::max()) {
                    reaper_running = false;
                    return;
                } else {
                    condition.wait_until(lock, next_deadline);
                }
            }
            reaper_running = false;
        }
    };

    static shared_isolate_pool_state& shared_isolate_pool()
    {
        static shared_isolate_pool_state pool;
        return pool;
    }

    class optional_isolate_locker final {
    public:
        optional_isolate_locker(v8::Isolate* isolate, bool enabled)
        {
            if (enabled && isolate != nullptr) locker_.emplace(isolate);
        }

        void reset() noexcept
        {
            locker_.reset();
        }

    private:
        std::optional<v8::Locker> locker_;
    };

    static constexpr int runtime_context_embedder_slot = 2;

    static std::shared_ptr<shared_isolate_state> acquire_shared_isolate()
    {
        const auto config = shared_isolate_config();
        auto& pool = shared_isolate_pool();
        std::vector<std::shared_ptr<shared_isolate_state>> expired;
        std::unique_lock lock(pool.mutex);
        pool.ensure_size(config.maximum_count);
        const auto now = std::chrono::steady_clock::now();
        for (size_t index = 0U; index < pool.warm_isolates.size(); ++index) {
            auto& state = pool.warm_isolates[index];
            if (state == nullptr || pool.warm_deadlines[index] > now) continue;
            expired.push_back(std::move(state));
            pool.warm_deadlines[index] = {};
            pool.isolates[index].reset();
        }

        std::shared_ptr<shared_isolate_state> recycled_hole;
        std::shared_ptr<shared_isolate_state> available;
        std::shared_ptr<shared_isolate_state> overflow;
        size_t recycled_hole_contexts{0U};
        size_t available_contexts = std::numeric_limits<size_t>::max();
        size_t overflow_contexts = std::numeric_limits<size_t>::max();
        size_t first_empty_slot = config.maximum_count;
        size_t live_isolates = 0U;
        for (size_t index = 0U; index < config.maximum_count; ++index) {
            auto state = pool.isolates[index].lock();
            if (state == nullptr) {
                if (first_empty_slot == config.maximum_count) {
                    first_empty_slot = index;
                }
                continue;
            }

            ++live_isolates;
            const auto active =
                state->active_contexts.load(std::memory_order_relaxed);
            const auto peak =
                state->peak_contexts.load(std::memory_order_relaxed);
            if (active < peak
                && active < config.context_capacity
                && (recycled_hole == nullptr
                    || active > recycled_hole_contexts)) {
                recycled_hole = state;
                recycled_hole_contexts = active;
            }
            if (active < config.context_capacity
                && (available == nullptr || active < available_contexts)) {
                available = state;
                available_contexts = active;
            }
            if (overflow == nullptr || active < overflow_contexts) {
                overflow = state;
                overflow_contexts = active;
            }
        }

        // Refill a slot vacated by a destroyed context before changing the
        // shard topology. During initial population, however, create one
        // context in each initially configured shard before adding a second
        // context. Once those shards reach capacity, grow lazily up to the
        // configured maximum. This preserves startup/interaction parallelism
        // for the four-chart 1/1 -> 2/2 case, supports larger dynamic chart
        // counts, and makes close/reopen placement deterministic.
        if (recycled_hole != nullptr) {
            const auto slot = recycled_hole->slot_index;
            if (slot < pool.warm_isolates.size()
                && pool.warm_isolates[slot] == recycled_hole) {
                pool.warm_isolates[slot].reset();
                pool.warm_deadlines[slot] = {};
                pool.condition.notify_one();
            }
            recycled_hole->active_contexts.fetch_add(
                1U,
                std::memory_order_relaxed);
            return recycled_hole;
        }
        if (live_isolates >= config.initial_count && available != nullptr) {
            const auto slot = available->slot_index;
            if (slot < pool.warm_isolates.size()
                && pool.warm_isolates[slot] == available) {
                pool.warm_isolates[slot].reset();
                pool.warm_deadlines[slot] = {};
                pool.condition.notify_one();
            }
            const auto active = available->active_contexts.fetch_add(
                1U,
                std::memory_order_relaxed) + 1U;
            auto peak = available->peak_contexts.load(std::memory_order_relaxed);
            while (active > peak
                && !available->peak_contexts.compare_exchange_weak(
                    peak,
                    active,
                    std::memory_order_relaxed)) {
            }
            return available;
        }
        if (live_isolates >= config.maximum_count && overflow != nullptr) {
            const auto active = overflow->active_contexts.fetch_add(
                1U,
                std::memory_order_relaxed) + 1U;
            auto peak = overflow->peak_contexts.load(std::memory_order_relaxed);
            while (active > peak
                && !overflow->peak_contexts.compare_exchange_weak(
                    peak,
                    active,
                    std::memory_order_relaxed)) {
            }
            return overflow;
        }

        if (first_empty_slot == config.maximum_count) return {};
        auto state = std::make_shared<shared_isolate_state>();
        state->slot_index = first_empty_slot;
        state->allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = state->allocator;
        if (const auto maximum_heap_mib =
                unsigned_environment_value("WEBSCENE_V8_MAX_HEAP_MIB");
            maximum_heap_mib.has_value() && *maximum_heap_mib > 0) {
            const auto initial_heap_mib =
                unsigned_environment_value("WEBSCENE_V8_INITIAL_HEAP_MIB").value_or(0);
            params.constraints.ConfigureDefaultsFromHeapSize(
                initial_heap_mib * 1024U * 1024U,
                *maximum_heap_mib * 1024U * 1024U);
        }
        configure_startup_snapshot(params);
        state->isolate = v8::Isolate::New(params);
        if (state->isolate == nullptr) return {};
        state->active_contexts.store(1U, std::memory_order_relaxed);
        state->peak_contexts.store(1U, std::memory_order_relaxed);
        pool.isolates[first_empty_slot] = state;
        return state;
    }

    static void release_shared_isolate(
        const std::shared_ptr<shared_isolate_state>& state) noexcept
    {
        if (state == nullptr) return;
        const auto config = shared_isolate_config();
        auto& pool = shared_isolate_pool();
        std::shared_ptr<shared_isolate_state> evicted;
        {
            std::lock_guard lock(pool.mutex);
            const auto active =
                state->active_contexts.load(std::memory_order_relaxed);
            if (active == 0U) return;
            const auto remaining = active - 1U;
            state->active_contexts.store(
                remaining,
                std::memory_order_relaxed);
            if (remaining != 0U
                || config.warm_count == 0U
                || config.warm_lease.count() == 0) {
                return;
            }

            pool.ensure_size(std::max(
                config.maximum_count,
                state->slot_index + 1U));
            size_t warm_count = 0U;
            size_t oldest_slot = pool.warm_isolates.size();
            auto oldest_deadline =
                std::chrono::steady_clock::time_point::max();
            for (size_t index = 0U;
                index < pool.warm_isolates.size();
                ++index) {
                if (pool.warm_isolates[index] == nullptr) continue;
                ++warm_count;
                if (pool.warm_deadlines[index] < oldest_deadline) {
                    oldest_deadline = pool.warm_deadlines[index];
                    oldest_slot = index;
                }
            }
            if (warm_count >= config.warm_count
                && oldest_slot < pool.warm_isolates.size()) {
                evicted = std::move(pool.warm_isolates[oldest_slot]);
                pool.warm_deadlines[oldest_slot] = {};
                pool.isolates[oldest_slot].reset();
            }

            pool.warm_isolates[state->slot_index] = state;
            pool.warm_deadlines[state->slot_index] =
                std::chrono::steady_clock::now() + config.warm_lease;
            pool.isolates[state->slot_index] = state;
            pool.ensure_reaper();
            pool.condition.notify_one();
        }
    }

    optional_isolate_locker lock_shared_isolate() const
    {
        return optional_isolate_locker(isolate, shared_isolate != nullptr);
    }

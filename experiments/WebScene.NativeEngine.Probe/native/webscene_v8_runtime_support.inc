enum class binding_category : size_t {
    canvas_draw,
    canvas_path,
    canvas_transform,
    canvas_state,
    canvas_backing_store,
    dom_geometry,
    dom_property,
    dom_mutation,
    dom_query,
    style,
    count
};

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
constexpr size_t binding_category_count = static_cast<size_t>(binding_category::count);
constexpr std::array<const char*, binding_category_count> binding_category_names{
    "canvas-draw",
    "canvas-path",
    "canvas-transform",
    "canvas-state",
    "canvas-backing-store",
    "dom-geometry",
    "dom-property",
    "dom-mutation",
    "dom-query",
    "style"};
#endif

struct binding_callback_stats final {
    uint64_t calls{0};
    uint64_t nanoseconds{0};
};

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
struct startup_phase_snapshot final {
    binding_callback_stats resource_read{};
    binding_callback_stats css_parse{};
    binding_callback_stats css_apply{};
    binding_callback_stats css_incremental_apply{};
    binding_callback_stats subtree_recascade{};
    binding_callback_stats stylesheet_recascade{};
    binding_callback_stats layout{};
    binding_callback_stats script_execute{};
    uint64_t stylesheet_nodes{0};
    uint64_t stylesheet_variable_nodes{0};
    uint64_t layout_passes{0};
    bool document_dirty{false};
};

struct startup_nested_phase_stats final {
    binding_callback_stats elapsed{};
    binding_callback_stats resource_read{};
    binding_callback_stats css_parse{};
    binding_callback_stats css_apply{};
    binding_callback_stats css_incremental_apply{};
    binding_callback_stats subtree_recascade{};
    binding_callback_stats stylesheet_recascade{};
    binding_callback_stats layout{};
    binding_callback_stats script_execute{};
    uint64_t stylesheet_nodes{0};
    uint64_t stylesheet_variable_nodes{0};
    uint64_t layout_passes{0};
    uint64_t clean_to_dirty_transitions{0};
    uint64_t tasks_left_dirty{0};
};

void accumulate_callback_delta(
    binding_callback_stats& target,
    const binding_callback_stats& current,
    const binding_callback_stats& before) noexcept
{
    target.calls += current.calls - before.calls;
    target.nanoseconds += current.nanoseconds - before.nanoseconds;
}
#endif

std::filesystem::path runtime_file_path(const std::filesystem::path& path)
{
#if defined(_WIN32)
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) absolute = path;
    auto native = absolute.lexically_normal().native();
    if (native.starts_with(L"\\\\?\\")) return std::filesystem::path(native);
    if (native.starts_with(L"\\\\")) {
        return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2U));
    }
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return path;
#endif
}

class binding_callback_timer final {
public:
    explicit binding_callback_timer(
        binding_callback_stats* stats,
        binding_callback_stats* named_stats = nullptr) noexcept
        : stats_(stats), named_stats_(named_stats)
    {
        if (stats_ != nullptr || named_stats_ != nullptr) {
            started_ = std::chrono::steady_clock::now();
        }
    }

    ~binding_callback_timer()
    {
        if (stats_ == nullptr && named_stats_ == nullptr) return;
        const auto elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started_).count());
        if (stats_ != nullptr) {
            ++stats_->calls;
            stats_->nanoseconds += elapsed;
        }
        if (named_stats_ != nullptr) {
            ++named_stats_->calls;
            named_stats_->nanoseconds += elapsed;
        }
    }

private:
    binding_callback_stats* stats_;
    binding_callback_stats* named_stats_;
    std::chrono::steady_clock::time_point started_{};
};

class immutable_byte_buffer final {
public:
    immutable_byte_buffer(const immutable_byte_buffer&) = delete;
    immutable_byte_buffer& operator=(const immutable_byte_buffer&) = delete;

    ~immutable_byte_buffer()
    {
#if defined(_WIN32)
        if (mapped_base_ != nullptr) UnmapViewOfFile(mapped_base_);
        if (mapping_ != nullptr) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
        if (mapped_base_ != MAP_FAILED) {
            static_cast<void>(munmap(mapped_base_, mapped_size_));
        }
        if (file_ >= 0) static_cast<void>(close(file_));
#endif
    }

    static std::shared_ptr<const immutable_byte_buffer> copy(
        const uint8_t* data,
        size_t size)
    {
        if (data == nullptr || size == 0U) return {};
        auto result = std::shared_ptr<immutable_byte_buffer>(
            new immutable_byte_buffer());
        result->owned_.assign(data, data + size);
        result->data_ = result->owned_.data();
        result->size_ = result->owned_.size();
        return result;
    }

    static std::shared_ptr<const immutable_byte_buffer> take(
        std::vector<uint8_t> data)
    {
        if (data.empty()) return {};
        auto result = std::shared_ptr<immutable_byte_buffer>(
            new immutable_byte_buffer());
        result->owned_ = std::move(data);
        result->data_ = result->owned_.data();
        result->size_ = result->owned_.size();
        return result;
    }

    static std::shared_ptr<const immutable_byte_buffer> map_file(
        const std::filesystem::path& path,
        size_t payload_offset,
        size_t payload_size)
    {
        if (payload_size == 0U
            || payload_offset > std::numeric_limits<size_t>::max() - payload_size) {
            return {};
        }
        auto result = std::shared_ptr<immutable_byte_buffer>(
            new immutable_byte_buffer());
#if defined(_WIN32)
        const auto native_path = runtime_file_path(path);
        result->file_ = CreateFileW(
            native_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (result->file_ == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER file_size{};
        if (!GetFileSizeEx(result->file_, &file_size)
            || file_size.QuadPart <= 0
            || static_cast<uint64_t>(file_size.QuadPart)
                < static_cast<uint64_t>(payload_offset + payload_size)) {
            return {};
        }
        result->mapping_ = CreateFileMappingW(
            result->file_,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr);
        if (result->mapping_ == nullptr) return {};
        result->mapped_base_ = static_cast<const uint8_t*>(
            MapViewOfFile(result->mapping_, FILE_MAP_READ, 0, 0, 0));
        if (result->mapped_base_ == nullptr) return {};
        result->mapped_size_ = static_cast<size_t>(file_size.QuadPart);
#else
        result->file_ = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (result->file_ < 0) return {};
        struct stat metadata{};
        if (fstat(result->file_, &metadata) != 0
            || metadata.st_size <= 0
            || static_cast<uint64_t>(metadata.st_size)
                < static_cast<uint64_t>(payload_offset + payload_size)) {
            return {};
        }
        result->mapped_size_ = static_cast<size_t>(metadata.st_size);
        result->mapped_base_ = mmap(
            nullptr,
            result->mapped_size_,
            PROT_READ,
            MAP_PRIVATE,
            result->file_,
            0);
        if (result->mapped_base_ == MAP_FAILED) return {};
#endif
        result->data_ =
            static_cast<const uint8_t*>(result->mapped_base_) + payload_offset;
        result->size_ = payload_size;
        return result;
    }

    const uint8_t* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0U; }
    bool mapped() const noexcept
    {
#if defined(_WIN32)
        return mapped_base_ != nullptr;
#else
        return mapped_base_ != MAP_FAILED;
#endif
    }

private:
    immutable_byte_buffer() = default;

    std::vector<uint8_t> owned_;
    const uint8_t* data_{nullptr};
    size_t size_{0U};
#if defined(_WIN32)
    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{nullptr};
    const uint8_t* mapped_base_{nullptr};
#else
    int file_{-1};
    void* mapped_base_{MAP_FAILED};
#endif
    size_t mapped_size_{0U};
};

class immutable_text_source final {
public:
    static std::shared_ptr<const immutable_text_source> from_string(
        std::shared_ptr<const std::string> source)
    {
        if (source == nullptr) return {};
        auto result = std::shared_ptr<immutable_text_source>(
            new immutable_text_source());
        result->string_ = std::move(source);
        return result;
    }

    static std::shared_ptr<const immutable_text_source> from_bytes(
        std::shared_ptr<const immutable_byte_buffer> source)
    {
        if (source == nullptr) return {};
        auto result = std::shared_ptr<immutable_text_source>(
            new immutable_text_source());
        result->bytes_ = std::move(source);
        return result;
    }

    const char* data() const noexcept
    {
        return string_ != nullptr
            ? string_->data()
            : reinterpret_cast<const char*>(bytes_->data());
    }

    size_t size() const noexcept
    {
        return string_ != nullptr ? string_->size() : bytes_->size();
    }

    bool empty() const noexcept { return size() == 0U; }

    bool mapped() const noexcept
    {
        return bytes_ != nullptr && bytes_->mapped();
    }

    std::string_view view() const noexcept
    {
        return {data(), size()};
    }

private:
    immutable_text_source() = default;

    std::shared_ptr<const std::string> string_;
    std::shared_ptr<const immutable_byte_buffer> bytes_;
};

using css::inline_style_property;
using enum css::inline_style_property;

std::once_flag v8_initialize_once;
std::unique_ptr<v8::Platform> v8_platform;

std::optional<uint64_t> unsigned_environment_value(const char* name)
{
    const auto* text = std::getenv(name);
    if (text == nullptr || *text == '\0') return {};
    uint64_t value = 0;
    const auto* end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end
        ? std::optional<uint64_t>{value}
        : std::optional<uint64_t>{};
}

std::optional<bool> boolean_environment_value(const char* name)
{
    const auto* text = std::getenv(name);
    if (text == nullptr) return {};
    const std::string_view value{text};
    if (value.empty()
        || value == "1"
        || value == "true"
        || value == "TRUE"
        || value == "yes"
        || value == "YES"
        || value == "on"
        || value == "ON") {
        return true;
    }
    if (value == "0"
        || value == "false"
        || value == "FALSE"
        || value == "no"
        || value == "NO"
        || value == "off"
        || value == "OFF") {
        return false;
    }
    return {};
}

std::filesystem::path native_module_directory()
{
#if defined(_WIN32)
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(&native_module_directory);
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address,
            &module) == 0) {
        throw std::runtime_error("Unable to locate the WebScene native engine module.");
    }

    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const auto length = GetModuleFileNameW(
            module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("Unable to read the WebScene native engine module path.");
        }
        if (length < buffer.size() - 1U) {
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
#else
    Dl_info module{};
    if (dladdr(reinterpret_cast<const void*>(&native_module_directory), &module) == 0
        || module.dli_fname == nullptr) {
        throw std::runtime_error("Unable to locate the WebScene native engine module.");
    }
    return std::filesystem::weakly_canonical(module.dli_fname).parent_path();
#endif
}

#if defined(WEBSCENE_V8_BOOTSTRAP_SNAPSHOT)
std::once_flag startup_snapshot_once;
std::vector<char> startup_snapshot_bytes;
v8::StartupData startup_snapshot_data{};

void configure_startup_snapshot(v8::Isolate::CreateParams& params)
{
    std::call_once(startup_snapshot_once, [] {
        const auto directory = native_module_directory();
        const auto metadata_path =
            directory / WEBSCENE_V8_SNAPSHOT_METADATA_FILENAME;
        const auto blob_path = directory / WEBSCENE_V8_SNAPSHOT_FILENAME;

        std::ifstream metadata_stream(metadata_path, std::ios::binary);
        std::string metadata(
            std::istreambuf_iterator<char>{metadata_stream},
            std::istreambuf_iterator<char>{});
        if (!metadata_stream || metadata != WEBSCENE_V8_SNAPSHOT_FINGERPRINT) {
            throw std::runtime_error(
                "WebScene V8 snapshot metadata is missing or incompatible: "
                + metadata_path.string());
        }

        std::ifstream blob_stream(blob_path, std::ios::binary | std::ios::ate);
        if (!blob_stream) {
            throw std::runtime_error(
                "WebScene V8 snapshot is missing: " + blob_path.string());
        }
        const auto size = blob_stream.tellg();
        if (size <= 0 || size > std::numeric_limits<int>::max()) {
            throw std::runtime_error(
                "WebScene V8 snapshot has an invalid size: " + blob_path.string());
        }
        startup_snapshot_bytes.resize(static_cast<size_t>(size));
        blob_stream.seekg(0);
        blob_stream.read(startup_snapshot_bytes.data(), size);
        if (!blob_stream) {
            throw std::runtime_error(
                "Unable to read WebScene V8 snapshot: " + blob_path.string());
        }
        startup_snapshot_data.data = startup_snapshot_bytes.data();
        startup_snapshot_data.raw_size = static_cast<int>(size);
        if (!startup_snapshot_data.IsValid()) {
            throw std::runtime_error(
                "V8 rejected the WebScene startup snapshot: " + blob_path.string());
        }
    });
    params.snapshot_blob = &startup_snapshot_data;
}

constexpr bool bootstrap_snapshot_enabled = true;
#else
void configure_startup_snapshot(v8::Isolate::CreateParams&) {}
constexpr bool bootstrap_snapshot_enabled = false;
#endif

display_mode parse_display_mode(std::string_view value) { return css::parse_display_mode(value); }

std::string_view display_mode_name(display_mode value)
{
    switch (value) {
    case display_mode::flex: return "flex";
    case display_mode::inline_flex: return "inline-flex";
    case display_mode::grid: return "grid";
    case display_mode::inline_grid: return "inline-grid";
    case display_mode::table: return "table";
    case display_mode::inline_table: return "inline-table";
    case display_mode::table_row_group: return "table-row-group";
    case display_mode::table_header_group: return "table-header-group";
    case display_mode::table_footer_group: return "table-footer-group";
    case display_mode::table_row: return "table-row";
    case display_mode::table_cell: return "table-cell";
    case display_mode::table_column_group: return "table-column-group";
    case display_mode::table_column: return "table-column";
    case display_mode::table_caption: return "table-caption";
    case display_mode::contents: return "contents";
    case display_mode::inline_flow: return "inline";
    case display_mode::inline_block: return "inline-block";
    case display_mode::list_item: return "list-item";
    case display_mode::none: return "none";
    case display_mode::block:
    default: return "block";
    }
}

using css::parsed_flex_flow;
parsed_flex_flow parse_flex_flow(std::string_view value) { return css::parse_flex_flow(value); }

std::string flex_direction_name(const node_style& style)
{
    return style.direction == flex_direction::row
        ? style.flex_reverse ? "row-reverse" : "row"
        : style.flex_reverse ? "column-reverse" : "column";
}

std::string flex_wrap_name(const node_style& style)
{
    return style.flex_wrap ? "wrap" : "nowrap";
}

using css::parse_overflow_mode;
using css::computed_overflow;
using css::refresh_overflow_state;

display_mode default_display_for_node(const dom_node& node)
{
    return native_default_display_for_node(node);
}

using css::inherited_font_size;
using css::resolved_font_family;
using css::resolved_font_smoothing;
using css::resolved_font_weight;
using css::resolved_letter_spacing;
using css::resolved_word_spacing;
using css::resolved_declared_font_size;
using css::resolved_declared_line_height;
using css::parse_font_shorthand;
using css::parsed_font_shorthand;

std::string serialize_overflow_mode(overflow_mode value)
{
    switch (value) {
    case overflow_mode::hidden: return "hidden";
    case overflow_mode::clip: return "clip";
    case overflow_mode::automatic: return "auto";
    case overflow_mode::scroll: return "scroll";
    default: return "visible";
    }
}

std::string serialize_css_number(double value)
{
    if (std::abs(value) < 0.0000001) value = 0;
    std::ostringstream stream;
    stream << std::setprecision(8) << value;
    return stream.str();
}

std::string canonical_css_property_name(std::string_view name)
{ return css::canonical_property_name(name); }

bool is_css_color_value_property(std::string_view name)
{
    const auto canonical_name = canonical_css_property_name(name);
    return canonical_name == "color"
        || canonical_name == "fill"
        || canonical_name == "stroke"
        || canonical_name.ends_with("-color");
}

std::string serialize_cssom_specified_value(
    std::string_view property_name,
    const std::string& value)
{
    if (!is_css_color_value_property(property_name)
        || value.empty()
        || value.front() != '#'
        || (value.size() != 4U && value.size() != 5U
            && value.size() != 7U && value.size() != 9U)) {
        return value;
    }

    const auto color = native_document::parse_color(value);
    const auto red = static_cast<unsigned>((color >> 24U) & 0xFFU);
    const auto green = static_cast<unsigned>((color >> 16U) & 0xFFU);
    const auto blue = static_cast<unsigned>((color >> 8U) & 0xFFU);
    const auto alpha = static_cast<unsigned>(color & 0xFFU);
    std::ostringstream stream;
    if (alpha == 0xFFU) {
        stream << "rgb(" << red << ", " << green << ", " << blue << ')';
    } else {
        const auto normalized_alpha = std::round(
            static_cast<double>(alpha) / 255.0 * 1000.0) / 1000.0;
        stream << "rgba(" << red << ", " << green << ", " << blue << ", "
            << serialize_css_number(normalized_alpha) << ')';
    }
    return stream.str();
}

std::string css_property_idl_name(std::string_view name)
{
    if (name == "gridGap" || name == "grid-gap") return "gap";
    if (name == "gridRowGap" || name == "grid-row-gap") return "rowGap";
    if (name == "gridColumnGap" || name == "grid-column-gap") return "columnGap";
    if (name == "float") return "cssFloat";
    if (name.find('-') == std::string_view::npos) return std::string(name);
    std::string result;
    result.reserve(name.size());
    auto uppercase_next = false;
    for (const auto character : name) {
        if (character == '-') {
            uppercase_next = true;
            continue;
        }
        result.push_back(uppercase_next
            ? static_cast<char>(std::toupper(static_cast<unsigned char>(character)))
            : character);
        uppercase_next = false;
    }
    return result;
}

bool valid_cssom_declaration_value(std::string_view property_name, std::string_view value)
{
    if (property_name.starts_with("--") || value.empty()) return true;

    const auto first = value.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string_view::npos) return false;
    const auto last = value.find_last_not_of(" \t\r\n\f\v");
    const auto trimmed = value.substr(first, last - first + 1U);
    const auto canonical_name = canonical_css_property_name(property_name);
    auto normalized_value = std::string(trimmed);
    std::transform(
        normalized_value.begin(), normalized_value.end(), normalized_value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (normalized_value == "inherit" || normalized_value == "initial"
        || normalized_value == "revert" || normalized_value == "revert-layer"
        || normalized_value == "unset" || normalized_value.find("var(") != std::string::npos
        || normalized_value.find("calc(") != std::string::npos
        || normalized_value.find("min(") != std::string::npos
        || normalized_value.find("max(") != std::string::npos
        || normalized_value.find("clamp(") != std::string::npos) {
        return true;
    }

    if (canonical_name == "position") {
        return normalized_value == "static" || normalized_value == "relative"
            || normalized_value == "absolute" || normalized_value == "fixed"
            || normalized_value == "sticky" || normalized_value == "-webkit-sticky";
    }

    if (canonical_name == "width" || canonical_name == "height"
        || canonical_name == "min-width" || canonical_name == "min-height"
        || canonical_name == "max-width" || canonical_name == "max-height"
        || canonical_name == "inline-size" || canonical_name == "block-size"
        || canonical_name == "min-inline-size" || canonical_name == "min-block-size"
        || canonical_name == "max-inline-size" || canonical_name == "max-block-size") {
        // Plain negative sizes are invalid declarations, not zero-sized boxes.
        // Ignoring the write preserves the previous value during transient
        // virtualizer updates. Math functions above are valid at parse time
        // and instead have their range enforced when resolving the used value.
        char* end = nullptr;
        const auto number = std::strtod(normalized_value.c_str(), &end);
        if (end != normalized_value.c_str() && number < 0) return false;
    }

    if (canonical_name == "font-size") {
        static constexpr std::array<std::string_view, 10> keywords{
            "xx-small", "x-small", "small", "medium", "large", "x-large",
            "xx-large", "xxx-large", "larger", "smaller"};
        if (std::find(keywords.begin(), keywords.end(), normalized_value) != keywords.end()) {
            return true;
        }
    } else if (canonical_name == "letter-spacing" && normalized_value == "normal") {
        return true;
    } else if (canonical_name != "letter-spacing") {
        return true;
    }

    char* end = nullptr;
    const auto numeric_text = std::string(normalized_value);
    const auto parsed = std::strtod(numeric_text.c_str(), &end);
    return end == nullptr || *end != '\0' || !std::isfinite(parsed) || parsed == 0.0;
}

bool is_css_whitespace(unsigned char character)
{
    return character == '\t' || character == '\n' || character == '\f'
        || character == '\r' || character == ' ';
}

bool valid_custom_property_name(std::string_view name)
{
    return css::valid_custom_property_name(name);
}

void initialize_v8_process()
{
    std::call_once(v8_initialize_once, [] {
#if defined(WEBSCENE_V8_PARTITION_ALLOC)
#if PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
        // Standalone V8's d8 shell performs this initialization before
        // entering V8. Without it, PartitionAlloc's process root has no
        // thread cache and allocation-heavy multi-context resize becomes
        // serialized on the central partition lock.
        allocator_shim::ConfigurePartitionsForTesting();
        auto* partition =
            allocator_shim::internal::PartitionAllocMalloc::Allocator();
        partition->EnableThreadCacheIfSupported();
        // PartitionAlloc's 512-byte default preserves the lock-avoidance win
        // for the chart workload without retaining the larger per-thread
        // buckets selected by d8's throughput-oriented 32 KiB policy.
        constexpr size_t default_thread_cache_large_size_threshold = 512;
        const auto thread_cache_large_size_threshold = static_cast<size_t>(
            unsigned_environment_value(
                "WEBSCENE_V8_PARTITION_ALLOC_THREAD_CACHE_MAX_BYTES")
                .value_or(default_thread_cache_large_size_threshold));
        partition_alloc::ThreadCache::SetLargestCachedSize(
            thread_cache_large_size_threshold);
        constexpr int foreground_dirty_bytes_shift = 2;
        constexpr int foreground_slot_span_ring_size = 1 << 10;
        partition->AdjustSlotSpanRing(
            foreground_slot_span_ring_size,
            foreground_dirty_bytes_shift);
#endif
#endif
        bool optimize_for_size = false;
#if defined(WEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT)
        optimize_for_size = true;
#endif
        if (const auto configured =
                boolean_environment_value("WEBSCENE_V8_OPTIMIZE_FOR_SIZE");
            configured.has_value()) {
            optimize_for_size = configured.value();
        }
        if (optimize_for_size) {
            v8::V8::SetFlagsFromString("--optimize-for-size");
        }
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        // V8 15.3 disables Debugger.setScriptSource by default even when the
        // Inspector is linked. WebScene keeps the transport opt-in, while an
        // attached debugger must retain the standard live-edit capability.
        v8::V8::SetFlagsFromString("--inspector-live-edit");
#endif
        if (const auto preconfigured_old_space_mib =
                unsigned_environment_value(
                    "WEBSCENE_V8_PRECONFIGURED_OLD_SPACE_MIB");
            preconfigured_old_space_mib.has_value()) {
            // V8 15.3 raised this process-wide startup policy from 0 to
            // 32 MiB. The higher value avoids an early full-GC feedback
            // cycle and is the better Release-JIT tradeoff, while zero
            // recovers about 12 MiB of V8 heap across two isolates in the
            // NativeAOT four-chart workload. Keep the upstream default unless
            // the host selects the measured AOT policy before process prewarm.
            constexpr uint64_t maximum_preconfigured_old_space_mib = 4096U;
            const auto bounded_value = std::min(
                *preconfigured_old_space_mib,
                maximum_preconfigured_old_space_mib);
            const auto flag = "--preconfigured-old-space-size="
                + std::to_string(bounded_value);
            v8::V8::SetFlagsFromString(flag.c_str());
        }
        if (const auto* experimental_flags =
                std::getenv("WEBSCENE_V8_EXPERIMENTAL_FLAGS");
            experimental_flags != nullptr && experimental_flags[0] != '\0') {
            v8::V8::SetFlagsFromString(experimental_flags);
        }
#if defined(WEBSCENE_V8_ICU_DATA_FILENAME)
        const auto icu_data_path =
            native_module_directory() / WEBSCENE_V8_ICU_DATA_FILENAME;
        if (!std::filesystem::is_regular_file(icu_data_path)
            || !v8::V8::InitializeICU(icu_data_path.string().c_str())) {
            throw std::runtime_error(
                "Unable to initialize V8 ICU data beside the WebScene native engine: "
                + icu_data_path.string());
        }
#else
        throw std::runtime_error("The WebScene native engine was built without an ICU data filename.");
#endif
        const auto thread_pool_size = static_cast<int>(std::min<uint64_t>(
            unsigned_environment_value("WEBSCENE_V8_PLATFORM_THREADS").value_or(0),
            16));
        // Chromium exposes idle deadlines to V8 as part of its normal event
        // loop. Keep the default platform's idle queue enabled so WebScene can
        // do the same at true idle boundaries; accepting an idle task does not
        // itself execute work or add per-node instrumentation overhead.
        const auto idle_tasks =
            std::getenv("WEBSCENE_V8_DISABLE_IDLE_TASKS") == nullptr
                ? v8::platform::IdleTaskSupport::kEnabled
                : v8::platform::IdleTaskSupport::kDisabled;
        v8_platform = v8::platform::NewDefaultPlatform(
            thread_pool_size,
            idle_tasks);
        v8::V8::InitializePlatform(v8_platform.get());
        v8::V8::Initialize();
    });
}

std::string to_utf8(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value text(isolate, value);
    return *text == nullptr ? std::string{} : std::string(*text, text.length());
}

// Web IDL DOMString preserves every UTF-16 code unit, including unpaired
// surrogates.  V8's Utf8Value deliberately replaces those code units, which
// makes a lone surrogate alias U+FFFD in attributes and selector matching.
// Store DOM strings as WTF-8 so the native tree remains byte-oriented without
// losing the original JavaScript string.
std::string to_wtf8(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    auto local_context = isolate->GetCurrentContext();
    v8::Local<v8::String> text;
    if (!value->ToString(local_context).ToLocal(&text)) return {};
    std::string result(text->Utf8LengthV2(isolate), '\0');
    const auto written = text->WriteUtf8V2(
        isolate,
        result.data(),
        result.size(),
        v8::String::WriteFlags::kNone);
    result.resize(written);
    return result;
}

v8::Local<v8::String> js_string(v8::Isolate* isolate, const char* value)
{
    return v8::String::NewFromUtf8(isolate, value, v8::NewStringType::kNormal)
        .ToLocalChecked();
}

std::vector<uint16_t> decode_wtf8_to_utf16(std::string_view value)
{
    std::vector<uint16_t> units;
    units.reserve(value.size());
    for (size_t index = 0; index < value.size();) {
        const auto first = static_cast<uint8_t>(value[index]);
        uint32_t codepoint = 0xfffdU;
        size_t length = 1U;
        uint32_t minimum = 0U;
        if (first < 0x80U) {
            codepoint = first;
        } else if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU;
            length = 2U;
            minimum = 0x80U;
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU;
            length = 3U;
            minimum = 0x800U;
        } else if ((first & 0xf8U) == 0xf0U) {
            codepoint = first & 0x07U;
            length = 4U;
            minimum = 0x10000U;
        }
        auto valid = length > 1U && index + length <= value.size();
        for (size_t offset = 1U; valid && offset < length; ++offset) {
            const auto continuation = static_cast<uint8_t>(value[index + offset]);
            valid = (continuation & 0xc0U) == 0x80U;
            if (valid) codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        if (length == 1U && first < 0x80U) {
            valid = true;
        } else if (!valid || codepoint < minimum || codepoint > 0x10ffffU) {
            codepoint = 0xfffdU;
            length = 1U;
        }
        if (codepoint <= 0xffffU) {
            // Deliberately retain surrogate code points decoded from WTF-8.
            units.push_back(static_cast<uint16_t>(codepoint));
        } else {
            codepoint -= 0x10000U;
            units.push_back(static_cast<uint16_t>(0xd800U + (codepoint >> 10U)));
            units.push_back(static_cast<uint16_t>(0xdc00U + (codepoint & 0x3ffU)));
        }
        index += length;
    }
    return units;
}

v8::Local<v8::String> js_dom_string(v8::Isolate* isolate, std::string_view value)
{
    const auto units = decode_wtf8_to_utf16(value);
    return v8::String::NewFromTwoByte(
        isolate,
        units.data(),
        v8::NewStringType::kNormal,
        static_cast<int>(units.size())).ToLocalChecked();
}

void install_navigator(
    v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    v8::Local<v8::Object> global)
{
#if defined(__APPLE__)
    constexpr auto platform = "MacIntel";
    constexpr auto user_agent_platform = "Macintosh; Intel Mac OS X 10_15_7";
    constexpr auto client_platform = "macOS";
#elif defined(_WIN32)
    constexpr auto platform = "Win32";
    constexpr auto user_agent_platform = "Windows NT 10.0; Win64; x64";
    constexpr auto client_platform = "Windows";
#else
    constexpr auto platform = "Linux x86_64";
    constexpr auto user_agent_platform = "X11; Linux x86_64";
    constexpr auto client_platform = "Linux";
#endif
    const auto user_agent = std::string("Mozilla/5.0 (") + user_agent_platform
        + ") AppleWebKit/537.36 (KHTML, like Gecko) WebScene.Native/V8";
    auto navigator = v8::Object::New(isolate);
    navigator->Set(
        context,
        js_string(isolate, "userAgent"),
        js_string(isolate, user_agent.c_str())).Check();
    navigator->Set(
        context,
        js_string(isolate, "platform"),
        js_string(isolate, platform)).Check();
    navigator->Set(
        context,
        js_string(isolate, "language"),
        js_string(isolate, "en-US")).Check();
    navigator->Set(
        context,
        js_string(isolate, "maxTouchPoints"),
        v8::Integer::New(isolate, 0)).Check();
    auto user_agent_data = v8::Object::New(isolate);
    user_agent_data->Set(
        context,
        js_string(isolate, "platform"),
        js_string(isolate, client_platform)).Check();
    user_agent_data->Set(
        context,
        js_string(isolate, "mobile"),
        v8::False(isolate)).Check();
    navigator->Set(
        context,
        js_string(isolate, "userAgentData"),
        user_agent_data).Check();
    global->Set(context, js_string(isolate, "navigator"), navigator).Check();
}

constexpr std::array<uint32_t, 64> sha256_round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

uint32_t rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32U - count));
}

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t length)
{
    std::array<uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    const auto bit_length = static_cast<uint64_t>(length) * 8U;
    const auto padded_length = ((length + 9U + 63U) / 64U) * 64U;
    std::vector<uint8_t> padded(padded_length, 0U);
    if (length > 0) std::memcpy(padded.data(), data, length);
    padded[length] = 0x80U;
    for (size_t index = 0; index < 8U; ++index) {
        padded[padded_length - 1U - index] = static_cast<uint8_t>(bit_length >> (index * 8U));
    }

    for (size_t offset = 0; offset < padded.size(); offset += 64U) {
        std::array<uint32_t, 64> words{};
        for (size_t index = 0; index < 16U; ++index) {
            const auto position = offset + index * 4U;
            words[index] = static_cast<uint32_t>(padded[position]) << 24U
                | static_cast<uint32_t>(padded[position + 1U]) << 16U
                | static_cast<uint32_t>(padded[position + 2U]) << 8U
                | static_cast<uint32_t>(padded[position + 3U]);
        }
        for (size_t index = 16; index < words.size(); ++index) {
            const auto s0 = rotate_right(words[index - 15U], 7U)
                ^ rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const auto s1 = rotate_right(words[index - 2U], 17U)
                ^ rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice + sha256_round_constants[index] + words[index];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<uint8_t, 32> result{};
    for (size_t index = 0; index < state.size(); ++index) {
        result[index * 4U] = static_cast<uint8_t>(state[index] >> 24U);
        result[index * 4U + 1U] = static_cast<uint8_t>(state[index] >> 16U);
        result[index * 4U + 2U] = static_cast<uint8_t>(state[index] >> 8U);
        result[index * 4U + 3U] = static_cast<uint8_t>(state[index]);
    }
    return result;
}

std::string to_hex(const std::array<uint8_t, 32>& digest)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : digest) stream << std::setw(2) << static_cast<unsigned>(value);
    return stream.str();
}

std::array<uint8_t, 32> compilation_key_digest(
    const std::string& document_name,
    std::string_view source)
{
    // Cached V8 data is only reusable under the native embedder contract that
    // produced it. Bump this identity whenever runtime bootstrap semantics or
    // compile flags change in a way that can invalidate accepted cached data.
    constexpr std::string_view runtime_identity = "webscene-native-engine-abi-2-v8-cache-2";
    std::vector<uint8_t> value;
    value.reserve(runtime_identity.size() + document_name.size() + source.size() + 2U);
    value.insert(value.end(), runtime_identity.begin(), runtime_identity.end());
    value.push_back(0U);
    value.insert(value.end(), document_name.begin(), document_name.end());
    value.push_back(0U);
    value.insert(value.end(), source.begin(), source.end());
    return sha256(value.data(), value.size());
}

std::array<uint8_t, 32> resource_key_digest(uint32_t kind, const std::string& url)
{
    std::vector<uint8_t> value;
    value.reserve(sizeof(kind) + url.size());
    for (size_t offset = 0U; offset < sizeof(kind); ++offset) {
        value.push_back(static_cast<uint8_t>(kind >> (offset * 8U)));
    }
    value.insert(value.end(), url.begin(), url.end());
    return sha256(value.data(), value.size());
}

uint64_t current_process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<uint64_t>(_getpid());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

float resolve_connected_css_length(
    const dom_node& context,
    css_length length,
    float percentage_reference)
{
    auto* root = &context;
    while (root->parent != nullptr) root = root->parent;
    const auto font_size = context.style.font_size >= 0
        ? context.style.font_size : inherited_font_size(context);
    const auto root_font_size = document_root_font_size(context);
    switch (length.unit) {
    case length_unit::pixels:
        return length.value;
    case length_unit::percent:
        return percentage_reference * length.value / 100.0F + length.pixel_offset;
    case length_unit::em:
        return length.value * font_size;
    case length_unit::rem:
        return length.value * root_font_size;
    case length_unit::viewport_width:
        return root->layout.width * length.value / 100.0F + length.pixel_offset;
    case length_unit::viewport_height:
        return root->layout.height * length.value / 100.0F + length.pixel_offset;
    case length_unit::viewport_width_capped:
        return std::min(
            root->layout.width * length.value / 100.0F,
            length.pixel_offset);
    case length_unit::viewport_height_capped:
        return std::min(
            root->layout.height * length.value / 100.0F,
            length.pixel_offset);
    case length_unit::viewport_width_floored:
        return std::max(
            root->layout.width * length.value / 100.0F,
            length.pixel_offset);
    case length_unit::viewport_height_floored:
        return std::max(
            root->layout.height * length.value / 100.0F,
            length.pixel_offset);
    case length_unit::automatic:
    case length_unit::max_content:
    case length_unit::min_content:
    case length_unit::fit_content:
    case length_unit::stretch:
    default:
        return 0.0F;
    }
}

struct connected_containing_box final {
    float padding_width{0};
    float padding_height{0};
    float content_width{0};
    float content_height{0};
};

connected_containing_box connected_box_for(const dom_node& node)
{
    const auto width = std::max(0.0F, node.layout.width);
    const auto height = std::max(0.0F, node.layout.height);
    const auto border_left = resolve_connected_css_length(
        node, node.style.border_left_width, width);
    const auto border_right = resolve_connected_css_length(
        node, node.style.border_right_width, width);
    const auto border_top = resolve_connected_css_length(
        node, node.style.border_top_width, width);
    const auto border_bottom = resolve_connected_css_length(
        node, node.style.border_bottom_width, width);
    const auto padding_width = std::max(0.0F, width - border_left - border_right);
    const auto padding_height = std::max(0.0F, height - border_top - border_bottom);
    const auto padding_left = resolve_connected_css_length(
        node, node.style.padding_left, padding_width);
    const auto padding_right = resolve_connected_css_length(
        node, node.style.padding_right, padding_width);
    const auto padding_top = resolve_connected_css_length(
        node, node.style.padding_top, padding_width);
    const auto padding_bottom = resolve_connected_css_length(
        node, node.style.padding_bottom, padding_width);
    return {
        padding_width,
        padding_height,
        std::max(0.0F, padding_width - padding_left - padding_right),
        std::max(0.0F, padding_height - padding_top - padding_bottom)};
}

bool has_display_none_ancestor(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.display == display_mode::none) return true;
    }
    return false;
}

float connected_computed_dimension(
    const dom_node& node,
    css_length specified,
    bool horizontal)
{
    if (has_display_none_ancestor(node)) {
        switch (specified.unit) {
        case length_unit::pixels:
        case length_unit::em:
        case length_unit::rem:
        case length_unit::viewport_width:
        case length_unit::viewport_height:
        case length_unit::viewport_width_capped:
        case length_unit::viewport_height_capped:
        case length_unit::viewport_width_floored:
        case length_unit::viewport_height_floored:
            // display:none suppresses box generation, not the computed value of
            // an independently resolvable declaration. Percentage and intrinsic
            // values still require a real formatting context and remain on the
            // normal used-value path below.
            return std::max(
                0.0F,
                resolve_connected_css_length(node, specified, 0.0F));
        default:
            break;
        }
    }
    const auto box = connected_box_for(node);
    if (node.style.border_box) {
        return horizontal ? node.layout.width : node.layout.height;
    }
    return horizontal ? box.content_width : box.content_height;
}

const dom_node* positioned_containing_node(const dom_node& node)
{
    if (node.style.position == position_mode::fixed) {
        auto* root = &node;
        while (root->parent != nullptr) root = root->parent;
        return root;
    }
    if (node.style.position == position_mode::absolute) {
        for (auto* ancestor = node.parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->style.position != position_mode::normal
                || ancestor->parent == nullptr) {
                return ancestor;
            }
        }
    }
    return node.parent;
}

bool try_connected_used_geometry_value(
    const dom_node& node,
    std::string_view canonical_name,
    float& value)
{
    const auto* containing_node = positioned_containing_node(node);
    if (containing_node == nullptr) return false;
    const auto containing = connected_box_for(*containing_node);
    const auto positioned = node.style.position == position_mode::absolute
        || node.style.position == position_mode::fixed;
    const auto inset_width = positioned
        ? containing.padding_width : containing.content_width;
    const auto inset_height = positioned
        ? containing.padding_height : containing.content_height;

    const css_length* inset = nullptr;
    float inset_reference = inset_width;
    if (canonical_name == "left" || canonical_name == "inset-inline-start") {
        inset = &node.style.left;
    } else if (canonical_name == "right" || canonical_name == "inset-inline-end") {
        inset = &node.style.right;
    } else if (canonical_name == "top" || canonical_name == "inset-block-start") {
        inset = &node.style.top;
        inset_reference = inset_height;
    } else if (canonical_name == "bottom" || canonical_name == "inset-block-end") {
        inset = &node.style.bottom;
        inset_reference = inset_height;
    }
    if (inset != nullptr) {
        if (inset->unit == length_unit::automatic) return false;
        value = resolve_connected_css_length(node, *inset, inset_reference);
        return true;
    }

    const auto horizontal_margin = canonical_name == "margin-left"
        || canonical_name == "margin-right";
    const auto vertical_margin = canonical_name == "margin-top"
        || canonical_name == "margin-bottom";
    if (!horizontal_margin && !vertical_margin) return false;
    const auto* margin = canonical_name == "margin-left" ? &node.style.margin_left
        : canonical_name == "margin-right" ? &node.style.margin_right
        : canonical_name == "margin-top" ? &node.style.margin_top
        : &node.style.margin_bottom;
    const auto automatic = canonical_name == "margin-left" ? node.style.margin_left_auto
        : canonical_name == "margin-right" ? node.style.margin_right_auto
        : canonical_name == "margin-top" ? node.style.margin_top_auto
        : node.style.margin_bottom_auto;
    if (!automatic) {
        // CSS percentage margins, including block-axis margins, resolve
        // against the containing block's inline size.
        value = resolve_connected_css_length(node, *margin, containing.content_width);
        return true;
    }
    if (!horizontal_margin) {
        value = 0.0F;
        return true;
    }
    const auto fixed_left = node.style.margin_left_auto
        ? 0.0F
        : resolve_connected_css_length(
            node, node.style.margin_left, containing.content_width);
    const auto fixed_right = node.style.margin_right_auto
        ? 0.0F
        : resolve_connected_css_length(
            node, node.style.margin_right, containing.content_width);
    const auto remaining = std::max(
        0.0F,
        containing.content_width - node.layout.width - fixed_left - fixed_right);
    value = node.style.margin_left_auto && node.style.margin_right_auto
        ? remaining * 0.5F : remaining;
    return true;
}

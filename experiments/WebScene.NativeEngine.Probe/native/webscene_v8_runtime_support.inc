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
    explicit binding_callback_timer(binding_callback_stats* stats) noexcept
        : stats_(stats)
    {
        if (stats_ != nullptr) started_ = std::chrono::steady_clock::now();
    }

    ~binding_callback_timer()
    {
        if (stats_ == nullptr) return;
        ++stats_->calls;
        stats_->nanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started_).count());
    }

private:
    binding_callback_stats* stats_;
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

enum inline_style_property : uint64_t {
    inline_width = 1ULL << 0U,
    inline_height = 1U << 1U,
    inline_left = 1U << 2U,
    inline_top = 1U << 3U,
    inline_right = 1U << 4U,
    inline_bottom = 1U << 5U,
    inline_display = 1U << 6U,
    inline_position = 1U << 7U,
    inline_flex_direction = 1U << 8U,
    inline_flex_grow = 1U << 9U,
    inline_background = 1U << 10U,
    inline_overflow = 1U << 11U,
    inline_color = 1U << 12U,
    inline_font_size = 1U << 13U,
    inline_font_family = 1U << 14U,
    inline_font_weight = 1U << 15U,
    inline_line_height = 1U << 16U,
    inline_text_align = 1U << 17U,
    inline_visibility = 1U << 18U,
    inline_pointer_events = 1U << 19U,
    inline_padding = 1U << 20U,
    inline_margin = 1U << 21U,
    inline_align_items = 1U << 22U,
    inline_opacity = 1U << 23U,
    inline_flex_wrap = 1U << 24U,
    inline_flex_shrink = 1U << 25U,
    inline_align_self = 1U << 26U,
    inline_min_width = 1U << 27U,
    inline_justify_content = 1U << 28U,
    inline_box_sizing = 1U << 29U,
    inline_border_radius = 1U << 30U,
    inline_transform = 1ULL << 31U,
    inline_white_space = 1ULL << 32U,
    inline_min_height = 1ULL << 33U,
    inline_max_width = 1ULL << 34U,
    inline_max_height = 1ULL << 35U,
    inline_gap = 1ULL << 36U,
    inline_box_shadow = 1ULL << 37U,
    inline_transform_origin = 1ULL << 38U,
    inline_svg_fill = 1ULL << 39U,
    inline_svg_stroke = 1ULL << 40U,
    inline_cursor = 1ULL << 41U,
    inline_letter_spacing = 1ULL << 42U,
    inline_word_spacing = 1ULL << 43U,
    inline_z_index = 1ULL << 44U,
    inline_float = 1ULL << 45U,
    inline_border = 1ULL << 46U,
    inline_flex_basis = 1ULL << 47U,
    inline_grid = 1ULL << 48U
};

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

display_mode default_display_for_tag(std::string_view tag)
{
    constexpr std::array<std::string_view, 8> non_rendered_tags{
        "base", "head", "link", "meta", "script", "style", "template", "title"};
    if (std::find(non_rendered_tags.begin(), non_rendered_tags.end(), tag)
        != non_rendered_tags.end()) {
        return display_mode::none;
    }
    if (tag == "table") return display_mode::table;
    if (tag == "thead") return display_mode::table_header_group;
    if (tag == "tbody") return display_mode::table_row_group;
    if (tag == "tfoot") return display_mode::table_footer_group;
    if (tag == "tr") return display_mode::table_row;
    if (tag == "td" || tag == "th") return display_mode::table_cell;
    if (tag == "colgroup") return display_mode::table_column_group;
    if (tag == "col") return display_mode::table_column;
    if (tag == "caption") return display_mode::table_caption;
    if (tag == "li") return display_mode::list_item;
    // Compact HTML UA defaults. Component libraries rely on phrasing elements (most
    // visibly the dialog submit-button span) remaining inline when no author
    // display declaration is present.
    constexpr std::array<std::string_view, 25> inline_tags{
        "a", "abbr", "b", "cite", "code", "em", "i", "kbd", "label",
        "mark", "q", "s", "samp", "small", "span", "strong", "sub", "sup",
        "svg", "time", "u", "var", "path", "use", "circle"};
    constexpr std::array<std::string_view, 6> inline_block_tags{
        "button", "img", "input", "rect", "select", "textarea"};
    if (std::find(inline_block_tags.begin(), inline_block_tags.end(), tag)
        != inline_block_tags.end()) {
        return display_mode::inline_block;
    }
    return std::find(inline_tags.begin(), inline_tags.end(), tag) != inline_tags.end()
        ? display_mode::inline_flow
        : display_mode::block;
}

display_mode parse_display_mode(std::string_view value)
{
    if (value == "flex") return display_mode::flex;
    if (value == "inline-flex") return display_mode::inline_flex;
    if (value == "grid") return display_mode::grid;
    if (value == "inline-grid") return display_mode::inline_grid;
    if (value == "table") return display_mode::table;
    if (value == "inline-table") return display_mode::inline_table;
    if (value == "table-row-group") return display_mode::table_row_group;
    if (value == "table-header-group") return display_mode::table_header_group;
    if (value == "table-footer-group") return display_mode::table_footer_group;
    if (value == "table-row") return display_mode::table_row;
    if (value == "table-cell") return display_mode::table_cell;
    if (value == "table-column-group") return display_mode::table_column_group;
    if (value == "table-column") return display_mode::table_column;
    if (value == "table-caption") return display_mode::table_caption;
    if (value == "contents") return display_mode::contents;
    if (value == "inline") return display_mode::inline_flow;
    if (value == "inline-block") return display_mode::inline_block;
    if (value == "list-item") return display_mode::list_item;
    if (value == "none") return display_mode::none;
    return display_mode::block;
}

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

struct parsed_flex_flow
{
    flex_direction direction{flex_direction::row};
    bool reverse{false};
    bool wrap{false};
};

parsed_flex_flow parse_flex_flow(std::string_view value)
{
    parsed_flex_flow result{};
    std::istringstream stream{std::string(value)};
    std::string token;
    while (stream >> token) {
        std::transform(
            token.begin(),
            token.end(),
            token.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (token == "row" || token == "row-reverse"
            || token == "column" || token == "column-reverse") {
            result.direction = token == "row" || token == "row-reverse"
                ? flex_direction::row : flex_direction::column;
            result.reverse = token == "row-reverse" || token == "column-reverse";
        } else if (token == "wrap" || token == "wrap-reverse") {
            result.wrap = true;
        } else if (token == "nowrap") {
            result.wrap = false;
        }
    }
    return result;
}

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

overflow_mode parse_overflow_mode(std::string_view value)
{
    if (value == "hidden") return overflow_mode::hidden;
    if (value == "clip") return overflow_mode::clip;
    if (value == "auto") return overflow_mode::automatic;
    if (value == "scroll") return overflow_mode::scroll;
    return overflow_mode::visible;
}

std::pair<overflow_mode, overflow_mode> computed_overflow(const node_style& style)
{
    auto x = style.overflow_x;
    auto y = style.overflow_y;
    const auto neither_visible_nor_clip = [](overflow_mode value) {
        return value != overflow_mode::visible && value != overflow_mode::clip;
    };
    if (x == overflow_mode::visible && neither_visible_nor_clip(y)) x = overflow_mode::automatic;
    else if (x == overflow_mode::clip && neither_visible_nor_clip(y)) x = overflow_mode::hidden;
    if (y == overflow_mode::visible && neither_visible_nor_clip(x)) y = overflow_mode::automatic;
    else if (y == overflow_mode::clip && neither_visible_nor_clip(x)) y = overflow_mode::hidden;
    return {x, y};
}

void refresh_overflow_state(node_style& style)
{
    const auto [x, y] = computed_overflow(style);
    style.clip = x != overflow_mode::visible || y != overflow_mode::visible;
    style.scroll_x_enabled = x == overflow_mode::automatic || x == overflow_mode::scroll;
    style.scroll_y_enabled = y == overflow_mode::automatic || y == overflow_mode::scroll;
}

display_mode default_display_for_node(const dom_node& node)
{
    if (node.kind != dom_node_kind::element) {
        return node.kind == dom_node_kind::text
            ? display_mode::inline_flow
            : display_mode::none;
    }
    if (node.tag == "input") {
        const auto type = node.attributes.find("type");
        if (type != node.attributes.end()) {
            auto value = type->second;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (value == "hidden") return display_mode::none;
        }
    }
    return default_display_for_tag(node.tag);
}

float inherited_font_size(const dom_node& node)
{
    for (auto* parent = node.parent; parent != nullptr; parent = parent->parent) {
        if (parent->style.font_size >= 0) return parent->style.font_size;
    }
    return 14.0F;
}

std::string resolved_font_family(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (!current->style.textual().font_family.empty()) {
            return current->style.textual().font_family;
        }
    }
    return "sans-serif";
}

int32_t resolved_font_weight(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.font_weight > 0) return current->style.font_weight;
    }
    return 400;
}

float resolved_letter_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.letter_spacing_specified) return current->style.letter_spacing;
    }
    return 0;
}

float resolved_word_spacing(const dom_node& node)
{
    for (auto* current = &node; current != nullptr; current = current->parent) {
        if (current->style.word_spacing_specified) return current->style.word_spacing;
    }
    return 0;
}

float resolved_declared_font_size(const dom_node& node, const std::string& value)
{
    if (value == "inherit" || value == "unset") return -1.0F;
    if (value == "initial" || value == "revert") return 14.0F;
    const auto parsed = native_document::parse_length(value);
    if (parsed.unit == length_unit::em) {
        return std::max(0.0F, parsed.value * inherited_font_size(node));
    }
    if (parsed.unit == length_unit::rem) {
        auto* root = &node;
        while (root->parent != nullptr) root = root->parent;
        const auto root_size = root->style.font_size >= 0 ? root->style.font_size : 14.0F;
        return std::max(0.0F, parsed.value * root_size);
    }
    if (parsed.unit == length_unit::percent) {
        return std::max(0.0F,
            parsed.value * inherited_font_size(node) / 100.0F + parsed.pixel_offset);
    }
    return std::max(0.0F, parsed.value);
}

float resolved_declared_line_height(
    const dom_node& node,
    const std::string& value,
    float font_size)
{
    if (value == "normal" || value == "inherit" || value == "unset") return -1.0F;
    if (value == "initial" || value == "revert") return -1.0F;
    const auto parsed = native_document::parse_length(value);
    if (parsed.unit == length_unit::em) {
        return std::max(0.0F, parsed.value * font_size);
    }
    if (parsed.unit == length_unit::rem) {
        auto* root = &node;
        while (root->parent != nullptr) root = root->parent;
        const auto root_size = root->style.font_size >= 0 ? root->style.font_size : 14.0F;
        return std::max(0.0F, parsed.value * root_size);
    }
    if (parsed.unit == length_unit::percent) {
        return std::max(0.0F,
            parsed.value * font_size / 100.0F + parsed.pixel_offset);
    }
    const auto parsed_value = std::max(0.0F, parsed.value);
    const auto has_explicit_unit = std::any_of(
        value.begin(),
        value.end(),
        [](unsigned char character) { return std::isalpha(character) || character == '%'; });
    return !has_explicit_unit && parsed_value <= 4.0F
        ? parsed_value * font_size
        : parsed_value;
}

struct parsed_font_shorthand final {
    float font_size{14.0F};
    float line_height{-1.0F};
    int32_t font_weight{400};
    std::string font_family;
    bool complete{true};
};

std::optional<parsed_font_shorthand> parse_font_shorthand(
    const dom_node& node,
    const std::string& value)
{
    const auto lowercase = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    };
    std::istringstream stream(value);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) tokens.push_back(std::move(token));
    if (tokens.empty()) return std::nullopt;

    const auto looks_like_size = [](std::string_view token) {
        if (token == "xx-small" || token == "x-small" || token == "small"
            || token == "medium" || token == "large" || token == "x-large"
            || token == "xx-large" || token == "smaller" || token == "larger") {
            return true;
        }
        const auto slash = token.find('/');
        if (slash != std::string_view::npos) token = token.substr(0, slash);
        return !token.empty()
            && (std::isdigit(static_cast<unsigned char>(token.front()))
                || token.front() == '.');
    };

    auto size_index = tokens.size();
    for (size_t index = 0; index < tokens.size(); ++index) {
        if (looks_like_size(tokens[index])) {
            size_index = index;
            break;
        }
    }
    if (size_index == tokens.size()) return std::nullopt;

    parsed_font_shorthand result;
    for (size_t index = 0; index < size_index; ++index) {
        const auto token = lowercase(tokens[index]);
        if (token == "bold" || token == "bolder") result.font_weight = 700;
        else if (token == "normal") continue;
        else if (token.size() == 3U && token.front() >= '1' && token.front() <= '9'
            && token[1] == '0' && token[2] == '0') {
            result.font_weight = std::atoi(token.c_str());
        } else {
            // Style, stretch, and variant tokens are accepted so their size,
            // line-height, weight, and family companions still take effect,
            // but those additional font axes are not represented natively.
            result.complete = false;
        }
    }

    auto size_token = tokens[size_index];
    std::string line_height_token;
    if (const auto slash = size_token.find('/'); slash != std::string::npos) {
        line_height_token = size_token.substr(slash + 1U);
        size_token.resize(slash);
    }
    const auto size_keyword = lowercase(size_token);
    if (size_keyword == "medium") result.font_size = 14.0F;
    else if (size_keyword == "small") result.font_size = 12.0F;
    else if (size_keyword == "large") result.font_size = 18.0F;
    else if (size_keyword == "x-small") result.font_size = 10.0F;
    else if (size_keyword == "xx-small") result.font_size = 9.0F;
    else if (size_keyword == "x-large") result.font_size = 24.0F;
    else if (size_keyword == "xx-large") result.font_size = 32.0F;
    else if (size_keyword == "smaller") result.font_size = inherited_font_size(node) * 0.8F;
    else if (size_keyword == "larger") result.font_size = inherited_font_size(node) * 1.2F;
    else result.font_size = resolved_declared_font_size(node, size_token);

    auto family_index = size_index + 1U;
    if (line_height_token.empty() && family_index < tokens.size()
        && tokens[family_index] == "/") {
        ++family_index;
        if (family_index < tokens.size()) line_height_token = tokens[family_index++];
    }
    if (!line_height_token.empty()) {
        result.line_height = resolved_declared_line_height(
            node,
            line_height_token,
            result.font_size);
    }
    for (auto index = family_index; index < tokens.size(); ++index) {
        if (!result.font_family.empty()) result.font_family.push_back(' ');
        result.font_family += tokens[index];
    }
    if (result.font_family.empty()) return std::nullopt;
    return result;
}

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
{
    if (name.starts_with("--")) return std::string(name);
    if (name == "cssFloat") return "float";
    std::string result;
    result.reserve(name.size() + 4U);
    for (const auto character : name) {
        if (std::isupper(static_cast<unsigned char>(character))) {
            result.push_back('-');
            result.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(character))));
        } else {
            result.push_back(character);
        }
    }
    return result;
}

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
    if (!name.starts_with("--") || name.size() <= 2U) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char character) {
        return character <= 0x20U || character == 0x7FU;
    });
}

bool has_multiple_grid_columns(std::string_view value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    value.remove_prefix(first);
    if (value.starts_with("repeat(")) {
        const auto count_start = value.find_first_of("0123456789", 7U);
        if (count_start != std::string_view::npos) {
            const auto count_end = value.find_first_not_of("0123456789", count_start);
            const auto count_text = value.substr(count_start, count_end - count_start);
            int count = 0;
            const auto result = std::from_chars(
                count_text.data(), count_text.data() + count_text.size(), count);
            if (result.ec == std::errc{} && count > 1) return true;
        }
    }

    size_t track_count = 0;
    size_t token_start = std::string_view::npos;
    int parenthesis_depth = 0;
    int bracket_depth = 0;
    auto finish_token = [&](size_t end) {
        if (token_start == std::string_view::npos) return;
        const auto token = value.substr(token_start, end - token_start);
        if (!token.empty() && token.front() != '[') ++track_count;
        token_start = std::string_view::npos;
    };
    for (size_t index = 0; index <= value.size(); ++index) {
        const auto character = index < value.size() ? value[index] : ' ';
        if (character == '(') ++parenthesis_depth;
        else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
        else if (character == '[') ++bracket_depth;
        else if (character == ']' && bracket_depth > 0) --bracket_depth;
        const auto separator = parenthesis_depth == 0 && bracket_depth == 0
            && std::isspace(static_cast<unsigned char>(character));
        if (!separator && token_start == std::string_view::npos) token_start = index;
        if (separator) finish_token(index);
    }
    return track_count > 1U;
}

std::vector<node_style::grid_data::track> parse_simple_grid_tracks(
    const std::string& value)
{
    using grid_track = node_style::grid_data::track;
    const auto trim = [](std::string_view input) {
        const auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return std::string_view{};
        return input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1U);
    };
    const auto parse_fraction = [&](std::string_view token) {
        token = trim(token);
        if (!token.ends_with("fr")) return 0.0F;
        token.remove_suffix(2U);
        if (token.empty()) return 1.0F;
        char* end = nullptr;
        const auto text = std::string(token);
        const auto result = std::strtof(text.c_str(), &end);
        return end != text.c_str() && *end == '\0' && result > 0 ? result : 0.0F;
    };
    const auto parse_track = [&](std::string_view raw) -> std::optional<grid_track> {
        const auto token = trim(raw);
        if (token.empty()) return std::nullopt;
        if (token == "auto" || token == "min-content" || token == "max-content") {
            grid_track track;
            track.kind = grid_track::sizing::automatic;
            return track;
        }
        if (const auto fraction = parse_fraction(token); fraction > 0) {
            grid_track track;
            track.kind = grid_track::sizing::fractional;
            track.fraction = fraction;
            return track;
        }
        if (token.starts_with("minmax(") && token.ends_with(')')) {
            const auto arguments = token.substr(7U, token.size() - 8U);
            auto comma = std::string_view::npos;
            auto depth = 0;
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (arguments[index] == '(') ++depth;
                else if (arguments[index] == ')') --depth;
                else if (arguments[index] == ',' && depth == 0) {
                    comma = index;
                    break;
                }
            }
            if (comma == std::string_view::npos) return std::nullopt;
            grid_track track;
            track.kind = grid_track::sizing::minmax;
            const auto minimum = trim(arguments.substr(0, comma));
            const auto maximum = trim(arguments.substr(comma + 1U));
            if (minimum != "auto" && minimum != "min-content"
                && minimum != "max-content") {
                track.minimum = native_document::parse_length(std::string(minimum));
            }
            track.fraction = parse_fraction(maximum);
            if (track.fraction <= 0 && maximum != "auto"
                && maximum != "min-content" && maximum != "max-content") {
                track.maximum = native_document::parse_length(std::string(maximum));
            }
            return track;
        }
        grid_track track;
        track.kind = grid_track::sizing::fixed;
        track.minimum = native_document::parse_length(std::string(token));
        track.maximum = track.minimum;
        return track;
    };

    std::vector<std::string_view> tokens;
    size_t token_start = std::string_view::npos;
    auto parenthesis_depth = 0;
    auto bracket_depth = 0;
    for (size_t index = 0; index <= value.size(); ++index) {
        const auto character = index < value.size() ? value[index] : ' ';
        if (character == '(') ++parenthesis_depth;
        else if (character == ')' && parenthesis_depth > 0) --parenthesis_depth;
        else if (character == '[') ++bracket_depth;
        else if (character == ']' && bracket_depth > 0) --bracket_depth;
        const auto separator = parenthesis_depth == 0 && bracket_depth == 0
            && std::isspace(static_cast<unsigned char>(character));
        if (!separator && token_start == std::string_view::npos) token_start = index;
        if (separator && token_start != std::string_view::npos) {
            tokens.push_back(std::string_view(value).substr(token_start, index - token_start));
            token_start = std::string_view::npos;
        }
    }

    std::vector<grid_track> tracks;
    for (auto token : tokens) {
        token = trim(token);
        if (token.empty() || token.front() == '[') continue;
        if (token == "none" || token.starts_with("repeat(")) return {};
        const auto parsed = parse_track(token);
        if (!parsed.has_value()) return {};
        tracks.push_back(*parsed);
    }
    return tracks;
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
        const auto idle_tasks = std::getenv("WEBSCENE_V8_IDLE_TASKS") != nullptr
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
    const auto root_font_size = root->style.font_size >= 0
        ? root->style.font_size : 14.0F;
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
        return root->layout.width * length.value / 100.0F;
    case length_unit::viewport_height:
        return root->layout.height * length.value / 100.0F;
    case length_unit::automatic:
    case length_unit::max_content:
    case length_unit::min_content:
    case length_unit::fit_content:
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

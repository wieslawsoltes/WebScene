#include "webscene_css_parser.h"

#include "webscene_css_parser_ffi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace webscene_native {
namespace {

std::string_view borrow_slice(webscene_css_byte_slice value)
{
    if (value.length == 0U || value.data == nullptr) return {};
    return {reinterpret_cast<const char*>(value.data), value.length};
}

void ascii_lower(std::string& value)
{
    for (auto& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
}

webscene_css_byte_slice borrow(std::string_view input)
{
    return {reinterpret_cast<const uint8_t*>(input.data()), input.size()};
}

enum class cached_event_kind : uint8_t { begin_rule, declaration, end_rule };

struct cached_event final {
    cached_event_kind kind{};
    uint32_t rule_kind{};
    bool has_block{};
    size_t rule_index{css_syntax_no_parent};
    size_t parent_index{css_syntax_no_parent};
    size_t declaration_count{};
    bool important{};
    uint32_t line{};
    uint32_t column{};
    std::string name;
    std::string value;
};

struct cached_stream final {
    std::vector<cached_event> events;
    css_syntax_metrics metrics;
};

struct syntax_cache_entry final {
    std::shared_ptr<const cached_stream> stream;
    std::list<std::string>::iterator lru;
};

constexpr size_t maximum_process_cache_entries = 256U;
std::mutex syntax_cache_mutex;
std::unordered_map<std::string, syntax_cache_entry> syntax_cache;
std::list<std::string> syntax_cache_lru;
std::string syntax_cache_directory;
std::atomic<uint64_t> process_cache_hits{0U};
std::atomic<uint64_t> persistent_cache_hits{0U};
std::atomic<uint64_t> compilation_count{0U};

uint64_t content_hash(uint8_t kind, std::string_view input)
{
    uint64_t hash = 1469598103934665603ULL;
    hash ^= kind;
    hash *= 1099511628211ULL;
    for (const auto character : input) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<uint64_t>(input.size());
    hash *= 1099511628211ULL;
    return hash;
}

std::string process_key(uint8_t kind, std::string_view input)
{
    std::string key;
    key.reserve(input.size() + 2U);
    key.push_back(static_cast<char>(kind));
    key.push_back('\0');
    key.append(input);
    return key;
}

std::filesystem::path persistent_path(uint8_t kind, std::string_view input)
{
    std::lock_guard lock(syntax_cache_mutex);
    if (syntax_cache_directory.empty()) return {};
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << content_hash(kind, input);
    return std::filesystem::path(syntax_cache_directory) / "css" / (name.str() + ".wscssc");
}

template<typename T>
bool write_scalar(std::ostream& output, const T& value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return bool(output);
}

template<typename T>
bool read_scalar(std::istream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return bool(input);
}

bool write_string(std::ostream& output, std::string_view value)
{
    const auto size = static_cast<uint64_t>(value.size());
    if (!write_scalar(output, size)) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return bool(output);
}

bool read_string(std::istream& input, std::string& value)
{
    uint64_t size{};
    if (!read_scalar(input, size) || size > (64ULL << 20U)) return false;
    value.resize(static_cast<size_t>(size));
    if (!value.empty()) input.read(value.data(), static_cast<std::streamsize>(value.size()));
    return bool(input);
}

void remember_process_cache(std::string key, std::shared_ptr<const cached_stream> stream)
{
    std::lock_guard lock(syntax_cache_mutex);
    if (auto found = syntax_cache.find(key); found != syntax_cache.end()) {
        syntax_cache_lru.erase(found->second.lru);
        syntax_cache_lru.push_front(key);
        found->second = {std::move(stream), syntax_cache_lru.begin()};
        return;
    }
    syntax_cache_lru.push_front(key);
    syntax_cache.emplace(syntax_cache_lru.front(), syntax_cache_entry{std::move(stream), syntax_cache_lru.begin()});
    while (syntax_cache.size() > maximum_process_cache_entries) {
        auto old = syntax_cache_lru.back();
        syntax_cache_lru.pop_back();
        syntax_cache.erase(old);
    }
}

std::shared_ptr<const cached_stream> find_process_cache(const std::string& key)
{
    std::lock_guard lock(syntax_cache_mutex);
    auto found = syntax_cache.find(key);
    if (found == syntax_cache.end()) return {};
    syntax_cache_lru.erase(found->second.lru);
    syntax_cache_lru.push_front(key);
    found->second.lru = syntax_cache_lru.begin();
    process_cache_hits.fetch_add(1U, std::memory_order_relaxed);
    return found->second.stream;
}

bool write_persistent_cache(uint8_t kind, std::string_view input, const cached_stream& stream)
{
    const auto path = persistent_path(kind, input);
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    constexpr char magic[8] = {'W','S','C','S','S','C','0','1'};
    output.write(magic, sizeof(magic));
    const uint32_t schema = 1U;
    const auto hash = content_hash(kind, input);
    const auto input_size = static_cast<uint64_t>(input.size());
    const auto event_count = static_cast<uint64_t>(stream.events.size());
    if (!write_scalar(output, schema) || !write_scalar(output, kind)
        || !write_scalar(output, hash) || !write_scalar(output, input_size)
        || !write_scalar(output, stream.metrics.parse_error_count)
        || !write_scalar(output, stream.metrics.first_error_line)
        || !write_scalar(output, stream.metrics.first_error_column)
        || !write_scalar(output, event_count)) return false;
    for (const auto& event : stream.events) {
        const auto event_kind = static_cast<uint8_t>(event.kind);
        const auto rule_index = static_cast<uint64_t>(event.rule_index);
        const auto parent_index = event.parent_index == css_syntax_no_parent
            ? UINT64_MAX : static_cast<uint64_t>(event.parent_index);
        const auto declaration_count = static_cast<uint64_t>(event.declaration_count);
        if (!write_scalar(output, event_kind) || !write_scalar(output, event.rule_kind)
            || !write_scalar(output, event.has_block) || !write_scalar(output, rule_index)
            || !write_scalar(output, parent_index) || !write_scalar(output, declaration_count)
            || !write_scalar(output, event.important) || !write_scalar(output, event.line)
            || !write_scalar(output, event.column) || !write_string(output, event.name)
            || !write_string(output, event.value)) return false;
    }
    output.close();
    if (!output) return false;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::shared_ptr<const cached_stream> read_persistent_cache(uint8_t kind, std::string_view input)
{
    const auto path = persistent_path(kind, input);
    if (path.empty()) return {};
    std::ifstream source(path, std::ios::binary);
    if (!source) return {};
    char magic[8]{};
    source.read(magic, sizeof(magic));
    constexpr char expected[8] = {'W','S','C','S','S','C','0','1'};
    if (!source || !std::equal(std::begin(magic), std::end(magic), std::begin(expected))) return {};
    uint32_t schema{};
    uint8_t stored_kind{};
    uint64_t stored_hash{}, input_size{}, event_count{};
    auto result = std::make_shared<cached_stream>();
    if (!read_scalar(source, schema) || !read_scalar(source, stored_kind)
        || !read_scalar(source, stored_hash) || !read_scalar(source, input_size)
        || !read_scalar(source, result->metrics.parse_error_count)
        || !read_scalar(source, result->metrics.first_error_line)
        || !read_scalar(source, result->metrics.first_error_column)
        || !read_scalar(source, event_count)
        || schema != 1U || stored_kind != kind || stored_hash != content_hash(kind, input)
        || input_size != input.size() || event_count > 1000000ULL) return {};
    result->events.reserve(static_cast<size_t>(event_count));
    for (uint64_t index = 0; index < event_count; ++index) {
        cached_event event;
        uint8_t event_kind{};
        uint64_t rule_index{}, parent_index{}, declaration_count{};
        if (!read_scalar(source, event_kind) || !read_scalar(source, event.rule_kind)
            || !read_scalar(source, event.has_block) || !read_scalar(source, rule_index)
            || !read_scalar(source, parent_index) || !read_scalar(source, declaration_count)
            || !read_scalar(source, event.important) || !read_scalar(source, event.line)
            || !read_scalar(source, event.column) || !read_string(source, event.name)
            || !read_string(source, event.value) || event_kind > 2U) return {};
        event.kind = static_cast<cached_event_kind>(event_kind);
        event.rule_index = static_cast<size_t>(rule_index);
        event.parent_index = parent_index == UINT64_MAX
            ? css_syntax_no_parent : static_cast<size_t>(parent_index);
        event.declaration_count = static_cast<size_t>(declaration_count);
        result->events.push_back(std::move(event));
    }
    persistent_cache_hits.fetch_add(1U, std::memory_order_relaxed);
    return result;
}

class recording_sink final : public css_syntax_sink {
public:
    explicit recording_sink(cached_stream& output) : output_(output) {}

    bool begin_rule(uint32_t kind, bool has_block, size_t parent_index,
        std::string_view name, std::string_view prelude, size_t& rule_index) override
    {
        return located_begin_rule(kind, has_block, parent_index, name, prelude, rule_index, 0U, 0U);
    }

    bool located_begin_rule(uint32_t kind, bool has_block, size_t parent_index,
        std::string_view name, std::string_view prelude, size_t& rule_index,
        uint32_t line, uint32_t column) override
    {
        rule_index = next_rule_index_++;
        output_.events.push_back({cached_event_kind::begin_rule, kind, has_block,
            rule_index, parent_index, 0U, false, line, column,
            std::string(name), std::string(prelude)});
        stack_.push_back(rule_index);
        return true;
    }

    bool declaration(std::string_view name, std::string_view value, bool important) override
    {
        return located_declaration(name, value, important, 0U, 0U);
    }

    bool located_declaration(std::string_view name, std::string_view value,
        bool important, uint32_t line, uint32_t column) override
    {
        const auto owner = stack_.empty() ? css_syntax_no_parent : stack_.back();
        output_.events.push_back({cached_event_kind::declaration, 0U, false,
            owner, css_syntax_no_parent, 0U, important, line, column,
            std::string(name), std::string(value)});
        return true;
    }

    bool end_rule(size_t rule_index, size_t declaration_count) override
    {
        output_.events.push_back({cached_event_kind::end_rule, 0U, false,
            rule_index, css_syntax_no_parent, declaration_count});
        if (!stack_.empty() && stack_.back() == rule_index) stack_.pop_back();
        return true;
    }

private:
    cached_stream& output_;
    size_t next_rule_index_{};
    std::vector<size_t> stack_;
};

struct stream_context final {
    css_syntax_sink* sink;
    uint64_t rule_count{0};
    uint64_t declaration_count{0};
};

uint8_t begin_rule(void* opaque, uint32_t kind, uint8_t has_block,
    size_t parent_index, webscene_css_byte_slice name, webscene_css_byte_slice prelude,
    size_t* rule_index, uint32_t line, uint32_t column)
{
    if (opaque == nullptr || rule_index == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        const auto accepted = context.sink->located_begin_rule(kind, has_block != 0U,
            parent_index, borrow_slice(name), borrow_slice(prelude), *rule_index, line, column);
        if (accepted) ++context.rule_count;
        return accepted ? 1U : 0U;
    } catch (...) { return 0U; }
}

uint8_t declaration(void* opaque, webscene_css_byte_slice name,
    webscene_css_byte_slice value, uint8_t important, uint32_t line, uint32_t column)
{
    if (opaque == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        const auto accepted = context.sink->located_declaration(
            borrow_slice(name), borrow_slice(value), important != 0U, line, column);
        if (accepted) ++context.declaration_count;
        return accepted ? 1U : 0U;
    } catch (...) { return 0U; }
}

uint8_t end_rule(void* opaque, size_t rule_index, size_t declaration_count)
{
    if (opaque == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        return context.sink->end_rule(rule_index, declaration_count) ? 1U : 0U;
    } catch (...) { return 0U; }
}

css_syntax_parse_result replay(const cached_stream& cached, css_syntax_sink& consumer)
{
    const auto started = std::chrono::steady_clock::now();
    std::unordered_map<size_t, size_t> rule_indices;
    std::unordered_map<size_t, bool> accepted;
    for (const auto& event : cached.events) {
        if (event.kind == cached_event_kind::begin_rule) {
            const auto parent_accepted = event.parent_index == css_syntax_no_parent
                || (accepted.contains(event.parent_index) && accepted[event.parent_index]);
            if (!parent_accepted) {
                accepted[event.rule_index] = false;
                continue;
            }
            auto parent = css_syntax_no_parent;
            if (event.parent_index != css_syntax_no_parent) {
                const auto known = rule_indices.find(event.parent_index);
                if (known == rule_indices.end()) {
                    accepted[event.rule_index] = false;
                    continue;
                }
                parent = known->second;
            }
            size_t destination_index{};
            const auto ok = consumer.located_begin_rule(event.rule_kind, event.has_block,
                parent, event.name, event.value, destination_index, event.line, event.column);
            accepted[event.rule_index] = ok;
            if (ok) rule_indices[event.rule_index] = destination_index;
        } else if (event.kind == cached_event_kind::declaration) {
            if (event.rule_index != css_syntax_no_parent
                && (!accepted.contains(event.rule_index) || !accepted[event.rule_index])) continue;
            if (!consumer.located_declaration(event.name, event.value, event.important,
                    event.line, event.column)) {
                return {{}, "cssparser cached sink callback failed"};
            }
        } else {
            if (!accepted.contains(event.rule_index) || !accepted[event.rule_index]) continue;
            const auto known = rule_indices.find(event.rule_index);
            if (known == rule_indices.end()
                || !consumer.end_rule(known->second, event.declaration_count)) {
                return {{}, "cssparser cached sink callback failed"};
            }
        }
    }
    auto metrics = cached.metrics;
    metrics.duration_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
    metrics.parser_allocation_count = 0U;
    metrics.parser_peak_bytes = 0U;
    metrics.parser_retained_bytes = 0U;
    metrics.compilation_cache_hit = true;
    return {metrics, {}};
}

template <typename Parse>
css_syntax_parse_result stream_parse(std::string_view input, css_syntax_sink& consumer,
    Parse parse_native, uint8_t cache_kind)
{
    if (webscene_css_stream_abi_version() != 2U)
        return {{}, "cssparser streaming ABI version mismatch"};

    const auto key = process_key(cache_kind, input);
    if (auto cached = find_process_cache(key)) return replay(*cached, consumer);
    if (auto cached = read_persistent_cache(cache_kind, input)) {
        remember_process_cache(key, cached);
        return replay(*cached, consumer);
    }

    auto compiled = std::make_shared<cached_stream>();
    recording_sink recorder(*compiled);
    constexpr webscene_css_sink_vtable callbacks{begin_rule, declaration, end_rule};
    stream_context context{&recorder};
    const auto started = std::chrono::steady_clock::now();
    const auto parsed = parse_native(borrow(input), &callbacks, &context);
    const auto finished = std::chrono::steady_clock::now();
    compiled->metrics.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    compiled->metrics.parse_error_count = parsed.parse_error_count;
    compiled->metrics.first_error_line = parsed.first_error_line;
    compiled->metrics.first_error_column = parsed.first_error_column;
    compiled->metrics.parser_allocation_count = parsed.rust_allocation_count;
    compiled->metrics.parser_peak_bytes = parsed.rust_peak_bytes;
    compiled->metrics.parser_retained_bytes = parsed.rust_retained_bytes;
    compilation_count.fetch_add(1U, std::memory_order_relaxed);

    if (parsed.status != 0U) {
        return {compiled->metrics, parsed.status == 1U
            ? "cssparser rejected invalid UTF-8 or arguments"
            : parsed.status == 3U ? "cssparser panicked"
            : parsed.status == 2U ? "cssparser sink callback failed"
            : "cssparser failed"};
    }
    if (parsed.rule_count != context.rule_count
        || parsed.declaration_count != context.declaration_count) {
        return {compiled->metrics, "cssparser streaming result count mismatch"};
    }

    remember_process_cache(key, compiled);
    write_persistent_cache(cache_kind, input, *compiled);
    // The first consumer is intentionally fed from the same compiled event stream
    // that warm loads use, so cold/runtime and cached/build-time paths cannot drift.
    return replay(*compiled, consumer);
}

class collecting_sink final : public css_syntax_sink {
public:
    collecting_sink(css_syntax_output& output, size_t input_size) : output_(output)
    {
        output_.rules.reserve(input_size / 128U);
        output_.declarations.reserve(input_size / 64U);
    }

    bool begin_rule(uint32_t kind, bool has_block, size_t parent_index,
        std::string_view name, std::string_view prelude, size_t& rule_index) override
    {
        auto copied_name = std::string(name);
        if (kind == css_syntax_at_rule) ascii_lower(copied_name);
        rule_index = output_.rules.size();
        output_.rules.push_back({kind, has_block, parent_index, std::move(copied_name),
            std::string(prelude), output_.declarations.size(), 0U});
        return true;
    }

    bool located_begin_rule(uint32_t kind, bool has_block, size_t parent_index,
        std::string_view name, std::string_view prelude, size_t& rule_index,
        uint32_t line, uint32_t column) override
    {
        begin_rule(kind, has_block, parent_index, name, prelude, rule_index);
        output_.rules.back().source_line = line;
        output_.rules.back().source_column = column;
        return true;
    }

    bool declaration(std::string_view name, std::string_view value, bool important) override
    {
        auto copied_name = std::string(name);
        if (!copied_name.starts_with("--")) ascii_lower(copied_name);
        output_.declarations.push_back({std::move(copied_name), std::string(value), important});
        return true;
    }

    bool located_declaration(std::string_view name, std::string_view value,
        bool important, uint32_t line, uint32_t column) override
    {
        declaration(name, value, important);
        output_.declarations.back().source_line = line;
        output_.declarations.back().source_column = column;
        return true;
    }

    bool end_rule(size_t rule_index, size_t declaration_count) override
    {
        if (rule_index >= output_.rules.size()) return false;
        output_.rules[rule_index].declaration_count = declaration_count;
        return true;
    }
private:
    css_syntax_output& output_;
};

template <typename Stream>
css_syntax_output collect(std::string_view input, Stream stream)
{
    css_syntax_output output;
    try {
        collecting_sink sink(output, input.size());
        auto result = stream(input, sink);
        output.metrics = result.metrics;
        output.error = std::move(result.error);
        if (!output.error.empty()) {
            output.rules.clear();
            output.declarations.clear();
        }
    } catch (...) {
        output.rules.clear();
        output.declarations.clear();
        output.error = "cssparser output allocation failed";
    }
    return output;
}

} // namespace

void set_css_syntax_compilation_cache_directory(std::string directory)
{
    std::lock_guard lock(syntax_cache_mutex);
    syntax_cache_directory = std::move(directory);
}

void clear_css_syntax_process_cache()
{
    std::lock_guard lock(syntax_cache_mutex);
    syntax_cache.clear();
    syntax_cache_lru.clear();
}

uint64_t css_syntax_process_cache_hits() noexcept
{ return process_cache_hits.load(std::memory_order_relaxed); }
uint64_t css_syntax_persistent_cache_hits() noexcept
{ return persistent_cache_hits.load(std::memory_order_relaxed); }
uint64_t css_syntax_compilation_count() noexcept
{ return compilation_count.load(std::memory_order_relaxed); }

css_syntax_parse_result stream_css_syntax_stylesheet(std::string_view input, css_syntax_sink& sink)
{
    return stream_parse(input, sink, webscene_css_stream_stylesheet, 1U);
}

css_syntax_parse_result stream_css_syntax_declarations(std::string_view input, css_syntax_sink& sink)
{
    return stream_parse(input, sink, webscene_css_stream_declarations, 2U);
}

css_syntax_output parse_css_syntax_stylesheet(std::string_view input)
{ return collect(input, stream_css_syntax_stylesheet); }

css_syntax_output parse_css_syntax_declarations(std::string_view input)
{ return collect(input, stream_css_syntax_declarations); }

} // namespace webscene_native

#include "webscene_css_parser.h"

#include "webscene_css_parser_ffi.h"

#include <chrono>
#include <utility>

namespace webscene_native {
namespace {

std::string_view borrow_slice(webscene_css_byte_slice value)
{
    if (value.length == 0U) return {};
    if (value.data == nullptr) return {};
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
    return {
        reinterpret_cast<const uint8_t*>(input.data()),
        input.size()};
}

struct stream_context final {
    css_syntax_sink* sink;
    uint64_t rule_count{0};
    uint64_t declaration_count{0};
};

uint8_t begin_rule(
    void* opaque,
    uint32_t kind,
    uint8_t has_block,
    size_t parent_index,
    webscene_css_byte_slice name,
    webscene_css_byte_slice prelude,
    size_t* rule_index)
{
    if (opaque == nullptr || rule_index == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        const auto accepted = context.sink->begin_rule(
            kind,
            has_block != 0U,
            parent_index,
            borrow_slice(name),
            borrow_slice(prelude),
            *rule_index);
        if (accepted) ++context.rule_count;
        return accepted ? 1U : 0U;
    } catch (...) {
        return 0U;
    }
}

uint8_t declaration(
    void* opaque,
    webscene_css_byte_slice name,
    webscene_css_byte_slice value,
    uint8_t important)
{
    if (opaque == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        const auto accepted = context.sink->declaration(
            borrow_slice(name),
            borrow_slice(value),
            important != 0U);
        if (accepted) ++context.declaration_count;
        return accepted ? 1U : 0U;
    } catch (...) {
        return 0U;
    }
}

uint8_t end_rule(void* opaque, size_t rule_index, size_t declaration_count)
{
    if (opaque == nullptr) return 0U;
    try {
        auto& context = *static_cast<stream_context*>(opaque);
        return context.sink->end_rule(rule_index, declaration_count) ? 1U : 0U;
    } catch (...) {
        return 0U;
    }
}

template <typename Parse>
css_syntax_parse_result stream_parse(
    std::string_view input,
    css_syntax_sink& consumer,
    Parse parse_native)
{
    css_syntax_parse_result output;
    if (webscene_css_stream_abi_version() != 1U) {
        output.error = "cssparser streaming ABI version mismatch";
        return output;
    }

    constexpr webscene_css_sink_vtable callbacks{
        begin_rule, declaration, end_rule};
    stream_context context{&consumer};
    const auto started = std::chrono::steady_clock::now();
    const auto parsed = parse_native(borrow(input), &callbacks, &context);
    const auto finished = std::chrono::steady_clock::now();
    output.metrics.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    output.metrics.parse_error_count = parsed.parse_error_count;
    output.metrics.parser_allocation_count = parsed.rust_allocation_count;
    output.metrics.parser_peak_bytes = parsed.rust_peak_bytes;
    output.metrics.parser_retained_bytes = parsed.rust_retained_bytes;

    if (parsed.status != 0U) {
        output.error = parsed.status == 1U
            ? "cssparser rejected invalid UTF-8 or arguments"
            : parsed.status == 3U
                ? "cssparser panicked"
                : parsed.status == 2U
                    ? "cssparser sink callback failed"
                    : "cssparser failed";
        return output;
    }
    if (parsed.rule_count != context.rule_count
        || parsed.declaration_count != context.declaration_count) {
        output.error = "cssparser streaming result count mismatch";
    }
    return output;
}

class collecting_sink final : public css_syntax_sink {
public:
    collecting_sink(css_syntax_output& output, size_t input_size)
        : output_(output)
    {
        output_.rules.reserve(input_size / 128U);
        output_.declarations.reserve(input_size / 64U);
    }

    bool begin_rule(
        uint32_t kind,
        bool has_block,
        size_t parent_index,
        std::string_view name,
        std::string_view prelude,
        size_t& rule_index) override
    {
        auto copied_name = std::string(name);
        if (kind == css_syntax_at_rule) ascii_lower(copied_name);
        rule_index = output_.rules.size();
        output_.rules.push_back({
            kind,
            has_block,
            parent_index,
            std::move(copied_name),
            std::string(prelude),
            output_.declarations.size(),
            0U});
        return true;
    }

    bool declaration(
        std::string_view name,
        std::string_view value,
        bool important) override
    {
        auto copied_name = std::string(name);
        if (!copied_name.starts_with("--")) ascii_lower(copied_name);
        output_.declarations.push_back({
            std::move(copied_name), std::string(value), important});
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

css_syntax_parse_result stream_css_syntax_stylesheet(
    std::string_view input,
    css_syntax_sink& sink)
{
    return stream_parse(input, sink, webscene_css_stream_stylesheet);
}

css_syntax_parse_result stream_css_syntax_declarations(
    std::string_view input,
    css_syntax_sink& sink)
{
    return stream_parse(input, sink, webscene_css_stream_declarations);
}

css_syntax_output parse_css_syntax_stylesheet(std::string_view input)
{
    return collect(input, stream_css_syntax_stylesheet);
}

css_syntax_output parse_css_syntax_declarations(std::string_view input)
{
    return collect(input, stream_css_syntax_declarations);
}

} // namespace webscene_native

#include "webscene_css_parser.h"

#include "webscene_css_parser_ffi.h"

#include <chrono>
#include <utility>

namespace webscene_native {
namespace {

std::string copy_slice(webscene_css_byte_slice value)
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
    css_syntax_output* output;
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
        auto& output = *static_cast<stream_context*>(opaque)->output;
        auto copied_name = copy_slice(name);
        if (kind == css_syntax_at_rule) ascii_lower(copied_name);
        *rule_index = output.rules.size();
        output.rules.push_back({
            kind,
            has_block != 0U,
            parent_index,
            std::move(copied_name),
            copy_slice(prelude),
            output.declarations.size(),
            0U});
        return 1U;
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
        auto& output = *static_cast<stream_context*>(opaque)->output;
        auto copied_name = copy_slice(name);
        if (!copied_name.starts_with("--")) ascii_lower(copied_name);
        output.declarations.push_back({
            std::move(copied_name), copy_slice(value), important != 0U});
        return 1U;
    } catch (...) {
        return 0U;
    }
}

uint8_t end_rule(void* opaque, size_t rule_index, size_t declaration_count)
{
    if (opaque == nullptr) return 0U;
    auto& output = *static_cast<stream_context*>(opaque)->output;
    if (rule_index >= output.rules.size()) return 0U;
    auto& rule = output.rules[rule_index];
    rule.declaration_count = declaration_count;
    return 1U;
}

template <typename Parse>
css_syntax_output parse(std::string_view input, Parse parse_native)
{
    css_syntax_output output;
    if (webscene_css_stream_abi_version() != 1U) {
        output.error = "cssparser streaming ABI version mismatch";
        return output;
    }

    constexpr webscene_css_sink_vtable sink{
        begin_rule, declaration, end_rule};
    try {
        output.rules.reserve(input.size() / 128U);
        output.declarations.reserve(input.size() / 64U);
    } catch (...) {
        output.error = "cssparser output allocation failed";
        return output;
    }
    stream_context context{&output};
    const auto started = std::chrono::steady_clock::now();
    const auto parsed = parse_native(borrow(input), &sink, &context);
    const auto finished = std::chrono::steady_clock::now();
    output.metrics.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    output.metrics.parse_error_count = parsed.parse_error_count;
    output.metrics.parser_allocation_count = parsed.rust_allocation_count;
    output.metrics.parser_peak_bytes = parsed.rust_peak_bytes;
    output.metrics.parser_retained_bytes = parsed.rust_retained_bytes;

    if (parsed.status != 0U) {
        output.rules.clear();
        output.declarations.clear();
        output.error = parsed.status == 1U
            ? "cssparser rejected invalid UTF-8 or arguments"
            : parsed.status == 3U
                ? "cssparser panicked"
                : parsed.status == 2U
                    ? "cssparser sink callback failed"
                    : "cssparser failed";
        return output;
    }
    if (parsed.rule_count != output.rules.size()
        || parsed.declaration_count != output.declarations.size()) {
        output.error = "cssparser streaming result count mismatch";
        output.rules.clear();
        output.declarations.clear();
    }
    return output;
}

} // namespace

css_syntax_output parse_css_syntax_stylesheet(std::string_view input)
{
    return parse(input, webscene_css_stream_stylesheet);
}

css_syntax_output parse_css_syntax_declarations(std::string_view input)
{
    return parse(input, webscene_css_stream_declarations);
}

} // namespace webscene_native

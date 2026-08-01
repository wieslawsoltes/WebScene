#include "webscene_css_parser.h"

#include "webscene_css_parser_ffi.h"

#include <chrono>
#include <limits>
#include <utility>

namespace webscene_native {
namespace {

std::string copy_slice(webscene_css_byte_slice value)
{
    if (value.length == 0U) return {};
    if (value.data == nullptr) return {};
    return {reinterpret_cast<const char*>(value.data), value.length};
}

webscene_css_byte_slice borrow(std::string_view input)
{
    return {
        reinterpret_cast<const uint8_t*>(input.data()),
        input.size()};
}

template <typename Parse>
css_syntax_output parse(std::string_view input, Parse&& parse_native)
{
    css_syntax_output output;
    if (webscene_css_parser_abi_version() != 1U) {
        output.error = "cssparser ABI version mismatch";
        return output;
    }

    const auto started = std::chrono::steady_clock::now();
    auto parsed = parse_native(borrow(input));
    const auto finished = std::chrono::steady_clock::now();
    output.metrics.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    output.metrics.parse_error_count = parsed.parse_error_count;
    output.metrics.parser_allocation_count = parsed.rust_allocation_count;
    output.metrics.parser_peak_bytes = parsed.rust_peak_bytes;
    output.metrics.parser_retained_bytes = parsed.rust_retained_bytes;

    struct handle_guard final {
        void* handle;
        ~handle_guard() { webscene_css_free(handle); }
    } guard{parsed.handle};

    if (parsed.status != 0U || parsed.handle == nullptr) {
        output.error = parsed.status == 1U
            ? "cssparser rejected invalid UTF-8 or arguments"
            : parsed.status == 3U
                ? "cssparser panicked"
                : "cssparser failed";
        return output;
    }
    if (parsed.rule_count > std::numeric_limits<size_t>::max()
        || parsed.declaration_count > std::numeric_limits<size_t>::max()) {
        output.error = "cssparser result exceeds native address space";
        return output;
    }

    output.rules.reserve(static_cast<size_t>(parsed.rule_count));
    for (size_t index = 0; index < static_cast<size_t>(parsed.rule_count); ++index) {
        webscene_css_rule_view view{};
        if (webscene_css_rule_at(parsed.handle, index, &view) == 0U) {
            output.error = "cssparser returned an invalid rule view";
            return output;
        }
        output.rules.push_back({
            view.kind,
            view.has_block != 0U,
            view.parent_index,
            copy_slice(view.name),
            copy_slice(view.prelude),
            view.first_declaration,
            view.declaration_count});
    }

    output.declarations.reserve(static_cast<size_t>(parsed.declaration_count));
    for (size_t index = 0; index < static_cast<size_t>(parsed.declaration_count); ++index) {
        webscene_css_declaration_view view{};
        if (webscene_css_declaration_at(parsed.handle, index, &view) == 0U) {
            output.error = "cssparser returned an invalid declaration view";
            return output;
        }
        output.declarations.push_back({
            copy_slice(view.name),
            copy_slice(view.value),
            view.important != 0U});
    }
    return output;
}

} // namespace

css_syntax_output parse_css_syntax_stylesheet(std::string_view input)
{
    return parse(input, webscene_css_parse_stylesheet);
}

css_syntax_output parse_css_syntax_declarations(std::string_view input)
{
    return parse(input, webscene_css_parse_declarations);
}

} // namespace webscene_native

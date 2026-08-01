#include "webscene_selector_parser.h"

#include "webscene_selector_parser_ffi.h"

#include <chrono>
#include <limits>
#include <utility>

namespace webscene_native {
namespace {

std::string copy_slice(webscene_selector_byte_slice value)
{
    if (value.length == 0U || value.data == nullptr) return {};
    return {reinterpret_cast<const char*>(value.data), value.length};
}

webscene_selector_byte_slice borrow(std::string_view input)
{
    return {
        reinterpret_cast<const uint8_t*>(input.data()),
        input.size()};
}

} // namespace

selector_syntax_output parse_selector_syntax(std::string_view input)
{
    selector_syntax_output output;
    if (webscene_selector_parser_abi_version() != 1U) {
        output.error = "Servo selector-parser ABI version mismatch";
        return output;
    }

    const auto started = std::chrono::steady_clock::now();
    auto parsed = webscene_selector_parse(borrow(input));
    const auto finished = std::chrono::steady_clock::now();
    output.metrics.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    output.metrics.rust_allocation_count = parsed.rust_allocation_count;
    output.metrics.rust_peak_bytes = parsed.rust_peak_bytes;
    output.metrics.rust_retained_bytes = parsed.rust_retained_bytes;

    struct handle_guard final {
        void* handle;
        ~handle_guard() { webscene_selector_free(handle); }
    } guard{parsed.handle};

    if (parsed.status != 0U || parsed.handle == nullptr) {
        output.error = parsed.status == 1U
            ? "Servo rejected the selector list"
            : parsed.status == 3U
                ? "Servo selector parser panicked"
                : "Servo selector parser failed";
        return output;
    }
    if (parsed.selector_count > std::numeric_limits<size_t>::max()) {
        output.error = "Servo selector result exceeds native address space";
        return output;
    }

    output.selectors.reserve(static_cast<size_t>(parsed.selector_count));
    for (size_t selector_index = 0;
        selector_index < static_cast<size_t>(parsed.selector_count);
        ++selector_index) {
        webscene_selector_view view{};
        if (webscene_selector_at(parsed.handle, selector_index, &view) == 0U
            || view.compound_count == 0U
            || view.combinator_count + 1U != view.compound_count) {
            output.error = "Servo returned an invalid selector view";
            return output;
        }
        selector_syntax_selector selector;
        selector.serialized = copy_slice(view.serialized);
        selector.specificity = view.specificity;
        selector.compounds.reserve(view.compound_count);
        for (size_t compound_index = 0;
            compound_index < view.compound_count;
            ++compound_index) {
            webscene_selector_byte_slice compound{};
            if (webscene_selector_compound_at(
                    parsed.handle,
                    selector_index,
                    compound_index,
                    &compound) == 0U) {
                output.error = "Servo returned an invalid compound selector";
                return output;
            }
            selector.compounds.push_back(copy_slice(compound));
        }
        selector.combinators.reserve(view.combinator_count);
        for (size_t combinator_index = 0;
            combinator_index < view.combinator_count;
            ++combinator_index) {
            uint8_t combinator{};
            if (webscene_selector_combinator_at(
                    parsed.handle,
                    selector_index,
                    combinator_index,
                    &combinator) == 0U
                || (combinator != ' ' && combinator != '>'
                    && combinator != '+' && combinator != '~')) {
                output.error = "Servo returned an invalid selector combinator";
                return output;
            }
            selector.combinators.push_back(static_cast<char>(combinator));
        }
        output.selectors.push_back(std::move(selector));
    }
    return output;
}

} // namespace webscene_native

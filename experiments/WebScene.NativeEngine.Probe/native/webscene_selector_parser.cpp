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

constexpr uint32_t surrogate_escape_sentinel = 0xF0000U;
constexpr uint32_t surrogate_escape_base = 0xF1000U;

void append_utf8(std::string& output, uint32_t value)
{
    if (value <= 0x7FU) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

bool decode_four_byte_utf8(std::string_view input, size_t offset, uint32_t& value)
{
    if (offset + 4U > input.size()) return false;
    const auto first = static_cast<uint8_t>(input[offset]);
    const auto second = static_cast<uint8_t>(input[offset + 1U]);
    const auto third = static_cast<uint8_t>(input[offset + 2U]);
    const auto fourth = static_cast<uint8_t>(input[offset + 3U]);
    if (first < 0xF0U || first > 0xF4U
        || (second & 0xC0U) != 0x80U
        || (third & 0xC0U) != 0x80U
        || (fourth & 0xC0U) != 0x80U) return false;
    value = ((first & 0x07U) << 18U)
        | ((second & 0x3FU) << 12U)
        | ((third & 0x3FU) << 6U)
        | (fourth & 0x3FU);
    return value >= 0x10000U && value <= 0x10FFFFU;
}

bool is_wtf8_surrogate(std::string_view input, size_t offset, uint32_t& surrogate)
{
    if (offset + 3U > input.size()
        || static_cast<uint8_t>(input[offset]) != 0xEDU) return false;
    const auto second = static_cast<uint8_t>(input[offset + 1U]);
    const auto third = static_cast<uint8_t>(input[offset + 2U]);
    if (second < 0xA0U || second > 0xBFU || (third & 0xC0U) != 0x80U) return false;
    surrogate = 0xD000U
        | ((second & 0x3FU) << 6U)
        | (third & 0x3FU);
    return surrogate >= 0xD800U && surrogate <= 0xDFFFU;
}

std::string encode_wtf8_surrogates(std::string_view input, bool& transformed)
{
    transformed = false;
    for (size_t offset = 0U; offset < input.size();) {
        uint32_t surrogate{};
        if (is_wtf8_surrogate(input, offset, surrogate)) {
            transformed = true;
            break;
        }
        ++offset;
    }
    if (!transformed) return {};

    std::string output;
    output.reserve(input.size() + 8U);
    for (size_t offset = 0U; offset < input.size();) {
        uint32_t surrogate{};
        if (is_wtf8_surrogate(input, offset, surrogate)) {
            append_utf8(output, surrogate_escape_sentinel);
            append_utf8(output, surrogate_escape_base + surrogate - 0xD800U);
            offset += 3U;
            continue;
        }
        uint32_t scalar{};
        if (decode_four_byte_utf8(input, offset, scalar)
            && scalar == surrogate_escape_sentinel) {
            append_utf8(output, surrogate_escape_sentinel);
            append_utf8(output, surrogate_escape_sentinel);
            offset += 4U;
            continue;
        }
        output.push_back(input[offset++]);
    }
    return output;
}

void restore_wtf8_surrogates(std::string& value)
{
    std::string restored;
    restored.reserve(value.size());
    for (size_t offset = 0U; offset < value.size();) {
        uint32_t scalar{};
        if (!decode_four_byte_utf8(value, offset, scalar)
            || scalar != surrogate_escape_sentinel) {
            restored.push_back(value[offset++]);
            continue;
        }

        uint32_t escaped{};
        if (decode_four_byte_utf8(value, offset + 4U, escaped)
            && escaped == surrogate_escape_sentinel) {
            append_utf8(restored, surrogate_escape_sentinel);
            offset += 8U;
        } else if (escaped >= surrogate_escape_base
            && escaped < surrogate_escape_base + 0x800U) {
            const auto surrogate = 0xD800U + escaped - surrogate_escape_base;
            restored.push_back(static_cast<char>(0xE0U | (surrogate >> 12U)));
            restored.push_back(static_cast<char>(0x80U | ((surrogate >> 6U) & 0x3FU)));
            restored.push_back(static_cast<char>(0x80U | (surrogate & 0x3FU)));
            offset += 8U;
        } else {
            append_utf8(restored, surrogate_escape_sentinel);
            offset += 4U;
        }
    }
    value = std::move(restored);
}

} // namespace

selector_syntax_output parse_selector_syntax(std::string_view input)
{
    selector_syntax_output output;
    if (webscene_selector_parser_abi_version() != 1U) {
        output.error = "Servo selector-parser ABI version mismatch";
        return output;
    }

    bool transformed_wtf8 = false;
    const auto normalized = encode_wtf8_surrogates(input, transformed_wtf8);
    const auto parser_input = transformed_wtf8 ? std::string_view(normalized) : input;
    const auto started = std::chrono::steady_clock::now();
    auto parsed = webscene_selector_parse(borrow(parser_input));
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
        if (transformed_wtf8) restore_wtf8_surrogates(selector.serialized);
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
            auto copied_compound = copy_slice(compound);
            if (transformed_wtf8) restore_wtf8_surrogates(copied_compound);
            selector.compounds.push_back(std::move(copied_compound));
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

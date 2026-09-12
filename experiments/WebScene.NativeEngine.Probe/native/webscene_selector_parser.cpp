#include "webscene_selector_parser.h"

#include "webscene_selector_parser_ffi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
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
constexpr size_t maximum_process_cache_entries = 512U;
std::mutex selector_cache_mutex;
std::unordered_map<std::string, selector_syntax_output> selector_process_cache;
std::string selector_cache_directory;
std::atomic<uint64_t> selector_process_hits{0U};
std::atomic<uint64_t> selector_persistent_hits{0U};
std::atomic<uint64_t> selector_compilations{0U};

uint64_t selector_hash(std::string_view input)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const auto character : input) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<uint64_t>(input.size());
    hash *= 1099511628211ULL;
    return hash;
}

std::filesystem::path selector_persistent_path(std::string_view input)
{
    std::lock_guard lock(selector_cache_mutex);
    if (selector_cache_directory.empty()) return {};
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << selector_hash(input);
    return std::filesystem::path(selector_cache_directory) / "css" / "selectors"
        / (name.str() + ".wsslc");
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

bool read_string(std::istream& input, std::string& value, uint64_t maximum = 16ULL << 20U)
{
    uint64_t size{};
    if (!read_scalar(input, size) || size > maximum) return false;
    value.resize(static_cast<size_t>(size));
    if (!value.empty()) input.read(value.data(), static_cast<std::streamsize>(value.size()));
    return bool(input);
}

void remember_selector(std::string input, const selector_syntax_output& output)
{
    std::lock_guard lock(selector_cache_mutex);
    if (selector_process_cache.size() >= maximum_process_cache_entries
        && !selector_process_cache.contains(input)) {
        selector_process_cache.clear();
    }
    selector_process_cache.insert_or_assign(std::move(input), output);
}

std::optional<selector_syntax_output> find_selector_process_cache(std::string_view input)
{
    std::lock_guard lock(selector_cache_mutex);
    const auto found = selector_process_cache.find(std::string(input));
    if (found == selector_process_cache.end()) return std::nullopt;
    selector_process_hits.fetch_add(1U, std::memory_order_relaxed);
    auto result = found->second;
    result.metrics.duration_ns = 0U;
    result.metrics.rust_allocation_count = 0U;
    result.metrics.rust_peak_bytes = 0U;
    result.metrics.rust_retained_bytes = 0U;
    result.metrics.compilation_cache_hit = true;
    return result;
}

bool write_selector_persistent_cache(std::string_view input, const selector_syntax_output& output)
{
    const auto path = selector_persistent_path(input);
    if (path.empty() || !output) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.string() + "." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    constexpr char magic[8] = {'W','S','S','L','C','0','0','1'};
    stream.write(magic, sizeof(magic));
    const uint32_t schema = 1U;
    const auto hash = selector_hash(input);
    const auto selector_count = static_cast<uint64_t>(output.selectors.size());
    if (!write_scalar(stream, schema) || !write_scalar(stream, hash)
        || !write_string(stream, input) || !write_scalar(stream, selector_count)) return false;
    for (const auto& selector : output.selectors) {
        const auto compound_count = static_cast<uint64_t>(selector.compounds.size());
        const auto combinator_count = static_cast<uint64_t>(selector.combinators.size());
        if (!write_string(stream, selector.serialized)
            || !write_scalar(stream, selector.specificity)
            || !write_scalar(stream, compound_count)) return false;
        for (const auto& compound : selector.compounds)
            if (!write_string(stream, compound)) return false;
        if (!write_scalar(stream, combinator_count)) return false;
        if (combinator_count) {
            stream.write(selector.combinators.data(),
                static_cast<std::streamsize>(selector.combinators.size()));
            if (!stream) return false;
        }
    }
    stream.close();
    if (!stream) return false;
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

std::optional<selector_syntax_output> read_selector_persistent_cache(std::string_view input)
{
    const auto path = selector_persistent_path(input);
    if (path.empty()) return std::nullopt;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    char magic[8]{};
    stream.read(magic, sizeof(magic));
    constexpr char expected[8] = {'W','S','S','L','C','0','0','1'};
    uint32_t schema{};
    uint64_t hash{}, selector_count{};
    std::string stored_input;
    if (!stream || !std::equal(std::begin(magic), std::end(magic), std::begin(expected))
        || !read_scalar(stream, schema) || !read_scalar(stream, hash)
        || !read_string(stream, stored_input, 64ULL << 20U)
        || !read_scalar(stream, selector_count) || schema != 1U
        || hash != selector_hash(input) || stored_input != input || selector_count > 4096U) {
        return std::nullopt;
    }
    selector_syntax_output output;
    output.selectors.reserve(static_cast<size_t>(selector_count));
    for (uint64_t index = 0; index < selector_count; ++index) {
        selector_syntax_selector selector;
        uint64_t compound_count{}, combinator_count{};
        if (!read_string(stream, selector.serialized)
            || !read_scalar(stream, selector.specificity)
            || !read_scalar(stream, compound_count) || compound_count > 4096U) return std::nullopt;
        selector.compounds.reserve(static_cast<size_t>(compound_count));
        for (uint64_t compound = 0; compound < compound_count; ++compound) {
            std::string value;
            if (!read_string(stream, value)) return std::nullopt;
            selector.compounds.push_back(std::move(value));
        }
        if (!read_scalar(stream, combinator_count) || combinator_count > 4096U) return std::nullopt;
        selector.combinators.resize(static_cast<size_t>(combinator_count));
        if (combinator_count) {
            stream.read(selector.combinators.data(),
                static_cast<std::streamsize>(selector.combinators.size()));
            if (!stream) return std::nullopt;
        }
        if (selector.compounds.empty()
            || selector.combinators.size() + 1U != selector.compounds.size()) return std::nullopt;
        output.selectors.push_back(std::move(selector));
    }
    output.metrics.compilation_cache_hit = true;
    selector_persistent_hits.fetch_add(1U, std::memory_order_relaxed);
    return output;
}

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

void set_selector_syntax_compilation_cache_directory(std::string directory)
{
    std::lock_guard lock(selector_cache_mutex);
    selector_cache_directory = std::move(directory);
}

void clear_selector_syntax_process_cache()
{
    std::lock_guard lock(selector_cache_mutex);
    selector_process_cache.clear();
}

uint64_t selector_syntax_process_cache_hits() noexcept
{ return selector_process_hits.load(std::memory_order_relaxed); }
uint64_t selector_syntax_persistent_cache_hits() noexcept
{ return selector_persistent_hits.load(std::memory_order_relaxed); }
uint64_t selector_syntax_compilation_count() noexcept
{ return selector_compilations.load(std::memory_order_relaxed); }

selector_syntax_output parse_selector_syntax(std::string_view input)
{
    if (auto cached = find_selector_process_cache(input)) return std::move(*cached);
    if (auto cached = read_selector_persistent_cache(input)) {
        remember_selector(std::string(input), *cached);
        return std::move(*cached);
    }

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
    selector_compilations.fetch_add(1U, std::memory_order_relaxed);

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
    remember_selector(std::string(input), output);
    write_selector_persistent_cache(input, output);
    return output;
}

} // namespace webscene_native

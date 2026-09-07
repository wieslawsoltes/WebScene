#include "webscene_native_dom.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace allocation_probe {

struct counters final {
    uint64_t calls{0};
    uint64_t requested_bytes{0};
    bool enabled{false};
};

thread_local counters current;

void record(std::size_t bytes) noexcept
{
    if (!current.enabled) return;
    ++current.calls;
    current.requested_bytes += static_cast<uint64_t>(bytes);
}

void* allocate(std::size_t bytes)
{
    bytes = std::max<std::size_t>(bytes, 1U);
    if (auto* result = std::malloc(bytes); result != nullptr) {
        record(bytes);
        return result;
    }
    throw std::bad_alloc();
}

void* allocate_aligned(std::size_t bytes, std::size_t alignment)
{
    bytes = std::max<std::size_t>(bytes, 1U);
#if defined(_WIN32)
    if (auto* result = _aligned_malloc(bytes, alignment); result != nullptr) {
        record(bytes);
        return result;
    }
#else
    void* result = nullptr;
    if (posix_memalign(&result, alignment, bytes) == 0) {
        record(bytes);
        return result;
    }
#endif
    throw std::bad_alloc();
}

void reset() noexcept
{
    current.calls = 0;
    current.requested_bytes = 0;
}

} // namespace allocation_probe

void* operator new(std::size_t bytes) { return allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocation_probe::allocate(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    return allocation_probe::allocate_aligned(
        bytes,
        static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return allocation_probe::allocate_aligned(
        bytes,
        static_cast<std::size_t>(alignment));
}

void operator delete(void* value, std::align_val_t) noexcept
{
#if defined(_WIN32)
    _aligned_free(value);
#else
    std::free(value);
#endif
}

void operator delete[](void* value, std::align_val_t alignment) noexcept
{
    ::operator delete(value, alignment);
}

void operator delete(void* value, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(value, alignment);
}

void operator delete[](void* value, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(value, alignment);
}

using namespace webscene_native;

namespace {

struct options final {
    std::string fixture{"four-chart-nested-flex-v1"};
    std::string phase{"layout"};
    size_t warmups{10U};
    size_t samples{30U};
    size_t iterations{100U};
};

struct sample final {
    uint64_t duration_ns{0};
    uint64_t allocation_calls{0};
    uint64_t requested_bytes{0};
    double geometry_checksum{0};
    uint64_t intrinsic_direct_cache_hits{0};
    uint64_t intrinsic_hash_lookups{0};
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    std::array<uint64_t, 17U> intrinsic_size_branch_counts{};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    std::array<uint64_t, 4U> intrinsic_view_box_parse_counts{};
#endif
};

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "layout scratch benchmark: " << message << '\n';
    std::exit(1);
}

size_t parse_positive(std::string_view value, std::string_view name)
{
    size_t parsed = 0;
    try {
        const auto text = std::string(value);
        size_t consumed = 0;
        parsed = std::stoull(text, &consumed);
        if (consumed != text.size() || parsed == 0U) throw std::invalid_argument(name.data());
    } catch (...) {
        fail(std::string(name) + " must be a positive integer");
    }
    return parsed;
}

options parse_options(int argc, char** argv)
{
    options result;
    for (int index = 1; index < argc; ++index) {
        const auto argument = std::string_view(argv[index]);
        if (argument == "--help") {
            std::cout << "Usage: webscene_layout_scratch_benchmark "
                         "[--fixture four-chart-nested-flex-v1|intrinsic-table-select-v1|inline-text-v1|inline-font-family-v1|intrinsic-svg-view-box-v1] "
                         "[--phase layout|scene] [--warmups N] [--samples N] [--iterations N]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) fail("missing option value");
        const auto value = std::string_view(argv[++index]);
        if (argument == "--fixture") result.fixture = value;
        else if (argument == "--phase") result.phase = value;
        else if (argument == "--warmups") result.warmups = parse_positive(value, "warmups");
        else if (argument == "--samples") result.samples = parse_positive(value, "samples");
        else if (argument == "--iterations") {
            result.iterations = parse_positive(value, "iterations");
        } else {
            fail(std::string("unknown option: ") + std::string(argument));
        }
    }
    if (result.fixture != "four-chart-nested-flex-v1"
        && result.fixture != "intrinsic-table-select-v1"
        && result.fixture != "inline-text-v1"
        && result.fixture != "inline-font-family-v1"
        && result.fixture != "intrinsic-svg-view-box-v1") {
        fail("unsupported fixture");
    }
    if (result.phase != "layout" && result.phase != "scene") fail("unsupported phase");
    return result;
}

css_length pixels(float value) noexcept
{
    return {value, length_unit::pixels};
}

css_length percent(float value) noexcept
{
    return {value, length_unit::percent};
}

dom_node& append(native_document& document, dom_node& parent, std::string_view tag)
{
    auto& child = document.create_element(std::string(tag));
    if (!document.append_child(parent, child)) fail("could not append fixture node");
    return child;
}

void add_flex_items(
    native_document& document,
    dom_node& parent,
    size_t count,
    float basis,
    bool nested_rows)
{
    for (size_t index = 0; index < count; ++index) {
        auto& item = append(document, parent, "div");
        item.style.flex_basis = pixels(basis + static_cast<float>(index % 3U) * 3.0F);
        item.style.min_width = pixels(18.0F + static_cast<float>(index % 2U) * 4.0F);
        item.style.height = pixels(20.0F + static_cast<float>(index % 3U));
        item.style.flex_grow = static_cast<float>((index % 4U) + 1U);
        item.style.flex_shrink = static_cast<float>((index % 3U) + 1U);
        if (!nested_rows) continue;
        item.style.display = display_mode::flex;
        item.style.direction = flex_direction::column;
        item.style.width = percent(100.0F);
        item.style.height = pixels(180.0F);
        for (size_t row_index = 0; row_index < 6U; ++row_index) {
            auto& row = append(document, item, "div");
            row.style.display = display_mode::flex;
            row.style.width = percent(100.0F);
            row.style.height = pixels(24.0F);
            row.style.column_gap = pixels(2.0F);
            add_flex_items(document, row, 8U, 26.0F, false);
        }
    }
}

void build_nested_flex_fixture(native_document& document)
{
    auto& body = document.body();
    body.style.display = display_mode::flex;
    body.style.flex_wrap = true;
    body.style.align_items = align_mode::start;
    body.style.column_gap = pixels(8.0F);
    body.style.row_gap = pixels(8.0F);

    for (size_t chart_index = 0; chart_index < 4U; ++chart_index) {
        auto& chart = append(document, body, "section");
        chart.style.display = display_mode::flex;
        chart.style.direction = flex_direction::column;
        chart.style.width = pixels(570.0F);
        chart.style.height = pixels(380.0F);
        chart.style.flex_shrink = 1.0F;
        chart.style.row_gap = pixels(4.0F);

        auto& toolbar = append(document, chart, "nav");
        toolbar.style.display = display_mode::flex;
        toolbar.style.flex_wrap = true;
        toolbar.style.width = percent(100.0F);
        toolbar.style.height = pixels(54.0F);
        toolbar.style.column_gap = pixels(3.0F);
        toolbar.style.row_gap = pixels(2.0F);
        add_flex_items(document, toolbar, 18U, 58.0F, false);

        auto& panes = append(document, chart, "main");
        panes.style.display = display_mode::flex;
        panes.style.width = percent(100.0F);
        panes.style.height = pixels(280.0F);
        panes.style.column_gap = pixels(4.0F);
        add_flex_items(document, panes, 3U, 210.0F, true);

        auto& status = append(document, chart, "footer");
        status.style.display = display_mode::flex;
        status.style.width = percent(100.0F);
        status.style.height = pixels(30.0F);
        status.style.column_gap = pixels(2.0F);
        add_flex_items(document, status, 12U, 42.0F, false);
    }
}

void add_intrinsic_content(
    native_document& document,
    dom_node& parent,
    size_t seed)
{
    auto& content = append(document, parent, "span");
    content.style.display = display_mode::inline_block;
    content.style.width = pixels(24.0F + static_cast<float>(seed % 7U) * 5.0F);
    content.style.height = pixels(14.0F + static_cast<float>(seed % 3U));
}

void build_intrinsic_table_select_fixture(native_document& document)
{
    auto& body = document.body();
    body.style.display = display_mode::flex;
    body.style.flex_wrap = true;
    body.style.align_items = align_mode::start;
    body.style.column_gap = pixels(8.0F);
    body.style.row_gap = pixels(8.0F);

    for (size_t chart_index = 0; chart_index < 4U; ++chart_index) {
        auto& chart = append(document, body, "section");
        chart.style.display = display_mode::flex;
        chart.style.direction = flex_direction::column;
        chart.style.width = pixels(570.0F);
        chart.style.flex_shrink = 1.0F;
        chart.style.row_gap = pixels(3.0F);

        auto& table = append(document, chart, "table");
        table.style.display = display_mode::table;
        auto& row_group = append(document, table, "tbody");
        row_group.style.display = display_mode::table_row_group;
        for (size_t row_index = 0; row_index < 8U; ++row_index) {
            auto& row = append(document, row_group, "tr");
            row.style.display = display_mode::table_row;
            for (size_t column_index = 0; column_index < 6U; ++column_index) {
                auto& cell = append(document, row, "td");
                cell.style.display = display_mode::table_cell;
                cell.style.padding_left = pixels(2.0F);
                cell.style.padding_right = pixels(2.0F);
                add_intrinsic_content(
                    document,
                    cell,
                    chart_index * 100U + row_index * 10U + column_index);
            }
        }

        auto& controls = append(document, chart, "div");
        controls.style.display = display_mode::flex;
        controls.style.width = percent(100.0F);
        controls.style.column_gap = pixels(4.0F);
        for (size_t select_index = 0; select_index < 4U; ++select_index) {
            auto& select = append(document, controls, "select");
            select.style.display = display_mode::inline_block;
            for (size_t option_index = 0; option_index < 20U; ++option_index) {
                auto& option = append(document, select, "option");
                option.text_content = "option-" + std::to_string(select_index)
                    + "-" + std::to_string(option_index);
            }
        }

        auto& intrinsic_row = append(document, chart, "div");
        intrinsic_row.style.display = display_mode::flex;
        intrinsic_row.style.width = percent(100.0F);
        intrinsic_row.style.column_gap = pixels(2.0F);
        for (size_t item_index = 0; item_index < 24U; ++item_index) {
            auto& item = append(document, intrinsic_row, "div");
            item.style.display = display_mode::flex;
            item.style.flex_shrink = 1.0F;
            add_intrinsic_content(document, item, item_index);
            add_intrinsic_content(document, item, item_index + 31U);
        }
    }
}

void build_inline_text_fixture(native_document& document)
{
    auto& body = document.body();
    body.style.display = display_mode::flex;
    body.style.flex_wrap = true;
    body.style.align_items = align_mode::start;
    body.style.column_gap = pixels(8.0F);
    body.style.row_gap = pixels(8.0F);

    for (size_t chart_index = 0; chart_index < 4U; ++chart_index) {
        auto& chart = append(document, body, "section");
        chart.style.display = display_mode::flex;
        chart.style.direction = flex_direction::column;
        chart.style.width = pixels(270.0F);
        chart.style.flex_shrink = 1.0F;

        for (size_t row_index = 0; row_index < 28U; ++row_index) {
            auto& row = append(document, chart, "div");
            row.style.width = pixels(240.0F);
            row.style.position = position_mode::relative;
            row.style.mutable_textual().text_align = row_index % 2U == 0U
                ? "center"
                : "right";

            for (size_t run_index = 0; run_index < 3U; ++run_index) {
                auto& span = append(document, row, "span");
                span.style.display = display_mode::inline_flow;
                auto& text = append(document, span, "#text");
                text.text_content = "chart " + std::to_string(chart_index)
                    + " row " + std::to_string(row_index)
                    + " value " + std::to_string(run_index) + " ";
            }

            auto& badge = append(document, row, "span");
            badge.style.display = display_mode::inline_flow;
            badge.style.position = position_mode::absolute;
            badge.style.right = pixels(2.0F);
            badge.style.top = pixels(1.0F);
            auto& badge_text = append(document, badge, "#text");
            badge_text.text_content = "live";
        }
    }
}

void build_inline_font_family_fixture(native_document& document)
{
    build_inline_text_fixture(document);
    document.body().style.mutable_textual().font_family =
        "Inter, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif";
}

void build_intrinsic_svg_view_box_fixture(native_document& document)
{
    auto& body = document.body();
    body.style.display = display_mode::flex;
    body.style.flex_wrap = true;
    body.style.align_items = align_mode::start;
    body.style.column_gap = pixels(2.0F);
    body.style.row_gap = pixels(2.0F);

    for (size_t index = 0; index < 256U; ++index) {
        auto& icon = append(document, body, "svg");
        icon.style.display = display_mode::inline_block;
        icon.style.flex_shrink = 1.0F;
        const auto width = 12U + index % 13U;
        const auto height = 10U + index % 11U;
        icon.attributes["viewBox"] = "-2 -3 " + std::to_string(width)
            + " " + std::to_string(height);
    }
}

void build_fixture(native_document& document, std::string_view fixture)
{
    if (fixture == "four-chart-nested-flex-v1") {
        build_nested_flex_fixture(document);
        return;
    }
    if (fixture == "inline-text-v1") {
        build_inline_text_fixture(document);
        return;
    }
    if (fixture == "inline-font-family-v1") {
        build_inline_font_family_fixture(document);
        return;
    }
    if (fixture == "intrinsic-svg-view-box-v1") {
        build_intrinsic_svg_view_box_fixture(document);
        return;
    }
    build_intrinsic_table_select_fixture(document);
}

double checksum_node(const dom_node& node)
{
    auto result = static_cast<double>(node.id) * 0.000001
        + node.layout.x * 0.5
        + node.layout.y * 0.25
        + node.layout.width * 0.125
        + node.layout.height * 0.0625;
    for (const auto* child : node.children) {
        if (child != nullptr) result += checksum_node(*child);
    }
    return result;
}

sample run_sample(native_document& document, size_t iterations)
{
    allocation_probe::reset();
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
    const auto direct_hits_before = document.intrinsic_size_direct_cache_hits();
    const auto hash_lookups_before = document.intrinsic_size_hash_lookups();
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    const auto intrinsic_branches_before = document.intrinsic_size_branch_counts();
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    const auto view_box_parses_before = document.intrinsic_view_box_parse_counts();
#endif
    const auto started = std::chrono::steady_clock::now();
    allocation_probe::current.enabled = true;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        const auto width = iteration % 2U == 0U ? 1200.0F : 1192.0F;
        document.layout(width, 820.0F);
    }
    allocation_probe::current.enabled = false;
    const auto finished = std::chrono::steady_clock::now();
    sample result{
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished - started).count()),
        allocation_probe::current.calls,
        allocation_probe::current.requested_bytes,
        checksum_node(document.body())};
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
    result.intrinsic_direct_cache_hits =
        document.intrinsic_size_direct_cache_hits() - direct_hits_before;
    result.intrinsic_hash_lookups =
        document.intrinsic_size_hash_lookups() - hash_lookups_before;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    const auto intrinsic_branches_after = document.intrinsic_size_branch_counts();
    for (size_t branch = 0; branch < result.intrinsic_size_branch_counts.size(); ++branch) {
        result.intrinsic_size_branch_counts[branch] =
            intrinsic_branches_after[branch] - intrinsic_branches_before[branch];
    }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    const auto view_box_parses_after = document.intrinsic_view_box_parse_counts();
    for (size_t counter = 0; counter < result.intrinsic_view_box_parse_counts.size();
        ++counter) {
        result.intrinsic_view_box_parse_counts[counter] =
            view_box_parses_after[counter] - view_box_parses_before[counter];
    }
#endif
    return result;
}

double checksum_scene(
    const std::vector<webscene_scene_command>& commands,
    const std::vector<webscene_scene_string>& strings,
    const std::vector<char>& string_bytes)
{
    auto result = static_cast<double>(commands.size()) * 0.5
        + static_cast<double>(strings.size()) * 0.25
        + static_cast<double>(string_bytes.size()) * 0.125;
    for (const auto& command : commands) {
        result += command.kind * 0.03125
            + command.node_id * 0.000001
            + command.x * 0.00001
            + command.y * 0.00002
            + command.width * 0.00003
            + command.height * 0.00004;
    }
    for (const auto value : string_bytes) {
        result += static_cast<unsigned char>(value) * 0.0000001;
    }
    return result;
}

sample run_scene_sample(
    native_document& document,
    size_t iterations,
    std::vector<webscene_scene_command>& commands,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& string_bytes)
{
    allocation_probe::reset();
    const auto started = std::chrono::steady_clock::now();
    allocation_probe::current.enabled = true;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        document.build_scene(commands, strings, string_bytes);
    }
    allocation_probe::current.enabled = false;
    const auto finished = std::chrono::steady_clock::now();
    return {
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished - started).count()),
        allocation_probe::current.calls,
        allocation_probe::current.requested_bytes,
        checksum_scene(commands, strings, string_bytes)};
}

uint64_t percentile(std::vector<uint64_t> values, double quantile)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(
        quantile * static_cast<double>(values.size() - 1U));
    return values[index];
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    native_document document;
    build_fixture(document, options.fixture);
    std::vector<webscene_scene_command> commands;
    std::vector<webscene_scene_string> strings;
    std::vector<char> string_bytes;
    document.layout(1200.0F, 820.0F);
    for (size_t index = 0; index < options.warmups; ++index) {
        if (options.phase == "scene") {
            document.build_scene(commands, strings, string_bytes);
        } else {
            document.layout(index % 2U == 0U ? 1200.0F : 1192.0F, 820.0F);
        }
    }

    std::vector<sample> samples;
    samples.reserve(options.samples);
    for (size_t index = 0; index < options.samples; ++index) {
        samples.push_back(options.phase == "scene"
            ? run_scene_sample(
                document,
                options.iterations,
                commands,
                strings,
                string_bytes)
            : run_sample(document, options.iterations));
    }

    const auto expected_calls = samples.front().allocation_calls;
    const auto expected_bytes = samples.front().requested_bytes;
    const auto expected_checksum = samples.front().geometry_checksum;
    const auto expected_direct_hits = samples.front().intrinsic_direct_cache_hits;
    const auto expected_hash_lookups = samples.front().intrinsic_hash_lookups;
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    const auto expected_intrinsic_branches =
        samples.front().intrinsic_size_branch_counts;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    const auto expected_view_box_parses =
        samples.front().intrinsic_view_box_parse_counts;
#endif
    for (const auto& sample : samples) {
        if (sample.allocation_calls != expected_calls
            || sample.requested_bytes != expected_bytes) {
            fail("allocation counts changed between identical samples");
        }
        if (std::abs(sample.geometry_checksum - expected_checksum) > 0.0001) {
            fail("geometry checksum changed between identical samples");
        }
        if (sample.intrinsic_direct_cache_hits != expected_direct_hits
            || sample.intrinsic_hash_lookups != expected_hash_lookups) {
            fail("intrinsic cache counters changed between identical samples");
        }
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
        if (sample.intrinsic_size_branch_counts != expected_intrinsic_branches) {
            fail("intrinsic branch counters changed between identical samples");
        }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
        if (sample.intrinsic_view_box_parse_counts != expected_view_box_parses) {
            fail("intrinsic viewBox parse counters changed between identical samples");
        }
#endif
    }

    if (expected_calls % options.iterations != 0U
        || expected_bytes % options.iterations != 0U) {
        fail("sample allocation totals are not exactly divisible per operation");
    }
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
    if (std::any_of(
            expected_intrinsic_branches.begin(),
            expected_intrinsic_branches.end(),
            [&](uint64_t count) { return count % options.iterations != 0U; })) {
        fail("intrinsic branch totals are not exactly divisible per layout");
    }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
    if (std::any_of(
            expected_view_box_parses.begin(),
            expected_view_box_parses.end(),
            [&](uint64_t count) { return count % options.iterations != 0U; })) {
        fail("intrinsic viewBox counters are not exactly divisible per layout");
    }
#endif

    std::vector<uint64_t> durations;
    durations.reserve(samples.size());
    for (const auto& sample : samples) durations.push_back(sample.duration_ns);
    const auto p50 = percentile(durations, 0.50);
    const auto p95 = percentile(durations, 0.95);
    const auto allocations_per_operation = expected_calls / options.iterations;
    const auto bytes_per_operation = expected_bytes / options.iterations;
    const auto memory = document.read_allocation_metrics();

    std::cout << "{\"schemaVersion\":1"
        << ",\"fixture\":\"" << options.fixture << "\""
#if defined(WEBSCENE_NATIVE_ENGINE_SCENE_PAINT_ORDER_CONTROL)
        << ",\"variant\":\"scene-paint-order-vector-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_SCENE_PAINT_ORDER_BENCHMARK)
        << ",\"variant\":\"scene-paint-order-inline-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK) \
    && defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_HASH_CACHE_CONTROL)
        << ",\"variant\":\"intrinsic-size-hash-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
        << ",\"variant\":\"intrinsic-size-direct-cache-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_ROW_COLLECTOR_CONTROL)
        << ",\"variant\":\"intrinsic-row-std-function-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_ROW_COLLECTOR_BENCHMARK)
        << ",\"variant\":\"intrinsic-row-recursive-lambda-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK) \
    && defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_STREAM_CONTROL)
        << ",\"variant\":\"intrinsic-view-box-stream-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
        << ",\"variant\":\"intrinsic-view-box-direct-parser-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INLINE_BOX_BOUNDS_BENCHMARK) \
    && defined(WEBSCENE_NATIVE_ENGINE_INLINE_BOX_BOUNDS_CALLBACK_CONTROL)
        << ",\"variant\":\"inline-box-std-function-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INLINE_BOX_BOUNDS_BENCHMARK)
        << ",\"variant\":\"inline-box-recursive-lambda-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_ITEM_COPY_CONTROL)
        << ",\"variant\":\"intrinsic-item-copy-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_ITEM_COPY_BENCHMARK)
        << ",\"variant\":\"intrinsic-item-direct-view-candidate\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_TEXT_NODE_FAST_PATH_CONTROL)
        << ",\"variant\":\"intrinsic-text-legacy-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_TEXT_NODE_FAST_PATH_BENCHMARK)
        << ",\"variant\":\"intrinsic-text-fast-path-candidate\""
#else
        << ",\"variant\":\"intrinsic-size-branch-profile\""
#endif
#elif defined(WEBSCENE_NATIVE_ENGINE_TEXT_MEASUREMENT_LOOKUP_CONTROL)
        << ",\"variant\":\"accepted-inline-layout-scratch-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_TEXT_TRANSFORM_COPY_CONTROL)
        << ",\"variant\":\"accepted-text-measurement-lookup-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_FONT_FAMILY_VIEW_CONTROL)
        << ",\"variant\":\"accepted-text-transform-none-copy-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INLINE_LAYOUT_SCRATCH_CONTROL)
        << ",\"variant\":\"accepted-layout-callback-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_LAYOUT_CALLBACK_CONTROL)
        << ",\"variant\":\"accepted-paint-order-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_RETAINED_PAINT_ORDER_CONTROL)
        << ",\"variant\":\"accepted-cumulative-control\""
#elif defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SCRATCH_CONTROL)
        << ",\"variant\":\"accepted-control\""
#else
        << ",\"variant\":\"font-family-view-candidate\""
#endif
        << ",\"warmups\":" << options.warmups
        << ",\"phase\":\"" << options.phase << "\""
        << ",\"samples\":" << options.samples
        << ",\"iterationsPerSample\":" << options.iterations
        << ",\"nodes\":" << memory.node_count
        << ",\"nodeObjectSizeBytes\":" << memory.node_object_size_bytes
        << ",\"nodeObjectBytes\":" << memory.node_object_bytes;
    if (options.phase == "scene") {
        std::cout << ",\"allocationCountPerScene\":" << allocations_per_operation
            << ",\"requestedBytesPerScene\":" << bytes_per_operation
            << ",\"p50NanosecondsPerScene\":" << p50 / options.iterations
            << ",\"p95NanosecondsPerScene\":" << p95 / options.iterations
            << ",\"sceneChecksum\":" << expected_checksum;
    } else {
        std::cout << ",\"allocationCountPerLayout\":" << allocations_per_operation
            << ",\"requestedBytesPerLayout\":" << bytes_per_operation
            << ",\"p50NanosecondsPerLayout\":" << p50 / options.iterations
            << ",\"p95NanosecondsPerLayout\":" << p95 / options.iterations
            << ",\"geometryChecksum\":" << expected_checksum;
    }
    std::cout << ",\"layoutScratchReservedBytes\":"
        << memory.layout_scratch_reserved_bytes
        << ",\"layoutScratchPeakBytes\":" << memory.layout_scratch_peak_bytes
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_DIRECT_CACHE_BENCHMARK)
        << ",\"intrinsicDirectCacheHitsPerLayout\":"
        << expected_direct_hits / options.iterations
        << ",\"intrinsicHashLookupsPerLayout\":"
        << expected_hash_lookups / options.iterations
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_SIZE_BRANCH_BENCHMARK)
        << ",\"intrinsicSizeBranchesPerLayout\":{"
        << "\"total\":" << samples.front().intrinsic_size_branch_counts[0] / options.iterations
        << ",\"authoredDefinite\":" << samples.front().intrinsic_size_branch_counts[1] / options.iterations
        << ",\"table\":" << samples.front().intrinsic_size_branch_counts[2] / options.iterations
        << ",\"collapsedSelect\":" << samples.front().intrinsic_size_branch_counts[3] / options.iterations
        << ",\"input\":" << samples.front().intrinsic_size_branch_counts[4] / options.iterations
        << ",\"replacedElementVisit\":" << samples.front().intrinsic_size_branch_counts[5] / options.iterations
        << ",\"textOrEmptyLeaf\":" << samples.front().intrinsic_size_branch_counts[6] / options.iterations
        << ",\"grid\":" << samples.front().intrinsic_size_branch_counts[7] / options.iterations
        << ",\"genericContainer\":" << samples.front().intrinsic_size_branch_counts[8] / options.iterations
        << ",\"tableRowCollectorCalls\":" << samples.front().intrinsic_size_branch_counts[9] / options.iterations
        << ",\"textNodeLeaf\":" << samples.front().intrinsic_size_branch_counts[10] / options.iterations
        << ",\"textNodeFastPath\":" << samples.front().intrinsic_size_branch_counts[11] / options.iterations
        << ",\"genericWithoutSyntheticItems\":" << samples.front().intrinsic_size_branch_counts[12] / options.iterations
        << ",\"genericWithSyntheticItems\":" << samples.front().intrinsic_size_branch_counts[13] / options.iterations
        << ",\"genericItemPointersCopied\":" << samples.front().intrinsic_size_branch_counts[14] / options.iterations
        << ",\"genericDirectChildViewHits\":" << samples.front().intrinsic_size_branch_counts[15] / options.iterations
        << ",\"genericItemPointersCopyAvoided\":" << samples.front().intrinsic_size_branch_counts[16] / options.iterations
        << "}"
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_INTRINSIC_VIEW_BOX_BENCHMARK)
        << ",\"intrinsicViewBoxParsesPerLayout\":{\"attempts\":"
        << expected_view_box_parses[0] / options.iterations
        << ",\"streamConstructions\":" << expected_view_box_parses[1] / options.iterations
        << ",\"directScans\":" << expected_view_box_parses[2] / options.iterations
        << ",\"successes\":" << expected_view_box_parses[3] / options.iterations
        << "}"
#endif
        << "}\n";
    return 0;
}

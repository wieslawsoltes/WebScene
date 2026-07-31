#include "webscene_html_parser.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace webscene_native;

namespace {

struct sample final {
    uint64_t duration_ns{0};
    uint64_t callbacks{0};
    uint64_t node_count{0};
    uint64_t node_bytes{0};
    uint64_t pool_reserved_bytes{0};
    uint64_t rust_allocation_count{0};
    uint64_t rust_peak_bytes{0};
    uint64_t rust_retained_bytes{0};
};

std::string repeated_fixture(size_t target_bytes)
{
    constexpr std::string_view component =
        "<section class='card'><h2>Entity &amp; text</h2>"
        "<p data-value='x'>Whitespace <b>and</b> nodes.</p>"
        "<svg viewBox='0 0 16 16'><path d='M0 0h16v16z'/></svg>"
        "<!--hydration-marker--></section>";
    std::string result;
    result.reserve(target_bytes + component.size());
    while (result.size() < target_bytes) result += component;
    return result;
}

sample run_once(std::string_view input, bool preserve_comments)
{
    native_document document;
    auto& root = document.create_node(dom_node_kind::document_fragment, "#fragment");
    html_parse_options options;
    options.preserve_comments = preserve_comments;
    const auto parsed = parse_html_fragment(
        document,
        root,
        input,
        "body",
        "http://www.w3.org/1999/xhtml",
        options);
    if (!parsed) {
        std::cerr << "HTML parser benchmark failed: " << parsed.error << '\n';
        std::exit(1);
    }
    const auto memory = document.read_allocation_metrics();
    return {
        parsed.duration_ns,
        parsed.callback_count,
        memory.node_count,
        memory.node_object_bytes + memory.attribute_storage_bytes,
        memory.node_pool_reserved_bytes,
        parsed.rust_allocation_count,
        parsed.rust_peak_bytes,
        parsed.rust_retained_bytes};
}

uint64_t percentile(std::vector<uint64_t> values, double quantile)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(
        quantile * static_cast<double>(values.size() - 1U));
    return values[index];
}

void benchmark(std::string_view name, std::string_view input, bool preserve_comments)
{
    constexpr size_t warmups = 5U;
    constexpr size_t iterations = 30U;
    for (size_t index = 0; index < warmups; ++index) {
        static_cast<void>(run_once(input, preserve_comments));
    }
    std::vector<sample> samples;
    samples.reserve(iterations);
    for (size_t index = 0; index < iterations; ++index) {
        samples.push_back(run_once(input, preserve_comments));
    }
    std::vector<uint64_t> durations;
    for (const auto& value : samples) durations.push_back(value.duration_ns);
    const auto& representative = samples.back();
    const auto median_ns = percentile(durations, 0.50);
    const auto p95_ns = percentile(durations, 0.95);
    const auto mib_per_second = median_ns == 0
        ? 0.0
        : static_cast<double>(input.size()) * 1'000'000'000.0
            / static_cast<double>(median_ns) / (1024.0 * 1024.0);
    std::cout << "{\"fixture\":\"" << name
        << "\",\"inputBytes\":" << input.size()
        << ",\"preserveComments\":" << (preserve_comments ? "true" : "false")
        << ",\"iterations\":" << iterations
        << ",\"p50Nanoseconds\":" << median_ns
        << ",\"p95Nanoseconds\":" << p95_ns
        << ",\"mibPerSecond\":" << mib_per_second
        << ",\"callbacks\":" << representative.callbacks
        << ",\"nodes\":" << representative.node_count
        << ",\"retainedNodeAndAttributeBytes\":" << representative.node_bytes
        << ",\"nodePoolReservedBytes\":" << representative.pool_reserved_bytes
        << ",\"rustAllocationCount\":" << representative.rust_allocation_count
        << ",\"rustPeakBytes\":" << representative.rust_peak_bytes
        << ",\"rustRetainedBytes\":" << representative.rust_retained_bytes
        << "}\n";
}

} // namespace

int main()
{
    const auto one_kib = repeated_fixture(1024U);
    const auto fifty_kib = repeated_fixture(50U * 1024U);
    const auto one_mib = repeated_fixture(1024U * 1024U);
    constexpr std::string_view malformed =
        "<p><b>one<i>two</b>three<table>outside<tr><td>A<td>B</table>"
        "<template><select><option>x<div>y</template>";
    benchmark("component-1k", one_kib, true);
    benchmark("component-50k", fifty_kib, true);
    benchmark("component-1m", one_mib, true);
    benchmark("malformed-tree-construction", malformed, true);
    benchmark("component-50k-comments-discarded", fifty_kib, false);
    return 0;
}

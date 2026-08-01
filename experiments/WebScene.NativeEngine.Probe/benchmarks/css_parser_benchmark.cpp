#include "webscene_css_parser.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace webscene_native;

namespace {

struct sample final {
    uint64_t duration_ns{0};
    uint64_t error_count{0};
    uint64_t allocation_count{0};
    uint64_t peak_bytes{0};
    uint64_t retained_bytes{0};
    size_t rule_count{0};
    size_t declaration_count{0};
};

std::string repeated_fixture(size_t target_bytes)
{
    constexpr std::string_view component = R"CSS(
@media (min-width: 640px) {
  .card[data-state="open"], .card:hover {
    --accent: rgb(20 100 220 / 80%);
    color: var(--accent, blue);
    width: calc(100% - 2rem);
    background-image: url("data:image/svg+xml;utf8,<svg viewBox='0 0 4 4'></svg>");
  }
}
)CSS";
    std::string result;
    result.reserve(target_bytes + component.size());
    while (result.size() < target_bytes) result += component;
    return result;
}

sample run_once(std::string_view input)
{
    const auto started = std::chrono::steady_clock::now();
    const auto parsed = parse_css_syntax_stylesheet(input);
    const auto finished = std::chrono::steady_clock::now();
    if (!parsed) {
        std::cerr << "CSS parser benchmark failed: " << parsed.error << '\n';
        std::exit(1);
    }
    return {
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished - started).count()),
        parsed.metrics.parse_error_count,
        parsed.metrics.parser_allocation_count,
        parsed.metrics.parser_peak_bytes,
        parsed.metrics.parser_retained_bytes,
        parsed.rules.size(),
        parsed.declarations.size()};
}

uint64_t percentile(std::vector<uint64_t> values, double quantile)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(
        quantile * static_cast<double>(values.size() - 1U));
    return values[index];
}

void benchmark(std::string_view name, std::string_view input)
{
    constexpr size_t warmups = 5U;
    constexpr size_t iterations = 30U;
    for (size_t index = 0; index < warmups; ++index) {
        static_cast<void>(run_once(input));
    }
    std::vector<sample> samples;
    samples.reserve(iterations);
    for (size_t index = 0; index < iterations; ++index) samples.push_back(run_once(input));
    std::vector<uint64_t> durations;
    durations.reserve(samples.size());
    for (const auto& value : samples) durations.push_back(value.duration_ns);
    const auto median_ns = percentile(durations, 0.50);
    const auto p95_ns = percentile(durations, 0.95);
    const auto mib_per_second = median_ns == 0
        ? 0.0
        : static_cast<double>(input.size()) * 1'000'000'000.0
            / static_cast<double>(median_ns) / (1024.0 * 1024.0);
    const auto& representative = samples.back();
    std::cout << "{\"fixture\":\"" << name
        << "\",\"inputBytes\":" << input.size()
        << ",\"iterations\":" << iterations
        << ",\"p50Nanoseconds\":" << median_ns
        << ",\"p95Nanoseconds\":" << p95_ns
        << ",\"mibPerSecond\":" << mib_per_second
        << ",\"rules\":" << representative.rule_count
        << ",\"declarations\":" << representative.declaration_count
        << ",\"parseErrors\":" << representative.error_count
        << ",\"parserAllocationCount\":" << representative.allocation_count
        << ",\"parserPeakBytes\":" << representative.peak_bytes
        << ",\"parserRetainedBytes\":" << representative.retained_bytes
        << "}\n";
}

} // namespace

int main()
{
    const auto one_kib = repeated_fixture(1024U);
    const auto fifty_kib = repeated_fixture(50U * 1024U);
    const auto one_mib = repeated_fixture(1024U * 1024U);
    benchmark("component-1k", one_kib);
    benchmark("component-50k", fifty_kib);
    benchmark("component-1m", one_mib);
    benchmark("malformed-recovery", ".a{color:red;broken}.b{content:\"};\";width:calc(1px + 2%)}");
    return 0;
}

#include "webscene_native_engine.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct completion final {
    std::mutex mutex;
    std::condition_variable signal;
    uint64_t operation_id{0U};
};

struct sample_result final {
    double elapsed_ms{0.0};
    uint64_t index_rule_calls{0U};
    uint64_t root_variable_refreshes{0U};
    uint64_t class_lookups{0U};
    uint64_t owned_class_lookup_keys{0U};
    uint64_t owned_class_lookup_bytes{0U};
    uint64_t checksum{0U};
};

void completed(void* user_data, uint64_t operation_id)
{
    auto& result = *static_cast<completion*>(user_data);
    {
        const std::lock_guard lock(result.mutex);
        result.operation_id = operation_id;
    }
    result.signal.notify_one();
}

double evaluate_number(webscene_engine* engine, const std::string& source)
{
    const webscene_interop_evaluate_request_v3 request{
        sizeof(webscene_interop_evaluate_request_v3),
        3U,
        source.data(),
        source.size(),
        "media-refresh-benchmark.js",
        sizeof("media-refresh-benchmark.js") - 1U,
        0U,
        0U};
    completion result;
    const auto operation_id = webscene_engine_begin_evaluate_v3(
        engine, &request, completed, &result);
    if (operation_id == 0U) throw std::runtime_error("evaluation submission failed");
    {
        std::unique_lock lock(result.mutex);
        if (!result.signal.wait_for(lock, std::chrono::seconds(30), [&] {
                return result.operation_id != 0U;
            })) {
            throw std::runtime_error("evaluation timed out");
        }
    }
    const auto* view = webscene_engine_take_invoke_result_v3(engine, operation_id);
    if (view == nullptr
        || view->status != WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
        || view->values == nullptr
        || view->root_value_index >= view->value_count
        || view->values[view->root_value_index].kind != WEBSCENE_INTEROP_VALUE_NUMBER_V3) {
        throw std::runtime_error("evaluation did not return a number");
    }
    const auto value = std::bit_cast<double>(
        view->values[view->root_value_index].payload);
    const auto lease_id = view->lease_id;
    webscene_interop_result_release_v3(view, lease_id);
    return value;
}

void enqueue_resize(webscene_engine* engine, double width, uint64_t sequence)
{
    const webscene_input_event input{
        WEBSCENE_INPUT_RESIZE,
        0U,
        sequence,
        width,
        720.0,
        1.0,
        0};
    if (webscene_engine_enqueue(engine, &input) == 0U) {
        throw std::runtime_error("resize was rejected");
    }
}

std::string setup_source(int indexed_rules)
{
    std::ostringstream source;
    source << "(() => { const style = document.createElement('style'); "
              "style.textContent = `"
              ":root { --probe-size: 19px; }\n"
              "@media (max-width:600px) { :root { --probe-size: 17px; } }\n"
              "@media (min-width:601px) { :root { --probe-size: 23px; } }\n"
              ".probe-class-name-that-exceeds-small-string-capacity { "
              "width:var(--probe-size); height:1px; }\n";
    for (auto index = 0; index < indexed_rules; ++index) {
        source << ".indexed-rule-" << index << " { padding-left:"
               << (index % 13) << "px; }\n";
    }
    source << "`; document.head.appendChild(style); "
              "const probe = document.createElement('div'); "
              "probe.className = 'probe-class-name-that-exceeds-small-string-capacity'; "
              "document.body.appendChild(probe); "
              "globalThis.__mediaRefreshProbe = probe; return 1; })()";
    return source.str();
}

sample_result run_once(int iterations, int indexed_rules)
{
    auto* engine = webscene_engine_create(0U);
    if (engine == nullptr) throw std::runtime_error("engine creation failed");
    try {
        enqueue_resize(engine, 1024.0, 1U);
        static_cast<void>(evaluate_number(engine, setup_source(indexed_rules)));
        webscene_media_refresh_benchmark_reset_counters();
        const auto started = std::chrono::steady_clock::now();
        uint64_t checksum = 0U;
        for (auto iteration = 0; iteration < iterations; ++iteration) {
            const auto narrow = iteration % 2 == 0;
            enqueue_resize(engine, narrow ? 320.0 : 1024.0,
                static_cast<uint64_t>(iteration) + 2U);
            const auto value = evaluate_number(engine, R"JS((() => {
              const width = Math.round(__mediaRefreshProbe.getBoundingClientRect().width);
              const narrow = matchMedia('(max-width:600px)').matches;
              return width * 2 + (narrow ? 1 : 0);
            })())JS");
            const auto expected = narrow ? 35.0 : 46.0;
            if (value != expected) {
                throw std::runtime_error("media refresh produced the wrong computed style");
            }
            checksum += static_cast<uint64_t>(std::llround(value));
        }
        const sample_result result{
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count(),
            webscene_media_refresh_benchmark_index_rule_calls(),
            webscene_media_refresh_benchmark_root_variable_refreshes(),
            webscene_media_refresh_benchmark_class_lookups(),
            webscene_media_refresh_benchmark_owned_class_lookup_keys(),
            webscene_media_refresh_benchmark_owned_class_lookup_bytes(),
            checksum};
        webscene_engine_destroy(engine);
        return result;
    } catch (...) {
        webscene_engine_destroy(engine);
        throw;
    }
}

double percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(
        fraction * static_cast<double>(values.size() - 1U))];
}

} // namespace

int main(int argc, char** argv)
{
    const auto iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 100;
    const auto samples = argc > 2 ? std::max(1, std::atoi(argv[2])) : 10;
    const auto warmups = argc > 3 ? std::max(0, std::atoi(argv[3])) : 2;
    const auto indexed_rules = argc > 4 ? std::max(1, std::atoi(argv[4])) : 256;
    if (webscene_engine_prewarm() == 0U) {
        std::cerr << "V8 prewarm failed\n";
        return 1;
    }
    try {
        for (auto index = 0; index < warmups; ++index) {
            static_cast<void>(run_once(iterations, indexed_rules));
        }
        std::vector<double> timings;
        timings.reserve(static_cast<size_t>(samples));
        sample_result exact;
        for (auto index = 0; index < samples; ++index) {
            const auto current = run_once(iterations, indexed_rules);
            if (index != 0
                && (current.index_rule_calls != exact.index_rule_calls
                    || current.root_variable_refreshes != exact.root_variable_refreshes
                    || current.class_lookups != exact.class_lookups
                    || current.owned_class_lookup_keys != exact.owned_class_lookup_keys
                    || current.owned_class_lookup_bytes != exact.owned_class_lookup_bytes
                    || current.checksum != exact.checksum)) {
                throw std::runtime_error("exact result was not deterministic");
            }
            exact = current;
            timings.push_back(current.elapsed_ms);
        }
        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0)
            / static_cast<double>(timings.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "{\"iterations\":" << iterations
                  << ",\"samples\":" << samples
                  << ",\"indexedFixtureRules\":" << indexed_rules
                  << ",\"indexRuleCalls\":" << exact.index_rule_calls
                  << ",\"indexRuleCallsPerRefresh\":"
                  << static_cast<double>(exact.index_rule_calls) / iterations
                  << ",\"rootVariableRefreshes\":" << exact.root_variable_refreshes
                  << ",\"classLookups\":" << exact.class_lookups
                  << ",\"ownedClassLookupKeys\":" << exact.owned_class_lookup_keys
                  << ",\"ownedClassLookupBytes\":" << exact.owned_class_lookup_bytes
                  << ",\"checksum\":" << exact.checksum
                  << ",\"meanMs\":" << mean
                  << ",\"p50Ms\":" << percentile(timings, 0.50)
                  << ",\"p95Ms\":" << percentile(timings, 0.95)
                  << "}\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

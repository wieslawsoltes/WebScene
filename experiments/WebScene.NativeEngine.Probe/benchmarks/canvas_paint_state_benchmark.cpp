#include "webscene_native_engine.h"

#include <algorithm>
#include <chrono>
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
    uint64_t operation_id{0};
};

struct sample_result final {
    double elapsed_ms{0};
    uint64_t string_property_probes{0};
    uint64_t utf8_conversions{0};
    uint64_t stack_comparisons{0};
    uint64_t cached_value_hits{0};
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

std::string fixture_source(int iterations)
{
    std::ostringstream source;
    source << R"JS((() => {
  const canvas = document.createElement('canvas');
  document.body.appendChild(canvas);
  const context = canvas.getContext('2d');
  context.font = '600 14px Inter, -apple-system, BlinkMacSystemFont, Segoe UI, Helvetica Neue, Arial, Noto Sans, Liberation Sans, Ubuntu, Cantarell, Fira Sans, Droid Sans, sans-serif';
  context.fillStyle = '#123456';
  context.textAlign = 'center';
  context.textBaseline = 'middle';
  context.shadowColor = 'rgba(12, 34, 56, 0.75)';
  for (let iteration = 0; iteration < )JS" << iterations << R"JS(; ++iteration) {
    context.fillText('unchanged', 12, 18);
  }
  return )JS" << iterations << R"JS(;
})())JS";
    return source.str();
}

sample_result run_once(const std::string& source)
{
    webscene_canvas_paint_state_benchmark_reset_counters();
    auto* engine = webscene_engine_create(0U);
    if (engine == nullptr) throw std::runtime_error("engine creation failed");
    const auto started = std::chrono::steady_clock::now();
    const webscene_interop_evaluate_request_v3 request{
        sizeof(webscene_interop_evaluate_request_v3),
        3U,
        source.data(),
        source.size(),
        "canvas-paint-state-benchmark.js",
        sizeof("canvas-paint-state-benchmark.js") - 1U,
        0U,
        0U};
    completion completion_result;
    const auto operation_id = webscene_engine_begin_evaluate_v3(
        engine, &request, completed, &completion_result);
    if (operation_id == 0U) {
        webscene_engine_destroy(engine);
        throw std::runtime_error("evaluation submission failed");
    }
    {
        std::unique_lock lock(completion_result.mutex);
        if (!completion_result.signal.wait_for(lock, std::chrono::seconds(30), [&] {
                return completion_result.operation_id != 0U;
            })) {
            webscene_engine_destroy(engine);
            throw std::runtime_error("evaluation timed out");
        }
    }
    const auto* view = webscene_engine_take_invoke_result_v3(engine, operation_id);
    if (view == nullptr || view->status != WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3) {
        webscene_engine_destroy(engine);
        throw std::runtime_error("evaluation failed");
    }
    const auto lease_id = view->lease_id;
    webscene_interop_result_release_v3(view, lease_id);
    const sample_result result{
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count(),
        webscene_canvas_paint_state_benchmark_string_property_probes(),
        webscene_canvas_paint_state_benchmark_utf8_conversions(),
        webscene_canvas_paint_state_benchmark_stack_comparisons(),
        webscene_canvas_paint_state_benchmark_cached_value_hits()};
    webscene_engine_destroy(engine);
    return result;
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
    const auto iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 2000;
    const auto samples = argc > 2 ? std::max(1, std::atoi(argv[2])) : 10;
    const auto warmups = argc > 3 ? std::max(0, std::atoi(argv[3])) : 2;
    if (webscene_engine_prewarm() == 0U) {
        std::cerr << "V8 prewarm failed\n";
        return 1;
    }
    try {
        const auto source = fixture_source(iterations);
        for (auto index = 0; index < warmups; ++index) run_once(source);
        std::vector<double> timings;
        timings.reserve(static_cast<size_t>(samples));
        sample_result exact{};
        for (auto index = 0; index < samples; ++index) {
            const auto result = run_once(source);
            if (index != 0
                && (result.string_property_probes != exact.string_property_probes
                    || result.utf8_conversions != exact.utf8_conversions
                    || result.stack_comparisons != exact.stack_comparisons
                    || result.cached_value_hits != exact.cached_value_hits)) {
                throw std::runtime_error("paint-state counters were not deterministic");
            }
            exact = result;
            timings.push_back(result.elapsed_ms);
        }
        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0)
            / static_cast<double>(timings.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "{\"iterations\":" << iterations
                  << ",\"samples\":" << samples
                  << ",\"stringPropertyProbes\":" << exact.string_property_probes
                  << ",\"utf8Conversions\":" << exact.utf8_conversions
                  << ",\"stackComparisons\":" << exact.stack_comparisons
                  << ",\"cachedValueHits\":" << exact.cached_value_hits
                  << ",\"meanMs\":" << mean
                  << ",\"p50Ms\":" << percentile(timings, 0.50)
                  << ",\"p95Ms\":" << percentile(timings, 0.95)
                  << "}\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

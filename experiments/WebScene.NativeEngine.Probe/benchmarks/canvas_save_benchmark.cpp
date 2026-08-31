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
    uint64_t operation_id{0};
};

struct sample_result final {
    double elapsed_ms{0};
    uint64_t property_reads{0};
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
  const saved = {
    fillStyle: '#123456',
    strokeStyle: '#abcdef',
    globalCompositeOperation: 'source-over',
    lineCap: 'round',
    lineJoin: 'bevel',
    font: '600 14px Inter, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif',
    textAlign: 'center',
    textBaseline: 'middle',
    imageSmoothingQuality: 'high',
    shadowColor: 'rgba(12, 34, 56, 0.75)',
    lineWidth: 2.5,
    globalAlpha: 0.75,
    miterLimit: 7,
    lineDashOffset: 1.5,
    shadowBlur: 3,
    shadowOffsetX: 4,
    shadowOffsetY: 5,
    imageSmoothingEnabled: false
  };
  const properties = Object.keys(saved);
  let reads = 0;
  for (const property of properties) {
    let value = saved[property];
    Object.defineProperty(context, property, {
      configurable: true,
      enumerable: true,
      get() { ++reads; return value; },
      set(next) { value = next; }
    });
  }
  context.setLineDash([2, 3]);
  context.setTransform(1, 0, 0, 1, 8, 13);
  let saveReads = 0;
  for (let iteration = 0; iteration < )JS" << iterations << R"JS(; ++iteration) {
    reads = 0;
    context.save();
    saveReads += reads;
    context.fillStyle = '#fedcba';
    context.strokeStyle = '#654321';
    context.font = '11px monospace';
    context.lineWidth = 9;
    context.globalAlpha = 0.25;
    context.imageSmoothingEnabled = true;
    context.setLineDash([7, 11]);
    context.translate(17, 19);
    context.restore();
    for (const property of properties) {
      if (!Object.is(context[property], saved[property])) {
        throw new Error(`restore mismatch: ${property}`);
      }
    }
    const dash = context.getLineDash();
    if (dash.length !== 2 || dash[0] !== 2 || dash[1] !== 3) {
      throw new Error('restore mismatch: line dash');
    }
  }
  return saveReads;
})())JS";
    return source.str();
}

sample_result run_once(const std::string& source)
{
    auto* engine = webscene_engine_create(0U);
    if (engine == nullptr) throw std::runtime_error("engine creation failed");
    const auto started = std::chrono::steady_clock::now();
    const webscene_interop_evaluate_request_v3 request{
        sizeof(webscene_interop_evaluate_request_v3),
        3U,
        source.data(),
        source.size(),
        "canvas-save-benchmark.js",
        sizeof("canvas-save-benchmark.js") - 1U,
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
    if (view == nullptr
        || view->status != WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3
        || view->values == nullptr
        || view->root_value_index >= view->value_count
        || view->values[view->root_value_index].kind != WEBSCENE_INTEROP_VALUE_NUMBER_V3) {
        webscene_engine_destroy(engine);
        throw std::runtime_error("evaluation did not return a number");
    }
    const auto value = std::bit_cast<double>(
        view->values[view->root_value_index].payload);
    const auto lease_id = view->lease_id;
    webscene_interop_result_release_v3(view, lease_id);
    webscene_engine_destroy(engine);
    return {
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count(),
        static_cast<uint64_t>(std::llround(value))};
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
    const auto iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 1000;
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
        uint64_t exact_reads = 0U;
        for (auto index = 0; index < samples; ++index) {
            const auto result = run_once(source);
            if (index != 0 && result.property_reads != exact_reads) {
                throw std::runtime_error("property-read result was not deterministic");
            }
            exact_reads = result.property_reads;
            timings.push_back(result.elapsed_ms);
        }
        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0)
            / static_cast<double>(timings.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "{\"iterations\":" << iterations
                  << ",\"samples\":" << samples
                  << ",\"propertyReads\":" << exact_reads
                  << ",\"propertyReadsPerSave\":"
                  << static_cast<double>(exact_reads) / iterations
                  << ",\"meanMs\":" << mean
                  << ",\"p50Ms\":" << percentile(timings, 0.50)
                  << ",\"p95Ms\":" << percentile(timings, 0.95)
                  << "}\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

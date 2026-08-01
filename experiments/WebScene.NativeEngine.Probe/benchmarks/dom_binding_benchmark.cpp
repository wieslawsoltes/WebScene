#include "webscene_native_engine.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace {

struct completion final {
    std::mutex mutex;
    std::condition_variable signal;
    uint64_t operation_id{0};
};

void completed(void* user_data, uint64_t operation_id)
{
    auto& value = *static_cast<completion*>(user_data);
    {
        std::lock_guard lock(value.mutex);
        value.operation_id = operation_id;
    }
    value.signal.notify_one();
}

double percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(
        fraction * static_cast<double>(values.size() - 1U));
    return values[index];
}

double create_ready_destroy(std::string_view source, std::string_view name)
{
    const auto started = std::chrono::steady_clock::now();
    auto* engine = webscene_engine_create(0U);
    if (engine == nullptr) throw std::runtime_error("engine creation failed");

    webscene_interop_evaluate_request_v3 request{
        sizeof(webscene_interop_evaluate_request_v3),
        3U,
        source.data(),
        source.size(),
        name.data(),
        name.size(),
        0U,
        0U};
    completion result;
    const auto operation_id = webscene_engine_begin_evaluate_v3(
        engine, &request, completed, &result);
    if (operation_id == 0U) {
        webscene_engine_destroy(engine);
        throw std::runtime_error("evaluation submission failed");
    }
    {
        std::unique_lock lock(result.mutex);
        if (!result.signal.wait_for(lock, std::chrono::seconds(10), [&] {
                return result.operation_id != 0U;
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
    webscene_engine_destroy(engine);
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
}

} // namespace

int main(int argc, char** argv)
{
    const auto samples = argc > 1 ? std::max(1, std::atoi(argv[1])) : 30;
    const auto warmups = argc > 2 ? std::max(0, std::atoi(argv[2])) : 5;
    const auto selector_workload = argc > 3
        && std::string_view(argv[3]) == "selectors";
    constexpr std::string_view lifecycle_source = "1";
    constexpr std::string_view selector_source = R"JS(
(() => {
  const root = document.createElement('section');
  root.className = 'selector-benchmark-root';
  for (let index = 0; index < 128; ++index) {
    const group = document.createElement('div');
    group.className = `group group-${index % 16}`;
    const item = document.createElement('span');
    item.className = `item item-${index % 32}`;
    item.setAttribute('data-index', String(index));
    group.appendChild(item);
    root.appendChild(group);
  }
  document.body.appendChild(root);
  const style = document.createElement('style');
  document.body.appendChild(style);
  let checksum = 0;
  for (let mutation = 0; mutation < 4; ++mutation) {
    let css = '';
    for (let index = 0; index < 2000; ++index) {
      css += `.selector-benchmark-root > .group-${index % 16} > `
        + `.item-${index % 32}[data-index="${index % 128}"]`
        + `:not(#missing-${mutation}-${index}) { --selector-${index}: ${mutation}; }\n`;
    }
    style.textContent = css;
    const probe = document.querySelector(
      `.selector-benchmark-root/**/>.group-${mutation}/**/>.item-${mutation}`);
    checksum += probe ? 1 : 0;
  }
  return checksum;
})()
)JS";
    const auto source = selector_workload ? selector_source : lifecycle_source;
    const auto name = selector_workload
        ? std::string_view("selector-runtime-benchmark.js")
        : std::string_view("dom-binding-benchmark.js");
    if (webscene_engine_prewarm() == 0U) {
        std::cerr << "V8 prewarm failed\n";
        return 1;
    }
    try {
        for (auto index = 0; index < warmups; ++index) {
            create_ready_destroy(source, name);
        }
        std::vector<double> timings;
        timings.reserve(static_cast<size_t>(samples));
        for (auto index = 0; index < samples; ++index) {
            timings.push_back(create_ready_destroy(source, name));
        }
        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0)
            / static_cast<double>(timings.size());
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        std::cout << std::fixed << std::setprecision(3)
                  << "samples=" << samples
                  << " warmups=" << warmups
                  << " mode=" << (selector_workload ? "selectors" : "lifecycle")
                  << " mean_ms=" << mean
                  << " p50_ms=" << percentile(timings, 0.50)
                  << " p95_ms=" << percentile(timings, 0.95)
                  << " min_ms=" << *std::min_element(timings.begin(), timings.end())
                  << " max_ms=" << *std::max_element(timings.begin(), timings.end())
                  << " peak_rss_bytes=" << usage.ru_maxrss
                  << '\n';
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

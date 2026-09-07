#pragma once
#include "webgpu_feature_names.h"
#include <string>
#include <utility>
namespace webscene::graphics {
struct webgpu_device_descriptor {
    std::string label,queue_label;
    std::vector<wgpu::FeatureName> required_features;
    // DOMString keys preserve UTF-16, including unpaired surrogates. Unknown
    // names and undefined values must reach subsequent WebGPU validation intact.
    std::vector<std::pair<std::u16string,std::optional<uint64_t>>> required_limits;
};
} // namespace webscene::graphics

#pragma once
#include "webgpu_device_descriptor.h"
#include "webgpu_required_limits.h"
#include <algorithm>
#include <memory>
namespace webscene::graphics {
enum class webgpu_device_request_error { none,unsupported_feature,operation_error };
// Owns all storage borrowed by the Dawn descriptor. Non-movable because the
// optional compatibility chain points into this allocation. Keep alive through
// RequestDevice; Dawn copies descriptor storage before that call returns.
class webgpu_prepared_device_descriptor {
    webgpu_device_descriptor requested_;
    wgpu::Limits limits_{};
    wgpu::CompatibilityModeLimits compatibility_{};
    webgpu_prepared_device_descriptor()=default;
public:
    webgpu_prepared_device_descriptor(const webgpu_prepared_device_descriptor&)=delete;
    webgpu_prepared_device_descriptor& operator=(const webgpu_prepared_device_descriptor&)=delete;
    wgpu::DeviceDescriptor native() const & {
        wgpu::DeviceDescriptor descriptor{};
        descriptor.label=wgpu::StringView(requested_.label.data(),requested_.label.size());
        descriptor.defaultQueue.label=wgpu::StringView(requested_.queue_label.data(),requested_.queue_label.size());
        descriptor.requiredFeatureCount=requested_.required_features.size();
        descriptor.requiredFeatures=requested_.required_features.data();
        descriptor.requiredLimits=&limits_;
        return descriptor;
    }
    wgpu::DeviceDescriptor native() const &&=delete;
    static std::unique_ptr<webgpu_prepared_device_descriptor> prepare(const webgpu_device_descriptor& requested,
        const wgpu::Adapter& adapter,bool consumed,webgpu_device_request_error& error) {
        error=webgpu_device_request_error::operation_error;
        if (!adapter) return {};
        // Feature failures have TypeError precedence over consumed/limit errors.
        for (auto feature:requested.required_features) {
            if (!webgpu_feature_to_name(feature) || !adapter.HasFeature(feature)) {
                error=webgpu_device_request_error::unsupported_feature; return {};
            }
        }
        if (consumed) return {};
        bool need_compatibility=false;
        for (const auto& [name,value]:requested.required_limits) {
            const auto* limit=webgpu_limit_from_name(name);
            if (value && limit && std::holds_alternative<uint32_t wgpu::CompatibilityModeLimits::*>(limit->member)) need_compatibility=true;
        }
        wgpu::Limits supported{};
        wgpu::CompatibilityModeLimits supported_compatibility{};
        if (need_compatibility) supported.nextInChain=&supported_compatibility;
        if (adapter.GetLimits(&supported)!=wgpu::Status::Success) return {};
        auto result=std::unique_ptr<webgpu_prepared_device_descriptor>(new webgpu_prepared_device_descriptor());
        if (!prepare_webgpu_required_limits(requested.required_limits,supported,supported_compatibility,result->limits_,result->compatibility_)) return {};
        result->requested_=requested;
        // WebGPU treats requiredFeatures as a set. Avoid passing duplicate
        // native features while preserving the first occurrence's order.
        auto& features=result->requested_.required_features;
        for (size_t i=0;i<features.size();++i)
            features.erase(std::remove(features.begin()+i+1,features.end(),features[i]),features.end());
        if (need_compatibility) result->limits_.nextInChain=&result->compatibility_;
        error=webgpu_device_request_error::none; return result;
    }
};
} // namespace webscene::graphics

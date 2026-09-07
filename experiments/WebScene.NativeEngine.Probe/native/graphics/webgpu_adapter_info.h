#pragma once
#include <webgpu/webgpu_cpp.h>
#include <string>
#include <string_view>
#include <stdexcept>
namespace webscene::graphics {
struct webgpu_adapter_info {
    std::string vendor,architecture,device,description;
    uint32_t subgroup_min_size=4,subgroup_max_size=128;
    bool is_fallback_adapter=false;
};
inline std::string webgpu_info_string(wgpu::StringView value) {
    if(!value.data)return {};
    return value.length==wgpu::kStrlen?std::string(value.data):std::string(value.data,value.length);
}
inline std::string webgpu_info_identifier(wgpu::StringView value) {
    auto text=webgpu_info_string(value);
    bool segment=false;
    for(char c:text) {
        if((c>='a'&&c<='z')||(c>='0'&&c<='9'))segment=true;
        else if(c=='-'&&segment)segment=false;
        else return {};
    }
    return segment?text:std::string{};
}
// Fallback state is supplied by discovery policy. CPU type alone does not match
// Dawn's fallback classification (the pinned Vulkan backend identifies SwiftShader).
inline webgpu_adapter_info read_webgpu_adapter_info(const wgpu::Adapter& adapter,bool fallback) {
    if(!adapter)throw std::invalid_argument("Adapter information requires a native adapter");
    wgpu::AdapterInfo native{};
    if(adapter.GetInfo(&native)!=wgpu::Status::Success)throw std::runtime_error("Native adapter information unavailable");
    webgpu_adapter_info result;
    result.vendor=webgpu_info_identifier(native.vendor);
    result.architecture=webgpu_info_identifier(native.architecture);
    result.device=webgpu_info_identifier(native.device);
    result.description=webgpu_info_string(native.description);
    result.is_fallback_adapter=fallback;
    if(adapter.HasFeature(wgpu::FeatureName::Subgroups)) {
        if(!native.subgroupMinSize || native.subgroupMaxSize<native.subgroupMinSize)throw std::runtime_error("Native subgroup information unavailable");
        result.subgroup_min_size=native.subgroupMinSize;result.subgroup_max_size=native.subgroupMaxSize;
    }
    return result;
}
} // namespace webscene::graphics

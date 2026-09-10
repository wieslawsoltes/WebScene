#pragma once
#include "webgpu_limit_names.h"
#include <optional>
#include <span>
#include <string>
#include <utility>
namespace webscene::graphics {
using webgpu_required_limit=std::pair<std::u16string,std::optional<uint64_t>>;
// A false result maps to requestDevice's OperationError. Undefined entries are
// ignored even when unknown. Supported values must come from the same adapter.
// Output is unchained: the caller attaches compatibility output when passing it
// to Dawn, keeping the two structures alive for the duration of RequestDevice.
inline bool prepare_webgpu_required_limits(std::span<const webgpu_required_limit> requested,
    const wgpu::Limits& supported,const wgpu::CompatibilityModeLimits& supported_compatibility,
    wgpu::Limits& output,wgpu::CompatibilityModeLimits& output_compatibility) {
    wgpu::Limits limits{};
    wgpu::CompatibilityModeLimits compatibility{};
    for (const auto& [name,value]:requested) {
        if (!value) continue;
        const auto* mapping=webgpu_limit_from_name(name);
        if (!mapping) return false;
        const auto available=mapping->read(supported,supported_compatibility);
        const bool wide=std::holds_alternative<uint64_t wgpu::Limits::*>(mapping->member);
        if (available==(wide?UINT64_MAX:UINT32_MAX)) return false;
        const bool alignment=name==u"minUniformBufferOffsetAlignment" || name==u"minStorageBufferOffsetAlignment";
        if (alignment) {
            if (!*value || *value>=uint64_t{1}<<32 || (*value&(*value-1)) || *value<available) return false;
        } else if (*value>available) return false;
        if (!mapping->write(limits,compatibility,*value)) return false;
    }
    output=limits; output_compatibility=compatibility; return true;
}
} // namespace webscene::graphics

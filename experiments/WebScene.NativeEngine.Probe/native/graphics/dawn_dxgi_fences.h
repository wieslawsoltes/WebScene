#pragma once
#include "nt_handle.h"
#include <span>
#include <system_error>
#include <vector>
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
enum class dxgi_fence_status {
    success,invalid_argument,unsupported_fence,handle_failure,out_of_memory
};
// ExportInfo returns a borrowed handle owned by the Dawn fence. Duplicate every
// handle before releasing EndAccessState; never close the borrowed export.
// Ops injection tests ownership/rollback without pretending to test a Windows GPU.
template<class Ops> struct dxgi_fence_wait {
    unique_nt_handle<Ops> handle;
    uint64_t value{};
};
template<class Ops, class Export> dxgi_fence_status duplicate_dxgi_fences(
    std::span<const wgpu::SharedFence> fences,std::span<const uint64_t> values,
    std::vector<dxgi_fence_wait<Ops>>& result,Export export_handle) {
    result.clear();
    if (fences.size()!=values.size()) return dxgi_fence_status::invalid_argument;
    try {
        std::vector<dxgi_fence_wait<Ops>> candidate;
        candidate.reserve(fences.size());
        for (size_t i=0;i<fences.size();++i) {
            // Export validates the fence and its native type before touching the
            // handle. Keep all duplicates private until the entire set succeeds.
            typename Ops::handle_type borrowed=Ops::empty();
            if (!export_handle(fences[i],borrowed) || !Ops::valid(borrowed))
                return dxgi_fence_status::unsupported_fence;
            candidate.push_back({unique_nt_handle<Ops>::duplicate(borrowed),values[i]});
        }
        result=std::move(candidate);
        return dxgi_fence_status::success;
    } catch (const std::bad_alloc&) { return dxgi_fence_status::out_of_memory; }
      catch (const std::system_error&) { return dxgi_fence_status::handle_failure; }
}
#if defined(_WIN32)
using owned_dxgi_fence_wait=dxgi_fence_wait<win32_nt_handle_ops>;
inline dxgi_fence_status export_dxgi_fences(const wgpu::SharedTextureMemoryEndAccessState& state,
    std::vector<owned_dxgi_fence_wait>& result) {
    if ((state.fenceCount && !state.fences) || (state.signaledValueCount && !state.signaledValues)) {
        result.clear(); return dxgi_fence_status::invalid_argument;
    }
    return duplicate_dxgi_fences<win32_nt_handle_ops>(
        {state.fences,state.fenceCount},{state.signaledValues,state.signaledValueCount},result,
        [](const wgpu::SharedFence& fence,HANDLE& handle) {
            if (!fence) return false;
            wgpu::SharedFenceExportInfo info{};
            fence.ExportInfo(&info);
            if (info.type!=wgpu::SharedFenceType::DXGISharedHandle) return false;
            wgpu::SharedFenceDXGISharedHandleExportInfo dxgi{};
            info.nextInChain=&dxgi;
            fence.ExportInfo(&info);
            handle=dxgi.handle;
            return info.type==wgpu::SharedFenceType::DXGISharedHandle;
        });
}
#endif
} // namespace webscene::graphics
